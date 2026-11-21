/**
 * @file B376.h
 * @brief b376 参考图轮廓效果 —— 全部 1202 条 VTracer 轮廓（烘焙场）+ 表层明暗着色 + 类圆变速运动
 * 场由 gen_b376_field.py 烘焙进 B376Field.h（覆盖+明暗+部件ID+主轴）。运动：每部件沿自身主轴
 * 按 (1-cos)/2 变速曲线滑出回位（慢→快→慢→原状），每帧 B376DisplaceFields 前向散射重算位移场。
 * 无音频绑定。
 */
#pragma once
#include "../IProceduralEffect.h"
#include <vector>
#include <cstdint>

class B376 : public IProceduralEffect {
public:
    B376();
    const wchar_t* GetName() const override;
    int GetQuality() const override { return QUALITY; }
    void Render(HDC hdc, const RECT& rect, const float*, int, const float*, int) override;

    // ---- IProceduralEffect ----
    void PrepareFrame(float dt, float bpm) override;
    float EvalPattern(float xNorm, float yNorm) const override;
    uint32_t MapToColor(float pattern, float xNorm, float yNorm, float beatPhase) const override;
    const PrecompData& GetPrecomp() const override { return m_pc; }

    // ---- 控制 ----
    void SyncBeatState(float bpm, float phase, bool triggered) override;
    void SetAudio(const AudioFrame& af) override { m_af = af; }
    void SetUserSpeed(float m) override { m_userSpeed = m; }
    static void SetFlowAmp(float a) { s_flowAmp = a; }   // 液态流动幅度（播放链滑杆）
    void SetDarkMode(bool d) override { m_darkBg = d; }
    bool IsDarkMode() const override { return m_darkBg; }
    void NextForm() {}   // 单形态（全部轮廓），无切换
    const wchar_t* GetFormName() const;

private:
    static constexpr int QUALITY = 2;
    float m_time = 0, m_bpm = 120, m_beatPhase = 0;
    AudioFrame m_af;
    float m_userSpeed = 1.0f;
    static float s_flowAmp;      // 液态流动幅度 0..1（主窗口滑杆，GPU/CPU 共享）
    bool  m_darkBg = false;
    PrecompData m_pc;
    std::vector<uint32_t> m_buf;
    std::vector<unsigned char> m_covDisp;   // 位移后覆盖场（每帧 B376DisplaceFields 散射）
    std::vector<unsigned char> m_lumDisp;   // 位移后明暗场
    std::vector<unsigned char> m_arcDisp;   // 位移后弧长场（描边流光）
    int m_renderW = 0, m_renderH = 0;

    void RebuildBuf(int fw, int fh);
    void ToField(float x, float y, float& u, float& v) const;   // 视口归一化 → 场内 [0,1]
    static float Sample(const std::vector<unsigned char>& f, int fw, int fh, float u, float v);  // 双线性（GL_LINEAR 同源）
    float EvalSurface(float xNorm, float yNorm) const;          // 表层明暗 0..1（位移场采样）
    void RenderChunk(int start, int end);
    static uint32_t ARGB(uint8_t r, uint8_t g, uint8_t b) { return 0xFF000000 | (r<<16) | (g<<8) | b; }
    static void HSLtoRGB(float h, float s, float l, uint8_t& r, uint8_t& g, uint8_t& b);
};
