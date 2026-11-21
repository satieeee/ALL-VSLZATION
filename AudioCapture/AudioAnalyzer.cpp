/**
 * @file AudioAnalyzer.cpp
 * @brief 音频分析器实现 —— 复用 FFTHelper 的 FFT / 对数频段分组
 */

#include "AudioAnalyzer.h"
#include <cstring>      // memcpy（上一帧幅度谱，算通量用）

AudioAnalyzer::AudioAnalyzer(int sampleRate)
    : m_sampleRate(sampleRate)
    , m_ring(kAudioFftSize, 0.0f)
{
    m_frame.bpm = 120.0f;
}

void AudioAnalyzer::Reset() {
    std::fill(m_ring.begin(), m_ring.end(), 0.0f);
    m_writePos = 0; m_lastFftPos = 0;
    m_time = 0; m_lastSpecTime = 0;
    m_ref = 1e-6f;
    m_refHigh = 1e-6f;
    m_refFlux = 1e-6f;
    m_prevMag.clear();
    m_hasPrev = false;
    m_frame = AudioFrame{};
    m_frame.bpm = 120.0f;
}

void AudioAnalyzer::Process(const float* samples, int n, float dt, float bpm, bool kick, bool snare) {
    m_time += dt;

    if (n > 0 && samples) {
        // 采样进环（环=fftSize，恒存最近 fftSize 个采样）
        for (int i = 0; i < n; ++i) {
            float s = samples[i];
            if (s > 1.0f) s = 1.0f; else if (s < -1.0f) s = -1.0f;
            m_ring[m_writePos] = s;
            m_writePos = (m_writePos + 1) % kAudioFftSize;
        }
        // 凑满一个 hop 就重算频谱
        int advanced = (m_writePos - m_lastFftPos + kAudioFftSize) % kAudioFftSize;
        if (advanced >= kAudioHop) {
            ComputeSpectrum(m_time - m_lastSpecTime);
            m_lastSpecTime = m_time;
            m_lastFftPos = m_writePos;
        }
    }

    DownsampleWave();

    // ---- 整体 RMS（快起慢落；典型音乐 RMS≈0.2 → 能量≈0.5）----
    if (n > 0 && samples) {
        float sum = 0;
        for (int i = 0; i < n; ++i) sum += samples[i] * samples[i];
        float rms = std::sqrt(sum / n);
        m_frame.energy = std::max(std::min(rms * 2.5f, 1.0f), m_frame.energy * std::exp(-dt / 0.5f));
    } else {
        m_frame.energy *= std::exp(-dt / 0.5f);
    }

    // ---- 对拍相位 + 鼓点闪光（替换 main.cpp 的自由跑表）----
    const bool hit = kick || snare;
    float beatInterval = (bpm > 1.0f) ? (60.0f / bpm) : 0.5f;
    if (hit) { m_frame.beatPhase = 0.0f; m_frame.beatFlash = 1.0f; }
    else {
        m_frame.beatPhase += dt / beatInterval;
        if (m_frame.beatPhase >= 1.0f) m_frame.beatPhase -= 1.0f;
        m_frame.beatFlash *= std::exp(-dt / 0.35f);   // 慢衰减：柔和脉冲而非频闪
        if (m_frame.beatFlash < 0.003f) m_frame.beatFlash = 0.0f;
    }

    // ---- 鼓点分路瞬态包络（抽插变速用）----
    // 瞬时置 1，然后指数回落。衰减常数刻意不同：
    //   kick  τ=0.28s 深、中衰减 —— 负责"顶出去再插回来"的主抽插
    //   snare τ=0.12s 浅、快     —— 叠在其上做"啪"的次级颤动
    // 落到 0.003 就归零，免得包络拖一条永远不消失的尾巴。
    if (kick)  m_frame.hitKick = 1.0f;
    else { m_frame.hitKick  *= std::exp(-dt / 0.28f); if (m_frame.hitKick  < 0.003f) m_frame.hitKick  = 0.0f; }
    if (snare) m_frame.hitSnare = 1.0f;
    else { m_frame.hitSnare *= std::exp(-dt / 0.12f); if (m_frame.hitSnare < 0.003f) m_frame.hitSnare = 0.0f; }

    m_frame.bpm = bpm;
}

void AudioAnalyzer::ComputeSpectrum(float specDt) {
    if (specDt < 1e-4f) specDt = 0.03f;   // 防除零/过小

    // 最近 fftSize 个采样（环起点 = 当前写入位置）
    std::vector<float> win(kAudioFftSize);
    for (int j = 0; j < kAudioFftSize; ++j)
        win[j] = m_ring[(m_writePos + j) % kAudioFftSize];
    ApplyHannWindow(win.data(), kAudioFftSize);

    std::vector<Complex> fd(kAudioFftSize);
    for (int j = 0; j < kAudioFftSize; ++j) fd[j] = Complex(win[j], 0.0f);
    FFT(fd.data(), kAudioFftSize, false);

    int half = kAudioFftSize / 2;
    std::vector<float> mag(half);
    float peak = 0;
    for (int j = 0; j < half; ++j) {
        mag[j] = fd[j].Magnitude() / kAudioFftSize;
        if (mag[j] > peak) peak = mag[j];
    }

    // 参考峰值：慢衰减，hold 住最响，作为频段归一化基准（避免静音时全部爆表）
    m_ref = std::max(peak, m_ref * std::exp(-specDt / 3.0f));
    if (m_ref < 1e-7f) m_ref = 1e-7f;

    // ========================================================================
    // 频谱形状描述子 —— 描述【音色】而不是响度。
    // 下面每一项都除以 Σa（总能量），所以是【比值】：音量调大调小读数不变，
    // 不需要自适应基准。这一点和 fft[] / energy 那些绝对量刚好相反。
    // 频率一律走对数坐标 log2(f/20Hz)（约 9.97 个八度），因为音高是等比感知的。
    // ========================================================================
    {
        const double F_MIN = 20.0, F_MAX = 20000.0;
        const double binHz = (double)m_sampleRate / (double)kAudioFftSize;
        const double logSpan = std::log2(F_MAX / F_MIN);

        double sumA = 0, sumLA = 0, sumL2A = 0, sumLnA = 0, total = 0;
        int    nBins = 0;
        for (int j = 1; j < half; ++j) {            // 跳过 DC
            double f = j * binHz;
            if (f < F_MIN || f > F_MAX) continue;
            double a = mag[j];
            double L = std::log2(f / F_MIN);
            sumA += a; sumLA += a * L; sumL2A += a * L * L;
            sumLnA += std::log(a + 1e-12);          // 平坦度要几何平均
            total += a; ++nBins;
        }

        m_frame.centroid = m_frame.spread = m_frame.rolloff = m_frame.flatness = 0.0f;
        if (sumA > 1e-12 && nBins > 0) {
            double c   = sumLA / sumA;              // 质心（八度坐标）
            double var = sumL2A / sumA - c * c;     // 关于质心的方差
            if (var < 0) var = 0;
            m_frame.centroid = (float)std::clamp(c / logSpan, 0.0, 1.0);
            m_frame.spread   = (float)std::clamp(std::sqrt(var) / logSpan * 2.0, 0.0, 1.0);
            // 平坦度 = 几何平均 / 算术平均：纯音→接近 0，白噪声→接近 1
            m_frame.flatness = (float)std::clamp(std::exp(sumLnA / nBins) / (sumA / nBins), 0.0, 1.0);

            // 滚降：累积能量达到 85% 时所在的频率（对数坐标）
            double cum = 0, target = total * 0.85;
            for (int j = 1; j < half; ++j) {
                double f = j * binHz;
                if (f < F_MIN || f > F_MAX) continue;
                cum += mag[j];
                if (cum >= target) {
                    m_frame.rolloff = (float)std::clamp(std::log2(f / F_MIN) / logSpan, 0.0, 1.0);
                    break;
                }
            }
        }

        // 色度：把每个 bin 按频率折到 12 个半音上，再归一化成【分布】。
        // 范围取 A1(55Hz)~4.5kHz —— 人耳能听清音高的区间，
        // 低频鼓点是噪声没有音高，高频泛音会糊成一片，都不该计入。
        {
            double ch[12] = {};
            for (int j = 1; j < half; ++j) {
                double f = j * binHz;
                if (f < 55.0 || f > 5000.0) continue;
                double midi = 69.0 + 12.0 * std::log2(f / 440.0);   // A4=440Hz=MIDI 69
                int pc = (int)std::llround(midi) % 12;
                if (pc < 0) pc += 12;
                ch[pc] += mag[j];
            }
            double s = 0;
            for (int i = 0; i < 12; ++i) s += ch[i];
            int top = 0;
            for (int i = 0; i < 12; ++i) {
                m_frame.chroma[i] = (s > 1e-12) ? (float)(ch[i] / s) : 0.0f;
                if (m_frame.chroma[i] > m_frame.chroma[top]) top = i;
            }
            m_frame.chromaTop = (s > 1e-12) ? top : 0;
        }
    }

    // 频谱通量：只累加【增长】的部分（半波整流）。
    // 它测的是"频谱变化得多快"，也就是【起音强度】—— 升调、鼓击、咬字都会推高它，
    // 而持续的音（长音、pad）推不高。这是 onset detection 的经典做法。
    {
        double fl = 0;
        if (m_hasPrev) {
            for (int j = 1; j < half; ++j) {
                double d = (double)mag[j] - (double)m_prevMag[j];
                if (d > 0) fl += d;
            }
        }
        if (m_prevMag.size() != (size_t)half) m_prevMag.assign(half, 0.0f);
        std::memcpy(m_prevMag.data(), mag.data(), sizeof(float) * half);
        m_hasPrev = true;
        // 通量是绝对量（没有 Σa 可除）→ 用慢衰减峰值当基准，同 m_ref 的思路
        m_refFlux = std::max((float)fl, m_refFlux * std::exp(-specDt / 2.0f));
        if (m_refFlux < 1e-9f) m_refFlux = 1e-9f;
        m_frame.flux = (float)std::clamp(fl / (double)m_refFlux, 0.0, 1.0);
    }

    float raw[kAudioBands] = {};
    GroupSpectrumToBars(mag.data(), kAudioFftSize, m_sampleRate, kAudioBands, raw);
    float norm = 0.9f / m_ref;

    // 高频【自己的】参考峰值：只盯段 10~15，同样 3 秒慢衰减。
    // 这是 trebRel 的基准 —— 换掉"全频谱峰值"这个基准之后，高频读数就与低频强弱脱钩了：
    // 鼓重的曲子不会再把分母顶高、把高频压扁。
    {
        float peakHigh = 0.0f;
        for (int b = 10; b < kAudioBands; ++b) if (raw[b] > peakHigh) peakHigh = raw[b];
        m_refHigh = std::max(peakHigh, m_refHigh * std::exp(-specDt / 3.0f));
        if (m_refHigh < 1e-7f) m_refHigh = 1e-7f;
    }
    const float normHigh = 0.9f / m_refHigh;

    float nb[kAudioBands];
    float a = 1.0f - std::exp(-specDt / 0.06f);   // 频段攻击平滑
    float br = 0, mr = 0, tr = 0, trRel = 0;
    for (int b = 0; b < kAudioBands; ++b) {
        nb[b] = std::clamp(raw[b] * norm, 0.0f, 1.0f);
        m_frame.fft[b] += (nb[b] - m_frame.fft[b]) * a;
        if (b < 4)       br += nb[b];
        else if (b < 10) mr += nb[b];
        else             { tr += nb[b]; trRel += std::clamp(raw[b] * normHigh, 0.0f, 1.0f); }
    }
    m_frame.bass = br / 4.0f;   // 低频段
    m_frame.mid  = mr / 6.0f;   // 中频段
    m_frame.treb = tr / 6.0f;   // 高频段（基准 = 全频谱峰值）
    m_frame.trebRel = trRel / 6.0f;   // 高频段（基准 = 高频自身峰值）

    // ---- 能量包络：攻击快起、衰减慢落（Milkdrop bass_att/bass_dec 思想）----
    auto env = [&](float x, float& att, float& dec,
                   float tauUp, float tauDn, float tauDec) {
        float up = 1.0f - std::exp(-specDt / tauUp);
        float dn = 1.0f - std::exp(-specDt / tauDn);
        if (x > att) att += (x - att) * up;
        else         att += (x - att) * dn;
        dec = std::max(x, dec * std::exp(-specDt / tauDec));
    };
    env(m_frame.bass, m_frame.bassAtt, m_frame.bassDec, 0.015f, 0.22f, 0.55f);
    env(m_frame.mid,  m_frame.midAtt,  m_frame.midDec,  0.020f, 0.25f, 0.45f);
    env(m_frame.treb, m_frame.trebAtt, m_frame.trebDec, 0.020f, 0.18f, 0.35f);
    // trebRel 只要 att（没有再要 dec 的用途），传个临时变量把 dec 结果丢掉
    { float unusedDec = 0.0f;
      env(m_frame.trebRel, m_frame.trebRelAtt, unusedDec, 0.020f, 0.18f, 0.35f); }
    // 通量也要一个攻击包络：起音要"弹"出来（10ms 起），然后较快回落（150ms），
    // 这样它驱动的是"一下一下的冲击感"，而不是缓慢的亮度变化。
    { float unusedDec = 0.0f;
      env(m_frame.flux, m_frame.fluxAtt, unusedDec, 0.010f, 0.15f, 0.30f); }
}

void AudioAnalyzer::DownsampleWave() {
    // 环上最近 fftSize 个采样里等距抽 32 个（跨度≈93ms@44.1k，捕捉波形形状）
    const int stride = kAudioFftSize / kAudioWaveSamps;
    for (int i = 0; i < kAudioWaveSamps; ++i)
        m_frame.wave[i] = m_ring[(m_writePos + i * stride) % kAudioFftSize];
}
