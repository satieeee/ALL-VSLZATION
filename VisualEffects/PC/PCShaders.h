// ============================================================================
// PCShaders
//
// StellarVortex 点精灵粒子着色器。离线台：_hittest/particle_render_test.cpp。
//
// 【为什么单独一个头】离线台要的是【实际跑的那一份】原文。以前靠 ExtractRaw()
// 从 main.cpp 里抓文本，台子因此依赖 main.cpp 的排版；改成 include 之后两边
// 拿到的是同一个变量，抄错/漏改这一类问题就不存在了。
//
// 【改了这个文件要重编离线台】—— 着色器文本是编进 exe 的。
// ============================================================================
#pragma once

static const char* kParticleVS = R"(#version 330 core
// StellarVortex 顶点着色器：粒子位置全部由 gl_VertexID 推出，不读任何属性。
layout(location = 0) in vec2 aP;   // 只有背景/后处理两趟（g_vao）用；粒子那趟是空 VAO

uniform sampler2D uLut;      // 曲线 LUT：x 轴 = 曲线参数 u，一行一条曲线
uniform float uLutW;
uniform float uCnt[8];       // 每曲线点数（== kPCCnt）
uniform float uTime;         // 效果自己的时间（换效果时归零，所以相位天然同步）
uniform float uExitTime;     // 出场计时（同其它 mesh 效果：<0.6 时整体沿 +x 飞出）
uniform float uCount;        // 粒子数
uniform float uOpacity;      // 播放链给的不透明度（交叉淡入用）
uniform float uCurvePos;     // 曲线循环位置：整数部分=起点曲线，小数部分=过渡进度（CPU 推进）
uniform float uCamYaw;       // 相机朝向（每次鼓点随机走一步，CPU 缓动后传进来）
uniform float uCamPitch;
uniform float uCamDist;      // 相机距离（构造中心恒在视线原点 → 永远看着中心）
uniform vec2  uResolution;
uniform int   uIsBg;         // 0=粒子 1=背景 2=后处理
uniform vec4  uG[4];         // 当前调色板的四个色标。用 vec4 只是为了让上传走
                             //   glUniform4fv —— 本项目的 GL loader 没加载 glUniform3f/3fv。

out vec3  vCol;
out float vAlpha;

const int   kCurveN = 7;     // 必须 == kPCCurveN
const float kLutH   = 8.0;   // 必须 == kPCLutH

// ---- 参数：对应用户在 particle_curves.html 控制面板里调定的值（2026-09-12）----
//      改这里等价于拖动 HTML 里的同名滑块。
//      未实现的两项：色散(ca .32) 需要读回帧缓冲；流星(meteorRate 0) 用户已关掉。
const float PC_SIZE       = 0.040;   // 粒子尺寸
const float PC_GLOW       = 0.96;    // 辉光强度
const float PC_OPACITY    = 0.32;    // 不透明度
const float PC_DEPTH      = 0.38;    // 三维深度
const float PC_WAVE       = 4.00;    // 螺旋波数
const float PC_WAVE_SPD   = -1.52;   // 波速
const float PC_RADIUS     = 0.044;   // 管径（厚度）
const float PC_TWIST      = -1.18;   // 静态扭转
const float PC_VORTEX     = -0.86;   // 涡旋强度
const float PC_JITTER     = 0.03;    // 微颤
const float PC_SCATTER    = 1.16;    // 爆散幅度
const float PC_HUE_SHIFT  = 0.0;     // 色调偏移
const float PC_FOV        = 40.0;    // 视场角（度）
// 相机距离/俯仰/自转都不在这里了 —— 由 CPU 的 uCamDist/uCamPitch/uCamYaw 驱动（鼓点随机换视角）
// 曝光(1.28)不在这里 —— 挪到片元着色器的合成趟（全画面统一乘一次，同 HTML 版顺序）
// 曲线切换的节拍也不在这里 —— 由 CPU 推进 uCurvePos（鼓点是主信号，见 main.cpp 的 PLUpdate）

float h11(float p){ p = fract(p*0.1031); p *= p+33.33; p *= p+p; return fract(p); }
vec2  h22(float p){
    vec3 q = fract(vec3(p)*vec3(0.1031, 0.1030, 0.0973));
    q += dot(q, q.yzx + 33.33);
    return fract((q.xx + q.yz)*q.zy);
}
vec3 grad(float t){
    float s = fract(t)*3.0;
    if (s < 1.0) return mix(uG[0].rgb, uG[1].rgb, s);
    if (s < 2.0) return mix(uG[1].rgb, uG[2].rgb, s-1.0);
    return mix(uG[2].rgb, uG[3].rgb, s-2.0);
}
// 第 c 条曲线上参数 u 处的点：NEAREST 取相邻两个 LUT 点，再手写线性插值
vec2 curveAt(float c, float u){
    float n  = uCnt[int(c + 0.5)];
    float x  = fract(u)*n;
    float i0 = floor(x);
    float f  = x - i0;
    float v  = (c + 0.5)/kLutH;
    vec2 a = texture(uLut, vec2((i0 + 0.5)/uLutW, v)).xy;
    vec2 b = texture(uLut, vec2((mod(i0 + 1.0, n) + 0.5)/uLutW, v)).xy;
    return mix(a, b, f);
}

void main(){
    if (uIsBg != 0) {
        gl_Position = vec4(aP, 0.0, 1.0); gl_PointSize = 1.0;
        vCol = vec3(0.0); vAlpha = 0.0; return;
    }

    float fi = float(gl_VertexID);
    float u  = (fi + 0.5)/uCount;          // 沿曲线的等弧长参数

    // ---- 曲线循环：位置由 CPU 推进，鼓点让它往前走（hit 就是形态变换的信号）----
    // 整数部分 = 起点曲线，小数部分 m = 过渡进度。位置连续 → 鼓点再密也不会"重启"，
    // 只是持续往下游走；鼓点稀疏时 m 会精确落到 0，干净地停在某一条曲线上。
    float cA    = mod(floor(uCurvePos), float(kCurveN));
    float cB    = mod(floor(uCurvePos) + 1.0, float(kCurveN));
    float m     = fract(uCurvePos);
    float blend = m*m*m*(m*(m*6.0 - 15.0) + 10.0);            // smootherstep
    // 爆散包络取 sin^2：两侧保形，只有正中间炸开 —— 正好盖住两条曲线笛卡尔插值
    // 必然产生的"乱麻"中间态（HTML 版踩过这个坑，指数从 1.2 提到 2 就是为这个）
    float burst = pow(sin(3.14159265*m), 2.0);

    vec3 p = vec3(mix(curveAt(cA, u), curveAt(cB, u), blend), 0.0);

    // 抬升成 3D 螺旋带
    float ring = u*6.28318530718;
    p.z = PC_DEPTH*sin(ring*PC_WAVE + uTime*PC_WAVE_SPD);

    // 管状体积：绕曲线散开一点，形成有厚度的星尘
    vec2 rr = h22(fi*1.7 + 11.3) - 0.5;
    p.xy += rr*PC_RADIUS;
    p.z  += (h11(fi*2.9) - 0.5)*PC_RADIUS*1.6;

    // 静止时也活着
    float ph = fi*0.37;
    p.xy += vec2(sin(uTime*1.7 + ph), cos(uTime*1.3 + ph*1.7))*PC_JITTER;

    // 混沌爆散：方向由粒子哈希决定
    if (burst > 0.0001) {
        vec2 d = normalize(h22(fi*3.7 + 5.1) - 0.5 + 1e-4);
        float amp = burst*PC_SCATTER*(0.35 + h11(fi*5.3));
        p.xy += d*amp;
        p.z  += (h11(fi*7.9) - 0.5)*amp*2.2;
    }

    // 涡旋：扭转角随半径变化 → 星系式缠绕
    float r  = length(p.xy);
    float tw = PC_TWIST*(r + p.z*0.6) + PC_VORTEX*(0.35 + r)*sin(uTime*0.25);
    float ct = cos(tw), st = sin(tw);
    p.xz = mat2(ct, -st, st, ct)*p.xz;

    // 出场：整体沿 +x 飞出（与 Cubeeffect3 同一套 g_plExitT 语义）
    if (uExitTime < 0.6) p.x += pow(clamp(uExitTime/0.6, 0.0, 1.0), 2.0)*6.0;

    // 相机：绕原点转两个角（= 相机在球面上跑），再沿 z 推远距离。
    // 物体中心恒在原点、视线恒沿 -z → 【视角中心永远是这个构造的中心】。
    float cy = cos(uCamYaw), sy = sin(uCamYaw);
    p.xz = mat2(cy, -sy, sy, cy)*p.xz;
    float cp = cos(uCamPitch), sp = sin(uCamPitch);
    p.yz = mat2(cp, -sp, sp, cp)*p.yz;

    // 【这里原来写着 `p.y - uCamY`】—— 那个 uCamY 从来没有声明过（本文件的 uniform 只有
    // uCamYaw/uCamPitch/uCamDist），CPU 侧也没有这个量（粒子相机只有 g_pcCamYaw/Pitch/Dist），
    // 而且"卷起时把相机挪到管轴上"是 Involute_1 / Function1 的机制，粒子这边没有卷起。
    // 后果：顶点着色器编不过 → 整个 StellarVortex 渲染不出任何东西（错误只进了日志）。
    // 现在与那两个兄弟效果保持同一形式（SSShaders.h:290 / F1Shaders.h:93 都是 `q.z - uCamDist`）。
    vec3 v = vec3(p.x, p.y, p.z - uCamDist);
    float dist   = max(-v.z, 0.35);
    float aspect = uResolution.x/uResolution.y;
    float focal  = 1.0/tan(PC_FOV*3.14159265/360.0);

    gl_Position  = vec4(v.x*focal/aspect, v.y*focal, 0.0, dist);
    gl_PointSize = clamp(PC_SIZE*uResolution.y*0.5/dist, 0.6, 42.0)*(0.65 + 0.7*h11(fi*9.1));

    // 空气透视：远处变暗去饱和
    float fade = clamp((uCamDist + 1.9 - dist)/3.2, 0.0, 1.0);
    vec3 col = grad(u*0.72 + fade*0.32 + uTime*0.018 + PC_HUE_SHIFT);
    col = mix(col*0.40, col, fade);
    col *= (0.72 + 0.55*h11(fi*13.7));

    vCol   = col*PC_GLOW;   // 曝光不在这一步 —— 挪到合成趟统一乘（同 HTML 版顺序）
    vAlpha = uOpacity*PC_OPACITY*mix(0.30, 1.0, fade);
}
)";

static const char* kParticleFS = R"(#version 330 core
in vec3  vCol;
in float vAlpha;
out vec4 fragColor;

uniform int   uIsBg;
uniform vec2  uResolution;
uniform float uTime;
uniform float uOpacity;
uniform float uCamYaw;       // 星空视差用（与 VS 同源，两 stage 共用同一个 uniform）
uniform float uCamPitch;
uniform int   uDarkBg;       // 1=深色（默认外观） 0=浅色。浅色 = 深色的【严格负片】
// 泛光管线
uniform sampler2D uTexScene; // 全分辨率场景（背景+粒子，RGBA16F）
uniform sampler2D uTexBloom; // 半分辨率泛光（模糊后）
uniform vec2  uTexel;        // 1/本趟目标尺寸
uniform vec2  uBlurDir;      // 分离式高斯的方向：(1,0) 横 / (0,1) 纵
uniform float uBloomAmt;     // 合成时的泛光权重
uniform float uBloomThresh;  // 亮部提取阈值
uniform float uExposure;     // 曝光（在合成趟统一乘，和 HTML 的合成顺序一致）

const float PC_NEBULA   = 0.22;      // 星云强度
const float PC_STAR     = 2.00;      // 星密度
const float PC_SCAN     = 0.24;      // 扫描线
const float PC_GRAIN    = 0.26;      // 胶片颗粒
// 注意：相机的 yaw/pitch 是 uniform（鼓点随机驱动），星空视差直接用 uCamYaw/uCamPitch
// —— 星场跟着视角一起动，才有"在空间里穿行"的感觉。
// 曝光 PC_EXPOSURE 不在这个 stage 了 —— 挪到合成趟（全画面统一乘一次）。

float hash21(vec2 p){ p = fract(p*vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x*p.y); }
// 注意：必须重新声明一个 vec3 —— 不能对参数 p(vec2) 直接做 p*vec3(...) 或取 p.yzx。
// （这正是 HTML 版那个让整页白屏的 bug：vec2 上没有 z 分量，GLSL 编译期硬错误）
vec3 hash23(vec2 p){
    vec3 q = fract(vec3(p.xy, p.x + p.y)*vec3(123.34, 456.21, 789.12));
    q += dot(q, q.yzx + 45.32);
    return fract((q.xxy + q.yzz)*q.zyx);
}
float vnoise(vec2 p){
    vec2 i = floor(p), f = fract(p);
    f = f*f*(3.0 - 2.0*f);
    float a = hash21(i), b = hash21(i + vec2(1,0));
    float c = hash21(i + vec2(0,1)), d = hash21(i + vec2(1,1));
    return mix(mix(a,b,f.x), mix(c,d,f.x), f.y);
}
float fbm(vec2 p){
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 5; ++i) { v += a*vnoise(p); p = p*2.03 + 17.1; a *= 0.5; }
    return v;
}
// ACES 电影级色调映射。它在中间调是【抬亮】的（0.2→0.30、0.5→0.62），
// 所以这一趟本身就贡献了相当一部分"看起来更亮"——HTML 版也走的是这条曲线。
vec3 aces(vec3 x){
    return clamp((x*(2.51*x + 0.03))/(x*(2.43*x + 0.59) + 0.14), 0.0, 1.0);
}

void main(){
    // ---- ③ 亮部提取：取场景里的高光喂给半分辨率模糊（渲染进 blA）----
    if (uIsBg == 2) {
        vec2 uv = gl_FragCoord.xy/uResolution;
        vec3 c = texture(uTexScene, uv).rgb;
        float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
        fragColor = vec4(c*max(0.0, l - uBloomThresh)/max(l, 1e-4), 1.0);
        return;
    }
    // ---- ④ 分离式高斯（横/纵由 uBlurDir 决定，在 blA/blB 之间 ping-pong）----
    if (uIsBg == 3) {
        vec2 uv = gl_FragCoord.xy/uResolution;
        vec2 t  = uBlurDir*uTexel;
        vec3 s  = texture(uTexBloom, uv).rgb*0.2270270;
        s += (texture(uTexBloom, uv + t*1.3846).rgb + texture(uTexBloom, uv - t*1.3846).rgb)*0.3162162;
        s += (texture(uTexBloom, uv + t*3.2308).rgb + texture(uTexBloom, uv - t*3.2308).rgb)*0.0702703;
        fragColor = vec4(s, 1.0);
        return;
    }
    // ---- ⑤ 合成上屏：场景 + 泛光 → 曝光 → ACES → 暗角/扫描线/颗粒 ----
    // 顺序与 HTML 版的合成趟一致 —— 泛光必须在曝光/色调映射【之前】加进来，
    // 否则高光会被压扁，看起来就是"没泛光"。
    if (uIsBg == 4) {
        vec2 uv = gl_FragCoord.xy/uResolution;
        vec3 col = texture(uTexScene, uv).rgb + texture(uTexBloom, uv).rgb*uBloomAmt;
        col *= uExposure;
        col = aces(col);

        vec2 c = uv - 0.5;
        col *= 1.0 - dot(c, c)*0.85;                                  // 暗角
        col *= 1.0 - PC_SCAN*0.18*(0.5 + 0.5*sin(gl_FragCoord.y*3.14159265));
        float g = fract(sin(dot(gl_FragCoord.xy + uTime*13.0, vec2(12.9898, 78.233)))*43758.5453);
        col *= 1.0 - g*PC_GRAIN*0.14;                                 // 颗粒

        // 浅色模式 = 深色的【严格负片】，与 Cubeeffect3 的约定一致
        //   （那边是把每个颜色常数取 1-x；这边画面完全由这一趟合成，
        //     所以在【整帧成品的最后一步】整体取反，等价且不会漏掉任何一项）。
        // 注意放在暗角/扫描线/颗粒【之后】：负片就是逐像素反相，
        // 暗角变亮角、暗颗粒变亮颗粒，这才是严格的负片关系。
        if (uDarkBg == 0) col = vec3(1.0) - col;

        // alpha 用播放链给的不透明度 → 交叉淡入时叠在旧效果上，而不是把它一刀切掉
        fragColor = vec4(col, uOpacity);
        return;
    }

    // ---- ① 背景：星云 + 银河带 + 三层视差星场 ----
    if (uIsBg == 1) {
        float t = uTime;
        // 相机自转带来的视差（星空无限滚动 → 像在星际间穿行）
        vec2 par = vec2(-uCamYaw*0.055, -uCamPitch*0.06);
        vec2 uv = (gl_FragCoord.xy - 0.5*uResolution)/uResolution.y + par;

        float n1 = fbm(uv*1.6 + t*0.008);
        float n2 = fbm(uv*3.1 - t*0.012 + 7.3);
        vec3 neb = mix(vec3(0.012,0.006,0.035), vec3(0.09,0.03,0.22), n1);
        neb += vec3(0.02,0.10,0.22)*pow(n2, 2.0);
        neb += vec3(0.18,0.03,0.16)*pow(max(n1 - n2, 0.0), 2.0);

        float bd   = dot(uv, vec2(-sin(0.42), cos(0.42)));
        float band = exp(-bd*bd*11.0);
        float bn   = fbm(uv*2.2 + vec2(0.0, bd*3.0));
        vec3  gal  = mix(vec3(0.35,0.42,0.75), vec3(0.85,0.75,0.95), bn)*band*(0.10 + 0.30*bn);

        vec3 col = (neb + gal)*PC_NEBULA + vec3(0.004,0.005,0.012);

        for (int L = 0; L < 3; ++L) {
            float fl = float(L);
            vec2  g  = uv*(40.0 + fl*70.0);
            vec2  id = floor(g), fr = fract(g) - 0.5;
            vec3  h  = hash23(id + fl*31.7);
            float d2 = length(fr - (h.xy - 0.5)*0.75);
            float present = step(0.90 - fl*0.02, h.z);
            float sz = (0.030 - fl*0.007)*(0.5 + h.x);
            float st = smoothstep(sz, 0.0, d2)*present;
            st *= 0.55 + 0.45*sin(t*(1.5 + h.y*3.0) + h.z*62.0);      // 闪烁
            col += mix(vec3(0.75,0.85,1.0), vec3(1.0,0.85,0.85), h.x)*st*(0.85 - fl*0.22)*PC_STAR;
        }
        // 背景趟现在渲染进【离屏场景缓冲】，所以是纯覆盖（alpha=1）；
        // 交叉淡入要的不透明度在合成趟统一处理。
        fragColor = vec4(col, 1.0);
        return;
    }

    // ---- ② 点精灵：实心核 + 柔光晕。真正的辉光交给后面的泛光趟 ----
    // 注意这里【没有 discard】：圆外只要让 t=0、alpha 自然归零即可。
    // 之前用 discard 裁圆形，除了多余（alpha 本来就到 0），还有个副作用 ——
    // discard 允许硬件重排片元，而加法混合对累加顺序敏感（16 位浮点尤甚），
    // 结果是同一帧连渲两次，约 0.7% 的像素会差（最多 66/255），高光核心上表现为闪烁。
    // 去掉 discard 之后渲染变确定（离线台有对照实验守着）。
    float rr = length(gl_PointCoord - 0.5)*2.0;
    float t  = max(1.0 - rr, 0.0);          // 夹住：圆外是 0，不产生负数的 pow
    float core = pow(t, 3.2);
    float halo = pow(t, 0.7)*0.22;
    fragColor = vec4(vCol, (core + halo)*vAlpha);
}
)";
