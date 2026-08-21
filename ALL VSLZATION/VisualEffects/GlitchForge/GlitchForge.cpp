/**
 * @file GlitchForge.cpp
 * @brief 视频故障效果 —— 扫描线撕裂 / 通道分离 / 块损坏 / 波浪扭曲
 */

#include "GlitchForge.h"
#include "../ThreadPool.h"
#include <algorithm>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

GlitchForge::GlitchForge()
    : m_currentMode(SCANLINE_TEAR), m_targetMode(SCANLINE_TEAR)
    , m_modeBlend(1), m_modeBlendSpeed(0.06f)
    , m_colorScheme(COLOR_GREEN_CRT)
{ memset(&m_pc, 0, sizeof(m_pc)); }

const wchar_t* GlitchForge::GetName() const { return L"GlitchForge 视频故障"; }

void GlitchForge::SyncBeatState(float bpm, float ph, bool tr) {
    m_bpm = bpm; m_beatPhase = ph; m_beatTrigger = tr;
    if (tr) { m_beatCount++; NextMode(); }
    m_beatInterval = 60.0f / m_bpm;
}

void GlitchForge::NextMode() {
    m_targetMode = static_cast<GlitchMode>((m_targetMode + 1) % MODE_COUNT);
    m_modeBlend = 0;
    m_modeBlendSpeed = 0.04f + (m_bpm - 40) / 260.0f * 0.10f;
}

void GlitchForge::NextColorScheme() {
    m_colorScheme = static_cast<ColorScheme>((m_colorScheme + 1) % COLOR_COUNT);
}

const wchar_t* GlitchForge::GetModeName() const {
    static const wchar_t* n[] = { L"扫描线撕裂", L"通道分离", L"块状损坏", L"波浪扭曲" };
    return n[m_currentMode];
}

const wchar_t* GlitchForge::GetColorSchemeName() const {
    static const wchar_t* n[] = {
        L"绿色CRT", L"毒液撕裂", L"VHS溢血",
        L"热力崩溃", L"霓虹损坏", L"数据乱码"
    };
    return n[m_colorScheme];
}

// ============================================================================
// IProceduralEffect 接口实现
// ============================================================================

void GlitchForge::PrepareFrame(float dt, float bpm) {
    m_time += dt;
    m_bpm = bpm;

    if (m_currentMode != m_targetMode) {
        m_modeBlend += m_modeBlendSpeed;
        if (m_modeBlend >= 1) {
            m_modeBlend = 1;
            m_currentMode = m_targetMode;
        }
    }

    BuildPrecompData(m_pc, m_time, m_bpm);
    m_pc.speed *= m_userSpeed;
}

void GlitchForge::ComputeDisplacedCoords(float xNorm, float yNorm,
                                          float& r, float& theta,
                                          float& rDispR, float& rDispB) const {
    // 笛卡尔 → 极坐标 + 万花筒折叠（与 LUT 构建逻辑一致）
    r = sqrtf(xNorm * xNorm + yNorm * yNorm);
    float th = atan2f(yNorm, xNorm);
    if (th < 0) th += 2.0f * M_PI;
    const float dw = 10.0f, wA = 2.0f * M_PI / dw;
    float wi = floorf(th / wA);
    float tw = th - wi * wA;
    if (fmodf(wi, 2.0f) >= 1.0f) tw = wA - tw;
    theta = tw / wA;  // 归一化 [0, 1]
    GlitchDisplace(m_currentMode, r, theta, m_time, m_pc, r, theta, rDispR, rDispB);
}

float GlitchForge::EvalPattern(float xNorm, float yNorm) const {
    float r, theta, rDispR, rDispB;
    ComputeDisplacedCoords(xNorm, yNorm, r, theta, rDispR, rDispB);
    return BasePattern(r, theta, m_time, m_pc.speed, m_pc);
}

uint32_t GlitchForge::MapToColor(float pattern, float xNorm, float yNorm,
                                  float beatPhase) const {
    float r, theta, rDispR, rDispB;
    ComputeDisplacedCoords(xNorm, yNorm, r, theta, rDispR, rDispB);

    // 径向色散模式：R/B 通道沿径向偏移采样
    if (fabsf(rDispR) > 0.0005f || fabsf(rDispB) > 0.0005f) {
        float pR = BasePattern(r + rDispR, theta, m_time, m_pc.speed, m_pc);
        float pB = BasePattern(r + rDispB, theta, m_time, m_pc.speed, m_pc);
        uint32_t cR = MapGlitchColor(m_colorScheme, pR, r + rDispR, theta, m_time, m_pc, beatPhase);
        uint32_t cG = MapGlitchColor(m_colorScheme, pattern, r, theta, m_time, m_pc, beatPhase);
        uint32_t cB = MapGlitchColor(m_colorScheme, pB, r + rDispB, theta, m_time, m_pc, beatPhase);
        uint8_t cr = (cR >> 16) & 0xFF, cg = (cG >> 8) & 0xFF, cb = cB & 0xFF;
        return ARGB(255, cr, cg, cb);
    }

    return MapGlitchColor(m_colorScheme, pattern, r, theta, m_time, m_pc, beatPhase);
}

// ============================================================================
void GlitchForge::HSLtoRGB(float h, float s, float l, uint8_t& r, uint8_t& g, uint8_t& b) {
    h = fmodf(h, 360.0f); if (h < 0) h += 360;
    float c = (1 - fabsf(2 * l - 1)) * s, hp = h / 60;
    float x = c * (1 - fabsf(fmodf(hp, 2) - 1)), m = l - c * .5f;
    float r1, g1, b1;
    if (hp < 1) { r1 = c; g1 = x; b1 = 0; }
    else if (hp < 2) { r1 = x; g1 = c; b1 = 0; }
    else if (hp < 3) { r1 = 0; g1 = c; b1 = x; }
    else if (hp < 4) { r1 = 0; g1 = x; b1 = c; }
    else if (hp < 5) { r1 = x; g1 = 0; b1 = c; }
    else { r1 = c; g1 = 0; b1 = x; }
    r = (uint8_t)((r1 + m) * 255); g = (uint8_t)((g1 + m) * 255); b = (uint8_t)((b1 + m) * 255);
}

// ============================================================================
// BasePattern —— 极坐标 VHS 条纹基底（同心弧 + 径向色带）
// ============================================================================
float GlitchForge::BasePattern(float r, float theta, float t, float speed, const PrecompData& pc) const {
    float a = theta * 2.0f * M_PI;
    float v = 0;
    v += fast_sin(a * 3.0f + t * 0.25f * speed) * 0.5f;
    v += fast_sin(a * 7.0f - t * 0.18f * speed) * 0.3f;
    v += fast_cos(r * 2.5f + a * 1.2f + t * 0.35f * speed) * 0.2f;
    v += fast_sin(r * 3.0f + t * 0.4f * speed) * fast_cos(a * 1.5f - t * 0.3f * speed) * 0.25f;
    v += fast_sin(r * 6.0f + a * 4.0f + pc.sr[25] * speed) * 0.12f;
    return v;
}

// ============================================================================
// GlitchDisplace —— 4 种极坐标故障位移
// dr/dtheta: 主坐标位移, rDispR/rDispB: R/B 通道径向偏移（色散用）
// ============================================================================
void GlitchForge::GlitchDisplace(GlitchMode mode, float r, float theta, float t,
                                  const PrecompData& pc, float& dr, float& dtheta,
                                  float& rDispR, float& rDispB) const {
    float sp = pc.speed, a = theta * 2.0f * M_PI;
    dr = r; dtheta = theta; rDispR = 0; rDispB = 0;

    switch (mode) {
    case SCANLINE_TEAR:
        dr = r + fast_sin(a * 12.0f + t * 1.3f * sp) * 0.08f
               + fast_sin(a * 28.0f + t * 1.7f * sp) * 0.04f
               + fast_cos(a * 6.0f - t * 2.1f * sp) * 0.025f;
        { float spike = fast_sin(a * 50.0f + pc.sr[35] * sp);
          if (spike > 0.92f) dr += (spike - 0.92f) * 0.6f; }
        if (dr < 0) dr = 0; if (dr > 1.5f) dr = 1.5f;
        break;

    case CHROMA_SPLIT:
        rDispR = fast_sin(a * 9.0f + t * 1.5f * sp) * 0.03f
               + fast_cos(a * 15.0f - t * 2.0f * sp) * 0.015f;
        rDispB = -rDispR * 0.7f + fast_sin(a * 7.0f + t * 1.9f * sp) * 0.01f;
        dr = r + fast_sin(a * 5.0f + t * 0.8f * sp) * 0.015f;
        break;

    case BLOCK_CORRUPT: {
        float ba = floorf(a * 3.0f / M_PI);   // 角向 3 扇区
        float br = floorf(r * 3.0f);           // 径向 3 环
        float ha = fast_sin(ba * 127.1f + br * 311.7f + t * 0.7f * sp) * 0.5f
                 + fast_sin(ba * 269.5f + br * 183.3f - t * 0.5f * sp) * 0.3f
                 + fast_cos(ba * 53.0f - br * 91.0f + t * 1.1f * sp) * 0.2f;
        float hr = fast_cos(ba * 89.0f + br * 173.0f + t * 0.9f * sp) * 0.4f
                 + fast_sin(ba * 211.0f - br * 57.0f + t * 0.6f * sp) * 0.3f;
        dr = r + hr * 0.10f;
        dtheta = theta + ha * 0.015f;
        if (fabsf(ha) > 0.8f) dtheta += ha * 0.025f;
        if (dr < 0) dr = 0; if (dr > 1.5f) dr = 1.5f;
        break;
    }

    case WAVE_GLITCH:
        dr = r + fast_sin(a * 5.0f + t * 1.0f * sp) * 0.06f
               + fast_cos(r * 6.0f + a * 3.0f + t * 1.6f * sp) * 0.035f
               + fast_sin(r * 10.0f - a * 7.0f + pc.sr[40] * sp) * 0.025f;
        dtheta = theta + (fast_cos(r * 8.0f + t * 1.2f * sp) * 0.035f
                        + fast_sin(a * 10.0f + r * 4.0f - t * 1.4f * sp) * 0.02f) / (2.0f * M_PI);
        rDispR = fast_sin(a * 13.0f - t * 1.8f * sp) * 0.012f;
        rDispB = -rDispR;
        if (dr < 0) dr = 0; if (dr > 1.5f) dr = 1.5f;
        break;
    }
}

// ============================================================================
// MapGlitchColor —— 6 种配色方案
// ============================================================================
uint32_t GlitchForge::MapGlitchColor(ColorScheme cs, float pat, float r, float theta,
                                      float t, const PrecompData& pc, float phase) const {
    float p = fabsf(pat);
    if (p < 0.42f) return m_darkBg ? 0xFF000000 : 0xFFFFFFFF;

    float brightness;
    const float T0 = 0.42f, T1 = 0.56f, T2 = 0.70f, T3 = 0.84f, T4 = 0.96f;
    if (p < T1)      brightness = (p - T0) / (T1 - T0) * 0.35f;
    else if (p < T2) brightness = 0.35f + (p - T1) / (T2 - T1) * 0.35f;
    else if (p < T3) brightness = 0.70f + (p - T2) / (T3 - T2) * 0.20f;
    else if (p < T4) brightness = 0.90f + (p - T3) / (T4 - T3) * 0.08f;
    else             brightness = 1.0f;

    float bp = 1 - phase;
    brightness += bp * 0.06f;
    if (brightness > 1) brightness = 1;
    if (brightness < 0) brightness = 0;

    uint8_t r8, g8, b8;
    float hue, sat, light = 0.92f - brightness * 0.22f;
    float sp = pc.speed;

    switch (cs) {
    default:
    case COLOR_GREEN_CRT:
        hue = 105 + brightness * 50 + bp * 10;
        sat = brightness * 0.5f + 0.10f; break;
    case COLOR_TOXIC_GLITCH:
        hue = (brightness < 0.5f) ? 115 + brightness * 30
             : 270 + (brightness - 0.5f) * 40;
        sat = brightness * 0.55f + 0.10f; break;
    case COLOR_VHS_BLEED:
        hue = 180 + fast_cos(r * 15.0f + t * sp) * 30 + bp * 15;
        sat = brightness * 0.50f + 0.10f; break;
    case COLOR_THERMAL_CRASH:
        hue = 3 + brightness * 50 + bp * 10;
        sat = brightness * 0.60f + 0.10f; break;
    case COLOR_NEON_DAMAGE:
        hue = fmodf(310 + brightness * 100 + r * 10.0f + t * 20.0f * sp, 360.0f);
        sat = brightness * 0.55f + 0.10f; break;
    case COLOR_DATA_RAINBOW:
        hue = fmodf(theta * 360.0f + r * 180.0f + pc.sr[15] * 15 + phase * 40, 360.0f);
        sat = brightness * 0.55f + 0.10f; break;
    }

    if (sat < 0) sat = 0; if (sat > 1) sat = 1;
    if (light < 0) light = 0; if (light > 1) light = 1;

    HSLtoRGB(hue, sat, light, r8, g8, b8);

    // 同心弧扫描线效果
    int rowMod = ((int)(theta * 4000.0f)) % 3;
    if (rowMod == 0) {
        r8 = (uint8_t)(r8 * 0.85f);
        g8 = (uint8_t)(g8 * 0.85f);
        b8 = (uint8_t)(b8 * 0.85f);
    }

    return ARGB(255, r8, g8, b8);
}

// ============================================================================
void GlitchForge::RebuildBuf(int fw, int fh) {
    int rw = fw / QUALITY, rh = fh / QUALITY;
    if (rw < 1) rw = 1; if (rh < 1) rh = 1;
    if (m_fw == fw && m_fh == fh && m_rw == rw && m_rh == rh) return;
    m_fw = fw; m_fh = fh; m_rw = rw; m_rh = rh;
    int n = rw * rh;
    m_buf.resize(n);
    m_kLut.resize(n);

    // 预计算极坐标 + 万花筒折叠（窗口 resize 时才重建）
    float cx = fw * .5f, cy = fh * .5f, mr = std::min(cx, cy);
    if (mr < 1) mr = 1;
    const float dw = 10.0f, wA = 2.0f * M_PI / dw;
    for (int sy = 0; sy < rh; ++sy) {
        for (int sx = 0; sx < rw; ++sx) {
            float px = ((sx + 0.5f) * QUALITY - cx) / mr;
            float py = ((sy + 0.5f) * QUALITY - cy) / mr;
            float kr = sqrtf(px * px + py * py);
            float kth = atan2f(py, px);
            if (kth < 0) kth += 2.0f * M_PI;
            float wi = floorf(kth / wA);
            float tw = kth - wi * wA;
            if (fmodf(wi, 2.0f) >= 1.0f) tw = wA - tw;
            int idx = sy * rw + sx;
            m_kLut[idx].r     = kr;
            m_kLut[idx].theta = tw / wA;  // 归一化 [0, 1]
        }
    }
}

// ============================================================================
void GlitchForge::RenderChunk(int start, int end, GlitchMode cur, GlitchMode tgt,
                               float blend, float bpm, float phase,
                               ColorScheme cs, const PrecompData& pc, float t, float speed) {
    float sb = blend * blend * (3.0f - 2.0f * blend);
    bool bl = (sb < 1);
    float ib = 1 - sb;

    for (int i = start; i < end; ++i) {
        float kr = m_kLut[i].r;
        float kt = m_kLut[i].theta;

        if (bl) {
            float dr1, dt1, rR1, rB1, dr2, dt2, rR2, rB2;
            GlitchDisplace(cur, kr, kt, t, pc, dr1, dt1, rR1, rB1);
            GlitchDisplace(tgt, kr, kt, t, pc, dr2, dt2, rR2, rB2);
            float dr  = dr1 * ib + dr2 * sb;
            float dt  = dt1 * ib + dt2 * sb;
            float dR  = rR1 * ib + rR2 * sb;
            float dB  = rB1 * ib + rB2 * sb;

            if (fabsf(dR) > 0.0005f || fabsf(dB) > 0.0005f) {
                float pR = BasePattern(kr + dR, kt, t, speed, pc);
                float pG = BasePattern(kr, kt, t, speed, pc);
                float pB = BasePattern(kr + dB, kt, t, speed, pc);
                uint32_t cR = MapGlitchColor(cs, pR, kr + dR, kt, t, pc, phase);
                uint32_t cG = MapGlitchColor(cs, pG, kr, kt, t, pc, phase);
                uint32_t cB = MapGlitchColor(cs, pB, kr + dB, kt, t, pc, phase);
                m_buf[i] = ARGB(255, (cR>>16)&0xFF, (cG>>8)&0xFF, cB&0xFF);
            } else {
                float pat = BasePattern(dr, dt, t, speed, pc);
                m_buf[i] = MapGlitchColor(cs, pat, dr, dt, t, pc, phase);
            }
        } else {
            float dr, dt, dR, dB;
            GlitchDisplace(cur, kr, kt, t, pc, dr, dt, dR, dB);
            if (fabsf(dR) > 0.0005f || fabsf(dB) > 0.0005f) {
                float pR = BasePattern(kr + dR, kt, t, speed, pc);
                float pG = BasePattern(kr, kt, t, speed, pc);
                float pB = BasePattern(kr + dB, kt, t, speed, pc);
                uint32_t cR = MapGlitchColor(cs, pR, kr + dR, kt, t, pc, phase);
                uint32_t cG = MapGlitchColor(cs, pG, kr, kt, t, pc, phase);
                uint32_t cB = MapGlitchColor(cs, pB, kr + dB, kt, t, pc, phase);
                m_buf[i] = ARGB(255, (cR>>16)&0xFF, (cG>>8)&0xFF, cB&0xFF);
            } else {
                float pat = BasePattern(dr, dt, t, speed, pc);
                m_buf[i] = MapGlitchColor(cs, pat, dr, dt, t, pc, phase);
            }
        }
    }
}

// ============================================================================
// Render —— 独立渲染（HDC 输出）
// ============================================================================
void GlitchForge::Render(HDC hdc, const RECT& rect, const float*, int, const float*, int) {
    int fw = rect.right - rect.left, fh = rect.bottom - rect.top;
    if (fw <= 0 || fh <= 0) return;

    PrepareFrame(0.016f, m_bpm);

    RebuildBuf(fw, fh);
    int total = m_rw * m_rh;

    unsigned nTh = std::thread::hardware_concurrency();
    if (nTh < 1) nTh = 1; if (nTh > 16) nTh = 16;
    int cs_ = total / nTh; if (cs_ < 1) cs_ = 1;
    float t = m_time, speed = m_pc.speed;

    static ThreadPool pool;
    for (unsigned ti = 0; ti < nTh; ++ti) {
        int s = ti * cs_, e = (ti == nTh - 1) ? total : s + cs_;
        pool.Enqueue([this, s, e, t, speed] {
            RenderChunk(s, e, m_currentMode, m_targetMode,
                        m_modeBlend, m_bpm, m_beatPhase, m_colorScheme,
                        std::cref(m_pc), t, speed);
        });
    }
    pool.Wait();

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = m_rw; bmi.bmiHeader.biHeight = -(int)m_rh;
    bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32; bmi.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(hdc, HALFTONE);
    StretchDIBits(hdc, 0, 0, fw, fh, 0, 0, m_rw, m_rh,
                  m_buf.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
}
