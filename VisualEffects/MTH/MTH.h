/**
 * @file MTH.h
 * @brief MTH（极坐标线条）—— 4 形态 × 6 配色 × 5 矢量场 × SDF 细线
 *
 * 种子驱动线条生成器的 CPU 版（对应 MTH.frag）。
 * 形态：螺旋/玫瑰/径向/阿基米德螺旋；矢量场：源/汇/漩涡/鞍点/均匀流。
 * 连续种子参数硬编码自 y2k_liquid_chrome_polar。
 */

#pragma once

#include "../IProceduralEffect.h"
#include <vector>
#include <cstdint>
#include <cmath>

class MTH : public IProceduralEffect {
public:
    enum Form { FORM_SPIRAL=0, FORM_ROSE, FORM_RADIAL, FORM_ARCHIMEDEAN, FORM_COUNT };
    enum ColorScheme { COLOR_BLUE=0, COLOR_CYAN, COLOR_PURPLE, COLOR_GREEN, COLOR_RED, COLOR_KLEIN, COLOR_COUNT };
    enum Projection { PROJ_SOURCE=0, PROJ_SINK, PROJ_VORTEX, PROJ_SADDLE, PROJ_UNIFORM, PROJ_COUNT };

    MTH();
    ~MTH() override = default;

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

    void SetUserSpeed(float m) override { m_userSpeed = m; }
    void SetDarkMode(bool d) override { m_darkBg = d; }
    bool IsDarkMode() const override { return m_darkBg; }
    void NextForm(); void NextColorScheme(); void NextProjection();
    void SyncBeatState(float bpm, float phase, bool triggered) override;
    const wchar_t* GetFormName() const;
    const wchar_t* GetColorSchemeName() const;
    const wchar_t* GetProjectionName() const;

private:
    static constexpr int QUALITY = 2;

    // 种子连续参数（硬编码自 y2k_liquid_chrome_polar）
    static constexpr float ARM_COUNT = 8.0f;
    static constexpr float SPIRAL_GROWTH = 0.22f;
    static constexpr float TWIST = 0.75f;
    static constexpr float THICKNESS = 0.25f;
    static constexpr float SHARPNESS = 0.85f;
    static constexpr float OFFSET_X = -0.05f;
    static constexpr float OFFSET_Y = 0.05f;

    float m_time=0, m_bpm=120, m_beatPhase=0;
    bool  m_beatTrigger=false;
    Form  m_currentForm=FORM_SPIRAL, m_targetForm=FORM_SPIRAL;
    float m_formBlend=1, m_formBlendSpeed=0.05f;
    ColorScheme m_colorScheme=COLOR_BLUE;
    Projection  m_projection=PROJ_SOURCE;
    float m_userSpeed=1.0f;
    bool  m_darkBg=false;
    PrecompData m_pc;

    static uint32_t ARGB(uint8_t r, uint8_t g, uint8_t b) {
        return 0xFF000000 | (r<<16) | (g<<8) | b;
    }
    static void HSLtoRGB(float h, float s, float l, uint8_t& r, uint8_t& g, uint8_t& b);
    static float Smoothstep(float e0, float e1, float x);

    float PatternField(Form f, float r, float theta, float t, float speed) const;
    void  MapProjection(Projection p, float& r, float& theta, float t) const;

    struct LUTPoint { float xNorm, yNorm, r, theta; };
    std::vector<LUTPoint> m_lut;
    std::vector<uint32_t> m_renderBuf;
    int m_renderW=0, m_renderH=0, m_fullW=0, m_fullH=0;

    void RebuildLUT(int fw, int fh);
    void RenderChunk(int start, int end, Projection proj, Form cur, Form tgt,
                     float blend, float t, float speed);
};
