// ============================================================================
// Shardstorm 渲染器
//
// 与 Filigree（VisualEffects/TN/TNRender.cpp）同一套结构：一个效果一个模块，
// 自己的程序/FBO/状态全 static 在这里，外界只看 XSRender.h 的两个入口。
//
// 管线（5 趟，与其它泛光类效果一致）：
//   ① 场景 = 程序化深空背景 + 碎片×BS_BUCKETS 桶
//   ② 亮部提取（阈值以上才进泛光）   ③ 两轮分离式高斯   ④ 合成回原目标
//
// 【为什么"变化幅度"天生就高】碎片【各自公转】—— 每片的角速度烘在顶点里
// （内圈快外圈慢 + 逐片随机）。按 1/30 秒量：每片每帧位移 ≈ 自身尺寸，
// 也就是"这帧在这儿的碎片，下帧已经飞到别处" → 逐像素差天然很大。
// 实测 0.2047（离线台 xs_render_test + dynamics.py），是同类效果里最高的。
// ============================================================================
#include "XSRender.h"
#include "XSGeom.h"
#include "XSShaders.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// 运动/几何参数全部在 XSGeom.h（【唯一来源】，离线台 include 同一份）；
// 这里只放渲染侧的参数。
static const float XS_FADE_IN     = 1.5f;    // 近端淡入（同项目其它效果）
static const float XS_FADE_FULL   = 6.0f;
static const float XS_NEAR_Z      = -10.0f;
static const float XS_SPAN        = 30.0f;
static const float XS_FOCAL       = 1.0f / 0.4452f;   // fov ≈ 48°
static const float XS_EDGE_I      = 1.35f;   // 碎片边缘高光强度
static const float XS_EDGE_W      = 0.16f;
static const float XS_HUE_SPREAD  = 0.85f;
static const float XS_BLOOM_AMT   = 0.85f;
static const float XS_BLOOM_TH    = 0.42f;
static const float XS_EXPOSURE    = 1.15f;
static const float XS_SAT         = 1.15f;
// 手持镜头（同 Filigree 的分工：roll 只转画面、yaw/pitch 才动灭点、平移给视差）
static const float XS_CAM_SHAKE   = 0.9f;

struct XSRenderer { GLuint vao = 0, vbo = 0; GLsizei vcount = 0; };
struct XSFbo { GLuint fb = 0, tex = 0; };

static GLuint     g_xsProg = 0;
static XSRenderer g_xsShard;
static int        g_xsBucket[XS_BUCKETS + 1] = {0};
static XSFbo      g_xsScene, g_xsBlurA, g_xsBlurB;
static int        g_xsFboW = 0, g_xsFboH = 0;
static float      g_xsAnimT = 0.0f;
static float      g_xsHitT  = 999.0f;
static float      g_xsCamR[12] = { 1,0,0,0, 0,1,0,0, 0,0,1,0 };
static float      g_xsCamT[4]  = { 0,0,0,0 };
// 调色板（四色，按色相混合；见 XSShaders.h 的 uColA..D）
static const float kXSPalA[4] = { 0.95f, 0.42f, 0.18f, 1.0f };
static const float kXSPalB[4] = { 1.00f, 0.86f, 0.42f, 1.0f };
static const float kXSPalC[4] = { 0.30f, 0.85f, 0.95f, 1.0f };
static const float kXSPalD[4] = { 0.72f, 0.32f, 0.95f, 1.0f };

void ResetXS() {
    g_xsProg = 0;
    if (g_xsShard.vao) { glDeleteVertexArrays(1, &g_xsShard.vao); g_xsShard.vao = 0; }
    if (g_xsShard.vbo) { glDeleteBuffers(1, &g_xsShard.vbo);      g_xsShard.vbo = 0; }
    if (g_xsScene.fb) { glDeleteFramebuffers(1, &g_xsScene.fb); glDeleteTextures(1, &g_xsScene.tex); g_xsScene.fb = g_xsScene.tex = 0; }
    if (g_xsBlurA.fb) { glDeleteFramebuffers(1, &g_xsBlurA.fb); glDeleteTextures(1, &g_xsBlurA.tex); g_xsBlurA.fb = g_xsBlurA.tex = 0; }
    if (g_xsBlurB.fb) { glDeleteFramebuffers(1, &g_xsBlurB.fb); glDeleteTextures(1, &g_xsBlurB.tex); g_xsBlurB.fb = g_xsBlurB.tex = 0; }
    g_xsFboW = g_xsFboH = 0;
    g_xsAnimT = 0.0f; g_xsHitT = 999.0f;
    const float I[12] = { 1,0,0,0, 0,1,0,0, 0,0,1,0 };
    for (int k = 0; k < 12; ++k) g_xsCamR[k] = I[k];
    for (int k = 0; k < 4; ++k)  g_xsCamT[k] = 0.0f;
    g_xsShard.vcount = 0;
}

static bool CompileXS() {
    if (g_xsProg) return true;
    auto comp = [](GLenum ty, const char* src, const char* lbl) -> GLuint {
        GLuint sh = glCreateShader(ty);
        glShaderSource(sh, 1, &src, nullptr);
        glCompileShader(sh);
        GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[4096]; GLsizei n = 0; glGetShaderInfoLog(sh, sizeof log, &n, log);
            char msg[4600]; _snprintf(msg, sizeof msg, "%s 编译失败:\n%s", lbl, log);
            MessageBoxA(nullptr, msg, "Shardstorm Shader Error", MB_OK);
            glDeleteShader(sh); return 0;
        }
        return sh;
    };
    GLuint vs = comp(GL_VERTEX_SHADER,   kXSVS, "kXSVS");
    GLuint fs = comp(GL_FRAGMENT_SHADER, kXSFS, "kXSFS");
    if (!vs || !fs) return false;
    GLuint p = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs); glLinkProgram(p);
    glDeleteShader(vs); glDeleteShader(fs);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096]; GLsizei n = 0; glGetProgramInfoLog(p, sizeof log, &n, log);
        MessageBoxA(nullptr, log, "Shardstorm Link Error", MB_OK);
        glDeleteProgram(p); return false;
    }
    g_xsProg = p;
    return true;
}

static bool EnsureXS() {
    if (g_xsShard.vao) return true;
    if (!CompileXS()) return false;

    std::vector<float> verts;
    XSBuild(verts, g_xsBucket);
    glGenVertexArrays(1, &g_xsShard.vao);
    glBindVertexArray(g_xsShard.vao);
    glGenBuffers(1, &g_xsShard.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_xsShard.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(float)), verts.data(), GL_STATIC_DRAW);
    // 属性布局必须与 XSGeom.h 的推入顺序一一对应（改一处要改两处 —— 见那里的约定注释）
    {
        const GLsizei st = (GLsizei)(XSStride() * sizeof(float));
        const int sz[11] = { 3,2,3,1,1,1,1,1,1,1,1 };   // pos uv nrm id phase omega radius elev size hue alpha
        int off = 0;
        for (int i = 0; i < 11; ++i) {
            glVertexAttribPointer(i, sz[i], GL_FLOAT, GL_FALSE, st, (const void*)(off * sizeof(float)));
            glEnableVertexAttribArray(i);
            off += sz[i];
        }
    }
    g_xsShard.vcount = (GLsizei)(verts.size() / XSStride());
    glBindVertexArray(0);
    return true;
}

static void XSRebuildFbos(int w, int h) {
    if (w == g_xsFboW && h == g_xsFboH && g_xsScene.fb) return;
    if (g_xsScene.fb) { glDeleteFramebuffers(1, &g_xsScene.fb); glDeleteTextures(1, &g_xsScene.tex); g_xsScene.fb = g_xsScene.tex = 0; }
    if (g_xsBlurA.fb) { glDeleteFramebuffers(1, &g_xsBlurA.fb); glDeleteTextures(1, &g_xsBlurA.tex); g_xsBlurA.fb = g_xsBlurA.tex = 0; }
    if (g_xsBlurB.fb) { glDeleteFramebuffers(1, &g_xsBlurB.fb); glDeleteTextures(1, &g_xsBlurB.tex); g_xsBlurB.fb = g_xsBlurB.tex = 0; }
    g_xsFboW = w; g_xsFboH = h;
    const int bw = w / 2 > 2 ? w / 2 : 2, bh = h / 2 > 2 ? h / 2 : 2;
    auto mk = [](XSFbo& f, int fw, int fh) {
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
    mk(g_xsScene, w, h); mk(g_xsBlurA, bw, bh); mk(g_xsBlurB, bw, bh);
}

void RenderShardstorm(float t, float speed, float w, float h, float opacity, bool darkBg,
                      GLuint quadVao, float beatFlash, unsigned beatCount, float exitT) {
    if (w < 2.0f || h < 2.0f) return;
    if (!EnsureXS()) return;
    const int wi = (int)w, hi = (int)h;
    XSRebuildFbos(wi, hi);
    if (!g_xsScene.fb || !g_xsShard.vao) return;

    const GLuint prog = g_xsProg;
    glUseProgram(prog);
    auto U = [prog](const char* n) { return glGetUniformLocation(prog, n); };

    // ---- 帧间差（切效果回来时相位不跳变，同 Filigree/Function1）----
    static float s_lastT = -1.0f;
    const bool tRestart = (s_lastT < 0.0f || t < s_lastT);
    float dt = tRestart ? 0.0f : (t - s_lastT);
    if (tRestart) g_xsAnimT = 0.0f;
    s_lastT = t;
    if (dt > 0.1f) dt = 0.1f;

    // 动画时间按 speed 缩放（同 Filigree：用户拖 speed 滑块时画面必须跟着变）
    g_xsAnimT += dt * speed;
    const float tA = g_xsAnimT;

    // ---- 鼓点：beatFlash 的上升沿 ----
    static float s_prevFlash = 0.0f;
    const bool hit = (beatFlash > s_prevFlash + 1e-4f);
    if (hit) g_xsHitT = 0.0f;
    s_prevFlash = beatFlash;      // 写在 if 外面：解锁时不至于用积攒的差值误触发
    g_xsHitT += dt;
    float gs = 0.0f;
    {
        float hk = 1.0f - g_xsHitT / 0.34f;
        if (hk < 0.0f) hk = 0.0f;
        gs = hk * hk * hk;        // 三次衰减（同 Filigree 的 TN_HIT_CURVE）
    }

    // ---- 手持镜头 + 鼓点推进（与 Filigree 同一套分工）----
    {
        const float tw = t;       // 手是摄影师的 → 真实时基，不乘 speed
        float yaw   = XS_CAM_SHAKE * (0.055f * std::sin(tw * 2.60f) + 0.028f * std::sin(tw * 4.10f + 1.7f));
        float pitch = XS_CAM_SHAKE * (0.045f * std::sin(tw * 2.25f + 2.4f) + 0.024f * std::sin(tw * 3.60f + 0.6f));
        float roll  = XS_CAM_SHAKE * (0.028f * std::sin(tw * 1.55f + 0.9f) + 0.014f * std::sin(tw * 3.20f + 3.1f));
        yaw   += gs * 0.040f * std::sin(tw * 18.0f);
        pitch += gs * 0.032f * std::cos(tw * 15.0f);
        roll  += gs * 0.038f * std::sin(tw * 22.0f);
        const float cy = std::cos(yaw),   sy = std::sin(yaw);
        const float cx = std::cos(pitch), sx = std::sin(pitch);
        const float cz = std::cos(roll),  sz = std::sin(roll);
        g_xsCamR[0] =  cz*cy - sz*sx*sy;  g_xsCamR[1]  = -sz*cx;  g_xsCamR[2]  =  cz*sy + sz*sx*cy;  g_xsCamR[3]  = 0.0f;
        g_xsCamR[4] =  sz*cy + cz*sx*sy;  g_xsCamR[5]  =  cz*cx;  g_xsCamR[6]  =  sz*sy - cz*sx*cy;  g_xsCamR[7]  = 0.0f;
        g_xsCamR[8] = -cx*sy;             g_xsCamR[9]  =  sx;     g_xsCamR[10] =  cx*cy;             g_xsCamR[11] = 0.0f;
        // z 分量 = 鼓点瞬间推进（命中瞬间到位、之后弹回）
        g_xsCamT[0] = 0.10f * std::sin(tw * 1.90f + 0.4f);
        g_xsCamT[1] = 0.09f * std::sin(tw * 2.15f + 1.1f);
        g_xsCamT[2] = gs * 0.85f;
        g_xsCamT[3] = 0.0f;
    }

    // ---- 出场：细/碎片整体朝镜头压过去 + 加速自转（切走时的退场动画）----
    // exitT 从 0 涨到 1 的这段里，公转倍率被拉高、整体沿 +z 冲出去。
    float exitBoost = 0.0f, exitPush = 0.0f;
    if (exitT < 1.0f) {
        const float e = exitT < 0.0f ? 0.0f : exitT;
        exitBoost = e * 3.0f;
        exitPush  = e * e * 26.0f;
    }

    const float orbit  = XS_ORBIT_BASE + gs * XS_ORBIT_HIT + exitBoost;
    const float strobe = XS_STROBE + gs * 0.25f;
    const float flash  = gs * 0.35f;

    const float nearZ = XS_NEAR_Z - exitPush;   // 出场时把可见带整段往前拉（= 冲出去）

    // ---- 每帧常量 uniform ----
    glUniform1f(U("uTime"), tA);
    glUniform2f(U("uResolution"), w, h);
    glUniform1f(U("uFocal"), XS_FOCAL);
    glUniform1f(U("uNear"), nearZ);
    glUniform1f(U("uSpan"), XS_SPAN);
    glUniform1f(U("uFadeIn"), XS_FADE_IN);
    glUniform1f(U("uFadeFull"), XS_FADE_FULL);
    glUniform1f(U("uOpacity"), opacity);
    glUniform1i(U("uDarkBg"), darkBg ? 1 : 0);
    glUniform4fv(U("uColA"), 1, kXSPalA);
    glUniform4fv(U("uColB"), 1, kXSPalB);
    glUniform4fv(U("uColC"), 1, kXSPalC);
    glUniform4fv(U("uColD"), 1, kXSPalD);
    glUniform1f(U("uHueSpread"), XS_HUE_SPREAD);
    glUniform1f(U("uEdgeI"), XS_EDGE_I);
    glUniform1f(U("uEdgeW"), XS_EDGE_W);
    glUniform1f(U("uSpin"), XS_SPIN_BASE);
    glUniform1f(U("uOrbit"), orbit);
    glUniform1f(U("uSizeMul"), XS_SIZE_MUL);
    glUniform1f(U("uSpread"), XS_SPREAD_BASE);
    glUniform1f(U("uBandSpeed"), XS_BAND_SPEED);
    glUniform1f(U("uStrobe"), strobe);
    glUniform1f(U("uFlash"), flash);
    glUniform4fv(U("uCamR0"), 1, &g_xsCamR[0]);
    glUniform4fv(U("uCamR1"), 1, &g_xsCamR[4]);
    glUniform4fv(U("uCamR2"), 1, &g_xsCamR[8]);
    glUniform4fv(U("uCamT"),  1, g_xsCamT);
    glUniform1f(U("uBloomAmt"), XS_BLOOM_AMT);
    glUniform1f(U("uBloomThresh"), XS_BLOOM_TH);
    glUniform1f(U("uExposure"), XS_EXPOSURE);
    glUniform1f(U("uSat"), XS_SAT);

    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

    // ---- ① 场景 ----
    glBindFramebuffer(GL_FRAMEBUFFER, g_xsScene.fb);
    glViewport(0, 0, wi, hi);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUniform1i(U("uPass"), 0);
    glDisable(GL_BLEND);
    glBindVertexArray(quadVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glUniform1i(U("uPass"), 1);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(g_xsShard.vao);
    // 逐桶提交：桶是按半径（由外到内）分的，远的先画，近的压上去
    for (int b = 0; b < XS_BUCKETS; ++b) {
        const int first = g_xsBucket[b];
        const int cnt   = g_xsBucket[b + 1] - first;
        if (cnt > 0) glDrawArrays(GL_TRIANGLES, first, cnt);
    }
    glBindVertexArray(0);
    glDisable(GL_BLEND);

    // ---- ②③④ 亮部 + 两轮模糊 + 合成 ----
    const int bw = wi / 2 > 2 ? wi / 2 : 2, bh = hi / 2 > 2 ? hi / 2 : 2;
    glUniform2f(U("uResolution"), (float)bw, (float)bh);
    glUniform2f(U("uTexel"), 1.0f / bw, 1.0f / bh);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_xsScene.tex);
    glUniform1i(U("uTexScene"), 0);
    glUniform1i(U("uPass"), 2);
    glBindFramebuffer(GL_FRAMEBUFFER, g_xsBlurA.fb);
    glViewport(0, 0, bw, bh);
    glBindVertexArray(quadVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glUniform1i(U("uPass"), 3);
    glUniform1i(U("uTexBloom"), 0);
    const float radii[2] = { 1.7f, 3.6f };
    for (int p2 = 0; p2 < 2; ++p2) {
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g_xsBlurA.tex);
        glBindFramebuffer(GL_FRAMEBUFFER, g_xsBlurB.fb); glViewport(0, 0, bw, bh);
        glUniform2f(U("uBlurDir"), radii[p2], 0.0f);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g_xsBlurB.tex);
        glBindFramebuffer(GL_FRAMEBUFFER, g_xsBlurA.fb); glViewport(0, 0, bw, bh);
        glUniform2f(U("uBlurDir"), 0.0f, radii[p2]);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
    glViewport(0, 0, wi, hi);
    glUniform2f(U("uResolution"), w, h);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g_xsScene.tex);  glUniform1i(U("uTexScene"), 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, g_xsBlurA.tex); glUniform1i(U("uTexBloom"), 1);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(U("uPass"), 4);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(quadVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}
