/**
 * @file AudioAnalyzer.h
 * @brief 音频分析器 —— 把音频变成可视化可用的 AudioFrame
 *
 * 每帧对 WASAPI 环回音频做：
 *   - 16 对数频段频谱（攻击平滑）→ fft[16]
 *   - 32 点下采样波形 → wave[32]
 *   - bass/mid/treb 三套能量（瞬时 / 攻击 / 衰减）→ 抽吸感来源
 *   - 整体 RMS → energy
 *   - 对拍相位 + 鼓点闪光 → beatPhase / beatFlash（修掉自由跑表）
 *
 * CPU/GPU 两端共用同一份 AudioFrame（GPU 上传 uniform，CPU 走 SetAudio）。
 * 静音时调制值回到中性（乘=1、加=0、系数=0.5），不破坏旧效果。
 */

#pragma once
#include "FFTHelper.h"
#include <vector>
#include <cmath>
#include <algorithm>

constexpr int kAudioFftSize   = 4096;   // FFT 点数
constexpr int kAudioHop       = 2048;   // 每凑满半窗算一次频谱
constexpr int kAudioBands     = 16;     // 对数频段数
constexpr int kAudioWaveSamps = 32;     // 波形下采样点数

struct AudioFrame {
    float fft[kAudioBands] = {};         // 攻击平滑频段 0..1
    float wave[kAudioWaveSamps] = {};    // 下采样波形 -1..1
    float bass = 0, mid = 0, treb = 0;           // 瞬时能量（基准 = 全频谱峰值 m_ref）
    float bassAtt = 0, midAtt = 0, trebAtt = 0;  // 攻击平滑
    float bassDec = 0, midDec = 0, trebDec = 0;  // 衰减（抽吸）
    // 高频能量，但【基准换成高频自己的近期峰值】——
    // 用全频谱峰值当分母时，鼓重的曲子会把分母顶高、高频被整体压低，
    // 同一个镲片在低频猛的曲子里读数能低好几倍。这一路与低频强弱脱钩。
    float trebRel = 0;
    float trebRelAtt = 0;                        // 同上，攻击平滑（快起慢落）
    float energy = 0;                             // 整体 RMS（典型≈0.5）
    float beatFlash = 0;                          // kick 命中=1，~0.18s 衰减
    float beatPhase = 0;                          // 对拍相位 [0,1)，kick 归零
    // ---- 鼓点分路瞬态包络（抽插变速用）----
    // BPMDetector 里 kick 与 snare 各有独立的频谱签名判定，但过去只把二者
    // 【合并成一个布尔】传出来（IsKick/IsSnare 从未被调用过）。这里把两路
    // 接出来，各自整形成快起慢落的包络：kick 深而慢（"顶"），snare 浅而快（"啪"）。
    float hitKick  = 0;                           // 底鼓瞬态包络 0..1，τ≈0.28s
    float hitSnare = 0;                           // 军鼓瞬态包络 0..1，τ≈0.12s
    float bpm = 120.0f;

    // ========================================================================
    // 频谱形状描述子（2026-09-14 新增）—— 描述【音色】，不是响度
    //
    // 上面 fft[16] 回答的是"每个频段多响"；这一组回答的是"这个声音长什么样"。
    // 全部是【比值】（除出来的），所以【天然与系统音量无关】——
    // 不需要像 trebRel 那样做自适应基准，调大调小音量读数不变。
    //
    // 频率一律用【对数坐标】 log2(f/20Hz)，跨度约 9.97 个八度（20Hz~20kHz），
    // 因为人耳对音高是等比感知的，线性坐标会让高频占掉一大半量程却没意义。
    // 所有值都归一化到 0..1。
    // ========================================================================
    float centroid = 0;   // 质心      —— "亮度"：能量集中在低频还是高频
    float spread   = 0;   // 扩散      —— "宽窄"：纯音是窄的，噪声/失真铺得很宽
    float rolloff  = 0;   // 滚降      —— "频谱的上边界在哪"（85% 能量以下）
    float flatness = 0;   // 平坦度    —— "噪声性 vs 音调性"：白噪声≈1，纯音≈0
    float flux     = 0;   // 通量      —— "起音强度"：频谱变化多快（只算增长）
    float fluxAtt = 0;    // 通量的攻击包络（快起慢落）

    // ---- 色度：12 个半音（C=0 .. B=11）上的能量分布，已归一化成分布 ----
    // 它与 fft[16] 是【正交】的维度：fft 说的是"哪个频段响"，
    // 色度说的是"哪些音在响"——移调后的同一段旋律频段分布会变，色度不变。
    float chroma[12] = {};
    int   chromaTop = 0;  // 最强音级 0..11（0 = C）
};

class AudioAnalyzer {
public:
    explicit AudioAnalyzer(int sampleRate = 44100);

    /// 每帧喂入新采样；内部推进波形/频谱/能量包络/对拍相位
    /// kick / snare 由 BPMDetector::IsKick() / IsSnare() 分别传入（不再合并成一个布尔）
    void Process(const float* samples, int numSamples, float dt, float bpm, bool kick, bool snare);

    const AudioFrame& Frame() const { return m_frame; }
    void Reset();

private:
    void ComputeSpectrum(float specDt);   // FFT → 频段 + 能量包络
    void DownsampleWave();

    int m_sampleRate;
    std::vector<float> m_ring;   // 环形缓冲，容量=fftSize，恒存最近 fftSize 个采样
    int m_writePos = 0;          // 下一个写入位置
    int m_lastFftPos = 0;        // 上次算频谱时的写入位置
    float m_time = 0;            // 累计秒数
    float m_lastSpecTime = 0;    // 上次算频谱的时刻
    float m_ref = 1e-6f;         // 慢衰减参考峰值（全频谱，频段归一化基准）
    float m_refHigh = 1e-6f;     // 慢衰减参考峰值（只看高频段 10~15）—— trebRel 的基准
    std::vector<float> m_prevMag;// 上一帧幅度谱（算频谱通量用）
    bool  m_hasPrev = false;
    float m_refFlux = 1e-6f;     // 通量的慢衰减参考峰值 —— 通量是绝对量，需要基准
    AudioFrame m_frame;
};
