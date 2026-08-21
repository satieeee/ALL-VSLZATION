/**
 * @file TransitionEngine.h
 * @brief 跨效果数学过渡引擎 —— 在参数场层面做 smoothstep morphing
 *
 * 核心算法（逐像素）：
 *   oldPat = oldEffect->EvalPattern(xNorm, yNorm)
 *   newPat = newEffect->EvalPattern(xNorm, yNorm)
 *   blend_s = smoothstep(blendT)
 *   blendedPat = oldPat*(1-blend_s) + newPat*blend_s
 *   oldColor = oldEffect->MapToColor(blendedPat, xNorm, yNorm, phase)
 *   newColor = newEffect->MapToColor(blendedPat, xNorm, yNorm, phase)
 *   pixel = lerp(oldColor, newColor, blend_s)
 *
 * 这与 Psychedelic 内部 6 形态互切的逻辑完全同构 ——
 * 形态函数 EvalForm 替换为不同效果的 EvalPattern，其余不变。
 */

#pragma once

#include "IProceduralEffect.h"
#include "FastMath.h"
#include <vector>
#include <thread>
#include <cstring>
#include <algorithm>

class TransitionEngine {
public:
    /// 渲染一帧跨效果数学过渡
    /// @param oldEffect 旧效果（过渡起点）
    /// @param newEffect 新效果（过渡终点）
    /// @param blendT    过渡进度 [0, 1]，0=纯旧效果，1=纯新效果
    /// @param dt        帧间隔（秒）
    /// @param bpm       当前 BPM
    /// @param beatPhase 节拍相位 [0, 1]
    /// @param hdc       目标设备上下文
    /// @param rect      目标矩形
    static void RenderTransition(
        IProceduralEffect* oldEffect,
        IProceduralEffect* newEffect,
        float blendT,
        float dt,
        float bpm,
        float beatPhase,
        HDC hdc,
        const RECT& rect,
        int renderW = -1,   // 固定渲染分辨率（-1 = 用 rect 尺寸）
        int renderH = -1);

private:
    struct ChunkCtx {
        IProceduralEffect* oldEffect;
        IProceduralEffect* newEffect;
        float blendS;      // smoothstepped blendT
        float beatPhase;
        int fw, fh;        // full window dimensions
        int rw, rh;        // render buffer dimensions
        float cx, cy, mr;  // center and normalization factor
        int quality;
        uint32_t* buf;
    };

    static void RenderChunk(int start, int end, const ChunkCtx& ctx);

    static inline uint32_t LerpRGBA(uint32_t a, uint32_t b, float t) {
        int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
        int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
        auto L = [t](int c0, int c1) {
            int v = (int)(c0 + (c1 - c0) * t + 0.5f);
            return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
        };
        return 0xFF000000 | (L(ar, br) << 16) | (L(ag, bg) << 8) | L(ab, bb);
    }
};

// ============================================================================
// 实现
// ============================================================================

inline void TransitionEngine::RenderChunk(int start, int end, const ChunkCtx& ctx) {
    IProceduralEffect* oldEff = ctx.oldEffect;
    IProceduralEffect* newEff = ctx.newEffect;
    float blendS = ctx.blendS;
    float phase  = ctx.beatPhase;
    int   rw     = ctx.rw;
    float cx     = ctx.cx, cy = ctx.cy, mr = ctx.mr;
    int   q      = ctx.quality;
    uint32_t* buf = ctx.buf;

    // 极值快速路径：避免无意义的采样和颜色映射
    if (blendS <= 0.0f) {
        for (int i = start; i < end; ++i) {
            int sy = i / rw, sx = i % rw;
            float xn = ((sx + 0.5f) * q - cx) / mr;
            float yn = ((sy + 0.5f) * q - cy) / mr;
            float pat = oldEff->EvalPattern(xn, yn);
            buf[i] = oldEff->MapToColor(pat, xn, yn, phase);
        }
    } else if (blendS >= 1.0f) {
        for (int i = start; i < end; ++i) {
            int sy = i / rw, sx = i % rw;
            float xn = ((sx + 0.5f) * q - cx) / mr;
            float yn = ((sy + 0.5f) * q - cy) / mr;
            float pat = newEff->EvalPattern(xn, yn);
            buf[i] = newEff->MapToColor(pat, xn, yn, phase);
        }
    } else {
        float ib = 1.0f - blendS;
        for (int i = start; i < end; ++i) {
            int sy = i / rw, sx = i % rw;
            float xn = ((sx + 0.5f) * q - cx) / mr;
            float yn = ((sy + 0.5f) * q - cy) / mr;

            // 1. 从两个效果采样模式场
            float oldPat = oldEff->EvalPattern(xn, yn);
            float newPat = newEff->EvalPattern(xn, yn);

            // 2. smoothstep 混合模式值（等同于 Psychedelic 内部形态混合逻辑）
            float blendedPat = oldPat * ib + newPat * blendS;

            // 3. 两个效果各自将混合后的模式值映射为颜色
            uint32_t oldCol = oldEff->MapToColor(blendedPat, xn, yn, phase);
            uint32_t newCol = newEff->MapToColor(blendedPat, xn, yn, phase);

            // 4. RGB 空间线性插值 —— 色彩也随形态一起过渡
            buf[i] = LerpRGBA(oldCol, newCol, blendS);
        }
    }
}

inline void TransitionEngine::RenderTransition(
    IProceduralEffect* oldEffect,
    IProceduralEffect* newEffect,
    float blendT,
    float dt,
    float bpm,
    float beatPhase,
    HDC hdc,
    const RECT& rect,
    int renderW,
    int renderH)
{
    int fw = rect.right - rect.left;
    int fh = rect.bottom - rect.top;
    if (fw <= 0 || fh <= 0) return;

    // 1. 两个效果各自更新内部状态
    oldEffect->PrepareFrame(dt, bpm);
    newEffect->PrepareFrame(dt, bpm);

    // 2. 取两个效果中较高的 Quality（数值越大分辨率越低）
    int qOld = oldEffect->GetQuality();
    int qNew = newEffect->GetQuality();
    int quality = (qOld > qNew) ? qOld : qNew;  // 过渡期间用较低分辨率，保证性能
    if (quality < 1) quality = 1;

    // 渲染基准尺寸：默认用 rect 尺寸；可指定固定分辨率（保持默认大小渲染）
    int baseW = (renderW > 0) ? renderW : fw;
    int baseH = (renderH > 0) ? renderH : fh;

    int rw = baseW / quality;
    int rh = baseH / quality;
    if (rw < 1) rw = 1;
    if (rh < 1) rh = 1;

    float cx = baseW * 0.5f;
    float cy = baseH * 0.5f;
    float mr = (cx < cy) ? cx : cy;
    if (mr < 1.0f) mr = 1.0f;

    // 3. smoothstep 缓入缓出
    float blendS = blendT * blendT * (3.0f - 2.0f * blendT);

    // 4. 分配渲染缓冲区（static 避免每帧分配）
    static std::vector<uint32_t> s_buf;
    int total = rw * rh;
    if ((int)s_buf.size() < total) s_buf.resize(total);

    // 5. 多线程渲染
    ChunkCtx ctx;
    ctx.oldEffect = oldEffect;
    ctx.newEffect = newEffect;
    ctx.blendS    = blendS;
    ctx.beatPhase = beatPhase;
    ctx.fw = fw; ctx.fh = fh;
    ctx.rw = rw; ctx.rh = rh;
    ctx.cx = cx; ctx.cy = cy; ctx.mr = mr;
    ctx.quality = quality;
    ctx.buf = s_buf.data();

    unsigned nTh = std::thread::hardware_concurrency();
    if (nTh < 1) nTh = 1;
    if (nTh > 16) nTh = 16;
    int chunkSize = total / nTh;
    if (chunkSize < 1) chunkSize = 1;

    std::vector<std::thread> threads;
    threads.reserve(nTh);
    for (unsigned ti = 0; ti < nTh; ++ti) {
        int s = ti * chunkSize;
        int e = (ti == nTh - 1) ? total : s + chunkSize;
        threads.emplace_back(RenderChunk, s, e, std::cref(ctx));
    }
    for (auto& th : threads) th.join();

    // 6. 输出到 HDC
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth  = rw;
    bmi.bmiHeader.biHeight = -(int)rh;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(hdc, HALFTONE);
    StretchDIBits(hdc, 0, 0, fw, fh,
                  0, 0, rw, rh,
                  s_buf.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
}
