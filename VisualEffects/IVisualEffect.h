#pragma once

#define NOMINMAX
#include <windows.h>
#include "AudioCapture/AudioAnalyzer.h"

/**
 * @brief 可视化效果抽象接口
 *
 * 所有可视化效果都必须实现此接口。
 * 主程序通过此接口统一调用不同效果的渲染函数。
 *
 * 使用方式：
 *   1. 继承 IVisualEffect
 *   2. 实现 GetName() 返回效果名称
 *   3. 实现 Render() 完成绘制
 *   4. 在主程序中注册：g_effects.push_back(std::make_unique<MyEffect>())
 */
class IVisualEffect {
public:
    virtual ~IVisualEffect() = default;

    /// 返回可视化效果的显示名称（用于标题栏）
    virtual const wchar_t* GetName() const = 0;

    /// 渲染一帧可视化效果
    /// @param hdc          设备上下文
    /// @param rect         客户区矩形
    /// @param spectrum     归一化后的频谱数据（0.0 ~ 1.0），大小为 spectrumBins
    /// @param spectrumBins 频谱条带数
    /// @param waveform     原始波形采样（-1.0 ~ 1.0），大小为 waveformSize
    /// @param waveformSize 波形采样数
    virtual void Render(HDC hdc, const RECT& rect,
                        const float* spectrum, int spectrumBins,
                        const float* waveform, int waveformSize) = 0;

    /// 返回效果内置渲染分辨率系数（用于进程内播放链）
    /// 值越大渲染精度越低但更快；例如 Psychedelic=2 表示半分辨率
    virtual int GetQuality() const { return 1; }

    /// 同步 BPM 和节拍状态（播放链模式调用）
    /// 默认空实现，需要 BPM 驱动的效果自行重写
    virtual void SyncBeatState(float /*bpm*/, float /*phase*/, bool /*triggered*/) {}

    /// 每帧同步音频分析结果（频谱/波形/能量包络/对拍相位）。
    /// 默认空实现 = 忽略音频，旧效果行为不变。
    virtual void SetAudio(const AudioFrame& /*af*/) {}

    /// 设置用户手动速度倍率（1.0 = 正常，0.5 = 半速，2.0 = 双速）
    virtual void SetUserSpeed(float /*multiplier*/) {}

    /// 出场计时（秒）。项目时长到了、正在等下一次鼓点切走时，播放链会从 0 开始递增；
    /// 不处于待切出状态时给一个很大的值。默认空实现 = 该效果没有出场动画。
    /// 语义与 GPU 侧 main.cpp 的 g_plExitT / shader 的 uExitTime 一致。
    virtual void SetExitTime(float /*seconds*/) {}

    /// 设置黑底/白底模式（true=黑底, false=白底）
    virtual void SetDarkMode(bool) {}
    virtual bool IsDarkMode() const { return false; }
};
