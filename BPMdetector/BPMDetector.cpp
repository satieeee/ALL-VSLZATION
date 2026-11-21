/**
 * @file BPMDetector.cpp
 * @brief Kick/Snare 频谱特征识别
 *
 * 判定逻辑（详见 .h 顶部的修订说明）：
 *
 *   Kick  ── 两类条件同时成立：
 *     1. 40-80Hz 出现瞬态尖峰（快包络 > 谷值基线 × 4）
 *     2. 该低频能量 > (150-8000Hz) 能量 × 20
 *        底鼓在低频绝对主导；和弦/人声的起音虽也有低频溅射，但中高频更强。
 *
 *   Snare ── 两类条件同时成立（时间差 ≤ 2 个 hop ≈ 23ms）：
 *     1. 150-250Hz 的"膛"出现瞬态尖峰
 *     2. 2-8kHz 的"啪"出现瞬态尖峰
 *        军鼓 = 鼓膜的低频膛音 + 响弦的宽频噪声。踩镲只有"啪"、
 *        人声/钢琴只有"膛"，单一条件都不够。
 *
 * 尖峰判据 = 快包络（3 个 hop 时间常数）显著高于基线。
 * 基线跟踪【谷值】：能量低于基线时快速下降、高于基线时极慢上升，
 * 避免持续响度把基线抬高导致"响度大时比值塌缩漏检"。
 *
 * 冷却期按【样本数】计（不是 Process 调用次数），因此与帧率、
 * 音频缓冲区大小完全无关 —— 旧版在 8.3ms/次时同一个 kick 会触发两次，
 * 在 92.9ms/次时又会漏掉一半。
 */

#include "BPMDetector.h"
#include "../AudioCapture/FFTHelper.h"
#include <algorithm>
#include <cmath>

namespace {
/// 频段下界 → bin（含）
inline int BinLo(float hz, float binW, int half) {
    int b = (int)std::floor(hz / binW);
    if (b < 1) b = 1;            // 跳过 DC
    if (b > half) b = half;
    return b;
}
/// 频段上界 → bin（含）
inline int BinHi(float hz, float binW, int half) {
    int b = (int)std::ceil(hz / binW);
    if (b < 1) b = 1;
    if (b > half) b = half;      // 不超过 Nyquist
    return b;
}
}

BPMDetector::BPMDetector(int sr, int fft, int hop)
    : m_sampleRate(sr), m_fftSize(fft), m_hopSize(hop)
{
    if (m_fftSize < 64) m_fftSize = 64;
    if (m_hopSize < 1)  m_hopSize = m_fftSize / 4;
    m_ringBuf.assign(m_fftSize, 0.0f);

    const float binW = (float)m_sampleRate / (float)m_fftSize;
    const int   half = m_fftSize / 2;

    m_kickLo = BinLo(40.0f,   binW, half);  m_kickHi = BinHi(80.0f,   binW, half);
    m_bodyLo = BinLo(150.0f,  binW, half);  m_bodyHi = BinHi(250.0f,  binW, half);
    m_snapLo = BinLo(2000.0f, binW, half);  m_snapHi = BinHi(8000.0f, binW, half);
    m_restLo = BinLo(150.0f,  binW, half);  m_restHi = BinHi(8000.0f, binW, half);
}

void BPMDetector::Reset() {
    std::fill(m_ringBuf.begin(), m_ringBuf.end(), 0.0f);
    m_wpos = 0; m_total = 0;
    m_lvl = 0.0f;
    m_kickVar = m_kickMean = 0;
    m_bodyVar = m_bodyMean = 0;
    m_snapVar = m_snapMean = 0;
    m_lastKick = m_lastSnare = -1000000;
    m_lastBody = -1;
    m_kick = m_snare = false;
}

// ============================================================================
float BPMDetector::BandEnergy(const float* mag, int lo, int hi) const {
    float e = 0.0f;
    for (int i = lo; i <= hi; ++i) e += mag[i] * mag[i];
    return e / (hi - lo + 1);  // 按带宽归一化
}

/// 快包络 vs 谷值基线 —— 返回本帧是否"瞬态尖峰"
bool BPMDetector::Spike(float e, float& var, float& mean, float k) {
    var = var * 0.70f + e * 0.30f;                 // 快包络：≈3 个 hop 的时间常数
    if (e < mean) mean = mean * 0.85f + e * 0.15f; // 低于基线 → 快速跟随
    else          mean = mean * 0.995f + e * 0.005f; // 高于基线 → 极慢上升
    // 门限 = 相对基线项 + 相对整体电平项，两项都随信号大小缩放 → 系统音量怎么变判据都一样。
    // kFloorE 只是个极小的下限，只负责兜住纯静音/极弱底噪（见头文件里的标定说明）。
    return var > mean * k + std::max(kFloorE, kFloorRatio * m_lvl);
}

// ============================================================================
void BPMDetector::Analyze() {
    // 环写指针处是最旧样本（下一步即被覆写）→ 按时间顺序展开
    std::vector<float> frame(m_fftSize);
    const int p = m_wpos;
    for (int j = 0; j < m_fftSize; ++j)
        frame[j] = m_ringBuf[(p + j) % m_fftSize];
    ApplyHannWindow(frame.data(), m_fftSize);

    std::vector<Complex> fd(m_fftSize);
    for (int j = 0; j < m_fftSize; ++j)
        fd[j] = Complex(frame[j], 0.0f);
    FFT(fd.data(), m_fftSize, false);

    const int half = m_fftSize / 2;
    std::vector<float> mag(half);
    for (int j = 0; j < half; ++j)
        mag[j] = fd[j].Magnitude() / (float)m_fftSize;

    const float ke = BandEnergy(mag.data(), m_kickLo, m_kickHi);
    const float be = BandEnergy(mag.data(), m_bodyLo, m_bodyHi);
    const float se = BandEnergy(mag.data(), m_snapLo, m_snapHi);
    const float re = BandEnergy(mag.data(), m_restLo, m_restHi);

    // 【长期电平】全频段平均 bin 功率，≈2.3 秒时间常数（hop≈86 次/秒 × 0.005 的系数）。
    // 慢到不会被单个鼓点抬起来 —— 它要代表"这段音乐现在大概多大声"，不是"这一刻多大声"。
    {
        float tot = 0.0f;
        for (int j = 0; j < half; ++j) tot += mag[j] * mag[j];
        tot /= (float)half;
        m_lvl = (m_lvl <= 0.0f) ? tot : (m_lvl * 0.995f + tot * 0.005f);
    }

    const long long ref = (long long)kRefractMs * m_sampleRate / 1000;

    // ---- Kick：低频尖峰 + 低频绝对主导 ----
    const bool kSpike = Spike(ke, m_kickVar, m_kickMean, kKickSpike)
                        && ke > kKickDominance * re;
    // ---- Snare：膛 + 啪 同时出现 ----
    const bool bSpike = Spike(be, m_bodyVar, m_bodyMean, kBodySpike);
    const bool sSpike = Spike(se, m_snapVar, m_snapMean, kSnapSpike);

    if (kSpike && m_total - m_lastKick >= ref) {
        m_kick = true;
        m_lastKick = m_total;
    }
    if (bSpike) m_lastBody = m_total;
    if (sSpike && m_lastBody > 0
        && m_total - m_lastBody <= (long long)kCoincideHops * m_hopSize
        && m_total - m_lastSnare >= ref) {
        m_snare = true;
        m_lastSnare = m_total;
    }
}

// ============================================================================
bool BPMDetector::Process(const float* samples, int numSamples) {
    if (numSamples <= 0 || !samples) return false;
    m_kick = false; m_snare = false;

    for (int i = 0; i < numSamples; ++i) {
        float s = samples[i];
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        m_ringBuf[m_wpos] = s;
        m_wpos = (m_wpos + 1) % m_fftSize;
        ++m_total;

        // 凑满一个 hop 且窗口已填满就分析一次
        if (m_total % m_hopSize == 0 && m_total >= m_fftSize)
            Analyze();
    }
    return (m_kick || m_snare);
}
