// ============================================================================
// Filigree 渲染器的对外接口（实现全在 TNRender.cpp）
//
// 它原来长在 main.cpp 里（约 600 行：程序编译、FBO 三件套、六趟绘制、状态清零）。
// 搬出来之后 main.cpp 只剩一行调用，这个效果的状态也真正归它自己。
//
// 【要 include 在 GLFuncs.h 的"定义"块之后】—— 见 GLFuncs.h 里的顺序说明。
// ============================================================================
#pragma once

#include "../GLFuncs.h"

/// 每帧渲染一帧 Filigree。
/// 【为什么这些参数要显式传】它们原来直接吃 main.cpp 的全局量，搬出去之后必须由调用方
/// 明确交进来 —— 隐式依赖全局量正是"效果堆在 main.cpp 里"的成因。
///   quadVao   —— main.cpp 的 NDC 全屏四边形（背景/亮部/模糊/合成四趟都用它）
///   fft       —— 音乐频谱，AudioFrame::fft（16 个对数频段，0..1）。
///                贴在细条的两条长边上：亮度 = 该频段响度，颜色随机（每下鼓点换一次）。
///                长度必须是 kAudioBands（== TN_SPEC_BANDS），main.cpp 有 static_assert 兜着。
///   beatFlash —— 音频鼓点闪光（用它的上升沿判鼓点，同 Involute / Function1）
///   beatCount —— 已发生的鼓点数（剪纸换批的种子 + 频谱随机色的种子）
void RenderFiligree(float t, float speed, float w, float h, float opacity, bool darkBg,
                    GLuint quadVao, const float* fft, float beatFlash, unsigned beatCount);

/// 清掉本渲染器的全部 GL 资源与状态（重建 GL 上下文 / 重开窗口时调用）。
void ResetTNRenderer();
