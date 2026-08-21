/**
 * @file Psychedelic.h
 * @brief 致幻剂 —— 6形态 × 6配色 × 6投影 × BPM驱动 × 多线程渲染
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
#include <algorithm>

class Psychedelic : public IProceduralEffect {
public:
    enum Form {
        FORM_VEINS=0, FORM_MYCELIUM, FORM_TENDRILS,
        FORM_STARBURST, FORM_MANDALA, FORM_FRACTAL, FORM_COUNT
    };
    enum ColorScheme {
        COLOR_NEON_GREEN=0, COLOR_TOXIC, COLOR_OCEAN,
        COLOR_INFERNO, COLOR_COSMIC, COLOR_PSYCHEDELIC, COLOR_COUNT
    };
    enum Projection {
        PROJ_RADIAL=0, PROJ_LINEAR_WATER, PROJ_SPIRAL_IN,
        PROJ_SPIRAL_OUT, PROJ_ZIGZAG, PROJ_TUNNEL_DEPTH, PROJ_COUNT
    };

    Psychedelic();
    ~Psychedelic() override = default;

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
    void SetBPM(float bpm);
    float GetBPM() const { return m_bpm; }
    void NextForm();
    void NextColorScheme();
    void NextProjection();
    void SetManualMode(bool m);
    void SyncBeatState(float bpm, float phase, bool triggered) override;
    void SetUserSpeed(float multiplier) override { m_userSpeed = multiplier; }
    void SetDarkMode(bool d) override { m_darkBg = d; }
    bool IsDarkMode() const override { return m_darkBg; }
    const wchar_t* GetFormName() const;
    const wchar_t* GetColorSchemeName() const;
    const wchar_t* GetProjectionName() const;

private:
    static constexpr int    QUALITY  = 2;
    static constexpr float  FORM_AUTO_BEATS = 1.0f;

    float  m_time = 0, m_bpm = 120, m_beatPhase = 0, m_beatInterval = 0.5f;
    bool   m_beatTrigger = false;
    int    m_beatCount = 0;
    bool   m_manualForm = false;
    Form   m_currentForm = FORM_VEINS, m_targetForm = FORM_VEINS;
    float  m_formBlend = 1, m_formBlendSpeed = 0.05f;
    ColorScheme m_colorScheme = COLOR_NEON_GREEN;
    Projection  m_projection = PROJ_RADIAL;

    PrecompData m_pc;
    float m_userSpeed = 1.0f;
    bool  m_darkBg = false;

    // ---- 辅助函数 ----
    void ComputeProjectedCoords(float xNorm, float yNorm,
                                float& r, float& theta) const;

    static uint32_t ARGB(uint8_t r, uint8_t g, uint8_t b) {
        return 0xFF000000 | (r<<16) | (g<<8) | b;
    }
    static void HSLtoRGB(float h, float s, float l, uint8_t& r, uint8_t& g, uint8_t& b);
    float EvalForm(Form form, float r, float theta, float t, float speed,
                   const PrecompData& pc) const;
    uint32_t MapColor(ColorScheme cs, float pattern, float r, float theta,
                      const PrecompData& pc, float bpm, float phase) const;
    void MapProjection(Projection proj, float xNorm, float yNorm,
                       const PrecompData& pc, float& r, float& theta) const;

    // ---- 独立渲染（Render 内部使用）----
    struct LUTPoint { float xNorm, yNorm, r, theta; };
    std::vector<LUTPoint> m_lut;
    std::vector<uint32_t> m_renderBuf;
    int m_renderW=0, m_renderH=0, m_fullW=0, m_fullH=0;

    void RebuildLUT(int fullW, int fullH);
    void RenderChunk(int startIdx, int endIdx,
                     Projection proj, Form curForm, Form tgtForm,
                     float blend, float bpm, float phase,
                     ColorScheme cs, const PrecompData& pc,
                     float t, float speed);
};
