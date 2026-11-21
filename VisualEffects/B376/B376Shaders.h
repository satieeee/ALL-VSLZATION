// ============================================================================
// B376Shaders
//
// B376 轮廓网格着色器。注意 M2/M3/M4 的网格渲染器共用这一对（不是 B376 专用）。
//
// 【为什么单独一个头】离线台要的是【实际跑的那一份】原文。以前靠 ExtractRaw()
// 从 main.cpp 里抓文本，台子因此依赖 main.cpp 的排版；改成 include 之后两边
// 拿到的是同一个变量，抄错/漏改这一类问题就不存在了。
//
// 【改了这个文件要重编离线台】—— 着色器文本是编进 exe 的。
// ============================================================================
#pragma once

static const char* kB376MeshVS = R"(#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aCenter;
layout(location=2) in float aPhase;
layout(location=3) in float aRadius;
layout(location=4) in float aArc;
layout(location=5) in float aStatic;
layout(location=6) in float aZ;       // 单体深度 zHome（世界Z）
layout(location=7) in float aS;       // 弧长（去包裹 0..1+）
layout(location=8) in float aD;       // 有符号距离（内部为负）
layout(location=9) in float aM;       // 曲线行（静态=-1）
out vec2 vUV;
out float vDepth;
out float vS;
out float vD;
out float vM;
out vec2 vCenter;    // 单体中心（图像坐标，与 vUV 一致；世界变换用局部翻转 center）
out float vRadius;   // 单体半径（不封顶，用于把流动源夹回单体）
uniform float uTime, uSpeed, uFlowAmp;
uniform float uAspect;   // 窗口宽高比 w/h（保持原图比例，不拉伸）
uniform float uImgA;     // 图像宽高比 W/H（世界布局，保持单体形状不拉伸）
uniform float uFitK, uFitM;  // 内容 bbox 的 contain-fit 缩放（世界→NDC；按实际单体范围适配窗口，替代整图 imgA）
uniform float uExitTime; // 出场计时（切出动画，立刻飞出）
void main() {
    // 世界平面：图像布局（场坐标[0,1] → 世界，保持图像宽高比）；翻转 y → 图像摆正
    vec2 worldScale = vec2(2.0, 2.0 / uImgA);
    vec2 center = vec2(aCenter.x, 1.0 - aCenter.y);
    vec2 pos    = vec2(aPos.x, 1.0 - aPos.y);
    vec2 home   = (center - 0.5) * worldScale;       // 单体位置 = 图像摆放位置（固定，相对位置不动）
    vec2 local  = (pos - center) * worldScale;       // 局部形状
    // 投影缩放 k/m（提前算：出入场离屏位移要按 NDC 反算世界位移）
    float camZ = 3.5;
    float k, m;   // 投影 x/y 缩放（按单体实际内容 bbox 适配窗口：M2 方图内容横条也能占满，不拉伸、不裁内容）
    if (uAspect * uFitK >= uFitM) { m = uFitM;      k = m / uAspect; }   // 窗口较"横"→贴满高度
    else                          { k = uFitK;      m = k * uAspect; }   // 窗口较"竖"→贴满宽度
    // 入场动画（effect 切入时）：单体从屏外沿四分之一椭圆飞入归位（变加速）
    // 时长 1.4s：留足呼吸空间让"远快近慢"的缓出被肉眼感知（原 0.6s 太短看不出变速）
    float ph = aPhase;
    float tt = uTime * uSpeed;
    float cutIn = clamp(tt / 0.6, 0.0, 1.0);          // 入场进度 0→1（120BPM 下 0.6s 完成）
    float easeIn = 1.0 - pow(1.0 - cutIn, 3.0);       // 入场快进慢停（缓出）
    float dispIn = 1.0 - easeIn;                       // 1→0（屏外→home）
    float cutOut = clamp(uExitTime / 0.6, 0.0, 1.0);  // 出场进度 0→1（0.6s 飞出）
    // 正常(uExitTime≥990)=不退出 dispOut=0；退出中(<0.6)=0→1；退完等待切换(<990)=保持屏外
    float dispOut = 0.0;
    if      (uExitTime < 0.6)    dispOut = cutOut*cutOut;
    else if (uExitTime < 990.0)  dispOut = 1.0;
    float disp = max(dispIn, dispOut);                 // 入场或出场的位移
    // 离屏位移幅值：按 NDC 反算，保证每个单体（含自身尺寸）整体离开窗口
    //（中心超出窗口角 √2 + 单体投影半宽；最坏方向也成立，故入场起点/出场终点都在窗外）
    float dist = camZ - aZ;                            // 单体透视距离
    float rN = max(aRadius * 2.0 * k, aRadius * 2.0 / uImgA * m) / dist;  // 单体 NDC 半宽
    float homeD = length(home * vec2(k, m) / dist);    // 原位中心到窗口中心的 NDC 距离
    float amp = (1.4142 + rN + homeD + 0.1) * dist / min(k, m);           // 世界位移幅值
    float ea = amp, eb = amp*0.6;
    float theta = 1.5708*disp;                       // 0→π/2→0
    vec2 E0 = vec2(ea, 0.0);
    vec2 Et = vec2(ea*cos(theta), eb*sin(theta));
    float cr = cos(ph*1.7), sr = sin(ph*1.7);        // 椭圆朝向（每单体不同）
    vec2 off = vec2(cr*(Et.x-E0.x) - sr*(Et.y-E0.y), sr*(Et.x-E0.x) + cr*(Et.y-E0.y));
    // 出入场三维缓慢旋转：绕单体中心慢转（yaw+pitch 按相位分轴），静止时=0 不改变造型
    float rot = 0.7 * disp;                          // 0→0.7rad（≈40°，缓慢）
    float aY = rot * sin(ph * 2.1);
    float aX = rot * cos(ph * 2.1);
    vec3 p = vec3(local, 0.0);
    vec3 q = vec3(p.x*cos(aY) - p.z*sin(aY), p.y, p.x*sin(aY) + p.z*cos(aY));   // 绕 Y
    vec3 r = vec3(q.x, q.y*cos(aX) - q.z*sin(aX), q.y*sin(aX) + q.z*cos(aX));   // 绕 X
    // 曲线流 movement：单体固定原位（+出入场位移+旋转），贴图沿曲线流动（片元着色器处理）
    vec3 monPos = vec3(home + off, aZ);
    vec3 world = vec3(monPos.xy + r.xy, monPos.z + r.z);
    vUV = aPos; vDepth = aZ;
    vS = aS; vD = aD; vM = aM;
    vCenter = aCenter; vRadius = aRadius;   // vCenter 用图像坐标（与 vUV 同系，AO/流夹紧才正确）

    // 透视相机：让图像【不扭曲比例】地尽量占满整个窗口（contain-fit，限制维度贴窗口边，另一维留边）
    vec3 camPos = vec3(0.0, 0.0, 3.5);
    vec3 v = world - camPos;
    float n = 1.0, far = 8.0;
    gl_Position = vec4(v.x*k, v.y*m, (far+n)/(n-far)*v.z + 2.0*far*n/(n-far), -v.z);   // 深度方向修正：近挡远
}
)";

static const char* kB376MeshFS = R"(#version 330 core
in vec2 vUV;
in float vDepth;
in float vS;
in float vD;
in float vM;
in vec2 vCenter;
in float vRadius;
out vec4 fragColor;
uniform sampler2D uLum;
uniform sampler2D uCurves;
uniform float uTime, uSpeed, uFlowAmp;
uniform float uFlowPhase;   // 流动相位（CPU 实时累计：音频变速驱动流速，不跳变）
uniform int uDarkBg;
uniform float uOpacity;
vec3 hsl2rgb(float h, float s, float l) {
    h = mod(h, 360.0); if (h < 0.0) h += 360.0;
    float c = (1.0 - abs(2.0*l - 1.0)) * s;
    float hp = h / 60.0;
    float x = c * (1.0 - abs(mod(hp, 2.0) - 1.0));
    float m = l - c * 0.5;
    vec3 rgb;
    if (hp < 1.0)      rgb = vec3(c, x, 0);
    else if (hp < 2.0) rgb = vec3(x, c, 0);
    else if (hp < 3.0) rgb = vec3(0, c, x);
    else if (hp < 4.0) rgb = vec3(0, x, c);
    else if (hp < 5.0) rgb = vec3(x, 0, c);
    else               rgb = vec3(c, 0, x);
    return rgb + m;
}
void main() {
    // 静态单体（无曲线，m=-1）：显示原位贴图
    if (vM < 0.0) {
        vec3 g = texture(uLum, vUV).rgb;   // 表层 = 原图彩色
        // 表面实感：边缘 AO + 深度分层（固定，不随贴图流动 → 光和谐）
        float rN = length(vUV - vCenter) / max(vRadius, 1e-4);
        float ao = 1.0 - 0.35 * smoothstep(0.55, 1.0, rN);
        float depthTone = 0.7 + 0.6 * (0.5 - vDepth);
        vec3 col = clamp(g, 0.0, 1.0) * clamp(depthTone, 0.0, 1.0) * ao;
        fragColor = vec4(col, uOpacity);
        return;
    }
    // 曲线流：贴图沿曲线顺时针流动（纯切向）；相位由 CPU 实时累计（音频实时变速，不跳变）
    float sf = fract(vS - uFlowPhase);
    // 采样曲线纹理：texel = (Cx, Cy, 内法线nx, ny)
    vec4 c = texture(uCurves, vec2(sf, vM));
    vec2 C = c.xy;                        // 曲线点（不做径向位移）
    vec2 N = c.zw;
    // 源位置 = 曲线 + 法线*d（沿曲线平行线移动 = 纯切向流）
    vec2 src = C + N * vD;
    // 把源位置夹回单体范围（不采样背景）：距单体中心 ≤ 半径
    vec2 toC = src - vCenter;
    float dl = length(toC);
    if (dl > vRadius && dl > 1e-4) src = vCenter + (toC/dl) * vRadius;
    src = clamp(src, 0.001, 0.999);
    vec3 g = texture(uLum, src).rgb;   // 表层 = 原图彩色
    // 表面实感：边缘 AO + 深度分层（固定，不随贴图流动 → 光和谐）
    float rN = length(vUV - vCenter) / max(vRadius, 1e-4);
    float ao = 1.0 - 0.35 * smoothstep(0.55, 1.0, rN);
    float depthTone = 0.7 + 0.6 * (0.5 - vDepth);
    vec3 col = clamp(g, 0.0, 1.0) * clamp(depthTone, 0.0, 1.0) * ao;
    col = clamp(col, 0.0, 1.0);
    fragColor = vec4(col, uOpacity);
}
)";
