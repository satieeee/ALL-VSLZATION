/**
 * @file Azurite_c.h
 * @brief Azurite_c（蓝铜矿·极坐标）—— 6 高度场 × 6 配色 × 6 投影 × 伪3D × 万花筒
 *
 * Azurite 的极坐标万花筒版本。形态函数从笛卡尔 (x,y) 改为极坐标 (r,theta)，
 * LUT 预计算 10 楔形折叠。高度场 + 法线 + Phong 光照不变。
 */

#pragma once

#include "../IProceduralEffect.h"
#include <vector>
#include <cstdint>
#include <cmath>
#include <string>
#include <thread>
#include <algorithm>

class Azurite_c : public IProceduralEffect {
public:
    enum Form {
        FORM_STRATA=0, FORM_WEAVE, FORM_BLOCKS,
        FORM_RIPPLES, FORM_PILLARS, FORM_TERRAIN, FORM_COUNT
    };
    enum ColorScheme {
        COLOR_AZURE_DEEP=0, COLOR_CYAN_FROST, COLOR_COBALT_NIGHT,
        COLOR_ICE_VEIN, COLOR_PATINA_FLOW, COLOR_INDIGO_MIST, COLOR_COUNT
    };
    enum Projection {
        PROJ_RADIAL=0, PROJ_SOFT_SPIRAL, PROJ_CENTER_BLOOM,
        PROJ_HORIZON, PROJ_VERTICAL, PROJ_DIAGONAL, PROJ_COUNT
    };

    Azurite_c();
    ~Azurite_c() override = default;

    const wchar_t* GetName() const override;
    int GetQuality() const override { return QUALITY; }
    void Render(HDC hdc, const RECT& rect,
                const float* spectrum, int spectrumBins,
                const float* waveform, int waveformSize) override;

    void PrepareFrame(float dt, float bpm) override;
    float EvalPattern(float xNorm, float yNorm) const override;
    uint32_t MapToColor(float pattern, float xNorm, float yNorm,
                        float beatPhase) const override;
    const PrecompData& GetPrecomp() const override { return m_pc; }

    void SetBPM(float bpm);
    float GetBPM() const { return m_bpm; }
    void SetUserSpeed(float m) override { m_userSpeed = m; }
    void SetDarkMode(bool d) override { m_darkBg = d; }
    bool IsDarkMode() const override { return m_darkBg; }
    void NextForm(); void NextColorScheme(); void NextProjection();
    void SyncBeatState(float bpm, float phase, bool triggered) override;
    const wchar_t* GetFormName() const;
    const wchar_t* GetColorSchemeName() const;
    const wchar_t* GetProjectionName() const;

private:
    static constexpr int  QUALITY = 2;
    static constexpr float FORM_AUTO_BEATS = 1.0f;
    static constexpr float NORM_EPS_R = 0.003f, NORM_EPS_T = 0.0008f;

    float  m_time = 0, m_bpm = 120, m_beatPhase = 0;
    bool   m_beatTrigger = false;
    int    m_beatCount = 0;
    Form   m_currentForm = FORM_STRATA, m_targetForm = FORM_STRATA;
    float  m_formBlend = 1, m_formBlendSpeed = 0.05f;
    ColorScheme m_colorScheme = COLOR_AZURE_DEEP;
    Projection  m_projection = PROJ_RADIAL;
    float  m_userSpeed = 1.0f;
    bool   m_darkBg = false;

    PrecompData m_pc;

    static uint32_t ARGB(uint8_t r, uint8_t g, uint8_t b) {
        return 0xFF000000 | (r<<16) | (g<<8) | b;
    }
    static void HSLtoRGB(float h, float s, float l, uint8_t& r, uint8_t& g, uint8_t& b);

    float HeightField(Form f, float r, float theta, float t, float speed,
                      const PrecompData& pc) const;
    uint32_t Shade(ColorScheme cs, float h, float nx, float ny, float nz,
                   const PrecompData& pc, float phase) const;
    void MapProjection(Projection proj, const PrecompData& pc,
                       float& r, float& theta) const;

    struct LUTPoint { float xNorm, yNorm, r, theta; };
    std::vector<LUTPoint> m_lut;
    std::vector<uint32_t> m_renderBuf;
    int m_renderW=0, m_renderH=0, m_fullW=0, m_fullH=0;

    void RebuildLUT(int fw, int fh);
    void RenderChunk(int start, int end, Projection proj, Form cur, Form tgt,
                     float blend, float bpm, float phase,
                     ColorScheme cs, const PrecompData& pc, float t, float speed);
};
