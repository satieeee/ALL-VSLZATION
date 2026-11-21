#version 330 core
// ============================================================================
// Sporefield — 由 layers.html 的配方拼装
//
//   LAYERS S0=4(自定义)  S1=3(径向脉冲)  S2=5(自定义)
//          S3=3(双旋镜像)  S4=0(无)  S5=5(自定义)
//   KNOBS  dens=9.70 sym=2.00 twist=1.95 roll=0.55 hue=0.49 core=0.00
// ============================================================================

in vec2 vUV; out vec4 fragColor;
uniform float uTime, uBpm, uSpeed; uniform vec2 uResolution;
uniform float uTimeS;
uniform int uEncode, uDarkBg, uColorScheme;
uniform float uOpacity;
uniform sampler2D uOldEncoded;
uniform float uMorphBlend;

#ifndef FX_STAGE
#define FX_STAGE 4
#endif

#ifndef FX_DENS
#define FX_DENS 9.7
#endif
#ifndef FX_SYM
#define FX_SYM 2.0
#endif
#ifndef FX_ROLL
#define FX_ROLL 0.55
#endif
#ifndef FX_HUE
#define FX_HUE 0.49
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

    // ================= 层 0 · 坐标 =================
    float rad = max(length(p), 1e-4);
    float th  = atan(p.y, p.x);
    float lr  = (rad * 2.19) * 1.54;
    float a   = th * FX_SYM;
    vec2  q   = p;

#if FX_STAGE >= 1
    // ================= 层 1 · 径向脉冲 =================
    field = sin(a * 2.0 + lr * FX_DENS * 0.9 * (0.7 + 0.5 * sin(rad * 6.0 - T)));
#endif

#if FX_STAGE >= 2
    // ================= 层 2 · 复合谐波 =================
    {
        float w = thc(2.0, lr * 3.0 + a - T * 0.5);
        field += 0.35 * sin(field * 2.5 + w * 2.0);
        field *= 0.85 + 0.25 * thc(3.0, a * 2.0 + lr);
    }
#endif

#if FX_STAGE >= 3
    // ================= 层 3 · 双旋镜像 =================
    {
        float ang = 3.0 * sin(2.2 * lr * FX_DENS - T * 0.8) * FX_ROLL;
        q = rot2(p, ang * 0.5);
        q.x = -q.x;
    }
#endif

#if FX_STAGE >= 4
    // ================= 层 4 · 无 =================
#endif

    // ================= 层 5 · 上色 =================
    {
        float _p = a / TAU * 1.0
                 + (smoothstep(-0.39, 0.039, fract(field) * 2.0 - 1.0) * 2.0 - 10.0) * 0.46
                 + T * 0.09 + FX_HUE;
        vec3 col = pal(_p, vec3(0.0, 1.0, 2.0) / 3.0 + vec3(0.08, T / 3.5, -0.07))
                 * (0.060 + 0.50 * field);

        glow = smoothstep(0.0, 3.2, -lr) * FX_CORE;
        glow *= 1.0 + uBeatFlash * 0.9 + uHitKick * 1.2;
        col   = mix(col, vec3(1.0, 0.94, 0.88) * 2.0, glow);
        col  *= smoothstep(0.0, 0.15, FX_VIG - length(p));
        col   = (col * (2.51 * col + 0.03)) / (col * (2.43 * col + 0.59) + 0.14);

        // ---- 工程协议（别动）----
        float pat   = clamp(field, -1.0, 1.0);
        vec3  bg    = (uDarkBg > 0) ? vec3(0.0) : vec3(0.95);
        float cover = clamp(max(abs(field) * 0.9 * FX_COVER, glow), 0.0, 1.0);
        col = mix(bg, col, cover);

        if (uEncode > 0) { fragColor = vec4(pat * 0.5 + 0.5, col); return; }
        if (uMorphBlend > 0.0 && uMorphBlend < 1.0) {
            vec2  tuv = gl_FragCoord.xy / uResolution;
            vec4  oe  = texture(uOldEncoded, tuv);
            float sb  = uMorphBlend * uMorphBlend * (3.0 - 2.0 * uMorphBlend);
            col = mix(oe.gba, col, sb);
        }
        col = audioGrade(col);
        col = cpuFinalize(col, gl_FragCoord.xy, uDitherStrength);
        fragColor = vec4(col, uOpacity);
    }
}
