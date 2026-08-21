#version 330 core
// blend.frag — 7 种 Photoshop 混合模式，1:1 匹配 CPU BlendChannel()
// 由 PLRender 在非 BLUR 状态下调用，合成底层 + 上层效果
in vec2 vUV;
out vec4 c;
uniform sampler2D uBase;
uniform sampler2D uOverlay;
uniform int uMode;
uniform float uOpacity;

// 单通道 Photoshop 混合公式（与 CPU main.cpp BlendChannel 完全一致）
float ch(float a, float b, int m) {
    if (m == 1) return max(a, b);                                    // Lighten
    if (m == 2) return 1.0 - (1.0 - a) * (1.0 - b);                // Screen
    if (m == 3) return a < 0.5 ? 2.0*a*b : 1.0-2.0*(1.0-a)*(1.0-b); // Overlay
    if (m == 4) return b < 0.5 ? 2.0*a*b : 1.0-2.0*(1.0-a)*(1.0-b); // HardLight
    if (m == 5) {                                                     // SoftLight
        if (b < 0.5)
            return a - (1.0 - 2.0*b) * a * (1.0 - a);
        else {
            float g = a <= 0.25 ? ((16.0*a - 12.0)*a + 4.0)*a : sqrt(a);
            return a + (2.0*b - 1.0) * (g - a);
        }
    }
    if (m == 6) return a + b - 1.0;                                  // LinearBurn
    if (m == 7) return a * b;                                        // Multiply
    return b;                                                         // Normal (fallback)
}

void main() {
    vec4 a = texture(uBase, vUV);
    vec4 b = texture(uOverlay, vUV);
    // 逐通道独立混合（=CPU 的 BlendChannel(br,tr,mode) × 3 通道）
    float mr = ch(a.r, b.r, uMode);
    float mg = ch(a.g, b.g, uMode);
    float mb = ch(a.b, b.b, uMode);
    // 不透明度插值（=CPU 的 rr = br + (mr - br) * opacity）
    c = vec4(mix(a.rgb, clamp(vec3(mr, mg, mb), 0.0, 1.0), uOpacity), 1.0);
}
