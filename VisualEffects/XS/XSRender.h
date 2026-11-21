// ============================================================================
// Shardstorm 渲染器的对外接口（实现全在 XSRender.cpp）
//
// 【要 include 在 GLFuncs.h 的"定义"块之后】—— 见 GLFuncs.h 里的顺序说明。
// ============================================================================
#pragma once

#include "../GLFuncs.h"

/// 每帧渲染一帧 Shardstorm。
///   quadVao   —— main.cpp 的 NDC 全屏四边形（背景/亮部/模糊/合成四趟都用它）
///   beatFlash —— 音频鼓点闪光（上升沿判鼓点，同其它 mesh 效果）
///   beatCount —— 已发生的鼓点数（逐片闪烁/换色的种子）
///   exitT     —— 出场计时（切走时的退场动画；>=1 表示不在退场）
void RenderShardstorm(float t, float speed, float w, float h, float opacity, bool darkBg,
                      GLuint quadVao, float beatFlash, unsigned beatCount, float exitT);

/// 清掉本渲染器的全部 GL 资源与状态（重建 GL 上下文 / 重开窗口时调用）。
void ResetXS();
