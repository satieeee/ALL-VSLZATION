/**
 * @file BPMDetector.cpp
 * @brief Kick/Snare 频谱瞬态检测
 *
 * Kick:  40-100Hz  —— 短促强低频脉冲
 * Snare: 200-2000Hz —— 中频宽频瞬态
 *
 * 判定逻辑：
 *   1. 快包络（瞬时能量）显著高于基线（近期谷值）→ 判定为瞬态鼓点
 *   2. 基线跟踪谷值：能量低于基线快速下降、高于基线极慢上升，避免持续响度漏检
 *   3. 距上次同类型触发超过冷却期
 */

#include "BPMDetector.h"
#include "../AudioCapture/FFTHelper.h"
#include <algorithm>
#include <cmath>

BPMDetector::BPMDetector(int sr, int fft, int hop)
    : m_sampleRate(sr), m_fftSize(fft), m_hopSize(hop)
    , m_ringPos(0)
    , m_kickEnergy(0), m_kickMean(0), m_kickVar(0), m_kickCooldown(0)
    , m_snareEnergy(0), m_snareMean(0), m_snareVar(0), m_snareCooldown(0)
    , m_kick(false), m_snare(false)
{
    m_ringBuf.resize(m_fftSize, 0.0f);

    // bin = freq * fftSize / sampleRate
    float binWidth = (float)sr / fft;
    m_kickBinLow  = (int)(40.0f  / binWidth);  m_kickBinHigh  = (int)(100.0f / binWidth) + 1;
    m_snareBinLow = (int)(200.0f / binWidth);  m_snareBinHigh = (int)(2000.0f / binWidth) + 1;

    int half = fft / 2;
    if (m_kickBinHigh  > half) m_kickBinHigh  = half;
    if (m_snareBinHigh > half) m_snareBinHigh = half;
    if (m_kickBinLow  < 1) m_kickBinLow = 1;
    if (m_snareBinLow < 1) m_snareBinLow = 1;
}

void BPMDetector::Reset() {
    m_ringPos = 0; m_kickEnergy = 0; m_kickMean = 0; m_kickVar = 0; m_kickCooldown = 0;
    m_snareEnergy = 0; m_snareMean = 0; m_snareVar = 0; m_snareCooldown = 0;
    m_kick = false; m_snare = false;
    std::fill(m_ringBuf.begin(), m_ringBuf.end(), 0.0f);
}

// ============================================================================
float BPMDetector::BandEnergy(const float* mag, int lo, int hi) {
    float e = 0.0f;
    for (int i = lo; i <= hi; ++i) e += mag[i] * mag[i];
    return e / (hi - lo + 1);  // 均值归一化
}

// ============================================================================
bool BPMDetector::Process(const float* samples, int numSamples) {
    if (numSamples <= 0 || !samples) return false;
    m_kick = false; m_snare = false;
    if (m_kickCooldown > 0) m_kickCooldown--;
    if (m_snareCooldown > 0) m_snareCooldown--;

    for (int i = 0; i < numSamples; ++i) {
        float s = samples[i];
        if (s > 1.0f) s = 1.0f; if (s < -1.0f) s = -1.0f;
        m_ringBuf[m_ringPos % m_fftSize] = s;
        m_ringPos++;

        if (m_ringPos % m_hopSize == 0 && m_ringPos >= m_fftSize) {
            // ---- FFT ----
            std::vector<float> frame(m_fftSize);
            int p = m_ringPos % m_fftSize;
            for (int j = 0; j < m_fftSize; ++j)
                frame[j] = m_ringBuf[(p + j) % m_fftSize];
            ApplyHannWindow(frame.data(), m_fftSize);

            std::vector<Complex> fftData(m_fftSize);
            for (int j = 0; j < m_fftSize; ++j)
                fftData[j] = Complex(frame[j], 0.0f);
            FFT(fftData.data(), m_fftSize, false);

            int half = m_fftSize / 2;
            std::vector<float> mag(half);
            for (int j = 0; j < half; ++j)
                mag[j] = fftData[j].Magnitude() / m_fftSize;

            // ---- 频段能量 ----
            float ke = BandEnergy(mag.data(), m_kickBinLow, m_kickBinHigh);
            float se = BandEnergy(mag.data(), m_snareBinLow, m_snareBinHigh);

            // ---- Kick 检测（快慢双包络，基线跟踪谷值）----
            {
                m_kickVar  = m_kickVar  * 0.70f + ke * 0.30f;   // 快包络：瞬时能量
                // 基线跟踪近期谷值：能量低于基线快速下降、高于基线极慢上升，
                // 避免持续响度把基线抬高导致"响度大时比值塌缩漏检"
                if (ke < m_kickMean) m_kickMean = m_kickMean * 0.85f + ke * 0.15f;
                else                 m_kickMean = m_kickMean * 0.995f + ke * 0.005f;
                // 命中：快包络显著高于基线，且能量高于绝对底噪（拒绝静音噪声）
                bool spike = (m_kickVar > m_kickMean * 3.5f + 1e-6f);
                if (spike && m_kickCooldown <= 0) {
                    m_kick = true;
                    m_kickCooldown = 18;
                }
            }

            // ---- Snare 检测（快慢双包络，基线跟踪谷值）----
            {
                m_snareVar  = m_snareVar  * 0.70f + se * 0.30f;
                if (se < m_snareMean) m_snareMean = m_snareMean * 0.85f + se * 0.15f;
                else                  m_snareMean = m_snareMean * 0.995f + se * 0.005f;
                bool spike = (m_snareVar > m_snareMean * 3.5f + 1e-6f);
                if (spike && m_snareCooldown <= 0) {
                    m_snare = true;
                    m_snareCooldown = 18;
                }
            }
        }
    }
    return (m_kick || m_snare);
}
