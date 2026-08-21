/**
 * @file MTH_C.h
 * @brief MTH_C（笛卡尔非对称）—— 4 代数形状 × 6 配色 × 5 矢量场（禁止中心/轴对称）
 *
 * 对应 MTH_C.frag 的 CPU 版。形状：非对称椭圆/三次曲线/偏心形线/非对称星形。
 */

#pragma once

#include "../IProceduralEffect.h"
#include <vector>
#include <cstdint>
#include <cmath>

class MTH_C : public IProceduralEffect {
public:
    enum Form { FORM_ELLIPSE=0, FORM_CUBIC, FORM_CARDIOID, FORM_STAR, FORM_COUNT };
    enum ColorScheme { COLOR_BLUE=0, COLOR_CYAN, COLOR_PURPLE, COLOR_GREEN, COLOR_RED, COLOR_KLEIN, COLOR_COUNT };
    enum Projection { PROJ_SOURCE=0, PROJ_SINK, PROJ_VORTEX, PROJ_SADDLE, PROJ_UNIFORM, PROJ_COUNT };

    MTH_C();
    ~MTH_C() override = default;

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

    static constexpr float TWIST = 0.75f;
    static constexpr float FREQ_LOW = 0.05f;
    static constexpr float FREQ_HIGH = 3.20f;
    static constexpr float THICKNESS = 0.25f;
    static constexpr float SHARPNESS = 0.85f;
    static constexpr float OFFSET_X = -0.05f;
    static constexpr float OFFSET_Y = 0.05f;
    static constexpr float ROTATION = 0.0f;

    float m_time=0, m_bpm=120, m_beatPhase=0;
    bool  m_beatTrigger=false;
    Form  m_currentForm=FORM_ELLIPSE, m_targetForm=FORM_ELLIPSE;
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

    float ShapeField(Form f, float tx, float ty) const;
    void  WarpCoord(float xNorm, float yNorm, float t, float speed, float& tx, float& ty) const;

    struct LUTPoint { float xNorm, yNorm; };
    std::vector<LUTPoint> m_lut;
    std::vector<uint32_t> m_renderBuf;
    int m_renderW=0, m_renderH=0, m_fullW=0, m_fullH=0;

    void RebuildLUT(int fw, int fh);
    void RenderChunk(int start, int end, Form cur, Form tgt,
                     float blend, float t, float speed);
};
