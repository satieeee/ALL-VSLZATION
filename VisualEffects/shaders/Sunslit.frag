#version 330 core
// ============================================================================
// Sunslit — 由 layers.html 的配方直接拼装（笛卡尔坐标）
//
//   LAYERS  S0=4  S1=5  S2=5  S3=4  S4=4  S5=5   （六层全是自定义）
//
//   滑块：密度 12 · 对称 4 · 扭转 0.95 · 剪切 0 · 色相 0.18 · 中心核 0
//
//   ★ 坐标是【笛卡尔】：th = p.x，lr = p.y，所以 a = x × 4。
//     没有 atan，也就不存在分支切割；极坐标那套"角度不能乘小数"的约束在这里不适用。
// ============================================================================

in vec2 vUV; out vec4 fragColor;
uniform float uTime, uBpm, uSpeed; uniform vec2 uResolution;
uniform float uTimeS;   // ∫dt·速度滑杆（CPU 积分；改速度时不跳相位）
uniform int uEncode, uDarkBg, uColorScheme;
uniform float uOpacity;
uniform sampler2D uOldEncoded;
uniform float uMorphBlend;

#ifndef FX_STAGE
#define FX_STAGE 4
#endif

#ifndef FX_DENS
#define FX_DENS 12.0      // 密度（uDens）
#endif
#ifndef FX_SYM
#define FX_SYM 4.0        // 对称阶数（uSym）；笛卡尔下就是 x 的倍率
#endif
#ifndef FX_ROLL
#define FX_ROLL 0.0       // 剪切（uRoll）—— 工具里是 0
#endif
#ifndef FX_HUE
#define FX_HUE 0.18       // 色相（uHue）
#endif
#ifndef FX_CORE
#define FX_CORE 0.0
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
    float T   = uTimeS + uHitDrive;
    float spd = uBpm / 120.0;

    vec2 p = (gl_FragCoord.xy - 0.5 * uResolution) / uResolution.y;

    float field = 0.0;
    float glow  = 0.0;

    // ================= 层 0 · 笛卡尔坐标 =================
    float rad = max(length(p), 1e-4);   // 仍然定义：收尾/部分层要读
    float th  = p.x;                    // th 现在就是 x
    float lr  = p.y;                    // lr 现在就是 y
    float a   = th * FX_SYM;            // = x × 4
    vec2  q   = p;

#if FX_STAGE >= 1
    // ================= 层 1 · 基础标量 =================
    field = ((mix(((FX_DENS) * (sin(a + T))), cos(lr),
                  clamp(0.5 + 0.5 * sin(lr), 0.0, 1.0)))
           / (0.4 + abs(((sin(a + lr * 2.12))
                        * (pow(abs(sin(a)) + 0.05, 1.42) * 2.0 - 1.0)))));
#endif

#if FX_STAGE >= 2
    // ================= 层 2 · 扰动 =================
    {
        float _w = ((mix(FX_DENS, field, clamp(0.5 + 0.5 * field, 0.0, 1.0)))
                  / (0.4 + abs(sin(((a + lr * 1.56) + (0.31))))));
        field += 0.36 * sin(field * 3.92 + _w);
    }
#endif

#if FX_STAGE >= 3
    // ================= 层 3 · 空间剪切 =================
    {
        float ang = T * 0.58 * spd * FX_ROLL + a * 4.0
                  + 2.48 * sin(((sin(((-1.66) + (a - rad * 2.59))))
                              * (((field) / (0.4 + abs(sin(th)))))));
        q = rot2(p, ang);
        q.y = -q.y;
        q *= 1.0 + 0.15 * sin(lr * 2.10 - T);
    }
#endif

#if FX_STAGE >= 4
    // ================= 层 4 · 实体雕刻 =================
    {
        float _e = smoothstep(0.012, 0.0, abs(q.x) - 0.07);
        field = mix(field * 0.44, 1.0, _e);
    }
#endif

    // ================= 层 5 · 上色（余弦全谱）=================
    {
        float t = thc(2.0, field * 2.2 + lr * 1.6 - T * 0.7) * 0.35 - 0.5 * T * 0.35;
        vec3 col5 = pal(t + FX_HUE, vec3(0.0, 1.0, 2.0) / 3.0);

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
