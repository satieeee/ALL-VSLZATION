// ============================================================================
// Filigree 渲染器（从 main.cpp 搬出来的，代码逐字未改，只接了三处缝）
//
// 【三处缝】原来这些量直接吃 main.cpp 的全局变量，现在改成显式参数 ——
//   quadVao   <- g_vao（main.cpp 的 NDC 全屏四边形）
//   beatFlash <- g_af.beatFlash（鼓点闪光）
//   beatCount <- g_beatCount（剪纸换批的种子）
// 其余全部原样：本文件自己的状态都是 static，外界看不到，只暴露 TNRender.h 里那两个入口。
//
// 【为什么值得搬】它原来是 main.cpp 里最长的一段效果代码（600 行），而且带着程序句柄、
// FBO 三件套、剪纸池、相机矩阵一堆全局量 —— 改 Filigree 得先在那个 6000 行文件里找它们。
// ============================================================================
#include "TNRender.h"      // 本模块的对外声明（内含 GLFuncs.h）
#include "TNShaders.h"     // kTVS / kVTFS 原文
#include "TNParams.h"      // TN_* 常量与调色盘（与离线台同一份）
#include "VTGeom.h"        // 隧道壁 / 细条几何
#include "TNChipGeom.h"    // 剪纸池 / 生成 / 漂移 / 逐帧顶点
#include "VTKnotGeom.h"    // 中轴纽结几何
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>          // _snprintf（着色器编译失败信息）

// ============================================================================
// Filigree —— 隧道 · 细长金属条 · 内壁剪纸流动
// 几何全在 VisualEffects/TN/VTGeom.h（纯数学，可离线验证），这里只管渲染。
//
// 【相机在隧道轴向】= 相机固定在原点朝 +z 看，隧道的中轴就是视轴 ——
// 与 Involute 同一套：不做视图矩阵，靠 gl_Position.w = 视深 做透视除法。
//
// 三段几何各一次（或多次）独立提交，用 uMat 切材质：
//   0 = 隧道壁（每帧 6 层，靠 CPU 侧的 fmod 环绕出"无限"）
//   1 = 金属细条（每帧 6 个 z 桶，按远近排序提交）
//   2 = 内壁剪纸片（CPU 每帧重建的顶点缓冲，一次提交）
// 每帧 draw = 背景1 + 壁6 + 条6 + 纸1 + 亮部1 + 模糊4 + 合成1 = 20
// ============================================================================

// ---- 参数（可见带/雾/棱脊与细条边辉光/调色盘）已移到 VisualEffects/TN/TNParams.h ----
// ---- 着色器 kTVS/kVTFS 已移到 VisualEffects/TN/TNShaders.h（台子直接 include 它）----
// 顶部 include 进来的就是，这里不再重复声明 ——「两份真相」是上一轮栽跟头的根源。

// 相机（固定在中轴原点；只做轻微的呼吸感偏移）
static float g_tnAnimT  = 0.0f;     // 按 speed 缩放的动画时间（细条摆动/高光带/剪纸寿命都用它）
static float g_tnPhase  = 0.0f;     // 沿 z 的整体推进（无限隧道）
static float g_tnHitT   = 999.0f;   // 距上次鼓点的秒数

// 各趟常量（TN_NEAR_Z/SPAN/FADE_*、棱脊与细条边辉光强度、调色盘 g_tn*）：见 TN/TNParams.h。

// ----------------------------------------------------------------------------
// Filigree 渲染器
// ----------------------------------------------------------------------------
struct TNRenderer { GLuint vao = 0, vbo = 0; GLsizei vcount = 0; };
struct TNFbo { GLuint fb = 0, tex = 0; };

static GLuint    g_tnProg = 0;
// 几何趟（壁 / 细条 / 剪纸）专用程序。理由见 TnGeomVSSource() 上面那段。
static GLuint    g_tnProgGeom = 0;
static TNRenderer g_tnWall, g_tnRibbon, g_tnKnot;
// 中轴纽结的辉光强度（CPU 每帧算：常驻底值 + 鼓点冲击）。见 kVTFS 的 uMat==3。
static float      g_tnKnotGlow = 7.0f;
static float      g_tnEdgeGlow = 5.0f;   // 隧道壁棱脊的炫彩辉光强度（见 uEdgeGlow；壁已删，这条现在没人读）
static float      g_tnRibGlow  = TN_RIB_GLOW_BASE;   // 金属细条边辉光的强度（见 uRibGlow）
static int       g_tnRibBucket[VT_RIB_BUCKETS + 1] = {0};
static TNFbo     g_tnScene, g_tnBlurA, g_tnBlurB;
static int       g_tnFboW = 0, g_tnFboH = 0;

// ---- 剪纸片 ----
// 几何部分（池子筛选 / 生成 / 漂移 / 逐帧顶点）已抽到 VisualEffects/TN/TNChipGeom.h ——
// 纯数学、不碰 GL，所以 _hittest/vt_render_test.cpp 能跑【同一份】代码。
// 【为什么非抽不可】第一版写在这里，结果它成了 Filigree 唯一没验证过的路径：
// 剪纸只在鼓点上生成，离线台读不到这里的静态全局量，够不着 —— 实机一放音频才走到。
// 这里只留 GL 侧的状态。
static std::vector<TNChip>  g_tnChips;
static std::vector<int>     g_tnChipPool;
static bool                 g_tnChipPoolReady = false;
static GLuint               g_tnChipVao = 0, g_tnChipVbo = 0;
static int                  g_tnChipVerts = 0, g_tnChipCap = 0;
static std::vector<float>   g_tnChipCpu;

// 手持镜头的 3x3 旋转，按行铺在 12 个 float 里（每行 4 个，第 4 个补 0）。
// 【初值是单位矩阵】—— 万一某帧没算（或 uniform 位置取不到），传下去的也必须是单位阵；
// 全零会把每个顶点压到原点，整个效果一片空。
static float g_tnCamR[12] = { 1.0f, 0.0f, 0.0f, 0.0f,
                              0.0f, 1.0f, 0.0f, 0.0f,
                              0.0f, 0.0f, 1.0f, 0.0f };
// 手持镜头的平移（视图空间，只横向；第 4 个分量只为凑 glUniform4fv —— 这个工程的
// GL 加载器没有 glUniform3fv）。初值必须是 0：非零的话所有几何会整体偏出去。
static float g_tnCamT[4] = { 0.0f, 0.0f, 0.0f, 0.0f };


// ---------------------------------------------------------------------------
// 几何趟专用顶点着色器：由 kTVS **原地派生**，不是第二份源码。
// ---------------------------------------------------------------------------
// 【为什么需要它】kTVS 同时背着两条互不相干的分支 —— 全屏趟的 `if (uPass != 1) {...return;}`
// 和几何趟的 `if (uMat == 1) {...}`。几何趟本来就只走 uPass==1，那个提前返回是【纯死代码】。
// 但实测（_hittest/vt_col_repro.cpp，2026-09-23）：这份 VS 编出来的程序，
// **只有进程内第一趟 glDrawArrays 是好的，之后每一趟几何写的 alpha 掉到 ~0.004（等于没画）**。
// RenderFiligree 的次序是 背景 → 壁×6 → 细条 → 剪纸，剪纸永远是最后一趟，所以一直看不见；
// 壁（第 2~7 趟）同样被拖坏，整幅只剩细条那点暗射线。
// 把两条分支拆到两个程序里（各留一条），三种绘制次序实测全部正常 —— 见该文件头注释。
//
// 【为什么派生而不是复制一份】复制会变成第二份真相：以后改 kTVS 得记得同步改这里。
// 这里只做一次文本手术，源只有 kTVS 一处。找不到锚点时返回空串，调用方会退回 g_tnProg。
static std::string TnGeomVSSource() {
    std::string s = kTVS;
    const char* a = "    if (uPass != 1) {";
    const char* b = "    vec3 p = aPos;";
    const size_t qa = s.find(a), qb = s.find(b);
    if (qa == std::string::npos || qb == std::string::npos || qb <= qa) return std::string();
    s.erase(qa, qb - qa);
    return s;
}

static bool CompileTNProg() {
    if (g_tnProg) return true;
    auto comp = [](GLenum type, const char* src, const char* lbl) -> GLuint {
        GLuint sh = glCreateShader(type);
        glShaderSource(sh, 1, &src, nullptr);
        glCompileShader(sh);
        GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[4096]; GLsizei n = 0;
            glGetShaderInfoLog(sh, sizeof log, &n, log);
            char msg[4600]; _snprintf(msg, sizeof msg, "%s 编译失败:\n%s", lbl, log);
            MessageBoxA(nullptr, msg, "Filigree Shader Error", MB_OK);
            glDeleteShader(sh);
            return 0;
        }
        return sh;
    };
    GLuint vs = comp(GL_VERTEX_SHADER,   kTVS, "kTVS");
    GLuint fs = comp(GL_FRAGMENT_SHADER, kVTFS, "kVTFS");
    if (!vs || !fs) return false;
    GLuint p = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs); glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    glDeleteShader(vs); glDeleteShader(fs);
    if (!ok) {
        char log[4096]; GLsizei n = 0;
        glGetProgramInfoLog(p, sizeof log, &n, log);
        MessageBoxA(nullptr, log, "Filigree Link Error", MB_OK);
        glDeleteProgram(p);
        return false;
    }
    g_tnProg = p;

    // ---- 几何趟专用程序：同一份 kTVFS，VS 换成去掉 uPass 分支的那份 ----
    const std::string gvs = TnGeomVSSource();
    if (gvs.empty()) {
        // 没找到锚点（kTVS 被改过）—— 不静默降级成"看起来没事"，明确告诉人
        MessageBoxA(nullptr,
                    "TnGeomVSSource() 找不到 kTVS 里的 uPass 提前返回块。\n"
                    "几何趟会退回共用程序，剪纸/壁可能仍然画不出来。",
                    "Filigree: 几何趟 VS 派生失败", MB_OK);
    } else {
        GLuint gvsObj = comp(GL_VERTEX_SHADER, gvs.c_str(), "kTVSGeom");
        // 【重新编一份 FS】不共用上面的 fs —— 那个已经被 glDeleteShader 标记过，
        // 虽然"仍被 g_tnProg 引用"时规范保证它不会真销毁，但那种正确性太依赖细节。
        // 多编一次只在启动时发生，换来的是这段代码不用靠脑内推演 spec。
        GLuint gfsObj = comp(GL_FRAGMENT_SHADER, kVTFS, "kVTFS(geom)");
        if (gvsObj && gfsObj) {
            GLuint gp = glCreateProgram();
            glAttachShader(gp, gvsObj);
            glAttachShader(gp, gfsObj);
            glLinkProgram(gp);
            glDeleteShader(gfsObj);
            GLint ok2 = 0; glGetProgramiv(gp, GL_LINK_STATUS, &ok2);
            glDeleteShader(gvsObj);
            if (ok2) { g_tnProgGeom = gp; }
            else {
                char log[4096]; GLsizei n = 0;
                glGetProgramInfoLog(gp, sizeof log, &n, log);
                MessageBoxA(nullptr, log, "Filigree Geom Link Error", MB_OK);
                glDeleteProgram(gp);
            }
        }
    }
    return true;
}

void ResetTNRenderer() {
    // 【g_tnProg 之前没在这里清零】—— 别的渲染器（C3 / PC / VX）重建上下文时都是
    // `prog = 0` 强制重编，只有这里漏了，会留着失效句柄。顺手一起清掉。
    g_tnProg = 0;
    g_tnProgGeom = 0;
    if (g_tnWall.vao)   { glDeleteVertexArrays(1, &g_tnWall.vao);   g_tnWall.vao = 0; }
    if (g_tnWall.vbo)   { glDeleteBuffers(1, &g_tnWall.vbo);        g_tnWall.vbo = 0; }
    if (g_tnRibbon.vao) { glDeleteVertexArrays(1, &g_tnRibbon.vao); g_tnRibbon.vao = 0; }
    if (g_tnRibbon.vbo) { glDeleteBuffers(1, &g_tnRibbon.vbo);      g_tnRibbon.vbo = 0; }
    if (g_tnChipVao)    { glDeleteVertexArrays(1, &g_tnChipVao);    g_tnChipVao = 0; }
    if (g_tnChipVbo)    { glDeleteBuffers(1, &g_tnChipVbo);         g_tnChipVbo = 0; }
    if (g_tnKnot.vao)   { glDeleteVertexArrays(1, &g_tnKnot.vao);   g_tnKnot.vao = 0; }
    if (g_tnKnot.vbo)   { glDeleteBuffers(1, &g_tnKnot.vbo);        g_tnKnot.vbo = 0; }
    if (g_tnScene.fb) { glDeleteFramebuffers(1, &g_tnScene.fb); glDeleteTextures(1, &g_tnScene.tex); g_tnScene.fb = g_tnScene.tex = 0; }
    if (g_tnBlurA.fb) { glDeleteFramebuffers(1, &g_tnBlurA.fb); glDeleteTextures(1, &g_tnBlurA.tex); g_tnBlurA.fb = g_tnBlurA.tex = 0; }
    if (g_tnBlurB.fb) { glDeleteFramebuffers(1, &g_tnBlurB.fb); glDeleteTextures(1, &g_tnBlurB.tex); g_tnBlurB.fb = g_tnBlurB.tex = 0; }
    g_tnFboW = g_tnFboH = 0;
    g_tnAnimT = 0.0f; g_tnPhase = 0.0f; g_tnHitT = 999.0f;
    g_tnKnotGlow = TN_KNOT_GLOW_BASE;
    g_tnRibGlow  = TN_RIB_GLOW_BASE;
    // 手持镜头回单位阵 + 平移归零 —— 别的渲染器重开窗口时也会复位自己的状态，这里不能漏
    const float I[12] = { 1,0,0,0, 0,1,0,0, 0,0,1,0 };
    for (int k = 0; k < 12; ++k) g_tnCamR[k] = I[k];
    for (int k = 0; k < 4; ++k) g_tnCamT[k] = 0.0f;
    g_tnWall.vcount = g_tnRibbon.vcount = 0;
    g_tnChipVerts = 0; g_tnChipCap = 0;
    g_tnChipCpu.clear();
    g_tnChips.clear();
    g_tnChipPoolReady = false;      // 池子依赖 kC3PolyV，是常量，但重开窗口后重建一次最省心
    g_tnChipPool.clear();
}

static bool EnsureTNRenderer() {
    if (g_tnRibbon.vao) return true;
    if (!CompileTNProg()) return false;

    // ---- 隧道壁：2026-09-23 按用户要求【已删】 ----
    // Filigree 现在只有 金属细条 + 内壁剪纸 两层。
    // 想恢复的话：把注释掉的这段放回来、RenderFiligree 里那个壁的绘制循环也放回来，
    // 并把下面两处守卫从 g_tnRibbon.vao 改回 (g_tnWall.vao && g_tnRibbon.vao)。
    // 着色器一行没动 —— kVTFS 的 uMat==0（壁）分支和 uWallA/B 等 uniform 都还在，
    // 只是再没有绘制调用会走到那里。VTGeom.h 也原样保留（VTSurfacePoint 还在给剪纸定位，
    // 离线台 mode 1 还在用它画壁）。
    // {
    //     ... VTBuildMesh / VAO / VBO ...
    // }

    // ---- 金属细条 ----
    {
        std::vector<float> verts;
        VTRibbonBuild(verts, g_tnRibBucket);
        glGenVertexArrays(1, &g_tnRibbon.vao);
        glBindVertexArray(g_tnRibbon.vao);
        glGenBuffers(1, &g_tnRibbon.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, g_tnRibbon.vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(float)), verts.data(), GL_STATIC_DRAW);
        const GLsizei st = VTRibbonStride() * (GLsizei)sizeof(float);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, st, (const void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, st, (const void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, st, (const void*)(5 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, st, (const void*)(8 * sizeof(float)));
        glEnableVertexAttribArray(3);      // loc3 = 每根的相位
        // loc6 = 本根原本的长度（世界单位）。顶点着色器按频谱缩放针的长短要用它 ——
        // 见 VTGeom.h 的 VTRibbonStride 与 kTVS 里那段。忘了启用的话它会读到默认 0，
        // 表现是"所有针的长度都按 0 算"（缩放完全失效，但不报任何错）。
        glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, st, (const void*)(9 * sizeof(float)));
        glEnableVertexAttribArray(6);
        // loc7 = 这根条的中轴（脊线）坐标 —— "几何由参数决定"的把手，见 VTGeom.h
        glVertexAttribPointer(7, 3, GL_FLOAT, GL_FALSE, st, (const void*)(10 * sizeof(float)));
        glEnableVertexAttribArray(7);
        g_tnRibbon.vcount = (GLsizei)(verts.size() / VTRibbonStride());
        glBindVertexArray(0);
    }
    // ---- 剪纸片：VAO 先建好，顶点每帧重传 ----
    {
        glGenVertexArrays(1, &g_tnChipVao);
        glBindVertexArray(g_tnChipVao);
        glGenBuffers(1, &g_tnChipVbo);
        glBindBuffer(GL_ARRAY_BUFFER, g_tnChipVbo);
        const GLsizei st = (GLsizei)TN_CHIP_STRIDE * (GLsizei)sizeof(float);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, st, (const void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, st, (const void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, st, (const void*)(5 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, st, (const void*)(8 * sizeof(float)));
        glEnableVertexAttribArray(4);      // loc4 = 面明暗
        glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, st, (const void*)(9 * sizeof(float)));
        glEnableVertexAttribArray(5);      // loc5 = 透明度
        glBindVertexArray(0);
    }
    // ---- 中轴纽结：静态几何，建一次 ----
    // 布局与隧道壁相同（8 float：pos3 + uv2 + nrm3），所以只启用 loc 0/1/2；
    // loc3/4/5 保持关闭，着色器读通用默认值 —— uMat==3 那支本来也不用它们。
    {
        std::vector<float> kv;
        VTKnotBuild(kv);
        glGenVertexArrays(1, &g_tnKnot.vao);
        glBindVertexArray(g_tnKnot.vao);
        glGenBuffers(1, &g_tnKnot.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, g_tnKnot.vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(kv.size() * sizeof(float)), kv.data(), GL_STATIC_DRAW);
        const GLsizei ks = VTKnotStride() * (GLsizei)sizeof(float);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, ks, (const void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, ks, (const void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, ks, (const void*)(5 * sizeof(float)));
        glEnableVertexAttribArray(2);
        g_tnKnot.vcount = (GLsizei)(kv.size() / VTKnotStride());
        glBindVertexArray(0);
    }
    return true;
}

static void TNRebuildFbos(int w, int h) {
    if (w == g_tnFboW && h == g_tnFboH && g_tnScene.fb) return;
    if (g_tnScene.fb) { glDeleteFramebuffers(1, &g_tnScene.fb); glDeleteTextures(1, &g_tnScene.tex); g_tnScene.fb = g_tnScene.tex = 0; }
    if (g_tnBlurA.fb) { glDeleteFramebuffers(1, &g_tnBlurA.fb); glDeleteTextures(1, &g_tnBlurA.tex); g_tnBlurA.fb = g_tnBlurA.tex = 0; }
    if (g_tnBlurB.fb) { glDeleteFramebuffers(1, &g_tnBlurB.fb); glDeleteTextures(1, &g_tnBlurB.tex); g_tnBlurB.fb = g_tnBlurB.tex = 0; }
    g_tnFboW = w; g_tnFboH = h;
    const int bw = w / 2 > 2 ? w / 2 : 2, bh = h / 2 > 2 ? h / 2 : 2;
    auto mk = [](TNFbo& f, int fw, int fh) {
        glGenTextures(1, &f.tex);
        glBindTexture(GL_TEXTURE_2D, f.tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        // 必须 RGBA16F：8 位主缓冲在 1.0 处截断，亮部提取就没有源头
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, fw, fh, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
        glGenFramebuffers(1, &f.fb);
        glBindFramebuffer(GL_FRAMEBUFFER, f.fb);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, f.tex, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
    };
    mk(g_tnScene, w, h); mk(g_tnBlurA, bw, bh); mk(g_tnBlurB, bw, bh);
}

// ============================================================================
/// 每下鼓点换一个随机色种子：由鼓点序号散列而来。
/// 【必须是"每下鼓点一个值"，不能逐帧随机】逐帧随机会变成雪花噪点 —— 那不是"颜色变化"，
/// 那是电视没信号。同一次鼓点内种子不变，所以颜色只在鼓点那一刻整体跳一次。
static float SeedFromBeat(unsigned beatCount) {
    unsigned h = beatCount * 2654435761u;
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
    return (float)(h & 0xFFFFFFu) / 16777215.0f;
}

void RenderFiligree(float t, float speed, float w, float h, float opacity, bool darkBg,
                    GLuint quadVao, const float* fft, float beatFlash, unsigned beatCount) {
    if (w < 2.0f || h < 2.0f) return;
    if (!EnsureTNRenderer()) return;
    const int wi = (int)w, hi = (int)h;
    TNRebuildFbos(wi, hi);
    if (!g_tnScene.fb || !g_tnRibbon.vao) return;
    const float beatSeed = SeedFromBeat(beatCount);

    const GLuint prog = g_tnProg;
    glUseProgram(prog);
    auto U = [prog](const char* n) { return glGetUniformLocation(prog, n); };

    // ---- 帧间差（同 F1）：切效果回来时相位不跳变 ----
    static float s_lastT = -1.0f;
    const bool tRestart = (s_lastT < 0.0f || t < s_lastT);
    float dt = tRestart ? 0.0f : (t - s_lastT);
    if (tRestart) g_tnAnimT = 0.0f;      // 播放链重开：动画时间跟着归零
    s_lastT = t;
    if (dt > 0.1f) dt = 0.1f;

    // ---- 动画时间：按 speed 缩放 ----
    // 【为什么需要它】这个 effect 里会动的东西（细条摆动与高光带、剪纸漂移与出生/消亡、
    // 背景星点闪烁）全都挂在时间上，而这里吃的 `t` 是 g_plTime —— 那是【真实秒】、
    // 不乘 speed（见 PLUpdate 的 `g_plTime += dt`）。于是用户拖 speed 滑块时画面纹丝不动。
    // 现在统一累计一份按 speed 缩放的动画时间，所有动画与剪纸的寿命判据都用它。
    // 【寿命也一起缩放】spawnT/killT/TN_CHIP_REVEAL 的判据必须和漂移同一个时基，
    // 否则"按真实秒出生、按缩放秒漂移"，速度一高纸片就会瞬间过期。
    g_tnAnimT += dt * speed;
    const float tA = g_tnAnimT;

    // ---- 鼓点：beatFlash 的【上升沿】（同 Involute / Function1）----
    static float s_prevFlash = 0.0f;
    bool hit = false;
    if (beatFlash > s_prevFlash + 1e-4f) { hit = true; g_tnHitT = 0.0f; }
    s_prevFlash = beatFlash;   // 写在 if 外面：解锁时不至于用积攒的差值误触发
    g_tnHitT += dt;

    // ---- 沿 z 推进（无限隧道）+ 鼓点冲击包络 ----
    // gs = 鼓点冲击包络：击中的瞬间为 1，在 TN_HIT_DUR 内按 TN_HIT_CURVE 衰减到 0。
    // 【提到块外来算】手持镜头的抖也吃同一个包络 —— 放在块里就得再算一遍，
    // 两遍迟早会因为改了其中一处而对不上。
    float gs = 0.0f;
    {
        float hk = 1.0f - g_tnHitT / TN_HIT_DUR;
        if (hk < 0.0f) hk = 0.0f;
        gs = std::pow(hk, TN_HIT_CURVE);
        g_tnPhase += dt * speed * TN_BASE_SPD * (1.0f + (TN_HIT_RATE - 1.0f) * gs);
    }

    // ---- 手持镜头：常驻慢漂移 + 鼓点推一把 ----
    // 两组【频率不互约】的正弦叠加：单频看着像机器匀速摆，两个拍频错开的频率叠起来才像手。
    // 幅度单位是弧度；roll 给最大 —— 隧道是管状的，绕视线转一下最像"手没端稳"，
    // yaw/pitch 太大会直接把消失点晃出画面。总量控制在几度以内。
    //
    // 【频率必须落在 1.5~5 rad/s】—— 我第一版写了 0.47~1.37（周期 6~13 秒），
    // 结果是"两秒钟内画面几乎不动"，慢到那个尺度就不再是"手"了。
    //
    // 【时基用 t（真实秒），不是 tA】—— 这是本渲染里唯一一个【不吃 speed】的动画，故意的：
    // 手是摄影师的，不是画面的一部分。回放速度调慢，隧道该慢下来，但端相机的人不会
    // 跟着变慢。所以这里和下面 gs 的衰减（也是真实时间）一致，全段都在真实时基上。
    // 对比：细条摆动、纸片漂移、星点闪烁都挂在 tA 上 —— 那些是画面内容，必须跟 speed。
    {
        const float tw = t;
        // 【2026-09-24 重排过一次振幅，别再按"三个同类数字"去理解它们】
        //   · roll（绕视轴）：把画面绕【窗口中心】转，对灭点的贡献【正好是 0】。
        //   · yaw/pitch：整幅平移 focal·tan(θ) —— 这是"汇聚点扫过画面"的【唯一】来源。
        //     老版 0.030/0.026（1.7°/1.5°）实测只把整幅平移 20px（960 宽里的 2%），
        //     所以汇聚点看着钉死在窗口中心；0.085/0.070（约 7°/6°）能扫 ±130px。
        //   · 平移：不移动灭点（灭点只由朝向决定），给的是视差 —— 近处的条相对远处横扫，
        //     隧道"摆"起来。这是原地转镜头做不到的那点立体感。
        // 所以现在 yaw/pitch 是主力、roll 退成调味、平移补上视差。
        // 【2026-09-24 第二次调：用户说"振幅太大了，小一点"】上一版 yaw 顶到 7.4°、
        // 平移顶到 0.64（隧道半径才 1.30 —— 相当于每下鼓点相机横向甩半个隧道）。
        // 现在整体收到上一版的 ~55%，仍然明显大于"汇聚点钉死"那版（20px）：
        // 实测整幅平移 ~56px（上一版 88px、最初版 20px）。
        float yaw   = 0.055f * std::sin(tw * 2.60f)        + 0.028f * std::sin(tw * 4.10f + 1.7f);
        float pitch = 0.045f * std::sin(tw * 2.25f + 2.4f) + 0.024f * std::sin(tw * 3.60f + 0.6f);
        float roll  = 0.028f * std::sin(tw * 1.55f + 0.9f) + 0.014f * std::sin(tw * 3.20f + 3.1f);
        float trX   = 0.100f * std::sin(tw * 1.90f + 0.4f) + 0.052f * std::sin(tw * 3.05f + 2.2f);
        float trY   = 0.085f * std::sin(tw * 2.15f + 1.1f) + 0.043f * std::sin(tw * 2.80f + 3.7f);
        // 鼓点冲击：高频短促，往当前姿态上再压一把，随 gs 衰减
        yaw   += gs * 0.040f * std::sin(tw * 18.0f);
        pitch += gs * 0.032f * std::cos(tw * 15.0f);
        roll  += gs * 0.038f * std::sin(tw * 22.0f);
        trX   += gs * 0.180f * std::sin(tw * 16.0f);
        trY   += gs * 0.150f * std::cos(tw * 13.0f);

        const float cy = std::cos(yaw),   sy = std::sin(yaw);
        const float cx = std::cos(pitch), sx = std::sin(pitch);
        const float cz = std::cos(roll),  sz = std::sin(roll);
        // R = Rz(roll) · Rx(pitch) · Ry(yaw) 展开后的三行
        g_tnCamR[0] =  cz*cy - sz*sx*sy;  g_tnCamR[1]  = -sz*cx;  g_tnCamR[2]  =  cz*sy + sz*sx*cy;  g_tnCamR[3]  = 0.0f;
        g_tnCamR[4] =  sz*cy + cz*sx*sy;  g_tnCamR[5]  =  cz*cx;  g_tnCamR[6]  =  sz*sy - cz*sx*cy;  g_tnCamR[7]  = 0.0f;
        g_tnCamR[8] = -cx*sy;             g_tnCamR[9]  =  sx;     g_tnCamR[10] =  cx*cy;             g_tnCamR[11] = 0.0f;
        // 平移：z 分量恒 0（往 +z 挪会把近端顶点推到相机后面 → d=max(z,0.05) 放大成畸形三角形）
        // z 分量 = 鼓点那一刻的【瞬间推进】：gs 命中瞬间为 1 → 推动量立刻到位（不缓动），
        // 之后随包络弹回来。和手持镜头的抖共用 gs，所以同一下鼓点是一起动。
        // （uCamT 是"相机在视图空间的位移"，p -= uCamT，所以 +z = 相机往隧道里走。）
        g_tnCamT[0] = trX;  g_tnCamT[1] = trY;  g_tnCamT[2] = gs * TN_HIT_PUSH;  g_tnCamT[3] = 0.0f;
    }

    // ---- 中轴纽结的辉光强度 ----
    // 常驻底值 + 鼓点冲击。它是【光源】而不是物体：输出的辐射值远超 1，
    // 靠场景 FBO 的 HDR 余量让泛光从消失点炸开一片彩色光晕。
    g_tnKnotGlow = TN_KNOT_GLOW_BASE + gs * TN_KNOT_GLOW_HIT;
    // 两种背景模式用两套值 —— 理由见 TN_EDGE_GLOW_LIGHT_* 上面那段（浅色模式整幅反相，
    // HDR 会反成黑洞）
    g_tnEdgeGlow = darkBg ? (TN_EDGE_GLOW_BASE + gs * TN_EDGE_GLOW_HIT)
                          : (TN_EDGE_GLOW_LIGHT_BASE + gs * TN_EDGE_GLOW_LIGHT_HIT);
    // 细条边辉光：同一套"常驻 + 鼓点冲击"，浅色模式是另一组值（理由见 TN_RIB_GLOW_LIGHT_*）
    g_tnRibGlow  = darkBg ? (TN_RIB_GLOW_BASE + gs * TN_RIB_GLOW_HIT)
                          : (TN_RIB_GLOW_LIGHT_BASE + gs * TN_RIB_GLOW_LIGHT_HIT);

    // ---- 剪纸：鼓点换批 + 漂移 + 重建顶点 ----
    // 时基全部换 tA / tA 缩放的 dt —— 见上面"动画时间"那段。
    if (hit) {
        if (!g_tnChipPoolReady) { TNChipPoolBuild(g_tnChipPool); g_tnChipPoolReady = true; }
        TNSpawnChips(g_tnChips, g_tnChipPool, tA, beatCount);
    }
    TNUpdateChips(g_tnChips, dt * speed, tA);
    g_tnChipVerts = TNBuildChipGeometry(g_tnChips, tA, g_tnChipCpu);

    // ---- 每帧常量 uniform ----
    // 【两份程序都要传】几何趟用的是 g_tnProgGeom。同名 uniform 在两个程序里是
    // 各自独立的状态，只往 g_tnProg 传的话几何趟会全读默认值 0，画面变纯黑。
    auto uploadCommon = [&](GLuint p) {
        if (!p) return;
        // 【必须先绑】—— glGetUniformLocation(p,…) 只是"从 p 里取位置"，
        // 而 glUniform* 写的是【当前绑定的程序】。少了这行，给 g_tnProgGeom 传的那一串
        // 会全写到还在绑定的 g_tnProg 上：几何程序一个都没拿到、全读默认 0，
        // 几何趟直接渲染成纯黑。（这个坑我自己在同一段代码的注释里刚写过一遍。）
        glUseProgram(p);
        auto V = [p](const char* n) { return glGetUniformLocation(p, n); };
        glUniform1f(V("uTime"), tA);      // 缩放过的时间 —— 细条摆动/高光带/星点闪烁都吃它
        glUniform1f(V("uNear"), TN_NEAR_Z);
        glUniform1f(V("uSpan"), TN_SPAN);
        glUniform1f(V("uFadeIn"), TN_FADE_IN);
        glUniform1f(V("uFadeFull"), TN_FADE_FULL);
        glUniform2f(V("uResolution"), w, h);
        glUniform1f(V("uOpacity"), opacity);
        glUniform1i(V("uDarkBg"), darkBg ? 1 : 0);
        glUniform1f(V("uRMax"), VTMaxRadius());
        glUniform1f(V("uRimW"), TN_RIM_W);
        glUniform1f(V("uRimI"), TN_RIM_I);
        glUniform4fv(V("uWallA"),  1, g_tnWallA.a);
        glUniform4fv(V("uWallB"),  1, g_tnWallB.a);
        glUniform4fv(V("uMetalA"), 1, g_tnMetalA.a);
        glUniform4fv(V("uMetalB"), 1, g_tnMetalB.a);
        glUniform4fv(V("uPaperA"), 1, g_tnPaperA.a);
        glUniform4fv(V("uPaperB"), 1, g_tnPaperB.a);
        glUniform4fv(V("uLaceC"),  1, g_tnLaceC.a);
        glUniform4fv(V("uRimC"),   1, g_tnRimC.a);
        // 手持镜头。3x3 拆成三行传 —— 见 kTVS 里 uCamR0/1/2 的声明处。
        glUniform4fv(V("uCamR0"), 1, &g_tnCamR[0]);
        glUniform4fv(V("uCamR1"), 1, &g_tnCamR[4]);
        glUniform4fv(V("uCamR2"), 1, &g_tnCamR[8]);
        glUniform4fv(V("uCamT"),  1, g_tnCamT);
        // 中轴纽结的辉光强度（会远大于 1 —— 靠 HDR 溢出炸泛光，见 uMat==3）
        glUniform1f(V("uKnotGlow"), g_tnKnotGlow);
        glUniform1f(V("uEdgeGlow"), g_tnEdgeGlow);
        // 金属细条的边辉光（实机现在唯一的辉光源）
        glUniform1f(V("uRibGlow"),  g_tnRibGlow);
        glUniform1f(V("uRibGlowW"), TN_RIB_GLOW_W);
        // 音乐频谱（贴在细条两条长边上）：16 个频段 + 随机色的种子。
        // 【uFft 是本工程已有的音频约定】（别的效果同名），上传方式照抄 GpuEffect 那条。
        // 【归一化在 CPU 做】除以当前最响的那一段。
        // 分析器是按"最近 3 秒的峰值"归一化的，实测安静段落整排都在 0.01~0.05 挤着 ——
        // 不归一化的话画面上永远是细细一圈、看不出哪段在响。
        // 放 CPU 而不是着色器：顶点端也要用同一个值（缩放针的长短），那边每根针算一次太浪费。
        // 分母留 0.10 的地板：完全安静时不至于把底噪当信号放大。
        float nf[TN_SPEC_BANDS];
        {
            float mx = 0.10f;
            for (int i = 0; i < TN_SPEC_BANDS; ++i) if (fft[i] > mx) mx = fft[i];
            for (int i = 0; i < TN_SPEC_BANDS; ++i) {
                const float v = fft[i] / mx;
                nf[i] = (v > 1.0f) ? 1.0f : v;
            }
        }
        glUniform1fv(V("uFft"), TN_SPEC_BANDS, nf);
        glUniform1f(V("uRibLenBase"), TN_RIB_LEN_BASE);
        glUniform1f(V("uRibLenMax"), TN_RIB_LEN_MAX);
        // 形状参数：默认全是恒等（= 原样）。过渡/驱动以后从这里插值过去。
        glUniform1f(V("uRibWidth"),  TN_RIB_WIDTH_BASE);
        glUniform1f(V("uRibSpread"), TN_RIB_SPREAD_BASE);
        glUniform1f(V("uRibCurl"),   TN_RIB_CURL_BASE);
        glUniform1f(V("uBeatSeed"), beatSeed);
        glUniform1f(V("uSpecAmt"),   darkBg ? TN_SPEC_GAIN_DARK : TN_SPEC_GAIN_LIGHT);
        glUniform1f(V("uSpecFloor"), TN_SPEC_GAIN_BASE);
        glUniform1f(V("uRibWide"), TN_RIB_WIDE_AMT);
        glUniform1f(V("uSat"), 1.15f);
        glUniform1f(V("uExposure"), 1.15f);
        glUniform1f(V("uBloomAmt"), 0.85f);
        glUniform1f(V("uBloomThresh"), 0.42f);
        // 相机在中轴原点朝 +z 看；fov ≈ 48°，与其它 mesh 效果一致
        // （uFocal 写进 VS 的 gl_Position，不走视图矩阵）
        glUniform1f(V("uFocal"), 1.0f / 0.4452f);
    };
    uploadCommon(prog);
    uploadCommon(g_tnProgGeom);
    // uploadCommon 内部会 glUseProgram，回来时绑定的是最后那个 —— 背景趟用的
    // U(...) 是按 g_tnProg 取的位置，这里必须切回去。
    glUseProgram(prog);

    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

    // ---- ① 场景 = 背景 + 壁×6层 + 细条×6桶 + 剪纸 ----
    glBindFramebuffer(GL_FRAMEBUFFER, g_tnScene.fb);
    glViewport(0, 0, wi, hi);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glUniform1i(U("uPass"), 0);
    glUniform1i(U("uMat"), 0);
    glUniform1f(U("uLayerZ"), 0.0f);
    glUniform1f(U("uRibWave"), 0.0f);
    glDisable(GL_BLEND);
    glBindVertexArray(quadVao);                  // quadVao = NDC 全屏四边形
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // ---- 几何趟：从这里开始换成 g_tnProgGeom ----
    // 背景趟已经把 g_tnScene 铺满，几何趟与它无关。见 TnGeomVSSource() 上面的说明。
    const GLuint gprog = g_tnProgGeom ? g_tnProgGeom : prog;
    glUseProgram(gprog);
    auto UG = [gprog](const char* n) { return glGetUniformLocation(gprog, n); };

    // 壁：2026-09-23 按用户要求【已删】。原先是画 TN_LAYERS 遍、每遍整体平移一个 z 并环绕。
    // 要恢复就把这段放回来（环绕必须仍在 CPU 侧逐层做 —— 放到 VS 里按顶点 mod 会把
    // 跨边界的三角形撕成横贯屏幕的细条，VXGeom 记过这一条）：
    //   glUniform1i(UG("uMat"), 0);
    //   glBindVertexArray(g_tnWall.vao);
    //   for (int L = 0; L < TN_LAYERS; ++L) {
    //       float z = TN_NEAR_Z + std::fmod(g_tnPhase + (float)L * TN_SEG - TN_NEAR_Z, TN_SPAN);
    //       if (z < TN_NEAR_Z) z += TN_SPAN;
    //       glUniform1f(UG("uLayerZ"), z);
    //       glDrawArrays(GL_TRIANGLES, 0, g_tnWall.vcount);
    //   }

    glUniform1i(UG("uPass"), 1);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // 细条：按 z 分桶逐桶提交，让半透明的远端淡出也按远近排序
    glUniform1i(UG("uMat"), 1);
    glUniform1f(UG("uLayerZ"), 0.0f);
    glUniform1f(UG("uRibWave"), VT_RIB_WAVE);
    glBindVertexArray(g_tnRibbon.vao);
    for (int b = 0; b < VT_RIB_BUCKETS; ++b) {
        const int first = g_tnRibBucket[b];
        const int cnt   = g_tnRibBucket[b + 1] - first;
        if (cnt <= 0) continue;
        glDrawArrays(GL_TRIANGLES, first, cnt);
    }

    // 剪纸：整块一次提交
    if (g_tnChipVerts > 0) {
        glUniform1i(UG("uMat"), 2);
        glUniform1f(UG("uRibWave"), 0.0f);
        glBindVertexArray(g_tnChipVao);
        // 【必须显式绑 ARRAY_BUFFER 再传数据】—— 绑定 VAO 并不会绑定 GL_ARRAY_BUFFER
        // （它不是 VAO 状态，只有 ELEMENT_ARRAY_BUFFER 是）。漏了这行 glBufferData
        // 会作用到上一个碰过的 buffer 上，纸片要么不显示要么污染别的几何。
        glBindBuffer(GL_ARRAY_BUFFER, g_tnChipVbo);
        if (g_tnChipVerts > g_tnChipCap) {
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(g_tnChipCpu.size() * sizeof(float)), g_tnChipCpu.data(), GL_DYNAMIC_DRAW);
            g_tnChipCap = g_tnChipVerts;
        } else {
            glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(g_tnChipCpu.size() * sizeof(float)), g_tnChipCpu.data());
        }
        glDrawArrays(GL_TRIANGLES, 0, g_tnChipVerts);
    }

    // 【中轴纽结先停掉】—— 那是我上一轮理解错需求自己造的东西，用户要的是给【已有的边】
    // 加辉光，不是新增一个构造。代码（VTKnotGeom.h / uMat==3 / g_tnKnot）全部保留，
    // 想恢复的话把下面这段的注释去掉即可；确认不要了我再整个删干净。
    if (false && g_tnKnot.vao && g_tnKnot.vcount > 0) {
        glUniform1i(UG("uMat"), 3);
        glUniform1f(UG("uRibWave"), 0.0f);
        glUniform1f(UG("uLayerZ"), KT_Z);
        glBindVertexArray(g_tnKnot.vao);
        glDrawArrays(GL_TRIANGLES, 0, g_tnKnot.vcount);
    }
    glBindVertexArray(0);
    glDisable(GL_BLEND);

    // ---- 几何趟结束，切回共用程序 ----
    // 【必须切回来】glUniform* 作用在"当前绑定的程序"上。下面这几趟全屏渲染的 U(...)
    // 是按 g_tnProg 取的位置，程序没切回来的话会喂给几何程序 —— 全屏趟全读默认值。
    glUseProgram(prog);

    // ---- ② 亮部提取 + 两轮分离式高斯 ----
    const int bw = g_tnBlurA.fb ? (wi / 2) : 2, bh = g_tnBlurB.fb ? (hi / 2) : 2;
    glUniform2f(U("uResolution"), (float)bw, (float)bh);
    glUniform2f(U("uTexel"), 1.0f / bw, 1.0f / bh);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_tnScene.tex);
    glUniform1i(U("uTexScene"), 0);
    glUniform1i(U("uPass"), 2);
    glBindFramebuffer(GL_FRAMEBUFFER, g_tnBlurA.fb);
    glViewport(0, 0, bw, bh);
    glBindVertexArray(quadVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glUniform1i(U("uPass"), 3);
    glUniform1i(U("uTexBloom"), 0);
    const float radii[2] = { 1.7f, 3.6f };
    for (int p2 = 0; p2 < 2; ++p2) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_tnBlurA.tex);
        glBindFramebuffer(GL_FRAMEBUFFER, g_tnBlurB.fb);
        glViewport(0, 0, bw, bh);
        glUniform2f(U("uBlurDir"), radii[p2], 0.0f);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_tnBlurB.tex);
        glBindFramebuffer(GL_FRAMEBUFFER, g_tnBlurA.fb);
        glViewport(0, 0, bw, bh);
        glUniform2f(U("uBlurDir"), 0.0f, radii[p2]);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    // ---- ③ 合成回原目标 ----
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
    glViewport(0, 0, wi, hi);
    glUniform2f(U("uResolution"), w, h);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_tnScene.tex);
    glUniform1i(U("uTexScene"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, g_tnBlurA.tex);
    glUniform1i(U("uTexBloom"), 1);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(U("uPass"), 4);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(quadVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}
