/**
 * @file F1Geom.h
 * @brief Function1（Zindler conoid）的几何与 movement —— 纯数学，不碰 GL
 *
 * ---------------------------------------------------------------------------
 * 公式（取自 effect reference/Formula.jpg，以它为准）
 * ---------------------------------------------------------------------------
 *      x = cos(u)·( 2v/π − tanh v )
 *      y = cos(u + φ) / cosh v
 *      z = cos(u − φ) / cosh v            φ = 2π/3        0 ≤ u, v ≤ 2π
 *
 * 【形状】沿 x 轴的一只号角：v=0 处断面最大（cosh 0 = 1），v 走到 2π 时 1/cosh v → 0
 * 收成尖，同时 x 的系数 (2v/π − tanh v) 从 0 长到 ≈3。也就是"断面慢慢收、轴向慢慢伸"。
 * 包围盒 x∈[−3,3]、y,z∈[−1,1]（乘上 F1_SCALE）。
 *
 * 【它不是直纹面】—— 别被 "conoid" 这个名字骗了：固定 v、取 u = 0/π/2/π 三点，
 * 共线残差 sin ≈ 0.88~0.99，根本不走直线族（古典的 conoid 要求由直线族生成）。
 * 所以这里的 conoid 是广义用法。
 *
 * 【这一版全程无奇点】—— 早期我在 MathWorld 上找到的那个 z = a·tan 2v 版本在 v→45°
 * 会发散；这一个 0 ≤ v ≤ 2π 全程 bounded，movement 才敢把 v 拉到满。
 *
 * ---------------------------------------------------------------------------
 * movement（用户 2026-09-17 定）
 * ---------------------------------------------------------------------------
 *   · 入场：u 的范围由 0 长到 1（× 2π），smootherstep，F1_ENTRY_DUR 秒。
 *     注意 u 是【绕断面】的参数：u 范围 0 时整张曲面塌成一条线 —— 入场是"张开"。
 *   · v 的范围受鼓点驱动：命中时冲到最高（冲满 = 1.0）再指数回落；
 *     没有鼓点时轻度跟着音频走，落在 [F1_IDLE_LO, F1_IDLE_HI] = [0.29, 0.43]。
 *   · 整体缩放、分段数都是定值（F1_SCALE / F1_NU / F1_NV）。
 *
 * ---------------------------------------------------------------------------
 * 与渲染侧的约定
 * ---------------------------------------------------------------------------
 *   顶点缓冲里【只存归一化的 (u,v) 参数】＝ (i/nu, j/nv)，两个 float。
 *   曲面点与法线【全部在顶点着色器里按 uniform 现算】——
 *   因为 movement 每帧都在改 u/v 范围，烘死在顶点里就得每帧重建整条 VBO；
 *   存参数的话网格只建一次，每帧只更新几个 uniform。
 *   法线由 ∂P/∂u × ∂P/∂v 的有限差分得到：它是 uniform 的纯函数，逐帧确定、可复现
 *   （和 dFdx/dFdy 那种屏幕空间导数不是一回事，那个在本项目里被证实会抖）。
 */
#pragma once
#include <vector>
#include <cmath>

// ---------------------------------------------------------------------------
// movement 常数（要和 Function1_zindler.html 里那一份保持一致）
// ---------------------------------------------------------------------------
const float F1_ENTRY_DUR = 1.40f;   // 入场：u 范围 0 → 1 的时长（秒）
const float F1_IDLE_LO   = 0.29f;   // v 范围空闲段下限
const float F1_IDLE_HI   = 0.43f;   // v 范围空闲段上限
const float F1_HIT_RISE  = 0.10f;   // 鼓点冲高的上升时间（秒）
const float F1_HIT_DECAY = 0.35f;   // 冲到最高之后的回落时间常数（秒）

// ---------------------------------------------------------------------------
// 定值参数
// ---------------------------------------------------------------------------
const float F1_SCALE = 1.05f;       // 整体缩放（观感，不改方程）
const int   F1_NU    = 84;          // u 方向分段
const int   F1_NV    = 50;          // v 方向分段

/// 每顶点 2 个 float：归一化的 (u, v) 参数
inline int F1VertexStride() { return 2; }
/// 顶点数：(nu+1)·(nv+1) —— 84×50 的网格每个方向都要有端点，所以各 +1
inline int F1VertexCount()  { return (F1_NU + 1) * (F1_NV + 1); }

/// 生成参数网格（只生成一次）。out 按 [uNorm, vNorm] 顺序追加。
inline void F1BuildGrid(std::vector<float>& out) {
    out.clear();
    out.reserve((size_t)F1VertexCount() * F1VertexStride());
    for (int j = 0; j <= F1_NV; ++j) {
        const float vN = (float)j / (float)F1_NV;
        for (int i = 0; i <= F1_NU; ++i) {
            out.push_back((float)i / (float)F1_NU);
            out.push_back(vN);
        }
    }
}

/// 索引数：每格 2 个三角形 × 3 个索引
inline int F1IndexCount() { return F1_NU * F1_NV * 6; }

/// 生成索引（三角形列表）。顶点编号 = j·(NU+1) + i。
/// 【必须用 glDrawElements，不能 glDrawArrays】—— 这是个参数网格，顶点是共享的；
/// 直接 DrawArrays 的话"连续三个顶点"凑出来的全是退化三角形，整张曲面几乎什么都画不出来
/// （实测只画出了 290 个像素，剩下全是背景 —— 我一开始就是照抄了 SS 的 DrawArrays，
/// 而 SS 的网格是每格显式吐 6 个顶点的三角形列表，两者不是一回事）。
inline void F1BuildIndices(std::vector<unsigned short>& out) {
    out.clear();
    out.reserve((size_t)F1IndexCount());
    for (int j = 0; j < F1_NV; ++j) {
        for (int i = 0; i < F1_NU; ++i) {
            const unsigned short a = (unsigned short)(j * (F1_NU + 1) + i);
            const unsigned short b = (unsigned short)(a + 1);
            const unsigned short c = (unsigned short)(a + F1_NU + 1);
            const unsigned short d = (unsigned short)(c + 1);
            out.push_back(a); out.push_back(c); out.push_back(b);
            out.push_back(b); out.push_back(c); out.push_back(d);
        }
    }
}

/// 曲面上的点。uNorm/vNorm 是 [0,1] 的归一化参数，uFrac/vFrac 是 movement 给的范围。
inline void F1Surf(float uNorm, float vNorm, float uFrac, float vFrac,
                   float phi, float s, float* out3) {
    const float TAU = 6.28318530717959f;
    const float u = uNorm * uFrac * TAU;
    const float v = vNorm * vFrac * TAU;
    const float A = 2.0f * v / 3.14159265358979f - std::tanh(v);
    const float B = 1.0f / std::cosh(v);
    out3[0] = s * std::cos(u) * A;
    out3[1] = s * std::cos(u + phi) * B;
    out3[2] = s * std::cos(u - phi) * B;
}

// ---------------------------------------------------------------------------
// movement 取值
// ---------------------------------------------------------------------------
/// 入场进度 [0,1]：smootherstep（起步慢、中间快、收尾慢）
inline float F1EntryFrac(float t) {
    float x = t / F1_ENTRY_DUR;
    x = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
    return x * x * x * (x * (x * 6.0f - 15.0f) + 10.0f);
}

/// 鼓点包络 [0,1]：冲到 1 再指数回落。
/// 上升段用 smoothstep（起手不硬），顶点精确落在 age = F1_HIT_RISE 处且值正好是 1 ——
/// "冲到最高"是【保证出来的】，不是靠参数凑巧撞上。
inline float F1HitEnv(float age) {
    if (age >= 6.0f) return 0.0f;                 // 早就停了（也避开大参数的精度问题）
    const float a = age < 0.0f ? 0.0f : age;
    const float r = a / F1_HIT_RISE;
    const float rr = r < 0.0f ? 0.0f : (r > 1.0f ? 1.0f : r);
    const float rs = rr * rr * (3.0f - 2.0f * rr);
    const float dec = (a > F1_HIT_RISE) ? std::exp(-(a - F1_HIT_RISE) / F1_HIT_DECAY) : 1.0f;
    return rs * dec;
}

/// v 的范围：空闲段（随能量在 [LO,HI] 之间）+ (1 − 空闲段)·鼓点包络 → 峰值正好到 1.0
inline float F1VFrac(float energy, float hitAge) {
    const float e = energy < 0.0f ? 0.0f : (energy > 1.0f ? 1.0f : energy);
    const float idle = F1_IDLE_LO + (F1_IDLE_HI - F1_IDLE_LO) * e;
    return idle + (1.0f - idle) * F1HitEnv(hitAge);
}
