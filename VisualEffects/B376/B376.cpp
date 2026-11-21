/**
 * @file B376.cpp
 * @brief b376 轮廓效果实现（CPU）—— 烘焙覆盖场 + 表层明暗着色 + 类圆变速运动
 * 与 B376.frag 1:1 同源（字段采样、letterbox、配色一致）。运动由 PrepareFrame 每帧散射重算。
 */
#include "B376.h"
#include "../audio_helpers.h"
#include "../ThreadPool.h"
#include <algorithm>
#include <cstring>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// 烘焙覆盖场 + 表层明暗场 + 部件元数据（540x720，VTracer 全部轮廓 + 参考图亮度）
#include "B376Field.h"

B376::B376() {
    m_covDisp.resize(kB376FieldW * kB376FieldH);
    m_lumDisp.resize(kB376FieldW * kB376FieldH);
    m_arcDisp.resize(kB376FieldW * kB376FieldH);
}

float B376::s_flowAmp = 0.5f;

const wchar_t* B376::GetName() const { return L"b376 轮廓"; }

void B376::PrepareFrame(float dt, float bpm) {
    m_time += dt; m_bpm = bpm;
    // 液态流动：每单体沿自己中心处的液态流轨迹独立平移 → 重算位移场
    B376DisplaceFields(m_time, m_userSpeed, s_flowAmp, m_covDisp.data(), m_lumDisp.data(), m_arcDisp.data());
}

void B376::SyncBeatState(float v, float ph, bool tr) { m_bpm = v; m_beatPhase = ph; }

const wchar_t* B376::GetFormName() const { static const wchar_t* n = L"全部轮廓"; return n; }

// ---- 视口归一化坐标 → 场内坐标（letterbox 保持参考图 3:4，图像顶部 → v=0；与 GPU toField 同源）----
void B376::ToField(float x, float y, float& u, float& v) const {
    const float kAspect = 3.0f / 4.0f;               // 参考图宽高比 0.75
    float vp = (float)m_renderW / (float)m_renderH;  // 视口长宽比
    float sx, sy;
    if (vp > kAspect) { sx = 0.5f * kAspect; sy = 0.5f; x *= kAspect / vp; }
    else              { sx = 0.5f * vp;       sy = 0.5f; y *= vp / kAspect; }
    u = (x + sx) / (2.0f * sx);
    v = 1.0f - (y + sy) / (2.0f * sy);
}

// ---- 双线性采样场（GL_LINEAR + CLAMP_TO_EDGE 同源；f 为覆盖场或明暗场）----
float B376::Sample(const std::vector<unsigned char>& f, int fw, int fh, float u, float v) {
    float x = u * fw - 0.5f, y = v * fh - 0.5f;
    int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    float fx = x - x0, fy = y - y0;
    auto cl = [&](int c, int n) { return c < 0 ? 0 : (c > n - 1 ? n - 1 : c); };
    int x1 = cl(x0 + 1, fw), y1 = cl(y0 + 1, fh);
    x0 = cl(x0, fw); y0 = cl(y0, fh);
    float p00 = f[y0 * fw + x0] / 255.0f, p10 = f[y0 * fw + x1] / 255.0f;
    float p01 = f[y1 * fw + x0] / 255.0f, p11 = f[y1 * fw + x1] / 255.0f;
    float top = p00 + (p10 - p00) * fx;
    float bot = p01 + (p11 - p01) * fx;
    return top + (bot - top) * fy;
}

// ---- 模式场：采样位移后覆盖场（单体独立液态流动已由 PrepareFrame 散射完成）----
float B376::EvalPattern(float xNorm, float yNorm) const {
    float u, v;
    ToField(xNorm, yNorm, u, v);
    float cov = 0.0f;
    if (u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f)   // letterbox 黑边=0
        cov = Sample(m_covDisp, kB376FieldW, kB376FieldH, u, v);
    float t = (cov - 0.45f) / 0.25f;                         // smoothstep(0.45, 0.70, cov)
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    return t * t * (3.0f - 2.0f * t);
}

// ---- 表层明暗：采样位移后的明暗场（与 pattern 同坐标，随部件一起移动）----
float B376::EvalSurface(float xNorm, float yNorm) const {
    float u, v;
    ToField(xNorm, yNorm, u, v);
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) return 0.0f;
    return Sample(m_lumDisp, kB376FieldW, kB376FieldH, u, v);
}

// ---- 配色：表层明暗 + 描边流光（亮度波沿单元外轮廓扫过；与 GPU mapColor 同源）----
uint32_t B376::MapToColor(float pattern, float xNorm, float yNorm, float beatPhase) const {
    (void)beatPhase;
    float u, v;
    ToField(xNorm, yNorm, u, v);
    float cov = 0.0f, surf = 0.0f, arcv = 0.0f, cb = 0.0f;
    if (u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f) {
        cov   = Sample(m_covDisp, kB376FieldW, kB376FieldH, u, v);
        surf  = Sample(m_lumDisp, kB376FieldW, kB376FieldH, u, v);
        arcv  = Sample(m_arcDisp, kB376FieldW, kB376FieldH, u, v);
        // 8 邻域模糊覆盖场（与 GPU 同源，描边流光边缘带用）
        const float kx = 3.0f / kB376FieldW, ky = 3.0f / kB376FieldH;
        cb = cov
            + Sample(m_covDisp, kB376FieldW, kB376FieldH, u + kx, v)
            + Sample(m_covDisp, kB376FieldW, kB376FieldH, u - kx, v)
            + Sample(m_covDisp, kB376FieldW, kB376FieldH, u, v + ky)
            + Sample(m_covDisp, kB376FieldW, kB376FieldH, u, v - ky)
            + Sample(m_covDisp, kB376FieldW, kB376FieldH, u + kx, v + ky)
            + Sample(m_covDisp, kB376FieldW, kB376FieldH, u - kx, v - ky)
            + Sample(m_covDisp, kB376FieldW, kB376FieldH, u + kx, v - ky)
            + Sample(m_covDisp, kB376FieldW, kB376FieldH, u - kx, v + ky);
        cb *= (1.0f / 9.0f);
    }
    float p = pattern < 0 ? 0 : (pattern > 1 ? 1 : pattern);
    float hue = 190.0f + (1.0f - surf)*55.0f;
    float sat = 0.40f + surf*0.35f;
    float body = m_darkBg ? (0.20f + surf*0.80f) * p        // 暗底：块内明暗 × 块隶属（黑底=0）
                          : (1.0f - (0.05f + surf*0.55f) * p); // 亮底：暗块叠在亮背景
    // 描边流光：模糊覆盖场 → 1-|2cb-1| 在轮廓处成 ~6px 亮带；亮度波沿弧长参数扫过（相位去同步）
    float mc = cb < (1.0f - cb) ? cb : (1.0f - cb);
    float e = 1.0f - 2.0f * mc;                                 // 1 - abs(2cb-1)
    float te = (e - 0.05f) / 0.33f;
    te = te < 0 ? 0 : (te > 1 ? 1 : te);
    float edge = te * te * (3.0f - 2.0f * te);                  // smoothstep(0.05, 0.38, 1-|2cb-1|)
    float flow = 0.5f + 0.5f * std::cos(6.2831853f * (arcv - m_time / kFlowPeriod));
    float light = body * (1.0f - edge * 0.6f) + edge * (0.30f + 0.70f * flow);
    uint8_t r8, g8, b8;
    HSLtoRGB(hue, sat, light, r8, g8, b8);
    return ARGB(r8, g8, b8);
}

void B376::HSLtoRGB(float h, float s, float l, uint8_t& r, uint8_t& g, uint8_t& b) {
    h = fmodf(h, 360.0f); if (h < 0) h += 360;
    float c = (1 - fabsf(2*l - 1)) * s, hp = h / 60;
    float x = c * (1 - fabsf(fmodf(hp,2) - 1)), m = l - c * .5f;
    float r1, g1, b1;
    if (hp < 1) { r1 = c; g1 = x; b1 = 0; }
    else if (hp < 2) { r1 = x; g1 = c; b1 = 0; }
    else if (hp < 3) { r1 = 0; g1 = c; b1 = x; }
    else if (hp < 4) { r1 = 0; g1 = x; b1 = c; }
    else if (hp < 5) { r1 = x; g1 = 0; b1 = c; }
    else { r1 = c; g1 = 0; b1 = x; }
    r = (uint8_t)((r1+m)*255); g = (uint8_t)((g1+m)*255); b = (uint8_t)((b1+m)*255);
}

// ---- 渲染缓冲 ----
void B376::RebuildBuf(int fw, int fh) {
    int rw = fw / QUALITY, rh = fh / QUALITY;
    if (rw < 1) rw = 1; if (rh < 1) rh = 1;
    if (m_renderW == rw && m_renderH == rh) return;
    m_renderW = rw; m_renderH = rh;
    m_buf.resize(rw * rh);
}

void B376::RenderChunk(int start, int end) {
    float mr = (float)std::min(m_renderW, m_renderH);
    float cx = m_renderW*0.5f, cy = m_renderH*0.5f;
    for (int i = start; i < end; ++i) {
        int px = i % m_renderW, py = i / m_renderW;
        float x = ((float)px - cx) / mr;
        float y = ((float)py - cy) / mr;
        float pat = EvalPattern(x, y);
        m_buf[i] = MapToColor(pat, x, y, m_beatPhase);
    }
}

void B376::Render(HDC hdc, const RECT& rect, const float*, int, const float*, int) {
    int fw = rect.right - rect.left, fh = rect.bottom - rect.top;
    if (fw <= 0 || fh <= 0) return;
    PrepareFrame(0.016f, m_bpm);
    RebuildBuf(fw, fh);
    int total = m_renderW * m_renderH;
    unsigned nTh = std::thread::hardware_concurrency();
    if (nTh < 1) nTh = 1; if (nTh > 16) nTh = 16;
    int cs = total / nTh; if (cs < 1) cs = 1;
    static ThreadPool pool;
    for (unsigned ti = 0; ti < nTh; ++ti) {
        int s = ti*cs, e = (ti == nTh-1) ? total : s + cs;
        pool.Enqueue([this, s, e]{ RenderChunk(s, e); });
    }
    pool.Wait();
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = m_renderW; bmi.bmiHeader.biHeight = -(int)m_renderH;
    bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32; bmi.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(hdc, HALFTONE);
    StretchDIBits(hdc, 0, 0, fw, fh, 0, 0, m_renderW, m_renderH, m_buf.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
}
