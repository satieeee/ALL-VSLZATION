/**
 * @file main.cpp
 * @brief MAIN.exe GPU 版 —— 全 OpenGL 3.3 渲染，播放链 + 预设 + 过渡morphing + 黑底切换
 *
 * 所有可视化效果都是 .frag shader，存在 VisualEffects/shaders/ 下。
 * CPU 只负责：音频采集 → FFT → BPM → 传 uniform 给 shader。
 * 所有像素计算在 GPU 并行完成。
 *
 * 过渡：旧效果渲染到 FBO 纹理，新效果采样旧纹理做 smoothstep 混合。
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#define _UNICODE
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <gl/GL.h>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <cmath>
#include <chrono>

#include "AudioCapture/AudioCapture.h"
#include "AudioCapture/AudioAnalyzer.h"
#include "BPMdetector/BPMDetector.h"
#include "SharedState.h"

// CPU 渲染后端（从 ALL VSLZATION 项目编译进来的 GDI 效果）
#include "B376/B376.h"
#include "B376/B376Field.h"   // 烘焙覆盖场（540x720, VTracer 全部 1202 条轮廓）
#include "B376/B376Mesh.h"    // 轮廓 GL 网格（顶点几何渲染用）
#include "B376/B376Curves.h"  // 单体平滑曲线（曲线流 movement）
#include "B376/B376Shaders.h"
#include "M2/M2Mesh.h"        // 单体网格+曲线流+亮度贴图（Metalheart2）
#include "M3/M3Mesh.h"        // 单体网格+曲线流+亮度贴图（Metalheart3）
#include "M4/M4Mesh.h"        // 单体网格+曲线流+亮度贴图（Metalheart4）
#include "C3/C3Lines.h"       // Cubeeffect3 线段数据（after.png 矢量化，GL_LINES 几何渲染）
// 折纸片纯几何（面积重心 / 内缩 / 分面打光）—— 必须早于下面的 Origami 全局变量，
// 而且和 _hittest/origami_test.cpp 共用同一份，改完能离线验证。
#include "C3/C3OrigamiGeom.h"
#include "C3/C3Shaders.h"
#include "PC/PCurves.h"      // StellarVortex 曲线 LUT（生成物，见 effect reference/conclusion/gen_pcurves.py）
#include "PC/PCShaders.h"
#include "VX/VXGeom.h"       // Involute 渐开线曲面几何（纯数学，不碰 GL）
#include "VX/VXShaders.h"
#include "SS/SSGeom.h"       // Involute_1 台阶板几何（纯数学，不碰 GL）
#include "SS/SSShaders.h"
#include "F1/F1Geom.h"       // Function1 的 Zindler conoid 几何与 movement（纯数学，不碰 GL）
#include "F1/F1Shaders.h"
#include "TN/VTGeom.h"       // Filigree 的隧道壁/金属细条/内壁贴点几何（纯数学，不碰 GL）
#include "TN/TNChipGeom.h"   // Filigree 的内壁剪纸片：池子/生成/漂移/逐帧顶点（纯数学，不碰 GL）
#include "TN/VTKnotGeom.h"   // Filigree 的中轴纽结（环面纽结管），配 uMat==3 的炫彩辉光
#include "TN/TNParams.h"     // Filigree 的参数唯一来源（调色 + 各趟常量），离线台 include 同一份
#include "TN/TNShaders.h"    // Filigree 的 kTVS/kVTFS 原文，同上

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "opengl32.lib")

// ===== GL 3.3 Loader =====
// 【清单只有一份】typedef / 函数指针 / GL 常量都在 VisualEffects/GLFuncs.h 里。
// 这里用 GLFUNCS_DEFINE 让它展开成【定义】（全局一份），别处 include 同一个头得到的是
// extern 声明 —— 所以效果代码搬进 VisualEffects/<效果>/ 之后照样能调这些函数。
// 【谁填值】下面 LoadGL()：wglGetProcAddress。新增函数记得往它的 L(...) 里加一行。
#define GLFUNCS_DEFINE
#include "GLFuncs.h"
#undef GLFUNCS_DEFINE

#include "TN/TNRender.h"   // Filigree 渲染器（效果代码搬出去了；必须在 GLFuncs.h 的定义块之后）
#include "XS/XSRender.h"   // Shardstorm 渲染器（同上）
#include "Transition/TransitionFx.h"   // 跨效果过渡（MilkDrop 式，引擎侧统一处理）

static void LoadGL(){
#define L(n) n=(PFN_##n)wglGetProcAddress(#n); if(!n) n=(PFN_##n)wglGetProcAddress(#n "ARB");
L(glGenVertexArrays) L(glBindVertexArray) L(glDeleteVertexArrays)
L(glGenBuffers) L(glBindBuffer) L(glBufferData) L(glBufferSubData) L(glDeleteBuffers)
L(glVertexAttribPointer) L(glEnableVertexAttribArray)
L(glCreateShader) L(glShaderSource) L(glCompileShader)
L(glGetShaderiv) L(glGetShaderInfoLog) L(glDeleteShader)
L(glCreateProgram) L(glAttachShader) L(glLinkProgram)
L(glGetProgramiv) L(glGetProgramInfoLog) L(glDeleteProgram)
L(glUseProgram) L(glGetUniformLocation)
L(glUniform1f) L(glUniform2f) L(glUniform1i) L(glUniform1fv) L(glUniform4fv)
L(glGenFramebuffers) L(glBindFramebuffer) L(glFramebufferTexture2D) L(glCheckFramebufferStatus) L(glBlendColor) L(glDeleteFramebuffers)
L(glGetFramebufferAttachmentParameteriv)
L(glBlitFramebuffer)
L(glActiveTexture)
#undef L
}

// ===== CPU 渲染副产品模拟: 全局状态 =====
static float g_ditherStrength = 1.0f; // GDI质感强度: 0=纯GPU 0.5=微妙 0.7=标准 1.0=低保真
// ⅔ 降采样（DownscaleBlit）开关。Q 键切换。
// 它是"CPU GDI 质感"的一部分：Linear 降到 ⅔ 再 NEAREST 放大回原尺寸。
// 代价很大 —— 1080p 窗口内部实际只有 1280x720，细线被摊薄、边缘变成 1.5px 硬块。
// 保留了原默认（开），但现在是可切换的，方便直接 A/B。
static bool g_downscaleOn = true;
static GLuint g_halfFbo = 0, g_halfTex = 0;
static int    g_halfW = 0, g_halfH = 0;
static std::string g_artifactCode;    // CpuArtifact.glsl 内容
static std::string g_audioPrelude;    // audio_prelude.glsl 内容（音频驱动 ABI）
static AudioFrame g_af = {};          // 最近一帧音频分析结果（GPU uniform 上传用）
static GLuint g_b376FieldTex = 0;     // B376 烘焙覆盖场纹理（GL_R8, 540x720, 启动时解码一次）
static GLuint g_b376LumTex = 0;       // B376 表层明暗场纹理（参考图亮度，同网格）
static GLuint g_b376IdTex = 0;        // B376 运动单元 ID 场纹理（GL_R32F，调试边用）
static GLuint g_b376StaticTex = 0;    // B376 灰绿背景静态掩码纹理（GL_R8）
static GLuint g_b376ArcTex = 0;       // B376 轮廓弧长参数场纹理（GL_R8，描边流光）
static bool   g_debugParts = false;   // 显示 B376 运动单元边缘（前景红/背景绿），按 P 切换
static float  g_b376FlowAmp = 0.5f;   // B376 液态流动幅度（固定值；滑杆已移除）
static GLuint g_b376CurveTex = 0;     // B376 单体平滑曲线纹理（GL_RGBA32F, 128x33，曲线流）
static float  g_plExitT = 999.0f;     // 当前效果出场计时（B376 切出动画用，时长到 WAITBEAT 归零）
static float  g_b376FlowPhase = 0.0f; // B376 流动相位（CPU 实时累计：音频变速驱动流速，不跳变）
static float  g_audioPhase = 0.0f;   // 全效果音频驱动旋转相位（原 uTime×(0.85+trebAtt·0.3) → CPU 积分，变速不跳回）
static float  g_hitDrive   = 0.0f;   // 抽插变速：hit 累积的额外相位（CPU 积分，变速不跳回）
// 【平滑鼓点通道】单记 kick 的包络是个 0.28s 的尖峰，直接拿去推时间轴就是"一冲一停"。
// uHitFlow 是它再低通一次的结果（起音 τ=0.10s 跟得上鼓，回落 τ=0.60s 像松一口气）；
// uHitFlowDrive 是它按同样峰值速率积分出来的相位（秒）—— 时间轴用积分，才不会回弹。
static float  g_hitFlow      = 0.0f; // 0..1 当前速率（空闲 0，冲到正常速度 1）
static float  g_hitFlowDrive = 0.0f; // 秒，速率的积分 —— 时间轴用它，不回弹
// 【单次运动周期】一记 hit 只开一个周期，周期内再来的 hit 一律忽略（见下方计算处）。
static float  g_hitCycleT    = -1.0f; // 周期内已过时间；<0 = 空闲，可以接受新 hit
static float  g_prevHitKick  = 0.0f;  // 上一帧 kick 包络，用来抓"上升沿 = 新的一记"
static constexpr float kHitCycleIdle = 1.00f;   // 基础速率 = 正常播放速度（不再"静默几乎不动"）
static constexpr float kHitCyclePeak = 1.50f;   // 命中脉冲的峰值速率
static constexpr float kHitCycleDur  = 0.90f;   // 一个脉冲持续多久（秒）
static constexpr float kHitPulseT0   = 0.35f;   // 高斯峰值出现在命中后多少秒（延迟 = 起手平滑）
static constexpr float kHitPulseSig  = 0.15f;   // 高斯宽度 σ（越小越尖）
// MilkDrop per_frame 的两级音频低通（extrilliance 系列用的那一套）。
// 原作：vb = vb*0.95 + (1-vb)*bass_att^2*0.02;  vvb = vvb*0.95 + (1-vvb)*vb*0.01
// 第二级增益只有 0.01，所以 q28 是【非常缓慢】起伏的量 —— 不是攻击包络那种抖动。
static float  g_mdVb = 0.0f, g_mdVvb = 0.0f;
static float  g_mdVm = 0.0f, g_mdVvm = 0.0f;
static float  g_mdVt = 0.0f, g_mdVvt = 0.0f;
static float  g_mdQ28 = 0.0f;        // = (vvb + vvm + vvt) * 10，供 warp 用
// 速度滑杆直接缩放【时间轴本身】：g_plTime 按 dt·用户速度 积分。
// 这样所有效果自动跟随，不用每个 shader 自己去乘 uSpeed（乘法会让相位在拖动时整体跳）。
// uTimeS 保留为 uTime 的别名，给已经按它写过的新效果用。
// static float g_plTimeS 已移除
// 抽插变速的【峰值速度倍率】：kick / snare 满格时画面以多少倍实时速度推进。
// 原来的 1.55× 是 (1 + 0.55·kick + 0.18·snare)；现在按峰值直接写。
// 【snare 没跟着缩】原始设计里 snare/kick 的增量比是 0.18/0.55 ≈ 0.33，
// 现在 9.5 : 5.7 ≈ 0.60 —— 军鼓相对变强了。要还原比值就把 snare 改成 3.75。
static constexpr float kHitPumpKick  =  9.5f;   // kick  满格 →  9.5×
static constexpr float kHitPumpSnare =  5.7f;   // snare 满格 →  5.7×
static float  g_cubeCamPhase = 0.0f; // Cubeeffect1 相机 z 相位（CPU 积分：速度=0.47×(1+4×energy)=1x~5x，音频变速不回跳）

// ===== Cubeeffect1 form1「粘滴 + 染墨」状态（CPU 单一真源，GPU 只渲染）=====
// 每个粘滴隶属一个"深度平面"oz（在所属平面内做 2D 运动，从画外沿多边形轨迹切入穿行）。
// CPU 沿每滴的多边形折线推进位置，贴近格线时短暂停驻（粘住片刻）并留下一段墨痕。
// 墨痕记在世界坐标 + 轴向(0沿z/1沿x/2沿y)，shader 沿轴展开成线段；随相机推进掠过/消失。
const int CUBE_DROP_N = 150;        // 粘滴数量（×10→再×3 = 150）
const int CUBE_MARK_MAX = 96;       // 撞边闪光点数（上限）
const int CUBE_POLY_MAX = 40;       // 每滴多边形轨迹最多顶点（类圆折线：入场+弧点+离场，最多 ~17 点）
struct CubeDrop {
    float x, y, z, r, r0;           // 当前世界坐标 + 当前/基础半径（shader 用；融边时 r 收小）
    float vx, vy;                   // 当前运动方向（2D 单位向量，拖尾/融边方向用）
    float oz;                       // 所属深度（相对相机 z）
    int   polyN;                    // 轨迹顶点数
    float px[CUBE_POLY_MAX], py[CUBE_POLY_MAX];  // 多边形顶点（相机系偏移 ox/oy）
    float tAlong;                   // 沿多边形轨迹的位置(0..总长)
    float mergeT;                   // 融进棱线剩余时间(>0 时体积收小、不再前进)
};
struct CubeMark { float x, y, z, flash; int axis; };   // xyz=段中心, flash=点亮强度(0..1, 撞边后衰), axis=棱线轴向(1沿x/2沿y)
static std::vector<CubeDrop> g_cubeDrops;
static std::vector<CubeMark> g_cubeMarks;
static float g_cubeDropSpeedBase = 0.5f;    // 粘滴速度（音频驱动，1x~10x；见 UpdateCubeDrops）
static float g_cubeGfxAspect = 1.0f;        // 播放窗口宽高比（BindUniforms 每帧更新，供轨迹圆心铺满窗口）

// ===== Cubeeffect3 迷宫锚点（鼓点持久重定向：CPU 单一真源，GPU 只按锚点做剪切 warp）=====
// 每个锚点=一个"蛛网被雨滴砸到"的扰动源：世界坐标 pos + 新朝向 dir + 出生时间 spawnT。
// 鼓点时把最旧锚点重新掷在镜头前方随机位置并重掷方向；锚点世界固定，镜头推进时被搅乱
// 的区域自然迎面而来/掠过/落到身后被回收。shader 用高斯衰减的方向剪切把附近棱线推到一个
// 新朝向并停留（持久），spawnT 供 shader 算"先颤后定"的瞬态。
// SpiderVeil / 锚点上限 / 撒点逻辑都在 VisualEffects/C3/C3Veil.h —— GPU 与 CPU 两个后端共用一份，
// 用同一个随机种子就能得到同一批锚点（各写一份迟早漂移）。
static std::vector<SpiderVeil> g_cubeVeils;

static wchar_t g_shaderDir[MAX_PATH] = {};   // shader 目录（LoadEffects 里设置）

// 效果家族（效果树分类根；与渲染路径正交，同 isMesh 可属不同家族）
enum { FAM_NORMAL = 0, FAM_METALHEART = 1, FAM_CUBE = 2, FAM_3D = 3, FAM_LAYERS = 4, FAM_COUNT = 5 };

// ============================================================================
// 效果清单（declarative）—— 加效果只改这里，不再往加载器里塞 if
//
// 背景：渲染路径分成三类，差别是真实的，但以前全部写死成 LoadEffects 里的
// `if (名字 == L"...")`。现在改成一行一条声明，加载器只按声明执行。
//
//   kind = EK_FRAG  全屏片元着色器，只需 shaders/<名字>.frag
//   kind = EK_MESH  程序化几何，走 DrawEffect 里的 meshSlot 分支（不需要 .frag）
//
// 没列在这里的效果 = 默认的全屏 shader（kind=EK_FRAG、FAM_NORMAL、6 形态、可 morph 协议）。
// ----------------------------------------------------------------------------
enum EffKind  { EK_FRAG = 0, EK_MESH = 1 };
enum EffInput { EIN_NONE = 0, EIN_PREV = 1 << 0 };   // 位域：效果需要哪些动态输入

struct EffectDecl {
    const wchar_t* name;
    EffKind  kind       = EK_FRAG;
    int      meshSlot   = -1;
    int      family     = FAM_NORMAL;
    unsigned inputs     = EIN_NONE;   // EIN_PREV = 引擎每帧把上一帧挂到 uPrevFrame
    bool     noFeedback = false;      // 不套引擎的固定网格 warp
    int      formCount  = -1;         // -1=保持默认 6
    int      formInit   = -1;         // -1=不改；否则同时设 form 与 targetForm
    int      proj       = 0;
};

static const EffectDecl kEffectDecls[] = {
    // ---- 程序化几何（meshSlot 见 DrawEffect 的 switch）----
    { L"Involute_1",          EK_MESH, 8,  FAM_NORMAL,      EIN_NONE, true,  1 },
    { L"Function1",           EK_MESH, 9,  FAM_3D,          EIN_NONE, true,  1 },   // Zindler conoid
    { L"Involute",            EK_MESH, 7,  FAM_NORMAL,      EIN_NONE, true,  1 },   // 渐开线曲面
    { L"StellarVortex",       EK_MESH, 6,  FAM_NORMAL,      EIN_NONE, true,  1 },   // 点精灵
    { L"Cubeeffect3 (dark)",  EK_MESH, 4,  FAM_CUBE,        EIN_NONE, true,  1 },   // 同一套几何，深色配色
    { L"Cubeeffect3 (light)", EK_MESH, 5,  FAM_CUBE,        EIN_NONE, true,  1 },   //               浅色配色
    { L"Metalheart1",         EK_MESH, 0,  FAM_METALHEART,  EIN_NONE, true,  1 },
    { L"Metalheart2",         EK_MESH, 1,  FAM_METALHEART,  EIN_NONE, true,  1 },
    { L"Metalheart3",         EK_MESH, 2,  FAM_METALHEART,  EIN_NONE, true,  1 },
    { L"Metalheart4",         EK_MESH, 3,  FAM_METALHEART,  EIN_NONE, true,  1 },
    { L"Shardstorm",          EK_MESH, 11, FAM_NORMAL,      EIN_NONE, true,  1 },   // 碎片风暴
    { L"Filigree",            EK_MESH, 10, FAM_NORMAL,      EIN_NONE, true,  1 },   // 隧道 + 金属细条

    // ---- 全屏 shader 的个别设定 ----
    { L"Psychedelic", EK_FRAG, -1, FAM_NORMAL, EIN_NONE, false, -1, -1, 1 },          // 用另一套投影
    { L"Quicksilver", EK_FRAG, -1, FAM_NORMAL, EIN_NONE, false,  1 },                // 五阶段是一次建构，不参与每拍切形态
    { L"Azurite_c",   EK_FRAG, -1, FAM_NORMAL, EIN_NONE, false, -1,  4 },            // 初始形态 4
    { L"MTH_C",       EK_FRAG, -1, FAM_NORMAL, EIN_NONE, false,  5,  3 },            // 初始形态 3，共 5 形态
    { L"Cubeeffect1", EK_FRAG, -1, FAM_CUBE,   EIN_NONE, true,   2 },                // 双形态：白底墨线 / 黑底粘滴
    { L"Wormtunnel",  EK_FRAG, -1, FAM_3D,     EIN_NONE, false, -1 },                // 光线步进 SDF 隧道

    // ---- Layers 族：layers.html 配方直接拼装的整屏 shader ----
    // 归族只为在树里成组；渲染路径与 Normal 族一样，所以 DownscaleApplies（⅔ 降采样）
    // 和 EffIsMixable（随机播放的上下层混层）仍然放行 —— 这批 dens≈10 的重 shader
    // 正需要降采样保帧率，随机列表也靠它们凑混层。
    { L"Pulseblade",   EK_FRAG, -1, FAM_LAYERS }, { L"Indigoflare",  EK_FRAG, -1, FAM_LAYERS },
    { L"Indigoflare1", EK_FRAG, -1, FAM_LAYERS }, { L"Hyperpush",    EK_FRAG, -1, FAM_LAYERS },
    { L"Rosette",      EK_FRAG, -1, FAM_LAYERS }, { L"Sunslit",      EK_FRAG, -1, FAM_LAYERS },
    { L"Starbloom",    EK_FRAG, -1, FAM_LAYERS }, { L"Starbloom2",   EK_FRAG, -1, FAM_LAYERS },
    { L"Sporefield",   EK_FRAG, -1, FAM_LAYERS }, { L"Silkwheel",    EK_FRAG, -1, FAM_LAYERS },
    { L"Marbleflow",   EK_FRAG, -1, FAM_LAYERS },
    // 鼓点触发的一对：H = max(uHitKick, uHitSnare*0.75)
    { L"Hitpetal",     EK_FRAG, -1, FAM_LAYERS },   // H 只调亮度（S1 的 mix 系数空转）
    { L"Hitpulse",     EK_FRAG, -1, FAM_LAYERS },   // H 接进 S1，鼓点改纹理形态
};

static const EffectDecl* FindEffectDecl(const wchar_t* n) {
    for (const EffectDecl& d : kEffectDecls) if (wcscmp(d.name, n) == 0) return &d;
    return nullptr;
}

// ===== GpuEffect ===== 一个 .frag shader 的封装
// ===== 效果用的图片纹理 =====
// 【为什么是 BMP 不是 PNG】工程里没有任何图片解码器（无 stb_image、无 GDI+/WIC）。
// BMP 未压缩，三十行就能读，零新依赖。素材由 Python 生成/转换。
static GLuint g_shipTex = 0;          // Wormtunnel 的飞船贴图（32 位 BGRA）

static unsigned char* LoadBMP32(const wchar_t* path, int* outW, int* outH) {
    FILE* f = _wfopen(path, L"rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz < 54) { fclose(f); return nullptr; }
    std::vector<unsigned char> buf((size_t)sz);
    if (fread(buf.data(), 1, (size_t)sz, f) != (size_t)sz) { fclose(f); return nullptr; }
    fclose(f);
    if (buf[0] != 'B' || buf[1] != 'M') return nullptr;
    int off  = *(int*)(buf.data() + 10);
    int w    = *(int*)(buf.data() + 18);
    int h    = *(int*)(buf.data() + 22);
    short bpp = *(short*)(buf.data() + 28);
    if (bpp != 32 || w <= 0 || h == 0) return nullptr;
    bool topDown = (h < 0); if (topDown) h = -h;
    if (off < 0 || (size_t)off + (size_t)w * h * 4 > (size_t)sz) return nullptr;
    unsigned char* out = new unsigned char[(size_t)w * h * 4];
    for (int y = 0; y < h; ++y) {
        int srcY = topDown ? y : (h - 1 - y);          // BMP 默认自下而上
        const unsigned char* s = buf.data() + off + (size_t)srcY * w * 4;
        unsigned char* q = out + (size_t)y * w * 4;
        for (int x = 0; x < w; ++x) {                  // BGRA → RGBA
            q[x*4+0] = s[x*4+2]; q[x*4+1] = s[x*4+1];
            q[x*4+2] = s[x*4+0]; q[x*4+3] = s[x*4+3];
        }
    }
    *outW = w; *outH = h;
    return out;
}

// ===== shader 编译 / 链接（唯一一份实现）=====
//
// 以前六个网格渲染器（B376 / C3 / StellarVortex / Involute / Involute_1 / Function1）
// 各抄一份 30 行的 comp/link/log，加上效果装载、pass 装载、混合 shader —— 同一段逻辑
// 在文件里出现九次，唯一的差别是调试标签。现在只剩下面这两段。
//
// 【为什么必须放在 GpuEffect 之上】类内函数体（GpuEffect::Load）只能看见在它【之前】
// 声明的自由函数；放在下面就得额外写前置声明。
//
// 失败一律删句柄并返回 0。旧代码里 B376 / C3 那条路失败时既不删 shader 也不删 program，
// 属于只在错误路径上出现的泄漏；这里顺手修掉，正常路径的行为完全不变。

static const char* const kFullscreenVS =
    "#version 330 core\n"
    "layout(location=0)in vec2 p;layout(location=1)in vec2 u;out vec2 vUV;"
    "void main(){vUV=u;gl_Position=vec4(p,0,1);}";

// 最近一次编译/链接失败的日志 —— 需要弹框给用户看的调用点（混合 shader）用它。
static char g_lastShaderLog[4096] = "";

// logFile 非空时把失败日志追加落盘（C3 以前就这么做，保留）。
static void LogShaderFailure(const char* tag, const char* log, const char* logFile) {
    OutputDebugStringA(tag); OutputDebugStringA(log); OutputDebugStringA("\n");
    _snprintf(g_lastShaderLog, sizeof g_lastShaderLog, "%s%s", tag, log);
    g_lastShaderLog[sizeof g_lastShaderLog - 1] = 0;
    if (logFile) if (FILE* f = fopen(logFile, "a")) { fprintf(f, "%s\n%s\n", tag, log); fclose(f); }
}

static GLuint CompileProgram(const char* vsSrc, const char* fsSrc, const char* label,
                             const char* logFile = nullptr)
{
    char tag[128];
    auto comp = [&](GLenum t, const char* s) -> GLuint {
        GLuint sh = glCreateShader(t); glShaderSource(sh, 1, &s, 0); glCompileShader(sh);
        GLint ok; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[4096]; GLsizei len = 0;
            glGetShaderInfoLog(sh, 4096, &len, log);
            log[(len < 4096) ? len : 4095] = 0;
            _snprintf(tag, sizeof tag, "=== %s Shader Error ===\n", label);
            tag[sizeof tag - 1] = 0;
            LogShaderFailure(tag, log, logFile);
            glDeleteShader(sh);
            return 0;
        }
        return sh;
    };
    GLuint vs = comp(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = comp(GL_FRAGMENT_SHADER, fsSrc);
    if (!vs || !fs) { if (vs) glDeleteShader(vs); if (fs) glDeleteShader(fs); return 0; }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs); glLinkProgram(prog);
    glDeleteShader(vs); glDeleteShader(fs);
    GLint ok; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096]; GLsizei len = 0;
        glGetProgramInfoLog(prog, 4096, &len, log);
        log[(len < 4096) ? len : 4095] = 0;
        _snprintf(tag, sizeof tag, "=== %s Link Error ===\n", label);
        tag[sizeof tag - 1] = 0;
        LogShaderFailure(tag, log, logFile);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

// 一个 pass（多遍效果的一段）。定义在 GpuEffect 之前，因为它要作为成员。
struct PassDef {
    std::string name, inputs, src;
    GLuint program = 0;
    std::map<std::string, GLint> uniforms;
};

struct GpuEffect {
    std::wstring name;
    std::vector<PassDef> passes;   // 非空 = 多遍效果（见 SplitPasses）
    GLuint program = 0;
    std::map<std::string, GLint> uniforms;
    int form = 0, targetForm = 0, proj = 0, formCount = 6;
    bool noFeedback = false;   // 该效果不用帧反馈 warp（自带 movement / 忠实还原）
    // 【自反馈效果】源码里带 //@milk-feedback 标记时置位：
    // 效果自己从 uPrevFrame 采样上一帧并做重映射（MilkDrop 的 warp shader 就是这么干的），
    // 引擎不再套固定的网格 warp，只负责"每帧把当前画面存成下一帧的上一帧"。
    bool milkFeedback = false;
    bool isMesh = false;       // 网格渲染（顶点几何，非全屏 shader，Blender 同原理）
    int meshSlot = -1;         // isMesh 网格槽：0=B376, 1=M2, 2=M3, 3=M4, 4=C3暗, 5=C3亮, 9=Function1,
                               // 10=Filigree,
                               // 6=StellarVortex, 7=Involute, 8=Involute_1（见 DrawEffect 的 switch）
    int family = FAM_NORMAL;   // 效果家族（树分类根，见 FAM_*）
    float formBlend = 1.0f, formBlendSpeed = 0.05f;

    GLint U(const char* n) {
        auto it = uniforms.find(n);
        if (it != uniforms.end() && it->second >= 0) return it->second;
        GLint loc = glGetUniformLocation(program, n);
        uniforms[n] = loc;
        return loc;
    }
    bool Load(const char* src) {
        uniforms.clear();
        program = CompileProgram(kFullscreenVS, src, "Effect");
        return program != 0;
    }
    void BindUniforms(float t, float bpm, float phase, float speed, float w, float h, bool darkBg, float opacity = 1.0f) {
        g_cubeGfxAspect = (h > 1.0f) ? (w / h) : 1.0f;   // 供粘滴轨迹圆心按真实窗口宽高比铺满
        glUniform1f(U("uTime"), t); glUniform1f(U("uTimeS"), t); glUniform1f(U("uBpm"), bpm); glUniform1f(U("uBeatPhase"), phase);
        glUniform1f(U("uSpeed"), speed); glUniform2f(U("uResolution"), w, h);
        glUniform1i(U("uForm"), form); glUniform1i(U("uFormB"), targetForm);
        glUniform1f(U("uFormBlend"), formBlend);
        // uColorScheme 恒为 0（默认值也是 0）：没有任何效果切换过配色方案，字段已删。
        // 保留这一行是给将来留口子 —— shader 侧已经声明了它。
        glUniform1i(U("uColorScheme"), 0); glUniform1i(U("uProjection"), proj);
        glUniform1i(U("uDarkBg"), darkBg ? 1 : 0);
        glUniform1f(U("uOpacity"), opacity);
        glUniform1i(U("uEncode"), 0);
        glUniform1f(U("uMorphBlend"), 0.0f);
        // CPU 渲染质感
        glUniform1f(U("uDitherStrength"), g_ditherStrength);
        // 音频驱动 ABI（audio_prelude.glsl；未声明的 uniform 位置=-1，GL 静默忽略）
        {
            GLint l;
            if ((l = glGetUniformLocation(program, "uFft")) >= 0) glUniform1fv(l, kAudioBands, g_af.fft);
            if ((l = glGetUniformLocation(program, "uWave")) >= 0) glUniform1fv(l, kAudioWaveSamps, g_af.wave);
            glUniform1f(U("uBass"), g_af.bass); glUniform1f(U("uMid"), g_af.mid); glUniform1f(U("uTreb"), g_af.treb);
            glUniform1f(U("uBassAtt"), g_af.bassAtt); glUniform1f(U("uMidAtt"), g_af.midAtt); glUniform1f(U("uTrebAtt"), g_af.trebAtt);
            glUniform1f(U("uBassDec"), g_af.bassDec); glUniform1f(U("uMidDec"), g_af.midDec); glUniform1f(U("uTrebDec"), g_af.trebDec);
            glUniform1f(U("uEnergy"), g_af.energy); glUniform1f(U("uBeatFlash"), g_af.beatFlash);
            glUniform1f(U("uAudioPhase"), g_audioPhase);
            glUniform1f(U("uHitKick"),    g_af.hitKick);
            glUniform1f(U("uHitSnare"),   g_af.hitSnare);
            glUniform1f(U("uHitDrive"),   g_hitDrive);
            glUniform1f(U("uHitFlow"),      g_hitFlow);       // 0..1 平滑鼓点强度
            glUniform1f(U("uHitFlowDrive"), g_hitFlowDrive);  // 秒，它的积分（时间轴用）
            glUniform1f(U("uQ28"),        g_mdQ28);
            // 频谱形状描述子（描述音色，与音量无关）—— 没声明这些 uniform 的 shader 会被静默忽略
            glUniform1f(U("uCentroid"), g_af.centroid);
            glUniform1f(U("uSpread"),   g_af.spread);
            glUniform1f(U("uRolloff"),  g_af.rolloff);
            glUniform1f(U("uFlatness"), g_af.flatness);
            glUniform1f(U("uFlux"),     g_af.flux);
            glUniform1f(U("uFluxAtt"),  g_af.fluxAtt);
            if ((l = glGetUniformLocation(program, "uChroma")) >= 0)
                glUniform1fv(l, 12, g_af.chroma);
            glUniform1f(U("uCubeCamPhase"), g_cubeCamPhase);   // Cubeeffect1 相机 z（CPU 积分；无此 uniform 的 shader 忽略）
            // Cubeeffect1 form1：粘滴 uDrops[CUBE_DROP_N]（世界坐标+半径）、撞边闪光 uMarks[CUBE_MARK_MAX]
            {
                const int ND = CUBE_DROP_N, NM = CUBE_MARK_MAX;
                GLint lD = glGetUniformLocation(program, "uDrops[0]");
                if (lD >= 0) {
                    static std::vector<float> d4(ND * 4, 0.0f);
                    static std::vector<float> dd4(ND * 4, 0.0f);   // 粘滴速度方向（拉长形用）
                    int n = (int)g_cubeDrops.size();
                    for (int i = 0; i < ND; ++i) {
                        if (i < n) { const auto& d = g_cubeDrops[i];
                            // shader 相机 ro=vec3(0.35,0.32,uCubeCamPhase)，CPU 存世界坐标 → 转偏移 ro+off=world
                            d4[i*4+0] = d.x - 0.35f; d4[i*4+1] = d.y - 0.32f; d4[i*4+2] = d.z - g_cubeCamPhase; d4[i*4+3] = d.r;
                            dd4[i*4+0] = d.vx; dd4[i*4+1] = d.vy; dd4[i*4+2] = 0.0f; dd4[i*4+3] = 0.0f; }
                        else { d4[i*4+0]=0; d4[i*4+1]=0; d4[i*4+2]=0; d4[i*4+3]=0;
                               dd4[i*4+0]=1; dd4[i*4+1]=0; dd4[i*4+2]=0; dd4[i*4+3]=0; }
                    }
                    glUniform4fv(lD, ND, d4.data());
                    GLint lDd = glGetUniformLocation(program, "uDropDir[0]");
                    if (lDd >= 0) glUniform4fv(lDd, ND, dd4.data());
                }
                GLint lM = glGetUniformLocation(program, "uMarks[0]");
                if (lM >= 0) {
                    static std::vector<float> m4(NM * 4, 0.0f);
                    int nm = (int)g_cubeMarks.size();
                    for (int i = 0; i < NM; ++i) {
                        if (i < nm) { const auto& m = g_cubeMarks[i];
                            m4[i*4+0] = m.x; m4[i*4+1] = m.y; m4[i*4+2] = m.z;
                            m4[i*4+3] = (float)m.axis + 3.0f*m.flash; }   // w: 轴向(1沿x/2沿y) + 3×闪光强度；shader 解包
                        else { m4[i*4+0]=0; m4[i*4+1]=0; m4[i*4+2]=0; m4[i*4+3]=-1.0f; }
                    }
                    glUniform4fv(lM, NM, m4.data());
                }
                // Cubeeffect3：迷宫锚点 uVeil[N]（xyz=世界坐标, w=spawnT）、uVeilDir[N]（xyz=方向, w=幅度）
                //   注意：Cubeeffect3 现已走 mesh 路径（RenderLines 自行上传），此处仅供
                //   声明了 uVeil 的全屏 shader 效果使用。
                GLint lV = glGetUniformLocation(program, "uVeil[0]");
                if (lV >= 0) {
                    static std::vector<float> v4(C3_VEIL_MAX * 4, 0.0f);
                    static std::vector<float> d4v(C3_VEIL_MAX * 4, 0.0f);
                    int nv = (int)g_cubeVeils.size();
                    for (int i = 0; i < C3_VEIL_MAX; ++i) {
                        if (i < nv) { const auto& v = g_cubeVeils[i];
                            v4[i*4+0] = v.x; v4[i*4+1] = v.y; v4[i*4+2] = v.z; v4[i*4+3] = v.spawnT;
                            d4v[i*4+0] = v.dx; d4v[i*4+1] = v.dy; d4v[i*4+2] = v.dz; d4v[i*4+3] = 1.0f; }   // 幅度 1.0，shader 内 × g_veilAmp
                        else { v4[i*4+0]=0; v4[i*4+1]=0; v4[i*4+2]=0; v4[i*4+3]=0.0f;
                               d4v[i*4+0]=0; d4v[i*4+1]=0; d4v[i*4+2]=0; d4v[i*4+3]=0.0f; }
                    }
                    glUniform4fv(lV, C3_VEIL_MAX, v4.data());
                    GLint lVd = glGetUniformLocation(program, "uVeilDir[0]");
                    if (lVd >= 0) glUniform4fv(lVd, C3_VEIL_MAX, d4v.data());
                }
            }
        }
        // B376 烘焙覆盖场：VTracer 全部轮廓光栅化成 GL_R8 场贴图（启动时解码一次，shader 每帧 1 次采样）
        {
            GLint l;
            if ((l = glGetUniformLocation(program, "uField")) >= 0) {
                if (!g_b376FieldTex || !glIsTexture(g_b376FieldTex)) {
                    std::vector<unsigned char> f(kB376FieldW * kB376FieldH);
                    DecodeB376Field(f.data());
                    glGenTextures(1, &g_b376FieldTex);
                    glBindTexture(GL_TEXTURE_2D, g_b376FieldTex);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kB376FieldW, kB376FieldH, 0, GL_RED, GL_UNSIGNED_BYTE, f.data());
                    glBindTexture(GL_TEXTURE_2D, 0);
                }
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, g_b376FieldTex);
                glUniform1i(l, 2);
            }
            // 表层明暗场（参考图亮度，与覆盖场同网格）→ TEXTURE3
            if ((l = glGetUniformLocation(program, "uLum")) >= 0) {
                if (!g_b376LumTex || !glIsTexture(g_b376LumTex)) {
                    std::vector<unsigned char> f(kB376FieldW * kB376FieldH);
                    DecodeB376Lum(f.data());
                    glGenTextures(1, &g_b376LumTex);
                    glBindTexture(GL_TEXTURE_2D, g_b376LumTex);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kB376FieldW, kB376FieldH, 0, GL_RED, GL_UNSIGNED_BYTE, f.data());
                    glBindTexture(GL_TEXTURE_2D, 0);
                }
                glActiveTexture(GL_TEXTURE3);
                glBindTexture(GL_TEXTURE_2D, g_b376LumTex);
                glUniform1i(l, 3);
            }
            // B376 运动单元 ID 场（调试红边用，静态上传一次）
            if ((l = glGetUniformLocation(program, "uPartId")) >= 0) {
                if (!g_b376IdTex || !glIsTexture(g_b376IdTex)) {
                    std::vector<unsigned short> ids(kB376FieldW * kB376FieldH);
                    DecodeB376Id(ids.data());
                    std::vector<float> f(kB376FieldW * kB376FieldH);
                    for (size_t i = 0; i < f.size(); ++i) f[i] = (float)ids[i];
                    glGenTextures(1, &g_b376IdTex);
                    glBindTexture(GL_TEXTURE_2D, g_b376IdTex);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, kB376FieldW, kB376FieldH, 0, GL_RED, GL_FLOAT, f.data());
                    glBindTexture(GL_TEXTURE_2D, 0);
                }
                glActiveTexture(GL_TEXTURE4);
                glBindTexture(GL_TEXTURE_2D, g_b376IdTex);
                glUniform1i(l, 4);
            }
            // B376 灰绿背景静态掩码（调试绿边用，静态上传一次）
            if ((l = glGetUniformLocation(program, "uStatic")) >= 0) {
                if (!g_b376StaticTex || !glIsTexture(g_b376StaticTex)) {
                    std::vector<unsigned char> f(kB376FieldW * kB376FieldH);
                    DecodeB376Static(f.data());
                    glGenTextures(1, &g_b376StaticTex);
                    glBindTexture(GL_TEXTURE_2D, g_b376StaticTex);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kB376FieldW, kB376FieldH, 0, GL_RED, GL_UNSIGNED_BYTE, f.data());
                    glBindTexture(GL_TEXTURE_2D, 0);
                }
                glActiveTexture(GL_TEXTURE5);
                glBindTexture(GL_TEXTURE_2D, g_b376StaticTex);
                glUniform1i(l, 5);
            }
            // B376 轮廓弧长参数场（描边流光用，静态上传一次）
            if ((l = glGetUniformLocation(program, "uArc")) >= 0) {
                if (!g_b376ArcTex || !glIsTexture(g_b376ArcTex)) {
                    std::vector<unsigned char> f(kB376FieldW * kB376FieldH);
                    DecodeB376Arc(f.data());
                    glGenTextures(1, &g_b376ArcTex);
                    glBindTexture(GL_TEXTURE_2D, g_b376ArcTex);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kB376FieldW, kB376FieldH, 0, GL_RED, GL_UNSIGNED_BYTE, f.data());
                    glBindTexture(GL_TEXTURE_2D, 0);
                }
                glActiveTexture(GL_TEXTURE6);
                glBindTexture(GL_TEXTURE_2D, g_b376ArcTex);
                glUniform1i(l, 6);
            }
            // 飞船贴图（Wormtunnel）：shader 声明了 uShipTex 才绑，单元 7
            if ((l = glGetUniformLocation(program, "uShipTex")) >= 0) {
                if (!g_shipTex || !glIsTexture(g_shipTex)) {
                    wchar_t sp[MAX_PATH];
                    _snwprintf(sp, MAX_PATH, L"%sWormtunnel_ship.bmp", g_shaderDir);
                    int tw = 0, th = 0;
                    unsigned char* px = LoadBMP32(sp, &tw, &th);
                    if (px) {
                        glGenTextures(1, &g_shipTex);
                        glBindTexture(GL_TEXTURE_2D, g_shipTex);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);   // 不用 mipmap：工程没声明 glGenerateMipmap
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
                        glBindTexture(GL_TEXTURE_2D, 0);
                        delete[] px;
                    }
                }
                glActiveTexture(0x84C7);   // 0x84C7 = GL_TEXTURE7
                glBindTexture(GL_TEXTURE_2D, g_shipTex);
                glUniform1i(l, 7);
            }
            // B376 液态流动：每帧 CPU 前向散射重算位移场（覆盖+明暗+弧长）并上传
            // 每单体沿自己中心处的液态流轨迹独立平移（刚体），速度接 uSpeed，幅度接滑杆
            {
                static std::vector<unsigned char> dcov(kB376FieldW * kB376FieldH);
                static std::vector<unsigned char> dlum(kB376FieldW * kB376FieldH);
                static std::vector<unsigned char> darc(kB376FieldW * kB376FieldH);
                B376DisplaceFields(t, speed, g_b376FlowAmp, dcov.data(), dlum.data(), darc.data());
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, g_b376FieldTex);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kB376FieldW, kB376FieldH, GL_RED, GL_UNSIGNED_BYTE, dcov.data());
                glActiveTexture(GL_TEXTURE3);
                glBindTexture(GL_TEXTURE_2D, g_b376LumTex);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kB376FieldW, kB376FieldH, GL_RED, GL_UNSIGNED_BYTE, dlum.data());
                glActiveTexture(GL_TEXTURE6);
                glBindTexture(GL_TEXTURE_2D, g_b376ArcTex);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kB376FieldW, kB376FieldH, GL_RED, GL_UNSIGNED_BYTE, darc.data());
            }
            glUniform1i(U("uDebugParts"), g_debugParts ? 1 : 0);
            glUniform1f(U("uFlowAmp"), g_b376FlowAmp);       // 液态流动幅度（固定 0.5）
            B376::SetFlowAmp(g_b376FlowAmp);                 // 同步 CPU 版
        }
        // 网格效果（B376/M2/M3）自带 movement，不走 BindUniforms；此处无额外 uniform
    }
};

// ===== 效果名表 —— 唯一真源 =================================================
// 历史上这里是三个各自独立的数：g_effects[容量] / LoadEffects 里的 names[] /
// 循环上界 `i < 上界`。加一个效果要改三处，漏一处就越界。
// 2026-09-30 加 QKnopic 时就是这么炸的：names 与循环改到 22、容量还停在 21
//   → 写 g_effects[21] 越界 → 启动即 0xC0000005（访问违规）。
// 现在容量从这张表本身派生 —— 加效果【只需要在下面加一行名字】。
static const wchar_t* const kEffectNames[] = {
    L"Psychedelic", L"GlitchForge", L"Verdigris", L"Azurite", L"Azurite_c",
    L"MTH", L"MTH_C",
    L"Metalheart1", L"Metalheart2", L"Metalheart3", L"Metalheart4",
    L"Cubeeffect1", L"Cubeeffect3 (dark)", L"Cubeeffect3 (light)",
    L"StellarVortex", L"Involute", L"Involute_1", L"Function1",
    L"Filigree", L"Shardstorm", L"Quicksilver", L"QKnopic",
    L"rollingsolarsystem", L"blackhole1", L"Wormtunnel",
    L"Loxodrome", L"Pulseblade", L"Indigoflare", L"Indigoflare1", L"Hyperpush",
    L"Rosette", L"Sunslit", L"Starbloom", L"Starbloom2", L"Sporefield",
    L"Silkwheel", L"Marbleflow", L"Hitpetal", L"Hitpulse",
};
static constexpr int kEffectCapacity =
    (int)(sizeof(kEffectNames) / sizeof(kEffectNames[0]));
static GpuEffect g_effects[kEffectCapacity];
static int g_effectCount = 0;
static GLuint g_vao = 0;
static GLuint g_oldFbo = 0, g_oldTex = 0, g_oldDepth = 0;
static GLuint g_blendFbo = 0, g_blendTex = 0, g_blendDepth = 0;  // 上层效果纹理
static GLuint g_blendProg = 0;  // 混合 shader

// 混合 shader 的 GLSL 只有这一份。以前它在"玩家窗口 WM_CREATE"和"WinMain 启动时的
// 临时 OpenGL 上下文"各写了一遍（连 15 行 GLSL 源都复制了），改一处忘一处就会出现
// "播放时和主窗口里表现不一样"。
// showErrors=false 用在启动期的临时上下文：那里失败还弹框会挡在用户面前，
// 真正的错误诊断留给玩家窗口那一次。
static const char* const kBlendFS =
    "#version 330 core\n"
    "in vec2 vUV;out vec4 c;"
    "uniform sampler2D uBase,uOverlay;uniform int uMode;uniform float uOpacity;"
    "float ch(float a,float b,int m){"
    "if(m==1)return max(a,b);"
    "if(m==2)return 1.0-(1.0-a)*(1.0-b);"
    "if(m==3)return a<0.5?2.0*a*b:1.0-2.0*(1.0-a)*(1.0-b);"
    "if(m==4)return b<0.5?2.0*a*b:1.0-2.0*(1.0-a)*(1.0-b);"
    "if(m==5){if(b<0.5)return a-(1.0-2.0*b)*a*(1.0-a);else{float g=a<=0.25?((16.0*a-12.0)*a+4.0)*a:sqrt(a);return a+(2.0*b-1.0)*(g-a);}}"
    "if(m==6)return a+b-1.0;"
    "if(m==7)return a*b;"
    "return b;}"
    "void main(){"
    "vec4 a=texture(uBase,vUV),b=texture(uOverlay,vUV);"
    "float mr=ch(a.r,b.r,uMode),mg=ch(a.g,b.g,uMode),mb=ch(a.b,b.b,uMode);"
    "vec3 bl=clamp(vec3(mr,mg,mb),0.0,1.0);float eb=b.a*uOpacity;"
    "float onlyA=a.a*(1.0-eb);float onlyB=eb*(1.0-a.a);float both=a.a*eb;"
    "vec3 res=a.rgb*onlyA+b.rgb*onlyB+bl*both;float tot=onlyA+onlyB+both;"
    "c=vec4(a.rgb*(1.0-tot)+res,1.0);}";

static void CompileBlendProgram(bool showErrors) {
    g_blendProg = CompileProgram(kFullscreenVS, kBlendFS, "Blend");
    if (!g_blendProg && showErrors)
        MessageBoxA(nullptr, g_lastShaderLog, "Blend Shader Error", MB_OK);
}

static int g_fboW = 0, g_fboH = 0;
// 最近一次实际渲染的客户区尺寸（WM_PAINT 里更新）。锚点铺设范围要按【当前窗口
// 宽高比】算可见世界范围，不能用固定值 —— 否则改窗口大小后扭曲只覆盖旧矩形。
static int g_clientW = 0, g_clientH = 0;
static int g_blendMode = 0;
static float g_blendOpacity = 1.0f;

// ===== 帧反馈 warp（MilkDrop 式【逐顶点网格】warp；'F' 切换）=====
//
// 2026-10-12 重做。原来是【全屏逐像素噪声位移 + 45% 对半混合】，出来是一片高频的
// 糊、没有流动。对照 MilkDrop 3.36 的 data/warp_vs.fx + warp_ps.fx：
//   · 它的位移是【逐顶点】的（warped UV 在顶点属性里传进来）→ 顶点间插值，大尺度、连贯
//   · 参数是一整套 zoom / zoomexp / rot / warp / stretch / cx,cy / dx,dy（见它的
//     docs/milkdrop_preset_authoring.html）
//   · 衰减是【减法】的：warp_ps.fx 里就一句 `ret -= 0.004`
// 这三样现在都照搬过来了。
static bool   g_feedbackOn = true;
static float  g_feedbackMix = 0.80f;      // 残影强度 0..1（0=纯当前帧，1=拖影全强度）
static float  g_feedbackWarp = 0.60f;     // warp 强度 0..2（MilkDrop 的 warp 参数语义）
static float  g_feedbackDecay = 0.004f;   // 每帧减法衰减（照 MilkDrop：ret -= 0.004）
static GLuint g_fbCurTex = 0;             // 当前帧拷贝（glCopyTexSubImage2D 目标）
static GLuint g_fbPrevFbo = 0, g_fbPrevTex = 0;   // ping-pong A
static GLuint g_fbNextFbo = 0, g_fbNextTex = 0;   // ping-pong B
static int    g_fbW = 0, g_fbH = 0;
static GLuint g_feedbackProg = 0;

// warp 网格：32×24 个格子。每顶点只存归一化 uv，位移全在顶点着色器里现算 ——
// 网格建一次，之后每帧只换 uniform。必须 glDrawElements 画（顶点是共享的，
// 走 DrawArrays 会全是退化三角形，F1Geom.h L84-87 记着这个坑）。
static const int kFbMeshX = 32, kFbMeshY = 24;
static GLuint  g_fbMeshVao = 0, g_fbMeshVbo = 0, g_fbMeshEbo = 0;
static GLsizei g_fbMeshICount = 0;

static void InitVAO() {
    float v[] = {-1,-1,0,0, 1,-1,1,0, -1,1,0,1, 1,-1,1,0, -1,1,0,1, 1,1,1,1};
    glGenVertexArrays(1, &g_vao); GLuint vbo;
    glGenBuffers(1, &vbo); glBindVertexArray(g_vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void*)8); glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

static void EnsureFBO(int w, int h) {
    if (w == g_fboW && h == g_fboH) return;
    if (g_oldFbo) { glDeleteFramebuffers(1, &g_oldFbo); glDeleteTextures(1, &g_oldTex); glDeleteTextures(1, &g_oldDepth); }
    if (g_blendFbo) { glDeleteFramebuffers(1, &g_blendFbo); glDeleteTextures(1, &g_blendTex); glDeleteTextures(1, &g_blendDepth); }
    g_fboW = w; g_fboH = h;

    auto makeFBO = [](GLuint& fbo, GLuint& tex, GLuint& depthTex) {
        glGenFramebuffers(1, &fbo); glGenTextures(1, &tex); glGenTextures(1, &depthTex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_fboW, g_fboH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        // 深度附件：mesh 效果在混合模式 Pass1/2 渲染到这些 FBO，需要 3D 前后遮挡
        glBindTexture(GL_TEXTURE_2D, depthTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, g_fboW, g_fboH, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex, 0);
        GLenum fbStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (fbStatus != 0x8CD5) { // GL_FRAMEBUFFER_COMPLETE
            char msg[64]; snprintf(msg, 64, "FBO incomplete: 0x%X", fbStatus);
            MessageBoxA(0, msg, "FBO Error", MB_OK);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    };
    makeFBO(g_oldFbo, g_oldTex, g_oldDepth);
    makeFBO(g_blendFbo, g_blendTex, g_blendDepth);
}

// 当前绑定的帧缓冲是否有深度附件（默认帧缓冲有 24 位深度；我们自己的混合 FBO 也挂深度纹理）
static bool CurrentFboHasDepth() {
    GLint curFbo = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &curFbo);
    if (curFbo == 0) return true;   // 默认帧缓冲
    GLint type = GL_NONE;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
    return type != GL_NONE;
}

// ===== B376 顶点网格渲染（Blender 同原理：轮廓网格 + 顶点级液态边缘流动）=====
static GLuint g_b376MeshVao = 0, g_b376MeshProg = 0;

// kB376MeshVS / kB376MeshFS 两段着色器已移到 VisualEffects/B376/B376Shaders.h（顶部 include 进来了）。

// 确保明暗贴图（参考图亮度场）
static void EnsureB376LumTex() {
    if (g_b376LumTex && glIsTexture(g_b376LumTex)) return;
    std::vector<unsigned char> f(kB376FieldW * kB376FieldH);
    DecodeB376Lum(f.data());
    // 灰度 → RGB 通道复制（FS 统一 .rgb 采样，B376 保持灰度观感）
    std::vector<unsigned char> rgb(kB376FieldW * kB376FieldH * 3);
    for (size_t i = 0; i < f.size(); ++i) { rgb[i*3] = f[i]; rgb[i*3+1] = f[i]; rgb[i*3+2] = f[i]; }
    glGenTextures(1, &g_b376LumTex);
    glBindTexture(GL_TEXTURE_2D, g_b376LumTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, kB376FieldW, kB376FieldH, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

// 确保曲线纹理（曲线流 movement，行=单体、列=弧长采样，RGBA32F）
static void EnsureB376CurveTex() {
    if (g_b376CurveTex && glIsTexture(g_b376CurveTex)) return;
    glGenTextures(1, &g_b376CurveTex);
    glBindTexture(GL_TEXTURE_2D, g_b376CurveTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);      // 弧长环绕
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, kB376CurveW, kB376CurveH, 0,
                 GL_RGBA, GL_FLOAT, kB376Curves);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// 网格顶点布局（B376 与 M2/M3/M4 共用同一份 VS，所以布局也共用一份）：
// 每顶点 12 个 float = pos2 + uv2 + 8 个标量（见 B376Shaders.h 的 VS 声明）。
// 两个渲染器以前各抄了一遍这 11 行，改了布局要记得改两处。
static void SetupMeshAttribs() {
    const size_t stride = 12 * sizeof(float);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2*sizeof(float)));
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (void*)(4*sizeof(float)));
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, (void*)(5*sizeof(float)));
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride, (void*)(6*sizeof(float)));
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, stride, (void*)(7*sizeof(float)));
    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, stride, (void*)(8*sizeof(float)));
    glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, stride, (void*)(9*sizeof(float)));   // s 弧长
    glVertexAttribPointer(8, 1, GL_FLOAT, GL_FALSE, stride, (void*)(10*sizeof(float)));  // d 距离
    glVertexAttribPointer(9, 1, GL_FLOAT, GL_FALSE, stride, (void*)(11*sizeof(float)));  // m 曲线行
    for (int i = 0; i < 10; ++i) glEnableVertexAttribArray(i);
}

static void EnsureB376Mesh() {
    if (g_b376MeshVao) return;
    EnsureB376LumTex();
    glGenVertexArrays(1, &g_b376MeshVao);
    glBindVertexArray(g_b376MeshVao);
    GLuint vbo, ibo;
    glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kB376MeshV), kB376MeshV, GL_STATIC_DRAW);
    glGenBuffers(1, &ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kB376MeshI), kB376MeshI, GL_STATIC_DRAW);
    SetupMeshAttribs();
    glBindVertexArray(0);
}

static void RenderB376Mesh(float t, float speed, float w, float h, bool darkBg, float opacity) {
    EnsureB376Mesh();
    EnsureB376CurveTex();
    if (!g_b376MeshProg) g_b376MeshProg = CompileProgram(kB376MeshVS, kB376MeshFS, "B376 Mesh");
    if (!g_b376MeshProg) return;
    glUseProgram(g_b376MeshProg);
    // 深度缓冲可用性：默认帧缓冲有 24 位深度；混合模式的 FBO 也挂深度纹理（EnsureFBO）
    // 按"当前帧缓冲有无深度附件"决定是否开深度测试（mesh 需要 3D 前后遮挡）
    bool useDepth = CurrentFboHasDepth();
    if (useDepth) { glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glClearDepth(1.0f); glClear(GL_DEPTH_BUFFER_BIT); }
    glUniform1f(glGetUniformLocation(g_b376MeshProg, "uTime"), t);
    glUniform1f(glGetUniformLocation(g_b376MeshProg, "uSpeed"), speed);
    glUniform1f(glGetUniformLocation(g_b376MeshProg, "uFlowAmp"), g_b376FlowAmp);
    glUniform1i(glGetUniformLocation(g_b376MeshProg, "uDarkBg"), darkBg ? 1 : 0);
    glUniform1f(glGetUniformLocation(g_b376MeshProg, "uOpacity"), opacity);
    glUniform1f(glGetUniformLocation(g_b376MeshProg, "uAspect"), (w > 0 && h > 0) ? (w / h) : 1.777f);
    glUniform1f(glGetUniformLocation(g_b376MeshProg, "uImgA"), 0.75f);   // B376 图像宽高比（3:4）
    glUniform1f(glGetUniformLocation(g_b376MeshProg, "uFitK"), 3.5f);   // B376 保持原样：按整图 3:4 贴窗口
    glUniform1f(glGetUniformLocation(g_b376MeshProg, "uFitM"), 3.5f * 0.75f);
    glUniform1f(glGetUniformLocation(g_b376MeshProg, "uExitTime"), g_plExitT);   // 出场计时（切出动画）
    glUniform1f(glGetUniformLocation(g_b376MeshProg, "uFlowPhase"), g_b376FlowPhase);   // 流动相位（CPU 实时累计）
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, g_b376LumTex);
    glUniform1i(glGetUniformLocation(g_b376MeshProg, "uLum"), 3);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, g_b376CurveTex);
    glUniform1i(glGetUniformLocation(g_b376MeshProg, "uCurves"), 4);
    glBindVertexArray(g_b376MeshVao);
    glDrawElements(GL_TRIANGLES, kB376MeshInds, GL_UNSIGNED_SHORT, 0);
    glBindVertexArray(0);
    if (useDepth) glDisable(GL_DEPTH_TEST);
}

// ===== 通用单体网格渲染（M2/M3：复用 B376 同款 shader，仅数据不同）=====
struct MeshEffectData {
    const float* meshV; int meshVerts;
    const unsigned short* meshI; int meshInds;
    const float* curves; int curveW, curveH;
    const unsigned char* lum; int lumW, lumH;
    float imgA;   // 图像宽高比 W/H（世界布局与 letterbox 用）
};
struct MeshRenderer {
    GLuint vao = 0, prog = 0, lumTex = 0, curveTex = 0;
    float flowPhase = 0.0f;   // 流动相位（CPU 实时累计，音频变速不跳变）
    float fitK = 0.0f, fitM = 0.0f;   // 内容 bbox 的 contain-fit 缩放（EnsureMeshRenderer 时按顶点投影范围算出）
};
static MeshRenderer g_m2Renderer, g_m3Renderer, g_m4Renderer;
static const MeshEffectData kM2Data = {
    &kM2MeshV[0][0], kM2MeshVerts, kM2MeshI, kM2MeshInds,
    &kM2Curves[0][0][0], kM2CurveW, kM2CurveH,
    kM2Lum, kM2LumW, kM2LumH, kM2ImgA
};
static const MeshEffectData kM3Data = {
    &kM3MeshV[0][0], kM3MeshVerts, kM3MeshI, kM3MeshInds,
    &kM3Curves[0][0][0], kM3CurveW, kM3CurveH,
    kM3Lum, kM3LumW, kM3LumH, kM3ImgA
};
static const MeshEffectData kM4Data = {
    &kM4MeshV[0][0], kM4MeshVerts, kM4MeshI, kM4MeshInds,
    &kM4Curves[0][0][0], kM4CurveW, kM4CurveH,
    kM4Lum, kM4LumW, kM4LumH, kM4ImgA
};
// 播放链窗口重建 GL 上下文时清句柄，强制重建（否则二次播放用失效句柄闪退，同 B376 的处理）
static void ResetMeshRenderer(MeshRenderer& mr) { mr.vao = 0; mr.prog = 0; mr.lumTex = 0; mr.curveTex = 0; }

// 初始化一个网格渲染器（VAO/VBO/EBO + 亮度/曲线纹理 + shader）
static void EnsureMeshRenderer(MeshRenderer& mr, const MeshEffectData& d) {
    if (mr.vao) return;
    // 内容 bbox 的 contain-fit 缩放：扫所有顶点投影后的实际范围（含深度透视），让单体占满窗口且不裁内容
    {
        const float camZ = 3.5f;
        float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
        for (int i = 0; i < d.meshVerts; ++i) {
            const float* r = d.meshV + (size_t)i * 12;
            float X = (r[0] - 0.5f) * 2.0f;                    // 图像坐标 → 世界 x
            float Y = (r[1] - 0.5f) * 2.0f / d.imgA;           // → 世界 y（保持图像宽高比）
            float dist = camZ - r[8];                          // 深度 zHome 透视距离
            float xn = X / dist, yn = Y / dist;                // 投影后（除以 k/m 前的 NDC 量）
            if (xn < minX) minX = xn; if (xn > maxX) maxX = xn;
            if (yn < minY) minY = yn; if (yn > maxY) maxY = yn;
        }
        float bw = maxX - minX, bh = maxY - minY;
        mr.fitK = (bw > 1e-5f) ? 2.0f / bw : camZ;             // 贴满宽度的 k
        mr.fitM = (bh > 1e-5f) ? 2.0f / bh : camZ * d.imgA;    // 贴满高度的 m
    }
    // 亮度贴图
    glGenTextures(1, &mr.lumTex);
    glBindTexture(GL_TEXTURE_2D, mr.lumTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, d.lumW, d.lumH, 0, GL_RGB, GL_UNSIGNED_BYTE, d.lum);
    glBindTexture(GL_TEXTURE_2D, 0);
    // 曲线纹理
    glGenTextures(1, &mr.curveTex);
    glBindTexture(GL_TEXTURE_2D, mr.curveTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);      // 弧长环绕
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, d.curveW, d.curveH, 0, GL_RGBA, GL_FLOAT, d.curves);
    glBindTexture(GL_TEXTURE_2D, 0);
    // 网格 VAO/VBO/EBO
    glGenVertexArrays(1, &mr.vao);
    glBindVertexArray(mr.vao);
    GLuint vbo, ibo;
    glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (size_t)d.meshVerts * 12 * sizeof(float), d.meshV, GL_STATIC_DRAW);
    glGenBuffers(1, &ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (size_t)d.meshInds * sizeof(unsigned short), d.meshI, GL_STATIC_DRAW);
    SetupMeshAttribs();
    glBindVertexArray(0);
    // shader（与 B376 同一份）
    if (!mr.prog) mr.prog = CompileProgram(kB376MeshVS, kB376MeshFS, "B376 Mesh");
}

// 渲染一个网格效果（深度缓冲按 framebuffer 有无深度自动开关，修黑底 resize 消失）
static void RenderMesh(MeshRenderer& mr, const MeshEffectData& d, float t, float speed, float w, float h, bool darkBg, float opacity) {
    EnsureMeshRenderer(mr, d);
    if (!mr.prog) return;
    glUseProgram(mr.prog);
    // 深度缓冲可用性：默认帧缓冲有 24 位深度；混合模式的 FBO 也挂深度纹理（EnsureFBO）
    // 按"当前帧缓冲有无深度附件"决定是否开深度测试（mesh 需要 3D 前后遮挡）
    bool useDepth = CurrentFboHasDepth();
    if (useDepth) { glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glClearDepth(1.0f); glClear(GL_DEPTH_BUFFER_BIT); }
    glUniform1f(glGetUniformLocation(mr.prog, "uTime"), t);
    glUniform1f(glGetUniformLocation(mr.prog, "uSpeed"), speed);
    glUniform1f(glGetUniformLocation(mr.prog, "uFlowAmp"), g_b376FlowAmp);
    glUniform1i(glGetUniformLocation(mr.prog, "uDarkBg"), darkBg ? 1 : 0);
    glUniform1f(glGetUniformLocation(mr.prog, "uOpacity"), opacity);
    glUniform1f(glGetUniformLocation(mr.prog, "uAspect"), (w > 0 && h > 0) ? (w / h) : 1.777f);
    glUniform1f(glGetUniformLocation(mr.prog, "uImgA"), d.imgA);
    glUniform1f(glGetUniformLocation(mr.prog, "uFitK"), mr.fitK);
    glUniform1f(glGetUniformLocation(mr.prog, "uFitM"), mr.fitM);
    glUniform1f(glGetUniformLocation(mr.prog, "uExitTime"), g_plExitT);
    glUniform1f(glGetUniformLocation(mr.prog, "uFlowPhase"), mr.flowPhase);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, mr.lumTex);
    glUniform1i(glGetUniformLocation(mr.prog, "uLum"), 3);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, mr.curveTex);
    glUniform1i(glGetUniformLocation(mr.prog, "uCurves"), 4);
    glBindVertexArray(mr.vao);
    glDrawElements(GL_TRIANGLES, d.meshInds, GL_UNSIGNED_SHORT, 0);
    glBindVertexArray(0);
    if (useDepth) glDisable(GL_DEPTH_TEST);
}

// ===== Cubeeffect3：线段几何渲染（after.png 矢量化 → GL_LINES）=====
// 每条线段两顶点；VS 里端点各自做锚点位移 → 线段仍是直线段但端点错开(= 四边形被重排)。
// FS 用屏幕空间距离把 1px 线扩成可控宽度 + 轻微发光。程序内含"背景灰黑底"层(uIsBg=1)。
struct LineRenderer { GLuint vao = 0, prog = 0; };
static LineRenderer g_c3Renderer;
// ===== 折纸片系统（几何算法与注释见 RenderLines 之前那一节）=====
// C3_ORIGAMI_* 参数已移到 VisualEffects/C3/C3OrigamiGeom.h —— GPU 与 CPU 两个后端共用一份。

static std::vector<Origami> g_c3Origami;
static std::vector<int>     g_c3OrigamiPool;       // 可放折纸的格子索引（首次用时筛一遍）
static bool                 g_c3OrigamiPoolReady = false;
static std::vector<float>   g_c3OrigData;          // 每帧重建的顶点 [x,y,kindCode]
static int                  g_c3OrigFillVerts = 0; // 前 N 个顶点是纸面三角，其余是折痕线
// 每帧重建的动态顶点缓冲（纸面三角 + 折痕线，与线段共用同一个 program）
struct C3OrigRenderer { GLuint vao = 0, vbo = 0; };
static C3OrigRenderer g_c3OrigR;
// Cubeeffect3 层前进相位（CPU 积分；速度∝音频能量 → 静音=0 不前进）。镜头慢慢前推 → 后层浮现。
static float g_c3LayerPhase = 0.0f;
static C3TrebRefTracker g_c3TrebRef;   // 镜头速度的满量程自适应（见 C3Veil.h）
static const float kC3LayerSpan = 2.4f - (-6.0f);   // 与 shader 的 NEAR_Z - FAR_Z 一致

// kC3LineVS / kC3LineFS 两段着色器已移到 VisualEffects/C3/C3Shaders.h（顶部 include 进来了）。

static void ResetC3LineRenderer(LineRenderer& lr) {
    lr.vao = 0; lr.prog = 0;
    // GL 上下文重建 → 折纸片的 VAO/VBO 句柄同样失效，必须清零强制重建
    g_c3OrigR.vao = 0; g_c3OrigR.vbo = 0;
    g_c3Origami.clear(); g_c3OrigData.clear(); g_c3OrigFillVerts = 0;
    // 锚点也一起清：PlaylistStart 会把 g_plTime 归零，留着旧时间轴的 spawnT 就是同一个爆炸源
    g_cubeVeils.clear();
}

static void EnsureC3LineRenderer(LineRenderer& lr) {
    if (lr.vao) return;
    glGenVertexArrays(1, &lr.vao);
    glBindVertexArray(lr.vao);
    GLuint vbo;
    glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kC3V), kC3V, GL_STATIC_DRAW);
    // 数据布局: 每顶点 3 float = [x, y, axis]（12 字节）。
    // 连续排布正好使 glDrawArrays(GL_LINES, 2*N) 把 (v0,v1)=段0、(v2,v3)=段1 …
    size_t stride = 3 * sizeof(float);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);               // x,y
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));  // axis
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    if (!lr.prog) lr.prog = CompileProgram(kC3LineVS, kC3LineFS, "C3 Line", "C3_shader_log.txt");
}

// ============================================================================
// ===== Cubeeffect3 折纸片系统 =====
// ----------------------------------------------------------------------------
// 单个鼓点输出一批折纸片。每一片：
//   · 落在【随机一个格子的面积重心】上 —— 这就是纸片"双次对折"的折痕交点；
//   · 形状 = 该格子轮廓【向内缩 C3_ORIGAMI_INSET 画布像素】→ 与所在格子边界保持固定像素距离；
//   · 两条折痕 = 双次对折的痕，过重心，方向取该格子两组"对边中点连线"（对折就是沿中线折）。
//     四边形时这两条中线正好交于重心；五边形没有中线概念，退化为顶点主轴。
//   · 显化动画：整张纸连同折痕一起【从折痕交点向外放大】（ease-out），末尾淡出。
//
// 纸片走与网格线完全相同的渲染路径（同 shader / 同相机 / 同图层纵深推进 / 同鼓点扭曲场），
// 所以格子被扭曲时纸片跟着一起扭曲，不会和格子边线穿帮。
// 顶点编码：aKind 的【整数部分】=类型（0 网格线 / 1 坐标轴 / 2 纸面 / 3 折痕），
// 【小数部分】= 该顶点的透明度（网格线/轴不用，走深度淡入淡出）。见 kC3LineVS。
// ============================================================================
static float cubeRnd(int a, int b);   // 定义在后面（粘滴那节）

/// 把第 polyIdx 个格子做成一张折纸片（失败返回 false）
static bool C3MakeOrigami(int polyIdx, float now, float delay, Origami& o) {
    if (!C3MakeOrigamiShapeFromCell(polyIdx, C3_ORIGAMI_INSET, o)) return false;
    o.spawnT = now; o.delay = delay;
    o.killT = 0.0f;                 // 新片还活着（C3MakeOrigamiShape 不管生命周期字段）
    return true;
}

/// 筛出"放得下折纸"的格子（只做一次）。具体判定在 C3BuildOrigamiPool（共享头文件），
/// CPU 版用的是同一个函数 —— 两边格子池必须完全一致。
static void C3BuildOrigamiPoolOnce() {
    if (g_c3OrigamiPoolReady) return;
    g_c3OrigamiPoolReady = true;
    C3BuildOrigamiPool(g_c3OrigamiPool, C3_ORIGAMI_INSET);
}

/// 鼓点：上一批立刻作废 → 输出新的一批（同批内不重复用同一个格子）
static void C3SpawnOrigami(float now, unsigned seed) {
    C3BuildOrigamiPoolOnce();
    // 下一批来了 → 上一批全部开始淡出（killT 只设一次）
    for (size_t i = 0; i < g_c3Origami.size(); ++i)
        if (g_c3Origami[i].killT <= 0.0f) g_c3Origami[i].killT = now;

    const int NP = (int)g_c3OrigamiPool.size();
    if (NP <= 0) return;

    // 洗牌后取前 N 个 → 天然不重复，且是 O(n)（原来"随机 + 去重试 8 次"在 N=240 时又慢又可能凑不够）
    static std::vector<int> order;
    order.assign(g_c3OrigamiPool.begin(), g_c3OrigamiPool.end());
    for (int i = NP - 1; i > 0; --i) {
        int j = (int)(cubeRnd((int)(seed + (unsigned)i * 2654435761u), 9000 + i) * (float)(i + 1));
        if (j < 0) j = 0;
        if (j > i) j = i;
        int t = order[i]; order[i] = order[j]; order[j] = t;
    }
    int want = C3_ORIGAMI_PER_HIT;
    if (want > NP) want = NP;
    for (int k = 0; k < want; ++k) {
        if ((int)g_c3Origami.size() >= C3_ORIGAMI_MAX) break;
        Origami o;
        // 极小随机错开（0~80ms）：一大批同时长出来会太机械，错开一点更像"一片片展开"
        float dly = cubeRnd((int)(seed + (unsigned)k * 131u), 6000 + (k & 255)) * 0.08f;
        if (!C3MakeOrigami(order[k], now, dly, o)) continue;
        g_c3Origami.push_back(o);
    }
}

/// 每帧重建折纸片顶点（CPU 端算好显化动画 → shader 只管投影/扭曲/上色）
/// 顶点 = [x, y, kindCode, shade]（4 个 float）。shade 是所在面的明暗系数，
/// 折痕线用 1.0（折痕本身不参与面的明暗）。
static void C3BuildOrigamiGeometry(float now) {
    g_c3OrigData.clear();
    g_c3OrigFillVerts = 0;
    const float kx = 1.0f / C3CanvasW(), ky = 1.0f / kC3CanvasH;

    // 显化进度 / 透明度（两个 pass 都要用，抽成 lambda）
    auto anim = [&](const Origami& o, float& e, float& alpha) -> bool {
        float age = now - o.spawnT - o.delay;
        if (age <= 0.0f) return false;
        float p = age / C3_ORIGAMI_REVEAL; if (p > 1.0f) p = 1.0f;
        e = 1.0f - (1.0f - p) * (1.0f - p) * (1.0f - p);   // 从折痕交点向外放大
        float aIn  = (p < 0.001f) ? 0.0f : fminf(1.0f, p / 0.22f);
        float rest = C3_ORIGAMI_LIFE - age;
        float aOut = (rest >= C3_ORIGAMI_FADE) ? 1.0f : fmaxf(0.0f, rest / C3_ORIGAMI_FADE);
        alpha = aIn * aOut;
        // 被下一批 hit 顶掉 → 快速淡出（这是主要的消失方式，LIFE 只是兜底）
        if (o.killT > 0.0f) {
            float kf = 1.0f - (now - o.killT) / C3_ORIGAMI_KILL;
            if (kf <= 0.0f) return false;
            alpha *= kf;
        }
        // 上限必须压在 1.0 以下：alpha=1.0 会让编码变成 3.0，被 kC3LineVS 解码成"折痕"
        alpha = fminf(alpha, 0.98f);
        return alpha > 0.004f;
    };

    // ① 纸面：按【4 个面】分别扇形三角化 —— 三角形绝不跨面，各面带自己的明暗
    for (size_t i = 0; i < g_c3Origami.size(); ++i) {
        const Origami& o = g_c3Origami[i];
        float e, alpha;
        if (!anim(o, e, alpha)) continue;
        const float code = 2.0f + alpha;
        for (int s = 0; s < 4; ++s) {
            // pn==2 是合法面板：该扇区内没有轮廓顶点，面就是一个三角形（重心 + 两个折痕端点）。
            // 用 <3 会把这种面整个丢掉 → 纸片上出现空洞。
            if (o.pn[s] < 2) continue;
            const float sh = o.shade[s];
            for (int v = 0; v + 1 < o.pn[s]; ++v) {
                float x0 = o.cx + (o.px[s][v] - o.cx) * e,     y0 = o.cy + (o.py[s][v] - o.cy) * e;
                float x1 = o.cx + (o.px[s][v+1] - o.cx) * e,   y1 = o.cy + (o.py[s][v+1] - o.cy) * e;
                g_c3OrigData.push_back(o.cx * kx); g_c3OrigData.push_back(o.cy * ky);
                g_c3OrigData.push_back(code);      g_c3OrigData.push_back(sh);
                g_c3OrigData.push_back(x0 * kx);   g_c3OrigData.push_back(y0 * ky);
                g_c3OrigData.push_back(code);      g_c3OrigData.push_back(sh);
                g_c3OrigData.push_back(x1 * kx);   g_c3OrigData.push_back(y1 * ky);
                g_c3OrigData.push_back(code);      g_c3OrigData.push_back(sh);
            }
        }
    }
    g_c3OrigFillVerts = (int)(g_c3OrigData.size() / 4);   // 纸面顶点数（每顶点 4 个 float）

    // ② 折痕：和纸面一起从交点向外长出来
    // 折痕 = 纸面上的【折痕阴影】，不是高光亮线：亮度取四个面的平均值再压暗，
    // 于是它的明暗跟着所在纸片走（亮纸上的折痕也亮一些），像真的折痕一样是"同色更暗"。
    for (size_t i = 0; i < g_c3Origami.size(); ++i) {
        const Origami& o = g_c3Origami[i];
        float e, alpha;
        if (!anim(o, e, alpha)) continue;
        float sum = 0.0f; int cnt = 0;
        for (int s = 0; s < 4; ++s)
            if (o.pn[s] >= 2) { sum += o.shade[s]; ++cnt; }
        const float creaseShade = ((cnt > 0) ? (sum / (float)cnt) : 1.0f) * 0.34f;
        const float code = 3.0f + alpha;
        // 折痕必须【经过重心点】，所以分两段画：A→重心、重心→B。
        // 画成单段 A→B 的话，A/重心/B 三个顶点被锚点各自独立扭曲（扭曲是位置的非线性函数），
        // 直线 A'─B' 和面板边界的折线 A'→重心'→B' 就对不上 —— 四面的分界线会和折痕错开。
        // 多一个折点（4 个顶点而不是 2 个）代价可以忽略。
        for (int k = 0; k < 2; ++k) {
            float ax = o.cx - o.dx[k] * o.rb[k] * e, ay = o.cy - o.dy[k] * o.rb[k] * e;
            float bx = o.cx + o.dx[k] * o.rf[k] * e, by = o.cy + o.dy[k] * o.rf[k] * e;
            g_c3OrigData.push_back(ax * kx); g_c3OrigData.push_back(ay * ky);   // A
            g_c3OrigData.push_back(code);    g_c3OrigData.push_back(creaseShade);
            g_c3OrigData.push_back(o.cx * kx); g_c3OrigData.push_back(o.cy * ky);  // 重心
            g_c3OrigData.push_back(code);    g_c3OrigData.push_back(creaseShade);
            g_c3OrigData.push_back(o.cx * kx); g_c3OrigData.push_back(o.cy * ky);  // 重心
            g_c3OrigData.push_back(code);    g_c3OrigData.push_back(creaseShade);
            g_c3OrigData.push_back(bx * kx); g_c3OrigData.push_back(by * ky);   // B
            g_c3OrigData.push_back(code);    g_c3OrigData.push_back(creaseShade);
        }
    }
}

// 顶点布局与线段完全一致（vec2 位置 + 1 个 float 类型码），所以能共用同一个 program
static void C3UploadOrigamiGeometry() {
    if (g_c3OrigData.empty()) return;    // 没有折纸片 → 不碰缓冲（draw 那边也会跳过）
    if (!g_c3OrigR.vao) {
        glGenVertexArrays(1, &g_c3OrigR.vao);
        glGenBuffers(1, &g_c3OrigR.vbo);
        glBindVertexArray(g_c3OrigR.vao);
        glBindBuffer(GL_ARRAY_BUFFER, g_c3OrigR.vbo);
        // 折纸片顶点比线段多一个"面明暗"分量（stride 16 而不是 12）。
        // 线段那套 VAO 里 attrib 2 是关闭的 —— shader 只在 kind>=2 时才读它，
        // 所以关闭状态下拿到的常量值不会被用到。
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                              (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                              (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glBindVertexArray(0);
    }
    // 数据量很小（几十张纸 × 十几顶点）→ 整块重传即可，不必细分更新
    const GLsizeiptr need = (GLsizeiptr)(g_c3OrigData.size() * sizeof(float));
    glBindBuffer(GL_ARRAY_BUFFER, g_c3OrigR.vbo);
    glBufferData(GL_ARRAY_BUFFER, need, g_c3OrigData.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static void RenderLines(LineRenderer& lr, float t, float w, float h, float opacity, bool light) {
    EnsureC3LineRenderer(lr);
    if (!lr.prog) return;
    glUseProgram(lr.prog);
    // 深/浅两套配色共用这同一个 program —— 几何、相机、图层、扭曲、折纸片全都一样，
    // 只有 FS 里那几个颜色常数按这一位切换。
    glUniform1i(glGetUniformLocation(lr.prog, "uLightMode"), light ? 1 : 0);
    glUniform1f(glGetUniformLocation(lr.prog, "uAspect"), (w > 0 && h > 0) ? (w / h) : 1.777f);
    glUniform1f(glGetUniformLocation(lr.prog, "uImgA"), kC3ImgA);
    glUniform1f(glGetUniformLocation(lr.prog, "uTime"), t);
    glUniform1f(glGetUniformLocation(lr.prog, "uExitTime"), g_plExitT);
    // 锚点（复用 g_cubeVeils）——单独程序需自行上传
    {
        static std::vector<float> v4(C3_VEIL_MAX * 4, 0.0f);
        static std::vector<float> d4(C3_VEIL_MAX * 4, 0.0f);
        int nv = (int)g_cubeVeils.size();
        for (int i = 0; i < C3_VEIL_MAX; ++i) {
            if (i < nv) { const auto& v = g_cubeVeils[i];
                v4[i*4+0]=v.x; v4[i*4+1]=v.y; v4[i*4+2]=v.z; v4[i*4+3]=v.spawnT;
                d4[i*4+0]=v.dx; d4[i*4+1]=v.dy; d4[i*4+2]=v.dz; d4[i*4+3]=1.0f; }
            else { v4[i*4+0]=0; v4[i*4+1]=0; v4[i*4+2]=0; v4[i*4+3]=0.0f;
                   d4[i*4+0]=0; d4[i*4+1]=0; d4[i*4+2]=0; d4[i*4+3]=0.0f; }
        }
        GLint lV = glGetUniformLocation(lr.prog, "uVeil[0]");
        if (lV >= 0) glUniform4fv(lV, C3_VEIL_MAX, v4.data());
        GLint lD = glGetUniformLocation(lr.prog, "uVeilDir[0]");
        if (lD >= 0) glUniform4fv(lD, C3_VEIL_MAX, d4.data());
    }
    glUniform1f(glGetUniformLocation(lr.prog, "uLayerPhase"), g_c3LayerPhase);
    glUniform1f(glGetUniformLocation(lr.prog, "uLayerZoom"), kC3LayerZoom);
    glUniform2f(glGetUniformLocation(lr.prog, "uResolution"), w, h);   // 背景柔和起伏用
    glUniform1f(glGetUniformLocation(lr.prog, "uOpacity"), opacity);   // 两趟都要（不设则默认 0 → 全透明黑屏）
    // ① 灰黑底：全屏三角（g_vao 是 NDC 全屏三角，VS 里 uIsBg=1 直通）
    glUniform1i(glGetUniformLocation(lr.prog, "uIsBg"), 1);
    glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    // ② 多层扭曲网：同一 VBO 的【前 kC3MeshSegN 段】画 LAYERS 次（各层 z 循环推进 → 后层浮现）。
    //    开 alpha 混合，各层叠加、远处淡入、近处淡出。
    glUniform1i(glGetUniformLocation(lr.prog, "uIsBg"), 0);
    GLint lLayer = glGetUniformLocation(lr.prog, "uLayer");
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // 线条抗锯齿：GL_LINE_SMOOTH 让驱动按覆盖率调制这条线的 alpha（配合上面已有的混合），
    // 于是网格线/坐标轴/折痕的边缘不再是硬阶梯。只作用于 GL_LINES，不影响折纸片的三角面。
    // 前提是本源不能走 DownscaleBlit 的 NEAREST 放大（见 DownscaleApplies）。
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    // ②b 折纸片：每帧在 CPU 端算好显化动画 → 上传顶点（含各自的透明度）
    //     注意顺序：上传时若首次创建折纸片 VAO 会切走当前绑定，所以 lr.vao 必须在它之后绑。
    C3BuildOrigamiGeometry(t);
    C3UploadOrigamiGeometry();
    glBindVertexArray(lr.vao);
    const GLsizei origTotal = (GLsizei)(g_c3OrigData.size() / 4);   // 折纸片每顶点 4 个 float

    // 每一层取迷宫【不同的窗口】：只平移、不改变朝向（朝向一致是用户明确要求的），
    // 但 4 层看到的是迷宫的不同区域，不再"同一片网格叠 4 次"。
    //
    // 取值约束（两条都很重要）：
    //   · |偏移| ≤ 1 → 窗口 [0,1]+ofs 仍落在画布 pos 范围 [-1,2] 内，不会露出没格子的空白；
    //   · |偏移| > 0.6 → 画布中心线（生成时为了避开双轴，网格在那里留了 ~4px 断口）
    //     映射到 world = 偏移×2，超过可见半宽就跑到屏幕外了，不会露出断口。
    //     0.6 能覆盖到约 1:1 的窗口（更方的窗口才会露）。
    static const float kLayerOfs[4][2] = {
        {  0.00f,  0.00f },     // 第 0 层居中：这一层才和那双固定坐标轴对齐
        {  0.86f,  0.88f },
        { -0.83f,  0.90f },
        { -0.91f, -0.85f },
    };
    GLint lLayerOfs = glGetUniformLocation(lr.prog, "uLayerOfs");

    // 图层 z 是【循环】的（新层从最远处浮现、近处飞过），所以绘制顺序 ≠ 深度顺序。
    // 折纸片现在是不透明实心纸 → 必须按 z【从远到近】画，遮挡关系才对。
    const int LAYERS = 4;
    const float farZ = -6.0f, spanZ = kC3LayerSpan;
    float lz[LAYERS]; int lord[LAYERS] = { 0, 1, 2, 3 };
    for (int i = 0; i < LAYERS; ++i)
        lz[i] = farZ + fmodf(g_c3LayerPhase + (float)i * spanZ / (float)LAYERS, spanZ);
    for (int a = 0; a < LAYERS - 1; ++a)
        for (int b = a + 1; b < LAYERS; ++b)
            if (lz[lord[b]] < lz[lord[a]]) { int t = lord[a]; lord[a] = lord[b]; lord[b] = t; }

    for (int k = 0; k < LAYERS; ++k) {
        const int i = lord[k];                             // 远 → 近
        if (lLayer >= 0) glUniform1i(lLayer, i);
        if (lLayerOfs >= 0) glUniform2f(lLayerOfs, kLayerOfs[i][0], kLayerOfs[i][1]);
        glDrawArrays(GL_LINES, 0, 2 * kC3MeshSegN);        // 只画网格段
        // 折纸片：与网格线同层、同 z 循环、同扭曲场、同窗口偏移
        if (origTotal > 0 && g_c3OrigFillVerts > 0) {
            glBindVertexArray(g_c3OrigR.vao);
            glDrawArrays(GL_TRIANGLES, 0, g_c3OrigFillVerts);                 // 纸面
            glDrawArrays(GL_LINES, g_c3OrigFillVerts,
                         origTotal - g_c3OrigFillVerts);                      // 折痕
            glBindVertexArray(lr.vao);
        }
    }
    // ③ 坐标轴【已删除】：那对"相对静止的十字"不再画。
    //    C3Lines.h 里轴段的数据仍然保留（排在 kC3MeshSegN 之后），要恢复只需把这一趟加回来；
    //    现在只画前面的网格段。
    glBindVertexArray(0);
    glDisable(GL_LINE_SMOOTH);
    glDisable(GL_BLEND);
}

// ============================================================================
// ===== StellarVortex：星际漩涡粒子场（7 条数学曲线 + 点精灵）=====
// ----------------------------------------------------------------------------
// 移植自 effect reference/particle_curves.html。走【网格几何 + 点精灵】而不是全屏
// .frag —— 曲线是 1D 流形，用片元着色器"反向收集"要每像素遍历几千个曲线采样点
// （蝴蝶曲线归一化后弧长 117，是悬链线的 35 倍），任何显卡都跑不动。走几何之后
// 【粒子数就是采样数】，21000 颗粒子一次 drawArrays 画完，不存在这个问题。
//
// 曲线数据在 PCurves.h（生成物）：7 条曲线等弧长重采样 + 归一化，铺进一张
// RGBA32F 纹理（x 轴 = 曲线参数 u，一行一条曲线）。顶点着色器按 u 采样、并在
// 两行之间做线性插值 → 曲线之间的变形天然连续（不需要任何关键帧数据）。
//
// 顶点【不占任何属性缓冲】：粒子序号直接用 gl_VertexID，一切随机量由哈希推出。
// 绑的是一个空 VAO，就是为这个准备的（g_vao 里有 6 个顶点的数据，用它画 21000 个
// 点会越界读属性）。
//
// 三趟，同一个 program，靠 uIsBg 分派：
//   ① uIsBg=1  全屏 quad：星云 + 银河带 + 三层视差星场
//   ② uIsBg=0  点精灵：加色混合（GL_SRC_ALPHA / GL_ONE）
//   ③ uIsBg=2  全屏 quad：暗角/扫描线/颗粒，GL_DST_COLOR / GL_ZERO（乘法）
// 第③趟用【乘法】而不是画在最上层，是因为粒子已经加色叠进帧缓冲了，画不透明层会
// 把它们盖住；而项目里没有可复用的后处理通道（只有 ⅔ 降采样和帧反馈）。
// 交叉淡入期间跳过第③趟：乘法会把底下的旧效果一起乘暗。
// ============================================================================

#define kPCParticles 21000   // 粒子数（= HTML 版 count 滑块）

// 生成物改了这两项，下面两个 static_assert 会先把编译拦下来，提醒同步改 shader 里的常量。
static_assert(kPCCurveN == 7, "PCurves.h 曲线条数变了 -> kParticleVS 里的 kCurveN 要同步改");
static_assert(kPCLutH == 8,   "PCurves.h LUT 行数变了 -> kParticleVS 里的 kLutH 要同步改");
// Filigree 的频谱：kVTFS 里 uFft[16] 是写死的（数组大小必须是常量），所以这里兜住"两个 16"。
// 音频那边真改了频段数，这条会先拦下来，而不是让着色器静默越界读 0。
static_assert(TN_SPEC_BANDS == kAudioBands, "TN_SPEC_BANDS 必须等于 kAudioBands（见 TNParams.h / AudioAnalyzer.h）");

struct PCRenderer { GLuint vao = 0, prog = 0, lutTex = 0; };
static PCRenderer g_pcRenderer;

// ===== 泛光三件套（四个网格效果共用）=====
//
// StellarVortex / Involute / Involute_1 / Function1 的离屏缓冲完全同构：
// scene 全分辨率（RGBA16F，保留 >1 的高光）+ blA/blB 半分辨率做分离式高斯 ping-pong。
// 以前这套结构体和重建函数各写了一份（三份结构体、四份 30 行的重建），改一处漏三处。
//
// 唯一的差别是 Function1 的 scene 要挂深度附件 —— 它画的是【实体曲面】，
// 没有深度就等于"按提交顺序糊上去"，能从近处的尖透过中间的盘看到对面的尖。
// 深度是【建缓冲时】决定的参数，每帧的绘制路径里没有任何分支。
struct ColorFbo { GLuint fb = 0, tex = 0, depth = 0; };

static void EnsureBlurTrio(ColorFbo& scene, ColorFbo& a, ColorFbo& b,
                           int w, int h, int& fboW, int& fboH, bool sceneDepth) {
    if (w < 2 || h < 2) return;
    if (w == fboW && h == fboH && scene.fb) return;   // 尺寸没变就直接走
    auto kill = [](ColorFbo& f) {
        if (f.tex)   glDeleteTextures(1, &f.tex);
        if (f.depth) glDeleteTextures(1, &f.depth);
        if (f.fb)    glDeleteFramebuffers(1, &f.fb);
        f = ColorFbo();
    };
    kill(scene); kill(a); kill(b);
    auto mk = [](ColorFbo& f, int fw, int fh, bool wantDepth) {
        glGenTextures(1, &f.tex);
        glBindTexture(GL_TEXTURE_2D, f.tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, fw, fh, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &f.fb);
        glBindFramebuffer(GL_FRAMEBUFFER, f.fb);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, f.tex, 0);
        if (wantDepth) {
            glGenTextures(1, &f.depth);
            glBindTexture(GL_TEXTURE_2D, f.depth);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, fw, fh, 0,
                         GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, f.depth, 0);
        }
    };
    const int bw = (w > 2) ? w/2 : 2, bh = (h > 2) ? h/2 : 2;
    mk(scene, w, h, sceneDepth);
    mk(a, bw, bh, false);
    mk(b, bw, bh, false);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    fboW = w; fboH = h;
}

static ColorFbo g_pcSceneFbo, g_pcBlurA, g_pcBlurB;
static int   g_pcFboW = 0, g_pcFboH = 0;

/// 当前调色板序号。每次鼓点随机换一个（见 PLUpdate 的 beat 分支），
/// 换效果时归零（见 PLUpdate 的效果切换块）。用全局而不是挂在 GpuEffect 上，
/// 与 g_c3LayerPhase / g_cubeVeils 一样 —— 这是单个 mesh 效果的私有状态。
static int g_pcPalIdx = 0;

// 曲线循环位置：整数部分 = 起点曲线序号，小数部分 = 过渡进度。
// 【每次鼓点让目标 +1】—— hit 与换色一样，也是形态变换的信号。
// 用"连续位置"而不是"每拍重启一次过渡"：morphDur 远长于鼓点间隔时，重启式实现
// 会让过渡永远走不完。这样鼓点密就是持续流动，鼓点疏就是每条曲线干净落定。
static float g_pcCurvePos    = 0.0f;   // 当前（可为小数）
static float g_pcCurveTarget = 0.0f;   // 目标（整数）
static const float kPCMorphDur  = 0.45f;  // 一次曲线过渡的时长（秒）。HTML 的 morphDur=2.4 太长，
                                          //   0.9 时鼓点密集仍会一直"半路上"，再砍一半。
                                          //   0.45 略短于典型鼓点间隔(~0.5s) → 每拍之间过渡能【走完】，
                                          //   每条曲线会有一刹那干净落定，而不是永远糊在两条之间。

// 相机：每次鼓点在空间里随机换一个朝向，但【始终看向构造中心】—— 着色器是绕原点旋转物体，
// 所以"相机在球面上跑、永远对准原点"就等价于随机一个新的 yaw/pitch。位置用指数缓动去追，
// 追不上也没关系（缓动天然可重定向，不会像固定时长插值那样在鼓点密集时"重启"）。
static const float kPCCamZ      = 4.45f;  // 基准相机距离 = HTML 的 camZ
static const float kPCCamEase   = 0.25f;  // 指数缓动时间常数（秒）：越小越跟手
static const float kPCSpin      = 0.15f;  // 基础自转 = HTML 的 spin（静音时也保持有运动）
static float g_pcCamYaw   = 0.0f,  g_pcCamPitch   = -0.42f, g_pcCamDist   = kPCCamZ;
static float g_pcCamYawT  = 0.0f,  g_pcCamPitchT  = -0.42f, g_pcCamDistT  = kPCCamZ;

// kParticleVS / kParticleFS 两段着色器已移到 VisualEffects/PC/PCShaders.h（顶部 include 进来了）。

/// GL 上下文重建时清零（挂在 PlaylistStart，与 ResetC3LineRenderer 同一处）
static void ResetPCRenderer() {
    g_pcRenderer.vao = 0; g_pcRenderer.prog = 0; g_pcRenderer.lutTex = 0;
    g_pcSceneFbo = ColorFbo(); g_pcBlurA = ColorFbo(); g_pcBlurB = ColorFbo();
    g_pcFboW = g_pcFboH = 0;
}

static void EnsurePCRenderer() {
    if (g_pcRenderer.vao) return;
    glGenVertexArrays(1, &g_pcRenderer.vao);   // 空 VAO：粒子顶点信息全部来自 gl_VertexID

    // 曲线 LUT → RGBA32F 纹理。一行一条曲线，x 轴 = 曲线参数 u，每行只填前 kPCCnt[c] 个 texel。
    {
        std::vector<float> tex((size_t)kPCLutW * kPCLutH * 4, 0.0f);
        for (int c = 0; c < kPCCurveN; ++c) {
            const int n = kPCCnt[c], off = kPCOff[c];
            for (int k = 0; k < n; ++k) {
                float* px = &tex[((size_t)c * kPCLutW + k) * 4];
                px[0] = kPPT[(size_t)(off + k) * 2 + 0];
                px[1] = kPPT[(size_t)(off + k) * 2 + 1];
                px[2] = 0.0f; px[3] = 1.0f;
            }
        }
        glGenTextures(1, &g_pcRenderer.lutTex);
        glBindTexture(GL_TEXTURE_2D, g_pcRenderer.lutTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, kPCLutW, kPCLutH, 0, GL_RGBA, GL_FLOAT, tex.data());
        // NEAREST + 着色器里手写线性插值：浮点纹理的 LINEAR 过滤需要额外扩展，
        // 而 NEAREST 在所有设备上都有保证。
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    g_pcRenderer.prog = CompileProgram(kParticleVS, kParticleFS, "StellarVortex");
}

static void RenderParticles(float t, float w, float h, float opacity, bool darkBg) {
    EnsurePCRenderer();
    GLuint prog = g_pcRenderer.prog;
    if (!prog) return;
    glUseProgram(prog);
    auto U = [prog](const char* n) { return glGetUniformLocation(prog, n); };

    glUniform1f(U("uTime"), t);
    glUniform1f(U("uExitTime"), g_plExitT);
    glUniform1f(U("uCount"), (float)kPCParticles);
    glUniform1f(U("uOpacity"), opacity);
    glUniform1f(U("uCurvePos"), g_pcCurvePos);
    glUniform1f(U("uCamYaw"),   g_pcCamYaw);
    glUniform1f(U("uCamPitch"), g_pcCamPitch);
    glUniform1f(U("uCamDist"),  g_pcCamDist);
    glUniform1i(U("uDarkBg"),   darkBg ? 1 : 0);   // 浅色 = 深色的严格负片（同 Cubeeffect3 的约定）
    glUniform2f(U("uResolution"), w, h);
    glUniform1f(U("uLutW"), (float)kPCLutW);
    {
        static std::vector<float> cnt(8, 0.0f);
        for (int i = 0; i < kPCCurveN && i < 8; ++i) cnt[i] = (float)kPCCnt[i];
        glUniform1fv(U("uCnt"), 8, cnt.data());
    }
    {
        // 调色板四段渐变的色标。走 vec4 + glUniform4fv，是因为本项目的 GL loader
        // 只加载了 glUniform1f/2f/1i/1fv/4fv —— 没有 glUniform3f/glUniform3fv。
        int pi = g_pcPalIdx % kPCPalN; if (pi < 0) pi += kPCPalN;
        static float pal[16];
        for (int i = 0; i < 4; ++i) {
            pal[i*4+0] = kPCPal[pi][i][0];
            pal[i*4+1] = kPCPal[pi][i][1];
            pal[i*4+2] = kPCPal[pi][i][2];
            pal[i*4+3] = 1.0f;
        }
        glUniform4fv(U("uG[0]"), 4, pal);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_pcRenderer.lutTex);
    glUniform1i(U("uLut"), 0);
    glDisable(GL_DEPTH_TEST);

    // 泛光参数（与 HTML 版一致：曝光 1.28、泛光权重 0.82、亮部阈值 0.55）
    glUniform1f(U("uExposure"), 1.28f);
    glUniform1f(U("uBloomAmt"), 0.82f);
    glUniform1f(U("uBloomThresh"), 0.55f);

    const int Wi = (int)w, Hi = (int)h;
    if (Wi < 2 || Hi < 2) return;
    EnsureBlurTrio(g_pcSceneFbo, g_pcBlurA, g_pcBlurB, Wi, Hi, g_pcFboW, g_pcFboH, false);
    if (!g_pcSceneFbo.fb) return;

    // DrawEffect 可能在交叉淡入时把目标设成别的 FBO —— 先记下来，最后合成回那里
    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

    const int bw = Wi/2, bh = Hi/2;   // EnsureBlurTrio 已保证缓冲存在（下面还有 .fb 判空）

    // ---- ① 场景（背景 + 粒子）画进离屏 RGBA16F 缓冲 ----
    // 一定要离屏：8 位主缓冲会在 1.0 处截断，亮部提取就没东西可取了。
    glBindFramebuffer(GL_FRAMEBUFFER, g_pcSceneFbo.fb);
    glViewport(0, 0, Wi, Hi);
    glUniform2f(U("uResolution"), w, h);

    glDisable(GL_BLEND);                   // 背景纯覆盖
    glUniform1i(U("uIsBg"), 1);
    glBindVertexArray(g_vao);              // 6 个顶点的 NDC 全屏四边形
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // 粒子：加色混合，21000 个点一次画完
    glUniform1i(U("uIsBg"), 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    // 这两个 glEnable 缺一不可：
    //   GL_PROGRAM_POINT_SIZE —— 否则 gl_PointSize 无效，粒子全退化成 1px；
    //   GL_POINT_SPRITE       —— 否则 gl_PointCoord 是废值，片元全被 discard 掉，
    //                            一个像素都不画且不报错（兼容性 profile 的坑）。
    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_POINT_SPRITE);
    glBindVertexArray(g_pcRenderer.vao);
    glDrawArrays(GL_POINTS, 0, kPCParticles);
    glDisable(GL_POINT_SPRITE);
    glDisable(GL_PROGRAM_POINT_SIZE);
    glDisable(GL_BLEND);

    // ---- ② 泛光：亮部提取到半分辨率，再来两轮分离式高斯（半径逐轮加大 → 光晕更宽）----
    glUniform2f(U("uResolution"), (float)bw, (float)bh);
    glUniform2f(U("uTexel"), 1.0f/bw, 1.0f/bh);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_pcSceneFbo.tex);
    glUniform1i(U("uTexScene"), 0);
    glUniform1i(U("uIsBg"), 2);
    glBindFramebuffer(GL_FRAMEBUFFER, g_pcBlurA.fb);
    glViewport(0, 0, bw, bh);
    glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // 在 blA/blB 之间 ping-pong：先横向进 blB，再纵向回 blA
    glUniform1i(U("uIsBg"), 3);
    glUniform1i(U("uTexBloom"), 0);
    const float radii[2] = { 1.6f, 3.4f };
    for (int pass = 0; pass < 2; ++pass) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_pcBlurA.tex);
        glBindFramebuffer(GL_FRAMEBUFFER, g_pcBlurB.fb);
        glViewport(0, 0, bw, bh);
        glUniform2f(U("uBlurDir"), radii[pass], 0.0f);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_pcBlurB.tex);
        glBindFramebuffer(GL_FRAMEBUFFER, g_pcBlurA.fb);
        glViewport(0, 0, bw, bh);
        glUniform2f(U("uBlurDir"), 0.0f, radii[pass]);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    // ---- ③ 合成回原来的目标：场景 + 泛光 → 曝光 → ACES → 暗角/扫描线/颗粒 ----
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
    glViewport(0, 0, Wi, Hi);
    glUniform2f(U("uResolution"), w, h);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_pcSceneFbo.tex);
    glUniform1i(U("uTexScene"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, g_pcBlurA.tex);
    glUniform1i(U("uTexBloom"), 1);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(U("uIsBg"), 4);
    // alpha 用播放链给的不透明度 → 交叉淡入时叠在旧效果上，而不是把旧的一刀切掉
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}

// ============================================================================
// ===== Involute：圆的渐开线无限展开曲面 =====
// ----------------------------------------------------------------------------
// 几何见 VisualEffects/VX/VXGeom.h（纯数学，不碰 GL）。
// 横截面 = 圆的渐开线镜像拼成的闭合叶形；沿 z 复制成拱道；相邻顶点沿径向
// 棋盘格偏移 ±h → 四边形变鞍面 → 每个三角形一个法线 → 硬的【凹凸分面】。
//
// 无限：几何只生成【一个周期】，渲染时画 uLayers 份在视深上错开并回绕 ——
// 与 Cubeeffect3 的多层推进同一套路，几何量不随"无限"增长。
//
// 音频：质心(centroid) → 画面颜色【饱和度】。质心是"能量重心落在低频还是高频"，
// 也就是亮度；它是比值，不受系统音量影响。
//
// 硬分面靠屏幕空间导数拿：normalize(cross(dFdx(world), dFdy(world))) ——
// 每个三角形天然得到一个法线，不需要复制顶点、也不用手工算法线。
// ============================================================================

// kVXVS / kVXFS 两段着色器已移到 VisualEffects/VX/VXShaders.h（顶部 include 进来了）。

// ===========================================================================
// Involute_1（台阶板）—— Involute 的隧道剪开摊平成一整块矩形板
// 渲染骨架与 Involute 逐条对应：程序化网格 + 泛光 5 趟 + morph 协议 + 外缘微光。
// 差别只在三点：① 相机在球面上绕着板的几何中心转（不是在管内往前飞）
//               ② 每次 hit 是【每一级台阶各自独立弹跳】（不是镜头加速 + 整体分离）
//               ③ 板是实体，没有雾/纵深淡出
// ===========================================================================
// kSSVS / kSSFS 两段着色器已移到 VisualEffects/SS/SSShaders.h（顶部 include 进来了）。

struct VXRenderer { GLuint vao = 0, vbo = 0, prog = 0; GLsizei vcount = 0; };
static VXRenderer g_vxRenderer;
static float g_vxPhase = 0.0f;      // 推进相位：只增不减 → 永远在展开新的部分
static float g_vxHitT  = 999.0f;    // 距上次鼓点的秒数（驱动瞬间加速的衰减）
const float kVXBaseSpeed = 2.6f;    // 1× 的推进速度（世界单位/秒）——"镜头无限推进"
const float kVXHitRate   = 30.0f;   // 鼓点瞬间加速的峰值倍率
const float kVXHitDur    = 0.34f;   // 加速段总时长（秒）——到点精确回到 1×，不拖尾
const float kVXHitCurve  = 3.0f;    // 回落陡度：(1-t/T)^P 的 P。越大越陡（1=线性，3=起步就陡降）
// 第二形态：每次 hit 翻转"台阶沿切向分离"，用 smootherstep 过渡（变速），到位后停住
static float g_vxFormTarget = 0.0f; // 0=原始态 1=分离态（每次 hit 翻转）
static float g_vxFormT      = 99.0f;// 本次过渡已进行的秒数
const float kVXFormDur      = 0.85f;// 过渡时长（秒）
/// 【第二形态】进度：0 = 原始态，1 = 沿径向整个分出去。
/// 从 RenderInvolute 里抽出来，是因为 Involute_1 在过渡时必须【跟着一起动】——
/// 两边各写一份公式迟早对不上，而这一对现在已经要求逐帧对齐。
/// 只读不推进：推进由 RenderInvolute 的 g_vxFormT += dt 负责。
static float VXFormDisp() {
    float fm = g_vxFormT / kVXFormDur;
    fm = fm < 0.0f ? 0.0f : (fm > 1.0f ? 1.0f : fm);
    const float fe = fm * fm * fm * (fm * (fm * 6.0f - 15.0f) + 10.0f);
    return (1.0f - g_vxFormTarget) + (2.0f * g_vxFormTarget - 1.0f) * fe;
}
const float kVXFormDist     = 2.50f;// 分离距离（世界单位，沿【径向】= 朝画面外）

// ---- 外缘微光 -------------------------------------------------------------
// "外边缘"就是渐开线叶形收口的那一圈：u≈0.5 处 t→T 的两个端点，以及它们之间的闭合边。
// 它同时是【离中轴最远】的一圈（r = R·√(1+T²) ≈ 2.6），所以在屏幕上也是最靠外的那条边。
// 亮度故意加在 HDR 区间（场景是 RGBA16F）：>1 才越得过泛光阈值 0.42，
// 于是那层"晕"是【泛光趟】给的 —— 这里只负责画一条窄的高光带。
// 半宽的单位是【占最大半径的比例】：0.035 = 只取最外面 3.5% 的那一圈。
// 这个值扫过参（_hittest/vx_rim_sweep.py）：0.035 时约 5% 的亮部被点亮，是"一条细边"；
// 再宽就糊成一片了。
const float kVXRimW = 0.035f;
const float kVXRimI = 0.85f;    // 强度。调大 = 更亮更粗的晕；调到 0 = 完全关掉

// 鼓点累计数（kick+snare）。单调递增、永不归零；既当 SharedMem 的对外字段，
// 也当 C3 锚点/折纸片撒点、StellarVortex 与 Involute 换配色的随机种子（同一 hit 可复现）。
// 【声明位置】必须在所有拿它当种子的代码之前 —— 它已经被上移过两次了
// （一次为了 PLUpdate，一次为了 Involute 换色）。再往前挪的话记得看一眼。
static DWORD g_beatCount = 0;

// ---- 鼓点换色：每次 hit 换一套配色 ----------------------------------------
// 关键点：【过渡区间和过渡曲线跟镜头加速完全共用】。
//   镜头：boost = 1 + (30-1)·gs，  gs = (1 - t/T)^P  → t=0 处 30×，t=T 处精确 1×
//   颜色：ce = 1 - gs             → t=0 处 0（还是旧色），t=T 处 1（已是新色）
// 两者用的都是 kVXHitDur 这段时长和 kVXHitCurve 这条曲线，所以"猛地一冲刺"和
// "猛地一换色"是同一个动作的两个侧面，同起同落。ce 是 gs 的补，所以曲线形状也一致：
// 起步最陡（瞬间换掉大半），随后渐渐定住 —— 和镜头"起步就陡降"的手感严格对应。
static const int kVXPalN = 6;
static const float kVXPal[kVXPalN][2][3] = {
    //  A（近端 / vUV.x 小）      B（远端 / vUV.x 大）
    { {0.16f, 0.85f, 0.95f}, {0.95f, 0.22f, 0.72f} },   // 青   → 洋红（原配）
    { {0.25f, 0.45f, 1.00f}, {1.00f, 0.78f, 0.25f} },   // 靛蓝 → 金
    { {0.10f, 0.90f, 0.55f}, {0.95f, 0.95f, 0.30f} },   // 翠绿 → 柠檬
    { {0.55f, 0.30f, 0.98f}, {1.00f, 0.35f, 0.45f} },   // 紫罗兰 → 玫红
    { {1.00f, 0.45f, 0.18f}, {0.35f, 0.85f, 1.00f} },   // 橙红 → 冰蓝
    { {0.35f, 0.95f, 0.85f}, {0.90f, 0.25f, 0.95f} },   // 薄荷 → 品红
};
static int   g_vxPalIdx  = 0;       // 本次过渡的【目标】配色
static float g_vxColFrom[2][3];     // 本次过渡的起点色（hit 那一刻屏上的真实颜色）
static float g_vxColNow [2][3];     // 当前实际用色（每帧插值出来，上传给 uColA/uColB）
static bool  g_vxPalInit = false;

static void VXPaletteReset() {
    g_vxPalIdx = 0;
    for (int k = 0; k < 2; ++k)
        for (int c = 0; c < 3; ++c)
            g_vxColFrom[k][c] = g_vxColNow[k][c] = kVXPal[0][k][c];
    g_vxPalInit = true;
}

/// 换到【另一套】配色。随机是为了不显得机械，但加法上保证不会抽到自己 ——
/// 否则会有 hit 触发却"颜色没变"，看起来像漏拍了。
/// 种子用 g_beatCount 是本项目惯例（同一次 hit 结果可复现），同 StellarVortex。
static void VXPaletteNext() {
    const int rest = kVXPalN - 1;
    int k = (int)(cubeRnd((int)g_beatCount, 4242) * (float)rest);
    if (k < 0) k = 0;
    if (k >= rest) k = rest - 1;
    g_vxPalIdx = (g_vxPalIdx + 1 + k) % kVXPalN;    // 跳过自己，落在剩下 N-1 套里
}

// 泛光离屏缓冲（与 StellarVortex 结构相同，但各自持有 —— 两个效果互不干扰）
// ===========================================================================
// 【曲变直过渡期间把音频整个锁掉】
// ===========================================================================
// 这一对过渡是【几何变形】，1.4 秒里形状一直在变。中途要是被鼓点插一脚，画面会被砸断：
//   Involute 侧   —— 相位推进瞬间冲到 30×（管子窜一下）、第二形态沿径向炸开、换配色、
//                    质心→饱和度把画面颜色也带跑；
//   Involute_1 侧 —— 镜头甩 120°~180°、台阶上下乱弹。
// 用户的要求："过渡的时候不受任何因素影响，播放完了再受影响。"
// 所以过渡期间这些一概不接，锁一解除立刻恢复实时响应。注意两个渲染器里的 s_prevFlash
// 必须【照常更新】：否则解锁那一刻会把攒了 1.4 秒的 beatFlash 变化当成新鼓点，补一记冲刺。
// 声明放在这里（而不是 PLUpdate 旁），是因为下面两个渲染函数都要用。
static bool  g_plVxLock = false;
static float g_plVxLockCentroid = 0.5f;   // 进入过渡那一刻的质心，冻着当显示级调色用

static ColorFbo g_vxScene, g_vxBlurA, g_vxBlurB;
static int   g_vxFboW = 0, g_vxFboH = 0;

static void ResetVXRenderer() {
    g_vxRenderer.vao = 0; g_vxRenderer.vbo = 0; g_vxRenderer.prog = 0;
    g_vxRenderer.vcount = 0;
    g_vxPhase = 0.0f;
    g_vxHitT = 999.0f;
    g_vxFormTarget = 0.0f; g_vxFormT = 99.0f;   // 回到原始态
    VXPaletteReset();                           // 配色也回第一套
    g_vxScene = ColorFbo(); g_vxBlurA = ColorFbo(); g_vxBlurB = ColorFbo();
    g_vxFboW = g_vxFboH = 0;
}

static void EnsureVXRenderer() {
    if (g_vxRenderer.vao) return;
    std::vector<float> mesh;
    VXBuildMesh(mesh);                                   // 只生成【一个周期】
    g_vxRenderer.vcount = (GLsizei)(mesh.size() / VXVertexStride());

    glGenVertexArrays(1, &g_vxRenderer.vao);
    glBindVertexArray(g_vxRenderer.vao);
    glGenBuffers(1, &g_vxRenderer.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vxRenderer.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(mesh.size() * sizeof(float)),
                 mesh.data(), GL_STATIC_DRAW);
    const size_t stride = VXVertexStride() * sizeof(float);   // 7 float：pos3 + uv2 + tangent2
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)0);                  // 位置
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)(3 * sizeof(float))); // u,v
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)(5 * sizeof(float))); // 切向
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
    g_vxRenderer.prog = CompileProgram(kVXVS, kVXFS, "Involute");
}

static void RenderInvolute(float t, float w, float h, float opacity, bool darkBg) {
    EnsureVXRenderer();
    if (!g_vxPalInit) VXPaletteReset();   // 没走过 PlaylistStart 就单放这个效果时兜底
    GLuint prog = g_vxRenderer.prog;
    if (!prog || !g_vxRenderer.vcount) return;
    int wi = (int)w, hi = (int)h;
    if (wi < 2 || hi < 2) return;
    EnsureBlurTrio(g_vxScene, g_vxBlurA, g_vxBlurB, wi, hi, g_vxFboW, g_vxFboH, false);
    if (!g_vxScene.fb) return;

    glUseProgram(prog);
    auto U = [prog](const char* n) { return glGetUniformLocation(prog, n); };

    // 无限推进：相位只增不减（用真实帧间差而不是 uTime，免得切效果回来时跳变）
    static float s_lastT = -1.0f;
    float dt = (s_lastT < 0.0f || t < s_lastT) ? 0.0f : (t - s_lastT);
    s_lastT = t;

    // ---- hit → 镜头【瞬间加速后恢复】：峰值 30×，下凸幂曲线回落 ----
    // 【不用高斯】——高斯在峰值附近是平的（t=0 处导数为 0），命中后头 50ms 几乎不掉速，
    // 手感是"黏在 30× 上"。改用 (1 - t/T)^P：t=0 处导数最大（起步就陡降），
    // 且 t=T 时精确归零 → 加速有明确的结束时刻，不拖尾。
    //   P 越大越陡；T 是加速总时长。
    // 触发用 beatFlash 的【上升沿】：它平时只衰减，只有新鼓点会把它顶回高位，
    // 所以"比上一帧大"就是新鼓点（比定阈值可靠 —— 阈值的绝对值会被全局音频强度缩放）。
    static float s_prevFlash = 0.0f;
    // 【曲变直过渡期间：鼓点一概不接】—— 否则这三样都会在变形中途把画面砸断：
    //   ① 镜头的 30× 冲刺（见下面 boost）
    //   ② 第二形态沿径向整个炸开
    //   ③ 换配色（青→洋红→金…，变到一半突然变色）
    // 用户的要求是"过渡时不受任何因素影响，播放完再受"。
    // ⚠ s_prevFlash 仍然要照常更新（放在 if 外面）：不更新的话，锁一解除，
    //   累积了 1.4 秒的 beatFlash 变化会被当成"新鼓点"，立刻补一记 30× 冲刺。
    if (!g_plVxLock && g_af.beatFlash > s_prevFlash + 1e-4f) {
        g_vxHitT = 0.0f;                              // 镜头加速（不变）
        g_vxFormTarget = 1.0f - g_vxFormTarget;       // 【第二形态】翻转：分出去 / 收回来
        g_vxFormT = 0.0f;                             // 过渡重新计时
        // 【换色】起点取"此刻屏上真正的颜色"，而不是上一套的标称值：
        // 连着两拍很近时上一段过渡还没走完，用标称值会当场跳色。
        for (int k = 0; k < 2; ++k)
            for (int c = 0; c < 3; ++c) g_vxColFrom[k][c] = g_vxColNow[k][c];
        VXPaletteNext();
    }
    s_prevFlash = g_af.beatFlash;
    g_vxHitT += dt;

    // ---- 第二形态过渡：smootherstep → "变速"（起步慢、中间快、收尾慢）----
    // 目标只有 0/1，起点必是 1-目标，所以进度可以直接由 (目标, t) 推出来，不需要额外存起点。
    g_vxFormT += dt;
    const float formDisp = VXFormDisp();
    float hk = 1.0f - g_vxHitT / kVXHitDur;
    hk = hk < 0.0f ? 0.0f : hk;
    const float gs = std::pow(hk, kVXHitCurve);
    // 【曲变直过渡期间不受音频影响】：boost 强制回 1（否则过渡中途来一记鼓点，
    // 相位的推进速度瞬间冲到 30× —— 正在变形的管子会当场"窜"一下，过渡被砸断）。
    const float boost = g_plVxLock ? 1.0f : (1.0f + (kVXHitRate - 1.0f) * gs);
    g_vxPhase += dt * kVXBaseSpeed * boost;   // 永不停止的推进（乘上鼓点加速）

    // ---- 鼓点换色的插值：ce 是 gs 的补，起止点与镜头严格重合 ----
    const float ce = 1.0f - gs;
    for (int k = 0; k < 2; ++k)
        for (int c = 0; c < 3; ++c)
            g_vxColNow[k][c] = g_vxColFrom[k][c]
                             + (kVXPal[g_vxPalIdx][k][c] - g_vxColFrom[k][c]) * ce;

    // 近端要够远，否则整个画面都贴在管壁上、看不出渐开线的形状；
    // 26/6≈4.3 的层间距仍然保持"新的一节从远处浮现"的推进感。
    const float NEAR_Z = -9.5f, SPAN = 30.0f;
    const int   LAYERS = 6;

    glUniform1f(U("uPhase"), g_vxPhase);   // 只用于调试；回绕已在 CPU 算好
    glUniform1f(U("uNear"), NEAR_Z);
    glUniform1f(U("uSpan"), SPAN);
    glUniform1f(U("uTime"), t);
    glUniform1f(U("uFocal"), 1.0f / 0.4452f);          // fov ≈ 48°
    glUniform2f(U("uResolution"), w, h);
    glUniform1f(U("uExitTime"), g_plExitT);
    glUniform1f(U("uFormDisp"), formDisp);        // 第二形态进度（每次 hit 翻转）
    glUniform1f(U("uFormDist"), kVXFormDist);
    // 音频：质心 → 饱和度。
    // 【曲变直过渡期间冻住】：这是【显示级】的调色，音乐一变画面颜色就跟着变，
    // 同样算"被音频影响"。冻在进入过渡那一刻的值（音频重新接管后立刻回到实时）。
    glUniform1f(U("uCentroid"), g_plVxLock ? g_plVxLockCentroid : g_af.centroid);
    // 允许过饱和：sat 上限 0.35+1.50 = 1.85（外插），低沉段落 0.35
    glUniform1f(U("uSatAmount"), 1.50f);
    glUniform1f(U("uSatFloor"), 0.35f);
    {
        // 第 4 个分量恒为 1：走 glUniform4fv 只是因为本项目的 GL loader 没加载
        // glUniform3f/3fv（同 StellarVortex），着色器里只取 .rgb
        float cA[4] = { g_vxColNow[0][0], g_vxColNow[0][1], g_vxColNow[0][2], 1.0f };
        float cB[4] = { g_vxColNow[1][0], g_vxColNow[1][1], g_vxColNow[1][2], 1.0f };
        glUniform4fv(U("uColA"), 1, cA);
        glUniform4fv(U("uColB"), 1, cB);
    }
    glUniform1i(U("uDarkBg"), darkBg ? 1 : 0);
    glUniform1f(U("uOpacity"), opacity);
    glUniform1f(U("uExposure"), 1.15f);
    glUniform1f(U("uBloomAmt"), 0.85f);
    glUniform1f(U("uBloomThresh"), 0.42f);
    // shader 侧的 uEncode/uMorphBlend 协议仍然保留（所有 .frag 都实现了），
    // 但引擎不再驱动它：uEncode 恒 0、uMorphBlend 恒 0 = 正常绘制趟。
    glUniform1i(U("uEncode"), 0);
    glUniform1f(U("uMorphBlend"), 0.0f);
    glUniform1f(U("uVXHalfM"), (float)(VX_HALF * 2));   // 由 VXGeom.h 唯一定义，不写死
    glUniform1f(U("uVXRings"), (float)VX_RINGS);
    glUniform1f(U("uRimW"), kVXRimW);                   // 外缘微光
    glUniform1f(U("uRimI"), kVXRimI);
    // 外缘半径要跟着【第二形态】走：分离是整条曲线沿径向平移 d，半径整体 +d，
    // 不跟着加的话，分离态下外缘会跑到画面外，微光就没了。d 对每个顶点都一样，所以能直接加。
    glUniform1f(U("uVXRMax"), VXMaxRadius() + formDisp * kVXFormDist);

    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    const int bw = wi/2, bh = hi/2;   // EnsureBlurTrio 已保证缓冲存在

    const float SEG = SPAN / (float)LAYERS;

    // ---- ① 场景 = 背景趟 + 几何趟，都画进 RGBA16F 离屏缓冲 ----
    // 必须离屏：8 位主缓冲在 1.0 处截断，亮部提取就没了源头（同 StellarVortex）
    glBindFramebuffer(GL_FRAMEBUFFER, g_vxScene.fb);
    glViewport(0, 0, wi, hi);
    glUniform2f(U("uResolution"), w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glUniform1i(U("uPass"), 0);                       // 背景
    glDisable(GL_BLEND);
    glBindVertexArray(g_vao);                          // g_vao = NDC 全屏四边形
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glUniform1i(U("uPass"), 1);                       // 几何（多层推进）
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // 远处淡出到背景（alpha = vFog）
    glBindVertexArray(g_vxRenderer.vao);
    // 每层一段 6 单位长的几何、间隔 5 单位沿 z 铺开（重叠 1 单位 → 层间无缝），
    // 各自独立回绕。回绕在这里做：整层一起搬，顶点之间不会脱节。
    // （SEG 在上面声明，编码趟要用同一份）
    for (int L = 0; L < LAYERS; ++L) {
        float z = g_vxPhase + (float)L * SEG;
        z = NEAR_Z + std::fmod(z - NEAR_Z, SPAN);
        if (z < NEAR_Z) z += SPAN;
        glUniform1f(U("uLayerZ"), z);
        glDrawArrays(GL_TRIANGLES, 0, g_vxRenderer.vcount);
    }
    glDisable(GL_BLEND);

    // ---- ② 泛光：亮部提取到半分辨率 + 两轮分离式高斯 ----
    glUniform2f(U("uResolution"), (float)bw, (float)bh);
    glUniform2f(U("uTexel"), 1.0f/bw, 1.0f/bh);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_vxScene.tex);
    glUniform1i(U("uTexScene"), 0);
    glUniform1i(U("uPass"), 2);
    glBindFramebuffer(GL_FRAMEBUFFER, g_vxBlurA.fb);
    glViewport(0, 0, bw, bh);
    glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glUniform1i(U("uPass"), 3);
    glUniform1i(U("uTexBloom"), 0);
    const float radii[2] = { 1.7f, 3.6f };
    for (int pass = 0; pass < 2; ++pass) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_vxBlurA.tex);
        glBindFramebuffer(GL_FRAMEBUFFER, g_vxBlurB.fb);
        glViewport(0, 0, bw, bh);
        glUniform2f(U("uBlurDir"), radii[pass], 0.0f);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_vxBlurB.tex);
        glBindFramebuffer(GL_FRAMEBUFFER, g_vxBlurA.fb);
        glViewport(0, 0, bw, bh);
        glUniform2f(U("uBlurDir"), 0.0f, radii[pass]);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    // ---- ③ 合成回原目标：场景 + 泛光 → 曝光 → ACES → 暗角 →（浅色时取反）----
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
    glViewport(0, 0, wi, hi);
    glUniform2f(U("uResolution"), w, h);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_vxScene.tex);
    glUniform1i(U("uTexScene"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, g_vxBlurA.tex);
    glUniform1i(U("uTexBloom"), 1);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(U("uPass"), 4);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}

// ===========================================================================
// Involute_1 渲染器 —— 结构与 Involute 的三件套逐条对应
// ===========================================================================
struct SSRenderer { GLuint vao = 0, vbo = 0, prog = 0; GLsizei vcount = 0; };
static SSRenderer g_ssRenderer;
static ColorFbo g_ssScene, g_ssBlurA, g_ssBlurB;
static int   g_ssFboW = 0, g_ssFboH = 0;

// 相机：**hit 才动，没 hit 就静止**（用户明确要求"hit 时相机才触发我说的运动"）。
// 位置永远在半径 SS_CAM_DIST 的球面上，视线永远指向板的几何中心（原点）—— 这两条是恒等式，
// 不是每帧算出来的，见 kSSVS 里"绕原点转物体"那段。
// 初始偏航角也给到 0.85：板子是【斜着升上去】的，正对着看（yaw=0）只能看到一块平板，
// 斜坡完全读不出来。斜 45° 左右才是"这是一条升上去的楼梯"最清楚的视角。
static float g_ssCamYaw = 0.85f, g_ssCamYawT = 0.85f;
// 【本次转位】的起点与计时。缓动不再是"指数逼近"，而是按固定时长走一条 S 曲线（见下面的注释），
// 所以必须记住起点 —— 指数逼近只靠"当前值+目标值"就够了，S 曲线不行。
static float g_ssCamYawFrom = 0.85f, g_ssCamPitchFrom = 0.35f;
// 初始俯角给到 0.52（约 30°）：板面是【斜着升上去】的，正对着看(fov 下 10°)
// 那个斜面完全看不出来 —— 只能看见一块平板上铺着方块，跟 Involute 没关系。
// 斜一点才一眼看出"这是一条升上去的楼梯"。
static float g_ssCamPitch = 0.35f, g_ssCamPitchT = 0.35f;
static float g_ssCamT    = 99.0f;    // 本次转位已经走了几秒（起点那一刻归零）
static float g_ssCamDur  = 0.7f;     // 本次转位的总时长，每次 hit 重掷 → 这就是"随机变加速"
static float g_ssHitAge = 999.0f;    // 距上次 hit 的秒数（没 hit 过给大数 → 台阶全归位）
// 【上一拍】的序号和间隔 —— 给着色器的"两拍叠加"用，见 kSSVS 里的说明。
// 序号不能想当然用"当前序号 − 1"：漏拍的节拍会让真正的上一拍不是上一拍。
static float g_ssPrevHitIdx = -1.0f;
static float g_ssPrevGap    = 999.0f;
static float g_ssHitIdx     = -1.0f;   // 当前这一拍的序号（= hit 那一刻的 g_beatCount）
// 【无限延伸】的流动相位：CPU 积分、只增不减，和 Involute 的 g_vxPhase 同一个思路。
// 绝不能用 uTime×常数 —— 那样一切效果、一变速就会"回跳"（项目里反复强调过的铁律）。
static float g_ssPhase = 0.0f;
const float kSSFlowSpeed = 0.55f;         // 台阶流动速度（世界单位/秒）。调大 = 爬得更快

// 【画几份】让梯段在画面外接下去。每份错开一个梯段长，接缝天然无缝。
// 5 份 × 7.28 ≈ 36 长，而相机距离 13 时可见长度约 11.6 —— 两头都远在画面外。
// 【只画一份】。以前是"26 级 × 5 份拷贝沿 z 错开"，代价是每道接缝都要接严 ——
// 而实测接不齐（俯视图能看见横向错位）。接缝本来就是自找的：要长就【把楼梯做长】，
// 而不是复制粘贴。（130 级 = 3900 顶点，对 GPU 来说不算什么。）
const int   kSSRep = 1;

// ===========================================================================
// 【曲变直】到底怎么弯 —— 先读这段，别再回到"把直杆弯成圆弧"那条老路上
// ===========================================================================
// 老做法是把板当直杆、弯成半径 R 的圆弧（x → 周向、y → 径向）。**它是错的**，
// 而且不是参数没调好，是【拓扑上就不可能对】：
//   rr = R0 + y，而 y（上升）从 0 走到 20.7 → 半径从 1 涨到 21.7，
//   卷出来是一个越张越开的喇叭口，不是管子；实测并排渲染就是一坨 V 形的带子。
// 正解是【直接卷到 Involute 的断面上】——三个自由度一一对应：
//   宽度 x  →  断面的弧长位置   （板的两条长边合拢成叶形的尖点，板中线是最外缘）
//   上升 y  →  绕 z 轴的扭转     （不是径向！见下）
//   长度 z  →  管轴坐标
// 写出来就三行（uMix=0 时逐点还原成 x/y/z 本身，uMix=1 时逐点等于 Involute）：
//   Xc,Yc = 断面(mix(直线上的一点, 叶形上的一点))
//   th    = -uMix * 2π * (z - zMin) / VX_PERIOD_LEN      ← 扭转由【连续的 z】给
//   P     = (Rot(th) · (Xc, mix(y, Yc))) , z
//
// 【为什么上升能变成扭转】台阶沿 z 每走一步就升高一点，而管子沿 z 每走 uPeriod 就转一整圈
// —— 两件事都是"沿轴线累积"，把"升高"读成"转角"，交接处就对得严丝合缝。
// 而且数值上刚好自洽：每级进深 = 6.0/26、每级升高 0.159 → 26 级正好升 4.134，
// 与 2π 只差一个常数因子，两者是同一条螺距被两种坐标读出来的。
//
// 【宽度必须是整条叶形两臂的弧长】= VX_R·VX_T² = 5.76（见 SSGeom.h 的 SS_HALF_W）。
// 用单臂 2.88 的话断面只包半圈，卷出来是半张壳 —— 和 Involute 并排渲过，量级都对不上。
//
// 【过渡混合量】1 = 完全卷紧（形状 = Involute 的管，相机在管轴上朝前看）；
// 0 = 平放的直楼梯 + 绕板转的轨道视角。由 PLRender 的 Involute<->Involute_1 分支驱动。
static float g_ssTransMix = 0.0f;

const float kSSCamPitchMax = 1.15f;                       // 俯仰夹住，免得翻过头顶
// 【一次转位的时长】每次 hit 重掷 → "随机变加速"：短的猛地甩过去，长的慢慢漂。
// 注意这是【时长】不是"速率"：原来的 kSSCamRate 是 exp 逼近的速率，两种缓动的量纲不一样，
// 换曲线时不能把旧数值直接搬过来（1.6~5.5 的倒数 0.18~0.63 秒，对 120°~180° 的甩动太快了、
// 两头那两段"几乎不动"会被压没）。
const float kSSCamDurMin = 0.45f, kSSCamDurMax = 1.10f;
// 【缓动曲线的"突然程度"】归一化 tanh 的陡度：
//   e(u) = (tanh(k·(u−0.5)) + tanh(k/2)) / (2·tanh(k/2))
// 速度剖面是 sech²，两头按指数衰减、中间一个尖峰 —— 就是用户要的
// "先缓慢移一下、突然加速到很快、又慢下来，像过山车"。k 越大中间越突然、两头越接近静止。
// k=4：起步速度约为峰值的 7%（看得见在动，不是干等）、峰值 2.07× 均速、
//      走完 25% 路程只用 10% 的时间就够说明它有多"突然"。
const float kSSCamEaseK = 4.0f;
// 每级升高 SS_STEP_R = 0.159，所以幅度可以直接读成"能弹起几级"：
//   0.40 ≈ 2.5 级 ｜ 1.20 ≈ 7.5 级 ｜ 4.00 ≈ 25 级（是整条梯高度 20.7 的 19%）
// 用户要"扩大十倍"→ 0.40 × 10 = 4.00。
// ⚠ 实测（_hittest 的 SS_AMP 扫参）：到 4.00 时鼓点峰值那一下台阶会整个散开、
//   读不出"楼梯"了。1.20 左右还能看出是楼梯在弹。要收就在这一个数上收。
const float kSSBounceAmp   = 4.00f;
const float kSSBounceSpread= 0.22f;   // 各台阶起跳的错开时间（秒），必须短于鼓点间隔
const float kSSRimW        = 0.045f;  // 外缘微光半宽（占板半宽/半高的比例）
const float kSSRimI        = 0.85f;   // 外缘微光强度

static void ResetSSRenderer() {
    // GL 上下文每次重开都会失效 —— 不清零就会拿旧句柄去画（本文件反复出现的崩溃模式）
    g_ssRenderer.vao = 0; g_ssRenderer.vbo = 0; g_ssRenderer.prog = 0;
    g_ssRenderer.vcount = 0;
    g_ssCamYaw = g_ssCamYawT = 0.85f;
    g_ssCamPitch = g_ssCamPitchT = 0.35f;
    g_ssCamYawFrom = 0.85f; g_ssCamPitchFrom = 0.35f;
    g_ssCamT = 99.0f; g_ssCamDur = 0.7f;
    g_ssHitAge = 999.0f;
    g_ssHitIdx = g_ssPrevHitIdx = -1.0f; g_ssPrevGap = 999.0f;
    g_ssPhase = 0.0f;
    g_ssTransMix = 0.0f;   // 归位：不归零的话重开时楼梯是卷着的（侧通道残留在上一帧）
    g_ssScene = ColorFbo(); g_ssBlurA = ColorFbo(); g_ssBlurB = ColorFbo();
    g_ssFboW = g_ssFboH = 0;
}

/// 【自报家门】把这份构建实际生成的几何写进 exe 同目录的 ss_build.txt。
/// 纯数学、不需要 GL，所以能在启动时就调用 —— 不用等你选中效果、也不用等播放链。
///
/// 为什么要有这个：这几轮反复出现"我改了、对面看到的还是老样子"，而光看画面
/// 分不出到底是【构建没进 exe】、【跑的是另一个实例】、还是【效果本身没做对】。
/// 这个文件是最直接的判据：台阶几级、升高多少、包围盒多大，一读就知道。
void SSDumpBuildInfo() {
    std::vector<float> mesh;
    SSBuildMesh(mesh);
    float mn[3] = { 1e9f, 1e9f, 1e9f }, mx[3] = { -1e9f, -1e9f, -1e9f };
    for (size_t v = 0; v + (size_t)SSVertexStride() <= mesh.size(); v += (size_t)SSVertexStride())
        for (int c = 0; c < 3; ++c) {
            const float f = mesh[v + c];
            if (f < mn[c]) mn[c] = f;
            if (f > mx[c]) mx[c] = f;
        }
    if (FILE* fp = fopen("ss_build.txt", "w")) {
        fprintf(fp, "build        %s %s\n", __DATE__, __TIME__);
        fprintf(fp, "台阶         %d x %d = %d 级\n", SS_NX, SS_NY, SS_NX * SS_NY);
        fprintf(fp, "每级         踏面 %.3f / 立面 %.3f（单调上升，不交错）\n", SS_STEP_D, SS_STEP_R);
        fprintf(fp, "SS_CAM_DIST  %.3f\n", SS_CAM_DIST);
        fprintf(fp, "顶点         %d\n", (int)(mesh.size() / SSVertexStride()));
        fprintf(fp, "包围盒       x[%.2f, %.2f]  y[%.2f, %.2f]  z[%.2f, %.2f]\n",
                mn[0], mx[0], mn[1], mx[1], mn[2], mx[2]);
        fprintf(fp, "z 跨度       %.2f  (≈ 级数×立面 才说明每一级都比前一级高进了构建)\n", mx[2] - mn[2]);
        fclose(fp);
    }
}

static void EnsureSSRenderer() {
    if (g_ssRenderer.vao) return;
    std::vector<float> mesh;
    SSBuildMesh(mesh);
    g_ssRenderer.vcount = (GLsizei)(mesh.size() / SSVertexStride());


    glGenVertexArrays(1, &g_ssRenderer.vao);
    glBindVertexArray(g_ssRenderer.vao);
    glGenBuffers(1, &g_ssRenderer.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_ssRenderer.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(mesh.size() * sizeof(float)),
                 mesh.data(), GL_STATIC_DRAW);
    const size_t stride = SSVertexStride() * sizeof(float);   // 8 float：pos3 + cell2 + nrm3
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)0);                  // 位置
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)(3 * sizeof(float))); // 格子坐标
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)(5 * sizeof(float))); // 面法线
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
    g_ssRenderer.prog = CompileProgram(kSSVS, kSSFS, "Involute_1");
}

static void RenderInvolute_1(float t, float w, float h, float opacity, bool darkBg) {
    EnsureSSRenderer();
    GLuint prog = g_ssRenderer.prog;
    if (!prog || !g_ssRenderer.vcount) return;
    int wi = (int)w, hi = (int)h;
    if (wi < 2 || hi < 2) return;
    EnsureBlurTrio(g_ssScene, g_ssBlurA, g_ssBlurB, wi, hi, g_ssFboW, g_ssFboH, false);
    if (!g_ssScene.fb) return;

    glUseProgram(prog);
    auto U = [prog](const char* n) { return glGetUniformLocation(prog, n); };

    // 用真实帧间差而不是 uTime —— 切效果回来时不会跳变
    static float s_lastT = -1.0f;
    float dt = (s_lastT < 0.0f || t < s_lastT) ? 0.0f : (t - s_lastT);
    s_lastT = t;
    if (dt > 0.1f) dt = 0.1f;   // 卡顿/最小化之后别让相机一步跳完

    // ---- hit：相机在球面上随机变速地转位 + 台阶开始弹 ----
    // 触发用 beatFlash 的【上升沿】（同 Involute / StellarVortex）：它平时只衰减，
    // 只有新鼓点会把它顶回高位，所以"比上一帧大"就是新鼓点。
    static float s_prevFlash = 0.0f;
    // 【曲变直过渡期间：鼓点一概不接】—— 过渡中途镜头甩 120°~180° 会把正在变形的
    // 楼梯整个掀翻，台阶弹跳也会把"边扭曲边过渡"搅成两件事一起发生。
    // 用户的要求是"过渡时不受任何因素影响，播放完再受"。
    // ⚠ s_prevFlash 照常更新（放在 if 外面）：否则锁一解除就会补一记攒了 1.4 秒的鼓点。
    if (!g_plVxLock && g_af.beatFlash > s_prevFlash + 1e-4f) {
        // 【先把"上一拍"记下来再归零】：此刻的 g_ssHitAge 就是与上一拍的间隔，
        // 而它马上要被清零。这两个值决定上一拍的余波还剩多少（见 kSSVS 的两拍叠加）。
        g_ssPrevGap    = g_ssHitAge;
        g_ssPrevHitIdx = g_ssHitIdx;
        g_ssHitIdx     = (float)g_beatCount;
        g_ssHitAge = 0.0f;
        // 随机游走而不是绝对赋值：连续几次鼓点读起来是"绕着板转"，而不是每拍硬跳到不相干的方向。
        //
        // 【每次转位至少 120° 弧】——用户定的下界。相机在半径 SS_CAM_DIST 的球面上跑，
        // 120° 对应的弧长 = 17.0 × 2.0944 ≈ 35.6 世界单位；转的角小了就不是"绕过去"，
        // 只是原地抖一下，鼓点读起来没有位移。
        // 所以步长直接在 [120°, 180°] 里取、方向随机 —— 【下界是保证出来的，不是碰运气】。
        // （旧写法是 (rand−0.5)·3.0 ∈ [−86°, +86°]，【上界都够不到 120°】，所以经常抖一下。）
        {
            const float kSwingMin = 2.0943951f;   // 120°
            const float kSwingMax = 3.1415927f;   // 180°
            float swing = kSwingMin + cubeRnd((int)g_beatCount, 8101) * (kSwingMax - kSwingMin);
            float dir   = (cubeRnd((int)g_beatCount, 8104) < 0.5f) ? -1.0f : 1.0f;
            g_ssCamYawT += dir * swing;
        }
        g_ssCamPitchT += (cubeRnd((int)g_beatCount, 8102) - 0.5f) * 1.0f;
        if (g_ssCamPitchT >  kSSCamPitchMax) g_ssCamPitchT =  kSSCamPitchMax;
        if (g_ssCamPitchT < -kSSCamPitchMax) g_ssCamPitchT = -kSSCamPitchMax;
        // 【起点取"此刻真正在哪儿"】：鼓点密的时候上一段还没走完就来了下一拍，
        // 从当前位置重新起步才不会跳（S 曲线是按起点算的，起点取错就会当场一顿）。
        g_ssCamYawFrom = g_ssCamYaw; g_ssCamPitchFrom = g_ssCamPitch;
        // 【这就是"随机变加速"】：每次 hit 的时长都不同 —— 短的猛地一甩，长的慢慢漂过去。
        g_ssCamDur = kSSCamDurMin
                   + cubeRnd((int)g_beatCount, 8103) * (kSSCamDurMax - kSSCamDurMin);
        g_ssCamT = 0.0f;
        // 偏航是【随机游走】，会无限累积：每拍 120°~180°，几分钟就是几百弧度，
        // float 精度掉光之后缓动会开始抖。归一到 [-π, π) ——
        // 起点、目标、当前值【三个一起】搬 2π 的整数倍，正在走的那段弧一点不变。
        while (g_ssCamYawT   >  3.1415927f) { g_ssCamYawT -= 6.2831853f; g_ssCamYaw -= 6.2831853f; g_ssCamYawFrom -= 6.2831853f; }
        while (g_ssCamYawT   < -3.1415927f) { g_ssCamYawT += 6.2831853f; g_ssCamYaw += 6.2831853f; g_ssCamYawFrom += 6.2831853f; }
    }
    s_prevFlash = g_af.beatFlash;
    g_ssHitAge += dt;
    // ---- 相机按【过山车曲线】走完这次转位 ----
    // 用户的原话："先缓慢移一下，然后突然加速到很快，然后又慢下来，像过山车一样"。
    //   · 原来是 `yaw += (yawT−yaw)·(1−exp(−dt·rate))`：起步最快、越近越慢 —— 反的。
    //   · 我上一版改成了"两头留白的 smoothstep(0.18,0.82)"，但前 18% 是【完全不动】——
    //     那是"干等"，不是"缓慢移一下"（用户当场否掉的就是这个）。
    // 所以换成归一化 tanh：速度剖面 sech²，两头按指数衰减、中间一个尖峰。
    //   u   = 这次转位的进度 [0,1]
    //   e   = (tanh(k(u−0.5)) + tanh(k/2)) / (2·tanh(k/2))     ← 保证 e(0)=0、e(1)=1
    //   yaw = From + (T − From)·e
    // k=4 时的实测剖面（见下方注释里的注释）：起步就有 7% 的速度（在动，不是静止），
    // 中段冲到 2.07 倍均速，收尾再指数式收住。
    // 附带好处：时长有限，转位有明确的"到达"时刻，不像 exp 那样永远差一点点。
    g_ssCamT += dt;
    {
        float u = g_ssCamT / std::max(g_ssCamDur, 1e-3f);
        u = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
        const float h  = std::tanh(kSSCamEaseK * 0.5f);
        const float e  = (std::tanh(kSSCamEaseK * (u - 0.5f)) + h) / std::max(2.0f * h, 1e-6f);
        g_ssCamYaw   = g_ssCamYawFrom   + (g_ssCamYawT   - g_ssCamYawFrom)   * e;
        g_ssCamPitch = g_ssCamPitchFrom + (g_ssCamPitchT - g_ssCamPitchFrom) * e;
    }

    // 【过渡时相机必须一起变】——这是整件事的关键，我前面四轮全漏了。
    // Involute 是"相机在管子里面沿轴飞"，Involute_1 是"相机在外面绕球面转"，
    // 两套完全不同的视角。只卷几何、不动相机的话，叠出来永远是两种视角的糊，
    // 看不出任何"变形"。
    //   mixT = 1 → 相机退到轴上、朝前看（就是管内的样子），几何同时卷到最紧
    //   mixT = 0 → 正常的轨道视角，几何是直的
    const float mixT = g_ssTransMix;
    glUniform1f(U("uCamYaw"),   g_ssCamYaw   * (1.0f - mixT));
    glUniform1f(U("uCamPitch"), g_ssCamPitch * (1.0f - mixT));
    // 【相机从"绕着板转"挪到"站在管轴的一端往前看"】——
    //   平放：距离 SS_CAM_DIST(7)，绕原点（= 板的几何中心）转
    //   卷起：距离 SS_HALF_H()(= 半个梯段长 = 15)，落在 z 轴上、yaw/pitch 已经收到 0，
    //         于是相机就在管轴上；管子（z∈[-15,15]）整条落在镜头前方 0~30。
    // 这个 15 不是拍脑袋：管子的可见长度必须够，而雾在 20.5 处就把远端淡没了
    // （与 Involute 同一个常数）→ 隧道口看不见，两边观感一致。
    glUniform1f(U("uCamDist"),  SS_CAM_DIST + (SS_HALF_H() - SS_CAM_DIST) * mixT);
    glUniform1f(U("uFocal"),    1.0f / 0.4452f);       // fov ≈ 48°（与 Involute 一致）
    glUniform2f(U("uResolution"), w, h);
    glUniform1f(U("uExitTime"), g_plExitT);
    glUniform1f(U("uHitAge"),   g_ssHitAge);
    // 【用 g_ssHitIdx 而不是 g_beatCount】：两拍叠加里"这一拍"和"上一拍"必须是
    // 【同一次 hit 记下来的那一对序号】，否则第二项会对到别的拍上去。
    glUniform1f(U("uHitIndex"),    g_ssHitIdx);
    glUniform1f(U("uPrevHitIdx"),  g_ssPrevHitIdx);
    glUniform1f(U("uPrevGap"),     g_ssPrevGap);
    glUniform1f(U("uCellNX"),   (float)SS_NX);         // 由 SSGeom.h 唯一定义，不写死
    glUniform1f(U("uCellNY"),   (float)SS_NY);
    // 只增不减：楼梯永远往上流。回绕到 [0, 梯段长) —— 每 30 又是一模一样的梯段，
    // 存成有界值才不会跑几个小时后 float 精度掉光（着色器里还要对它取 mod）。
    const float kSSWrapLen = (float)SS_NX * SS_STEP_D;
    g_ssPhase += dt * kSSFlowSpeed;
    if (g_ssPhase >= kSSWrapLen) g_ssPhase -= kSSWrapLen;
    glUniform1f(U("uPhase"),   g_ssPhase);
    // 【推进速率对齐 Involute】—— 交叉淡化那一秒两边形状已经重合，如果推进速率不同，
    // 两条同形的管子会以不同速度沿螺旋爬，叠出来是重影（用户的原话："一个有 movement，
    // 另一个干巴巴的等着指令，多割裂"）。
    // uAdvance = Involute 的相位 − 本效果自己已经走掉的流动相位：本效果的流动已经把
    // 台阶往前推了 g_ssPhase（在旋转上等价于转 g_ssPhase·2π/周期），补上差值之后
    // 合计正好等于 Involute 的相位，速率逐帧一致。只在卷起来时有意义（th 里乘了 uMix）。
    // 取模到 [0, 周期)：相位差整整一个周期对螺旋是恒等变换（且避免大参数的精度问题）。
    float adv = fmodf(g_vxPhase - g_ssPhase, VX_PERIOD_LEN);
    if (adv < 0.0f) adv += VX_PERIOD_LEN;
    glUniform1f(U("uAdvance"), adv);
    // 第二形态也跟着 Involute（见 VXFormDisp）。推进在 RenderInvolute 里，
    // 这里只读，所以会慢一帧 —— 0.85 秒的动画上差一帧看不出来。
    glUniform1f(U("uRadialPush"), VXFormDisp() * kVXFormDist);
    // 【材质过渡】Involute 那侧的材质参数：配色用它【当前】的（它鼓点会换色），
    // 外缘半径也带上第二形态的分离量 —— 与 RenderInvolute 里上传给 uVXRMax 的是同一份。
    {
        float va[4] = { g_vxColNow[0][0], g_vxColNow[0][1], g_vxColNow[0][2], 1.0f };
        float vb[4] = { g_vxColNow[1][0], g_vxColNow[1][1], g_vxColNow[1][2], 1.0f };
        glUniform4fv(U("uVXColA"), 1, va);
        glUniform4fv(U("uVXColB"), 1, vb);
        glUniform1f(U("uVXRMax"), VXMaxRadius() + VXFormDisp() * kVXFormDist);
    }
    glUniform1f(U("uStepD"),   SS_STEP_D);
    glUniform1f(U("uStepR"),   SS_STEP_R);
    glUniform1f(U("uStepLen"), (float)SS_NX * SS_STEP_D);
    glUniform1f(U("uYMin"),    -(float)SS_NX * SS_STEP_D * 0.5f);
    // 【渐变的铺色范围 = 上升（y）的范围】，不是梯段长度的范围。
    // 这两个量在"轴改名"之后一直混着用：uniform 名字叫 uYLo/uYHi，算的却是 SS_STEP_D（z 的步长）。
    // 结果是上升 0~20.5 被塞进 ±15 的窗口里 —— 渐变只用得到一半还被截断在 1.0。
    glUniform1f(U("uYLo"),     -(float)SS_NX * SS_STEP_R * 0.5f);
    glUniform1f(U("uYHi"),      (float)SS_NX * SS_STEP_R * 0.5f);
    // 【曲变直】的三个量都由 SSGeom.h / VXGeom.h 唯一定义，不在这里抄常数 ——
    // 抄一份就等着漂移（这次的宽度就吃过一次亏：一边写一半，卷出来是半张壳）。
    glUniform1f(U("uMix"),    mixT);
    glUniform1f(U("uLeafR"),  VX_R);
    glUniform1f(U("uPeriod"), VX_PERIOD_LEN);
    glUniform1f(U("uBounceAmp"),    kSSBounceAmp);
    glUniform1f(U("uBounceSpread"), kSSBounceSpread);
    glUniform1f(U("uHalfW"), SS_HALF_W);
    glUniform1f(U("uHalfH"), SS_HALF_H());
    glUniform1f(U("uSat"),   1.15f);                   // 固定饱和度（本效果不接质心）
    glUniform1f(U("uRimW"),  kSSRimW);
    glUniform1f(U("uRimI"),  kSSRimI);
    {
        // 第 4 个分量恒为 1：走 glUniform4fv 只是因为 loader 没加载 glUniform3f/3fv
        static float cA[4] = { 0.20f, 0.55f, 1.00f, 1.0f };   // 蓝
        static float cB[4] = { 1.00f, 0.62f, 0.18f, 1.0f };   // 琥珀
        glUniform4fv(U("uColA"), 1, cA);
        glUniform4fv(U("uColB"), 1, cB);
    }
    glUniform1i(U("uDarkBg"), darkBg ? 1 : 0);
    glUniform1f(U("uOpacity"), opacity);
    glUniform1f(U("uExposure"), 1.15f);
    glUniform1f(U("uBloomAmt"), 0.85f);
    glUniform1f(U("uBloomThresh"), 0.42f);
    // shader 侧协议保留、引擎侧恒写"正常趟"（见 VX 那处说明）
    glUniform1i(U("uEncode"), 0);
    glUniform1f(U("uMorphBlend"), 0.0f);

    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

    // ---- ① 场景 = 背景趟 + 几何趟，都画进 RGBA16F 离屏缓冲 ----
    // 必须离屏：8 位主缓冲在 1.0 处截断，亮部提取就没有源头
    glBindFramebuffer(GL_FRAMEBUFFER, g_ssScene.fb);
    glViewport(0, 0, wi, hi);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glUniform1i(U("uPass"), 0);
    glDisable(GL_BLEND);
    glBindVertexArray(g_vao);          // g_vao = NDC 全屏四边形
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glUniform1i(U("uPass"), 1);
    // 【几何趟必须开混合】：alpha = vFog，雾淡出的部分要真的淡到背景里去。
    // 关着混合写死 alpha 的话，管子近端/远端那圈"看不见的几何"会用纯黑盖住背景，
    // 浅色底一负片就是正中间一坨白斑（Involute 的几何趟一直是这么开的）。
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(g_ssRenderer.vao);
    // 【画 kSSRep 份、每份错开一个梯段长】—— 让梯段在画面外接下去，
    // 用户看到的永远只是一条穿场而过的长梯，看不到头尾。
    // 每份都是独立的一次 draw（本项目没有实例化绘制，这也正合 Involute 多层的路子）。
    for (int r = 0; r < kSSRep; ++r) {
        const float half = (float)(kSSRep - 1) * 0.5f;
        glUniform1f(U("uRepShift"), ((float)r - half) * (float)SS_NX * SS_STEP_D);
        glDrawArrays(GL_TRIANGLES, 0, g_ssRenderer.vcount);
    }
    glBindVertexArray(0);
    glDisable(GL_BLEND);

    // ---- ② 泛光：亮部提取到半分辨率 + 两轮分离式高斯 ----
    const int bw = wi/2, bh = hi/2;   // EnsureBlurTrio 已保证缓冲存在
    glUniform2f(U("uResolution"), (float)bw, (float)bh);
    glUniform2f(U("uTexel"), 1.0f/bw, 1.0f/bh);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_ssScene.tex);
    glUniform1i(U("uTexScene"), 0);
    glUniform1i(U("uPass"), 2);
    glBindFramebuffer(GL_FRAMEBUFFER, g_ssBlurA.fb);
    glViewport(0, 0, bw, bh);
    glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glUniform1i(U("uPass"), 3);
    glUniform1i(U("uTexBloom"), 0);
    const float radii[2] = { 1.7f, 3.6f };
    for (int pass = 0; pass < 2; ++pass) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_ssBlurA.tex);
        glBindFramebuffer(GL_FRAMEBUFFER, g_ssBlurB.fb);
        glViewport(0, 0, bw, bh);
        glUniform2f(U("uBlurDir"), radii[pass], 0.0f);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_ssBlurB.tex);
        glBindFramebuffer(GL_FRAMEBUFFER, g_ssBlurA.fb);
        glViewport(0, 0, bw, bh);
        glUniform2f(U("uBlurDir"), 0.0f, radii[pass]);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    // ---- ③ 合成回原目标 ----
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
    glViewport(0, 0, wi, hi);
    glUniform2f(U("uResolution"), w, h);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_ssScene.tex);
    glUniform1i(U("uTexScene"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, g_ssBlurA.tex);
    glUniform1i(U("uTexBloom"), 1);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(U("uPass"), 4);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}

// ===========================================================================
// Function1 —— Zindler conoid（新族 FAM_3D 的第一个效果）
// ===========================================================================
// 几何/公式/movement 全部在 VisualEffects/F1/F1Geom.h 里，这里只负责画。
// 【顶点缓冲只存归一化的 (u,v) 参数】，曲面点和法线都在 VS 里按 uniform 现算 ——
// movement 每帧都在改 u/v 的范围，把点烘死在 VBO 里就得每帧重建整条缓冲；
// 存参数的话网格只建一次（84×50），每帧只更新几个 uniform。
// 法线用 ∂P/∂u × ∂P/∂v 的中心差分：它是 uniform 的纯函数，逐帧确定、可复现
// （和 dFdx/dFdy 那种屏幕空间导数不是一回事 —— 那个在本项目里被证实会抖）。
// ===========================================================================
// kF1VS / kF1FS 两段着色器已移到 VisualEffects/F1/F1Shaders.h（顶部 include 进来了）。

struct F1Renderer { GLuint vao = 0, vbo = 0, ebo = 0, prog = 0; GLsizei vcount = 0; GLsizei icount = 0; };
static F1Renderer g_f1Renderer;
static ColorFbo g_f1Scene, g_f1BlurA, g_f1BlurB;
static int g_f1FboW = 0, g_f1FboH = 0;

// ---------------------------------------------------------------------------
// movement 状态
// ---------------------------------------------------------------------------
static float g_f1EntryT  = 0.0f;      // 入场已经走了几秒
static float g_f1HitAge  = 999.0f;    // 距上次鼓点的秒数（没 hit 过给大数 → 回落到空闲段）
static float g_f1Energy  = 0.0f;      // 音频能量（驱动空闲段）
// ---------------------------------------------------------------------------
// 相机：**与 Involute_1 同一套**（用户 2026-09-17 要求"加上和 Involute1 一样的镜头受 hit 影响"）
// ---------------------------------------------------------------------------
// 所以这里不重写一套，规则和常数都跟 SS 那边一致：
//   · 每次鼓点转位【至少 120° 弧】、方向随机（下界是保证出来的，不是碰运气）；
//   · 缓动是【过山车曲线】（归一化 tanh，速度快慢快），不是指数逼近；
//   · 一次转位的时长每次重掷 = "随机变加速"；
//   · 偏航是随机游走，三个值一起搬 2π 归一到 [−π,π)。
static float g_f1CamYaw = 0.90f, g_f1CamYawT = 0.90f;
static float g_f1CamPitch = 0.42f, g_f1CamPitchT = 0.42f;
static float g_f1CamYawFrom = 0.90f, g_f1CamPitchFrom = 0.42f;
static float g_f1CamT = 99.0f;       // 本次转位已经走了几秒
static float g_f1CamDur = 0.7f;      // 本次转位的总时长
const float kF1CamDist = 6.00f;      // 相机到几何中心的恒定距离

static void ResetF1Renderer() {
    // GL 上下文每次重开都会失效 —— 不清零就会拿旧句柄去画（本文件反复出现的崩溃模式）
    g_f1Renderer.vao = 0; g_f1Renderer.vbo = 0; g_f1Renderer.ebo = 0; g_f1Renderer.prog = 0;
    g_f1Renderer.vcount = 0; g_f1Renderer.icount = 0;
    g_f1EntryT = 0.0f;                  // 重开时重新播入场
    g_f1HitAge = 999.0f;
    g_f1Energy = 0.0f;
    g_f1Scene = ColorFbo(); g_f1BlurA = ColorFbo(); g_f1BlurB = ColorFbo();
    g_f1FboW = g_f1FboH = 0;
}

static void EnsureF1Renderer() {
    if (g_f1Renderer.vao) return;
    std::vector<float> grid;
    F1BuildGrid(grid);
    g_f1Renderer.vcount = (GLsizei)(grid.size() / F1VertexStride());
    std::vector<unsigned short> idx;
    F1BuildIndices(idx);
    g_f1Renderer.icount = (GLsizei)idx.size();

    glGenVertexArrays(1, &g_f1Renderer.vao);
    glBindVertexArray(g_f1Renderer.vao);
    glGenBuffers(1, &g_f1Renderer.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_f1Renderer.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(grid.size() * sizeof(float)),
                 grid.data(), GL_STATIC_DRAW);
    const size_t stride = F1VertexStride() * sizeof(float);   // 2 float：(uNorm, vNorm)
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)0);
    glEnableVertexAttribArray(0);
    // 【索引缓冲】：参数网格的顶点是共享的，必须走 DrawElements（见 F1Geom.h 的说明）
    glGenBuffers(1, &g_f1Renderer.ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_f1Renderer.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(idx.size() * sizeof(unsigned short)),
                 idx.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);   // 解绑 VAO 之前先解 EBO，免得被后面的绑定串味
    g_f1Renderer.prog = CompileProgram(kF1VS, kF1FS, "Function1");
}

static void RenderFunction1(float t, float w, float h, float opacity, bool darkBg) {
    EnsureF1Renderer();
    GLuint prog = g_f1Renderer.prog;
    if (!prog || !g_f1Renderer.vcount) return;
    int wi = (int)w, hi = (int)h;
    if (wi < 2 || hi < 2) return;
    EnsureBlurTrio(g_f1Scene, g_f1BlurA, g_f1BlurB, wi, hi, g_f1FboW, g_f1FboH, true);
    if (!g_f1Scene.fb) return;

    glUseProgram(prog);
    auto U = [prog](const char* n) { return glGetUniformLocation(prog, n); };

    // 用真实帧间差而不是 uTime —— 切效果回来时不会跳变
    static float s_lastT = -1.0f;
    float dt = (s_lastT < 0.0f || t < s_lastT) ? 0.0f : (t - s_lastT);
    s_lastT = t;
    if (dt > 0.1f) dt = 0.1f;   // 卡顿/最小化之后别让入场一步跳完

    // ---- 鼓点：触发用 beatFlash 的【上升沿】（同 Involute / StellarVortex / Involute_1）----
    // beatFlash 平时只衰减、只有新鼓点把它顶回高位，所以"比上一帧大"就是新鼓点。
    // ⚠ 这个 hit 本身来自 BPMdetector（UpdateBeat 里 g_drums.Process），不是另起一套判据。
    static float s_prevFlash = 0.0f;
    if (g_af.beatFlash > s_prevFlash + 1e-4f) {
        g_f1HitAge = 0.0f;                       // v 范围冲顶
        // ---- 镜头转位：与 Involute_1 同一套规则（见 globals 处的说明）----
        g_f1CamYawFrom = g_f1CamYaw; g_f1CamPitchFrom = g_f1CamPitch;
        {
            const float kSwingMin = 2.0943951f;   // 120°：用户给的下界
            const float kSwingMax = 3.1415927f;   // 180°
            float swing = kSwingMin + cubeRnd((int)g_beatCount, 8201) * (kSwingMax - kSwingMin);
            float dir   = (cubeRnd((int)g_beatCount, 8204) < 0.5f) ? -1.0f : 1.0f;
            g_f1CamYawT += dir * swing;
        }
        g_f1CamPitchT += (cubeRnd((int)g_beatCount, 8202) - 0.5f) * 1.0f;
        if (g_f1CamPitchT >  kSSCamPitchMax) g_f1CamPitchT =  kSSCamPitchMax;
        if (g_f1CamPitchT < -kSSCamPitchMax) g_f1CamPitchT = -kSSCamPitchMax;
        g_f1CamDur = kSSCamDurMin + cubeRnd((int)g_beatCount, 8203) * (kSSCamDurMax - kSSCamDurMin);
        g_f1CamT = 0.0f;
        // 随机游走会无限累积，三个值一起搬 2π 归一（正在走的那段弧不受影响）
        while (g_f1CamYawT >  3.1415927f) { g_f1CamYawT -= 6.2831853f; g_f1CamYaw -= 6.2831853f; g_f1CamYawFrom -= 6.2831853f; }
        while (g_f1CamYawT < -3.1415927f) { g_f1CamYawT += 6.2831853f; g_f1CamYaw += 6.2831853f; g_f1CamYawFrom += 6.2831853f; }
    }
    s_prevFlash = g_af.beatFlash;
    g_f1HitAge += dt;
    // 相机按过山车曲线走完这次转位（与 Involute_1 同一个 kSSCamEaseK / tanh 公式）
    g_f1CamT += dt;
    {
        float u = g_f1CamT / std::max(g_f1CamDur, 1e-3f);
        u = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
        const float h  = std::tanh(kSSCamEaseK * 0.5f);
        const float e  = (std::tanh(kSSCamEaseK * (u - 0.5f)) + h) / std::max(2.0f * h, 1e-6f);
        g_f1CamYaw   = g_f1CamYawFrom   + (g_f1CamYawT   - g_f1CamYawFrom)   * e;
        g_f1CamPitch = g_f1CamPitchFrom + (g_f1CamPitchT - g_f1CamPitchFrom) * e;
    }
    // 空闲段由音频能量驱动（"轻度受影响"）。energy 已被全局强度缩放过，这里再夹一次 [0,1]。
    g_f1Energy = g_af.energy; if (g_f1Energy < 0.0f) g_f1Energy = 0.0f; if (g_f1Energy > 1.0f) g_f1Energy = 1.0f;
    g_f1EntryT += dt;

    const float f1U = F1EntryFrac(g_f1EntryT);               // 入场：0 → 1
    const float f1V = F1VFrac(g_f1Energy, g_f1HitAge);
    glUniform1f(U("uU"),     f1U);
    glUniform1f(U("uV"),     f1V);
    glUniform1f(U("uPhi"),   6.28318530717959f / 3.0f);      // 公式里的 2π/3
    glUniform1f(U("uScale"), F1_SCALE);
    glUniform1f(U("uCamYaw"),   g_f1CamYaw);
    glUniform1f(U("uCamPitch"), g_f1CamPitch);
    glUniform1f(U("uCamDist"),  kF1CamDist);
    glUniform1f(U("uFocal"), 1.0f / 0.4452f);                // fov ≈ 48°，与其它 mesh 效果一致
    glUniform2f(U("uResolution"), w, h);
    glUniform1f(U("uExitTime"), g_plExitT);
    glUniform1f(U("uTime"), t);
    glUniform1f(U("uOpacity"), opacity);
    glUniform1f(U("uSat"), 1.15f);
    glUniform1f(U("uExposure"), 1.15f);
    glUniform1f(U("uBloomAmt"), 0.85f);
    glUniform1f(U("uBloomThresh"), 0.42f);
    glUniform1i(U("uDarkBg"), darkBg ? 1 : 0);
    {   // 【相机位置，必须在"物体被旋转之后"的坐标系里】——
        // VS 转的是【物体】（q.xz/yx 各乘一个旋转矩阵），相机永远在 (0,0,uCamDist) 朝 −z 看，
        // 所以这里也要传 (0,0,camDist)。传世界系的 eye = (d·cp·sy, d·sp, d·cp·cy) 是错的：
        // 那是"相机绕物体转"的等价写法，和 VS 里"物体绕原点转"差了整整一个旋转 ——
        // 后果是 FS 里 dot(n,V) 判错、背面翻不过来，整张曲面几乎全黑（实测就是这样）。
        // vec3 只能走 glUniform1fv —— 本项目的 loader 没加载 glUniform3f，写了会【静默无效】。
        float eye[3] = { 0.0f, 0.0f, kF1CamDist };
        glUniform1fv(U("uEye"), 3, eye);
    }
    {   // 第 4 个分量恒为 1：走 glUniform4fv 只是因为 loader 没加载 glUniform3f/3fv
        static float cA[4] = { 0.16f, 0.55f, 1.00f, 1.0f };   // 蓝
        static float cB[4] = { 1.00f, 0.28f, 0.42f, 1.0f };   // 玫红
        glUniform4fv(U("uColA"), 1, cA);
        glUniform4fv(U("uColB"), 1, cB);
    }
    // shader 侧协议保留、引擎侧恒写"正常趟"（见 VX 那处说明）
    glUniform1i(U("uEncode"), 0);
    glUniform1f(U("uMorphBlend"), 0.0f);

    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);   // 合成趟要回到调用者的 framebuffer

    glBindFramebuffer(GL_FRAMEBUFFER, g_f1Scene.fb);
    glViewport(0, 0, wi, hi);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glUniform1i(U("uPass"), 0);
    glDisable(GL_BLEND);
    glBindVertexArray(g_vao);          // g_vao = NDC 全屏四边形
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glUniform1i(U("uPass"), 1);
    // 【几何趟开深度】VS 里写的是标准透视（w = 视深、z 是标准透视项），
    // 所以默认的 GL_LESS 就是"近挡远"。必须先清深度，否则会继承上一帧的。
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glBindVertexArray(g_f1Renderer.vao);
    glDrawElements(GL_TRIANGLES, g_f1Renderer.icount, GL_UNSIGNED_SHORT, 0);
    glBindVertexArray(0);
    glDisable(GL_DEPTH_TEST);   // 后面的亮部/模糊/合成都是全屏趟，不该被深度影响

    // ---- 泛光：亮部提取到半分辨率 + 两轮分离式高斯 ----
    const int bw = wi/2, bh = hi/2;   // EnsureBlurTrio 已保证缓冲存在
    glUniform2f(U("uResolution"), (float)bw, (float)bh);
    glUniform2f(U("uTexel"), 1.0f/bw, 1.0f/bh);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_f1Scene.tex);
    glUniform1i(U("uTexScene"), 0);
    glUniform1i(U("uPass"), 2);
    glBindFramebuffer(GL_FRAMEBUFFER, g_f1BlurA.fb);
    glViewport(0, 0, bw, bh);
    glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glUniform1i(U("uPass"), 3);
    glUniform1i(U("uTexBloom"), 0);
    const float radii[2] = { 1.7f, 3.6f };
    for (int pass = 0; pass < 2; ++pass) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_f1BlurA.tex);
        glBindFramebuffer(GL_FRAMEBUFFER, g_f1BlurB.fb);
        glViewport(0, 0, bw, bh);
        glUniform2f(U("uBlurDir"), radii[pass], 0.0f);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_f1BlurB.tex);
        glBindFramebuffer(GL_FRAMEBUFFER, g_f1BlurA.fb);
        glViewport(0, 0, bw, bh);
        glUniform2f(U("uBlurDir"), 0.0f, radii[pass]);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    // ---- 合成回原目标 ----
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
    glViewport(0, 0, wi, hi);
    glUniform2f(U("uResolution"), w, h);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_f1Scene.tex);
    glUniform1i(U("uTexScene"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, g_f1BlurA.tex);
    glUniform1i(U("uTexBloom"), 1);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(U("uPass"), 4);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}

// Filigree 的渲染器（程序编译 / FBO 三件套 / 六趟绘制 / 状态清零）已移到
// VisualEffects/TN/TNRender.cpp —— 这里只剩下面一行的调用。

// 效果程序是否已就绪（网格效果没有 program，由调用点自己用 isMesh 分流）
static bool EnsureEffectProgram(int idx) {
    if (idx < 0 || idx >= g_effectCount) return false;
    return g_effects[idx].program != 0;
}

// ============================================================================
// Pass 图 —— 一个效果可以由多遍组成
//
// 写法（frag 里）：
//   [共享头部：uniform 声明 + 公共函数]
//   //@pass warp inputs: prev, blur1, blur2, blur3
//   void main(){ ... }
//   //@pass comp inputs: last
//   void main(){ ... }
//
// 引擎把每个 pass 编译成独立程序（artifact + audio_prelude + 共享头部 + 本遍源码），
// 依次渲染到 ping-pong 链缓冲，最后一遍写当前 framebuffer。
// 可用输入：
//   prev   上一帧（uPrevFrame）        last   上一遍的结果（uLast）
//   blur1  1/2 分辨率模糊（uBlur1）    blur2  1/4（uBlur2）   blur3  1/8（uBlur3）
//
// 没有 //@pass 标记的 frag 走原来的单遍路径，行为完全不变。
// ============================================================================
static GLint PassU(PassDef& p, const char* n) {
    auto it = p.uniforms.find(n);
    if (it != p.uniforms.end()) return it->second;
    GLint loc = glGetUniformLocation(p.program, n);
    p.uniforms[n] = loc;
    return loc;
}

// 把 frag 按 //@pass 切成 [共享头部] + [若干 pass]。没有标记返回 false。
static bool SplitPasses(const std::string& src, std::string& shared, std::vector<PassDef>& out) {
    const std::string MARK = "//@pass";
    size_t first = src.find(MARK);
    if (first == std::string::npos) return false;
    shared = src.substr(0, first);
    size_t pos = first;
    while (pos != std::string::npos) {
        size_t lineEnd = src.find('\n', pos);
        if (lineEnd == std::string::npos) lineEnd = src.size();
        std::string head = src.substr(pos + MARK.size(), lineEnd - pos - MARK.size());
        size_t next = src.find(MARK, lineEnd);
        PassDef p;
        p.src = src.substr(lineEnd + 1, (next == std::string::npos ? src.size() : next) - lineEnd - 1);
        // head = " warp inputs: prev, blur1" → 名字 + 可选输入表
        size_t ip = head.find("inputs:");
        std::string namePart = (ip == std::string::npos) ? head : head.substr(0, ip);
        if (ip != std::string::npos) p.inputs = head.substr(ip + 7);
        // 去掉空白
        auto trim = [](std::string& s) {
            size_t a = s.find_first_not_of(" \t\r\n"); size_t b = s.find_last_not_of(" \t\r\n");
            s = (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
        };
        trim(namePart); trim(p.inputs);
        p.name = namePart.empty() ? "pass" : namePart;
        out.push_back(p);
        pos = next;
    }
    return !out.empty();
}

// 把一个 pass 的完整源码编译成程序（顶点着色器与单遍效果同一份）
static bool CompilePassProgram(PassDef& p, const std::string& source) {
    p.uniforms.clear();
    p.program = CompileProgram(kFullscreenVS, source.c_str(), "Pass");
    return p.program != 0;
}

static void EnsureFeedback(int w, int h);   // 定义在 FeedbackWarp 附近；自反馈效果要提前用它建纹理

// 自反馈效果：把上一帧纹理挂到 2 号纹理单元（uPrevFrame）。
// 首帧纹理还不存在时绑 0 —— 采样结果全黑，画面从黑里长出来，正是 MilkDrop 的行为。
static void BindPrevFrame(GpuEffect& fx, int w, int h) {
    if (!fx.milkFeedback) return;
    EnsureFeedback(w, h);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, g_fbPrevTex);
    glUniform1i(fx.U("uPrevFrame"), 2);      // 没声明这个 uniform 时位置=-1，GL 静默忽略
    glActiveTexture(GL_TEXTURE0);
}

// ---- pass 之间的链缓冲（ping-pong）----
static GLuint g_chainTex[2] = {0, 0}, g_chainFbo[2] = {0, 0};
static int    g_chainW = 0, g_chainH = 0;

// ---- 引擎模糊链：1/2、1/4、1/8 三档，由上一帧生成 ----
static GLuint g_blurTex[3] = {0, 0, 0}, g_blurFbo[3] = {0, 0, 0};
static GLuint g_blurTmpTex[3] = {0, 0, 0}, g_blurTmpFbo[3] = {0, 0, 0};   // 可分离模糊的中间趟
static int    g_blurW[3] = {0, 0, 0}, g_blurH[3] = {0, 0, 0};
static GLuint g_blurProg = 0;

static void MakeTexFbo(GLuint& tex, GLuint& fbo, int w, int h, GLenum filter) {
    glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    // 链缓冲/模糊链同样用 16F：pass 之间的中间结果允许 >1（曝光余量），
    // 模糊链在暗部也不会因为 1/255 的台阶出现色带。
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void EnsureChain(int w, int h) {
    if (w == g_chainW && h == g_chainH && g_chainFbo[0]) return;
    for (int i = 0; i < 2; ++i) {
        if (g_chainFbo[i]) { glDeleteFramebuffers(1, &g_chainFbo[i]); glDeleteTextures(1, &g_chainTex[i]); }
        MakeTexFbo(g_chainTex[i], g_chainFbo[i], w, h, GL_LINEAR);
    }
    g_chainW = w; g_chainH = h;
}

static void BuildBlurChain(int w, int h) {
    if (!g_blurProg) {
        const char* vs = R"(#version 330 core
layout(location=0)in vec2 p;layout(location=1)in vec2 u;out vec2 vUV;void main(){vUV=u;gl_Position=vec4(p,0,1);})";
        // 【可分离高斯】横向一遍 + 纵向一遍。MilkDrop 的 GetBlur1/2/3 就是这么做的：
        // 单遍 9 抽样只能覆盖 1~2 texel，细小的梯度根本留不住；分离两遍能把半径
        // 拉到十几 texel，而且每个方向都是真高斯，边缘不会出现采样纹。
        const char* fs = R"(#version 330 core
in vec2 vUV; out vec4 fragColor;
uniform sampler2D uSrc; uniform vec2 uDir;   // 方向 × (1/宽, 1/高)
void main(){
    vec3 c  = texture(uSrc, vUV).rgb * 0.2270270270;
    c += texture(uSrc, vUV + uDir * 1.3846153846).rgb * 0.3162162162;
    c += texture(uSrc, vUV - uDir * 1.3846153846).rgb * 0.3162162162;
    c += texture(uSrc, vUV + uDir * 3.2307692308).rgb * 0.0702702703;
    c += texture(uSrc, vUV - uDir * 3.2307692308).rgb * 0.0702702703;
    fragColor = vec4(c, 1.0);
})";
        GLuint v = glCreateShader(GL_VERTEX_SHADER); glShaderSource(v, 1, &vs, 0); glCompileShader(v);
        GLuint f = glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(f, 1, &fs, 0); glCompileShader(f);
        g_blurProg = glCreateProgram(); glAttachShader(g_blurProg, v); glAttachShader(g_blurProg, f);
        glLinkProgram(g_blurProg); glDeleteShader(v); glDeleteShader(f);
    }
    for (int i = 0; i < 3; ++i) {
        int bw = w >> (i + 1), bh = h >> (i + 1);
        if (bw < 8) bw = 8; if (bh < 8) bh = 8;
        if (bw != g_blurW[i] || bh != g_blurH[i]) {
            if (g_blurFbo[i]) { glDeleteFramebuffers(1, &g_blurFbo[i]); glDeleteTextures(1, &g_blurTex[i]); }
            if (g_blurTmpFbo[i]) { glDeleteFramebuffers(1, &g_blurTmpFbo[i]); glDeleteTextures(1, &g_blurTmpTex[i]); }
            MakeTexFbo(g_blurTex[i], g_blurFbo[i], bw, bh, GL_LINEAR);
            MakeTexFbo(g_blurTmpTex[i], g_blurTmpFbo[i], bw, bh, GL_LINEAR);
            g_blurW[i] = bw; g_blurH[i] = bh;
        }
    }
    if (!g_fbPrevTex) return;
    GLuint srcTex = g_fbPrevTex; int cw = w, ch = h;
    glUseProgram(g_blurProg);
    glBindVertexArray(g_vao);
    GLint uSrcLoc = glGetUniformLocation(g_blurProg, "uSrc");
    GLint uDirLoc = glGetUniformLocation(g_blurProg, "uDir");
    glUniform1i(uSrcLoc, 0);
    for (int i = 0; i < 3; ++i) {
        const int bw = g_blurW[i], bh = g_blurH[i];
        // 横向：src → tmp
        glBindFramebuffer(GL_FRAMEBUFFER, g_blurTmpFbo[i]);
        glViewport(0, 0, bw, bh);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, srcTex);
        glUniform2f(uDirLoc, 1.0f / cw, 0.0f);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        // 纵向：tmp → blurTex[i]
        glBindFramebuffer(GL_FRAMEBUFFER, g_blurFbo[i]);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g_blurTmpTex[i]);
        glUniform2f(uDirLoc, 0.0f, 1.0f / bh);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        srcTex = g_blurTex[i]; cw = bw; ch = bh;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void BindPassInputs(PassDef& p, int passIndex, float w, float h) {
    auto has = [&](const char* k) { return p.inputs.find(k) != std::string::npos; };
    auto bind = [&](const char* uni, GLuint tex, int unit) {
        GLint l = PassU(p, uni);
        if (l < 0) return;
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, tex);
        glUniform1i(l, unit);
    };
    if (has("prev"))  bind("uPrevFrame", g_fbPrevTex, 2);
    if (has("last"))  bind("uLast", passIndex ? g_chainTex[(passIndex - 1) % 2] : 0, 4);
    if (has("blur1")) bind("uBlur1", g_blurTex[0], 8);
    if (has("blur2")) bind("uBlur2", g_blurTex[1], 9);
    if (has("blur3")) bind("uBlur3", g_blurTex[2], 10);
    (void)w; (void)h;
}

// 多遍效果：按声明顺序执行，中间结果进 ping-pong 链缓冲，最后一遍写调用者的 framebuffer。
static void DrawMultiPass(GpuEffect& fx, float t, float bpm, float phase, float speed,
                          int w, int h, bool darkBg, float opacity) {
    GLint savedFbo = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &savedFbo);

    bool needPrev = false, needBlur = false;
    for (auto& p : fx.passes) {
        if (p.inputs.find("prev")  != std::string::npos) needPrev = true;
        if (p.inputs.find("blur")  != std::string::npos) needBlur = true;
    }
    if (needPrev || needBlur) EnsureFeedback(w, h);
    if (needBlur) BuildBlurChain(w, h);
    EnsureChain(w, h);

    for (size_t i = 0; i < fx.passes.size(); ++i) {
        PassDef& p = fx.passes[i];
        const bool last = (i + 1 == fx.passes.size());
        glBindFramebuffer(GL_FRAMEBUFFER, last ? (GLuint)savedFbo : g_chainFbo[i % 2]);
        glViewport(0, 0, w, h);
        glUseProgram(p.program);
        // 通用 uniform（每遍都要，最后一遍还要工程协议那几个）
        glUniform1f(PassU(p, "uTime"), t);
        glUniform1f(PassU(p, "uTimeS"), t);
        glUniform1f(PassU(p, "uBpm"), bpm);
        glUniform1f(PassU(p, "uBeatPhase"), phase);
        glUniform1f(PassU(p, "uSpeed"), speed);
        glUniform2f(PassU(p, "uResolution"), (float)w, (float)h);
        glUniform1i(PassU(p, "uDarkBg"), darkBg ? 1 : 0);
        glUniform1f(PassU(p, "uOpacity"), opacity);
        glUniform1i(PassU(p, "uEncode"), 0);
        glUniform1f(PassU(p, "uMorphBlend"), 0.0f);
        glUniform1f(PassU(p, "uDitherStrength"), g_ditherStrength);
        glUniform1f(PassU(p, "uBassAtt"), g_af.bassAtt);
        glUniform1f(PassU(p, "uTrebAtt"), g_af.trebAtt);
        glUniform1f(PassU(p, "uBeatFlash"), g_af.beatFlash);
        glUniform1f(PassU(p, "uHitDrive"), g_hitDrive);
        glUniform1f(PassU(p, "uHitFlow"),      g_hitFlow);
        glUniform1f(PassU(p, "uHitFlowDrive"), g_hitFlowDrive);
        glUniform1f(PassU(p, "uQ28"), g_mdQ28);
        glUniform1f(PassU(p, "uHitKick"), g_af.hitKick);
        glUniform1f(PassU(p, "uHitSnare"), g_af.hitSnare);
        BindPassInputs(p, (int)i, (float)w, (float)h);
        glBindVertexArray(g_vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // 自反馈 + 多遍：第 1 遍的结果就是下一帧的"上一帧"（MilkDrop 的 sampler_main）。
        // 不能等整条链跑完再存 —— 那样存下来的是 comp 处理过的成品，色调映射会被二次压缩。
        if (i == 0 && fx.milkFeedback && g_fbPrevFbo) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, g_chainFbo[0]);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_fbPrevFbo);
            glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, savedFbo);
    glViewport(0, 0, w, h);
}

static void DrawEffect(int idx, float t, float bpm, float phase, float speed, float w, float h, bool darkBg, float opacity = 1.0f) {
    if (idx < 0 || idx >= g_effectCount) return;
    if (g_effects[idx].isMesh) {
        switch (g_effects[idx].meshSlot) {
            case 0: RenderB376Mesh(t, speed, w, h, darkBg, opacity); break;   // B376
            case 1: RenderMesh(g_m2Renderer, kM2Data, t, speed, w, h, darkBg, opacity); break;   // M2
            case 2: RenderMesh(g_m3Renderer, kM3Data, t, speed, w, h, darkBg, opacity); break;   // M3
            case 3: RenderMesh(g_m4Renderer, kM4Data, t, speed, w, h, darkBg, opacity); break;   // M4
            case 4: RenderLines(g_c3Renderer, t, w, h, opacity, false); break;   // Cubeeffect3 (dark)
            case 5: RenderLines(g_c3Renderer, t, w, h, opacity, true);  break;   // Cubeeffect3 (light)
            case 6: RenderParticles(t, w, h, opacity, darkBg); break;             // StellarVortex
            case 7: RenderInvolute(t, w, h, opacity, darkBg); break;              // Involute
            case 8: RenderInvolute_1(t, w, h, opacity, darkBg); break;             // Involute_1
            case 9: RenderFunction1(t, w, h, opacity, darkBg); break;              // Function1
            case 10: RenderFiligree(t, speed, w, h, opacity, darkBg, g_vao, g_af.fft, g_af.beatFlash, (unsigned)g_beatCount); break;   // Filigree
            case 11: RenderShardstorm(t, speed, w, h, opacity, darkBg, g_vao, g_af.beatFlash, (unsigned)g_beatCount, g_plExitT); break;   // Shardstorm
            default: break;
        }
        return;   // 顶点网格渲染
    }
    // 声明了 pass 图的效果走多遍路径（单遍效果不受影响）
    if (!g_effects[idx].passes.empty()) {
        DrawMultiPass(g_effects[idx], t, bpm, phase, speed, (int)w, (int)h, darkBg, opacity);
        return;
    }
    if (!EnsureEffectProgram(idx)) return;
    glUseProgram(g_effects[idx].program);
    g_effects[idx].BindUniforms(t, bpm, phase, speed, w, h, darkBg, opacity);
    BindPrevFrame(g_effects[idx], (int)w, (int)h);
    glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

// ===== 共享数据定义（CPU/GPU 后端共用）=====
enum BlendMode { BM_None=0, BM_Lighten, BM_Screen, BM_Overlay, BM_HardLight, BM_SoftLight, BM_LinearBurn, BM_Multiply, BM_COUNT };

struct PlaylistItem { std::wstring name, name2; BlendMode blendMode=BM_None; float opacity=1.0f; int durationSec=30; };

// ============================================================================
// CPU 渲染后端 —— GDI 效果 + TransitionEngine（匹配 ALL VSLZATION 的 PlaylistManager）
// 通过界面上的 CPU/GPU 切换按钮选择渲染模式
// ============================================================================

// 播放链窗口默认尺寸（CPU 渲染分辨率由动态像素预算决定，与窗口默认尺寸独立）
static const int PLAYER_DEFAULT_W = 512;
static const int PLAYER_DEFAULT_H = 350;

// 播放链窗口是【无边框】的：画面上方不再有那条白色标题栏。
// 顶部这 PLAYER_DRAG_H 像素在 WM_NCHITTEST 里仍报 HTCAPTION，所以照样能按住拖动，
// 右键也还能弹出系统菜单；窗口四周边缘保留 8px 的缩放热区（见 PlayerWndProc）。
static const int PLAYER_DRAG_H = 32;

// ===== 播放链 (GPU) =====
static std::vector<PlaylistItem> g_playlist;
static int g_plIndex = 0, g_plEffIdx[2] = {-1,-1}, g_plOldEffIdx = -1;
static float g_plElapsed = 0, g_plBlurT = 0;
enum PLState { PL_STOPPED, PL_PLAYING, PL_WAITBEAT, PL_BLUR };
static PLState g_plState = PL_STOPPED;
static float g_plTime = 0, g_plOldTime = 0, g_plBpm = 120, g_plPhase = 0;

// 旧效果的出场计时：不跟 g_plExitT 混用 —— 交叉淡入时两个效果要各自一个值
// （旧的在飞出、新的不该飞），而渲染读的是同一个全局。
static float g_plOldExitT = 999.0f;
static bool g_plBlackBg = false;
static bool g_plTopmost = true;      // 播放窗口是否置顶（设置对话框里可关）
static float g_userSpeed = 1.0f;
static float g_audioStrength = 1.0f;   // 音频驱动全局强度（临时滑杆；定稿后移除）
static const float BLUR_SEC = 1.20f;   // 跨效果过渡时长（MilkDrop 用户切 1.7s / 自动 2.7s，这里取折中）
static const int  kBarBeats  = 4;      // 一小节拍数（4/4 拍）：Cubeeffect1 双形态按小节切换
static int        g_formBarBeat = 0;   // 小节内拍计数（0..kBarBeats-1）

// 预设
static std::vector<PlaylistItem> g_presets[5];

static int EffectIndex(const std::wstring& name) {
    for (int i = 0; i < g_effectCount; ++i)
        if (g_effects[i].name == name) return i;
    return -1;
}

static void PLStart() {
    if (g_playlist.empty()) return;
    g_plIndex = 0; g_plElapsed = 0; g_plTime = 0; g_plExitT = 999.0f; g_b376FlowPhase = 0.0f; g_audioPhase = 0.0f; g_hitDrive = 0.0f; g_hitFlow = g_hitFlowDrive = 0.0f; g_hitCycleT = -1.0f; g_prevHitKick = 0.0f; g_mdVb = g_mdVvb = g_mdVm = g_mdVvm = g_mdVt = g_mdVvt = 0.0f; g_mdQ28 = 0.0f;
    g_m2Renderer.flowPhase = 0.0f; g_m3Renderer.flowPhase = 0.0f; g_m4Renderer.flowPhase = 0.0f;
    g_plEffIdx[0] = EffectIndex(g_playlist[0].name);
    g_plEffIdx[1] = g_playlist[0].name2.empty() ? -1 : EffectIndex(g_playlist[0].name2);
    g_plState = PL_PLAYING;
}

static void PLStop() { g_plState = PL_STOPPED; }

/// 换效果时 g_plTime 会归零，但锚点/折纸片里的 spawnT 是相对【旧时间轴】的。
/// 不跟着挪的话，shader 算出的 age = uTime - spawnT 会变成大负数，
/// exp(-age*4) 直接爆掉 → 全屏图案被甩飞。这里把所有时间戳整体平移到新时间轴，
/// 相对年龄（"刚出生 / 已经老了"）反而保持连续 —— 深色切浅色时网格的扭曲状态不会跳。
/// shift = 归零前的 g_plTime。
static void C3RebaseVeilTime(float shift) {
    for (size_t i = 0; i < g_cubeVeils.size(); ++i) g_cubeVeils[i].spawnT -= shift;
    for (size_t i = 0; i < g_c3Origami.size(); ++i) {
        g_c3Origami[i].spawnT -= shift;
        if (g_c3Origami[i].killT > 0.0f) g_c3Origami[i].killT -= shift;
    }
}

// 鼓点累计数（kick+snare）的声明已挪到 VX 那节之前（见「鼓点换色」），
// 因为 Involute 换配色也要拿它当种子。这里只是原来的位置，保留注释指路。

static void PLUpdate(float dt, float bpm, float phase, bool beat) {
    if (g_plState == PL_STOPPED || g_playlist.empty()) return;
    // 兜底：不在过渡里就一定是解锁状态（比如过渡中途停了播放链）。
    // 放在最前面 —— 效果切换块会把状态置成 PL_BLUR，那一帧在同一调用里又会上锁，
    // 所以这里不会把刚上的锁清掉。
    if (g_plState != PL_BLUR) g_plVxLock = false;
    g_plBpm = bpm; g_plPhase = phase;
    g_plTime += dt * g_userSpeed;    // 速度滑杆缩放时间轴本身（积分 → 改速度时不跳相位）
    // StellarVortex 曲线循环位置推进：鼓点是【唯一】信号 —— 没有 hit 就不动。
    //   这里只负责让位置以 1/kPCMorphDur 的速率追过去（目标由下面的 beat 分支 +1）。
    //   用"连续位置"而不是"每拍重启一次过渡"：鼓点比过渡快时，重启式会让过渡永远走不完、
    //   画面卡在两条曲线之间来回抖；连续位置则只是持续往下游走。
    //
    // 【曾经的"静音兜底"已删除】——那是一条"7 秒没鼓点就自己往前推一条"的规则，
    //   本意是免得没音乐时画面定死。但它不区分"音频暂停"和"音乐里的安静段落"，
    //   结果音频一停，形态照样每 7 秒变一次，看着像在响应不存在的鼓点。
    //   现在的取舍：没有鼓点就不该有形态变换。代价是长时间无鼓点的段落画面会
    //   定在一条曲线上（相机自转、粒子波动、星场闪烁仍在动，只是不换形态）。
    {
        if (g_pcCurvePos < g_pcCurveTarget) {
            g_pcCurvePos += dt / kPCMorphDur;
            if (g_pcCurvePos > g_pcCurveTarget) g_pcCurvePos = g_pcCurveTarget;
        }
        // 相机缓动：基础自转慢慢推目标，实际朝向按指数缓动去追。
        // 指数缓动的好处是【可随时重定向】—— 鼓点密集时目标不停变，画面也是平滑地改变方向，
        // 不会像固定时长插值那样每拍重来一遍。
        g_pcCamYawT += kPCSpin * dt;
        const float k = 1.0f - (float)std::exp(-dt / kPCCamEase);
        g_pcCamYaw   += (g_pcCamYawT   - g_pcCamYaw)   * k;
        g_pcCamPitch += (g_pcCamPitchT - g_pcCamPitch) * k;
        g_pcCamDist  += (g_pcCamDistT  - g_pcCamDist)  * k;
    }
    // 形态 blend 推进
    if (g_plEffIdx[0] >= 0 && g_plEffIdx[0] < g_effectCount) {
        auto& e = g_effects[g_plEffIdx[0]];
        if (e.formBlend < 1.0f) {
            e.formBlend += e.formBlendSpeed;
            if (e.formBlend >= 1.0f) { e.formBlend = 1.0f; e.form = e.targetForm; }
        }
        // 上层也推进
        if (g_plEffIdx[1] >= 0 && g_plEffIdx[1] < g_effectCount) {
            auto& e2 = g_effects[g_plEffIdx[1]];
            if (e2.formBlend < 1.0f) {
                e2.formBlend += e2.formBlendSpeed;
                if (e2.formBlend >= 1.0f) { e2.formBlend = 1.0f; e2.form = e2.targetForm; }
            }
        }
    }
    // 形态切换触发：默认每拍切；Cubeeffect1 双形态改为每小节（4 拍）切一次。
    bool barEdge = false;
    if (beat && (g_plState == PL_PLAYING || g_plState == PL_BLUR)) {
        g_formBarBeat++;
        if (g_formBarBeat >= kBarBeats) { g_formBarBeat = 0; barEdge = true; }   // 满 4 拍 = 小节边界
    }
    if ((g_plState == PL_PLAYING || g_plState == PL_BLUR) && g_plEffIdx[0] >= 0 && g_plEffIdx[0] < g_effectCount) {
        auto& e = g_effects[g_plEffIdx[0]];
        bool trig = (e.name == L"Cubeeffect1") ? barEdge : beat;   // Cubeeffect1 每小节，其余仍每拍
        if (trig) {
            e.targetForm = (e.form + 1) % e.formCount;
            e.formBlend = 0.0f;
            e.formBlendSpeed = 0.03f + (bpm - 40.0f) / 260.0f * 0.12f;
            // StellarVortex：每次鼓点【换一套调色板 + 往前走一条曲线】——
            // hit 在本项目里就是形态变换的信号（其它效果走 e.targetForm 那套）。
            // 调色板用 g_beatCount 当种子是既有惯例（C3 的锚点/折纸片撒点也用它），
            // 同一个 hit 稳定得到同一套配色、可复现。注意 g_beatCount 永不归零，
            // 所以"换效果后回到第一套"靠下面效果切换处的显式复位。
            g_pcPalIdx = (int)(cubeRnd((int)g_beatCount, 7777) * (float)kPCPalN);
            if (g_pcPalIdx < 0) g_pcPalIdx = 0;
            if (g_pcPalIdx >= kPCPalN) g_pcPalIdx = kPCPalN - 1;
            g_pcCurveTarget += 1.0f;   // 位置由 PLUpdate 以 1/kPCMorphDur 的速率追过去
            // 视角：只换【看这个构造的角度】，距离保持不变（g_pcCamDistT 恒为 kPCCamZ）。
            // 随机游走而不是绝对赋值 —— 连续几次鼓点读起来是"绕着构造转"，
            // 而不是每拍硬跳到一个不相干的方向。俯仰夹住，免得翻过头顶。
            g_pcCamYawT   += (cubeRnd((int)g_beatCount, 1001) - 0.5f) * 3.0f;   // ±1.5 rad
            g_pcCamPitchT += (cubeRnd((int)g_beatCount, 1002) - 0.5f) * 1.0f;   // ±0.5 rad
            if (g_pcCamPitchT >  1.05f) g_pcCamPitchT =  1.05f;
            if (g_pcCamPitchT < -1.05f) g_pcCamPitchT = -1.05f;
        }
    }

    if (g_plState == PL_PLAYING) {
        g_plElapsed += dt;
        if (g_plElapsed >= g_playlist[g_plIndex].durationSec) {
            g_plState = PL_WAITBEAT;
            // 【出场不再从这里开始】。本项时长到点只表示"准备切"，真正该走是等鼓点。
            // 以前这里就把 g_plExitT 置 0 → 效果当场往外飞、然后一直干等下一个鼓点
            // （鼓点稀疏时"飞出去空着"的窗口会很长）。现在出场挪到切换那一刻，见下面 PL_BLUR。
            g_plExitT = 999.0f;
        }
    }
    if (g_plState == PL_WAITBEAT && beat) {
        g_plOldEffIdx = g_plEffIdx[0]; // 保存旧效果
        if (g_plIndex + 1 >= (int)g_playlist.size()) g_plIndex = 0;
        else g_plIndex++;
        g_plElapsed = 0; g_plBlurT = 0;
        g_plEffIdx[0] = EffectIndex(g_playlist[g_plIndex].name);
        g_plEffIdx[1] = g_playlist[g_plIndex].name2.empty() ? -1 : EffectIndex(g_playlist[g_plIndex].name2);
        // 旧效果【从这一刻起】开始飞出；新效果不飞出。下一个效果相同则不播飞出（同 GPU 旧判据）。
        g_plOldExitT = (g_plOldEffIdx == g_plEffIdx[0]) ? 999.0f : 0.0f;
        g_plOldTime = g_plTime; g_plTime = 0; g_plExitT = 999.0f; g_b376FlowPhase = 0.0f; g_audioPhase = 0.0f; g_hitDrive = 0.0f; g_hitFlow = g_hitFlowDrive = 0.0f; g_hitCycleT = -1.0f; g_prevHitKick = 0.0f; g_mdVb = g_mdVvb = g_mdVm = g_mdVvm = g_mdVt = g_mdVvt = 0.0f; g_mdQ28 = 0.0f; g_c3LayerPhase = 0.0f; g_pcPalIdx = 0; // 新效果时间归零（匹配 CPU new 对象 m_time=0）；调色板也回第一套
        // Involute_1 的相机/弹跳状态也归零：hit 年龄不归零的话，切进来的第一帧
        // 台阶会带着上一个时间轴的年龄算出巨大位移，整块板炸开
        g_ssCamYaw = g_ssCamYawT = 0.85f;
        g_ssCamPitch = g_ssCamPitchT = 0.35f;
        g_ssCamYawFrom = 0.85f; g_ssCamPitchFrom = 0.35f;
        g_ssCamT = 99.0f; g_ssCamDur = 0.7f;
        g_ssHitAge = 999.0f;
        g_ssHitIdx = g_ssPrevHitIdx = -1.0f; g_ssPrevGap = 999.0f;
        g_ssPhase = 0.0f;
        g_ssTransMix = 0.0f;   // 【侧通道必须归零】否则切进来第一帧楼梯是卷着的
        // Function1：入场进度归零 → 每切到它一次都重新播一遍"张开"。
        // 鼓点年龄给大数 → 第一帧落在空闲段，不会带着上一次的冲高进来。
        g_f1EntryT = 0.0f; g_f1HitAge = 999.0f;
        g_pcCurvePos = g_pcCurveTarget = 0.0f;   // 曲线循环也回到起点（从科赫开始）
        g_pcCamYaw = g_pcCamYawT = 0.0f;
        g_pcCamPitch = g_pcCamPitchT = -0.42f;
        g_pcCamDist = g_pcCamDistT = kPCCamZ;
        C3RebaseVeilTime(g_plOldTime);   // 锚点/折纸片的时间戳跟着挪到新时间轴（否则年龄变负 → shader 指数爆炸）
        g_m2Renderer.flowPhase = 0.0f; g_m3Renderer.flowPhase = 0.0f; g_m4Renderer.flowPhase = 0.0f;
        g_plState = PL_BLUR;
        g_fboW = 0; // 强制重建 FBO
    }
    if (g_plState == PL_BLUR) {
        g_plOldExitT += dt;  // 旧效果【从切换这一刻起】才开始飞出
        g_plOldTime += dt * g_userSpeed; // 旧效果过渡中继续动画（同样按速度滑杆缩放）
        // 【Involute 与 Involute_1 之间是几何变形（曲变直），不是淡入淡出】
        // 0.5 秒闪过去根本看不出来 —— 这一对单独给长一点的时间。
        float blurSec = BLUR_SEC;
        {
            const int a = (g_plOldEffIdx >= 0 && g_effects[g_plOldEffIdx].isMesh) ? g_effects[g_plOldEffIdx].meshSlot : -1;
            const int b = (g_plEffIdx[0] >= 0 && g_effects[g_plEffIdx[0]].isMesh)   ? g_effects[g_plEffIdx[0]].meshSlot : -1;
            if ((a == 7 && b == 8) || (a == 8 && b == 7)) {
                blurSec = 1.40f;
                // 【锁音频】—— 上升沿才做事（g_plVxLock 从 false 变 true 的那一帧）：
                //   ① 记住此刻的质心（显示级调色要冻住）
                //   ② 第二形态收回原始态：目标置 0 之后它自己会按 kVXFormDur 缓动回去，
                //      不硬跳；若上一记鼓点刚把它炸开，这里正好收干净再开始变形。
                // 之后每帧只是维持 true，不再重复触发。
                if (!g_plVxLock) {
                    g_plVxLockCentroid = g_af.centroid;
                    g_vxFormTarget = 0.0f;
                }
                g_plVxLock = true;
            }
        }
        g_plBlurT += dt / blurSec;
        if (g_plBlurT >= 1.0f) {
            g_plBlurT = 0; g_plElapsed = 0;
            g_plState = PL_PLAYING;
            g_plVxLock = false;   // 过渡放完 → 音频立刻重新接管
        }
    }
}

// ===== 渲染播放链画面 =====
static void DrawArtifactHUD(HDC hdc);
static void DownscaleBlit(int w, int h) {
    int dw = w * 2 / 3, dh = h * 2 / 3;
    if (dw < 1) dw = 1; if (dh < 1) dh = 1;
    if (dw != g_halfW || dh != g_halfH) {
        if (g_halfFbo) { glDeleteFramebuffers(1, &g_halfFbo); glDeleteTextures(1, &g_halfTex); }
        g_halfW = dw; g_halfH = dh;
        glGenFramebuffers(1, &g_halfFbo); glGenTextures(1, &g_halfTex);
        glBindTexture(GL_TEXTURE_2D, g_halfTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, dw, dh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, g_halfFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_halfTex, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_halfFbo);
    glBlitFramebuffer(0, 0, w, h, 0, 0, dw, dh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_halfFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, dw, dh, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ===== 帧反馈 warp =====
static void EnsureFeedback(int w, int h) {
    if (w == g_fbW && h == g_fbH && g_fbCurTex) return;
    if (g_fbCurTex) {
        glDeleteFramebuffers(1, &g_fbPrevFbo); glDeleteTextures(1, &g_fbPrevTex);
        glDeleteFramebuffers(1, &g_fbNextFbo); glDeleteTextures(1, &g_fbNextTex);
        glDeleteTextures(1, &g_fbCurTex);
    }
    g_fbW = w; g_fbH = h;

    // 【16F 而不是 8-bit】反馈缓冲每帧被读一次、乘一次衰减、再写回；
    // 8-bit 下每一代都被量化到 1/255，几十代后细节被磨平、亮部到 1 就封顶。
    // 半浮点没有这两个问题：>1 的值能存住（真正的曝光），暗部也不丢精度。
    // 输入仍然是屏幕（8-bit、已截断到 1），所以现有效果的观感不变，只是拖影更平滑。
    auto makeTex = [](GLuint& tex) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, g_fbW, g_fbH, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    };
    auto makeFbo = [](GLuint& fbo, GLuint& tex, bool clearBlack) {
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        if (clearBlack) { glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT); }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    };
    makeTex(g_fbCurTex);
    makeTex(g_fbPrevTex);
    makeTex(g_fbNextTex);
    makeFbo(g_fbPrevFbo, g_fbPrevTex, true);   // 首帧 prev 清黑，淡入
    makeFbo(g_fbNextFbo, g_fbNextTex, true);
}

// 32×24 顶点网格：每顶点只存归一化 uv。抄 EnsureF1Renderer（main.cpp:2507）三件套。
static void EnsureFeedbackMesh() {
    if (g_fbMeshVao) return;
    std::vector<float> grid;
    grid.reserve((kFbMeshX+1)*(kFbMeshY+1)*2);
    for (int j = 0; j <= kFbMeshY; ++j)
        for (int i = 0; i <= kFbMeshX; ++i) {
            grid.push_back((float)i / (float)kFbMeshX);
            grid.push_back((float)j / (float)kFbMeshY);
        }
    std::vector<unsigned short> idx;
    idx.reserve((size_t)kFbMeshX*kFbMeshY*6);
    for (int j = 0; j < kFbMeshY; ++j)
        for (int i = 0; i < kFbMeshX; ++i) {
            unsigned short a = (unsigned short)(j*(kFbMeshX+1)+i);
            unsigned short b = (unsigned short)(a+1);
            unsigned short c = (unsigned short)(a+kFbMeshX+1);
            unsigned short d = (unsigned short)(c+1);
            // 逆时针绕序（(a,b,c) 与 (b,d,c)）—— 和 NDC 的正面约定一致
            idx.push_back(a); idx.push_back(b); idx.push_back(c);
            idx.push_back(b); idx.push_back(d); idx.push_back(c);
        }
    g_fbMeshICount = (GLsizei)idx.size();

    glGenVertexArrays(1, &g_fbMeshVao);
    glBindVertexArray(g_fbMeshVao);
    glGenBuffers(1, &g_fbMeshVbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_fbMeshVbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(grid.size()*sizeof(float)), grid.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, (void*)0);
    glEnableVertexAttribArray(0);
    glGenBuffers(1, &g_fbMeshEbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_fbMeshEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(idx.size()*sizeof(unsigned short)),
                 idx.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);   // 解 VAO 前先解 EBO，免得被后面绑定串味
}

static GLuint CompileFeedbackProg() {
    // 顶点着色器：逐顶点算【warp 之后的采样 uv】。这是和旧版最根本的区别 ——
    // 旧版位移在片段里逐像素算，所以是一片高频噪声；这里在顶点上算，格子之间插值，
    // 出来是大尺度、连贯的形变。
    const char* vs =
        "#version 330 core\n"
        "layout(location=0)in vec2 aUV;"
        "out vec2 vUV;out vec2 vOrig;"
        "uniform float uZoom,uZoomExp,uRot,uWarp,uSX,uSY,uDX,uDY,uCX,uCY,uAspect,uBass;"
        // 2D 值噪声（平滑插值）—— 形变场用，低频
        "float wh(vec2 p){p=fract(p*vec2(123.34,456.21));p+=dot(p,p+45.32);return fract(p.x*p.y);}"
        "float wn(vec2 p){vec2 i=floor(p);vec2 f=fract(p);f=f*f*(3.0-2.0*f);"
        "return mix(mix(wh(i),wh(i+vec2(1.0,0.0)),f.x),"
        "           mix(wh(i+vec2(0.0,1.0)),wh(i+vec2(1.0,1.0)),f.x),f.y);}"
        "void main(){"
        "  vec2 q=(aUV-vec2(uCX,uCY))*2.0;"          // 以 (cx,cy) 为中心 → [-1,1]
        "  q.x*=uAspect;"                            // 长宽比校正，形变在屏幕上是圆的
        // ★ rad 必须夹住下限。32×24 网格是偶数格，正中【正好有个顶点】落在 (cx,cy)，
        //   那里 q=(0,0) → atan(0,0) 未定义 → 出 NaN，会把挨着它的那几个三角形整块弄黑。
        "  float rad=max(length(q),1e-5);"
        "  float ang=atan(q.y,q.x);"
        // MilkDrop 的 zoom / zoomexp：zoomexp 让缩放在径向上弯曲
        "  float z=pow(uZoom,pow(uZoomExp,rad-1.0));"
        "  ang+=uRot;"
        // warp：固定的低频形变场（两个倍频），幅度 = uWarp×低音，中心少动、外圈多动。
        // 【注意】MilkDrop 的 warp 逐顶点函数文档里没写，只写了幅度语义
        //（"0=none, 1=normal, 2=major"），这里是照那个语义自己构的，不是逐字节复刻。
        "  vec2 np=q*1.7;"
        "  vec2 d=vec2(wn(np)-0.5, wn(np+vec2(17.0,31.0))-0.5);"
        "  d+=vec2(wn(np*2.3+vec2(5.0,9.0))-0.5, wn(np*2.3+vec2(41.0,3.0))-0.5)*0.5;"
        "  q+=d*uWarp*(1.0+uBass*1.5)*0.05*(0.25+0.75*min(rad,1.0));"
        "  q=vec2(cos(ang),sin(ang))*rad*z;"          // 转回去
        "  q.x*=uSX;q.y*=uSY;"
        "  q/=vec2(uAspect,1.0);"
        "  vUV=q*0.5+vec2(uCX,uCY)+vec2(uDX,uDY);"    // warp 后的采样 uv（给上一帧）
        "  vOrig=aUV;"                                // 未 warp 的 uv（给本帧）
        "  gl_Position=vec4(aUV*2.0-1.0,0.0,1.0);}";
    const char* fs =
        "#version 330 core\n"
        "in vec2 vUV;in vec2 vOrig;out vec4 c;"
        "uniform sampler2D uCur,uPrev;"
        "uniform float uMix,uDecay;"
        "void main(){"
        "  vec3 cur =texture(uCur ,clamp(vOrig,0.0005,0.9995)).rgb;"   // 本帧：不 warp
        "  vec3 prev=texture(uPrev,clamp(vUV  ,0.0005,0.9995)).rgb;"   // 上帧：按 warp 后的 uv 采
        // ★ 减法衰减。MilkDrop 的 warp_ps.fx 就一句 `ret -= 0.004` —— 亮处淡得慢、
        //   暗处淡得快，这才是"拖尾"的手感；旧的乘法衰减（每帧×0.45）5 帧就没了。
        "  prev=max(prev-uDecay,0.0);"
        // ★ 用 max 而不是 mix。mix 是"新旧各占一半"→ 两张图叠成糊；
        //   max 是"亮的地方当前帧赢、暗的地方留拖影"→ 这才是 MilkDrop 那种拖尾。
        //   本工程的效果都是全屏不透明地画，没有 alpha 可分，max 是这种情形下最接近的做法。
        "  c=vec4(max(cur,prev*clamp(uMix,0.0,1.0)),1.0);}";
    auto c3=[](GLenum t,const char* s)->GLuint{
        GLuint sh=glCreateShader(t);glShaderSource(sh,1,&s,0);glCompileShader(sh);
        GLint ok;glGetShaderiv(sh,GL_COMPILE_STATUS,&ok);
        if(!ok){char log[512];glGetShaderInfoLog(sh,512,0,log);MessageBoxA(0,log,"Feedback Shader Error",MB_OK);OutputDebugStringA("=== Feedback Shader Error ===\n");OutputDebugStringA(log);glDeleteShader(sh);return 0;}
        return sh;};
    GLuint v=c3(GL_VERTEX_SHADER,vs),f=c3(GL_FRAGMENT_SHADER,fs);
    if(!v||!f){if(v)glDeleteShader(v);if(f)glDeleteShader(f);return 0;}
    GLuint p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);
    glDeleteShader(v);glDeleteShader(f);
    GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok){char log[512];glGetProgramInfoLog(p,512,0,log);MessageBoxA(0,log,"Feedback Link Error",MB_OK);OutputDebugStringA("=== Feedback Link Error ===\n");OutputDebugStringA(log);glDeleteProgram(p);return 0;}
    return p;
}

static void FeedbackWarp(int w, int h) {
    if (w <= 0 || h <= 0) return;
    // 【自反馈效果】效果自己重映射上一帧，引擎只做一件事：把当前画面存成"上一帧"。
    // 不走下面的网格 warp（否则会被二次形变），也不受 g_feedbackOn 开关影响
    // —— 这类效果没有反馈就没有画面，关掉等于黑屏。
    {
        bool milk = false;
        for (int fbIdx = 0; fbIdx < 2; ++fbIdx) {
            int e = (fbIdx == 0) ? g_plEffIdx[0] : g_plEffIdx[1];
            if (e >= 0 && e < g_effectCount && g_effects[e].milkFeedback) milk = true;
        }
        if (milk) {
            EnsureFeedback(w, h);
            // 【多遍效果自己会存】：它的反馈源是第 1 遍（warp）的输出，不是屏幕上的成品
            // —— 这正是 MilkDrop 的语义（sampler_main = warp 输出，comp 只负责显示）。
            // 由 DrawMultiPass 在第一遍画完后直接 blit 进 g_fbPrevTex。
            bool selfStored = false;
            for (int fbIdx = 0; fbIdx < 2; ++fbIdx) {
                int e = (fbIdx == 0) ? g_plEffIdx[0] : g_plEffIdx[1];
                if (e >= 0 && e < g_effectCount && g_effects[e].milkFeedback && !g_effects[e].passes.empty())
                    selfStored = true;
            }
            if (!selfStored && g_fbPrevFbo) {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_fbPrevFbo);
                glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }
            return;
        }
    }
    if (!g_feedbackOn) return;
    // 底层或上层任一效果自带 movement（mesh）就跳过帧反馈拖影（否则混合模式把 mesh 叠出拖影/突变）
    for (int fbIdx = 0; fbIdx < 2; ++fbIdx) {
        int e = (fbIdx == 0) ? g_plEffIdx[0] : g_plEffIdx[1];
        if (e >= 0 && e < g_effectCount && g_effects[e].noFeedback) return;
    }
    EnsureFeedback(w, h);
    if (!g_feedbackProg) g_feedbackProg = CompileFeedbackProg();
    if (!g_feedbackProg) return;

    // 1) 当前帧（默认帧缓冲）→ g_fbCurTex
    glBindTexture(GL_TEXTURE_2D, g_fbCurTex);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
    glBindTexture(GL_TEXTURE_2D, 0);

    // 2) warp pass：uCur(本帧，不 warp) + uPrev(上帧，按【逐顶点 warp 后的 uv】采样) → next FBO
    EnsureFeedbackMesh();
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbNextFbo);
    glViewport(0, 0, w, h);
    glUseProgram(g_feedbackProg);
    // MilkDrop 的 warp 参数组，语义照它的 preset authoring 文档：
    //   zoom    1.006 = 每帧放大 0.6%（向外流的推手）
    //   cx,cy   0.5   = 旋转/拉伸的中心
    //   sx,sy   1.0   = 恒定拉伸（不动）
    //   dx,dy   0.0   = 恒定位移（不动）
    //   warp 强度和残影量用固定默认值（滑杆已移除），其余写死。
    glUniform1f(glGetUniformLocation(g_feedbackProg, "uZoom"),    1.006f);
    glUniform1f(glGetUniformLocation(g_feedbackProg, "uZoomExp"), 1.0f);
    glUniform1f(glGetUniformLocation(g_feedbackProg, "uRot"),     0.0f);
    glUniform1f(glGetUniformLocation(g_feedbackProg, "uWarp"),    g_feedbackWarp);
    glUniform1f(glGetUniformLocation(g_feedbackProg, "uSX"),      1.0f);
    glUniform1f(glGetUniformLocation(g_feedbackProg, "uSY"),      1.0f);
    glUniform1f(glGetUniformLocation(g_feedbackProg, "uDX"),      0.0f);
    glUniform1f(glGetUniformLocation(g_feedbackProg, "uDY"),      0.0f);
    glUniform1f(glGetUniformLocation(g_feedbackProg, "uCX"),      0.5f);
    glUniform1f(glGetUniformLocation(g_feedbackProg, "uCY"),      0.5f);
    glUniform1f(glGetUniformLocation(g_feedbackProg, "uAspect"),  (h > 0) ? (float)w / (float)h : 1.0f);
    glUniform1f(glGetUniformLocation(g_feedbackProg, "uBass"),    g_af.bassAtt);
    glUniform1f(glGetUniformLocation(g_feedbackProg, "uMix"),     g_feedbackMix);
    glUniform1f(glGetUniformLocation(g_feedbackProg, "uDecay"),   g_feedbackDecay);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_fbCurTex);
    glUniform1i(glGetUniformLocation(g_feedbackProg, "uCur"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, g_fbPrevTex);
    glUniform1i(glGetUniformLocation(g_feedbackProg, "uPrev"), 1);
    // ★ 逐顶点网格绘制，不是全屏 quad —— 位移在顶点之间插值，才有大尺度的连贯形变。
    glBindVertexArray(g_fbMeshVao);
    glDrawElements(GL_TRIANGLES, g_fbMeshICount, GL_UNSIGNED_SHORT, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // 3) next → 窗口（1:1 像素拷贝）
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_fbNextFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    // 4) ping-pong 交换：本轮输出成为下一帧的 prev
    GLuint tf = g_fbPrevFbo; g_fbPrevFbo = g_fbNextFbo; g_fbNextFbo = tf;
    GLuint tt = g_fbPrevTex; g_fbPrevTex = g_fbNextTex; g_fbNextTex = tt;
}

/// 是否对当前画面施加 ⅔ 降采样（DownscaleBlit）。
///
/// 规则（2026-09-12 按用户要求收窄）：
///   只降 Normal / Layers 家族的【全屏 shader】效果 —— Psychedelic / GlitchForge /
///   Verdigris / Azurite / Azurite_c / MTH / MTH_C，以及 layers.html 配方拼出来的整屏 shader。
///   其余一律全分辨率：
///     · 网格渲染（isMesh）—— 画的是 1px 细线或点精灵，Linear 降采样 + NEAREST 放大一过就毁了
///       （线条抗锯齿 GL_LINE_SMOOTH 会被彻底抹掉，粒子会糊成一团）；
///     · Cube / Metalheart 家族 —— 画面是自己构成的，同样不该被降采样糊掉。
///   交叉淡入时只要有一层不满足条件，就整体走全分辨率 —— 宁可清晰，不要糊。
// 效果家族（越界一律当 Normal）。效果树 / 混层判据 / 降采样判据三处以前各写一遍这行兜底，
// 注释里还特意警告过"两处判据必须同一套"—— 现在只有这一处，想不一致都难。
static int EffFamily(int idx) {
    if (idx < 0 || idx >= g_effectCount) return FAM_NORMAL;
    int fam = g_effects[idx].family;
    return (fam < 0 || fam >= FAM_COUNT) ? FAM_NORMAL : fam;
}

static bool DownscaleApplies() {
    if (!g_downscaleOn) return false;          // Q 键关掉画质降级
    bool any = false;
    for (int layer = 0; layer < 2; ++layer) {
        int idx = g_plEffIdx[layer];
        if (g_plState == PL_STOPPED || idx < 0 || idx >= g_effectCount) continue;
        any = true;
        // Layers 族放行：它们是整屏 shader（不是细线/点精灵），⅔ 降采样和 Normal 族一样安全，
        // 而这批 dens≈10 的配方恰恰最需要降采样保帧率。
        int fam = EffFamily(idx);
        if (g_effects[idx].isMesh || (fam != FAM_NORMAL && fam != FAM_LAYERS)) return false;
    }
    return any;
}

static void PLRender(HDC hdc, int w, int h) {
    float speed = g_plBpm / 120.0f * g_userSpeed;

    if (g_plState == PL_BLUR) {
        // 过渡统一走 TransitionFx（见 PL_BLUR 分支末尾）。
        // 【Involute 与 Involute_1 之间：曲变直】—— 这一对必须单独处理：
        // 两者是同一个数学对象的两种形态，通用过渡只会让两张图互相淡入淡出，
        // 画面上"没有一点数学图像变化"（用户的原话）。这里让楼梯始终不透明、
        // 用几何变形本身完成过渡。
        const int vxOld = (g_plOldEffIdx >= 0 && g_plOldEffIdx < g_effectCount && g_effects[g_plOldEffIdx].isMesh)
                          ? g_effects[g_plOldEffIdx].meshSlot : -1;
        const int vxNew = (g_plEffIdx[0] >= 0 && g_plEffIdx[0] < g_effectCount && g_effects[g_plEffIdx[0]].isMesh)
                          ? g_effects[g_plEffIdx[0]].meshSlot : -1;
        const bool vxPair = (vxOld == 7 && vxNew == 8) || (vxOld == 8 && vxNew == 7);

        if (vxPair) {
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            // 楼梯在这段时间里【被数学函数弯直】：
            //   管->楼梯：从"卷紧"展开成直的（k 由大变小）；楼梯->管：反过来。
            // smoothstep 与项目里其它过渡的平滑保持一致。
            // 【两段式：先几何变形到位，再换表面质感】——用户的原话是
            // "不应该等卷曲成 Involute 一样的形态再变成 Involute 的表面质感吗"。
            // 之前我把两件事同时做：管从第一帧就开始淡入，于是"管子还没成形就已经
            // 半透明地压在上面"，看到的只是两层糊在一起。
            //
            // 而且两段式在技术上也是【必须】的：形状对上之后，两者都是"相机在轴上
            // 朝前看"，视角正好重合 —— 这时候的材质交叉淡化才是干净的。
            // 形状没对上就淡化，等于把两个不同视角的画面叠起来。
            // 返回类型要显式写 -> float：三元表达式里 0.0f/1.0f/v 推不出唯一类型，
            // 不写的话 MSVC 会把它当 lambda 对象赋给 float（C2440）。
            const auto clamp01 = [](float v) -> float { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
            const auto smooth01 = [](float v) -> float { return v * v * (3.0f - 2.0f * v); };
            // 【时序：材质不再单独占一段了】——
            // 以前是两段式（先换表面质感、再变形），用户现在的意见是"边扭曲边过渡"。
            // 材质过渡已经搬进 kSSFS，由 uMix 驱动（同一进度、同一张几何），
            // 所以这里的 matP 只剩一个作用：【末尾清尾】—— 此刻楼梯的形状和材质都已与
            // Involute 一致，把管子那张图淡出去抹平两者残差（法线来源/分面/分层那点差异）。
            // 留 25% 就够：淡化发生在两个几乎一样的画面之间，本身就看不见。
            // 反过来（楼梯->管）就是开头 25% 把旧楼梯清掉、后 75% 变形 + 材质一起走。
            float geoP, matP;
            if (vxNew == 8) {
                // 管 -> 楼梯：前 25% 把管子淡出（楼梯此时已经是"管的形状 + 管的材质"），之后 75% 展开
                matP = smooth01(clamp01(g_plBlurT / 0.25f));
                geoP = smooth01(clamp01((g_plBlurT - 0.25f) / 0.75f));
            } else {
                // 楼梯 -> 管：前 75% 卷起来 + 材质一起过渡，后 25% 把管子淡进来
                geoP = smooth01(clamp01(g_plBlurT / 0.75f));
                matP = smooth01(clamp01((g_plBlurT - 0.75f) / 0.25f));
            }
            // 几何进度驱动一切几何/相机状态：geoP=0 是"完全卷紧 + 相机在轴上"，
            // geoP=1 是"摊平的直楼梯 + 轨道视角"。
            g_ssTransMix = (vxNew == 8) ? (1.0f - geoP) : geoP;
            const float tunnelA = (vxNew == 8) ? (1.0f - matP) : matP;

            // 【楼梯必须始终不透明，靠形状变化完成过渡】——
            // 一开始我照抄了下面交叉淡化的写法（旧的 1.0 铺底、新的按 t 盖上去），
            // 结果：k 最大（卷得最紧）时楼梯的 alpha 正好≈0，看得清了 k 又已经归零，
            // 画面上就是纯淡入淡出（用户的原话："没有一点数学图像变化"）。
            // 参数级 morph 要求"新的按 t 盖上去"是因为那边几何不动、只混场；
            // 几何变形恰好相反：新效果要一直在，让旧的在上面淡出。
            const int stair = (vxOld == 8) ? g_plOldEffIdx : g_plEffIdx[0];
            const int tunnel = (vxOld == 7) ? g_plOldEffIdx : g_plEffIdx[0];
            // 楼梯始终不透明、承担整个几何变形；管只在【材质段】按 matP 出现 ——
            // tunnelA 已在上面按两段式算好。
            DrawEffect(stair,  (vxOld == 8) ? g_plOldTime : g_plTime, g_plBpm, g_plPhase, speed, (float)w, (float)h, g_plBlackBg, 1.0f);
            g_ssTransMix = 0.0f;   // 管那趟不吃这套，还原成正常轨道视角
            DrawEffect(tunnel, (vxOld == 7) ? g_plOldTime : g_plTime, g_plBpm, g_plPhase, speed, (float)w, (float)h, g_plBlackBg, tunnelA);
            glDisable(GL_BLEND);
        } else if (g_plOldEffIdx >= 0 && g_plEffIdx[0] >= 0) {
            // ---- MilkDrop 式过渡：两个效果各自画成一张图，交给 TransitionFx 合成 ----
            // 旧效果继续按自己的时间轴跑（g_plOldTime），所以过渡里它是"活的"，不是一张冻结截图。
            // 位移场在中段最大、两端为 0；混合曲线是 MilkDrop 的余弦插值。
            if (TransitionFx::Ensure(w, h)) {
                const float savedExit = g_plExitT;
                g_plExitT = g_effects[g_plOldEffIdx].isMesh ? g_plOldExitT : 999.0f;

                glBindFramebuffer(GL_FRAMEBUFFER, TransitionFx::FboOld());
                glViewport(0, 0, w, h);
                glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
                DrawEffect(g_plOldEffIdx, g_plOldTime, g_plBpm, g_plPhase, speed, (float)w, (float)h, g_plBlackBg, 1.0f);

                glBindFramebuffer(GL_FRAMEBUFFER, TransitionFx::FboNew());
                glViewport(0, 0, w, h);
                glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
                DrawEffect(g_plEffIdx[0], g_plTime, g_plBpm, g_plPhase, speed, (float)w, (float)h, g_plBlackBg, 1.0f);

                g_plExitT = savedExit;
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glViewport(0, 0, w, h);

                TransitionFx::Params tp;
                tp.warp = 1.0f;
                tp.zoom = 1.2f;
                tp.rot  = 0.35f;
                tp.bass = g_af.bassAtt;
                TransitionFx::Composite(w, h, g_plBlurT, tp);
            } else {
                // 过渡资源建不起来时退化：直接画新效果，不中断播放
                DrawEffect(g_plEffIdx[0], g_plTime, g_plBpm, g_plPhase, speed, (float)w, (float)h, g_plBlackBg);
            }
        } else if (g_plEffIdx[0] >= 0) {
            DrawEffect(g_plEffIdx[0], g_plTime, g_plBpm, g_plPhase, speed, (float)w, (float)h, g_plBlackBg);
        }
        if (DownscaleApplies()) DownscaleBlit(w, h);   // 只降 Normal 全屏效果；网格/Cube/Metalheart 全分辨率
        FeedbackWarp(w, h);
        SwapBuffers(hdc);
        DrawArtifactHUD(hdc);
        return;
    }
    if (g_plState != PL_STOPPED && g_plEffIdx[0] >= 0) {
        auto& item = g_playlist[g_plIndex];
        float opacity = item.opacity;
        if (g_plEffIdx[1] >= 0 && opacity > 0.0f) {
            int blendMode = (int)item.blendMode;
            // ── 7 种 Photoshop 混合模式路径（匹配 CPU PlaylistManager::Render）──
            if (blendMode > 0 && g_blendProg) {
                EnsureFBO(w, h);
                // Pass 1: 底层效果 → g_blendFbo
                glBindFramebuffer(GL_FRAMEBUFFER, g_blendFbo);
                glViewport(0, 0, w, h);
                if (g_plBlackBg) { glClearColor(0.0f,0.0f,0.0f,1.0f); }
                else             { glClearColor(1.0f,1.0f,1.0f,1.0f); }
                glClear(GL_COLOR_BUFFER_BIT);
                DrawEffect(g_plEffIdx[0], g_plTime, g_plBpm, g_plPhase, speed, (float)w, (float)h, g_plBlackBg, 1.0f);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                // Pass 2: 上层效果 → g_oldFbo（全不透明度，由合成阶段控制）
                glBindFramebuffer(GL_FRAMEBUFFER, g_oldFbo);
                glViewport(0, 0, w, h);
                if (g_plBlackBg) { glClearColor(0.0f,0.0f,0.0f,1.0f); }
                else             { glClearColor(1.0f,1.0f,1.0f,1.0f); }
                glClear(GL_COLOR_BUFFER_BIT);
                DrawEffect(g_plEffIdx[1], g_plTime, g_plBpm, g_plPhase, speed, (float)w, (float)h, g_plBlackBg, 1.0f);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                // Pass 3: 逐通道 Photoshop 公式合成（=CPU BlendChannel + opacity lerp）
                glViewport(0, 0, w, h);
                glUseProgram(g_blendProg);
                glUniform1i(glGetUniformLocation(g_blendProg, "uMode"), blendMode);
                glUniform1f(glGetUniformLocation(g_blendProg, "uOpacity"), opacity);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, g_blendTex);
                glUniform1i(glGetUniformLocation(g_blendProg, "uBase"), 0);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, g_oldTex);
                glUniform1i(glGetUniformLocation(g_blendProg, "uOverlay"), 1);
                glBindVertexArray(g_vao);
                glDrawArrays(GL_TRIANGLES, 0, 6);
            } else {
                // 退化路径：简单 alpha 混合（blendMode==None 或 shader 不可用时）
                DrawEffect(g_plEffIdx[0], g_plTime, g_plBpm, g_plPhase, speed, (float)w, (float)h, g_plBlackBg, 1.0f);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                DrawEffect(g_plEffIdx[1], g_plTime, g_plBpm, g_plPhase, speed, (float)w, (float)h, g_plBlackBg, opacity);
                glDisable(GL_BLEND);
            }
        } else {
            DrawEffect(g_plEffIdx[0], g_plTime, g_plBpm, g_plPhase, speed, (float)w, (float)h, g_plBlackBg);
        }
    } else {
        glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
    }
    if (DownscaleApplies()) DownscaleBlit(w, h);   // 只降 Normal 全屏效果；网格/Cube/Metalheart 全分辨率
    FeedbackWarp(w, h);
    SwapBuffers(hdc);
    DrawArtifactHUD(hdc);
}

// ===== CPU 渲染质感 HUD =====
static void DrawArtifactHUD(HDC hdc) {
    wchar_t buf[256];
    bool loaded = !g_artifactCode.empty();
    bool active = g_ditherStrength > 0.5f;

    HFONT f = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Consolas");
    HFONT of = (HFONT)SelectObject(hdc, f);
    SetBkMode(hdc, TRANSPARENT);

    // 行 1: CpuArtifact 状态 + 降分辨率
    // 【曾写死成 L"⅔"】—— 那是个字面量，不管实际有没有降采样都显示 ⅔，会把人带偏。
    // 现在如实反映：网格效果（C3/StellarVortex）本来就不走降采样，关了开关也是全分辨率。
    const bool fullRes = !DownscaleApplies();
    _snwprintf(buf, 256, L"CpuArtifact: %s | 质感: %s | 画质: %s",
        loaded  ? L"已加载" : L"未加载(!)",
        active  ? L"ON" : L"OFF",
        fullRes ? L"全分辨率" : L"⅔");
    RECT tr = {10, 6, 800, 24};
    SetTextColor(hdc, loaded ? (active ? RGB(120, 255, 120) : RGB(180, 180, 180))
                             : RGB(255, 100, 100));
    DrawTextW(hdc, buf, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // 行 2: 过渡状态（仅在过渡期间显示）
    if (g_plState == PL_BLUR && g_plOldEffIdx >= 0 && g_plEffIdx[0] >= 0) {
        const wchar_t* oldName = g_effects[g_plOldEffIdx].name.c_str();
        const wchar_t* newName = g_effects[g_plEffIdx[0]].name.c_str();
        int pct = (int)(g_plBlurT * 100.0f);
        if (pct > 100) pct = 100;
        _snwprintf(buf, 256, L"  → 过渡: %s → %s  [%d%%]",
            oldName, newName, pct);
        RECT tr2 = {10, 26, 800, 46};
        SetTextColor(hdc, RGB(255, 220, 100));  // 金色提示
        DrawTextW(hdc, buf, -1, &tr2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else if (g_plState == PL_WAITBEAT) {
        RECT tr2 = {10, 26, 800, 46};
        SetTextColor(hdc, RGB(255, 200, 100));
        DrawTextW(hdc, L"  → 等待鼓点切换...", -1, &tr2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    SelectObject(hdc, of);
    DeleteObject(f);
}

// ===== Shader 加载 =====
static std::string LoadFile(const wchar_t* path) {
    FILE* f = _wfopen(path, L"rb"); if (!f) return "";
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    std::string s(len, 0); fread(&s[0], 1, len, f); fclose(f); return s;
}

// ---------------------------------------------------------------------------
// //@lib 标记 —— 按需注入 VisualEffects\lib\ 下的库
//
//   效果头部写：  //@lib psrdnoise2D
//   拼装时把它读进来，插在 audio_prelude 之后、效果源码之前。
//
// 【为什么不用全量 prelude】噪声库 ~50KB GLSL，23 个效果里只有少数用得上。
//   全量拼上去 = 让不用噪声的效果也付编译时间。标记式是 opt-in。
// 【一份真源】库文件在项目里只有一份；ShaderLab 的 render.cpp 走同一条规则 ——
//   两边行为必须一致，否则离线渲得出来、实机编不过。
// ---------------------------------------------------------------------------
static std::string CollectShaderLibs(const std::string& effectSrc, const wchar_t* projBase)
{
    std::string lib;
    // 【只认行首】的 //@lib（允许行首空白）。
    // 不能在整段源码里 find("//@lib") —— 那样注释里提到 //@lib 也会触发假查找。
    size_t ls = 0;
    while (ls <= effectSrc.size()) {
        size_t eol = effectSrc.find('\n', ls);
        if (eol == std::string::npos) eol = effectSrc.size();
        std::string line = effectSrc.substr(ls, eol - ls);
        size_t b = line.find_first_not_of(" \t\r");
        if (b != std::string::npos && line.compare(b, 6, "//@lib") == 0) {
            std::string names = line.substr(b + 6);
            size_t a = 0;
            for (;;) {
                size_t c = names.find(',', a);
                std::string one = names.substr(a, (c == std::string::npos ? names.size() : c) - a);
                size_t b2 = one.find_first_not_of(" \t\r");
                size_t e2 = one.find_last_not_of(" \t\r");
                one = (b2 == std::string::npos) ? std::string() : one.substr(b2, e2 - b2 + 1);
                if (!one.empty()) {
                    wchar_t lp[MAX_PATH];
                    _snwprintf(lp, MAX_PATH, L"%s\\VisualEffects\\lib\\%S.glsl", projBase, one.c_str());
                    std::string s = LoadFile(lp);
                    if (!s.empty()) {
                        // 库可能自带 #version（cellular* 是 120）—— 删掉，
                        // 否则拼在 artifact 之后会报 "version must be first statement"
                        size_t vp = s.find("#version");
                        if (vp != std::string::npos) {
                            size_t ve = s.find('\n', vp);
                            if (ve != std::string::npos) s.erase(vp, ve - vp + 1);
                        }
                        lib += "\n// ==== lib: " + one + " ====\n" + s + "\n";
                    } else {
                        std::string msg = "//@lib 找不到: " + one + "\n（应在 <项目>\\VisualEffects\\lib\\）";
                        MessageBoxA(nullptr, msg.c_str(), "ShaderLib Missing", MB_OK);
                    }
                }
                if (c == std::string::npos) break;
                a = c + 1;
            }
        }
        if (eol >= effectSrc.size()) break;
        ls = eol + 1;
    }
    return lib;
}

// 把清单里的设定写进效果：家族 / 帧反馈 / 形态数 / 初始形态 / 投影 / 需要的输入。
// 这段以前在"多遍"和"单遍"两个分支里各抄了一份 —— 两处必须完全一致，
// 正是最容易改一处忘一处的地方，所以收成一份。
static void ApplyEffectDecl(GpuEffect& e, const EffectDecl* d) {
    if (!d) return;
    e.family     = d->family;
    e.noFeedback = d->noFeedback;
    if (d->formCount > 0) e.formCount = d->formCount;
    if (d->formInit >= 0) e.form = e.targetForm = d->formInit;
    e.proj = d->proj;
    if (d->inputs & EIN_PREV) e.milkFeedback = true;
}

static void LoadEffects() {
    // Cubeeffect3 是一对【同一套几何、两套配色】：深色模式 / 浅色模式，
    // 走的都是同一个 mesh slot 渲染器（C3Lines.h 的线段 + 折纸片），只是 FS 里的调色板不同。
    const wchar_t* const* names = kEffectNames;   // 唯一真源，见 g_effects 上方
    wchar_t base[MAX_PATH]; GetModuleFileNameW(nullptr, base, MAX_PATH);
    wchar_t* p = wcsrchr(base, L'\\'); if (p) *p = L'\0'; // 去 exe 名, "D:\...\out"
    wchar_t* p2 = wcsrchr(base, L'\\');
    if (p2 && _wcsicmp(p2+1, L"out") == 0) *p2 = L'\0'; // 去 out, "D:\...\ALL VSLZATION_GPU"
    _snwprintf(g_shaderDir, MAX_PATH, L"%s\\VisualEffects\\shaders\\", base);

    // 加载 CPU 渲染副产品模拟代码（CpuArtifact.glsl）
    if (g_artifactCode.empty()) {
        wchar_t apath[MAX_PATH];
        _snwprintf(apath, MAX_PATH, L"%s\\VisualEffects\\simulation effect VST\\CpuArtifact.glsl", base);
        g_artifactCode = LoadFile(apath);
    }
    // 加载音频驱动 ABI（audio_prelude.glsl）
    if (g_audioPrelude.empty()) {
        wchar_t ppath[MAX_PATH];
        _snwprintf(ppath, MAX_PATH, L"%s\\VisualEffects\\audio_prelude.glsl", base);
        g_audioPrelude = LoadFile(ppath);
    }

    g_effectCount = 0;
    // 装载失败一律记在这里，循环结束后一次性报告。
    // 【以前是每个失败弹一个模态框】—— 缺三个 shader 就要点三次确定，
    // 而且失败之后槽位留空、后面的下标全部错位，效果树里会冒出一行空白。
    std::vector<std::wstring> fails;

    for (int i = 0; i < kEffectCapacity; ++i) {
        const wchar_t* name = names[i];
        // 先查清单：EK_MESH 的效果是程序化几何，没有 .frag，必须在读文件之前分流，
        // 否则会误报"找不到 shader"。其余（含未列出的）走全屏 shader 路径。
        const EffectDecl* decl = FindEffectDecl(name);

        // 【写 g_effectCount，不是 i】—— 效果清单是"成功装载了几个"的唯一来源。
        // 以前按名字表下标写、只在成功时 ++，任何一个失败都会让后面的效果与
        // kEffectNames 错位：树上多一行空的、按名字再也找不到那个效果。
        // 全工程没有按固定下标访问 g_effects 的地方，所以紧凑排布是安全的。
        GpuEffect& e = g_effects[g_effectCount];
        e = GpuEffect();   // 装载会在两个 GL 上下文里各跑一次，旧字段不能带过来

        if (decl && decl->kind == EK_MESH) {
            e.name = name;
            e.isMesh = true;
            e.meshSlot = decl->meshSlot;
            ApplyEffectDecl(e, decl);   // 网格效果的 formInit / proj 都是默认值，可以共用
            g_effectCount++;
            continue;
        }

        wchar_t path[MAX_PATH];
        _snwprintf(path, MAX_PATH, L"%s%s.frag", g_shaderDir, name);
        std::string src = LoadFile(path);
        if (src.empty()) {
            // 回退：exe 同目录
            _snwprintf(path, MAX_PATH, L"%s%s.frag", base, name);
            src = LoadFile(path);
        }
        if (src.empty()) {
            fails.push_back(std::wstring(name) + L".frag —— 文件不存在");
            continue;
        }

        // ---- 多遍效果：//@pass 把文件切成共享头部 + 若干 pass ----
        {
            std::string shared;
            std::vector<PassDef> passDefs;
            if (SplitPasses(src, shared, passDefs)) {
                auto stripVersion = [](std::string& s) {
                    auto vp = s.find("#version");
                    if (vp != std::string::npos) { auto ve = s.find('\n', vp); if (ve != std::string::npos) s.erase(vp, ve - vp + 1); }
                };
                stripVersion(shared);
                std::string libs   = CollectShaderLibs(src, base);
                std::string prefix = g_artifactCode + "\n"
                                   + (g_audioPrelude.empty() ? "" : g_audioPrelude + "\n")
                                   + libs + shared;
                bool ok = true;
                for (auto& pd : passDefs) {
                    if (!CompilePassProgram(pd, prefix + "\n" + pd.src)) {
                        // 已经编译成功的 pass 也要一起回收，否则反复重开窗口会漏 program
                        for (auto& q : passDefs) if (q.program) glDeleteProgram(q.program);
                        ok = false;
                        break;
                    }
                }
                if (ok) {
                    e.passes = passDefs;
                    e.name   = name;
                    ApplyEffectDecl(e, decl);
                    if (src.find("//@milk-feedback") != std::string::npos) e.milkFeedback = true;
                    g_effectCount++;
                } else {
                    fails.push_back(std::wstring(name) + L".frag —— 某个 pass 编译失败");
                }
                continue;
            }
        }

        // 拼接 CpuArtifact.glsl（去掉原 #version 行，由 artifact 文件提供）
        if (!g_artifactCode.empty()) {
            auto vp = src.find("#version");
            if (vp != std::string::npos) {
                auto ve = src.find('\n', vp);
                if (ve != std::string::npos) src.erase(vp, ve - vp + 1);
            }
            // 顺序：artifact（含 #version）→ audio_prelude（音频 ABI）→
            //       //@lib 注入的库 → effect 源码
            // prelude 必须在 #version 之后，否则 GLSL 编译报错
            std::string libs = CollectShaderLibs(src, base);   // 没有 //@lib 标记就是空串
            src = g_artifactCode + "\n"
                + (g_audioPrelude.empty() ? "" : g_audioPrelude + "\n")
                + libs + src;
        }
        if (e.Load(src.c_str())) {
            e.name = name;
            ApplyEffectDecl(e, decl);
            // 能力也能写在 frag 里（//@milk-feedback）：加同类效果时改 shader 就够了，不用动清单。
            if (src.find("//@milk-feedback") != std::string::npos) e.milkFeedback = true;
            g_effectCount++;
        } else {
            fails.push_back(std::wstring(name) + L".frag —— 编译/链接失败（详见调试输出）");
        }
    }

    if (!fails.empty()) {
        // 一次性报告 + 落盘。load_report.txt 放在 exe 同级，事后核对"这份 exe 到底装上了哪些"。
        std::wstring msg = L"效果装载 " + std::to_wstring(g_effectCount) + L" / "
                         + std::to_wstring(kEffectCapacity) + L" 个成功。\n\n以下 "
                         + std::to_wstring(fails.size()) + L" 个失败：\n";
        for (auto& f : fails) { msg += L"  "; msg += f; msg += L"\n"; }
        // 必须用【宽字符】文件接口：fails 里是中文，普通的 ANSI fopen + %ls 会把
        // 非 ASCII 部分直接丢掉（写出来只剩 "FAILED Rosette.frag"，原因不见了）。
        if (FILE* fp = _wfopen(L"load_report.txt", L"w, ccs=UTF-8")) {
            fwprintf(fp, L"loaded %d / %d\n", g_effectCount, kEffectCapacity);
            for (auto& f : fails) fwprintf(fp, L"FAILED %ls\n", f.c_str());
            fclose(fp);
        }
        MessageBoxW(nullptr, msg.c_str(), L"GPU Shader Load Report", MB_OK);
    }
    if (g_effectCount == 0) {
        wchar_t msg[1024];
        _snwprintf(msg, 1024,
            L"No shaders loaded.\nShader dir: %s\nExists: %s",
            g_shaderDir,
            (GetFileAttributesW(g_shaderDir)!=INVALID_FILE_ATTRIBUTES?L"yes":L"no"));
        MessageBoxW(nullptr, msg, L"GPU Load Error", MB_OK);
    }
}

// ===== 播放链子窗口（OpenGL） =====
#define TIMER_PLAYER 3
static HWND g_hPlayer = nullptr;
static HGLRC g_hPlayerRC = nullptr;
static bool g_playerMode = false;

LRESULT CALLBACK PlayerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HDC hdc = GetDC(hwnd);
        PIXELFORMATDESCRIPTOR pfd = {sizeof(pfd),1,PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER,
            PFD_TYPE_RGBA,32,0,0,0,0,0,0,0,0,0,0,0,0,0,24,8,0,PFD_MAIN_PLANE,0,0,0,0};
        int pf = ChoosePixelFormat(hdc, &pfd);
        if (!pf || !SetPixelFormat(hdc, pf, &pfd)) {
            MessageBoxW(hwnd, L"SetPixelFormat failed!", L"GL Error", MB_OK);
            DestroyWindow(hwnd); return 0;
        }
        g_hPlayerRC = wglCreateContext(hdc);
        if (!g_hPlayerRC) {
            MessageBoxW(hwnd, L"wglCreateContext failed!", L"GL Error", MB_OK);
            DestroyWindow(hwnd); return 0;
        }
        wglMakeCurrent(hdc, g_hPlayerRC);
        LoadGL();
        // 垂直同步：SwapBuffers 对齐显示器刷新，消除最大化大画面下的撕裂/频闪
        {
            typedef BOOL (APIENTRY *PFNWGLSWAPINTERVALEXTPROC)(int);
            PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT =
                (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
            if (wglSwapIntervalEXT) wglSwapIntervalEXT(1);
        }
        LoadEffects();
        if (g_effectCount == 0) {
            MessageBoxW(hwnd, L"Shader load failed in player context!", L"GPU Error", MB_OK);
            DestroyWindow(hwnd); return 0;
        }
        g_blendProg = 0;              // 先清零，防止用临时上下文的无效句柄
        CompileBlendProgram(true);
        InitVAO();
        ReleaseDC(hwnd, hdc);
        SetTimer(hwnd, TIMER_PLAYER, 16, nullptr);
        return 0;
    }
    case WM_TIMER:
        if (g_plState != PL_STOPPED) InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;
        if (w > 0 && h > 0) { g_clientW = w; g_clientH = h; }   // 锚点铺设范围跟随窗口
        {
            if (!g_hPlayerRC) { EndPaint(hwnd, &ps); return 0; }
            wglMakeCurrent(hdc, g_hPlayerRC);
            if (w > 0 && h > 0) {
                glViewport(0, 0, w, h);
                glClearColor(g_plBlackBg?0.0f:1.0f, g_plBlackBg?0.0f:1.0f, g_plBlackBg?0.0f:1.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);   // 含深度（resize 后深度缓冲重建）
                PLRender(hdc, w, h);
            }
            wglMakeCurrent(nullptr, nullptr);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    // ── 无边框窗口：把 WS_THICKFRAME 的边框尺寸清零，画面一直铺到窗口边缘 ──────
    // 只吃掉 NCCALCSIZE，不碰窗口样式，所以缩放热区（HTLEFT 等）依然有效。
    //
    // ⚠ 两种 wParam 都必须处理。网上常见的写法只有 `if (wParam) return 0;`，
    //   但【建窗那一刻系统发的是 wParam=0 那种】，那条分支根本不会跑 ——
    //   结果是标题栏没了、边框却还在（实测客户区比窗口小 14×14）。
    //   已在 _hittest/borderless_test.cpp 里复现并验证过两种都处理才对。
    case WM_NCCALCSIZE:
        if (wParam) return 0;                       // 客户区 = 整个窗口
        {                                       // wParam == 0：进来的是"建议客户区"，
            RECT* r = (RECT*)lParam;            // 交给 DefWindowProc 会被它按边框缩小，
            RECT wr; GetWindowRect(hwnd, &wr);  // 所以自己填成整个窗口矩形。
            r->left = wr.left;   r->top    = wr.top;
            r->right = wr.right; r->bottom = wr.bottom;
            return 0;
        }
    // 客户区已经等于整个窗口，边框的"手感"得自己补回来：
    //  · 四周 8px  → 缩放热区（交给系统，和普通窗口一样）
    //  · 顶部 PLAYER_DRAG_H → 当作标题栏，按住可拖动、右键弹系统菜单（关闭的兜底入口）
    case WM_NCHITTEST: {
        RECT rc; GetWindowRect(hwnd, &rc);
        const int px = GET_X_LPARAM(lParam), py = GET_Y_LPARAM(lParam);
        const int B = 8;
        const bool L = px < rc.left + B, R = px >= rc.right - B;
        const bool T = py < rc.top + B,  Bo = py >= rc.bottom - B;
        if (T && L) return HTTOPLEFT;
        if (T && R) return HTTOPRIGHT;
        if (Bo && L) return HTBOTTOMLEFT;
        if (Bo && R) return HTBOTTOMRIGHT;
        if (L) return HTLEFT;
        if (R) return HTRIGHT;
        if (T) return HTTOP;
        if (Bo) return HTBOTTOM;
        if (py < rc.top + PLAYER_DRAG_H) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
        if (wParam == 'B') { g_plBlackBg=!g_plBlackBg; InvalidateRect(hwnd, nullptr, FALSE); return 0; }
        if (wParam == 'D') { g_ditherStrength=(g_ditherStrength>0.5f)?0.0f:1.0f; InvalidateRect(hwnd, nullptr, FALSE); return 0; }
        if (wParam == 'Q') { g_downscaleOn = !g_downscaleOn; InvalidateRect(hwnd, nullptr, FALSE); return 0; }   // 画质：⅔ 降采样开关
        if (wParam == 'F') { g_feedbackOn = !g_feedbackOn; InvalidateRect(hwnd, nullptr, FALSE); return 0; }
        if (wParam == 'P') { g_debugParts = !g_debugParts; InvalidateRect(hwnd, nullptr, FALSE); return 0; }  // B376 运动单元红边
        break;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_PLAYER); g_hPlayer = nullptr;
        TransitionFx::Shutdown();   // GL 上下文要没了，句柄必须在这里作废
        if (g_hPlayerRC) { wglMakeCurrent(nullptr, nullptr); wglDeleteContext(g_hPlayerRC); g_hPlayerRC = nullptr; }
        PLStop(); return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ===== 主窗口 =====
#define IDC_LIST          1001
#define IDC_STATUS        1005
#define IDC_PLAYLIST      1101
#define IDC_BTN_ADD       1102
#define IDC_BTN_REMOVE    1103
#define IDC_BTN_UP        1104
#define IDC_BTN_DOWN      1105
#define IDC_BTN_CLEAR     1111
#define IDC_EDIT_DURATION 1106
#define IDC_BTN_PLAY      1107
#define IDC_BTN_STOP      1108
#define IDC_COMBO_BLEND   1110
#define IDC_PRESET_BASE   1120
#define IDC_TRACK_OPACITY 1131
#define IDC_STATIC_OPACITY 1132
#define TIMER_MAIN 1
#define TIMER_INIT 2
#define TIMER_CLEAR_HOLD 3
#define CLEAR_HOLD_MS 1000

// 顶部菜单栏 ID
#define IDM_SETTINGS   2000

static void ShowSettingsDialog(HWND parent);  // 设置对话框（定义在 WinMain 前）

static HWND g_hwnd=nullptr, g_hList=nullptr, g_hStatus=nullptr;
static HWND g_hPlaylist=nullptr, g_hEditDur=nullptr, g_hBtnPlay=nullptr, g_hBtnStop=nullptr;
static HWND g_hBtnClear=nullptr;
static bool g_holdingClear=false;
static bool g_holdCompleted=false;
static DWORD g_holdStartTick=0;
static POINT g_holdCursorPt={0,0};
static WNDPROC g_btnClearOldProc=nullptr;
static HWND g_treeHdrGpu=nullptr, g_treeHdrCpu=nullptr;      // 效果列表 GPU/CPU 列小表头标签
static int g_dotClientX_Gpu=0, g_dotClientX_Cpu=0;            // 树客户区内圆点列 x（自绘与表头共用基准）
static AudioCapture g_audio;
static BPMDetector g_drums(44100);
static AudioAnalyzer g_audioAnalyzer;
static SharedMem g_sharedMem;
static float g_bpm = 120.0f;
// g_beatCount 的声明已挪到 PLUpdate 之前（那里要用它当随机种子）
static int g_silentFrames = 0;

// 这两个 setter 只保留"改值"。以前它们还要顺手同步 BPM 输入框 / 速度滑块 / 菜单单选项，
// 但那三样控件从来没被创建过（主窗口只建了列表、播放链、混合那几个），分支永远是空的。
static void SetBPM(float bpm) { g_bpm = bpm; }
static void SetSpeed(float s) { g_userSpeed = s; }

static void RefreshPlaylistUI() {
    ListBox_ResetContent(g_hPlaylist);
    const wchar_t* bmNames[] = {L"无",L"变亮",L"滤色",L"叠加",L"强光",L"柔光",L"线性加深",L"正片叠底"};
    for (size_t i = 0; i < g_playlist.size(); ++i) {
        wchar_t s[300]; auto& it = g_playlist[i];
        if (!it.name2.empty())
            _snwprintf(s, 300, L"%d. %s + %s  [%s|%.0f%%]  [%ds]",
                (int)i+1, it.name.c_str(), it.name2.c_str(),
                bmNames[it.blendMode], it.opacity*100.0f, it.durationSec);
        else _snwprintf(s, 300, L"%d. %s  [%ds]", (int)i+1, it.name.c_str(), it.durationSec);
        ListBox_AddString(g_hPlaylist, s);
    }
}

// ===== 可用效果 TreeView（分类 + 可用性圆点）=====
static bool EffGpuAvail(int idx) {
    if (idx < 0 || idx >= g_effectCount) return false;
    return g_effects[idx].program != 0 || g_effects[idx].isMesh;
}
// 是否"可以当混层上下层"的效果（随机播放链的混层规则用）。family 的越界兜底与
// RebuildEffectTree 同一套，否则两处判据会在 family 异常时不一致。
// 注意判据是【家族】而不是 isMesh —— StellarVortex / Involute / Involute_1 都是网格
// 但属于 FAM_NORMAL（家族与渲染方式正交）。
// 【Layers 族也算】—— 它们只是从 layers.html 配方拼出来的整屏 shader，渲染路径与
// Normal 族一模一样。归族是【分类】，不该顺手把随机列表里的混层能力砍掉。
static bool EffIsMixable(int idx) {
    if (idx < 0 || idx >= g_effectCount) return false;
    int fam = EffFamily(idx);
    return fam == FAM_NORMAL || fam == FAM_LAYERS;
}

static HTREEITEM AddTreeItem(HTREEITEM parent, const std::wstring& text, int lParam) {
    TVINSERTSTRUCTW is = {};
    is.hParent = parent; is.hInsertAfter = TVI_LAST;
    is.item.mask = TVIF_TEXT | TVIF_PARAM;
    is.item.pszText = (LPWSTR)text.c_str();
    is.item.lParam = lParam;
    return (HTREEITEM)SendMessageW(g_hList, TVM_INSERTITEMW, 0, (LPARAM)&is);
}

// 家族名数组：下标 = FAM_* 值，新家族加枚举 + 一项即可
static const wchar_t* kFamilyTitle[FAM_COUNT] = {
    L"Normal effects", L"Metalheart effects", L"Cube effects", L"3D effects",
    L"Layers effects"
};
// 重建效果树：每家族一棵根，CPU 模式只保留 CPU 可用效果。
// 家族在 g_effects[i].family 上声明式分类——新家族（如 FAM_CUBE）加枚举 + 一行即可。
static void RebuildEffectTree() {
    if (!g_hList) return;
    SendMessageW(g_hList, TVM_DELETEITEM, 0, (LPARAM)TVI_ROOT);
    HTREEITEM rootItem[FAM_COUNT];
    int cnt[FAM_COUNT] = {};
    for (int f = 0; f < FAM_COUNT; ++f) rootItem[f] = AddTreeItem(nullptr, kFamilyTitle[f], -1);
    for (int i = 0; i < g_effectCount; ++i) {
        int fam = EffFamily(i);
        AddTreeItem(rootItem[fam], g_effects[i].name, i);
        ++cnt[fam];
    }
    // 分类名带计数
    TVITEMW t = {}; t.mask = TVIF_TEXT;
    wchar_t sn[64];
    for (int f = 0; f < FAM_COUNT; ++f) {
        _snwprintf(sn, 64, L"%s (%d)", kFamilyTitle[f], cnt[f]); t.hItem = rootItem[f]; t.pszText = sn;
        SendMessageW(g_hList, TVM_SETITEMW, 0, (LPARAM)&t);
        SendMessageW(g_hList, TVM_EXPAND, TVE_EXPAND, (LPARAM)rootItem[f]);
    }
}

// 当前选中的叶子效果下标（根/无选中返回 -1）
static int SelectedEffectIndex() {
    HTREEITEM hSel = (HTREEITEM)SendMessageW(g_hList, TVM_GETNEXTITEM, TVGN_CARET, 0);
    if (!hSel) return -1;
    TVITEMW t = {}; t.mask = TVIF_PARAM; t.hItem = hSel;
    SendMessageW(g_hList, TVM_GETITEMW, 0, (LPARAM)&t);
    int idx = (int)t.lParam;
    return (idx >= 0 && idx < g_effectCount) ? idx : -1;
}

static void PlaylistAdd() {
    int sel = SelectedEffectIndex();
    if (sel < 0) { SetWindowTextW(g_hStatus, L"  请先选择左侧一个具体效果"); return; }
    wchar_t buf[16]; GetWindowTextW(g_hEditDur, buf, 16);
    int dur = _wtoi(buf); if (dur < 1) dur = 1;
    PlaylistItem it; it.name = g_effects[sel].name; it.durationSec = dur;
    g_playlist.push_back(it);
    RefreshPlaylistUI();
    ListBox_SetCurSel(g_hPlaylist, (int)g_playlist.size()-1);
}

static void PlaylistRemove() {
    int sel = (int)SendMessageW(g_hPlaylist, LB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int)g_playlist.size()) return;
    g_playlist.erase(g_playlist.begin() + sel);
    RefreshPlaylistUI();
}

static void PlaylistMoveUp() {
    int sel = (int)SendMessageW(g_hPlaylist, LB_GETCURSEL, 0, 0);
    if (sel > 0 && sel < (int)g_playlist.size()) { std::swap(g_playlist[sel], g_playlist[sel-1]); RefreshPlaylistUI(); ListBox_SetCurSel(g_hPlaylist, sel-1); }
}
static void PlaylistMoveDown() {
    int sel = (int)SendMessageW(g_hPlaylist, LB_GETCURSEL, 0, 0);
    if (sel >= 0 && sel < (int)g_playlist.size()-1) { std::swap(g_playlist[sel], g_playlist[sel+1]); RefreshPlaylistUI(); ListBox_SetCurSel(g_hPlaylist, sel+1); }
}

static void PlaylistStart() {
    // ---- 【2026-09-29 用户要求】打开播放链时，这三个键控制的开关默认关掉 ----
    //   D = 抖动/噪点      （g_ditherStrength 1.0 → 0）
    //   Q = 画质 ⅔ 降采样  （关 = 全分辨率）
    //   F = 帧反馈 warp    （MilkDrop 式拖影）
    // 键本身照旧可用 —— 打开之后按对应键仍然能临时打开/关掉。
    // ⚠ F 关掉之后，Fibersnake 会失去它那层"拖影"（那个效果就是靠引擎的 FeedbackWarp
    //   当第四层，见 VisualEffects/FS/FSRender.cpp 的文件头）—— 想看那个效果得按一下 F。
    g_ditherStrength = 0.0f;
    g_downscaleOn    = false;
    g_feedbackOn     = false;

    // GL 播放链窗口重建上下文 → 重置 B376 网格/场句柄，强制重建（否则二次打开用失效句柄闪退）
    g_b376MeshVao = 0; g_b376MeshProg = 0;
    g_b376FieldTex = 0; g_b376LumTex = 0; g_b376IdTex = 0; g_b376StaticTex = 0; g_b376ArcTex = 0; g_b376CurveTex = 0;
    ResetMeshRenderer(g_m2Renderer); ResetMeshRenderer(g_m3Renderer); ResetMeshRenderer(g_m4Renderer);   // M2/M3/M4 同款：失效句柄清零强制重建
    ResetC3LineRenderer(g_c3Renderer);   // Cubeeffect3 线段渲染器同款清零
    ResetPCRenderer();                   // StellarVortex 粒子渲染器同款清零
    ResetVXRenderer();                   // Involute 同款清零
    ResetSSRenderer();                   // Involute_1 同款清零（漏了 = 关掉播放窗再开必崩）
    ResetTNRenderer();                   // Filigree 同款清零
    ResetXS();                           // Shardstorm 同款清零
    ResetF1Renderer();                   // Function1 同款清零（顺带把入场进度归零 → 重新播入场）
    g_pcPalIdx = 0;                      // 调色板也跟着回第一套
    g_pcCurvePos = g_pcCurveTarget = 0.0f;
    g_pcCamYaw = g_pcCamYawT = 0.0f;
    g_pcCamPitch = g_pcCamPitchT = -0.42f;
    g_pcCamDist = g_pcCamDistT = kPCCamZ;
    if (g_playlist.empty()) return;
    PLStart(); g_playerMode = true;
    if (!g_hPlayer) {
        static bool reg = false;
        if (!reg) { WNDCLASSEXW wc={}; wc.cbSize=sizeof(wc); wc.style=CS_HREDRAW|CS_VREDRAW|CS_OWNDC; wc.lpfnWndProc=PlayerWndProc; wc.hInstance=GetModuleHandle(nullptr); wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH); wc.lpszClassName=L"VizPlayerGPU"; RegisterClassExW(&wc); reg=true; }
        const wchar_t* title = L"播放链 [GPU]";
        // 无边框：丢掉 WS_CAPTION/WS_OVERLAPPED，那条白标题栏就没了。
        // 保留 WS_THICKFRAME 是为了留下系统的缩放热区 —— 配合 WM_NCCALCSIZE 把边框尺寸
        // 清零（客户区 = 整个窗口），于是边框【有功能但不可见】。窗口标题仍传给任务栏。
        const DWORD pstyle = WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU;
        g_hPlayer = CreateWindowExW(0, L"VizPlayerGPU", title, pstyle, CW_USEDEFAULT,CW_USEDEFAULT,PLAYER_DEFAULT_W,PLAYER_DEFAULT_H, nullptr,nullptr,GetModuleHandle(nullptr),nullptr);
        // 置顶由设置里的「窗口置顶」决定（默认开）
        SetWindowPos(g_hPlayer, g_plTopmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                     0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        ShowWindow(g_hPlayer, SW_SHOW); UpdateWindow(g_hPlayer);
    }
    EnableWindow(g_hBtnPlay, FALSE); EnableWindow(g_hBtnStop, TRUE);
    InvalidateRect(g_hPlayer, nullptr, FALSE);
}

static void PlaylistStop() {
    PLStop(); g_playerMode = false;
    if (g_hPlayer) { DestroyWindow(g_hPlayer); g_hPlayer = nullptr; }
    EnableWindow(g_hBtnPlay, TRUE); EnableWindow(g_hBtnStop, FALSE);
}

static void ClearPlaylist() {
    PlaylistStop();
    g_playlist.clear();
    RefreshPlaylistUI();
    if (g_hStatus) SetWindowTextW(g_hStatus, L"  已清空播放链");
}

static void DrawClearRing(HDC hdc, POINT c) {
    DWORD elapsed = GetTickCount() - g_holdStartTick;
    float progress = (float)elapsed / (float)CLEAR_HOLD_MS;
    if (progress < 0) progress = 0; if (progress > 1) progress = 1;
    int r = 13;
    int cx = c.x + 22, cy = c.y - 22;
    HPEN gp = CreatePen(PS_SOLID, 3, RGB(150,150,150));
    HGDIOBJ op = SelectObject(hdc, gp);
    HGDIOBJ ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Ellipse(hdc, cx-r, cy-r, cx+r, cy+r);
    double ang = progress * 2.0 * 3.14159265358979;
    int x1 = cx, y1 = cy - r;
    int x2 = cx + (int)(r * sin(ang));
    int y2 = cy - (int)(r * cos(ang));
    HBRUSH cb = CreateSolidBrush(RGB(80, 200, 120));
    SelectObject(hdc, GetStockObject(NULL_PEN));
    SelectObject(hdc, cb);
    Pie(hdc, cx-r, cy-r, cx+r, cy+r, x1, y1, x2, y2);
    DeleteObject(cb);
    SelectObject(hdc, op);
    SelectObject(hdc, ob);
    DeleteObject(gp);
}

static LRESULT CALLBACK ClearBtnProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_LBUTTONDOWN:
        g_holdingClear = true;
        g_holdCompleted = false;
        g_holdStartTick = GetTickCount();
        GetCursorPos(&g_holdCursorPt);
        SetTimer(g_hwnd, TIMER_CLEAR_HOLD, 16, nullptr);
        InvalidateRect(g_hwnd, nullptr, TRUE);
        break;
    case WM_LBUTTONUP:
        if (g_holdCompleted) {
            g_holdCompleted = false;
            g_holdingClear = false;
            return 0;  // 已清空，吞掉本次点击
        }
        if (g_holdingClear) {
            g_holdingClear = false;
            KillTimer(g_hwnd, TIMER_CLEAR_HOLD);
            InvalidateRect(g_hwnd, nullptr, TRUE);
        }
        break;
    case WM_CAPTURECHANGED:
        if (g_holdingClear && !g_holdCompleted) {
            g_holdingClear = false;
            KillTimer(g_hwnd, TIMER_CLEAR_HOLD);
            InvalidateRect(g_hwnd, nullptr, TRUE);
        }
        break;
    }
    return CallWindowProcW(g_btnClearOldProc, hwnd, msg, wParam, lParam);
}

// 预设存取
static void PresetsSaveToFile() {
    wchar_t path[MAX_PATH]; GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* p = wcsrchr(path, L'\\'); if (p) *(p+1) = L'\0'; wcscat(path, L"presets.dat");
    FILE* f = _wfopen(path, L"w"); if (!f) return;
    for (int s=0;s<5;++s){if(g_presets[s].empty())continue;fwprintf(f,L"SLOT %d\n",s);for(auto&it:g_presets[s])fwprintf(f,L"%s|%s|%d|%.3f|%d\n",it.name.c_str(),it.name2.c_str(),(int)it.blendMode,it.opacity,it.durationSec);}
    fclose(f);
}
static void PresetsLoadFromFile() {
    wchar_t path[MAX_PATH]; GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* p = wcsrchr(path, L'\\'); if (p) *(p+1) = L'\0'; wcscat(path, L"presets.dat");
    FILE* f = _wfopen(path, L"r"); if (!f) return;
    wchar_t line[512]; int cs=-1;
    while(fgetws(line,512,f)){if(wcsncmp(line,L"SLOT ",5)==0){cs=_wtoi(line+5);if(cs<0)cs=0;if(cs>4)cs=4;g_presets[cs].clear();}else if(cs>=0){PlaylistItem it;wchar_t*ctx=nullptr;wchar_t*tok=wcstok(line,L"|\n",&ctx);if(tok)it.name=tok;
        // 旧预设兼容：Cubeeffect3 已改名为 "Cubeeffect3 (dark)"，不映射的话旧 presets.dat 里
        // 这一项会因为 EffectIndex 精确匹配找不到而被丢掉。
        if (it.name == L"Cubeeffect3") it.name = L"Cubeeffect3 (dark)";tok=wcstok(nullptr,L"|\n",&ctx);if(tok)it.name2=tok;tok=wcstok(nullptr,L"|\n",&ctx);if(tok)it.blendMode=(BlendMode)_wtoi(tok);tok=wcstok(nullptr,L"|\n",&ctx);if(tok)it.opacity=(float)_wtof(tok);tok=wcstok(nullptr,L"|\n",&ctx);if(tok)it.durationSec=_wtoi(tok);if(!it.name.empty()&&it.durationSec>0)g_presets[cs].push_back(it);}}
    fclose(f);
}
static void PresetSave(int s) { g_presets[s]=g_playlist; PresetsSaveToFile(); wchar_t st[64];_snwprintf(st,64,L"  已存入预设%d（%d项）",s+1,(int)g_presets[s].size());SetWindowTextW(g_hStatus,st); }
static void PresetLoad(int s) { if(g_presets[s].empty()){SetWindowTextW(g_hStatus,L"  预设为空");return;}for(auto&it:g_presets[s])g_playlist.push_back(it);RefreshPlaylistUI();wchar_t st[64];_snwprintf(st,64,L"  已追加预设%d",s+1);SetWindowTextW(g_hStatus,st); }
static void PresetDelete(int s) { g_presets[s].clear(); PresetsSaveToFile(); }
static void UpdatePresetButtons() { for(int i=0;i<5;++i){int n=(int)g_presets[i].size();wchar_t lbl[32];_snwprintf(lbl,32,L"%d%s",i+1,n>0?L" ✓":L" -");HWND btn=GetDlgItem(g_hwnd,IDC_PRESET_BASE+i);if(btn)SetWindowTextW(btn,lbl);} }

// ===== Cubeeffect1 form1「粘滴/染墨」CPU 模拟（不播 Cubeeffect1 或停在 form0 时清空）=====
// 可复现随机数（简单 hash）
static float cubeRnd(int a, int b){ unsigned v=(unsigned)(a*73856093u ^ b*19349663u); v=(v^(v>>16))*0x7feb352du; v=(v^(v>>13))*0x846ca68bu; v^=(v>>16); return (v&0xffffff)/16777216.0f; }
// 为某滴在所属深度 oz 生成一条"类圆多边形"轨迹（相机系偏移 ox/oy）。
// 在一段弧上布点近似成圆；起点在弧起点外侧(画外)，绕大半圈穿过可视带，终点在对侧画外
// → 粘滴像"从窗口外切线切入 - 绕圈穿行 - 从对侧切出"，而不是直线直入。
static void GenCubeDropPath(CubeDrop& d, int seed){
    const float R  = (0.20f + 0.10f*cubeRnd(seed,1))*d.oz;   // 类圆半径（在可视带内）
    // 圆心在"整个窗口"里均匀散布：深度 oz 处窗口半高=0.2×oz、半宽=0.2×aspect×oz，圆心布满全场
    const float halfV = 0.20f*d.oz;                    // 窗口纵向半高
    const float halfH = 0.20f*g_cubeGfxAspect*d.oz;    // 窗口横向半宽
    const float cx = (cubeRnd(seed,2)-0.5f)*2.0f*halfH;   // 圆心横贯整个窗口
    const float cy = (cubeRnd(seed,3)-0.5f)*2.0f*halfV;   // 圆心纵贯整个窗口
    const int   nA = 10 + (int)(cubeRnd(seed,4)*6.0f);       // 弧上采样点 10..15（类圆多边形）
    const float a0 = cubeRnd(seed,5)*6.28318f;               // 起始角
    const float sweep = 3.14159f*(0.85f + 0.5f*cubeRnd(seed,6));   // 扫过 ~0.85π..1.35π（大半圈）
    const float dir  = cubeRnd(seed,7) < 0.5f ? 1.0f : -1.0f;      // 绕行方向
    const float leave= 1.7f;                                      // 入/出场半径倍率（画外切入）
    // 入场点：弧起点外侧（画外）
    d.px[0]=cx + cosf(a0 - dir*0.55f)*R*leave;
    d.py[0]=cy + sinf(a0 - dir*0.55f)*R*leave;
    int kk=1;
    for(int k=0;k<nA;k++){
        float a=a0 + dir*sweep*(float)k/(float)(nA-1);
        float rr=R*(1.0f + (cubeRnd(seed,20+k*3)-0.5f)*0.10f);   // 轻微半径抖动（类圆、非完美正圆）
        d.px[kk]=cx + cosf(a)*rr;
        d.py[kk]=cy + sinf(a)*rr;
        kk++;
    }
    // 离场点：弧终点外侧（画外）
    d.px[kk]=cx + cosf(a0 + dir*sweep + dir*0.55f)*R*leave;
    d.py[kk]=cy + sinf(a0 + dir*sweep + dir*0.55f)*R*leave;
    d.polyN=kk+1;
    d.tAlong=0.0f; d.mergeT=0.0f;
}
// 多边形总弧长
static float CubeDropPolyLen(const CubeDrop& d){
    float L=0;
    for(int k=0;k<d.polyN-1;k++) L+=hypotf(d.px[k+1]-d.px[k], d.py[k+1]-d.py[k]);
    return L;
}
// 沿多边形取位置 (tAlong∈[0,totalLen]) → (ox,oy,vx,vy)
static void CubeDropSample(const CubeDrop& d, float tt, float& ox, float& oy, float& vx, float& vy){
    // 求段
    float acc=0;
    for(int k=0;k<d.polyN-1;k++){
        float dx=d.px[k+1]-d.px[k], dy=d.py[k+1]-d.py[k];
        float len=hypotf(dx,dy);
        if(tt<=acc+len || k==d.polyN-2){
            float t=(tt-acc)/len; t=t<0?0:(t>1?1:t);
            ox=d.px[k]+dx*t; oy=d.py[k]+dy*t;
            float vl=hypotf(dx,dy)+1e-5f; vx=dx/vl; vy=dy/vl;
            return;
        }
        acc+=len;
    }
    ox=d.px[0]; oy=d.py[0]; vx=1; vy=0;
}

static void UpdateCubeDrops(float camZ, float dt) {
    // 判定当前是否正播放 Cubeeffect1（合并后的双形态：form1=黑底灰网格+粘滴）
    GpuEffect* cube = nullptr;
    for (int layer = 0; layer < 2; ++layer) {
        int idx = g_plEffIdx[layer];
        if (g_plState != PL_STOPPED && idx >= 0 && idx < g_effectCount &&
            g_effects[idx].name == L"Cubeeffect1") { cube = &g_effects[idx]; break; }
    }
    if (!cube) { g_cubeMarks.clear(); return; }
    // 完全处于 form0（白底墨线网格+飞蚊、不显示粘滴）：清空闪光点，先不推进粘滴
    if (cube->form == 0 && cube->formBlend >= 1.0f) { g_cubeMarks.clear(); return; }

    if (g_cubeDrops.empty()) {   // 首次进入：初始化 5 滴（各自从画面外切入，起始相位略错开）
        for (int i = 0; i < CUBE_DROP_N; ++i) {
            CubeDrop d; memset(&d,0,sizeof d);
            d.r = d.r0 = 0.005f;                 // 小亮滴（"一点点"光点）
            d.oz = 1.4f + 0.35f*(i%10);           // 各滴不同所属深度（铺开 1.4~4.55，150 滴拉得更散）
            GenCubeDropPath(d, 1000+i);
            d.tAlong = CubeDropPolyLen(d) * (0.0f + 0.05f*cubeRnd(1000+i,5));   // 起始相位错开
            g_cubeDrops.push_back(d);
        }
    }
    // 粘滴速度 ∝ 音频能量：基础 1× 到满能量 10×（能量∈[0,1]）。只缩速度不叠加相位 → 变速不回跳。
    { float eng = g_af.energy < 0.0f ? 0.0f : (g_af.energy > 1.0f ? 1.0f : g_af.energy);
      g_cubeDropSpeedBase = 0.5f * (1.0f + 9.0f*eng); }   // 0.5×(1..10) = 0.5~5.0 世界单位/秒
    const float SPEED = g_cubeDropSpeedBase;   // 每滴沿类圆折线速度(世界单位/秒，音频驱动)
    const float MERGE = 0.55f;   // 碰线"融进棱边"时长（体积渐缩、不再前进、融入线里）
    const float TOUCH = 0.03f;   // 贴线判定距离(≈滴半径量级)
    const float FADE  = 1.9f;    // 撞边闪光每秒衰减（≈0.5s 峰值后淡去）
    for (int i = 0; i < (int)g_cubeDrops.size(); ++i) {
        CubeDrop& d = g_cubeDrops[i];
        if (d.mergeT > 0.0f) {               // 融边中：体积渐缩→融入棱线，位置钉在接触点
            d.mergeT -= dt;
            d.r = d.r0 * (d.mergeT / MERGE);
            if (d.mergeT <= 0.0f) {          // 融完 → 换新轨迹，重新从画面外切入
                d.mergeT = 0.0f; d.r = d.r0;
                d.oz = 1.4f + 0.35f*(i%10) + 0.25f*cubeRnd(i,77);
                GenCubeDropPath(d, 5000+i);
            }
            continue;
        }
        float totalLen = CubeDropPolyLen(d);
        d.tAlong += SPEED*dt;
        if (d.tAlong >= totalLen) {          // 轨迹走完（未碰线离场）→ 换新轨迹
            d.oz = 1.8f + 0.7f*(i%3) + 0.25f*cubeRnd(i,77);
            d.r  = d.r0;
            GenCubeDropPath(d, 5000+i);
            totalLen = CubeDropPolyLen(d);
        }
        float ox,oy,vx,vy; CubeDropSample(d,d.tAlong,ox,oy,vx,vy);
        d.x = 0.35f+ox;  d.y = 0.32f+oy;  d.z = camZ + d.oz;
        d.vx = vx;  d.vy = vy;
        // 碰线检测：仅"与镜头前进方向垂直的墙面"(z 恒定面) 内的棱线 → 只认 沿x / 沿y 两族棱
        float rx=d.x-floorf(d.x+0.5f), ry=d.y-floorf(d.y+0.5f), rz=d.z-floorf(d.z+0.5f);
        float dX=sqrtf(ry*ry+rz*rz), dY=sqrtf(rx*rx+rz*rz);
        float best=dX; int axis=1; if(dY<best){best=dY;axis=2;}
        if (best < TOUCH) {
            d.mergeT = MERGE;               // 开始融进棱线（带拖尾、先闪一下）
            CubeMark m; m.flash=1.0f; m.axis=axis;
            if(axis==1){m.x=d.x;m.y=floorf(d.y+0.5f);m.z=floorf(d.z+0.5f);}
            else       {m.x=floorf(d.x+0.5f);m.y=d.y;m.z=floorf(d.z+0.5f);}
            bool dup=false;
            for(auto&e:g_cubeMarks){
                if(e.axis!=m.axis)continue;
                if(axis==1&&fabsf(e.x-m.x)<0.5f&&fabsf(e.y-m.y)<0.3f&&fabsf(e.z-m.z)<0.3f){dup=true;break;}
                if(axis==2&&fabsf(e.x-m.x)<0.3f&&fabsf(e.y-m.y)<0.5f&&fabsf(e.z-m.z)<0.3f){dup=true;break;}
            }
            if(!dup) g_cubeMarks.push_back(m);
        }
    }
    // 闪光衰退（每帧对全部墨点衰减）
    for(auto&e:g_cubeMarks) e.flash -= dt*FADE;
    // 淘汰：闪光淡尽 / 相机后方太远 / 超上限
    for(auto it=g_cubeMarks.begin(); it!=g_cubeMarks.end();){
        if(it->flash<=0.0f || it->z<camZ-2.0f) it=g_cubeMarks.erase(it); else ++it; }
    while((int)g_cubeMarks.size()>CUBE_MARK_MAX)g_cubeMarks.erase(g_cubeMarks.begin());
}

// ===== Cubeeffect3 锚点更新（CPU 单一真源）=====
// 仅当正播放 Cubeeffect3 时维护；否则清空。每个鼓点(hit)把最旧锚点重新掷在
// 【图像平面】(世界坐标 x∈[-1,1], y∈[-1/imgA,1/imgA], z=0) 的随机位置并重掷方向；
// 锚点按出生时间老化回收。shader 里线段两端各自在本顶点位置采样锚点高斯 → 端点错开成折线。
static void UpdateCubeVeils(bool hit) {
    // 判定当前是否正播放 Cubeeffect3（深/浅两种都算：meshSlot 4 = dark，5 = light）
    GpuEffect* cube = nullptr;
    for (int layer = 0; layer < 2; ++layer) {
        int idx = g_plEffIdx[layer];
        if (g_plState != PL_STOPPED && idx >= 0 && idx < g_effectCount &&
            g_effects[idx].isMesh &&
            (g_effects[idx].meshSlot == 4 || g_effects[idx].meshSlot == 5)) {
            cube = &g_effects[idx]; break;
        }
    }
    if (!cube) { if (!g_cubeVeils.empty()) g_cubeVeils.clear();
                 if (!g_c3Origami.empty()) g_c3Origami.clear(); return; }

    if (hit) {
        // 单次 hit：在整个【可见屏幕】范围内撒一组锚点（抖动网格 + 各自随机方向），
        // 使一次鼓点的扭曲【覆盖全屏】，而不是只弹一小片。
        //
        // 【可见世界范围必须按当前窗口宽高比算】—— shader 的投影是 contain-fit：
        //     fitK=3.5, fitM=3.5*uImgA
        //     aspect*fitK >= fitM ? (m=fitM, k=m/aspect) : (k=fitK, m=k*aspect)
        //     世界半范围 halfX = camZ/k, halfY = camZ/m
        // 16:9 时 halfX=1.0, halfY=1/uImgA；窗口更宽 → halfX 变大；更窄 → halfY 变大。
        // 写死成 16:9 的矩形是 bug：改成别的宽高比后，外围一圈就没有锚点、不扭曲。
        const float camZ   = 3.5f;                   // 与 shader 的 camZ 一致
        const float aspect = (g_clientW > 0 && g_clientH > 0)
                             ? (float)g_clientW / (float)g_clientH : kC3ImgA;
        const float fitK = camZ, fitM = camZ * kC3ImgA;
        float pk, pm;
        if (aspect * fitK >= fitM) { pm = fitM;      pk = pm / aspect; }
        else                       { pk = fitK;      pm = pk * aspect; }
        const float halfX = camZ / pk;               // 可见世界半宽
        const float halfY = camZ / pm;               // 可见世界半高

        // 撒点用共享实现（C3Veil.h）—— CPU 版调的是同一个函数、同一个种子，
        // 所以两个后端得到的是同一批锚点位置。
        C3SpawnVeils(g_cubeVeils, halfX, halfY, g_plTime, (unsigned)g_beatCount, C3_VEIL_MAX);
        // 折纸片：本次 hit 输出一批（随机格子 + 各自从折痕交点向外显化）
        C3SpawnOrigami(g_plTime, (unsigned)g_beatCount);
    }

    // 回收：锚点太老（被后续鼓点取代）→ 移除。
    // age < 0 也要扔：时间倒流（换效果 g_plTime 归零、而时间戳没来得及平移）时，
    // 原来只有 "> LIFE" 一条判据，负年龄永远不满足 → 这种锚点会活到下次重启。
    for (auto it = g_cubeVeils.begin(); it != g_cubeVeils.end();) {
        float age = g_plTime - it->spawnT;
        if (age > C3_VEIL_LIFE || age < -0.05f) it = g_cubeVeils.erase(it); else ++it; }
    // 折纸片回收：① 被下一批顶掉并已淡完 ② 兜底寿命到了 ③ 时间倒流（切效果 g_plTime 归零）
    for (auto it = g_c3Origami.begin(); it != g_c3Origami.end();) {
        float age = g_plTime - it->spawnT - it->delay;
        bool dead = (age > C3_ORIGAMI_LIFE) || (age < -0.05f);
        if (!dead && it->killT > 0.0f && g_plTime - it->killT > C3_ORIGAMI_KILL) dead = true;
        if (dead) it = g_c3Origami.erase(it); else ++it; }
}

static void UpdateBeat() {
    float dt = 0.016f;
    std::vector<float> buf(4096); int n = 0;
    g_audio.Capture(buf.data(), 4096, n);
    // BPMDetector 内部 kick / snare 是两套独立的频谱签名判定（见 BPMDetector.h），
    // 过去只取合并的返回值、把分路丢掉。这里两路都取出来往下传。
    bool kick = false, snare = false;
    if (n > 0) { g_silentFrames = 0; g_drums.Process(buf.data(), n);
                 kick = g_drums.IsKick(); snare = g_drums.IsSnare(); }
    else { g_silentFrames++; if (g_silentFrames > 180) { g_silentFrames = 0; g_audio.Shutdown(); if (g_audio.Initialize()) g_drums = BPMDetector(g_audio.GetSampleRate()); } }
    bool hit = kick || snare;
    if (hit) g_beatCount++;
    // ---- 音频分析：频谱/波形/能量包络/对拍（GPU/CPU 共用同一份 AudioFrame）----
    g_audioAnalyzer.Process(buf.data(), n, dt, g_bpm, kick, snare);
    AudioFrame af = g_audioAnalyzer.Frame();   // 拷贝，下面应用全局强度
    // 全局音频强度（临时滑杆 g_audioStrength；定稿后移除）。只缩幅度类字段，
    // beatPhase/bpm 是相位/节拍，不缩放。
    const float as = g_audioStrength;
    for (int i = 0; i < kAudioBands; ++i)     af.fft[i] *= as;
    for (int i = 0; i < kAudioWaveSamps; ++i) af.wave[i] *= as;
    af.bass *= as; af.mid *= as; af.treb *= as;
    af.bassAtt *= as; af.midAtt *= as; af.trebAtt *= as;
    af.bassDec *= as; af.midDec *= as; af.trebDec *= as;
    af.energy *= as; af.beatFlash *= as;
    af.hitKick *= as; af.hitSnare *= as;
    // NaN/超界防护：强瞬态或采集异常不得污染渲染（鼓点导致的崩坏多半来自 NaN uniform）
    for (int i = 0; i < kAudioBands; ++i)     if (af.fft[i] != af.fft[i] || af.fft[i] < 0.0f || af.fft[i] > 4.0f) af.fft[i] = 0.0f;
    for (int i = 0; i < kAudioWaveSamps; ++i) if (af.wave[i] != af.wave[i] || af.wave[i] < -4.0f || af.wave[i] > 4.0f) af.wave[i] = 0.0f;
    auto sn = [](float& x) { if (x != x || x < -4.0f || x > 4.0f) x = 0.0f; };
    sn(af.bass); sn(af.mid); sn(af.treb);
    sn(af.bassAtt); sn(af.midAtt); sn(af.trebAtt);
    sn(af.bassDec); sn(af.midDec); sn(af.trebDec);
    sn(af.energy); sn(af.beatFlash);
    sn(af.hitKick); sn(af.hitSnare);
    g_af = af;                   // GPU uniform 上传用（已缩放+防护）
    // Metalheart1/2/3 流动相位实时累计（积分）：流速 = 基础 ×(1+49×能量) = 1x~50x（音频实时变速不跳变）
    // 能量增益 1.5：典型音乐(0.5)→25.5x、大声压(0.67+)→35x、满能量→50x，让 50x 更够得着
    { float eng = af.energy * 1.5f; eng = eng < 0.0f ? 0.0f : (eng > 1.0f ? 1.0f : eng);
      g_b376FlowPhase += (g_b376FlowAmp * 0.03f * (1.0f + 49.0f * eng)) * dt;   // Metalheart1 曲线流相位
      g_m2Renderer.flowPhase += (g_b376FlowAmp * 0.03f * (1.0f + 49.0f * eng)) * dt;   // Metalheart2
      g_m3Renderer.flowPhase += (g_b376FlowAmp * 0.03f * (1.0f + 49.0f * eng)) * dt;   // Metalheart3
      g_m4Renderer.flowPhase += (g_b376FlowAmp * 0.03f * (1.0f + 49.0f * eng)) * dt;   // Metalheart4
    }
    g_audioPhase += (af.trebAtt * 0.3f) * dt;   // 全效果：高频驱动旋转相位（积分，变速不跳回）
    // 抽插变速：把"速度倍率"积分成【相位偏移】（同 g_audioPhase 的做法）。
    // 若直接写 uTime×(1+0.55·kick)，kick 一变 T 就整体跳一下 —— 画面瞬移。
    // 积分成相位则只是"这段时间走得快些"，连续、不跳回。
    // 这里写的是【速度倍率的峰值】而不是增量 —— 增量写法容易把
    // "1.55× 的 10 倍" 误读成 "增量 ×10"（那只有 6.5×）。
    // 一记 kick 贡献的额外相位 ≈ 8.5 × 0.28s ≈ 2.4s —— 鼓点一下画面猛冲一段。
    g_hitDrive += ((kHitPumpKick - 1.0f) * af.hitKick
                 + (kHitPumpSnare - 1.0f) * af.hitSnare) * dt;

    // ---- 鼓点【单次运动周期】通道（uHitFlow / uHitFlowDrive）----
    //
    // 规则（按用户要求）：
    //   · 没有 hit        → 【正常速率 1.00×】一直播（不再减速、不再冻住）
    //   · 来了一个 hit    → 叠一个高斯脉冲：1.00× → 1.50× → 1.00×，0.9 秒走完
    //   · 周期进行中又来 hit → 【不接受】，直接忽略（连击不会叠加）
    //
    // 为什么之前会"抽"：uHitDrive 积分的是尖峰包络，导数是方波 —— 0→8.5→0 一步到位。
    // 现在速率 = 1 + 0.5·高斯，两端都等于 1，导数连续，
    // 而且基础速率不归零，所以既不会"突然卡住"，也不会回到相位初始态。
    {
        // 上升沿 = 新的一记 kick（包络在一帧内从低跳到 1；衰减中的包络不会触发）
        const bool freshKick = (af.hitKick > g_prevHitKick + 0.35f);
        g_prevHitKick = af.hitKick;

        if (g_hitCycleT < 0.0f && freshKick) g_hitCycleT = 0.0f;   // 只有"空闲"才接受新 hit

        float rate = kHitCycleIdle;
        if (g_hitCycleT >= 0.0f) {
            g_hitCycleT += dt;
            const float p = g_hitCycleT / kHitCycleDur;
            if (p >= 1.0f) {
                g_hitCycleT = -1.0f;                               // 周期结束，回到空闲
            } else {
                // 【正态（高斯）脉冲】rate = 1 + 0.5·exp(-((t-t0)/σ)²)
                // 峰值推迟到 t0=0.35s 才出现 —— 命中那一帧高斯值只有 0.004，
                // 所以是从 1.00× 平滑地涨上去，不会"啪"跳到 1.5×（那才是抽搐）。
                // 到 t=0.90s 时高斯只剩 1e-6，回到 1.00× 的接缝也看不见。
                const float u = (g_hitCycleT - kHitPulseT0) / kHitPulseSig;
                rate = kHitCycleIdle + (kHitCyclePeak - kHitCycleIdle) * std::exp(-u * u);
            }
        }
        // 归一化到 0..1 给 uHitFlow（"现在有多用力"），积分给 uHitFlowDrive（"走了多少相位"）
        g_hitFlow      = (rate - kHitCycleIdle) / (kHitCyclePeak - kHitCycleIdle);
        g_hitFlowDrive += rate * dt;
    }
    // MilkDrop 的两级低通（逐帧、不乘 dt —— 原作就是按帧衰减的）
    g_mdVb  = g_mdVb  * 0.95f + (1.0f - g_mdVb ) * af.bassAtt * af.bassAtt * 0.02f;
    g_mdVvb = g_mdVvb * 0.95f + (1.0f - g_mdVvb) * g_mdVb  * 0.01f;
    g_mdVm  = g_mdVm  * 0.95f + (1.0f - g_mdVm ) * af.midAtt  * af.midAtt  * 0.02f;
    g_mdVvm = g_mdVvm * 0.95f + (1.0f - g_mdVvm) * g_mdVm  * 0.01f;
    g_mdVt  = g_mdVt  * 0.95f + (1.0f - g_mdVt ) * af.trebAtt * af.trebAtt * 0.02f;
    g_mdVvt = g_mdVvt * 0.95f + (1.0f - g_mdVvt) * g_mdVt  * 0.01f;
    g_mdQ28 = (g_mdVvb + g_mdVvm + g_mdVvt) * 10.0f;
    // Cubeeffect1 相机 z 相位：推进速度 0.47×(1+4×energy) = 1x~5x（energy∈[0,1]），CPU 积分变速不回跳
    { float ce = af.energy < 0.0f ? 0.0f : (af.energy > 1.0f ? 1.0f : af.energy);
      g_cubeCamPhase += (0.47f * (1.0f + 4.0f * ce)) * dt;
      UpdateCubeDrops(g_cubeCamPhase, dt); }   // Cubeeffect1 form1：粘滴沿类圆轨迹推进 + 撞边闪光（非播放/form0 时清空）
    // Cubeeffect3 层前进相位：镜头推进速度由【高频能量】驱动，下限 1×、上限 20×。
    //   公式在 C3Veil.h 的 C3LayerAdvance()—— GPU 与 CPU 共用同一份，
    //   不再各写一遍（见那里的注释：预增益 C3_TREB_GAIN 是唯一需要调的旋钮）。
    //   回绕 SPAN → 后层从远处浮现、前层飞过。
    { g_c3TrebRef.Update(af.trebRelAtt, dt);          // 满量程自适应（跟踪近期高频峰值）
      g_c3LayerPhase += C3LayerAdvance(af.trebRelAtt, g_c3TrebRef.ref, dt);
      if (g_c3LayerPhase >= kC3LayerSpan) g_c3LayerPhase -= kC3LayerSpan; }
    UpdateCubeVeils(hit);                      // Cubeeffect3：鼓点重掷锚点（每鼓点扰动一片线段端点；非播放时清空）
    g_sharedMem.Write(g_bpm, af.beatPhase, g_beatCount, hit);
    if (g_playerMode) PLUpdate(dt, g_bpm, af.beatPhase, hit);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hwnd = hwnd; g_sharedMem.Create();
        HFONT f = CreateFontW(16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei");

        // 顶部菜单栏（设置 → 打开设置对话框）
        {
            HMENU bar=CreateMenu();
            AppendMenuW(bar,MF_STRING,IDM_SETTINGS,L"设置");
            SetMenu(hwnd,bar);
        }

        int panelH=10;
        CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_ETCHEDHORZ,10,panelH,880,2,hwnd,nullptr,nullptr,nullptr);

        CreateWindowW(L"STATIC",L"可用的可视化效果",WS_CHILD|WS_VISIBLE|SS_LEFT,20,panelH+8,400,20,hwnd,nullptr,nullptr,nullptr);
        // 效果分类 TreeView（多家族分组，可折叠）+ GPU/CPU 状态圆点
        // 树用独立小号字体（13px），让长效果名也不顶到右侧圆点列
        static HFONT s_treeFont = CreateFontW(15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei");
        g_hList=CreateWindowW(WC_TREEVIEWW,nullptr,
            WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|TVS_HASLINES|TVS_LINESATROOT|TVS_HASBUTTONS|TVS_SHOWSELALWAYS,
            20,panelH+44,356,414,hwnd,(HMENU)IDC_LIST,nullptr,nullptr);   // 宽356(到x376)；底y=468留空给状态栏
        SendMessage(g_hList,WM_SETFONT,(WPARAM)s_treeFont,TRUE);
        // 深色主题：树背景/文字跟随现有 STATIC 深色方案（WM_CTLCOLORSTATIC 用的同色系）
        SendMessageW(g_hList, TVM_SETBKCOLOR, 0, (LPARAM)RGB(20,20,35));
        SendMessageW(g_hList, TVM_SETTEXTCOLOR, 0, (LPARAM)RGB(220,220,235));
        SendMessageW(g_hList, TVM_SETLINECOLOR, 0, (LPARAM)RGB(90,90,120));
        // GPU/CPU 列位置：按树客户区右端留 ~90px 两列（圆点自绘与表头共用同一 x 基准）
        {
            RECT rc; GetClientRect(g_hList, &rc);
            g_dotClientX_Gpu = rc.right - 62;
            g_dotClientX_Cpu = rc.right - 22;
            // 表头两个小标签：父坐标 = 树左(20) + 边框内缩 + 客户 x
            // 宽度给足(40px)避免 SS_RIGHT 裁掉左半字；文字右缘对齐点列中心
            RECT wrc; GetWindowRect(g_hList, &wrc);
            int insetX = ((wrc.right - wrc.left) - (rc.right - rc.left)) / 2;
            int hx = 20 + insetX;
            int hdrW = 40, hdrH = 14;
            g_treeHdrGpu = CreateWindowW(L"STATIC", L"GPU", WS_CHILD|WS_VISIBLE|SS_RIGHT,
                hx + g_dotClientX_Gpu - hdrW + 7, panelH+30, hdrW, hdrH, hwnd, nullptr, nullptr, nullptr);
            g_treeHdrCpu = CreateWindowW(L"STATIC", L"CPU", WS_CHILD|WS_VISIBLE|SS_RIGHT,
                hx + g_dotClientX_Cpu - hdrW + 7, panelH+30, hdrW, hdrH, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(g_treeHdrGpu, WM_SETFONT, (WPARAM)f, TRUE);
            SendMessageW(g_treeHdrCpu, WM_SETFONT, (WPARAM)f, TRUE);
        }
        RebuildEffectTree();

        CreateWindowW(L"STATIC",L"播放链",WS_CHILD|WS_VISIBLE|SS_LEFT,380,panelH+8,400,20,hwnd,nullptr,nullptr,nullptr);
        g_hPlaylist=CreateWindowW(L"LISTBOX",nullptr,WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|LBS_NOTIFY|LBS_HASSTRINGS,380,panelH+30,340,190,hwnd,(HMENU)IDC_PLAYLIST,nullptr,nullptr);
        SendMessage(g_hPlaylist,WM_SETFONT,(WPARAM)f,TRUE);

        int py=panelH+230;
        CreateWindowW(L"STATIC",L"时长(秒):",WS_CHILD|WS_VISIBLE|SS_RIGHT,380,py+4,60,22,hwnd,nullptr,nullptr,nullptr);
        g_hEditDur=CreateWindowW(L"EDIT",L"30",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER,445,py+2,45,24,hwnd,(HMENU)IDC_EDIT_DURATION,nullptr,nullptr);
        SendMessage(g_hEditDur,WM_SETFONT,(WPARAM)f,TRUE);SendMessage(g_hEditDur,EM_SETLIMITTEXT,3,0);

        CreateWindowW(L"BUTTON",L"加入→",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,380,py+30,70,28,hwnd,(HMENU)IDC_BTN_ADD,nullptr,nullptr);
        g_hBtnClear=CreateWindowW(L"BUTTON",L"←移除",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,455,py+30,70,28,hwnd,(HMENU)IDC_BTN_REMOVE,nullptr,nullptr);
        SendMessage(g_hBtnClear,WM_SETFONT,(WPARAM)f,TRUE);
        g_btnClearOldProc=(WNDPROC)SetWindowLongPtrW(g_hBtnClear,GWLP_WNDPROC,(LONG_PTR)ClearBtnProc);
        CreateWindowW(L"BUTTON",L"↑",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,535,py+30,35,28,hwnd,(HMENU)IDC_BTN_UP,nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"↓",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,575,py+30,35,28,hwnd,(HMENU)IDC_BTN_DOWN,nullptr,nullptr);

        g_hBtnPlay=CreateWindowW(L"BUTTON",L"▶ 播放链",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,380,py+66,100,32,hwnd,(HMENU)IDC_BTN_PLAY,nullptr,nullptr);
        SendMessage(g_hBtnPlay,WM_SETFONT,(WPARAM)f,TRUE);
        g_hBtnStop=CreateWindowW(L"BUTTON",L"⏹ 停止",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,490,py+66,100,32,hwnd,(HMENU)IDC_BTN_STOP,nullptr,nullptr);
        SendMessage(g_hBtnStop,WM_SETFONT,(WPARAM)f,TRUE);EnableWindow(g_hBtnStop,FALSE);

        // 混合模式
        int blendY=py+108;
        CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_ETCHEDHORZ,380,blendY,370,2,hwnd,nullptr,nullptr,nullptr);
        CreateWindowW(L"STATIC",L"混合模式:",WS_CHILD|WS_VISIBLE|SS_RIGHT,380,blendY+8,70,22,hwnd,nullptr,nullptr,nullptr);
        HWND hCombo=CreateWindowW(L"COMBOBOX",nullptr,WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,455,blendY+6,140,200,hwnd,(HMENU)IDC_COMBO_BLEND,nullptr,nullptr);
        SendMessageW(hCombo,WM_SETFONT,(WPARAM)f,TRUE);
        const wchar_t* bms[]={L"无混合",L"变亮",L"滤色",L"叠加",L"强光",L"柔光",L"线性加深",L"正片叠底"};
        for(int i=0;i<8;++i)SendMessageW(hCombo,CB_ADDSTRING,0,(LPARAM)bms[i]);
        SendMessageW(hCombo,CB_SETCURSEL,0,0);
        CreateWindowW(L"STATIC",L"不透明:",WS_CHILD|WS_VISIBLE|SS_RIGHT,380,blendY+34,70,22,hwnd,nullptr,nullptr,nullptr);
        HWND hOp=CreateWindowW(TRACKBAR_CLASSW,nullptr,WS_CHILD|WS_VISIBLE|TBS_HORZ|TBS_NOTICKS,455,blendY+32,130,28,hwnd,(HMENU)IDC_TRACK_OPACITY,nullptr,nullptr);
        SendMessageW(hOp,TBM_SETRANGE,TRUE,MAKELONG(0,100));SendMessageW(hOp,TBM_SETPOS,TRUE,100);
        CreateWindowW(L"STATIC",L"100%",WS_CHILD|WS_VISIBLE|SS_LEFT,590,blendY+34,50,22,hwnd,(HMENU)IDC_STATIC_OPACITY,nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"设为上层",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,380,blendY+62,80,28,hwnd,(HMENU)1133,nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"清除上层",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,470,blendY+62,80,28,hwnd,(HMENU)1134,nullptr,nullptr);

        // 预设按钮
        int presetX=730;
        CreateWindowW(L"STATIC",L"预设",WS_CHILD|WS_VISIBLE|SS_CENTER,presetX,panelH+8,100,18,hwnd,nullptr,nullptr,nullptr);
        for(int pi=0;pi<5;++pi){wchar_t lbl[32];_snwprintf(lbl,32,L"%d -",pi+1);CreateWindowW(L"BUTTON",lbl,WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,presetX,panelH+28+pi*48,100,40,hwnd,(HMENU)(IDC_PRESET_BASE+pi),nullptr,nullptr);}

        g_hStatus=CreateWindowW(L"STATIC",L"  正在加载 OpenGL shader...",WS_CHILD|WS_VISIBLE|SS_LEFT|SS_SUNKEN,0,panelH+470,900,22,hwnd,(HMENU)IDC_STATUS,nullptr,nullptr);

        DeleteObject(f);
        SetTimer(hwnd, TIMER_INIT, 100, nullptr);
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        DWORD cid=GetDlgCtrlID((HWND)lParam);
        if(cid==IDC_STATUS)return(LRESULT)GetStockObject(NULL_BRUSH);
        HDC hdcStatic=(HDC)wParam;SetBkMode(hdcStatic,OPAQUE);SetBkColor(hdcStatic,RGB(20,20,35));SetTextColor(hdcStatic,RGB(220,220,235));
        static HBRUSH br=CreateSolidBrush(RGB(20,20,35));return(LRESULT)br;
    }

    case WM_NOTIFY: {
        NMHDR* nh = (NMHDR*)lParam;
        if (nh && nh->idFrom == IDC_LIST && nh->code == NM_CUSTOMDRAW) {
            NMTVCUSTOMDRAW* cd = (NMTVCUSTOMDRAW*)lParam;
            if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) {
                // 请求 item 级通知（TreeView 需此返回才会发 ITEMPREPAINT/ITEMPOSTPAINT）
                return CDRF_NOTIFYITEMDRAW;
            }
            if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                int idx = (int)cd->nmcd.lItemlParam;
                if (idx >= 0 && idx < g_effectCount) {
                    // 在 PREPAINT 画圆点：圆点位于行右端，与左侧文字不重叠；画完返回默认让文字叠加其上
                    HDC hdc = cd->nmcd.hdc;
                    RECT r = cd->nmcd.rc;
                    int cy = (r.top + r.bottom) / 2;
                    auto dot = [&](int fromRight, bool ok) {
                        int cx = r.right - fromRight;
                        HBRUSH b = CreateSolidBrush(ok ? RGB(80,230,110) : RGB(255,70,70));
                        HGDIOBJ ob = SelectObject(hdc, b);
                        HGDIOBJ op = SelectObject(hdc, GetStockObject(NULL_PEN));
                        Ellipse(hdc, cx-6, cy-6, cx+6, cy+6);
                        SelectObject(hdc, ob); SelectObject(hdc, op);
                        DeleteObject(b);
                    };
                    dot(18, EffGpuAvail(idx));   // 可用性圆点
                }
                return CDRF_DODEFAULT;
            }
        }
        return 0;
    }

    case WM_TIMER:
        if(wParam==TIMER_INIT){KillTimer(hwnd,TIMER_INIT);
            g_audio.Initialize();if(g_audio.IsInitialized())g_drums=BPMDetector(g_audio.GetSampleRate());
            PresetsLoadFromFile();UpdatePresetButtons();SetTimer(hwnd,TIMER_MAIN,16,nullptr);
            wchar_t st[128];_snwprintf(st,128,L"  就绪 — %d 个效果 | BPM: %.0f",g_effectCount,g_bpm);SetWindowTextW(g_hStatus,st);
            return 0;}
        if(wParam==TIMER_MAIN){UpdateBeat();return 0;}
        if(wParam==TIMER_CLEAR_HOLD && g_holdingClear){
            if(GetTickCount()-g_holdStartTick >= CLEAR_HOLD_MS){
                g_holdingClear=false; g_holdCompleted=true; KillTimer(hwnd,TIMER_CLEAR_HOLD);
                ClearPlaylist(); InvalidateRect(hwnd,nullptr,TRUE);
            } else {
                InvalidateRect(hwnd,nullptr,TRUE);
            }
            return 0;
        }break;

    case WM_PAINT:{PAINTSTRUCT ps;HDC hdc=BeginPaint(hwnd,&ps);DefWindowProc(hwnd,msg,wParam,lParam);if(g_holdingClear){POINT cp=g_holdCursorPt;ScreenToClient(hwnd,&cp);DrawClearRing(hdc,cp);}EndPaint(hwnd,&ps);return 0;}

    case WM_COMMAND: {
        WORD id=LOWORD(wParam);
        switch(id){
        case IDC_BTN_PLAY: PlaylistStart(); break;
        case IDC_BTN_STOP: PlaylistStop(); break;
        case IDC_BTN_ADD: PlaylistAdd(); break;
        case IDC_BTN_REMOVE: PlaylistRemove(); break;
        case IDC_BTN_UP: PlaylistMoveUp(); break;
        case IDC_BTN_DOWN: PlaylistMoveDown(); break;
        case IDC_COMBO_BLEND: if(HIWORD(wParam)==CBN_SELCHANGE){g_blendMode=(int)SendMessageW(GetDlgItem(g_hwnd,IDC_COMBO_BLEND),CB_GETCURSEL,0,0);int ps=(int)SendMessageW(g_hPlaylist,LB_GETCURSEL,0,0);if(ps>=0&&ps<(int)g_playlist.size()){g_playlist[ps].blendMode=(BlendMode)g_blendMode;RefreshPlaylistUI();ListBox_SetCurSel(g_hPlaylist,ps);}} break;
        case 1133: {int effSel=SelectedEffectIndex();int plSel=(int)SendMessageW(g_hPlaylist,LB_GETCURSEL,0,0);
            if(effSel>=0&&effSel<g_effectCount&&plSel>=0&&plSel<(int)g_playlist.size()){
                g_playlist[plSel].name2=g_effects[effSel].name;
                g_playlist[plSel].blendMode=(BlendMode)g_blendMode;
                g_playlist[plSel].opacity=g_blendOpacity;
                if(g_blendMode==0){g_blendMode=2;g_playlist[plSel].blendMode=BM_Screen;SendMessageW(GetDlgItem(g_hwnd,IDC_COMBO_BLEND),CB_SETCURSEL,2,0);}
                RefreshPlaylistUI();ListBox_SetCurSel(g_hPlaylist,plSel);
                SetWindowTextW(g_hStatus,L"  已设上层效果");}
            else SetWindowTextW(g_hStatus,L"  请先选择左侧效果和播放链条目");}break;
        case 1134: {int plSel=(int)SendMessageW(g_hPlaylist,LB_GETCURSEL,0,0);
            if(plSel>=0&&plSel<(int)g_playlist.size()){g_playlist[plSel].name2.clear();RefreshPlaylistUI();ListBox_SetCurSel(g_hPlaylist,plSel);}
            g_blendMode=0;SendMessageW(GetDlgItem(g_hwnd,IDC_COMBO_BLEND),CB_SETCURSEL,0,0);SetWindowTextW(g_hStatus,L"  已清除上层");}break;
        case IDM_SETTINGS: ShowSettingsDialog(hwnd); break;
        default:
            if(id>=IDC_PRESET_BASE&&id<=IDC_PRESET_BASE+4){
                int s=id-IDC_PRESET_BASE;
                if(!g_presets[s].empty()) PresetLoad(s);
            }
            break;
        }
        return 0;
    }

    case WM_CONTEXTMENU: {
        HWND from=(HWND)wParam;int id=GetDlgCtrlID(from);
        if(id>=IDC_PRESET_BASE&&id<=IDC_PRESET_BASE+4){int s=id-IDC_PRESET_BASE;HMENU m=CreatePopupMenu();AppendMenuW(m,MF_STRING,1,L"追加到播放链");AppendMenuW(m,MF_STRING,2,L"覆盖保存");AppendMenuW(m,MF_STRING,3,L"删除");POINT pt;GetCursorPos(&pt);int c=TrackPopupMenu(m,TPM_RETURNCMD|TPM_NONOTIFY,pt.x,pt.y,0,hwnd,nullptr);DestroyMenu(m);if(c==1)PresetLoad(s);else if(c==2)PresetSave(s);else if(c==3){PresetDelete(s);UpdatePresetButtons();}}
        return 0;
    }

    case WM_HSCROLL: {
        int op=(int)SendMessageW(GetDlgItem(g_hwnd,IDC_TRACK_OPACITY),TBM_GETPOS,0,0);g_blendOpacity=(float)op/100.0f;int ps=(int)SendMessageW(g_hPlaylist,LB_GETCURSEL,0,0);if(ps>=0&&ps<(int)g_playlist.size()){g_playlist[ps].opacity=g_blendOpacity;}wchar_t buf[16];_snwprintf(buf,16,L"%d%%",op);SetWindowTextW(GetDlgItem(g_hwnd,IDC_STATIC_OPACITY),buf);}return 0;

    case WM_KEYDOWN: if(wParam==VK_ESCAPE)DestroyWindow(hwnd); return 0;
    case WM_SIZE: {RECT rc;GetClientRect(hwnd,&rc);SetWindowPos(g_hStatus,nullptr,0,rc.bottom-22,rc.right,22,SWP_NOZORDER);return 0;}
    case WM_DESTROY: PlaylistStop();KillTimer(hwnd,TIMER_INIT);KillTimer(hwnd,TIMER_MAIN);g_audio.Shutdown();PostQuitMessage(0);return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ===== 设置对话框 =====
#define IDD_SPEED      3003
#define IDD_SPEED_LBL  3004
#define IDD_BPM        3005
#define IDD_TAP        3006
#define IDD_OK         3007
#define IDD_BG         3008
#define IDD_CNT_MIN    3009
#define IDD_CNT_MAX    3010
#define IDD_OP_MIN     3011
#define IDD_OP_MAX     3012
#define IDD_RANDOM     3013
#define IDD_DUR_MIN    3014
#define IDD_DUR_MAX    3015
#define IDD_AUDIO_STR    3016
#define IDD_AUDIO_STR_LBL 3017
#define IDD_TOPMOST      3018

static void ApplyBackground(bool dark) {
    g_plBlackBg = dark;
    if (g_playerMode) InvalidateRect(g_hPlayer, nullptr, FALSE);
}

static int g_randMinCount = 3, g_randMaxCount = 10;
static float g_randMinOpacity = 0.3f, g_randMaxOpacity = 1.0f;
static int g_randMinDuration = 15, g_randMaxDuration = 45;

static void GenerateRandomPlaylist() {
    if (g_effectCount <= 0) return;
    if (g_randMaxCount < g_randMinCount) { int t=g_randMinCount; g_randMinCount=g_randMaxCount; g_randMaxCount=t; }
    if (g_randMaxOpacity < g_randMinOpacity) { float t=g_randMinOpacity; g_randMinOpacity=g_randMaxOpacity; g_randMaxOpacity=t; }
    if (g_randMaxDuration < g_randMinDuration) { int t=g_randMinDuration; g_randMinDuration=g_randMaxDuration; g_randMaxDuration=t; }
    int range = g_randMaxCount - g_randMinCount + 1;
    int count = g_randMinCount + (range > 1 ? rand() % range : 0);
    int drange = g_randMaxDuration - g_randMinDuration + 1;
    // 单 effect 不够成对时，无法保证每项都有上层 → 限制最大数量
    int maxPairs = (g_effectCount >= 2) ? (g_effectCount * (g_effectCount - 1)) : 0;
    if (maxPairs > 0 && count > maxPairs) count = maxPairs;
    // 【混层规则】只有 Normal / Layers 族的条目才配上层混层，且上层也只能取自这两族
    // —— 底层与上层都必须是这两个族之一。其余族（Metalheart / Cube / 3D）单独成条，name2 留空。
    // 判据用 EffIsMixable 而不是 !isMesh：家族与渲染方式是正交的，别把两者绑在一起。
    std::vector<int> normalIdx;
    for (int i = 0; i < g_effectCount; ++i) if (EffIsMixable(i)) normalIdx.push_back(i);
    const bool canPair = normalIdx.size() >= 2;   // 少于两个可混层效果就凑不出"上下层不同"的一对
    std::wstring prevBase, prevOver;
    for (int i = 0; i < count; ++i) {
        PlaylistItem item;
        // 底层 + 上层必不同，且与上一条目组合不重复（多次尝试）
        int bi = -1, ti = -1;
        for (int tries = 0; tries < 32; ++tries) {
            bi = rand() % g_effectCount;
            ti = -1;
            if (canPair && EffIsMixable(bi)) {
                ti = normalIdx[rand() % normalIdx.size()];
                if (ti == bi) continue;                   // 上下层必不同
            }
            // 相邻不重复：比的是 (底层, 上层) 整个组合；无混层时上层记为空
            if (g_effects[bi].name == prevBase &&
                (ti < 0 ? prevOver.empty() : g_effects[ti].name == prevOver)) continue;
            break;
        }
        if (bi < 0) bi = 0;
        if (ti == bi) ti = -1;                            // 兜底：绝不能自己叠自己
        item.name = g_effects[bi].name;
        if (ti >= 0) {
            item.name2 = g_effects[ti].name;
            item.blendMode = (BlendMode)(rand() % BM_COUNT);
        } else {
            item.name2.clear();                           // 非 Normal 底层：不带混层
            item.blendMode = BM_None;
        }
        prevBase = item.name; prevOver = item.name2;
        float r = (float)rand() / (float)RAND_MAX;
        float op = g_randMinOpacity + r * (g_randMaxOpacity - g_randMinOpacity);
        if (op < 0) op = 0; if (op > 1) op = 1;
        item.opacity = op;
        item.durationSec = g_randMinDuration + (drange > 1 ? rand() % drange : 0);
        g_playlist.push_back(item);
    }
    RefreshPlaylistUI();
    if (g_hStatus) SetWindowTextW(g_hStatus, L"  已随机生成播放链");
}

static LRESULT CALLBACK SettingsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HFONT f = CreateFontW(16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei");
        int y=14;
        CreateWindowW(L"STATIC",L"速度:",WS_CHILD|WS_VISIBLE|SS_RIGHT,10,y+4,80,22,hwnd,nullptr,nullptr,nullptr);
        HWND sp=CreateWindowW(TRACKBAR_CLASSW,nullptr,WS_CHILD|WS_VISIBLE|TBS_HORZ|TBS_NOTICKS,100,y,130,28,hwnd,(HMENU)IDD_SPEED,nullptr,nullptr);
        SendMessageW(sp,TBM_SETRANGE,TRUE,MAKELONG(10,1000)); SendMessageW(sp,TBM_SETPOS,TRUE,(int)(g_userSpeed*100.0f));
        HWND sl=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_LEFT,240,y+4,50,22,hwnd,(HMENU)IDD_SPEED_LBL,nullptr,nullptr);
        wchar_t sb[16]; _snwprintf(sb,16,L"%.1fx",g_userSpeed); SetWindowTextW(sl,sb);
        y+=40;
        CreateWindowW(L"STATIC",L"BPM:",WS_CHILD|WS_VISIBLE|SS_RIGHT,10,y+4,80,22,hwnd,nullptr,nullptr,nullptr);
        HWND bpm=CreateWindowW(L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER,100,y,60,24,hwnd,(HMENU)IDD_BPM,nullptr,nullptr);
        SendMessageW(bpm,WM_SETFONT,(WPARAM)f,TRUE); SendMessageW(bpm,EM_SETLIMITTEXT,5,0);
        wchar_t bb[16]; _snwprintf(bb,16,L"%.0f",g_bpm); SetWindowTextW(bpm,bb);
        HWND tap=CreateWindowW(L"BUTTON",L"Tap",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,170,y,50,24,hwnd,(HMENU)IDD_TAP,nullptr,nullptr);
        SendMessageW(tap,WM_SETFONT,(WPARAM)f,TRUE);
        y+=40;
        CreateWindowW(L"STATIC",L"背景:",WS_CHILD|WS_VISIBLE|SS_RIGHT,10,y+4,80,22,hwnd,nullptr,nullptr,nullptr);
        HWND bg=CreateWindowW(L"BUTTON",L"黑底",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,100,y+2,80,24,hwnd,(HMENU)IDD_BG,nullptr,nullptr);
        SendMessageW(bg,WM_SETFONT,(WPARAM)f,TRUE);
        SendMessageW(bg,BM_SETCHECK,g_plBlackBg?BST_CHECKED:BST_UNCHECKED,0);
        HWND topmost=CreateWindowW(L"BUTTON",L"窗口置顶",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,200,y+2,100,24,hwnd,(HMENU)IDD_TOPMOST,nullptr,nullptr);
        SendMessageW(topmost,WM_SETFONT,(WPARAM)f,TRUE);
        SendMessageW(topmost,BM_SETCHECK,g_plTopmost?BST_CHECKED:BST_UNCHECKED,0);
        y+=40;
        // 音频驱动全局强度（临时测试滑杆；定稿后移除）
        CreateWindowW(L"STATIC",L"音频强度:",WS_CHILD|WS_VISIBLE|SS_RIGHT,10,y+4,80,22,hwnd,nullptr,nullptr,nullptr);
        HWND ast=CreateWindowW(TRACKBAR_CLASSW,nullptr,WS_CHILD|WS_VISIBLE|TBS_HORZ|TBS_NOTICKS,100,y,130,28,hwnd,(HMENU)IDD_AUDIO_STR,nullptr,nullptr);
        SendMessageW(ast,WM_SETFONT,(WPARAM)f,TRUE);
        SendMessageW(ast,TBM_SETRANGE,TRUE,MAKELONG(0,300)); SendMessageW(ast,TBM_SETPOS,TRUE,(int)(g_audioStrength*100.0f));
        HWND al=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_LEFT,240,y+4,50,22,hwnd,(HMENU)IDD_AUDIO_STR_LBL,nullptr,nullptr);
        wchar_t ab[16]; _snwprintf(ab,16,L"%d%%",(int)(g_audioStrength*100.0f)); SetWindowTextW(al,ab);
        y+=40;
        CreateWindowW(L"STATIC",L"数量范围:",WS_CHILD|WS_VISIBLE|SS_RIGHT,10,y+4,80,22,hwnd,nullptr,nullptr,nullptr);
        HWND cmin=CreateWindowW(L"EDIT",L"3",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER,100,y,40,24,hwnd,(HMENU)IDD_CNT_MIN,nullptr,nullptr);
        SendMessageW(cmin,WM_SETFONT,(WPARAM)f,TRUE);
        CreateWindowW(L"STATIC",L"~",WS_CHILD|WS_VISIBLE|SS_CENTER,145,y+4,20,22,hwnd,nullptr,nullptr,nullptr);
        HWND cmax=CreateWindowW(L"EDIT",L"10",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER,170,y,40,24,hwnd,(HMENU)IDD_CNT_MAX,nullptr,nullptr);
        SendMessageW(cmax,WM_SETFONT,(WPARAM)f,TRUE);
        y+=40;
        CreateWindowW(L"STATIC",L"不透明度:",WS_CHILD|WS_VISIBLE|SS_RIGHT,10,y+4,80,22,hwnd,nullptr,nullptr,nullptr);
        HWND omin=CreateWindowW(L"EDIT",L"0.3",WS_CHILD|WS_VISIBLE|WS_BORDER,100,y,40,24,hwnd,(HMENU)IDD_OP_MIN,nullptr,nullptr);
        SendMessageW(omin,WM_SETFONT,(WPARAM)f,TRUE);
        CreateWindowW(L"STATIC",L"~",WS_CHILD|WS_VISIBLE|SS_CENTER,145,y+4,20,22,hwnd,nullptr,nullptr,nullptr);
        HWND omax=CreateWindowW(L"EDIT",L"1.0",WS_CHILD|WS_VISIBLE|WS_BORDER,170,y,40,24,hwnd,(HMENU)IDD_OP_MAX,nullptr,nullptr);
        SendMessageW(omax,WM_SETFONT,(WPARAM)f,TRUE);
        y+=40;
        CreateWindowW(L"STATIC",L"时长(秒):",WS_CHILD|WS_VISIBLE|SS_RIGHT,10,y+4,80,22,hwnd,nullptr,nullptr,nullptr);
        HWND dmin=CreateWindowW(L"EDIT",L"15",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER,100,y,40,24,hwnd,(HMENU)IDD_DUR_MIN,nullptr,nullptr);
        SendMessageW(dmin,WM_SETFONT,(WPARAM)f,TRUE);
        CreateWindowW(L"STATIC",L"~",WS_CHILD|WS_VISIBLE|SS_CENTER,145,y+4,20,22,hwnd,nullptr,nullptr,nullptr);
        HWND dmax=CreateWindowW(L"EDIT",L"45",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER,170,y,40,24,hwnd,(HMENU)IDD_DUR_MAX,nullptr,nullptr);
        SendMessageW(dmax,WM_SETFONT,(WPARAM)f,TRUE);
        y+=40;
        HWND rnd=CreateWindowW(L"BUTTON",L"随机生成播放链",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,10,y,150,30,hwnd,(HMENU)IDD_RANDOM,nullptr,nullptr);
        SendMessageW(rnd,WM_SETFONT,(WPARAM)f,TRUE);
        y+=40;
        HWND ok=CreateWindowW(L"BUTTON",L"确定",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,10,y,90,30,hwnd,(HMENU)IDD_OK,nullptr,nullptr);
        SendMessageW(ok,WM_SETFONT,(WPARAM)f,TRUE);
        DeleteObject(f);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDD_TAP: { static float lastTap=0; static int taps=0; float now=(float)GetTickCount()/1000.0f; if(taps>0){float dt=now-lastTap; if(dt>0.1f&&dt<5.0f){SetBPM(60.0f/dt); wchar_t b[16]; _snwprintf(b,16,L"%.0f",g_bpm); SetDlgItemTextW(hwnd,IDD_BPM,b);} } lastTap=now; taps++; } break;
        case IDD_BG: ApplyBackground(SendMessageW(GetDlgItem(hwnd,IDD_BG),BM_GETCHECK,0,0)==BST_CHECKED); break;
        case IDD_TOPMOST: {
            g_plTopmost = (SendMessageW(GetDlgItem(hwnd,IDD_TOPMOST),BM_GETCHECK,0,0)==BST_CHECKED);
            // 播放窗口已经开着就立刻生效，不用关掉重播
            if (g_hPlayer) SetWindowPos(g_hPlayer, g_plTopmost?HWND_TOPMOST:HWND_NOTOPMOST,
                                        0,0,0,0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
            break;
        }
        case IDD_RANDOM: {
            wchar_t buf[16];
            GetDlgItemTextW(hwnd,IDD_CNT_MIN,buf,16); int mn=_wtoi(buf);
            GetDlgItemTextW(hwnd,IDD_CNT_MAX,buf,16); int mx=_wtoi(buf);
            GetDlgItemTextW(hwnd,IDD_OP_MIN,buf,16); float omin=(float)_wtof(buf);
            GetDlgItemTextW(hwnd,IDD_OP_MAX,buf,16); float omax=(float)_wtof(buf);
            GetDlgItemTextW(hwnd,IDD_DUR_MIN,buf,16); int dmin=_wtoi(buf);
            GetDlgItemTextW(hwnd,IDD_DUR_MAX,buf,16); int dmax=_wtoi(buf);
            if (mn < 1) mn = 1; if (mx < mn) mx = mn;
            if (omin < 0) omin = 0; if (omax > 1) omax = 1; if (omax < omin) omax = omin;
            if (dmin < 1) dmin = 1; if (dmax < dmin) dmax = dmin;
            g_randMinCount = mn; g_randMaxCount = mx;
            g_randMinOpacity = omin; g_randMaxOpacity = omax;
            g_randMinDuration = dmin; g_randMaxDuration = dmax;
            GenerateRandomPlaylist();
            break;
        }
        case IDD_OK: case IDCANCEL: DestroyWindow(hwnd); return 0;
        case IDD_BPM: if(HIWORD(wParam)==EN_KILLFOCUS){wchar_t buf[16];GetDlgItemTextW(hwnd,IDD_BPM,buf,16);int v=_wtoi(buf);if(v>0)SetBPM((float)v);} break;
        }
        return 0;
    case WM_HSCROLL:
        if((HWND)lParam==GetDlgItem(hwnd,IDD_SPEED)){int pos=(int)SendMessageW(GetDlgItem(hwnd,IDD_SPEED),TBM_GETPOS,0,0);SetSpeed((float)pos/100.0f);wchar_t b[16];_snwprintf(b,16,L"%.1fx",g_userSpeed);SetDlgItemTextW(hwnd,IDD_SPEED_LBL,b);}
        else if((HWND)lParam==GetDlgItem(hwnd,IDD_AUDIO_STR)){int pos=(int)SendMessageW(GetDlgItem(hwnd,IDD_AUDIO_STR),TBM_GETPOS,0,0);g_audioStrength=(float)pos/100.0f;wchar_t b[16];_snwprintf(b,16,L"%d%%",pos);SetDlgItemTextW(hwnd,IDD_AUDIO_STR_LBL,b);}
        return 0;
    case WM_KEYDOWN: if(wParam==VK_ESCAPE){DestroyWindow(hwnd);return 0;} break;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    }
    return DefWindowProc(hwnd,msg,wParam,lParam);
}

static void ShowSettingsDialog(HWND parent) {
    static bool reg=false;
    if(!reg){WNDCLASSEXW wc={};wc.cbSize=sizeof(wc);wc.lpfnWndProc=SettingsDlgProc;wc.hInstance=GetModuleHandle(nullptr);wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);wc.lpszClassName=L"MTHSettingsDlg";RegisterClassExW(&wc);reg=true;}
    HWND dlg=CreateWindowExW(0,L"MTHSettingsDlg",L"设置",WS_POPUP|WS_CAPTION|WS_SYSMENU,100,100,310,470,parent,nullptr,GetModuleHandle(nullptr),nullptr);
    if(!dlg) return;
    RECT rp,rd; GetWindowRect(parent,&rp); GetWindowRect(dlg,&rd);
    int x=rp.left+(rp.right-rp.left)/2-(rd.right-rd.left)/2;
    int y=rp.top+(rp.bottom-rp.top)/2-(rd.bottom-rd.top)/2;
    SetWindowPos(dlg,nullptr,x,y,0,0,SWP_NOSIZE|SWP_NOZORDER);
    ShowWindow(dlg,SW_SHOW);
    EnableWindow(parent,FALSE);
    MSG msg;
    while(IsWindow(dlg) && GetMessageW(&msg,nullptr,0,0)){
        if(!IsDialogMessageW(dlg,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
    }
    EnableWindow(parent,TRUE);
    SetForegroundWindow(parent);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow) {
    // =======================================================================
    // 【声明 DPI 感知】—— 必须在创建任何窗口之前
    // =======================================================================
    // 之前没声明，而进程是 DPI【不感知】的：在 125% 缩放的显示器上，
    // GetClientRect 给的是【逻辑】尺寸（例如 512x350），而 OpenGL 的默认帧缓冲是
    // 【物理】尺寸（640x437）。app 却按 512x350 调 glViewport ——
    // 于是整个画面只画进帧缓冲的左下角一块，再被 Windows 缩放贴到窗口上：
    // **画面整体缩小约 0.8 倍、并锚在角落里**。
    // 这个坑对所有效果一视同仁地生效（不只 Function1），而且从画面上看只会以为
    // 是"相机/构图没调好"。诊断依据：把 VS 里的位置与投影【全部写死】再画，
    // 依然缩在同一个角落 —— 说明变换发生在 VS 之后（也就是视口/帧缓冲这一层）。
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES | ICC_TREEVIEW_CLASSES };
    InitCommonControlsEx(&icc);
    srand((unsigned)GetTickCount());
    SSDumpBuildInfo();   // 启动就写 out\ss_build.txt：一眼核对这份 exe 里的台阶几何

    // 提前加载 shader（需要临时 OpenGL 上下文加载 WGL 函数）
    {   WNDCLASSEXW wc={}; wc.cbSize=sizeof(wc); wc.lpfnWndProc=DefWindowProc; wc.hInstance=hInst; wc.lpszClassName=L"VizGLInit"; RegisterClassExW(&wc);
        HWND tmpHwnd = CreateWindowExW(0, L"VizGLInit", L"", 0, 0,0,1,1, nullptr,nullptr,hInst,nullptr);
        HDC tmpHdc = GetDC(tmpHwnd);
        PIXELFORMATDESCRIPTOR pfd={sizeof(pfd),1,PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER,PFD_TYPE_RGBA,32,0,0,0,0,0,0,0,0,0,0,0,0,0,24,8,0,PFD_MAIN_PLANE,0,0,0,0};
        SetPixelFormat(tmpHdc, ChoosePixelFormat(tmpHdc, &pfd), &pfd);
        HGLRC tmpRC = wglCreateContext(tmpHdc);
        if (tmpRC) {
            wglMakeCurrent(tmpHdc, tmpRC);
            LoadGL();
            LoadEffects();
            // 主初始化时也编译一次（播放前就能用）；这次失败不弹框，真正的报错在玩家窗口
            CompileBlendProgram(false);
            InitVAO();
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(tmpRC);
        }
        ReleaseDC(tmpHwnd, tmpHdc); DestroyWindow(tmpHwnd);
    }

    WNDCLASSEXW wc={}; wc.cbSize=sizeof(wc); wc.style=CS_HREDRAW|CS_VREDRAW; wc.lpfnWndProc=WndProc; wc.hInstance=hInst;
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.hbrBackground=CreateSolidBrush(RGB(20,20,35)); wc.lpszClassName=L"AudioVizGPU";
    RegisterClassExW(&wc);
    // 标题里带上编译时刻。这是踩了两次"我这边明明改了、你那边还是老样子"才加的：
    // 光看画面分不出你跑的是哪一份构建，而窗口标题一眼就能认。
    static const wchar_t* kBuildStamp = L"MAIN [GPU] — 音频可视化  (build "
        L"" __DATE__ L" " __TIME__ L")";
    HWND hwnd=CreateWindowExW(0,L"AudioVizGPU",kBuildStamp,WS_OVERLAPPEDWINDOW&~WS_MAXIMIZEBOX&~WS_THICKFRAME|WS_CLIPCHILDREN,
        CW_USEDEFAULT,CW_USEDEFAULT,920,750,nullptr,nullptr,hInst,nullptr);
    if(!hwnd)return 1;
    ShowWindow(hwnd,nShow);UpdateWindow(hwnd);

    MSG msg;while(GetMessageW(&msg,nullptr,0,0)){TranslateMessage(&msg);DispatchMessageW(&msg);}
    DeleteObject(wc.hbrBackground);
    return(int)msg.wParam;
}
