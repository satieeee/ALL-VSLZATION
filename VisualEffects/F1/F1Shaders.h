// ============================================================================
// F1Shaders
//
// Function1（Zindler conoid）着色器。离线台：_hittest/f1_render_test.cpp。
//
// 【为什么单独一个头】离线台要的是【实际跑的那一份】原文。以前靠 ExtractRaw()
// 从 main.cpp 里抓文本，台子因此依赖 main.cpp 的排版；改成 include 之后两边
// 拿到的是同一个变量，抄错/漏改这一类问题就不存在了。
//
// 【改了这个文件要重编离线台】—— 着色器文本是编进 exe 的。
// ============================================================================
#pragma once

static const char* kF1VS = R"(#version 330 core
layout(location = 0) in vec2 aUV;      // 几何趟：归一化的 (u,v) 参数，[0,1]
// 【全屏趟的 uv 在 location 1】—— g_vao 的 location 0 是 NDC 位置(±1)、location 1 才是 uv(0..1)。
// 原来直通里写的是 `vUV = aUV`（location 0），等于拿 NDC 当 uv 去采样场景纹理 ——
// 只有 [0,1] 那一段采得到，画面就是【永远缩在右上四分之一、且随窗口等比缩放】。
// Involute_1 那边直通写的是 `vUV = aCell`（它的 location 1），所以它是好的。
layout(location = 1) in vec2 aQuadUV;  // 全屏趟：quad 的 uv

uniform float uU, uV;                  // movement 给的 u / v 范围（比例，× 2π 之前的）
uniform float uPhi;                    // 相位 φ（弧度），公式里是 2π/3
uniform float uScale;
uniform float uCamYaw, uCamPitch, uCamDist;
uniform float uFocal;
uniform vec2  uResolution;
uniform float uExitTime;
uniform int   uPass;                   // 0=背景 1=几何+编码 2=亮部 3=模糊 4=合成

out vec3  vWorld;
out vec3  vNrm;
out float vT;                          // 配色参数 = u 的归一化值
out vec2  vUV;

const float PI  = 3.14159265358979;
const float TAU = 6.28318530717959;

// 【GLSL 没有 cosh/tanh】手写。v ≤ 2π 时 exp(2v) ≤ 2.9e5，不会溢出。
float f1cosh(float x){ return 0.5 * (exp(x) + exp(-x)); }
float f1tanh(float x){ float e = exp(2.0 * x); return (e - 1.0) / (e + 1.0); }

void f1surf(float un, float vn, out vec3 p) {
    float u = un * uU * TAU;
    float v = vn * uV * TAU;
    float A = 2.0 * v / PI - f1tanh(v);
    float B = 1.0 / f1cosh(v);
    p = vec3(uScale * cos(u) * A, uScale * cos(u + uPhi) * B, uScale * cos(u - uPhi) * B);
}

void main() {
    // 全屏那几趟：g_vao 给的是 NDC xy + uv，直通
    if (uPass != 1) {
        gl_Position = vec4(aUV.xy, 0.0, 1.0);
        vUV = aQuadUV;
        vWorld = vec3(aUV, 0.0);
        vNrm = vec3(0.0, 0.0, 1.0);
        vT = aUV.x;
        return;
    }

    vec3 p; f1surf(aUV.x, aUV.y, p);

    // 中心差分求法线。两端用 clamp —— 单边差分在边界会退化成零向量。
    const float e = 1e-3;
    vec3 a, b;
    f1surf(max(0.0, aUV.x - e), aUV.y, a);
    f1surf(min(1.0, aUV.x + e), aUV.y, b);
    vec3 du = b - a;
    f1surf(aUV.x, max(0.0, aUV.y - e), a);
    f1surf(aUV.x, min(1.0, aUV.y + e), b);
    vec3 dv = b - a;
    vec3 n = cross(du, dv);
    // 【退化要兜底】：入场最初 u 范围 = 0，整张曲面塌成一条线，du 恒为 0 → cross = 0 →
    // normalize(0) 是 NaN，整帧涂黑。必须给个默认法线。
    float nl = length(n);
    n = (nl > 1e-12) ? n / nl : vec3(0.0, 0.0, 1.0);

    // 相机：绕原点转两个角（相机在球面上），物体中心恒在原点
    vec3 q = p;
    float cy = cos(uCamYaw), sy = sin(uCamYaw);
    q.xz = mat2(cy, -sy, sy, cy) * q.xz;
    float cp = cos(uCamPitch), sp = sin(uCamPitch);
    q.yz = mat2(cp, -sp, sp, cp) * q.yz;
    n.xz = mat2(cy, -sy, sy, cy) * n.xz;
    n.yz = mat2(cp, -sp, sp, cp) * n.yz;

    vWorld = q;
    vNrm   = n;
    vT     = aUV.x;
    vUV    = aUV;

    vec3 v = vec3(q.x, q.y, q.z - uCamDist);
    // 出场：整体朝画面外侧飞出（在视图空间里加，所以永远朝右飞，与相机角度无关）
    if (uExitTime < 0.6) v.x += pow(clamp(uExitTime / 0.6, 0.0, 1.0), 2.0) * 16.0;

    float d = max(-v.z, 0.35);
    float aspect = uResolution.x / uResolution.y;
    gl_Position = vec4(v.x * uFocal / aspect, v.y * uFocal, 0.0, d);
}
)";

static const char* kF1FS = R"(#version 330 core
in vec3  vWorld;
in vec3  vNrm;
in float vT;
in vec2  vUV;
out vec4 fragColor;

// 【必须是 vec4】——本项目的 GL loader 没加载 glUniform3f/3fv，颜色一律走 glUniform4fv 上传。
// 声明成 vec3 的话类型对不上 → GL_INVALID_OPERATION → 值根本到不了着色器、恒为 0：
// 画面表现是只剩边缘微光、整张曲面发闷，而几何/法线/光照项其实全都正常（我为此绕了好几圈）。
uniform vec4  uColA, uColB;       // 双色渐变两端（只取 .rgb）
uniform vec3  uEye;
uniform float uOpacity;
uniform float uTime;
uniform int   uDarkBg;         // 【必须是 int】——上传用的是 glUniform1i。声明成 float 的话
                               // GL 报 INVALID_OPERATION、值写不进去恒为 0，浅色模式的取反就
                               // 永远生效（表现为"背景黑/白按键对本效果没用"，且过渡起手会跳色）。
                               // kVXFS/kSSFS/粒子那几处早就是 int，只有这里漏了。
uniform int   uPass;
// 泛光/合成
uniform sampler2D uTexScene, uTexBloom;
uniform vec2  uTexel, uBlurDir;
uniform float uBloomAmt, uBloomThresh, uExposure, uSat;
uniform vec2  uResolution;
// 跨效果 morph 协议（与 Psy/GF/VX/SS 同一份三个 uniform）
uniform int   uEncode;
uniform sampler2D uOldEncoded;
uniform float uMorphBlend;

float f1hash(vec2 p){ vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }

/// 交给搭档去上色的标量场：本效果的结构就是 u 参数
float f1Pattern() { return clamp(vT * 2.0 - 1.0, -1.0, 1.0); }

/// 程序化深空背景（同 Involute / Involute_1 的做法）
vec3 f1Background(vec2 uv) {
    float d = length(uv - 0.5);
    vec3 col = mix(vec3(0.050, 0.056, 0.104), vec3(0.004, 0.005, 0.012),
                   smoothstep(0.02, 0.62, d));
    for (int L = 0; L < 2; ++L) {
        float fl = float(L);
        vec2 g = uv * (170.0 + fl * 190.0);
        vec2 id = floor(g), f = fract(g) - 0.5;
        float h = f1hash(id + fl * 37.1);
        float d2 = length(f - vec2(f1hash(id + 1.7), f1hash(id + 3.3)) + 0.5);
        float st = smoothstep(0.055 - fl * 0.02, 0.0, d2) * step(0.985 - fl * 0.004, h);
        st *= 0.55 + 0.45 * sin(uTime * 1.6 + h * 60.0);
        col += vec3(0.72, 0.80, 1.0) * st * (0.9 - fl * 0.3);
    }
    return col;
}

// 外缘粗微光的两个旋钮：
//   K_F1_RIM  —— 越小越【粗】（1.0 ≈ 整条边往里铺开一大片；3.0 是一条贴边的细线）
//   K_F1_RIM_I—— 强度
const float K_F1_RIM   = 1.30;
const float K_F1_RIM_I = 0.50;

void main() {
    if (uPass == 0) {                       // 背景（编码趟同样要写，理由见 VX 那处说明）
        vec3 bg = f1Background(vUV);
        fragColor = (uEncode > 0) ? vec4(0.5, bg) : vec4(bg, 1.0);
        return;
    }
    if (uPass == 2) {
        vec3 c = texture(uTexScene, vUV).rgb;
        float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
        fragColor = vec4(c * max(0.0, l - uBloomThresh) / max(l, 1e-4), 1.0);
        return;
    }
    if (uPass == 3) {
        vec2 t = uBlurDir * uTexel;
        vec3 s = texture(uTexBloom, vUV).rgb * 0.2270270;
        s += (texture(uTexBloom, vUV + t*1.3846).rgb + texture(uTexBloom, vUV - t*1.3846).rgb) * 0.3162162;
        s += (texture(uTexBloom, vUV + t*3.2308).rgb + texture(uTexBloom, vUV - t*3.2308).rgb) * 0.0702703;
        fragColor = vec4(s, 1.0);
        return;
    }
    if (uPass == 4) {
        vec3 col = texture(uTexScene, vUV).rgb + texture(uTexBloom, vUV).rgb * uBloomAmt;
        col *= uExposure;
        float lum0 = max(dot(col, vec3(0.299, 0.587, 0.114)), 1e-4);
        float lumT = clamp((lum0*(2.51*lum0+0.03))/(lum0*(2.43*lum0+0.59)+0.14), 0.0, 1.0);
        col = clamp(col * (lumT / lum0), 0.0, 1.0);
        float lum = dot(col, vec3(0.299, 0.587, 0.114));
        col = clamp(mix(vec3(lum), col, uSat), 0.0, 1.0);
        vec2 c = vUV - 0.5;
        col *= 1.0 - dot(c, c) * 0.75;
        if (uDarkBg == 0) col = vec3(1.0) - col;
        if (uMorphBlend > 0.0 && uMorphBlend < 1.0) {
            float sb = uMorphBlend * uMorphBlend * (3.0 - 2.0 * uMorphBlend);
            vec3 oldCol = texture(uOldEncoded, vUV).gba;
            col = mix(oldCol, col, sb);
        }
        fragColor = vec4(col, uOpacity);
        return;
    }

    // ================= 几何趟 / 编码趟 =================
    vec3 n  = normalize(vNrm);
    vec3 V  = normalize(uEye - vWorld);
    // 【双面受光】：这张曲面正反两面都看得到，背面靠翻法线才不至于死黑
    if (dot(n, V) < 0.0) n = -n;
    // 【光源要在相机这一侧】。项目里 VX 那套写的是 z 为负的 L1/L2 —— 那是给
    // "相机在原点朝 +z 看"的 Involute 用的；本效果（以及 HTML 预览）相机朝 −z 看，
    // 照抄过来 `max(dot(n,L),0)` 就恒为 0，只剩 0.12 的环境光，整张曲面发闷发黑（实测过）。
    vec3 L1 = normalize(vec3(-0.45, 0.62, 0.64));
    vec3 L2 = normalize(vec3( 0.70,-0.34, 0.63));
    float lam = max(dot(n, L1), 0.0) * 0.75 + max(dot(n, L2), 0.0) * 0.45;
    float sh  = 0.12 + lam;

    vec3 col = mix(uColA.rgb, uColB.rgb, clamp(vT, 0.0, 1.0));
    col *= sh;
    // 边缘微光：法线越背离视线越亮一点，帮读清号角的起伏
    // ---- 外缘粗微光 ----
    // 【Fresnel 而不是几何边】—— 用"法线背离视线"的程度取边缘，好处是不管相机转到哪，
    // 亮的永远是这个形状【当前的外轮廓】，在一条固定的参数边上取是做不到的。
    // 【多粗】由指数 K 定：K 越大越贴边（越细）、越小越往面里铺开（越粗）。
    //   原来是 3.0 → 一条很细的亮线；用户要"粗"，压到 K_RIM。
    // 拿饱和度低一点的白调色，避免把配色盖掉。
    float fres = 1.0 - max(dot(n, V), 0.0);
    float rim  = pow(clamp(fres, 0.0, 1.0), K_F1_RIM);
    vec3  rimCol = mix(mix(uColA.rgb, uColB.rgb, 0.5), vec3(1.0), 0.6);
    col += rimCol * (rim * K_F1_RIM_I);

    if (uEncode > 0) {
        fragColor = vec4(f1Pattern() * 0.5 + 0.5, col);
        return;
    }
    fragColor = vec4(col, 1.0);
}
)";
