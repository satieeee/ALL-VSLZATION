#version 330 core
// ============================================================================
// QKnopic —— "无图"重建版（实验件，看完即删）
//
// 用 @samuelyan #Shader_240422_4 的五段【中文说明文字】重建，不看结果图。
//
// ★ 诚实声明：我【已经看过】原作者的 GLSL 源码，忘不掉。所以这不是干净的
//   "纯文字"实验。它能隔离的是另一件事 —— 哪些槽位是文字说不清、必须靠图标的。
//   下面每一处文字没给的取值都标了【文字未给】。
//
// 五段文字（原文）：
//   STAGE 01 对数螺线与空间极化
//     "将所有像素相对位置映射为对数螺线的极径与两倍极角"
//     公式  r = ln‖u‖,  θ = 2·atan2(u_y, u_x)
//   STAGE 02 复合正弦的流体谐波
//     "让余弦潜入正弦振幅深处，生成陡峭峰脊与平缓谷底的非线性波浪"
//     公式  thc(a,b) = sin(a·cos b)/sin a
//           r' = r·[0.6 + 0.25·l·thc(1, 3θ + 2‖u‖ − t)],  l = min(1, atan(0.2t)/0.95)
//   STAGE 03 非线性正弦旋转矩阵
//     "旋转角将极角、对数半径的正弦扰动与流逝的时间深度融合"
//     公式  u' = R(t + 2θ + 2π·sin(3r + θ − t))·u
//     附注  "动态相角乘子 h：随周期阶梯跳变，调控旋臂分支对称阶数"
//   STAGE 04 组合符号距离场的花瓣雕刻
//     "组合符号距离场…相位/极角/半径三重混合，赋予骨架水墨光晕"
//     公式  s = 1 − smoothstep(−0.1, 0.1, d + 0.5)
//   STAGE 05 余弦调色与奇点流光
//     "相位三相偏移 vec3(0,1,2)/3.0 模拟光波通过三棱镜的折射色散"
//     公式  pal(t,a,b,c,d) = a + b·sin(2π(c·t + d))
//           C = ¼s + s·pal(...) − t/2,  C_core = mix(C, 2c₀, smoothstep(0, 3.5, −r))
//
// 与原版 Quicksilver 的差别只应出现在【文字未给】的槽位上。
// ============================================================================

in vec2 vUV; out vec4 fragColor;
uniform float uTime, uBpm, uSpeed; uniform vec2 uResolution;
uniform int uEncode, uDarkBg, uColorScheme;
uniform float uOpacity;
uniform sampler2D uOldEncoded;
uniform float uMorphBlend;

const float PI  = 3.14159265359;
const float TAU = 6.28318530718;

// 文字给了公式，照抄
float thc(float a, float b) { return sin(a * cos(b)) / sin(a); }
vec3  pal(float t, vec3 a, vec3 b, vec3 c, vec3 d) { return a + b * sin(TAU * (c * t + d)); }
vec2  rot2(vec2 v, float ang) { float c = cos(ang), s = sin(ang); return mat2(c, -s, s, c) * v; }

// 【文字未给】SDF 的具体实现。文字只说"符号距离场"，没给函数。
// 这里用 IQ 的标准式 —— 一个读过 SDF 的人会写的默认实现。
float sdEquilateralTriangle(vec2 p, float r) {
    const float k = 1.7320508;
    p.x = abs(p.x) - r;
    p.y = p.y + r / k;
    if (p.x + k * p.y > 0.0) p = vec2(p.x - k * p.y, -k * p.x - p.y) * 0.5;
    p.x -= clamp(p.x, -2.0 * r, 0.0);
    return -length(p) * sign(p.y);
}
float sdBox(vec2 p, vec2 b) {
    vec2 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

vec3 schemePhase(int cs) {
    if (cs == 1) return vec3(0.10, 1.10, 2.10) / 3.0;
    if (cs == 2) return vec3(0.25, 0.85, 1.45) / 3.0;
    if (cs == 3) return vec3(0.00, 0.50, 1.00) / 3.0;
    return vec3(0.00, 1.00, 2.00) / 3.0;      // 文字点名的那一套
}

void main()
{
    float T   = uTime;
    float spd = uBpm / 120.0;

    vec2 p = (gl_FragCoord.xy - 0.5 * uResolution.xy) / uResolution.y;

    // ================= STAGE 01  对数螺线与空间极化 =================
    // 文字：r = ln‖u‖, θ = 2·atan2
    float rad0 = max(length(p), 1e-4);
    float a    = atan(p.y, p.x) * 2.0;
    float lr   = log(rad0);

    // ================= STAGE 02  复合正弦的流体谐波 =================
    // 文字给了完整公式，照抄
    float l = min(1.0, atan(0.2 * T) / 0.95);
    lr *= 0.6 + 0.25 * l * thc(1.0, 3.0 * a + 2.0 * rad0 - T);

    // ================= STAGE 03  非线性正弦旋转矩阵 =================
    // 文字附注："动态相角乘子 h：随周期阶梯跳变，调控旋臂分支对称阶数"
    // 【文字未给】h 的具体式子。只读文字的人会写一个【真的会跳变】的 ——
    // 所以这里用 1..4 阶梯，而不是原作者那个恒等于 1.0 的死代码。
    float h  = 1.0 + floor(4.0 * fract(0.1 * T));
    float aa = h * a;
    // 【文字未给】角向常数：文字公式写 2π，代码里是 6.1415。按文字走，用 TAU。
    float ang = T * spd + 2.0 * aa + TAU * sin(3.0 * lr + aa - T);
    p = rot2(p, ang);
    float ca = atan(p.y, p.x);

    // ================= STAGE 04  组合符号距离场的花瓣雕刻 =================
    // 文字："相位/极角/半径三重混合，赋予骨架水墨光晕" + 公式 s = 1−smoothstep(−0.1,0.1,d+0.5)
    // 【文字未给】骨架的位置偏移量、三角形半径、矩形尺寸 —— 全部自拟。
    float hh = 0.25;
    hh += 0.5 * thc(4.0, -length(p) + 2.0 * atan(p.y, p.x) + T);
    hh += 0.5 * (0.5 + 0.5 * thc(3.0, length(p) * 3.0 - T));

    float d  = sdEquilateralTriangle(p - vec2(0.0, 0.001 - hh), 1.0);   // 【文字未给】半径取 1.0
    float s  = 1.0 - smoothstep(-0.1, 0.1, d + 0.5);
    float d2 = sdBox(p - vec2(0.0, -hh), vec2(0.05, 0.2));             // 【文字未给】尺寸照抄常见值
    s += 1.0 - smoothstep(-0.5, 0.5, d2);
    s  = clamp(s, 0.0, 1.0);

    // ================= STAGE 05  余弦调色与奇点流光 =================
    // 文字给了 C 的完整式子和三相偏移，照抄
    vec3  disp = schemePhase(uColorScheme);
    vec3  c0   = vec3(1.0, 0.93, 0.92);                    // 文字里的 2c₀ 的 c₀
    vec3  col  = 0.25 * s + s * pal(
                     thc(2.0, s + 2.0 * lr + a - T) - 0.5 * T,
                     vec3(1.0), vec3(1.0), vec3(1.0), disp);

    // 【文字未给】暗角常数。文字只说"外圈柔和暗角收束噪动"。
    col *= smoothstep(0.0, 0.1, 1.6 - length(p));

    // 文字给了：C_core = mix(C, 2c₀, smoothstep(0, 3.5, −r))  —— 照抄
    col = mix(col, c0 * 2.0, smoothstep(0.0, 3.5, -lr));

    // 底色（工程协议，非原作者）
    vec3 bg = (uDarkBg > 0) ? vec3(0.0) : vec3(0.95);
    col = mix(bg, col, clamp(max(s, smoothstep(0.0, 3.5, -lr)), 0.0, 1.0));

    float pat = clamp(s * 1.6 + lr * 0.5, -1.0, 1.0);

        col = audioGrade(col);
    col = cpuFinalize(col, gl_FragCoord.xy, uDitherStrength);
    fragColor = vec4(col, uOpacity);
}
