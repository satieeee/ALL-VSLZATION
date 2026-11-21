#version 330 core
// ============================================================================
// Hitpulse —— layers.html 配方原样落地（六层取自 layers.myCode）
//
//   LAYERS  S0=4  S1=5  S2=5  S3=4  S4=4  S5=5
//   KNOBS   time=11.68 dens=2.90 sym=1.00 twist=0.95 roll=0.80 hue=0.50 core=0.00
//
//   与 layers 里的差异只有两处：
//     ① S0 的触发行：layers 写 float H = uHit;（页面里 uHit 由内置检测给出）
//        这里没有 uHit，等价写法是 max(uHitKick, uHitSnare*0.75) —— 同一个量。
//     ② S1 那个原本空转的 mix 系数接上了 H（原式里 field*T 恒为 0）：
//        0.5 + 0.5*((H)*2-1) == H  →  H=0 用 sin(T)，H=1 用 thc(2,T)。
//   S2 / S3 / S4 / S5 逐字未改。
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
// 【静默态】平时几乎不走、也压暗；鼓点才把时间和亮度推起来。
#ifndef FX_IDLE
#define FX_IDLE 0.00      // 空闲时额外自走的倍率。现在"无 hit 极慢、有 hit 走一个周期"
                          // 已经由引擎的 uHitFlowDrive 负责，这里保持 0，否则两边会叠加。
#endif
#ifndef FX_DIM
#define FX_DIM 1.00       // 亮度倍率。现在是 1 = 不压暗：基础速率已经是正常的 1×，
                          // 画面本来就一直在正常播放，没有"静默态"需要压暗。
#endif

const float PI  = 3.14159265359;
const float TAU = 6.28318530718;

float thc(float a, float b) { return sin(a * cos(b)) / sin(a); }
vec3  pal(float t, vec3 d)  { return 0.5 + 0.5 * sin(TAU * (t + d)); }
vec2  rot2(vec2 v, float ang) { float c = cos(ang), s = sin(ang); return mat2(c, -s, s, c) * v; }
float disc(vec2 q, float r) { return r - length(q); }

void main()
{
    // 【全局慢钟】静默时有底速，鼓点把相位往前推 —— S1 的相位和 S3 的旋转都用它。
    // 用 uHitFlowDrive 而不是 uHitDrive：后者积分的是【尖峰】包络，导数是方波，
    // 画面只能"一冲一停"；前者积分的是先低通过的包络（起 0.10s / 落 0.60s），
    // 导数是钟形，加速-减速连续 —— 这是抽动和呼吸的差别。
    float T = uTimeS * FX_IDLE + uHitFlowDrive;
    vec2  p = (gl_FragCoord.xy - 0.5 * uResolution) / uResolution.y;

    float field = 0.0;
    float glow  = 0.0;

    // ================= S0 · 坐标（+ TRIGGER）=================
    float rad = max(length(p), 1e-4);
    float th  = atan(p.y, p.x);
    float lr  = log(rad);
    float H = max(uHitKick, uHitSnare * 0.75);   // layers: float H = uHit;

    float a = th * FX_SYM;
    vec2  q = p;

    // ================= S1 · 基础标量（相位 = 慢钟 T，鼓点自动加速）=================
    // ① 形态函数：mix(sin, thc) + pow(abs)+0.05 —— 与 layers 里逐字一致，一个字不动。
    // ② 相位直接取 T。**不要再做"锚定"**：T 里已经含了 uHitDrive，
    //       dX/dt = FX_IDLE + 8.5·hitKick + 4.7·hitSnare
    //    它的最大值天然就在鼓点上 —— 这就是"变率最大落在鼓点"，而且全程连续。
    //
    //    （旧版用 tk = -0.28·log(uHitKick) 反解"距上次 kick 多久"再把相位拽回 0，
    //      那是【按 H 混合两个相位】+【round 折圈】。有音乐时 H 每帧都在变、
    //      log 对包络的小抖动极其敏感、round 每半圈还跳 2π —— 三者叠起来就是"鬼畜"。
    //      那条路只在"相位自由跑、与鼓点无关"时才需要，现在时间本身就由鼓点驱动，用不着了。）
    float X = T;

    // ③ mix 系数静音 = 0.5（与 Hitpetal 一致，节奏就是原来那个），鼓点推到 1.0。
    //    【不要写成 0.5+0.5*(2H-1)=H】：那样静音时系数塌到 0，thc 那一半消失，
    //    画面只剩一条宽而慢的呼吸，看上去就像播放速度掉了好几倍。
    // mix 系数也必须用 uHitFlow —— H 是尖峰，鼓点那一帧从 0 跳到 1，
    // 形态会"啪"地换一档；而这个 field 又被 S3 的旋转角直接吃进去（5.57*sin(mix(...field...))），
    // 于是整幅画面跟着抽一下。速度、亮度、形态三处必须同一条曲线，否则永远有一处在跳。
    field = pow(abs(mix(sin(X), thc(2.0, X), clamp(0.5 + 0.5 * uHitFlow, 0.0, 1.0))) + 0.05, 1.73) * 2.0 - 1.0;

    // ================= S2 =================
    { float _w = ((mix(lr, field, clamp(0.5 + 0.5 * rad, 0.0, 1.0))) / (0.4 + abs(sin(((a * 2.0) + (lr))))));
      field = mix(field, sin(field * 2.70 + _w * 2.0), 0.66); }

    // ================= S3 =================
    { float ang = T * 0.70 * FX_ROLL + a * 2.0
                 + 4.02 * sin(((fract(sin(a - rad * 2.61)) * 2.0 - 1.0) / (0.4 + abs(thc(2.0, rad)))))
                 + 5.57 * sin(mix(fract(FX_DENS) * 2.0 - 1.0, smoothstep(-0.23, 0.23, sin(a * 2.0)) * 2.0 - 1.0, clamp(0.5 + 0.5 * mix(field, -0.34, clamp(0.5 + 0.5 * field, 0.0, 1.0)), 0.0, 1.0)));
      q = rot2(p, ang);
      q *= 1.0 + 0.32 * sin(lr * 4.38 - T);
      q = q.yx; }

    // ================= S4 =================
    {   float _e = smoothstep(0.18, 0.04, length(fract(q * 4.02) - 0.5));
      field = mix(field * 0.63, 1.0, _e); }

    // ================= S5 =================
    vec3 col;
    { col = mix(vec3(0.0), (pal(a / TAU * 1.0 + (mix(smoothstep(-0.46, 0.46, -1.10) * 2.0 - 1.0, ((field) + (T)), clamp(0.5 + 0.5 * smoothstep(-0.22, 0.22, 1.01) * 2.0 - 1.0, 0.0, 1.0))) * 0.58 + T * 0.12 + FX_HUE, vec3(0.0, 1.0, 2.0) / 3.0 + vec3(0.11, -0.05, 0.12)) * mix(vec3(1.0), vec3(0.04, 0.90, 0.57), 0.47)), smoothstep(0.18, 0.51, abs(field))); }

    // ---- 静默压暗：空闲时只有 FX_DIM 的亮度，鼓点把周期速率推到 1 ----
    // 用 uHitFlow（周期速率 0..1，平滑起落）而不是原始尖峰 H ——
    // H 在鼓点那一帧从 0 跳到 1，亮度会"啪"一下再掉回去。
    col *= FX_DIM + (1.0 - FX_DIM) * uHitFlow;

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
