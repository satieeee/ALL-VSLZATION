#version 330 core
// ============================================================================
// Marbleflow — 由 layers.html 的配方拼装（六层全是自定义，全新配方）
//
//   画面：同心螺旋的玛瑙状条带，条带上有细密的径向尖刺，圆环上挂着一圈小圆点。
//   一对对撞色约 16.7 秒在色轮上转一圈（青/橙 → 绿/品红 → …），
//   另有约 5.24 秒一次的整幅跳变，来自层 0 里被 tanh 压住的 thc。
//
//   LAYERS  S0=4  S1=5  S2=5  S3=4  S4=4  S5=5
//   KNOBS   time=28.14 dens=4.80 sym=1.00 twist=3.00 roll=0.45 hue=0.50 core=0.00
//
//   层 0  指数径向 + thc 跳变   lr = exp(1.98·rad) − 9.005，再叠三项角度/时间波
//   层 1  fract 折回带          field = fract(T·3.3 + lr) 混 sin(a + 1.92·lr)
//   层 2  双路调制              field *= 0.77 + 0.38·(0.5+0.5·_w)
//   层 3  旋转 + 径向缩放       q = rot2(p, 0.56·roll·T + 4a + …)，再整体缩放
//   层 4  网格点 ∩ 圆环         q 空间里 4.92 网格圆点与 |q|=0.40 环的交集
//   层 5  绿橙对撞 + 高光       两个锚点随 T 在色轮上转（A/B 差半圈）
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
#define FX_DENS 4.80      // 密度（uDens）
#endif
#ifndef FX_SYM
#define FX_SYM 1.0        // 对称阶数（uSym）
#endif
#ifndef FX_TWIST
#define FX_TWIST 3.00     // 扭转（uTwist）—— 这条配方在 S0 里用到
#endif
#ifndef FX_ROLL
#define FX_ROLL 0.45      // 剪切强度（uRoll）
#endif
#ifndef FX_HUE
#define FX_HUE 0.50       // 色相（uHue）
#endif
#ifndef FX_CORE
#define FX_CORE 0.0       // 中心核（uCore）
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
    float rad = max(length(p), 10e-6);
    float th  = atan(p.y, p.x);
    // 开头那个 thc(T*0.6, twist) 会在 T = 5.24·k 处爆掉，但外面套了 tanh，被压回 ±1，
    // 所以 lr 不是无限大，而是每隔约 5.24 秒整体跳一下（下面 5.24 秒周期的脉动就是它）。
    float lr  = (exp(clamp(rad * 1.98, 0.0, 10.0)) - 9.005) * 1.10
              + thc(2.0 * tanh(thc(T * 0.6, FX_TWIST)), 6.5)
              + 0.29 * cos(th * 3.0 + T)
              + 0.31 * cos(th * 3.0);
    float a   = th * FX_SYM;
    vec2  q   = p;

#if FX_STAGE >= 1
    // ================= 层 1 · 自定义（fract 折回带）=================
    // clamp(x, 20.0, 1.0) 的下限比上限大 —— GLSL 里这是未定义行为，NVIDIA 上等价于
    // min(max(x,20),1) = 1，所以那一支实际上恒定取 1，整条 mix 退化成 sin(a + 1.92·lr)。
    field = fract(mix(((T * 3.3) + (lr)),
                      mix(FX_DENS, sin(a + lr * 1.92), clamp(0.5 + 0.5 * rad, 20.0, 1.0)),
                      clamp(0.5 + 0.5 * (FX_DENS * 1.58), 0.0, 1.0))) * 2.0 - 1.0;
#endif

#if FX_STAGE >= 2
    // ================= 层 2 · 自定义（双路调制）=================
    {
        float _w = mix(pow(abs(lr) + 0.05, 1.18) * 2.0 - 2.0,
                       thc(a, 0.02) * T,
                       clamp(10.5 + 0.5 * cos(a), 0.0, 0.005));   // 权重被夹到 ≤0.005，几乎全走第一支
        field *= 0.77 + 0.38 * (0.5 + 0.5 * _w);
    }
#endif

#if FX_STAGE >= 3
    // ================= 层 3 · 自定义（旋转 + 径向缩放）=================
    {
        float ang = T * 0.56 * FX_ROLL + a * 4.0
                  + 3.98 * sin((fract(sin(a * 2.0)) * 2.0 - 1.0) * cos(rad))
                  + 2.43 * sin(smoothstep(-0.51, 0.51, rad * lr) * 2.0 - 1.0);
        q  = rot2(p, ang);
        q *= 1.0 + 0.31 * sin(lr * 4.63 - T);
    }
#endif

#if FX_STAGE >= 4
    // ================= 层 4 · 自定义（网格点 ∩ 圆环）=================
    {
        float _e = min(smoothstep(0.16, 0.04, length(fract(q * 4.92) - 0.5)),
                       smoothstep(0.17, 0.02, abs(length(q) - 0.40)));
        field = mix(field * 0.42, 1.0, _e);
    }
#endif

    // ================= 层 5 · 上色 =================
    {
        // 取色参数 t：a/TAU·2 是整数圈（θ=±π 两侧无缝），T·0.08 是绿↔橙呼吸，FX_HUE 是整体偏移。
        // 注意分母里的 abs(T)：调制项会随时间衰减，运行久了图案会变平。
        float t = 0.5 + 0.5 * sin(TAU * (a / TAU * 2.0
                  + (sin(a + lr * 1.21) / (0.4 + abs(T)))
                    * (pow(abs(sin(a + lr * 2.81)) + 0.05, 1.14) * 2.0 - 1.0) * 0.51
                  + T * 0.08 + FX_HUE));

        // A 以 T·0.06 匀速转（≈16.7 秒一圈）；B 走的是 thc(T, twist·0.2) —— 一个会在
        // T = kπ（每约 3.14 秒）发散的量，所以 B 的颜色会不定时地跳到色轮上的随机位置，
        // 与 A 不再保持恒定的半圈关系。这是配方本身的设定，不是笔误。
        vec3 A = 0.5 + 0.5 * sin(TAU * (T * 0.06 + vec3(0.0, 1.0, 2.0) / 3.0));
        vec3 B = 0.5 + 0.5 * sin(TAU * (thc(T, FX_TWIST * 0.2) * 0.06 + 0.5 + vec3(0.0, 1.0, 2.0) / 3.0));

        vec3 col = clamp((mix(A, B, t) * (0.63 + 0.30 * field)
                        + vec3(0.41, 0.97, 0.12) * pow(max(field, 0.0), 7.81) * 0.43) * 1.10,
                         0.0, 1.0);

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
