/**
 * @file Verdigris.cpp
 * @brief 多线程渲染 + IProceduralEffect 接口实现
 *
 * 风格数据依据 (BATCH SUMMARY, 5 images):
 *   S=0.17 L=0.60 → 低饱和中亮调 → T0=0.28 大淡区, sat=0.08~0.30
 *   edge density=0.19 → 中频形态函数
 *   texture complexity=0.053 → 1~2 层 trig, 简洁叠加
 *   orient concentration=0.14 → 全方向对称形态（无条纹/线状偏置）
 *   hue clusters: 93-117°(绿) & 211-219°(蓝)
 */

#include "Verdigris.h"
#include "../ThreadPool.h"
#include <algorithm>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

Verdigris::Verdigris()
    : m_currentForm(FORM_RIPPLE), m_targetForm(FORM_RIPPLE)
    , m_formBlend(1), m_formBlendSpeed(0.05f)
    , m_colorScheme(COLOR_PATINA_BLUE)
    , m_projection(PROJ_RADIAL)
{ memset(&m_pc, 0, sizeof(m_pc)); }

const wchar_t* Verdigris::GetName() const { return L"Verdigris 铜绿"; }

void Verdigris::SetBPM(float v) { m_bpm = v; if (m_bpm < 1) m_bpm = 1; m_beatInterval = 60.0f / m_bpm; }
void Verdigris::NextForm() { m_targetForm = static_cast<Form>((m_targetForm + 1) % FORM_COUNT); m_formBlend = 0; m_formBlendSpeed = 0.03f + (m_bpm - 40) / 260.0f * 0.12f; }
void Verdigris::NextColorScheme() { m_colorScheme = static_cast<ColorScheme>((m_colorScheme + 1) % COLOR_COUNT); }
void Verdigris::NextProjection() { m_projection = static_cast<Projection>((m_projection + 1) % PROJ_COUNT); }
void Verdigris::SetManualMode(bool m) { m_manualForm = m; }
void Verdigris::SyncBeatState(float v, float ph, bool tr) {
    m_bpm = v; m_beatPhase = ph; m_beatTrigger = tr;
    if (tr) { m_beatCount++; if (!m_manualForm && m_beatCount % (int)FORM_AUTO_BEATS == 0) NextForm(); }
    m_beatInterval = 60.0f / m_bpm;
}

const wchar_t* Verdigris::GetFormName() const {
    static const wchar_t* n[] = { L"涟漪", L"漂移", L"绽放", L"薄纱", L"薄雾", L"暗流" }; return n[m_currentForm];
}
const wchar_t* Verdigris::GetColorSchemeName() const {
    static const wchar_t* n[] = { L"铜蓝", L"铜绿", L"蓝灰", L"薄荷霜", L"深水", L"灰橄榄" }; return n[m_colorScheme];
}
const wchar_t* Verdigris::GetProjectionName() const {
    static const wchar_t* n[] = { L"圆形", L"柔螺旋", L"水平缓流", L"中心绽放", L"对角微切", L"潮汐摆动" }; return n[m_projection];
}

// ============================================================================
// IProceduralEffect 接口
// ============================================================================

void Verdigris::PrepareFrame(float dt, float bpm) {
    m_time += dt;
    m_bpm = bpm;
    if (m_currentForm != m_targetForm) {
        m_formBlend += m_formBlendSpeed;
        if (m_formBlend >= 1) { m_formBlend = 1; m_currentForm = m_targetForm; }
    }
    BuildPrecompData(m_pc, m_time, m_bpm);
    m_pc.speed *= m_userSpeed;
}

void Verdigris::ComputeProjectedCoords(float xNorm, float yNorm,
                                        float& r, float& theta) const {
    r = sqrtf(xNorm * xNorm + yNorm * yNorm);
    float th = atan2f(yNorm, xNorm);
    if (th < 0) th += 2.0f * M_PI;
    const float dw = 10.0f, wA = 2.0f * M_PI / dw;
    float wi = floorf(th / wA);
    float tw = th - wi * wA;
    if (fmodf(wi, 2.0f) >= 1.0f) tw = wA - tw;
    theta = tw / wA;
    MapProjection(m_projection, xNorm, yNorm, m_pc, r, theta);
}

float Verdigris::EvalPattern(float xNorm, float yNorm) const {
    float r, theta;
    ComputeProjectedCoords(xNorm, yNorm, r, theta);
    return EvalForm(m_currentForm, r, theta, m_time, m_pc.speed, m_pc);
}

uint32_t Verdigris::MapToColor(float pattern, float xNorm, float yNorm,
                                float beatPhase) const {
    float r, theta;
    ComputeProjectedCoords(xNorm, yNorm, r, theta);
    return MapColor(m_colorScheme, pattern, r, theta, m_pc, m_bpm, beatPhase);
}

// ============================================================================
// HSL → RGB
// ============================================================================
void Verdigris::HSLtoRGB(float h, float s, float l, uint8_t& r, uint8_t& g, uint8_t& b) {
    h = fmodf(h, 360.0f); if (h < 0) h += 360;
    float c = (1 - fabsf(2 * l - 1)) * s, hp = h / 60;
    float x = c * (1 - fabsf(fmodf(hp, 2) - 1)), m = l - c * .5f;
    float r1, g1, b1;
    if (hp < 1)      { r1 = c; g1 = x; b1 = 0; }
    else if (hp < 2) { r1 = x; g1 = c; b1 = 0; }
    else if (hp < 3) { r1 = 0; g1 = c; b1 = x; }
    else if (hp < 4) { r1 = 0; g1 = x; b1 = c; }
    else if (hp < 5) { r1 = x; g1 = 0; b1 = c; }
    else             { r1 = c; g1 = 0; b1 = x; }
    r = (uint8_t)((r1 + m) * 255); g = (uint8_t)((g1 + m) * 255); b = (uint8_t)((b1 + m) * 255);
}

// ============================================================================
// 6 种形态函数
//   边缘密度 0.189 → 中等空间频率（屏幕内可见 6~15 个周期）
//   各向同性 0.14 → 全方向对称，无线状偏置
//   纹理复杂度 0.053 仅代表原图平滑 → 形态本身应有清晰可见的结构
// ============================================================================
float Verdigris::EvalForm(Form f, float r, float theta, float t, float speed,
                           const PrecompData& pc) const {
    float v = 0, a = theta * 2 * M_PI;
    switch (f) {
    case FORM_RIPPLE: {
        float w1 = 0.4f, w2 = 0.9f, w3 = 1.5f;
        v  = fast_sin(r * 0.06f + a * w1 + t * 0.5f * speed) * fast_cos(r * 0.08f - a * w2 + t * 0.6f * speed);
        v += fast_cos(r * 0.10f + a * w3 + t * 0.35f * speed) * 0.55f;
        v += fast_sin(r * 0.07f - a * w1 * 0.6f - t * 0.55f * speed) * 0.4f;
        v += fast_cos(r * 0.12f + a * w2 * 0.5f + t * 0.4f * speed) * 0.3f;
        return v / 2.2f;
    }
    case FORM_DRIFT: {
        float w1 = 0.5f, w2 = 1.1f, w3 = 1.8f;
        v  = fast_sin(r * 0.05f + a * w1 + t * 0.45f * speed) * fast_cos(r * 0.07f - a * w2 - t * 0.55f * speed);
        v += fast_sin(r * 0.09f + a * w3 + t * 0.3f * speed) * 0.5f;
        v += fast_cos(r * 0.06f - a * w1 * 0.7f + t * 0.6f * speed) * 0.4f;
        v += fast_sin(r * 0.11f + a * w2 * 0.4f - t * 0.5f * speed) * 0.3f;
        return v / 2.3f;
    }
    case FORM_BLOOM: {
        float petals = 0.4f + pc.s[20] * 0.15f;
        v  = fast_cos(a * petals + fast_sin(r * 0.08f + t * 0.5f * speed) * 0.15f) * (1.0f - r * 0.10f);
        v += fast_sin(r * 0.06f + a * 0.45f + t * 0.55f * speed) * 0.4f;
        v += fast_sin(r * 0.05f - a * petals * 0.7f) * fast_cos(r * 0.04f + t * 0.4f * speed) * 0.35f;
        v += fast_cos(r * 0.03f + a * petals * 1.3f) * fast_sin(r * 0.07f + t * 0.6f * speed) * 0.25f;
        return v / 2.0f;
    }
    case FORM_VEIL: {
        float ang = 0.7f + pc.sr[25] * 0.25f;
        float rx = r * fast_cos(a + ang), ry = r * fast_sin(a + ang);
        v  = fast_sin(rx * 0.10f + ry * 0.08f + t * 0.45f * speed);
        v += fast_cos(rx * 0.14f - ry * 0.11f - t * 0.55f * speed) * 0.55f;
        v += fast_sin(rx * 0.07f + ry * 0.13f + t * 0.35f * speed) * 0.4f;
        v += fast_cos(rx * 0.17f - ry * 0.05f + t * 0.5f * speed) * 0.3f;
        return v / 2.1f;
    }
    case FORM_MIST: {
        float w1 = 0.35f, w2 = 0.8f, w3 = 1.4f;
        v  = fast_sin(r * 0.04f + t * 0.35f * speed) * fast_cos(r * 0.07f - a * w1);
        v += fast_sin(r * 0.06f + a * w2 + t * 0.45f * speed) * 0.5f;
        v += fast_cos(r * 0.05f - a * w3 - t * 0.55f * speed) * 0.4f;
        v += fast_sin(r * 0.09f + a * w1 * 1.5f + t * 0.3f * speed) * 0.3f;
        return v / 2.0f;
    }
    case FORM_CURRENT: {
        float w1 = 0.6f, w2 = 1.2f, w3 = 2.0f;
        v  = fast_sin(r * 0.06f + a * w1 + t * 0.5f * speed) * fast_cos(r * 0.08f - a * w2 - t * 0.6f * speed);
        v += fast_sin(r * 0.10f + a * w3 + t * 0.35f * speed) * 0.5f;
        v += fast_cos(r * 0.07f + a * w1 * 0.8f - t * 0.45f * speed) * 0.4f;
        v += fast_sin(r * 0.12f - a * w2 * 0.5f + t * 0.55f * speed) * 0.3f;
        return v / 2.4f;
    }
    default: return 0;
    }
}

// ============================================================================
// 6 种投影变换 —— 居中构图，柔和变形
// ============================================================================
void Verdigris::MapProjection(Projection proj, float xNorm, float yNorm,
                               const PrecompData& pc, float& r, float& theta) const {
    float x = xNorm, y = yNorm, spd = pc.speed;
    switch (proj) {
    case PROJ_RADIAL:
        break;  // 直通极坐标

    case PROJ_SOFT_SPIRAL: {
        // 低旋量螺旋（orientation concentration 0.14 → 旋转量小）
        float spin = r * 1.5f;
        float ang = theta * 2 * M_PI + spin + pc.sr[30] * spd * 0.3f;
        r = r + fast_sin(ang * 2 + pc.sr[50] * spd) * 0.01f;
        if (r > 1) r = 1; if (r < 0) r = 0;
        theta = fmodf(ang / (2 * M_PI), 1.0f);
        break;
    }
    case PROJ_HORIZON_FLOW: {
        // 水平缓流，轻微纵向波动
        float ang = 0.6f + pc.sr[20] * 0.2f;
        float rx = x * fast_cos(ang) - y * fast_sin(ang);
        float ry = x * fast_sin(ang) + y * fast_cos(ang);
        r = ry * 0.4f + 0.5f;
        theta = rx * 0.4f + 0.5f + fast_sin(ry * 3 + pc.sr[40] * spd) * 0.08f;
        break;
    }
    case PROJ_CENTER_BLOOM: {
        // 中心绽放：径向加权，离中心越近越展开
        float weight = 1.0f - r * 0.5f;
        r = r * weight + fast_sin(theta * 2 * M_PI * 3 + pc.sr[30] * spd) * 0.02f;
        theta = theta + fast_cos(r * 4 + pc.sr[45] * spd) * 0.03f;
        if (r > 1) r = 1; if (r < 0) r = 0;
        break;
    }
    case PROJ_DIAGONAL_SHEAR: {
        // 对角线微切（dominant orient -45° conc 0.14 → 轻微对角偏置）
        float shear = (x + y) * 0.15f + pc.sr[35] * spd * 0.2f;
        r = fabsf(y * 0.6f + x * 0.4f) + shear * 0.05f;
        theta = fmodf(x * 0.5f - y * 0.5f + 1.0f, 1.0f);
        if (r > 1) r = 1;
        break;
    }
    case PROJ_TIDAL_SWAY: {
        // 潮汐摆动：缓波形扭曲
        float wave = fast_sin(x * 2.5f + pc.sr[50] * spd) * 0.06f
                   + fast_cos(y * 3.0f - pc.sr[35] * spd) * 0.04f;
        r = r + wave;
        theta = theta + fast_sin(r * 5 + pc.sr[25] * spd) * 0.05f;
        if (r > 1) r = 1; if (r < 0) r = 0;
        break;
    }
    }
}

// ============================================================================
// 6 种配色方案
//   S=0.17 → 低饱和；L=0.60 → 中亮调
//   T0=0.28 大淡区（匹配中高调图片的大量浅色面积）
//   palette diversity 0.13 → 6 方案集中在 90°~220° 区间
// ============================================================================
uint32_t Verdigris::MapColor(ColorScheme cs, float pattern, float r, float theta,
                              const PrecompData& pc, float bpm, float phase) const {
    float p = fabsf(pattern), bp = 1 - phase, brightness;
    // T0=0.25: 白底过渡区，软化边缘密度
    const float T0 = 0.25f, T1 = 0.44f, T2 = 0.62f, T3 = 0.79f, T4 = 0.94f;

    if (p < T0) {
        float w = (p / T0); w = w * w * w;
        brightness = w * 0.20f;
        uint8_t r8, g8, b8; float hue, sat, light;
        // 低饱和色系 + 白底混合
        switch (cs) {
        default:
        case COLOR_PATINA_BLUE:     hue = 215 + brightness * 25 + bp * 10; sat = brightness * 0.30f; light = 0.95f - brightness * 0.10f; break;
        case COLOR_VERDIGRIS_GREEN: hue = 117 + brightness * 30 + bp * 12; sat = brightness * 0.28f; light = 0.95f - brightness * 0.10f; break;
        case COLOR_SLATE_STEEL:     hue = 200 + brightness * 20 + bp * 6;  sat = brightness * 0.22f; light = 0.94f - brightness * 0.09f; break;
        case COLOR_MINT_FROST:      hue = 160 + brightness * 35 + bp * 14; sat = brightness * 0.26f; light = 0.96f - brightness * 0.08f; break;
        case COLOR_DEEP_WATER:      hue = 210 + brightness * 22 + bp * 8;  sat = brightness * 0.35f; light = 0.93f - brightness * 0.12f; break;
        case COLOR_ASHEN_OLIVE:     hue = 93  + brightness * 35 + bp * 12; sat = brightness * 0.25f; light = 0.94f - brightness * 0.10f; break;
        }
        if (sat < 0) sat = 0; if (sat > 1) sat = 1; if (light < 0) light = 0; if (light > 1) light = 1;
        HSLtoRGB(hue, sat, light, r8, g8, b8);
        float bg = m_darkBg ? 0.0f : 255.0f;
        uint8_t wr = (uint8_t)(r8 * w + bg * (1 - w)), wg = (uint8_t)(g8 * w + 255 * (1 - w)), wb = (uint8_t)(b8 * w + 255 * (1 - w));
        return ARGB(wr, wg, wb);
    }
    else if (p < T1) brightness = (p - T0) / (T1 - T0) * 0.25f;
    else if (p < T2) brightness = 0.25f + (p - T1) / (T2 - T1) * 0.30f;
    else if (p < T3) brightness = 0.55f + (p - T2) / (T3 - T2) * 0.25f;
    else if (p < T4) brightness = 0.80f + (p - T3) / (T4 - T3) * 0.15f;
    else brightness = 1;
    brightness += bp * 0.04f; if (brightness > 1) brightness = 1;

    uint8_t r8, g8, b8; float hue, sat, light;
    switch (cs) {
    default:
    case COLOR_PATINA_BLUE:
        hue = 215 + brightness * 25 + bp * 10; sat = brightness * 0.35f + 0.12f; light = 0.82f - brightness * 0.32f; break;
    case COLOR_VERDIGRIS_GREEN:
        hue = 117 + brightness * 30 + bp * 12; sat = brightness * 0.32f + 0.10f; light = 0.82f - brightness * 0.32f; break;
    case COLOR_SLATE_STEEL:
        hue = 200 + brightness * 20 + bp * 6;  sat = brightness * 0.25f + 0.08f; light = 0.80f - brightness * 0.30f; break;
    case COLOR_MINT_FROST:
        hue = 160 + brightness * 35 + bp * 14; sat = brightness * 0.30f + 0.10f; light = 0.85f - brightness * 0.26f; break;
    case COLOR_DEEP_WATER:
        hue = 210 + brightness * 22 + bp * 8;  sat = brightness * 0.45f + 0.15f; light = 0.78f - brightness * 0.36f; break;
    case COLOR_ASHEN_OLIVE:
        hue = 93  + brightness * 35 + bp * 12; sat = brightness * 0.30f + 0.10f; light = 0.80f - brightness * 0.30f; break;
    }
    if (sat < 0) sat = 0; if (sat > 1) sat = 1; if (light < 0) light = 0; if (light > 1) light = 1;
    HSLtoRGB(hue, sat, light, r8, g8, b8); return ARGB(r8, g8, b8);
}

// ============================================================================
// RebuildLUT
// ============================================================================
void Verdigris::RebuildLUT(int fw, int fh) {
    int rw = fw / QUALITY, rh = fh / QUALITY;
    if (rw < 1) rw = 1; if (rh < 1) rh = 1;
    if (m_fullW == fw && m_fullH == fh && m_renderW == rw && m_renderH == rh) return;
    m_fullW = fw; m_fullH = fh; m_renderW = rw; m_renderH = rh;
    int n = rw * rh; m_lut.resize(n); m_renderBuf.resize(n);

    float cx = fw * .5f, cy = fh * .5f, mr = std::min(cx, cy); if (mr < 1) mr = 1;
    const float dw = 10, wA = 2 * M_PI / dw;
    for (int sy = 0; sy < rh; ++sy) {
        float fy = (sy + .5f) * QUALITY, dy = fy - cy;
        for (int sx = 0; sx < rw; ++sx) {
            float fx = (sx + .5f) * QUALITY, dx = fx - cx;
            float r = sqrtf(dx * dx + dy * dy) / mr;
            float th = atan2f(dy, dx); if (th < 0) th += 2 * M_PI;
            float wi = floorf(th / wA), tw = th - wi * wA;
            if (fmodf(wi, 2) >= 1) tw = wA - tw;
            int idx = sy * rw + sx;
            m_lut[idx].xNorm = dx / mr;
            m_lut[idx].yNorm = dy / mr;
            m_lut[idx].r = r;
            m_lut[idx].theta = tw / wA;
        }
    }
}

// ============================================================================
// RenderChunk —— 独立渲染（含内部形态混合）
// ============================================================================
void Verdigris::RenderChunk(int start, int end,
    Projection proj, Form cur, Form tgt, float blend, float bpm, float phase,
    ColorScheme cs, const PrecompData& pc, float t, float speed) {
    float sb = blend * blend * (3.0f - 2.0f * blend);
    bool bl = (sb < 1); float ib = 1 - sb;
    for (int i = start; i < end; ++i) {
        const auto& pt = m_lut[i];
        float r = pt.r, th = pt.theta;  // 从 LUT 初始化，MapProjection 可能覆盖
        MapProjection(proj, pt.xNorm, pt.yNorm, pc, r, th);
        float pat;
        if (bl) pat = EvalForm(cur, r, th, t, speed, pc) * ib + EvalForm(tgt, r, th, t, speed, pc) * sb;
        else    pat = EvalForm(cur, r, th, t, speed, pc);
        m_renderBuf[i] = MapColor(cs, pat, r, th, pc, bpm, phase);
    }
}

// ============================================================================
// Render —— HDC 输出
// ============================================================================
void Verdigris::Render(HDC hdc, const RECT& rect, const float*, int, const float*, int) {
    int fw = rect.right - rect.left, fh = rect.bottom - rect.top;
    if (fw <= 0 || fh <= 0) return;

    PrepareFrame(0.016f, m_bpm);

    RebuildLUT(fw, fh);
    int total = m_renderW * m_renderH;

    unsigned nTh = std::thread::hardware_concurrency();
    if (nTh < 1) nTh = 1; if (nTh > 16) nTh = 16;
    int cs_ = total / nTh; if (cs_ < 1) cs_ = 1;
    float t = m_time, speed = m_pc.speed;

    static ThreadPool pool;
    for (unsigned ti = 0; ti < nTh; ++ti) {
        int s = ti * cs_, e = (ti == nTh - 1) ? total : s + cs_;
        pool.Enqueue([this, s, e, t, speed] {
            RenderChunk(s, e, m_projection, m_currentForm, m_targetForm,
                        m_formBlend, m_bpm, m_beatPhase, m_colorScheme,
                        std::cref(m_pc), t, speed);
        });
    }
    pool.Wait();

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = m_renderW; bmi.bmiHeader.biHeight = -(int)m_renderH;
    bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32; bmi.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(hdc, HALFTONE);
    StretchDIBits(hdc, 0, 0, fw, fh, 0, 0, m_renderW, m_renderH,
                  m_renderBuf.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
}
