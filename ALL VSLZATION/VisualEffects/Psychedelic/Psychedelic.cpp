/**
 * @file Psychedelic.cpp
 * @brief 多线程渲染 + IProceduralEffect 接口实现
 */

#include "Psychedelic.h"
#include "../ThreadPool.h"
#include <algorithm>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

Psychedelic::Psychedelic()
    : m_currentForm(FORM_VEINS), m_targetForm(FORM_VEINS)
    , m_formBlend(1), m_formBlendSpeed(0.05f)
    , m_colorScheme(COLOR_NEON_GREEN)
    , m_projection(PROJ_LINEAR_WATER)
{ memset(&m_pc, 0, sizeof(m_pc)); }

const wchar_t* Psychedelic::GetName() const { return L"致幻剂 Psychedelic"; }

void Psychedelic::SetBPM(float v) { m_bpm = v; if (m_bpm < 1) m_bpm = 1; m_beatInterval = 60.0f / m_bpm; }
void Psychedelic::NextForm() { m_targetForm = static_cast<Form>((m_targetForm + 1) % FORM_COUNT); m_formBlend = 0; m_formBlendSpeed = 0.03f + (m_bpm - 40) / 260.0f * 0.12f; }
void Psychedelic::NextColorScheme() { m_colorScheme = static_cast<ColorScheme>((m_colorScheme + 1) % COLOR_COUNT); }
void Psychedelic::NextProjection() { m_projection = static_cast<Projection>((m_projection + 1) % PROJ_COUNT); }
void Psychedelic::SetManualMode(bool m) { m_manualForm = m; }
void Psychedelic::SyncBeatState(float v, float ph, bool tr) {
    m_bpm = v; m_beatPhase = ph; m_beatTrigger = tr;
    if (tr) { m_beatCount++; if (!m_manualForm && m_beatCount % (int)FORM_AUTO_BEATS == 0) NextForm(); }
    m_beatInterval = 60.0f / m_bpm;
}

const wchar_t* Psychedelic::GetFormName() const {
    static const wchar_t* n[] = { L"叶脉", L"菌丝", L"藤蔓", L"星爆", L"曼陀罗", L"分形" }; return n[m_currentForm];
}
const wchar_t* Psychedelic::GetColorSchemeName() const {
    static const wchar_t* n[] = { L"霓虹绿", L"毒液绿紫", L"深海青蓝", L"炼狱红金", L"宇宙紫蓝", L"迷幻彩虹" }; return n[m_colorScheme];
}
const wchar_t* Psychedelic::GetProjectionName() const {
    static const wchar_t* n[] = { L"圆形万花筒", L"线性瀑布", L"螺旋收紧", L"螺旋散开", L"锯齿折线", L"隧道纵深" }; return n[m_projection];
}

// ============================================================================
// IProceduralEffect 接口实现
// ============================================================================

void Psychedelic::PrepareFrame(float dt, float bpm) {
    m_time += dt;
    m_bpm = bpm;

    // 形态过渡
    if (m_currentForm != m_targetForm) {
        m_formBlend += m_formBlendSpeed;
        if (m_formBlend >= 1) {
            m_formBlend = 1;
            m_currentForm = m_targetForm;
        }
    }

    BuildPrecompData(m_pc, m_time, m_bpm);
    m_pc.speed *= m_userSpeed;
}

// ---- 坐标计算辅助函数 ----
void Psychedelic::ComputeProjectedCoords(float xNorm, float yNorm,
                                          float& r, float& theta) const {
    // 笛卡尔 → 极坐标
    r = sqrtf(xNorm * xNorm + yNorm * yNorm);
    float th = atan2f(yNorm, xNorm);
    if (th < 0) th += 2.0f * M_PI;

    // 万花筒折叠（10 楔形，交替镜像）
    const float dw = 10.0f, wA = 2.0f * M_PI / dw;
    float wi = floorf(th / wA);
    float tw = th - wi * wA;
    if (fmodf(wi, 2.0f) >= 1.0f) tw = wA - tw;
    theta = tw / wA;  // 归一化到 [0, 1]

    // 投影变换
    MapProjection(m_projection, xNorm, yNorm, m_pc, r, theta);
}

float Psychedelic::EvalPattern(float xNorm, float yNorm) const {
    float r, theta;
    ComputeProjectedCoords(xNorm, yNorm, r, theta);
    return EvalForm(m_currentForm, r, theta, m_time, m_pc.speed, m_pc);
}

uint32_t Psychedelic::MapToColor(float pattern, float xNorm, float yNorm,
                                  float beatPhase) const {
    float r, theta;
    ComputeProjectedCoords(xNorm, yNorm, r, theta);
    return MapColor(m_colorScheme, pattern, r, theta, m_pc, m_bpm, beatPhase);
}

// ============================================================================
// HSL → RGB
// ============================================================================
void Psychedelic::HSLtoRGB(float h, float s, float l, uint8_t& r, uint8_t& g, uint8_t& b) {
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
// 6 种形态函数（空间频率 ÷16 消除高频褶皱）
// ============================================================================
float Psychedelic::EvalForm(Form f, float r, float theta, float t, float speed,
                             const PrecompData& pc) const {
    float v = 0, a = theta * 2 * M_PI;
    switch (f) {
    case FORM_VEINS: {
        float f1 = 0.375f, f2 = 0.8125f, f3 = 1.3125f;
        v  = fast_sin(r * .05f + a * f1 + t * .3f * speed) * fast_cos(r * .069f - a * f2 + t * .45f * speed);
        v += fast_sin(r * .088f + a * f3) * fast_cos(r * .038f - a * (f1 + f2 + f3) / 3 + t * .55f * speed);
        v += fast_cos(r * .056f + a * f1 * .5f - t * .35f * speed) * fast_sin(r * .081f - a * f2 * .7f + t * .5f * speed);
        v += fast_sin(r * .106f + a * f3 * .4f + t * .25f * speed) * fast_cos(r * .031f - a * (f1 + f3) * .6f) * .7f;
        return v / 3.2f;
    }
    case FORM_MYCELIUM: {
        float p1 = 0.5f, p2 = 1.0625f, p3 = 1.8125f;
        v  = fast_sin(r * .075f + a * p1 + t * .4f * speed) * fast_sin(r * .056f - a * p2 - t * .35f * speed);
        v += fast_cos(r * .094f + a * p3) * fast_cos(r * .044f - a * p1 + t * .5f * speed);
        v += fast_sin(r * .125f + a * (p1 + p2) / 2 + t * .3f * speed) * fast_cos(r * .069f - a * (p2 + p3) / 2) * .8f;
        v += fast_sin(r * .05f + a * p3 * .5f - t * .45f * speed) * fast_sin(r * .088f + a * p1 * .7f) * .6f;
        return v / 3.6f;
    }
    case FORM_TENDRILS: {
        float w1 = 0.3125f, w2 = 0.875f, w3 = 1.4375f;
        v  = fast_sin(r * .025f + a * w1 + t * .25f * speed) * fast_cos(r * .038f - a * w2 * .5f);
        v += fast_sin(r * .056f + a * w2 + t * .4f * speed) * fast_cos(r * .044f - a * w1 * 1.5f + t * .3f * speed) * .8f;
        v += fast_sin(r * .081f + a * w3 * .7f + t * .5f * speed) * fast_cos(r * .063f + a * (w1 + w2) / 2) * .6f;
        v += fast_sin(r * .1f - a * w3 * .4f + t * .35f * speed) * fast_sin(r * .031f + a * w2 * .8f - t * .45f * speed) * .5f;
        return v / 2.9f;
    }
    case FORM_STARBURST: {
        float rays = 0.5f + pc.s[20] * 0.1875f;
        v  = fast_cos(a * rays + fast_sin(r * .125f + t * .5f * speed) * .156f) * (1 - r * .025f);
        v += fast_sin(r * .156f + a * 0.375f + t * .6f * speed) * .4f;
        v += fast_sin(r * .113f - a * rays * .7f) * fast_cos(r * .038f + t * .4f * speed) * .5f;
        v += fast_cos(r * .031f + a * rays + t * .3f * speed) * r * .038f;
        return v / 2.5f;
    }
    case FORM_MANDALA: {
        float petals = 0.313f + pc.s[15] * 0.125f;
        v  = fast_sin(r * .044f + t * .3f * speed) * fast_cos(a * petals + r * .013f);
        v += fast_sin(r * .063f - t * .45f * speed) * fast_cos(a * petals * .6f + r * .009f);
        v += fast_sin(r * .088f + a * 0.3125f + t * .35f * speed) * .5f;
        v += fast_cos(r * .025f - a * petals * .8f) * fast_sin(r * .053f + t * .5f * speed) * .6f;
        return v / 2.7f;
    }
    case FORM_FRACTAL: {
        float br = 0.156f + pc.s[30] * 0.094f;
        v  = fast_sin(r * .063f + a * br + t * .2f * speed) * fast_cos(r * .031f - a * br * 1.5f);
        v += fast_sin(r * .125f + a * br * 2.5f + t * .4f * speed) * fast_cos(r * .075f - a * 0.25f) * .7f;
        v += fast_sin(r * .188f + a * br * 3.5f + t * .5f * speed) * fast_cos(r * .113f + a * 0.438f) * .4f;
        return v / 2.1f;
    }
    default: return 0;
    }
}

// ============================================================================
// MapProjection —— 6 种投影变换
// ============================================================================
void Psychedelic::MapProjection(Projection proj, float xNorm, float yNorm,
                                 const PrecompData& pc, float& r, float& theta) const {
    float x = xNorm, y = yNorm, spd = pc.speed;
    switch (proj) {
    case PROJ_RADIAL:
        // r, theta 已由 ComputeProjectedCoords 设置，直接通过
        break;
    case PROJ_LINEAR_WATER: {
        float ang = 0.7f + pc.sr[30] * 0.3f;
        float rx = x * fast_cos(ang) - y * fast_sin(ang);
        float ry = x * fast_sin(ang) + y * fast_cos(ang);
        r = ry * .5f + .5f;
        float streams = 6 + pc.sr[20] * 3;
        theta = rx * .5f + .5f + fast_sin(ry * streams + pc.sr[50] * spd) * .15f;
        theta += fast_cos(rx * streams * 1.7f + ry * 3 - pc.sr[40] * spd) * .08f;
        break;
    }
    case PROJ_SPIRAL_IN: {
        float spin = (1 - r) * 3, ang = theta * 2 * M_PI + spin + pc.sr[30] * spd;
        r = r + fast_sin(ang * 4 + pc.sr[60] * spd) * .02f;
        if (r > 1) r = 1; if (r < 0) r = 0;
        theta = fmodf(ang / (2 * M_PI), 1.0f); break;
    }
    case PROJ_SPIRAL_OUT: {
        float spin = r * 3.5f, ang = theta * 2 * M_PI + spin + pc.sr[30] * spd;
        r = r + fast_cos(ang * 3 - pc.sr[50] * spd) * .03f;
        if (r > 1) r = 1; if (r < 0) r = 0;
        theta = fmodf(ang / (2 * M_PI), 1.0f); break;
    }
    case PROJ_ZIGZAG: {
        float sweep = pc.sr[40] * spd, saw = fmodf(x * 8 + sweep, 2.0f);
        if (saw > 1) saw = 2 - saw;
        r = y * .5f + .5f;
        theta = saw * .7f + fast_sin(y * 6 + pc.sr[50] * spd) * .2f + fast_cos(x * 12 - y * 3 + pc.sr[30] * spd) * .1f;
        theta = fmodf(theta, 1.0f); if (theta < 0) theta += 1; break;
    }
    case PROJ_TUNNEL_DEPTH: {
        float dist = sqrtf(x * x + y * y), depth = 1 / (dist + .15f);
        float ang = atan2f(y, x) + depth * .3f + pc.sr[50] * spd;
        r = fmodf(depth * .15f + pc.sr[20] * spd, 1.0f);
        r += fast_sin(depth * .5f + pc.sr[60] * spd) * .05f;
        theta = fmodf(ang / (2 * M_PI), 1.0f); if (theta < 0) theta += 1; break;
    }
    }
}

// ============================================================================
// MapColor —— 6 种配色方案（白底淡区过渡）
// ============================================================================
uint32_t Psychedelic::MapColor(ColorScheme cs, float pattern, float r, float theta,
                                const PrecompData& pc, float bpm, float phase) const {
    float p = fabsf(pattern), bp = 1 - phase, brightness;
    const float T0 = .20f, T1 = .38f, T2 = .56f, T3 = .74f, T4 = .92f;

    if (p < T0) {
        float w = (p / T0); w = w * w * w;
        brightness = w * 0.25f;
        uint8_t r8, g8, b8; float hue, sat, light;
        switch (cs) {
        default:
        case COLOR_NEON_GREEN: hue = 115 + brightness * 25 + bp * 15; sat = brightness * .5f; light = .95f - brightness * .12f; break;
        case COLOR_TOXIC:      hue = 120 + brightness * 180; sat = brightness * .5f; light = .93f - brightness * .10f; break;
        case COLOR_OCEAN:      hue = 175 + brightness * 40; sat = brightness * .45f; light = .95f - brightness * .12f; break;
        case COLOR_INFERNO:    hue = 3 + brightness * 47; sat = brightness * .55f; light = .93f - brightness * .10f; break;
        case COLOR_COSMIC:     hue = 255 + brightness * 60; sat = brightness * .5f; light = .95f - brightness * .12f; break;
        case COLOR_PSYCHEDELIC: hue = fmodf(theta * 360 + pc.sr[15] * 15 + phase * 40, 360.0f); sat = brightness * .5f; light = .93f - brightness * .10f; break;
        }
        if (sat < 0) sat = 0; if (sat > 1) sat = 1; if (light < 0) light = 0; if (light > 1) light = 1;
        HSLtoRGB(hue, sat, light, r8, g8, b8);
        float bg = m_darkBg ? 0.0f : 255.0f;
        uint8_t wr = (uint8_t)(r8 * w + bg * (1 - w)), wg = (uint8_t)(g8 * w + bg * (1 - w)), wb = (uint8_t)(b8 * w + bg * (1 - w));
        return ARGB(wr, wg, wb);
    }
    else if (p < T1) brightness = (p - T0) / (T1 - T0) * .30f;
    else if (p < T2) brightness = .30f + (p - T1) / (T2 - T1) * .35f;
    else if (p < T3) brightness = .65f + (p - T2) / (T3 - T2) * .25f;
    else if (p < T4) brightness = .90f + (p - T3) / (T4 - T3) * .10f;
    else brightness = 1;
    brightness += bp * .06f; if (brightness > 1) brightness = 1;

    uint8_t r8, g8, b8; float hue, sat, light;
    switch (cs) {
    default:
    case COLOR_NEON_GREEN:
        hue = 115 + brightness * 25 + bp * 15; sat = brightness * .7f + .15f; light = .92f - brightness * .20f; break;
    case COLOR_TOXIC:
        hue = 120 + brightness * 180; sat = brightness * .75f + .10f; light = .90f - brightness * .18f; break;
    case COLOR_OCEAN:
        hue = 175 + brightness * 40; sat = brightness * .65f + .15f; light = .92f - brightness * .22f; break;
    case COLOR_INFERNO:
        hue = 3 + brightness * 47; sat = brightness * .85f + .10f; light = .90f - brightness * .20f; break;
    case COLOR_COSMIC:
        hue = 255 + brightness * 60; sat = brightness * .70f + .15f; light = .92f - brightness * .20f; break;
    case COLOR_PSYCHEDELIC:
        hue = fmodf(theta * 360 + pc.sr[15] * 15 + phase * 40, 360.0f); sat = brightness * .80f + .10f; light = .90f - brightness * .18f; break;
    }
    if (sat < 0) sat = 0; if (sat > 1) sat = 1; if (light < 0) light = 0; if (light > 1) light = 1;
    HSLtoRGB(hue, sat, light, r8, g8, b8); return ARGB(r8, g8, b8);
}

// ============================================================================
// RebuildLUT —— 预计算坐标查找表（仅在窗口尺寸变化时触发）
// ============================================================================
void Psychedelic::RebuildLUT(int fw, int fh) {
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
// RenderChunk —— 独立渲染的线程工作函数（含内部形态混合）
// ============================================================================
void Psychedelic::RenderChunk(int start, int end,
    Projection proj, Form cur, Form tgt, float blend, float bpm, float phase,
    ColorScheme cs, const PrecompData& pc, float t, float speed) {
    float sb = blend * blend * (3.0f - 2.0f * blend);
    bool bl = (sb < 1); float ib = 1 - sb;
    for (int i = start; i < end; ++i) {
        const auto& pt = m_lut[i];
        float r = pt.r, th = pt.theta;
        MapProjection(proj, pt.xNorm, pt.yNorm, pc, r, th);
        float pat;
        if (bl) pat = EvalForm(cur, r, th, t, speed, pc) * ib + EvalForm(tgt, r, th, t, speed, pc) * sb;
        else    pat = EvalForm(cur, r, th, t, speed, pc);
        m_renderBuf[i] = MapColor(cs, pat, r, th, pc, bpm, phase);
    }
}

// ============================================================================
// Render —— 独立/播放链 BLUR 外渲染（HDC 输出）
// ============================================================================
void Psychedelic::Render(HDC hdc, const RECT& rect, const float*, int, const float*, int) {
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
