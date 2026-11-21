#version 330 core
// ============================================================================
// Silkwheel — 由 layers.html 的配方拼装（六层全是自定义）
//
//   LAYERS  S0=4  S1=5  S2=5  S3=4  S4=4  S5=5
//   KNOBS   time=8.86 dens=9.70 sym=2.00 twist=1.95 roll=0.55 hue=0.49 core=0.00
//
//   画面：一层缓慢转动的丝质风车 —— 四枚叶瓣之间夹着黑色透镜状的空隙。
//   颜色随时间在【浅粉/青/薄荷的柔和彩虹】与【高饱和霓虹】之间来回摆动
//   （相位里的 T·0.12 一直累积，不是周期重复的）。
//
//   层 0  线性径向       lr = rad · 3.37
//   层 1  pow 压缩条带   pow(|lr·sin(a + 2.12lr)| + 0.05, 0.073) ▸ 近常数平台
//   层 2  噪声调相       field ← sin(2.12·field + 1.67·_w + a)，_w 来自 sin 哈希
//   层 3  差速旋转       q = rot2(p, 0.39·roll·T + 2a)  → 空间被剪成螺旋
//   层 4  网格点 ∩ 横带  乘 |q.y| < 0.03 的横条 → 只剩中间一条虚线
//   层 5  扫色 + 锐高光   pal(a/TAU·2 + cos(dens)·cos(th)·0.57 + 0.12T + hue)
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
#define FX_DENS 9.70      // 密度（uDens）
#endif
#ifndef FX_SYM
#define FX_SYM 2.0        // 对称阶数（uSym）
#endif
#ifndef FX_ROLL
#define FX_ROLL 0.55      // 剪切强度（uRoll）
#endif
#ifndef FX_HUE
#define FX_HUE 0.49       // 色相（uHue）
#endif
#ifndef FX_CORE
#define FX_CORE 0.0       // 中心核（uCore）
#endif
#ifndef FX_VIG
#define FX_VIG 1.75
#endif
#ifndef FX_COVER
#define FX_COVER 2.6      // 目前没用到：S5 自画底色，cover 恒为 1（见文件末尾）
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
    // ================= 层 1 · 自定义（pow 压缩条带）=================
    // 内层引用的是【层 1 之前】的 field（=0），那颗 pow(|0|+0.05, 1.59) 只是个常数偏置，
    // 真正决定形状的是分子里的 |lr·sin(a+2.12lr)|。压缩到 0.073 次方后几乎是平台，
    // 所以这一层的输出基本是【正的、大片同值】——它只是给层 2 的 sin 一个慢变的相位。
    field = ((pow(abs(lr * sin(a + lr * 2.12)) + 0.05, 0.073) * 2.0 - 1.0)
           / (0.4 + abs((pow(abs(field) + 0.05, 1.59) * 2.0 - 1.0)
                      / (0.4 + abs(sin(lr + th))))));
#endif

#if FX_STAGE >= 2
    // ================= 层 2 · 自定义（哈希噪声调相）=================
    {
        float _w = fract(sin(rad + T / 10.0 * 4.0)) * 2.0 - 1.0;
        field = sin(field * 2.12 + _w * 1.67 + a);
    }
#endif

#if FX_STAGE >= 3
    // ================= 层 3 · 自定义（差速旋转）=================
    // 转角里带 a（= 2θ），所以半径相同、角度不同的点转得不一样 → 空间被剪成螺旋。
    {
        float ang = T * 0.39 * FX_ROLL + a * 2.0;
        q = rot2(p, ang);
    }
#endif

#if FX_STAGE >= 4
    // ================= 层 4 · 自定义（网格点 ∩ 横带）=================
    // 三样相乘：3.87 网格里的圆点 ∩ 过原点的两条径向线（合起来是一条直径）∩ |q.y|<0.03 的横带。
    // 结果不是满屏点阵，而是藏在中间的一条虚线；q 已被层 3 剪过，所以这条虚线随场一起扭。
    {
        float _e = min(smoothstep(0.14, 0.04, length(fract(q * 3.87) - 0.5)),
                       smoothstep(0.02, 0.0, abs(fract(atan(q.y, q.x) / TAU * 2.0 + 0.5) - 0.5)))
                 * smoothstep(0.012, 0.0, abs(q.y) - 0.03);
        field = mix(field * 0.41, 1.0, _e);
    }
#endif

    // ================= 层 5 · 上色 =================
    {
        // 相位里 a/TAU·2 是整数圈的（θ=±π 两侧差 4 整圈，无缝）；
        // cos(th) 偶函数，在 ±π 处也连续 —— 这条配方没有分支切割的接缝问题。
        vec3 c0 = pal(a / TAU * 2.0 + cos(FX_DENS) * cos(th) * 0.57 + T * 0.12 + FX_HUE,
                      vec3(0.0, 1.0, 2.0) / 3.0 + vec3(0.10, 0.03, -0.09))
                + vec3(0.00, 0.78, 0.72) * pow(max(field, 0.0), 8.21) * 0.79
                + thc(T * 0.5, 2.6);
        // 底色 0.2（不是纯黑），遮罩门槛按 field+0.2 算 —— 暗部的宽度和位置都会变，
        // 叶瓣之间的空隙不再是"挖空"，而是压成一层深灰。
        float m5  = smoothstep(0.10, 0.68, abs(field + 0.2));
        vec3 col = mix(vec3(0.2), c0, m5);

        glow = smoothstep(0.0, 3.2, -lr) * FX_CORE;
        glow *= 1.0 + uBeatFlash * 0.9 + uHitKick * 1.2;
        col   = mix(col, vec3(1.0, 0.94, 0.88) * 2.0, glow);
        col  *= smoothstep(0.0, 0.15, FX_VIG - length(p));
        col   = (col * (2.51 * col + 0.03)) / (col * (2.43 * col + 0.59) + 0.14);

        // ---- 工程协议（别动）----
        float pat   = clamp(field, -1.0, 1.0);
        // 底色跟着模式走：深色模式用配方里的 0.2 灰，浅色模式用工程的白 0.95。
        // cover 直接取 S5 自己的遮罩 —— 叶瓣处 =1（整块用 col），空隙处 =0（整块用 bg），
        // 两边的底色都能原样露出来。若用 |field|·0.9·FX_COVER 那套判据，空隙里
        // cover 只有 ~0.47，0.2 会被压到 0.14、白会被压到 0.64（既不是灰也不是白）。
        vec3  bg    = (uDarkBg > 0) ? vec3(0.2) : vec3(0.95);
        float cover = clamp(max(m5, glow), 0.0, 1.0);
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
