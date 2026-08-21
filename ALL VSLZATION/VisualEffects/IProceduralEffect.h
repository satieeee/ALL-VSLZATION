/**
 * @file IProceduralEffect.h
 * @brief 可程序化过渡的可视化效果接口 —— 暴露模式场标量值以支持跨效果数学 morphing
 *
 * 传统 IVisualEffect::Render() 是 void 黑盒，跨效果过渡只能做像素层面叠化。
 * 此接口要求效果暴露其内部 (xNorm,yNorm) → float 标量场和颜色映射，
 * 使 TransitionEngine 能在参数场层面做 smoothstep 插值 ——
 * 效果等同于 Psychedelic 内部 6 形态之间的 morphing 质量。
 */

#pragma once

#include "IVisualEffect.h"
#include "FastMath.h"
#include <cstdint>
#include <cmath>

// ============================================================================
// 共享预计算三角函数表 —— Psychedelic & GlitchForge 使用完全相同的结构
// ============================================================================
struct PrecompData {
    float speed;
    float s[61], c[61];   // sin/cos(t * k/100 * speed), k=15..60
    float sr[61], cr[61]; // sin/cos(t * k/100) raw, k=15..60
};

inline void BuildPrecompData(PrecompData& pc, float t, float bpm) {
    float sp = bpm / 120.0f;
    pc.speed = sp;
    for (int k = 15; k <= 60; ++k) {
        float a = t * (k / 100.0f) * sp;
        pc.s[k] = sinf(a);
        pc.c[k] = cosf(a);
    }
    for (int k = 15; k <= 60; ++k) {
        float a = t * (k / 100.0f);
        pc.sr[k] = sinf(a);
        pc.cr[k] = cosf(a);
    }
}

// ============================================================================
// IProceduralEffect —— 支持跨效果数学过渡的可视化效果
// ============================================================================
class IProceduralEffect : public IVisualEffect {
public:
    virtual ~IProceduralEffect() = default;

    /// 每帧调用一次，效果内部推进时间并重建预计算数据
    /// @param dt 帧间隔（秒），通常 0.016
    /// @param bpm 当前 BPM
    virtual void PrepareFrame(float dt, float bpm) = 0;

    /// 评估归一化屏幕坐标处的模式场标量值
    /// @param xNorm 归一化 X 坐标 [-1, 1]
    /// @param yNorm 归一化 Y 坐标 [-1, 1]
    /// @return 模式场值 [-1, 1]，0 为空白/背景区域
    virtual float EvalPattern(float xNorm, float yNorm) const = 0;

    /// 将模式场值映射为像素颜色
    /// @param pattern EvalPattern 返回的标量值（可来自其他效果的混合结果）
    /// @param xNorm  归一化 X 坐标
    /// @param yNorm  归一化 Y 坐标
    /// @param beatPhase 节拍相位 [0, 1]
    virtual uint32_t MapToColor(float pattern, float xNorm, float yNorm,
                                float beatPhase) const = 0;

    /// 获取当前帧的预计算三角函数表
    virtual const PrecompData& GetPrecomp() const = 0;

    /// 渲染质量除数
    int GetQuality() const override = 0;
};
