/**
 * @file Verdigris.h
 * @brief Verdigris（铜绿）—— 6形态 × 6配色 × 6投影 × BPM驱动 × 多线程渲染
 *
 * 基于 5 张参考图的 BATCH SUMMARY 数据生成：
 *   色彩：低饱和蓝绿系 (H~128° S~0.17 L~0.60)，调色板紧凑 (diversity 0.13)
 *   边缘：中等密度 0.19，各向同性 (concentration 0.14)，柔和
 *   纹理：极简平滑 (complexity 0.053)，低频为主
 *   构图：居中 (0.52, 0.48)
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

class Verdigris : public IProceduralEffect {
public:
    // 6 形态 —— 全部全方向对称，匹配各向同性边缘数据
    enum Form {
        FORM_RIPPLE=0,    // 同心涟漪（圆环扩散）
        FORM_DRIFT,       // 柔缓漂移（低频流动曲线）
        FORM_BLOOM,       // 绽放（径向花瓣）
        FORM_VEIL,        // 薄纱（层叠透明帘幕）
        FORM_MIST,        // 薄雾（扩散散射场）
        FORM_CURRENT,     // 暗流（流动流线）
        FORM_COUNT
    };

    // 6 配色 —— 蓝绿区间，低饱和，调色板紧凑
    enum ColorScheme {
        COLOR_PATINA_BLUE=0,   // 铜蓝：H~215° S~0.17 L~0.54
        COLOR_VERDIGRIS_GREEN, // 铜绿：H~117° S~0.09 L~0.68
        COLOR_SLATE_STEEL,     // 蓝灰：H~200° S~0.05 L~0.50
        COLOR_MINT_FROST,      // 薄荷霜：H~160° S~0.12 L~0.85
        COLOR_DEEP_WATER,      // 深水：H~210° S~0.25 L~0.45
        COLOR_ASHEN_OLIVE,     // 灰橄榄：H~93° S~0.10 L~0.60
        COLOR_COUNT
    };

    // 6 投影 —— 居中构图，柔和变形
    enum Projection {
        PROJ_RADIAL=0,     // 圆形万花筒
        PROJ_SOFT_SPIRAL,  // 柔螺旋（低旋量）
        PROJ_HORIZON_FLOW, // 水平缓流
        PROJ_CENTER_BLOOM, // 中心绽放（径向加权）
        PROJ_DIAGONAL_SHEAR, // 对角线微切
        PROJ_TIDAL_SWAY,   // 潮汐摆动（波形扭曲）
        PROJ_COUNT
    };

    Verdigris();
    ~Verdigris() override = default;

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
    // 纹理复杂度 0.053 → QUALITY=3（更低分辨率无可见损失）
    static constexpr int    QUALITY  = 2;
    static constexpr float  FORM_AUTO_BEATS = 1.0f;

    float  m_time = 0, m_bpm = 120, m_beatPhase = 0, m_beatInterval = 0.5f;
    bool   m_beatTrigger = false;
    int    m_beatCount = 0;
    bool   m_manualForm = false;
    Form   m_currentForm = FORM_RIPPLE, m_targetForm = FORM_RIPPLE;
    float  m_formBlend = 1, m_formBlendSpeed = 0.05f;
    ColorScheme m_colorScheme = COLOR_PATINA_BLUE;
    Projection  m_projection = PROJ_RADIAL;

    PrecompData m_pc;
    float m_userSpeed = 1.0f; bool m_darkBg = false;

    // ---- 辅助函数 ----
    void ComputeProjectedCoords(float xNorm, float yNorm,
                                float& r, float& theta) const;

    static uint32_t ARGB(uint8_t r, uint8_t g, uint8_t b) {
        return 0xFF000000 | (r<<16) | (g<<8) | b;
    }
    static void HSLtoRGB(float h, float s, float l, uint8_t& r, uint8_t& g, uint8_t& b);

    // 形态函数 —— 纹理复杂度 0.053: 1~2 层三角函数叠加（非多层嵌套）
    float EvalForm(Form form, float r, float theta, float t, float speed,
                   const PrecompData& pc) const;

    // 配色映射 —— 低饱和度、白底大淡区
    uint32_t MapColor(ColorScheme cs, float pattern, float r, float theta,
                      const PrecompData& pc, float bpm, float phase) const;

    // 投影变换
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
