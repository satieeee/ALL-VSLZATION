//  BPMDetector.h - 鼓点检测 Kick(40-100Hz) / Snare(200-2000Hz) 频谱瞬态识别
//
//  用法:
//    while (audio) {
//        detector.Process(samples, n);
//        if (detector.IsKick())  { ... }  // 底鼓
//        if (detector.IsSnare()) { ... }  // 军鼓
//        if (detector.IsHit())   { ... }  // 任意鼓点
//    }
#pragma once
#include <vector>
#include <cstdint>

class BPMDetector {
public:
    BPMDetector(int sampleRate = 44100, int fftSize = 1024, int hopSize = 512);

    bool Process(const float* samples, int numSamples);

    // 上一帧是否检测到鼓点（Kick 或 Snare）
    bool IsKick()  const { return m_kick; }
    bool IsSnare() const { return m_snare; }
    bool IsHit()   const { return m_kick || m_snare; }

    void Reset();

private:
    int m_sampleRate, m_fftSize, m_hopSize;

    // FFT 环形缓冲
    std::vector<float> m_ringBuf;
    int m_ringPos = 0;

    // 频段能量追踪
    // Kick:  40-100Hz  → bin范围由 sampleRate 和 fftSize 决定
    // Snare: 200-2000Hz
    float m_kickEnergy     = 0;  // 当前帧
    float m_kickMean       = 0;  // 基线（跟踪近期谷值）
    float m_kickVar        = 0;  // 快包络（瞬时能量）
    int   m_kickCooldown   = 0;  // 防重触发冷却

    float m_snareEnergy    = 0;
    float m_snareMean      = 0;
    float m_snareVar       = 0;
    int   m_snareCooldown  = 0;

    // 输出
    bool m_kick  = false;
    bool m_snare = false;

    // 频段 bin 索引
    int m_kickBinLow, m_kickBinHigh;
    int m_snareBinLow, m_snareBinHigh;

    float BandEnergy(const float* mag, int lowBin, int highBin);
};
