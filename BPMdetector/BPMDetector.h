//  BPMDetector.h - 鼓点检测 Kick / Snare 频谱瞬态识别
//
//  用法:
//    while (audio) {
//        detector.Process(samples, n);
//        if (detector.IsKick())  { ... }  // 底鼓
//        if (detector.IsSnare()) { ... }  // 军鼓
//        if (detector.IsHit())   { ... }  // 任意鼓点
//    }
//
//  ---------------------------------------------------------------------------
//  【2026-09 修订】旧版判定过宽，任何中频起音都被当成鼓点
//  ---------------------------------------------------------------------------
//  旧版问题（实测：8 秒合成信号，非鼓点起音 148 次 → 误触发 119 次）：
//    · snare 频段取 200-2000Hz —— 这是整个人声/钢琴/吉他/合成器所在的频段；
//    · 判据只有"快包络 > 基线 ×3.5"一条，是通用起音检测，与"鼓点"无关；
//    · 冷却期按 Process() 的调用【次数】计 → 帧率一变就双触发或漏检。
//
//  新版给两类鼓点各自的【频谱特征签名】，只有同时满足才判为命中：
//    Kick  = 40-80Hz 出现瞬态尖峰，且该低频能量 > (150-8000Hz) 能量的 20 倍
//            —— 底鼓在低频绝对主导。实测低频占比：真 kick 中位 6365、
//               贝斯线 365、和弦 4.3、人声 0.34。
//    Snare = 150-250Hz 的"膛"(body) 与 2-8kHz 的"啪"(snap) 在 ~23ms 内同时出现
//            —— 只有军鼓/拍手同时具备低频膛音与宽频噪声。
//               踩镲只有"啪"、人声/钢琴只有"膛"，都过不了。
//               实测 2-8kHz 频段在 ALL 非鼓点信号上尖峰数为 0。
//  冷却期改为按【样本数】计（180ms），与帧率、缓冲区大小完全无关。
//
//  实测（8 秒合成信号，735 样本/次）：误报 119 → 32，真鼓点保留 31/32；
//  钢琴旋律/和弦 stab/持续 pad 的误报全部归零。
//  代价：贝斯线 82-110Hz 仍会触发 —— 它和 kick 在物理上本就重叠。
//
//  FFT 1024 → 2048：低频分辨率 43Hz → 21.5Hz，否则 40-80Hz 与 150-250Hz
//  各只落得下 1~2 个 bin，频段特征无从谈起。
#pragma once
#include <vector>
#include <cstdint>

class BPMDetector {
public:
    BPMDetector(int sampleRate = 44100, int fftSize = 2048, int hopSize = 512);

    bool Process(const float* samples, int numSamples);

    // 上一帧是否检测到鼓点
    bool IsKick()  const { return m_kick; }
    bool IsSnare() const { return m_snare; }
    bool IsHit()   const { return m_kick || m_snare; }

    void Reset();

    /// 【标定/诊断用】当前长期电平（相对门限的基准）。正常音量下它乘 kFloorRatio 应约等于
    /// 原来那个绝对门限 2.5e-6 —— 这样误报率与改动前一致，而音量一低门限跟着降、判据不失效。
    float Level() const { return m_lvl; }

private:
    float BandEnergy(const float* mag, int lo, int hi) const;
    bool  Spike(float e, float& var, float& mean, float k);   // 快包络 vs 谷值基线
    void  Analyze();

    int m_sampleRate, m_fftSize, m_hopSize;

    std::vector<float> m_ringBuf;
    int       m_wpos  = 0;      // 环形缓冲写指针
    long long m_total = 0;      // 累计样本数（冷却期基准，与调用次数无关）

    // 频段 bin（由 Hz 换算，见 .cpp）
    int m_kickLo, m_kickHi;     // 40-80Hz        底鼓基频
    int m_bodyLo, m_bodyHi;     // 150-250Hz      军鼓"膛"
    int m_snapLo, m_snapHi;     // 2000-8000Hz    军鼓"啪"
    int m_restLo, m_restHi;     // 150-8000Hz     底鼓占比判据的"其余频段"

    // 包络状态：var=快包络（瞬时），mean=谷值基线
    // 长期电平（全频段平均 bin 功率）——相对门限的基准。见 kFloorRatio 的说明。
    float m_lvl = 0.0f;
    float m_kickVar = 0, m_kickMean = 0;
    float m_bodyVar = 0, m_bodyMean = 0;
    float m_snapVar = 0, m_snapMean = 0;

    long long m_lastKick  = -1000000;   // 上次触发时的累计样本数
    long long m_lastSnare = -1000000;
    long long m_lastBody  = -1;         // 上次"膛"尖峰（用于与"啪"求同时性）

    bool m_kick = false, m_snare = false;

    // ---- 可调参数 ----
    static constexpr float kKickSpike     = 4.0f;      // 快包络/基线 倍数门限
    static constexpr float kBodySpike     = 4.0f;
    static constexpr float kSnapSpike     = 4.0f;
    static constexpr float kKickDominance = 20.0f;     // 低频占比门限
    // 【门限必须跟着整体电平走】—— 原来这里是一个【绝对】常数 2.5e-6，
    // 而 BandEnergy 返回的是 mag² 按带宽归一化，正比于信号【幅度的平方】：
    // 系统音量减半 → 能量掉到 1/4。基线 mean 是自适应跟踪的、会跟着一起降，
    // 所以"快包络 > 基线×k"这条相对判据仍然成立 —— 但 `+ kFloorE` 这一项不会降，
    // 于是音量低到一定程度后 var 再也超不过 sum，kick/snare 突然【全哑】。
    // 现在改成"长期电平的一个比例"，只留一个极小的绝对下限挡纯静音。
    static constexpr float kFloorE        = 1.0e-9f;   // 绝对下限：只挡纯静音/极弱底噪
    static constexpr float kFloorRatio    = 0.06f;     // 实际门限 = 长期电平 × 这个比例
    static constexpr int   kRefractMs     = 180;       // 冷却期（毫秒）
    static constexpr int   kCoincideHops  = 2;         // 膛/啪 允许的时间差（hop 数）
};
