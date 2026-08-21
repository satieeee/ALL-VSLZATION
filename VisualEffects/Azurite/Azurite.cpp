/**
 * @file Azurite.cpp
 * @brief 蓝铜矿 —— 伪 3D 高度场 + Phong 光照
 *
 * 技术方案：逐像素 3 次 HeightField 采样求法线 + Phong 模型着色
 *   高度场 trig 调用：形态内 1~3 项 × 3 次采样 = 3~9 次/像素（远低于 20 预算）
 */

#include "Azurite.h"
#include "../ThreadPool.h"
#include <algorithm>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

Azurite::Azurite()
    : m_currentForm(FORM_STRATA), m_targetForm(FORM_STRATA)
    , m_formBlend(1), m_formBlendSpeed(0.05f)
    , m_colorScheme(COLOR_AZURE_DEEP), m_projection(PROJ_IDENTITY)
{ memset(&m_pc, 0, sizeof(m_pc)); }

const wchar_t* Azurite::GetName() const { return L"Azurite 蓝铜矿"; }

void Azurite::SetBPM(float v) { m_bpm = v; if (m_bpm < 1) m_bpm = 1; }
void Azurite::NextForm() { m_targetForm = static_cast<Form>((m_targetForm+1)%FORM_COUNT); m_formBlend = 0; m_formBlendSpeed = 0.03f + (m_bpm-40)/260.0f*0.12f; }
void Azurite::NextColorScheme() { m_colorScheme = static_cast<ColorScheme>((m_colorScheme+1)%COLOR_COUNT); }
void Azurite::NextProjection() { m_projection = static_cast<Projection>((m_projection+1)%PROJ_COUNT); }
void Azurite::SyncBeatState(float v, float ph, bool tr) {
    m_bpm = v; m_beatPhase = ph; m_beatTrigger = tr;
    if (tr) { m_beatCount++; NextForm(); }
}

const wchar_t* Azurite::GetFormName() const {
    static const wchar_t* n[]={L"层理",L"编织",L"块垒",L"涟漪",L"柱林",L"地形"}; return n[m_currentForm];
}
const wchar_t* Azurite::GetColorSchemeName() const {
    static const wchar_t* n[]={L"深蓝",L"青霜",L"钴夜",L"冰纹",L"铜绿流",L"靛雾"}; return n[m_colorScheme];
}
const wchar_t* Azurite::GetProjectionName() const {
    static const wchar_t* n[]={L"恒等",L"水平拉伸",L"垂直拉伸",L"对角映射",L"旋转",L"波浪扭曲"};
    return n[m_projection];
}

// ============================================================================
// IProceduralEffect
// ============================================================================
void Azurite::PrepareFrame(float dt, float bpm) {
    m_time += dt; m_bpm = bpm;
    if (m_currentForm != m_targetForm) {
        m_formBlend += m_formBlendSpeed;
        if (m_formBlend >= 1) { m_formBlend = 1; m_currentForm = m_targetForm; }
    }
    BuildPrecompData(m_pc, m_time, m_bpm);
    m_pc.speed *= m_userSpeed;
}

float Azurite::EvalPattern(float xNorm, float yNorm) const {
    float x = xNorm, y = yNorm;
    MapProjection(m_projection, m_pc, x, y);
    return HeightField(m_currentForm, x, y, m_time, m_pc.speed, m_pc);
}

uint32_t Azurite::MapToColor(float pattern, float xNorm, float yNorm, float beatPhase) const {
    // TransitionEngine 兼容：回退到简化的亮暗映射
    float bright = (pattern + 1.0f) * 0.5f;  // [-1,1] → [0,1]
    if (bright < 0) bright = 0; if (bright > 1) bright = 1;
    // 使用配色方案的基调色 + 高度调制亮度
    uint8_t r8,g8,b8; float hue,sat,light;
    switch (m_colorScheme) {
    default: hue=215+bright*20; sat=0.35f; light=0.50f+bright*0.40f; break;
    }
    HSLtoRGB(hue,sat,light,r8,g8,b8); return ARGB(r8,g8,b8);
}

// ============================================================================
void Azurite::HSLtoRGB(float h, float s, float l, uint8_t& r, uint8_t& g, uint8_t& b) {
    h = fmodf(h, 360.0f); if (h<0) h+=360;
    float c=(1-fabsf(2*l-1))*s, hp=h/60, x=c*(1-fabsf(fmodf(hp,2)-1)), m=l-c*.5f;
    float r1,g1,b1;
    if (hp<1) {r1=c;g1=x;b1=0;} else if (hp<2) {r1=x;g1=c;b1=0;}
    else if (hp<3) {r1=0;g1=c;b1=x;} else if (hp<4) {r1=0;g1=x;b1=c;}
    else if (hp<5) {r1=x;g1=0;b1=c;} else {r1=c;g1=0;b1=x;}
    r=(uint8_t)((r1+m)*255); g=(uint8_t)((g1+m)*255); b=(uint8_t)((b1+m)*255);
}

// ============================================================================
// 6 高度场形态（纹理 0.12 → 1~3 项 trig，简洁但有立体感）
// ============================================================================
float Azurite::HeightField(Form f, float x, float y, float t, float speed,
                            const PrecompData& pc) const {
    switch (f) {
    case FORM_STRATA:
        return fast_sin(y*5.0f + t*0.3f*speed)*0.50f
             + fast_sin(y*12.0f - t*0.2f*speed)*0.30f
             + fast_cos(y*20.0f)*0.12f;

    case FORM_WEAVE:
        return fast_sin(x*6.0f + t*0.4f*speed)*fast_cos(y*5.0f - t*0.35f*speed)*0.55f
             + fast_sin(x*9.0f - y*8.0f + pc.sr[20]*speed)*0.25f;

    case FORM_BLOCKS: {
        float bx = floorf(x*5.0f), by = floorf(y*5.0f);
        float h = fast_sin(bx*127.1f + by*311.7f + t*0.7f*speed)*0.6f
                + fast_sin(bx*269.5f + by*183.3f - t*0.5f*speed)*0.4f;
        return h + fast_sin(x*3.0f + t*0.3f*speed)*0.12f;
    }
    case FORM_RIPPLES: {
        float r = sqrtf(x*x + y*y);
        return fast_sin(r*8.0f + t*0.5f*speed)*0.55f
             + fast_cos(r*15.0f - t*0.3f*speed)*0.25f;
    }
    case FORM_PILLARS:
        return fast_sin(x*5.5f + t*0.3f*speed)*fast_sin(y*5.5f + t*0.25f*speed)*0.65f
             + fast_cos(x*11.0f)*fast_cos(y*11.0f)*0.20f;

    case FORM_TERRAIN:
        return fast_sin(x*3.0f + y*1.5f + t*0.4f*speed)*fast_cos(y*3.5f - x*1.0f - t*0.5f*speed)*0.45f
             + fast_sin(y*5.0f + x*2.0f + t*0.25f*speed)*0.30f
             + fast_cos(x*6.0f - y*4.0f + t*0.45f*speed)*0.20f;

    default: return 0;
    }
}

// ============================================================================
// 6 投影
// ============================================================================
void Azurite::MapProjection(Projection proj, const PrecompData& pc,
                             float& x, float& y) const {
    float spd = pc.speed;
    switch (proj) {
    case PROJ_IDENTITY: break;
    case PROJ_HORIZON:
        x += fast_sin(y*4.0f + pc.sr[30]*spd)*0.08f;
        y = y*0.8f + fast_cos(x*5.0f - pc.sr[40]*spd)*0.06f; break;
    case PROJ_VERTICAL:
        y += fast_cos(x*4.0f + pc.sr[35]*spd)*0.08f;
        x = x*0.8f + fast_sin(y*5.0f - pc.sr[45]*spd)*0.06f; break;
    case PROJ_DIAGONAL: {
        float d=(x+y)*0.7f, e=(x-y)*0.7f;
        x = d + fast_sin(e*3.5f + pc.sr[50]*spd)*0.05f;
        y = e + fast_cos(d*4.5f - pc.sr[35]*spd)*0.04f; break;
    }
    case PROJ_ROTATE: {
        float ang = 0.5f + pc.sr[25]*0.3f;
        float rx=x*fast_cos(ang)-y*fast_sin(ang), ry=x*fast_sin(ang)+y*fast_cos(ang);
        x=rx+fast_sin(ry*3+pc.sr[40]*spd)*0.04f; y=ry+fast_cos(rx*4-pc.sr[30]*spd)*0.03f; break;
    }
    case PROJ_WAVE:
        x += fast_sin(y*5.0f+pc.sr[50]*spd)*0.06f + fast_cos(x*7.0f-pc.sr[30]*spd)*0.04f;
        y += fast_cos(x*5.0f+pc.sr[40]*spd)*0.05f + fast_sin(y*7.0f-pc.sr[35]*spd)*0.04f; break;
    }
}

// ============================================================================
// Phong 着色：高度场 → 法线 → 光照 → 颜色
// ============================================================================
uint32_t Azurite::Shade(ColorScheme cs, float h, float nx, float ny, float nz,
                         const PrecompData& pc, float phase) const {
    // 光照方向（固定右上前方）
    const float Lx=0.55f, Ly=0.35f, Lz=0.75f;
    float dotNL = nx*Lx + ny*Ly + nz*Lz;
    if (dotNL < 0) dotNL = 0;

    // 半角向量（视线从上方）
    float Hx=Lx, Hy=Ly, Hz=Lz+1.0f;
    float hLen = sqrtf(Hx*Hx+Hy*Hy+Hz*Hz);
    Hx/=hLen; Hy/=hLen; Hz/=hLen;
    float dotNH = nx*Hx + ny*Hy + nz*Hz;
    if (dotNH < 0) dotNH = 0;

    float ambient  = 0.10f;
    float diffuse  = dotNL * 0.55f;
    float specular = powf(dotNH, 40.0f) * 0.28f;
    float fresnel  = (1.0f - nz) * 0.07f;  // 边缘暗角
    float lighting = ambient + diffuse + specular - fresnel;
    if (lighting < 0.05f) lighting = 0.05f;
    if (lighting > 1.0f)  lighting = 1.0f;

    // 高度→饱和度/亮度调制
    float sat  = 0.30f + h*0.20f + lighting*0.15f;
    float light = 0.35f + h*0.25f + lighting*0.30f;
    if (sat<0)sat=0;if(sat>1)sat=1;if(light<0)light=0;if(light>1)light=1;

    float bp = 1.0f - phase;
    float hue;
    switch (cs) {
    default:
    case COLOR_AZURE_DEEP:   hue=215+h*15+bp*8;  break;
    case COLOR_CYAN_FROST:   hue=185+h*20+bp*10; break;
    case COLOR_COBALT_NIGHT: hue=240+h*12+bp*5;  break;
    case COLOR_ICE_VEIN:     hue=200+h*18+bp*7;  break;
    case COLOR_PATINA_FLOW:  hue=160+h*25+bp*10; break;
    case COLOR_INDIGO_MIST:  hue=250+h*15+bp*6;  break;
    }

    uint8_t r8,g8,b8;
    HSLtoRGB(hue, sat, light, r8, g8, b8);
    return ARGB(r8, g8, b8);
}

// ============================================================================
// LUT（仅存笛卡尔坐标）
// ============================================================================
void Azurite::RebuildLUT(int fw, int fh) {
    int rw=fw/QUALITY, rh=fh/QUALITY;
    if (rw<1) rw=1; if (rh<1) rh=1;
    if (m_fw==fw && m_fh==fh && m_rw==rw && m_rh==rh) return;
    m_fw=fw; m_fh=fh; m_rw=rw; m_rh=rh;
    int n=rw*rh; m_lut.resize(n); m_renderBuf.resize(n);
    float cx=fw*.5f, cy=fh*.5f, mr=std::min(cx,cy); if (mr<1) mr=1;
    for (int sy=0; sy<rh; ++sy)
        for (int sx=0; sx<rw; ++sx) {
            int idx=sy*rw+sx;
            m_lut[idx].xNorm = ((sx+0.5f)*QUALITY - cx)/mr;
            m_lut[idx].yNorm = ((sy+0.5f)*QUALITY - cy)/mr;
        }
}

// ============================================================================
// RenderChunk —— 逐像素 3 次高度采样 → 法线 → Phong 着色
// ============================================================================
void Azurite::RenderChunk(int start, int end,
    Projection proj, Form cur, Form tgt, float blend, float bpm, float phase,
    ColorScheme cs, const PrecompData& pc, float t, float speed) {
    float sb = blend*blend*(3.0f-2.0f*blend);
    bool bl=(sb<1); float ib=1-sb;
    float eps = NORM_EPS;

    for (int i=start; i<end; ++i) {
        float x0=m_lut[i].xNorm, y0=m_lut[i].yNorm;
        float x = x0, y = y0;
        MapProjection(proj, pc, x, y);

        // 中心 + 两个偏移点的高度（共 3 次 HeightField 调用）
        auto hf = [&](float px, float py) -> float {
            float h;
            if (bl) h = HeightField(cur, px, py, t, speed, pc)*ib
                      + HeightField(tgt, px, py, t, speed, pc)*sb;
            else    h = HeightField(cur, px, py, t, speed, pc);
            return h;
        };

        float h0 = hf(x, y);
        float hx = hf(x + eps, y);
        float hy = hf(x, y + eps);

        // 法线 = normalize( -dh/dx, -dh/dy, 1 )
        float nx = -(hx - h0) / eps;
        float ny = -(hy - h0) / eps;
        float nz = 1.0f;
        float nLen = sqrtf(nx*nx + ny*ny + nz*nz);
        nx /= nLen; ny /= nLen; nz /= nLen;

        m_renderBuf[i] = Shade(cs, h0, nx, ny, nz, pc, phase);
    }
}

// ============================================================================
void Azurite::Render(HDC hdc, const RECT& rect, const float*, int, const float*, int) {
    int fw=rect.right-rect.left, fh=rect.bottom-rect.top;
    if (fw<=0 || fh<=0) return;
    PrepareFrame(0.016f, m_bpm);
    RebuildLUT(fw, fh);
    int total = m_rw*m_rh;

    unsigned nTh=std::thread::hardware_concurrency();
    if (nTh<1)nTh=1; if (nTh>16)nTh=16;
    int cs_=total/nTh; if (cs_<1) cs_=1;
    float t=m_time, speed=m_bpm/120.0f;

    static ThreadPool pool;
    for (unsigned ti=0; ti<nTh; ++ti) {
        int s=ti*cs_, e=(ti==nTh-1)?total:s+cs_;
        pool.Enqueue([this, s, e, t, speed] {
            RenderChunk(s, e, m_projection, m_currentForm, m_targetForm,
                        m_formBlend, m_bpm, m_beatPhase, m_colorScheme,
                        std::cref(m_pc), t, speed);
        });
    }
    pool.Wait();

    BITMAPINFO bmi={};
    bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth=m_rw; bmi.bmiHeader.biHeight=-(int)m_rh;
    bmi.bmiHeader.biPlanes=1; bmi.bmiHeader.biBitCount=32; bmi.bmiHeader.biCompression=BI_RGB;
    SetStretchBltMode(hdc, HALFTONE);
    StretchDIBits(hdc,0,0,fw,fh,0,0,m_rw,m_rh,m_renderBuf.data(),&bmi,DIB_RGB_COLORS,SRCCOPY);
}
