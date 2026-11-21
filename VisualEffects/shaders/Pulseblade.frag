#version 330 core
// ============================================================================
// Pulseblade — 由 layers.html 的配方直接拼装
//
//   LAYERS S0=4(自定义)  S1=3(径向脉冲)  S2=5(自定义)
//          S3=1(位置相关旋转)  S4=4(自定义)  S5=0(余弦全谱 + 色散)
//
//   层 0  软化对数径向   lr = log(rad + 0.13) − 1.92·rad
//   层 1  径向脉冲带     sin(2a + 密度·(0.7 + 0.5·sin(6r − T)))
//   层 2  骰子扰动       pow 整流 × |sin 4a| 整流 → 交叉网格
//   层 3  位置相关旋转   空间被剪切，阶数 1..3 阶梯跳变
//   层 4  实体雕刻       q.x 附近切一道条带
//   层 5  余弦全谱 + 三相色散
// ============================================================================

in vec2 vUV; out vec4 fragColor;
uniform float uTime, uBpm, uSpeed; uniform vec2 uResolution;
uniform float uTimeS;   // ∫dt·速度滑杆（CPU 积分；没接这个滑杆的旧版宿主会传 0）
uniform int uEncode, uDarkBg, uColorScheme;
uniform float uOpacity;
uniform sampler2D uOldEncoded;
uniform float uMorphBlend;

#ifndef FX_STAGE
#define FX_STAGE 4
#endif

// ---- 可扫描参数（对应 layers.html 右侧滑块）----
#ifndef FX_SYM
#define FX_SYM 5.0        // 对称阶数（工具里的 uSym）
#endif
#ifndef FX_DENS
#define FX_DENS 3.4       // 密度（uDens）
#endif
#ifndef FX_ROLL
#define FX_ROLL 1.0       // 剪切强度（uRoll）
#endif
#ifndef FX_HUE
#define FX_HUE 0.70       // 色相（uHue）
#endif
#ifndef FX_VIG
#define FX_VIG 1.75       // 暗角半径
#endif
#ifndef FX_BARW
#define FX_BARW 0.030     // 层 4 条带半宽
#endif
#ifndef FX_BARSHARP
#define FX_BARSHARP 0.63  // 条带外区域的场衰减
#endif
#ifndef FX_CORE
#define FX_CORE 0.0       // 中心核（工具里 uCore = 0）
#endif
#ifndef FX_COVER
#define FX_COVER 1.4      // 场对底色的覆盖率增益：越大越"满"，越小越透
#endif

const float PI  = 3.14159265359;
const float TAU = 6.28318530718;

float thc(float a, float b) { return sin(a * cos(b)) / sin(a); }
vec3  pal(float t, vec3 d)  { return 0.5 + 0.5 * sin(TAU * (t + d)); }
vec2  rot2(vec2 v, float ang) { float c = cos(ang), s = sin(ang); return mat2(c, -s, s, c) * v; }

void main()
{
    float T   = uTimeS + uHitDrive;   // 跟着「速度」滑杆走，且改速度时不跳相位
    float spd = uBpm / 120.0;        // 只留 BPM 同步：用户速度已经积分进 uTimeS

    vec2 p = (gl_FragCoord.xy - 0.5 * uResolution) / uResolution.y;

    float field = 0.0;
    float glow  = 0.0;

    // ================= 层 0 · 软化对数径向 =================
    float rad = max(length(p), 1e-4);
    float th  = atan(p.y, p.x);
    float lr  = log(rad + 0.13) - rad * 1.92;
    float a   = th * FX_SYM;
    vec2  q   = p;

#if FX_STAGE >= 1
    // ================= 层 1 · 径向脉冲带 =================
    field = sin(a * 2.0 + lr * FX_DENS * 0.9 * (0.7 + 0.5 * sin(rad * 6.0 - T)));
#endif

#if FX_STAGE >= 2
    // ================= 层 2 · 骰子扰动 =================
    {
        float _w = ((pow(abs(0.88) + 0.05, 0.61) * 2.0 - 1.0)
                  * (pow(abs(sin(a * 4.0)) + 0.05, 0.91) * 2.0 - 1.0));
        field = sin(field * 2.63 + _w * 1.03);
    }
#endif

#if FX_STAGE >= 3
    // ================= 层 3 · 位置相关旋转 =================
    {
        float h   = 1.0 + floor(3.0 * fract(0.07 * T));
        float aa  = h * a;
        float ang = T * 0.55 * spd * FX_ROLL + 2.0 * aa
                  + 5.8 * sin(2.6 * lr * FX_DENS + aa - T * 0.9);
        q = rot2(p, ang);
    }
#endif

#if FX_STAGE >= 4
    // ================= 层 4 · 实体雕刻 =================
    {
        float _e = smoothstep(0.012, 0.0, abs(q.x) - FX_BARW);
        field = mix(field * FX_BARSHARP, 1.0, _e);
    }
#endif

    // ================= 层 5 · 余弦全谱 + 色散 =================
    vec3 disp = vec3(0.0, 1.0, 2.0) / 3.0
              + mix(vec3(0.0), vec3(0.06, 0.12, 0.20), float(uColorScheme) / 5.0);

    float t  = thc(2.0, field * 2.2 + lr * 1.6 - T * 0.7) * 0.35 - 0.5 * T * 0.35;
    vec3  col = pal(t + FX_HUE, disp);

    // 中心核 + 暗角（与工具里的收尾一致）
    glow = smoothstep(0.0, 3.2, -lr) * FX_CORE;
    glow *= 1.0 + uBeatFlash * 0.9 + uHitKick * 1.2;
    col  = mix(col, vec3(1.0, 0.94, 0.88) * 2.0, glow);
    col *= smoothstep(0.0, 0.15, FX_VIG - length(p));

    // 工具的色调映射（网页面板里看到的就是这一版）
    col = (col * (2.51 * col + 0.03)) / (col * (2.43 * col + 0.59) + 0.14);

    // ---- 工程协议（别动）----
    float pat   = clamp(field, -1.0, 1.0);
    vec3  bg    = (uDarkBg > 0) ? vec3(0.0) : vec3(0.95);
    float cover = clamp(max(abs(field) * 0.9 * FX_COVER, glow), 0.0, 1.0);
    col = mix(bg, col, cover);

        col = audioGrade(col);
    col = cpuFinalize(col, gl_FragCoord.xy, uDitherStrength);
    fragColor = vec4(col, uOpacity);
}
