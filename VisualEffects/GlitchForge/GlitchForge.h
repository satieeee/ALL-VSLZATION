/**
 * @file GlitchForge.h
 * @brief 视频故障效果 —— 4种运动函数 × 6种配色 × 鼓点切换 × 多线程高画质渲染
 *
 * 实现 IProceduralEffect 接口，支持跨效果数学过渡。
 */

#pragma once

#include "../IProceduralEffect.h"
#include <vector>
#include <cstdint>
#include <cmath>
#include <string>
#include <thread>

class GlitchForge : public IProceduralEffect {
public:
    enum GlitchMode {
        SCANLINE_TEAR = 0,
        CHROMA_SPLIT,
        BLOCK_CORRUPT,
        WAVE_GLITCH,
        MODE_COUNT
    };
    enum ColorScheme {
        COLOR_GREEN_CRT = 0,
        COLOR_TOXIC_GLITCH,
        COLOR_VHS_BLEED,
        COLOR_THERMAL_CRASH,
        COLOR_NEON_DAMAGE,
        COLOR_DATA_RAINBOW,
        COLOR_COUNT
    };

    GlitchForge();
    ~GlitchForge() override = default;

    // ---- IVisualEffect ----
    const wchar_t* GetName() const override;
    int GetQuality() const override { return QUALITY; }
    void Render(HDC hdc, const RECT& rect,
                const float* spectrum, int spectrumBins,
                const float* waveform, int waveformSize) override;

    // ---- IProceduralEffect ----
    void PrepareFrame(float dt, float bpm) override;
    float EvalPattern(float xNorm, float yNorm) const override;
    uint32_t MapToColor(float pattern, float xNorm, float yNorm,
                        float beatPhase) const override;
    const PrecompData& GetPrecomp() const override { return m_pc; }

    // ---- 控制 ----
    void SyncBeatState(float bpm, float phase, bool triggered) override;
    void SetUserSpeed(float multiplier) override { m_userSpeed = multiplier; }
    void SetDarkMode(bool d) override { m_darkBg = d; }
    bool IsDarkMode() const override { return m_darkBg; }
    void NextMode();
    void NextColorScheme();
    float GetBPM() const { return m_bpm; }
    const wchar_t* GetModeName() const;
    const wchar_t* GetColorSchemeName() const;

private:
    static constexpr int   QUALITY = 1;
    static constexpr float MODE_AUTO_BEATS = 1.0f;

    float  m_time = 0, m_bpm = 120, m_beatPhase = 0, m_beatInterval = 0.5f;
    bool   m_beatTrigger = false;
    int    m_beatCount = 0;
    GlitchMode m_currentMode = SCANLINE_TEAR, m_targetMode = SCANLINE_TEAR;
    float  m_modeBlend = 1, m_modeBlendSpeed = 0.06f;
    ColorScheme m_colorScheme = COLOR_GREEN_CRT;

    PrecompData m_pc;
    float m_userSpeed = 1.0f; bool m_darkBg = false;

    // ---- 辅助 ----
    void ComputeDisplacedCoords(float xNorm, float yNorm,
                                float& r, float& theta, float& rDispR, float& rDispB) const;

    static uint32_t ARGB(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
        return (a << 24) | (r << 16) | (g << 8) | b;
    }
    static void HSLtoRGB(float h, float s, float l, uint8_t& r, uint8_t& g, uint8_t& b);

    float BasePattern(float r, float theta, float t, float speed, const PrecompData& pc) const;
    void  GlitchDisplace(GlitchMode mode, float r, float theta, float t,
                         const PrecompData& pc, float& dr, float& dtheta,
                         float& rDispR, float& rDispB) const;
    uint32_t MapGlitchColor(ColorScheme cs, float pat, float r, float theta,
                            float t, const PrecompData& pc, float phase) const;

    // ---- 独立渲染 ----
    struct KaleidoPoint { float r, theta; };
    std::vector<KaleidoPoint> m_kLut;
    std::vector<uint32_t> m_buf;
    int m_rw = 0, m_rh = 0, m_fw = 0, m_fh = 0;
    void RebuildBuf(int fw, int fh);
    void RenderChunk(int start, int end, GlitchMode cur, GlitchMode tgt,
                     float blend, float bpm, float phase,
                     ColorScheme cs, const PrecompData& pc, float t, float speed);
};
