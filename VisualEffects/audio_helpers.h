/**
 * @file audio_helpers.h
 * @brief CPU 侧音频驱动 ABI（v1）—— 与 VisualEffects/audio_prelude.glsl 严格同源
 *
 * 效果内部用 SetAudio 传入的 AudioFrame (m_af) 驱动数学函数：
 *   - audioPump     bass 攻击 → 图案幅度（乘法，中性=1）
 *   - audioDrive    mid 攻击 → 转速/系数（中性=0.5）
 *   - audioAdd      treb 攻击 → 加法偏移（中性=0）
 *   - audioBand     某个频段的攻击平滑值（0..1）
 *   - audioWave     波形线性插值 → 空间抖动
 *   - audioGrade    颜色最终调制（整体能量压亮度 + 频段染色 + 鼓点闪光）
 *
 * 静音时所有值回到中性，效果不因此改变；唯一例外是 audioGrade 的亮度项。
 */

#pragma once
#include "AudioCapture/AudioAnalyzer.h"
#include <cmath>
#include <cstdint>

inline float audioPump(const AudioFrame& a, float amt)          { return 1.0f + a.bassAtt * amt; }
inline float audioPumpDec(const AudioFrame& a, float amt)       { return 1.0f + a.bassDec * amt; }
inline float audioDrive(const AudioFrame& a, float amt)         { return 0.5f + (a.midAtt - 0.5f) * amt; }
inline float audioAdd(const AudioFrame& a, float amt)           { return a.trebAtt * amt; }
inline float audioBand(const AudioFrame& a, int i)              { float v = a.fft[i]; return v < 0 ? 0.0f : (v > 1.0f ? 1.0f : v); }

// ---- 频谱形状描述子（与 audio_prelude.glsl 的 uCentroid 等严格同源）--------
// 这些都描述【音色】而不是响度，且都是比值 → 不受系统音量影响。
inline float audioCentroid(const AudioFrame& a) { return a.centroid; }  // 亮度
inline float audioSpread  (const AudioFrame& a) { return a.spread;   }  // 宽窄
inline float audioRolloff (const AudioFrame& a) { return a.rolloff;  }  // 频谱上边界
inline float audioFlatness(const AudioFrame& a) { return a.flatness; }  // 噪声性
inline float audioFlux    (const AudioFrame& a) { return a.flux;     }  // 起音强度（瞬时）
inline float audioFluxAtt (const AudioFrame& a) { return a.fluxAtt;  }  // 起音强度（包络）
/// 某个半音的能量占比（0..1，12 个加起来=1）；i = 0(C) .. 11(B)
inline float audioChroma(const AudioFrame& a, int i) {
    return (i < 0 || i > 11) ? 0.0f : a.chroma[i];
}
inline float audioWave(const AudioFrame& a, float x, float amt) {
    float f = x - std::floor(x);
    float fi = f * 31.0f;
    int ia = (int)fi, ib = ia + 1; if (ib > 31) ib = 31;
    return (a.wave[ia] + (a.wave[ib] - a.wave[ia]) * (fi - (float)ia)) * amt;
}
// ---- 鼓点分路（与 audio_prelude.glsl 的 audioHit* 严格同源）----
inline float audioHitKick (const AudioFrame& a) { return a.hitKick;  }
inline float audioHitSnare(const AudioFrame& a) { return a.hitSnare; }
/// 抽插速度倍率（乘法，中性=1）
inline float audioHitPump (const AudioFrame& a, float kickAmt, float snareAmt) {
    return 1.0f + a.hitKick * kickAmt + a.hitSnare * snareAmt;
}

inline void audioGrade(const AudioFrame& a, uint8_t& r, uint8_t& g, uint8_t& b) {
    float k = 0.85f + a.energy * 0.3f;                      // 能量 0.5 → 中性 1.0，峰值仅 ~1.15
    float tr = audioBand(a, 2) * 0.12f, tg = audioBand(a, 8) * 0.12f, tb = audioBand(a, 14) * 0.12f;
    float fl = a.beatFlash * 0.03f;                         // 极弱鼓点闪光（防频闪/压黑底）
    auto cl = [](float x){ return x < 0 ? 0.0f : (x > 255 ? 255.0f : x); };
    r = (uint8_t)cl(r * k + (tr + fl) * 255.0f);
    g = (uint8_t)cl(g * k + (tg + fl) * 255.0f);
    b = (uint8_t)cl(b * k + (tb + fl) * 255.0f);
}
