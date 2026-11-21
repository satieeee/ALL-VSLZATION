#version 330 core
// ============================================================================
// Rosette — 由 layers.html 的配方直接拼装（花瓣 + 节点）
//
//   LAYERS S0=3(软化对数)  S1=4(花瓣)  S2=1(复合谐波)
//          S3=3(双旋镜像)  S4=4(自定义)  S5=5(自定义)
//
//   滑块：密度 12 · 对称 4 · 扭转 0.95 · 剪切 0（！）· 色相 0.23 · 中心核 0
//
//   注意 S3「双旋镜像」的旋转量整体乘以 uRoll，而滑块是 0 ——
//   所以它实际只做了 q.x = -q.x 这一步镜像，没有旋转。
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
#define FX_SYM 4.0        // 对称阶数（uSym）
#endif
#ifndef FX_ROLL
#define FX_ROLL 0.0       // 剪切（uRoll）—— 工具里是 0
#endif
#ifndef FX_HUE
#define FX_HUE 0.23       // 色相（uHue）
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
#ifndef FX_GRID
#define FX_GRID 0.0       // 层 4 的均匀网格圆：0 = 去掉（默认），1 = 恢复原配方
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

    // ================= 层 0 · 软化对数 =================
    float rad = max(length(p), 1e-4);
    float th  = atan(p.y, p.x);
    float lr  = log(rad + 0.35);
    float a   = th * FX_SYM;
    vec2  q   = p;

#if FX_STAGE >= 1
    // ================= 层 1 · 花瓣 =================
    field = sin(a + lr * FX_DENS * 0.5) * (0.6 + 0.4 * sin(th * 3.0));
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
    // ================= 层 3 · 双旋镜像 =================
    {
        float ang = 3.0 * sin(2.2 * lr * FX_DENS - T * 0.8) * FX_ROLL;
        q = rot2(p, ang * 0.5);
        q.x = -q.x;
    }
#endif

#if FX_STAGE >= 4
    // ================= 层 4 · 实体雕刻 =================
    {
        // 中心实体
        float _e = smoothstep(0.0, 0.09, disc(q, 0.14));
        // 均匀网格圆（fract 格子）：默认关掉，要恢复就 FX_GRID=1
        if (FX_GRID > 0.5)
            _e = max(_e, smoothstep(0.19, 0.04, length(fract(q * 3.78) - 0.5)));
        field = mix(field * 0.51, 1.0, _e);
    }
#endif

    // ================= 层 5 · 双色 =================
    {
        float _u = 0.5 + 0.5 * sin(TAU * (a / TAU * 3.0
                 + (sin(mix(FX_DENS, sin(a * 3.0), clamp(0.5 + 0.5 * lr, 0.0, 1.0)))) * 0.42
                 + T * 0.06 + FX_HUE));
        vec3 col5 = mix(vec3(0.99, 0.16, 0.35), vec3(0.99, 0.19, 0.31), _u)
                  * (0.55 + 0.45 * field);

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
