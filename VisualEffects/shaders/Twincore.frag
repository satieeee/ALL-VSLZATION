#version 330 core
// ============================================================================
// Twincore —— layers.html 配方原样落地
//
//   LAYERS  S0=4(✎自定义)  S1=5(✎自定义)  S2=5(✎自定义)  S3=3(双旋镜像)  S4=0(无)  S5=5(✎自定义)
//   KNOBS   time=17.57 dens=1.00 sym=1.00 twist=0.65 roll=0.85 hue=0.79 core=0.00
//
//   与 layers 里逐字一致，只做两类映射：
//     ① uDens/uSym/uTwist/uRoll/uHue/uCore → FX_* 宏（下面 #define，引擎不传这些 uniform）
//     ② 时间轴 T = uTimeS + uHitDrive —— 和工程里其它落地效果同一套鼓点加速：
//        uTimeS 是积分过速度滑杆的秒数（改速度不跳相位），uHitDrive 是鼓点
//        积出来的额外相位（抽插变速，累积不回弹）。web 的 uTime 只对应 uTimeS。
//   S0/S1/S2/S5 是自定义代码，S3 是内置「双旋镜像」，S4 为「无」。
// ============================================================================

in vec2 vUV; out vec4 fragColor;
uniform float uTime, uBpm, uSpeed; uniform vec2 uResolution;
uniform float uTimeS;
uniform int uEncode, uDarkBg, uColorScheme;
uniform float uOpacity;
uniform sampler2D uOldEncoded;
uniform float uMorphBlend;

#ifndef FX_DENS
#define FX_DENS 1.00
#endif
#ifndef FX_SYM
#define FX_SYM 1.0
#endif
#ifndef FX_TWIST
#define FX_TWIST 0.65
#endif
#ifndef FX_ROLL
#define FX_ROLL 0.85
#endif
#ifndef FX_HUE
#define FX_HUE 0.79
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
    float T = uTimeS + uHitDrive;   // 速度滑杆积分 + 鼓点累积的额外相位（同其它落地效果）
    vec2  p = (gl_FragCoord.xy - 0.5 * uResolution) / uResolution.y;

    float field = 0.0;
    float glow  = 0.0;

    // ================= S0 · 坐标 =================
    float rad = max(length(p), 1e-4);
    float th  = atan(p.y, p.x);
    float lr  = (log(rad + 0.45)) * 1.44
      + 0.43 * cos(th * 2.0)
      + 0.58 * cos(th * 3.0);

    float a = th * FX_SYM;
    vec2  q = p;

    // ================= S1 · 基础标量 =================
    field = mix(sin(cos(FX_DENS)), ((((sin(th)) / (0.4 + abs(sin(a + lr * 1.04))))) + (sin(T))), clamp(0.5 + 0.5 * sin(((a - rad * 1.65) + (lr))), 0.0, 1.0));

    // ================= S2 · 标量扰动 =================
    { float _w = ((sin(((a * 3.0) + (rad)))) / (0.4 + abs(((sin(a * 4.0)) / (0.4 + abs(FX_DENS))))));
      field = sin(field * 2.93 + _w * 2.26); }

    // ================= S3 · 空间剪切（双旋镜像）=================
    { float ang = 3.0 * sin(2.2 * lr * FX_DENS - T * 0.8) * FX_ROLL;
      q = rot2(p, ang * 0.5);
      q.x = -q.x; }

    // ================= S4 · 实体雕刻（无）=================
    /* S4 off */

    // ================= S5 · 上色 =================
    vec3 col;
    { col = clamp((mix(vec3(0.0), ((pal(a / TAU * 3.0 + (((((sin(th)) / (0.4 + abs(sin(a))))) / (0.4 + abs(((field) / (0.4 + abs(rad))))))) * 0.29 + T * 0.02 + FX_HUE, vec3(0.0, 1.0, 2.0) / 3.0 + vec3(-0.10, 0.04, -0.04)) * mix(vec3(1.0), vec3(0.62, 0.86, 0.02), 0.41)) * (0.70 + 0.37 * field) + vec3(0.51, 0.93, 0.06) * pow(max(field, 0.0), 4.91) * 0.75), smoothstep(0.07, 0.78, abs(field)))) * 1.04, 0.0, 1.0); }

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
