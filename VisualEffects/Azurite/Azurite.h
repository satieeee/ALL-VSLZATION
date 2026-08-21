/**
 * @file Azurite.h
 * @brief Azurite（蓝铜矿）—— 6 高度场形态 × 6 配色 × 6 投影 × 伪3D光照 × 多线程
 *
 * BATCH SUMMARY (5 images):
 *   色彩：蓝 H~215° S~0.42 L~0.44  边缘 0.30  纹理 0.12  规律性 1.0
 *
 * 伪 3D 方案：形态函数输出高度场 h(x,y)，邻域采样求法线，Phong 光照着色
 */

#pragma once

#include "../IProceduralEffect.h"
#include <vector>
#include <cstdint>
#include <cmath>
#include <string>
#include <thread>
#include <algorithm>

class Azurite : public IProceduralEffect {
public:
    enum Form {
        FORM_STRATA=0,   // 层理 — 水平沉积层
        FORM_WEAVE,      // 编织 — 交错网格
        FORM_BLOCKS,     // 块垒 — 挤出方块
        FORM_RIPPLES,    // 涟漪 — 同心波
        FORM_PILLARS,    // 柱林 — 柱状阵列
        FORM_TERRAIN,    // 地形 — 流动起伏
        FORM_COUNT
    };
    enum ColorScheme {
        COLOR_AZURE_DEEP=0, COLOR_CYAN_FROST, COLOR_COBALT_NIGHT,
        COLOR_ICE_VEIN, COLOR_PATINA_FLOW, COLOR_INDIGO_MIST, COLOR_COUNT
    };
    enum Projection {
        PROJ_IDENTITY=0, PROJ_HORIZON, PROJ_VERTICAL,
        PROJ_DIAGONAL, PROJ_ROTATE, PROJ_WAVE, PROJ_COUNT
    };

    Azurite();
    ~Azurite() override = default;

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
    static constexpr int  QUALITY = 1;
    static constexpr float FORM_AUTO_BEATS = 1.0f;
    static constexpr float NORM_EPS = 0.002f;  // 法线计算步长

    float  m_time = 0, m_bpm = 120, m_beatPhase = 0;
    bool   m_beatTrigger = false;
    int    m_beatCount = 0;
    Form   m_currentForm = FORM_STRATA, m_targetForm = FORM_STRATA;
    float  m_formBlend = 1, m_formBlendSpeed = 0.05f;
    ColorScheme m_colorScheme = COLOR_AZURE_DEEP;
    Projection  m_projection = PROJ_IDENTITY;
    float  m_userSpeed = 1.0f;
    bool   m_darkBg = false;

    PrecompData m_pc;

    static uint32_t ARGB(uint8_t r, uint8_t g, uint8_t b) {
        return 0xFF000000 | (r<<16) | (g<<8) | b;
    }
    static void HSLtoRGB(float h, float s, float l, uint8_t& r, uint8_t& g, uint8_t& b);

    // 高度场函数（返回标量高度，用于伪 3D 着色）
    float HeightField(Form f, float x, float y, float t, float speed,
                      const PrecompData& pc) const;
    // 配色映射（高度 + 法线 → 色彩）
    uint32_t Shade(ColorScheme cs, float height, float nx, float ny, float nz,
                   const PrecompData& pc, float phase) const;
    void MapProjection(Projection proj, const PrecompData& pc,
                       float& x, float& y) const;

    struct LUTPoint { float xNorm, yNorm; };
    std::vector<LUTPoint> m_lut;
    std::vector<uint32_t> m_renderBuf;
    int m_rw=0, m_rh=0, m_fw=0, m_fh=0;

    void RebuildLUT(int fw, int fh);
    void RenderChunk(int start, int end, Projection proj, Form cur, Form tgt,
                     float blend, float bpm, float phase,
                     ColorScheme cs, const PrecompData& pc, float t, float speed);
};
