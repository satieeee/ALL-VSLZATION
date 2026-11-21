// ============================================================================
// Shardstorm 的着色器原文（kXSVS / kXSFS）
//
// 【和 Filigree 的关系】渲染管线（背景 → 几何 → 亮部 → 两轮模糊 → 合成）与
// Filigree 同一套结构，但几何与运动模型完全不同：那里是静态的隧道+细条，
// 这里是几千片各自公转/自转的薄板。
//
// 【改了这个文件要重编离线台】—— 着色器文本是编进 exe 的。
// ============================================================================
#pragma once

static const char* kXSVS = R"(#version 330 core
layout(location = 0) in vec3  aPos;    // 碎片内的相对位置（轨道平面内的偏移）
layout(location = 1) in vec2  aUV;     // 板面局部坐标（做边缘高光）
layout(location = 2) in vec3  aNrm;    // 板面法线（前后两面符号相反）
layout(location = 3) in float aId;     // 第几片
layout(location = 4) in float aPhase;  // 轨道相位
layout(location = 5) in float aOmega;  // 公转角速度（rad/s，可正可负）
layout(location = 6) in float aRadius; // 轨道半径
layout(location = 7) in float aElev;   // 沿隧道轴的基准位置
layout(location = 8) in float aSize;   // 尺寸倍率
layout(location = 9) in float aHue;    // 色相基准
layout(location = 10) in float aAlpha;

uniform int   uPass;        // 0=背景 1=几何 2=亮部 3=模糊 4=合成
uniform float uTime;
uniform float uSpin;        // 自转速度倍率（外部可压可放）
uniform float uOrbit;       // 公转速度倍率 —— 【变化幅度就是它调出来的】
uniform float uSizeMul;     // 整体尺寸倍率
uniform float uSpread;      // 轨道半径倍率（收拢/散开）
uniform float uBandSpeed;   // 沿隧道轴的流动速度（fraction of span/s）
uniform vec2  uResolution;
uniform float uFocal;
uniform float uNear, uSpan;
uniform float uFadeIn, uFadeFull;
uniform vec4  uCamR0, uCamR1, uCamR2;
uniform vec4  uCamT;

out vec3  vWorld;
out vec3  vNrm;
out vec2  vUV;
out float vFog;
out float vId;
out float vHue;
out float vSpinPh;
out float vAlpha;

void main() {
    // ---- 全屏趟：直通（uv 取 location 1 —— location 0 是 NDC）----
    if (uPass != 1) {
        gl_Position = vec4(aPos.xy, 0.0, 1.0);
        vUV = aUV; vWorld = vec3(aPos.xy, 0.0); vNrm = vec3(0.0, 0.0, 1.0);
        vFog = 1.0; vId = 0.0; vHue = 0.0; vSpinPh = 0.0; vAlpha = 1.0;
        return;
    }

    // ---- 公转：角 = 相位 + 角速度 × 时间 × 倍率 ----
    // 【每片自己的 omega 已经烘在顶点里】—— 差速旋转（内圈快外圈慢）+ 逐片随机，
    // 所以这不是"一块盘在转"，而是几千片各转各的。
    // 【速度倍率 uOrbit 是"变化幅度"的总闸】：它决定每帧位移多少。
    //   实测：要让画面每像素平均变化 ≥0.09，每帧位移必须【大于碎片自身的尺寸】——
    //   也就是"这一帧在这里的碎片，下一帧已经飞到别处"，逐像素差才会大。
    float ang = aPhase * 6.2831853 + aOmega * uTime * uOrbit;

    // 公转轨道平面：绕 z 轴转（平面本身再按 id 轻微倾斜，避免所有碎片共面）
    float tilt = (fract(aId * 0.6180339) - 0.5) * 0.45;
    float ca = cos(ang), sa = sin(ang);
    float ct = cos(tilt), st = sin(tilt);

    // 轨道上的基点
    float r = aRadius * uSpread;
    vec3 base = vec3(ca * r, sa * r * ct, sa * r * st);
    // 碎片在这个点上的局部基：切向 / 径向 / 法向
    vec3 T = vec3(-sa, ca * ct, ca * st);
    vec3 R = vec3(ca, sa * ct, sa * st);
    vec3 N = normalize(cross(T, R));

    // ---- 自转：板子绕自己的法线转（用 u/v 平面上的旋转实现）----
    float sp = fract(aId * 0.7548776) * 6.2831853 + uTime * aOmega * uSpin * 3.0;
    float csp = cos(sp), ssp = sin(sp);
    vec2 uv2 = vec2(aUV.x - 0.5, aUV.y - 0.5);
    vec2 uvr = vec2(csp * uv2.x - ssp * uv2.y, ssp * uv2.x + csp * uv2.y);

    // 把"相对位置"投影到局部基上：aPos 已经是 (切向, 径向) 两个方向的合成
    //   aPos.x 是切向系数、aPos.y 是径向系数（见 XSGeom.h 的角点构造）
    vec3 p = base + T * (aPos.x * uSizeMul) + R * (aPos.y * uSizeMul);

    // 自转改在 uv 上体现（板子太薄，转位置不如转贴图坐标便宜且稳）
    vUV = uvr + 0.5;

    // ---- 沿隧道轴流动：让风暴整体朝镜头压过来 ----
    p.z += aElev + fract(aId * 0.3819660 + uTime * uBandSpeed) * uSpan + uNear;
    // 上面那行把碎片撒在可见带的任意位置；fract 让它循环回卷 —— 保证任何时刻
    // 画面里总是满的（留白度低就靠这个）。

    // ---- 手持镜头（与 Filigree 同一套：原地旋转 + 平移）----
    p = vec3(dot(uCamR0.xyz, p), dot(uCamR1.xyz, p), dot(uCamR2.xyz, p));
    p -= uCamT.xyz;

    vWorld = p;
    vNrm   = normalize(vec3(dot(uCamR0.xyz, N), dot(uCamR1.xyz, N), dot(uCamR2.xyz, N)));
    vId    = aId;
    vHue   = aHue;
    vSpinPh = sp;
    vAlpha = aAlpha;

    // ---- 雾：两端都淡出（同 Filigree，深 1.5→6.0 / 远 到 0）----
    float k = clamp(1.0 - (p.z - uNear) / uSpan, 0.0, 1.0);
    float fadeFar  = smoothstep(0.0, 0.45, k);
    float fadeNear = clamp((p.z - uFadeIn) / max(uFadeFull - uFadeIn, 1e-4), 0.0, 1.0);
    vFog = fadeFar * fadeNear;

    // ---- 投影：相机在原点朝 +z 看，w = 视深做透视除法 ----
    float d = max(p.z, 0.05);
    float aspect = uResolution.x / uResolution.y;
    gl_Position = vec4(p.x * uFocal / aspect, p.y * uFocal, 0.0, d);
}
)";

static const char* kXSFS = R"(#version 330 core
in vec3  vWorld;
in vec3  vNrm;
in vec2  vUV;
in float vFog;
in float vId;
in float vHue;
in float vSpinPh;
in float vAlpha;
out vec4 fragColor;

uniform int   uPass;
uniform float uTime;
uniform float uOpacity;
uniform int   uDarkBg;
uniform vec4  uColA, uColB, uColC, uColD;   // 调色板四色（按色相混合）
uniform float uHueSpread;                    // 色相打散程度（0=同色，1=全彩虹）
uniform float uEdgeI;                        // 边缘高光强度
uniform float uEdgeW;                        // 边缘高光宽度
uniform float uStrobe;                       // 逐片闪烁强度（0=不闪）
uniform float uFlash;                        // 鼓点闪光（CPU 每帧给）
uniform sampler2D uTexScene, uTexBloom;
uniform vec2  uTexel, uBlurDir, uResolution;
uniform float uBloomAmt, uBloomThresh, uExposure, uSat;

float xshash(vec2 p){ vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
vec3 xshue(float h) {
    vec3 p = abs(fract(h + vec3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return clamp(p - 1.0, 0.0, 1.0);
}
/// 深空背景（同项目其它效果的做法：屏幕空间、不随镜头动）
vec3 xsBackground(vec2 uv) {
    float d = length(uv - 0.5);
    vec3 col = mix(vec3(0.030, 0.036, 0.070), vec3(0.002, 0.003, 0.008),
                   smoothstep(0.02, 0.62, d));
    for (int L = 0; L < 2; ++L) {
        float fl = float(L);
        vec2 g = uv * (150.0 + fl * 220.0);
        vec2 id = floor(g), f = fract(g) - 0.5;
        float h = xshash(id + fl * 41.7);
        float d2 = length(f - vec2(xshash(id + 2.3), xshash(id + 5.1)) + 0.5);
        float st = smoothstep(0.050 - fl * 0.018, 0.0, d2) * step(0.986 - fl * 0.004, h);
        st *= 0.55 + 0.45 * sin(uTime * 2.2 + h * 62.0);
        col += vec3(0.72, 0.80, 1.0) * st * (0.9 - fl * 0.3);
    }
    return col;
}

void main() {
    if (uPass == 0) { fragColor = vec4(xsBackground(vUV), 1.0); return; }
    if (uPass == 2) {   // 亮部提取
        vec3 c = texture(uTexScene, vUV).rgb;
        float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
        fragColor = vec4(c * max(0.0, l - uBloomThresh) / max(l, 1e-4), 1.0);
        return;
    }
    if (uPass == 3) {   // 分离式高斯
        vec2 t = uBlurDir * uTexel;
        vec3 s = texture(uTexBloom, vUV).rgb * 0.2270270;
        s += (texture(uTexBloom, vUV + t*1.3846).rgb + texture(uTexBloom, vUV - t*1.3846).rgb) * 0.3162162;
        s += (texture(uTexBloom, vUV + t*3.2308).rgb + texture(uTexBloom, vUV - t*3.2308).rgb) * 0.0702703;
        fragColor = vec4(s, 1.0);
        return;
    }
    if (uPass == 4) {   // 合成
        vec3 col = texture(uTexScene, vUV).rgb + texture(uTexBloom, vUV).rgb * uBloomAmt;
        col *= uExposure;
        float lum0 = max(dot(col, vec3(0.299, 0.587, 0.114)), 1e-4);
        float lumT = clamp((lum0*(2.51*lum0+0.03))/(lum0*(2.43*lum0+0.59)+0.14), 0.0, 1.0);
        col = clamp(col * (lumT / lum0), 0.0, 1.0);
        float lum = dot(col, vec3(0.299, 0.587, 0.114));
        col = clamp(mix(vec3(lum), col, uSat), 0.0, 1.0);
        vec2 c = vUV - 0.5;
        col *= 1.0 - dot(c, c) * 0.70;
        if (uDarkBg == 0) col = vec3(1.0) - col;
        fragColor = vec4(col, uOpacity);
        return;
    }

    // ================= 几何趟：一片金属碎片 =================
    vec3 n = normalize(vNrm);
    vec3 V = normalize(-vWorld);
    if (dot(n, V) < 0.0) n = -n;                  // 双面受光（碎片两面都看得到）
    // 两盏灯都在相机这一侧（同项目其它效果的约定）
    vec3 L1 = normalize(vec3(-0.45,  0.62, -0.64));
    vec3 L2 = normalize(vec3( 0.70, -0.34, -0.63));
    float lam = max(dot(n, L1), 0.0) * 0.85 + max(dot(n, L2), 0.0) * 0.45;
    float sh  = 0.08 + lam;

    // ---- 色相：调色板四色按色相取，再按 uHueSpread 打散 ----
    float h = fract(vHue * uHueSpread + 1.0 - uHueSpread * 0.5);
    vec3  base = h < 0.3333 ? mix(uColA.rgb, uColB.rgb, h * 3.0)
               : (h < 0.6667 ? mix(uColB.rgb, uColC.rgb, (h - 0.3333) * 3.0)
                             : mix(uColC.rgb, uColD.rgb, (h - 0.6667) * 3.0));
    base *= 0.55 + 0.75 * sh;

    // ---- 边缘高光：碎片很薄，让"边"亮起来才有金属感 ----
    // 判据用 u,v 到板子边界的距离（和 Filigree 的细条同一招，但这里是二维的）
    float e = min(min(vUV.x, 1.0 - vUV.x), min(vUV.y, 1.0 - vUV.y));
    float edge = 1.0 - smoothstep(0.0, max(uEdgeW, 1e-4), e);
    base += vec3(1.0) * edge * uEdgeI * max(dot(n, V), 0.0);

    // ---- 逐片闪烁：每片按自己的相位闪，频率很高 ----
    // 【它不是"噪点"】—— 每片是独立的小面，闪的是"这一片"，空间上是有结构的；
    //   而且它只是叠加项，真正的高低差来自公转位移。
    float fl = 0.5 + 0.5 * sin(vSpinPh * 3.0 + vId * 0.7);
    base += base * fl * uStrobe;

    // ---- 鼓点闪光：整体过曝一下（靠 HDR 溢出炸泛光）----
    base += vec3(1.0) * uFlash * 0.9;

    // ---- 雾：远端淡出到背景里 ----
    vec3 col = base * vFog;
    fragColor = vec4(col, vAlpha * vFog);
}
)";
