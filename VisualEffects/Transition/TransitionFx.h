// ============================================================================
// TransitionFx —— 跨效果过渡（MilkDrop 式）
//
// 以前过渡是"每个效果各自一份"：每个 .frag 尾部都写着 uEncode / uMorphBlend /
// uOldEncoded 那 12 行，24 个效果复制了 24 遍，而且已经分叉成"纯颜色混合"和
// "标量场再上色"两派。改一次曲线要改 24 个文件。
//
// 现在过渡属于引擎：两个效果各自照常渲染成一张图（旧效果继续按自己的时间轴跑），
// 由本模块合成。MilkDrop 的做法是"两套 per-vertex UV 场按进度混合"，这里没有
// per-vertex 场可借，退一步用 **共享位移场 + 余弦插值 + 轻微 zoom**：
//   · 位移在中段最大、两端为 0（sin(π·p) 钟形），所以起止两帧都是纯净画面
//   · 旧画面略放大、新画面略缩小 → 交接时有纵深，不是两张图叠着
//   · 混合曲线是 MilkDrop 的 CosineInterp：0.5 - 0.5·cos(π·p)
//
// 用法（main.cpp 的 PL_BLUR 分支）：
//   TransitionFx::Ensure(w, h);
//   旧效果 → TransitionFx::FboOld()
//   新效果 → TransitionFx::FboNew()
//   glBindFramebuffer(GL_FRAMEBUFFER, 0);
//   TransitionFx::Composite(w, h, g_plBlurT, params);
// ============================================================================
#pragma once

#include "../GLFuncs.h"

namespace TransitionFx {

struct Params {
    float warp  = 1.0f;   // 位移强度：0 = 纯淡入淡出
    float zoom  = 1.2f;   // 旧画面在过渡结束时放大的倍数（1.2 → 2.2×）
    float rot   = 0.35f;  // 旧画面在过渡结束时的旋转量（弧度）
    float bass  = 0.0f;   // 低音包络（0..1），放大位移
};

/// 确保两张离屏纹理就绪；尺寸变化会重建。失败返回 false（调用方应退回直接画新效果）。
bool Ensure(int w, int h);

/// 两张离屏 FBO（渲染前 glBindFramebuffer 到它们）。
GLuint FboOld();
GLuint FboNew();

/// 把两张纹理按 progress(0..1) 合成到【当前帧缓冲】。
void Composite(int w, int h, float progress, const Params& p);

/// 释放所有 GL 资源（上下文销毁前调用）。
void Shutdown();

} // namespace TransitionFx
