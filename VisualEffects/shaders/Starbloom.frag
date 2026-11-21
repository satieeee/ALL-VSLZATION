#version 330 core
// ============================================================================
// Starbloom — 由 layers.html 的配方直接拼装（六层全是自定义）
//
//   LAYERS S0=4  S1=5  S2=5  S3=4  S4=4  S5=5
//   KNOBS  time=8.86 dens=9.70 sym=5.00 twist=1.95 roll=0.55 hue=0.49 core=0.00
//
//   层 0  软化对数径向   lr = log(rad + 0.35)，a = th × 5
//   层 1  螺旋带         sin(a + lr·密度·0.5 − 0.6T)
//   层 2  复合谐波扰动
//   层 3  差速旋转剪切   q = rot2(p, 0.5·roll·T + 2a + 4·sin(2·lr·密度 + a − 0.8T))
//   层 4  六个轨道节点 + 中心实体
//   层 5  余弦全谱 + 场遮罩（field 接近 0 处落黑）
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
#define FX_SYM 5.0
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
#define FX_COVER 1.0      // 层 5 自己已经做了遮罩，这里不再叠一层
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
    float lr  = log(rad + 0.35);
    float a   = th * FX_SYM;
    vec2  q   = p;

#if FX_STAGE >= 1
    // ================= 层 1 · 螺旋带 =================
    field = sin(a + lr * FX_DENS * 0.5 - T * 0.6);
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
    // ================= 层 3 · 空间剪切 =================
    {
        float ang = T * 0.5 * spd * FX_ROLL + a * 2.0
                  + 4.0 * sin(2.0 * lr * FX_DENS + a - T * 0.8);
        q = rot2(p, ang);
    }
#endif

#if FX_STAGE >= 4
    // ================= 层 4 · 实体雕刻 =================
    {
        float n = -1e9;
        for (int i = 0; i < 6; i++) {
            float fi = float(i);
            float ra = 0.22 + 0.09 * fi;
            float pa = fi * 2.399 + T * (0.5 - 0.03 * fi);
            vec2  pp = vec2(cos(pa), sin(pa)) * ra;
            n = max(n, disc(q - pp, 0.03 - 0.002 * fi));
        }
        float e = smoothstep(0.0, 0.006, n);
        field = mix(field * 0.6, 1.0, e);
    }
#endif

    // ================= 层 5 · 余弦全谱 + 场遮罩 =================
    {
        float _p = lr * 0.35 + a / TAU + T * 0.05 + FX_HUE;
        vec3 base = pal(_p, vec3(0.0, 1.0, 2.0) / 3.0 + vec3(0.05, 0.08 * T, -0.04))
                  * (0.55 + 0.45 * field);
        float mask = smoothstep(0.15, 0.65, abs(field));
        vec3 col = mix(vec3(0.0), base, mask);

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
