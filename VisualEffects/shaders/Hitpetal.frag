#version 330 core
// ============================================================================
// Hitpetal —— layers.html 配方（hit 系列第一版：整幅随鼓点明暗起落）
//
//   LAYERS  S0=4  S1=5  S2=5  S3=4  S4=4  S5=5
//   KNOBS   time=11.68 dens=2.90 sym=1.00 twist=0.95 roll=0.80 hue=0.50 core=0.00
//
//   与第二版 Hitpulse 同源（同一套 S0/S4），差别在 S2/S3/S5：
//     本版 S1 的 mix 系数是【空转】的（field*T 恒为 0），H 只通过末尾
//     `col *= 0.35 + 0.65*H` 作用 —— 鼓点打上来是整幅变亮变暗，纹理形态不变。
//     Hitpulse 则把 H 接进了 S1 的 mix 系数，鼓点改的是纹理形态。
//   S0..S5 与 layers.html 里的自定义代码逐字一致。
// ============================================================================

in vec2 vUV; out vec4 fragColor;
uniform float uTime, uBpm, uSpeed; uniform vec2 uResolution;
uniform float uTimeS;
uniform int uEncode, uDarkBg, uColorScheme;
uniform float uOpacity;
uniform sampler2D uOldEncoded;
uniform float uMorphBlend;

#ifndef FX_DENS
#define FX_DENS 2.90
#endif
#ifndef FX_SYM
#define FX_SYM 1.0
#endif
#ifndef FX_TWIST
#define FX_TWIST 0.95
#endif
#ifndef FX_ROLL
#define FX_ROLL 0.80
#endif
#ifndef FX_HUE
#define FX_HUE 0.50
#endif
#ifndef FX_CORE
#define FX_CORE 0.00
#endif
#ifndef FX_VIG
#define FX_VIG 1.75
#endif
#ifndef FX_COVER
#define FX_COVER 1.2
#endif

const float PI  = 3.14159265359;
const float TAU = 6.28318530718;

float thc(float a, float b) { return sin(a * cos(b)) / sin(a); }
vec3  pal(float t, vec3 d)  { return 0.5 + 0.5 * sin(TAU * (t + d)); }
vec2  rot2(vec2 v, float ang) { float c = cos(ang), s = sin(ang); return mat2(c, -s, s, c) * v; }
float disc(vec2 q, float r) { return r - length(q); }

void main()
{
    float T = uTimeS + uHitDrive;
    vec2  p = (gl_FragCoord.xy - 0.5 * uResolution) / uResolution.y;

    float field = 0.0;
    float glow  = 0.0;

    // ================= S0 · 坐标（+ TRIGGER）=================
    float rad = max(length(p), 1e-4);
    float th  = atan(p.y, p.x);
    float lr  = log(rad);
    float H = max(uHitKick, uHitSnare * 0.75);

    float a = th * FX_SYM;
    vec2  q = p;

    // ================= S1 =================
    field = pow(abs(mix(sin(T), thc(2.0, T), clamp(0.5 + 0.5 * ((field) * (T)), 0.0, 1.0))) + 0.05, 1.73) * 2.0 - 1.0;

    // ================= S2 =================
    { float _w = cos(smoothstep(-0.44, 0.44, lr) * 2.0 - 1.0);
      field = mix(field, sin(field * 2.13 + _w * 2.0), 0.73); }

    // ================= S3 =================
    { float ang = T * 0.61 * FX_ROLL + a * 2.0
                 + 3.30 * sin(((thc(2.0, 1.61)) + (sin(th))));
      q = rot2(p, ang);
      q *= 1.0 + 0.28 * sin(lr * 4.77 - T); }

    // ================= S4 =================
    {   float _e = smoothstep(0.18, 0.04, length(fract(q * 4.02) - 0.5));
      field = mix(field * 0.63, 1.0, _e); }

    // ================= S5 =================
    vec3 col;
    { col = mix(vec3(0.0), (pal(a / TAU * 1.0 + (((thc(2.0, a - rad * 2.60)) + (fract(0.80) * 2.0 - 1.0))) * 0.24
                           + T * 0.03 + FX_HUE, vec3(0.0, 1.0, 2.0) / 3.0 + vec3(-0.12, 0.08, -0.06))),
                    smoothstep(0.23, 0.63, abs(field))); }

    // ---- 把 H 接到画面上 ----
    col *= 0.35 + 0.65 * H;

    // ---- 中心核 + 暗角 + 柔和收束（与 layers 模板一致）----
    glow  = smoothstep(0.0, 3.2, -lr) * FX_CORE;
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
