#version 330 core
// ============================================================================
// CpuArtifact.glsl — CPU 渲染质感模拟 + 画面锐化
// ============================================================================
// 还原 CPU GDI 管线的画面质感副产品:
//   1. 色阶量化 → 渐变可见阶梯状 banding
//   2. Bayer 8×8 有序抖动 → GDI HALFTONE 颗粒纹理
//   3. Laplacian 锐化 → 补偿降分辨率后 NEAREST 拉伸的模糊
//
// 用前须知:
//   · main.cpp 将此文件拼接在 effect .frag 前一起编译
//   · 每个 effect 在输出 fragColor 前加一行:
//     col = cpuFinalize(col, gl_FragCoord.xy, uDitherStrength);
//   · uEncode 路径（FBO 编码）有 early return，自动跳过此处
// ============================================================================

uniform float uDitherStrength;  // 质感强度 [0, 1]  0=纯GPU  0.7=标准  1.0=低保真

// ---------------------------------------------------------------------------
// bayer8 — 8×8 Bayer 有序抖动矩阵
//   正常化到 [0/64, 63/64]，输出 [0, 1)
//   这是 GDI StretchBlt(HALFTONE) 使用的同一类有序抖动算法
// ---------------------------------------------------------------------------
float bayer8(vec2 p) {
    int x = int(mod(p.x, 8.0));
    int y = int(mod(p.y, 8.0));
    int m[64] = int[64](
         0, 32,  8, 40,  2, 34, 10, 42,
        48, 16, 56, 24, 50, 18, 58, 26,
        12, 44,  4, 36, 14, 46,  6, 38,
        60, 28, 52, 20, 62, 30, 54, 22,
         3, 35, 11, 43,  1, 33,  9, 41,
        51, 19, 59, 27, 49, 17, 57, 25,
        15, 47,  7, 39, 13, 45,  5, 37,
        63, 31, 55, 23, 61, 29, 53, 21
    );
    return float(m[y * 8 + x]) / 64.0;
}

// ---------------------------------------------------------------------------
// cpuFinalize — GDI 质感管线
//
//   ditherStrength → 有效位深 → 色阶级数:
//     0.0  = 8-bit  → 256 级 (纯GPU, 无效果)
//     0.5  = 6.5bit → ~91 级 (微弱 banding + 颗粒)
//     0.7  = 5.9bit → ~60 级 (可见 banding + 可见 Bayer 纹理)
//     1.0  = 5-bit  →  32 级 (强烈低保真数字感)
//
//   处理顺序:
//     1. 色阶量化 → 制造 gradient banding
//     2. Bayer 有序抖动 → 在量化阶梯之间填充有序颗粒
//        R/G/B 三通道各自偏移采样 → 颜色更自然
// ---------------------------------------------------------------------------
vec3 cpuFinalize(vec3 color, vec2 fragCoord, float strength) {
    if (strength <= 0.0) return color;

    float levels = max(16.0, exp2(8.0 - strength * 3.8));
    color = floor(color * (levels - 1.0) + 0.5) / (levels - 1.0);

    float stepSize = 1.0 / (levels - 1.0);
    float dR = (bayer8(fragCoord)                  - 0.5) * stepSize * 3.0;
    float dG = (bayer8(fragCoord + vec2(3.0, 5.0)) - 0.5) * stepSize * 3.0;
    float dB = (bayer8(fragCoord + vec2(5.0, 3.0)) - 0.5) * stepSize * 3.0;
    color = clamp(color + vec3(dR, dG, dB), 0.0, 1.0);

    // Laplacian 锐化: 补偿降分辨率 NEAREST 拉伸的块状模糊
    //   dFdx/dFdy 取屏幕空间二阶导数检测边缘，反向叠加增强高频
    float sharpenAmt = strength * 0.35;
    vec3 lap = dFdx(dFdx(color)) + dFdy(dFdy(color));
    return clamp(color - lap * sharpenAmt, 0.0, 1.0);
}
