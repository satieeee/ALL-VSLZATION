#pragma once

#include <vector>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/**
 * @brief FFT 及频谱分析辅助函数（纯头文件，无外部依赖）
 *
 * 包含：
 *   - 汉宁窗 (Hann Window)
 *   - 位反转重排
 *   - Cooley-Tukey 基-2 FFT
 *   - 幅度谱计算
 *   - 对数频率分组（用于柱状频谱显示）
 */

// ============================================================================
// 窗函数
// ============================================================================

/// 就地应用汉宁窗
inline void ApplyHannWindow(float* data, int size) {
    for (int i = 0; i < size; ++i) {
        double multiplier = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (size - 1)));
        data[i] = static_cast<float>(data[i] * multiplier);
    }
}

// ============================================================================
// 位反转
// ============================================================================

inline int BitReverse(int x, int log2n) {
    int n = 0;
    for (int i = 0; i < log2n; ++i) {
        n = (n << 1) | (x & 1);
        x >>= 1;
    }
    return n;
}

// ============================================================================
// FFT (Cooley-Tukey, 基-2, 就地)
// ============================================================================

/// 复数结构（避免外部依赖）
struct Complex {
    float real, imag;
    Complex() : real(0), imag(0) {}
    Complex(float r, float i) : real(r), imag(i) {}

    Complex operator+(const Complex& o) const { return Complex(real + o.real, imag + o.imag); }
    Complex operator-(const Complex& o) const { return Complex(real - o.real, imag - o.imag); }
    Complex operator*(const Complex& o) const {
        return Complex(real * o.real - imag * o.imag,
                       real * o.imag + imag * o.real);
    }
    float Magnitude() const { return std::sqrt(real * real + imag * imag); }
};

/// 基-2 FFT（就地计算）
/// @param data   输入/输出复数数组，大小必须为 2 的幂
/// @param n      FFT 点数
/// @param invert false = 正变换, true = 逆变换
inline void FFT(Complex* data, int n, bool invert = false) {
    int log2n = 0;
    while ((1 << log2n) < n) ++log2n;

    // 位反转重排
    for (int i = 0; i < n; ++i) {
        int j = BitReverse(i, log2n);
        if (i < j) {
            std::swap(data[i], data[j]);
        }
    }

    // 蝶形运算
    for (int len = 2; len <= n; len <<= 1) {
        double angle = 2.0 * M_PI / len * (invert ? -1.0 : 1.0);
        Complex wlen(std::cos(angle), std::sin(angle));
        for (int i = 0; i < n; i += len) {
            Complex w(1, 0);
            for (int j = 0; j < len / 2; ++j) {
                Complex u = data[i + j];
                Complex v = data[i + j + len / 2] * w;
                data[i + j] = u + v;
                data[i + j + len / 2] = u - v;
                w = w * wlen;
            }
        }
    }

    // 逆变换缩放
    if (invert) {
        for (int i = 0; i < n; ++i) {
            data[i].real /= n;
            data[i].imag /= n;
        }
    }
}

// ============================================================================
// 频谱计算
// ============================================================================

/// 计算幅度谱（仅保留正频率部分）
/// @param waveform  时域采样（大小 = fftSize）
/// @param fftSize   2的幂次
/// @param magnitude 输出：幅度谱（大小 = fftSize/2）
inline void ComputeMagnitudeSpectrum(const float* waveform, int fftSize,
                                      float* magnitude) {
    // 复制到复数数组并应用窗函数
    std::vector<float> windowed(waveform, waveform + fftSize);
    ApplyHannWindow(windowed.data(), fftSize);

    std::vector<Complex> fftData(fftSize);
    for (int i = 0; i < fftSize; ++i) {
        fftData[i] = Complex(windowed[i], 0.0f);
    }

    FFT(fftData.data(), fftSize);

    // 幅度谱（仅正频率，即前 fftSize/2 个 bin）
    int halfSize = fftSize / 2;
    for (int i = 0; i < halfSize; ++i) {
        magnitude[i] = fftData[i].Magnitude() / fftSize;
    }
}

// ============================================================================
// 对数频率分组
// ============================================================================

/// 将对数分布的 FFT bins 分组为可视条带
/// @param spectrum     输入：线性频率幅度谱（fftSize/2 个 bin）
/// @param fftSize      FFT 点数
/// @param sampleRate   采样率
/// @param numBars      输出条带数
/// @param barHeights   输出：每个条带的高度
/// @param lowFreq      最低频率（Hz）
/// @param highFreq     最高频率（Hz，通常为 Nyquist）
inline void GroupSpectrumToBars(const float* spectrum, int fftSize,
                                 int sampleRate, int numBars,
                                 float* barHeights,
                                 float lowFreq = 20.0f,
                                 float highFreq = 20000.0f) {
    int halfSize = fftSize / 2;
    float nyquist = sampleRate / 2.0f;
    if (highFreq > nyquist) highFreq = nyquist;

    for (int bar = 0; bar < numBars; ++bar) {
        // 对数刻度：等间距在对数空间中
        float frac = static_cast<float>(bar) / numBars;
        float freqLow = lowFreq * std::pow(highFreq / lowFreq, frac);
        float freqHigh = lowFreq * std::pow(highFreq / lowFreq,
                                              static_cast<float>(bar + 1) / numBars);

        int binLow = static_cast<int>(freqLow / nyquist * halfSize);
        int binHigh = static_cast<int>(freqHigh / nyquist * halfSize) + 1;
        if (binLow < 0) binLow = 0;
        if (binHigh > halfSize) binHigh = halfSize;
        if (binLow >= binHigh) binHigh = binLow + 1;

        // 取该频段内 bin 的最大值
        float maxVal = 0.0f;
        for (int b = binLow; b < binHigh; ++b) {
            if (spectrum[b] > maxVal) maxVal = spectrum[b];
        }
        barHeights[bar] = maxVal;
    }
}

/// 计算功率谱密度 (Power Spectral Density) —— RMS 归一化
inline void NormalizeSpectrum(float* magnitude, int size) {
    float maxVal = 0.0f;
    for (int i = 0; i < size; ++i) {
        if (magnitude[i] > maxVal) maxVal = magnitude[i];
    }
    if (maxVal > 0.0001f) {
        for (int i = 0; i < size; ++i) {
            magnitude[i] /= maxVal;
        }
    }
}
