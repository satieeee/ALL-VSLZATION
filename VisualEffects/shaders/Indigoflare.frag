#version 330 core
// ============================================================================
// Indigoflare — 由 layers.html 的配方直接拼装
//
//   LAYERS S0=4(自定义)  S1=5(自定义)  S2=1(复合谐波)
//          S3=4(自定义)  S4=1(圆盘节点)  S5=5(自定义)
//
//   滑块：对称 3 · 剪切 2 · 中心核 0.2（其余为默认）
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

#ifndef FX_SYM
#define FX_SYM 3.0        // 对称阶数（uSym）
#endif
#ifndef FX_ROLL
#define FX_ROLL 2.0       // 剪切强度（uRoll）
#endif
#ifndef FX_CORE
#define FX_CORE 0.20      // 中心核（uCore）
#endif
#ifndef FX_VIG
#define FX_VIG 1.75
#endif
#ifndef FX_COVER
#define FX_COVER 1.4
#endif

const float PI  = 3.14159265359;
const float TAU = 6.28318530718;

float thc(float a, float b) { return sin(a * cos(b)) / sin(a); }
vec3  pal(float t, vec3 d)  { return 0.5 + 0.5 * sin(TAU * (t + d)); }
vec2  rot2(vec2 v, float ang) { float c = cos(ang), s = sin(ang); return mat2(c, -s, s, c) * v; }
float disc(vec2 q, float r) { return r - length(q); }

void main()
{
    float T   = uTimeS + uHitDrive;   // 跟着「速度」滑杆走，且改速度时不跳相位
    float spd = uBpm / 120.0;        // 只留 BPM 同步：用户速度已经积分进 uTimeS

    vec2 p = (gl_FragCoord.xy - 0.5 * uResolution) / uResolution.y;

    float field = 0.0;
    float glow  = 0.0;

    // ================= 层 0 · 坐标 =================
    float rad = max(length(p), 1e-4);
    float th  = atan(p.y, p.x);
    float lr  = log(rad + 0.52) * 1.28;
    float a   = th * FX_SYM;
    vec2  q   = p;

#if FX_STAGE >= 1
    // ================= 层 1 · 基础标量 =================
    field = ((mix(sin(((a) + (a + lr * 2.05))),
                  mix(lr, rad, clamp(0.5 + 0.5 * -1.71, 0.0, 1.0)),
                  clamp(0.5 + 0.5 * cos(a * 2.0), 0.0, 1.0)))
            / (0.4 + abs(smoothstep(-0.21, 0.21, cos(th)) * 2.0 - 1.0)));
#endif

#if FX_STAGE >= 2
    // ================= 层 2 · 复合谐波 =================
    {
        float wob = thc(2.0, lr * 3.5 + a + T * 0.6);
        field += 0.45 * sin(field * 3.0 + wob * 2.2);
        field *= 0.85 + 0.30 * thc(3.0, a * 2.0 - lr * 2.0);
    }
#endif

#if FX_STAGE >= 3
    // ================= 层 3 · 空间剪切 =================
    {
        float ang = T * 0.64 * spd * FX_ROLL + a * 2.0
                  + 5.54 * sin(smoothstep(-0.35, 0.35, mix(lr, sin(th), clamp(0.5 + 0.5 * T, 0.0, 1.0))) * 2.0 - 1.0)
                  + 3.53 * sin(fract(thc(2.0, a * 2.0)) * 2.0 - 1.0);
        q = rot2(p, ang);
        q.y = -q.y;
    }
#endif

#if FX_STAGE >= 4
    // ================= 层 4 · 圆盘节点 =================
    {
        float nodes = -1e9;
        for (int i = 0; i < 7; i++) {
            float fi = float(i);
            float ra = 0.20 + 0.098 * fi;
            float pa = fi * 2.399 + T * (0.55 - 0.045 * fi);
            vec2  pp = vec2(cos(pa), sin(pa)) * ra;
            nodes = max(nodes, disc(q - pp, 0.026 - 0.0021 * fi));
        }
        float e = smoothstep(0.0, 0.006, nodes);
        field = mix(field * 0.55, 1.0, e);
    }
#endif

    // ================= 层 5 · 上色 =================
    {
        float _v = clamp(0.5 + 0.5 * field, 0.0, 1.0);
        vec3 col5 = mix(vec3(0.01, 0.02, 0.05), vec3(0.27, 0.23, 1.00), pow(_v, 2.95));
        col5 += vec3(0.90, 0.95, 1.00) * pow(_v, 14.19);
        glow = smoothstep(0.0, 3.2, -lr) * FX_CORE;
        glow *= 1.0 + uBeatFlash * 0.9 + uHitKick * 1.2;
        col5  = mix(col5, vec3(1.0, 0.94, 0.88) * 2.0, glow);
        col5 *= smoothstep(0.0, 0.15, FX_VIG - length(p));
        col5  = (col5 * (2.51 * col5 + 0.03)) / (col5 * (2.43 * col5 + 0.59) + 0.14);

        // ---- 工程协议（别动）----
        float pat   = clamp(field, -1.0, 1.0);
        vec3  bg    = (uDarkBg > 0) ? vec3(0.0) : vec3(0.95);
        float cover = clamp(max(abs(field) * 0.9 * FX_COVER, glow), 0.0, 1.0);
        col5 = mix(bg, col5, cover);

        col5 = audioGrade(col5);
        col5 = cpuFinalize(col5, gl_FragCoord.xy, uDitherStrength);
        fragColor = vec4(col5, uOpacity);
    }
}
