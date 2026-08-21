/**
 * @file MTH_C.cpp
 * @brief MTH_C 笛卡尔非对称线条（对应 MTH_C.frag 的 CPU 版）
 */

#include "MTH_C.h"
#include "../ThreadPool.h"
#include <algorithm>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

MTH_C::MTH_C() { memset(&m_pc, 0, sizeof(m_pc)); }

const wchar_t* MTH_C::GetName() const { return L"MTH_C 笛卡尔非对称"; }

void MTH_C::NextForm() {
    m_targetForm = static_cast<Form>((m_targetForm+1)%FORM_COUNT);
    m_formBlend = 0; m_formBlendSpeed = 0.03f + (m_bpm-40)/260.0f*0.12f;
}
void MTH_C::NextColorScheme() { m_colorScheme = static_cast<ColorScheme>((m_colorScheme+1)%COLOR_COUNT); }
void MTH_C::NextProjection() { m_projection = static_cast<Projection>((m_projection+1)%PROJ_COUNT); }
void MTH_C::SyncBeatState(float bpm, float phase, bool tr) {
    m_bpm = bpm; m_beatPhase = phase; m_beatTrigger = tr;
    if (tr) NextForm();
}

const wchar_t* MTH_C::GetFormName() const {
    static const wchar_t* n[]={L"非对称椭圆",L"三次曲线",L"偏心形线",L"非对称星形"}; return n[m_currentForm];
}
const wchar_t* MTH_C::GetColorSchemeName() const {
    static const wchar_t* n[]={L"蓝",L"青",L"紫",L"绿",L"红",L"克莱因蓝"}; return n[m_colorScheme];
}
const wchar_t* MTH_C::GetProjectionName() const {
    static const wchar_t* n[]={L"源",L"汇",L"漩涡",L"鞍点",L"均匀流"}; return n[m_projection];
}

// ============================================================================
float MTH_C::Smoothstep(float e0, float e1, float x) {
    float t = (x - e0) / (e1 - e0);
    if (t < 0) t = 0; if (t > 1) t = 1;
    return t*t*(3 - 2*t);
}

void MTH_C::HSLtoRGB(float h, float s, float l, uint8_t& r, uint8_t& g, uint8_t& b) {
    h = fmodf(h, 360.0f); if (h < 0) h += 360;
    float c = (1-fabsf(2*l-1))*s, hp = h/60, x = c*(1-fabsf(fmodf(hp,2)-1)), m = l-c*0.5f;
    float r1,g1,b1;
    if (hp<1) { r1=c; g1=x; b1=0; } else if (hp<2) { r1=x; g1=c; b1=0; }
    else if (hp<3) { r1=0; g1=c; b1=x; } else if (hp<4) { r1=0; g1=x; b1=c; }
    else if (hp<5) { r1=x; g1=0; b1=c; } else { r1=c; g1=0; b1=x; }
    r=(uint8_t)((r1+m)*255); g=(uint8_t)((g1+m)*255); b=(uint8_t)((b1+m)*255);
}

// ============================================================================
void MTH_C::PrepareFrame(float dt, float bpm) {
    m_time += dt; m_bpm = bpm;
    if (m_currentForm != m_targetForm) {
        m_formBlend += m_formBlendSpeed;
        if (m_formBlend >= 1) { m_formBlend = 1; m_currentForm = m_targetForm; }
    }
    BuildPrecompData(m_pc, m_time, m_bpm);
    m_pc.speed *= m_userSpeed;
}

float MTH_C::EvalPattern(float xNorm, float yNorm) const {
    float tx, ty;
    WarpCoord(xNorm, yNorm, m_time, m_pc.speed, tx, ty);
    return ShapeField(m_currentForm, tx, ty);
}

uint32_t MTH_C::MapToColor(float pattern, float, float, float) const {
    float d = fabsf(pattern);
    float hi = THICKNESS + SHARPNESS*0.1f;
    float lo = THICKNESS - SHARPNESS*0.1f;
    float line = 1.0f - Smoothstep(lo, hi, d);
    float core = 1.0f - Smoothstep(0.0f, THICKNESS, d);
    float shade = fminf(1.0f, line * (0.25f + 0.75f*core));

    float hueBase;
    switch (m_colorScheme) {
    default: case COLOR_BLUE:   hueBase=210; break;
    case COLOR_CYAN:   hueBase=185; break;
    case COLOR_PURPLE: hueBase=240; break;
    case COLOR_GREEN:  hueBase=165; break;
    case COLOR_RED:    hueBase=0;   break;
    case COLOR_KLEIN:  hueBase=225; break;
    }
    float fractSat = pattern*2.7f - floorf(pattern*2.7f);
    float hue = hueBase + pattern*30.0f;
    float sat = 0.10f + 0.75f*fractSat;
    float lig = 0.05f + 0.90f*(0.5f + 0.5f*fast_sin(pattern*4.0f));
    uint8_t r8,g8,b8; HSLtoRGB(hue, sat, lig, r8, g8, b8);

    uint32_t bgCol = m_darkBg ? 0xFF000000 : 0xFFFFFFFF;
    int br=(bgCol>>16)&0xFF, bg=(bgCol>>8)&0xFF, bb=bgCol&0xFF;
    int ir=(int)((1.0f-shade)*br + shade*r8 + 0.5f);
    int ig=(int)((1.0f-shade)*bg + shade*g8 + 0.5f);
    int ib=(int)((1.0f-shade)*bb + shade*b8 + 0.5f);
    if(ir>255)ir=255; if(ir<0)ir=0; if(ig>255)ig=255; if(ig<0)ig=0; if(ib>255)ib=255; if(ib<0)ib=0;
    return ARGB((uint8_t)ir,(uint8_t)ig,(uint8_t)ib);
}

// ============================================================================
// 非对称形状（4 种，奇次幂/交叉项/线性项）
// ============================================================================
float MTH_C::ShapeField(Form f, float tx, float ty) const {
    switch (f) {
    case FORM_ELLIPSE:
        return tx*tx + 0.6f*ty*ty + 0.4f*tx*ty + 0.25f*tx - 0.15f*ty - 0.20f;
    case FORM_CUBIC:
        return tx*tx*tx + 0.8f*ty*ty*ty + 0.5f*tx*ty + 0.3f*tx - 0.18f;
    case FORM_CARDIOID: {
        float r2 = tx*tx + ty*ty;
        return r2*r2 - 0.5f*tx*r2 - 0.06f*ty*ty + 0.06f*tx;
    }
    case FORM_STAR: {
        float sx = (tx >= 0 ? 1.0f : -1.0f) * powf(fabsf(tx), 0.6f);
        float sy = (ty >= 0 ? 1.0f : -1.0f) * powf(fabsf(ty), 0.6f);
        return sx + sy - 0.35f + 0.1f*tx;
    }
    default: return 0;
    }
}

// ============================================================================
// 坐标扭曲（偏移 + 旋转 + 剪切 + 矢量场 + 域扭曲）
// ============================================================================
void MTH_C::WarpCoord(float xNorm, float yNorm, float t, float speed, float& tx, float& ty) const {
    float breathe = 1.0f + 0.10f*fast_sin(t*0.9f) + 0.06f*fast_cos(t*0.6f);
    float beatPulse = 1.0f + 0.25f*fast_sin(m_beatPhase*2.0f*M_PI);
    float scale = breathe * beatPulse;
    float aRad = (ROTATION + 30.0f) * M_PI / 180.0f;
    float ca = cosf(aRad), sa = sinf(aRad);
    float wx = (xNorm + OFFSET_X + 0.30f) * scale;
    float wy = (yNorm + OFFSET_Y - 0.20f) * scale;
    tx = wx*ca - wy*sa + 0.4f*wy;
    ty = wx*sa + wy*ca;
    switch (m_projection) {
    case PROJ_SOURCE: tx*=(1.0f+TWIST*0.3f); ty*=(1.0f+TWIST*0.3f); break;
    case PROJ_SINK:   tx*=(1.0f-TWIST*0.3f); ty*=(1.0f-TWIST*0.3f); break;
    case PROJ_VORTEX: tx+=TWIST*fast_sin(ty*FREQ_LOW*4.0f); ty+=TWIST*fast_sin(tx*FREQ_HIGH*4.0f); break;
    case PROJ_SADDLE: tx=tx*ty; break;
    case PROJ_UNIFORM: break;
    }
    float tt = t*speed*0.5f;  // 时间（砍半）
    tx += TWIST*fast_sin(ty*FREQ_LOW*8.0f + 1.3f + tt);
    ty += TWIST*fast_cos(tx*FREQ_HIGH*7.0f - 0.7f + tt);
}

// ============================================================================
void MTH_C::RebuildLUT(int fw, int fh) {
    int rw=fw/QUALITY, rh=fh/QUALITY;
    if(rw<1)rw=1; if(rh<1)rh=1;
    if(m_fullW==fw && m_fullH==fh && m_renderW==rw && m_renderH==rh) return;
    m_fullW=fw; m_fullH=fh; m_renderW=rw; m_renderH=rh;
    int n=rw*rh; m_lut.resize(n); m_renderBuf.resize(n);
    float cx=fw*.5f, cy=fh*.5f, mr=std::min(cx,cy); if(mr<1)mr=1;
    for(int sy=0; sy<rh; ++sy) {
        float dy=(sy+.5f)*QUALITY-cy;
        for(int sx=0; sx<rw; ++sx) {
            float dx=(sx+.5f)*QUALITY-cx;
            int idx=sy*rw+sx;
            m_lut[idx].xNorm=dx/mr; m_lut[idx].yNorm=dy/mr;
        }
    }
}

void MTH_C::RenderChunk(int start, int end, Form cur, Form tgt,
                        float blend, float t, float speed) {
    float sb=blend*blend*(3.0f-2.0f*blend);
    bool bl=(sb<1); float ib=1-sb;
    for(int i=start; i<end; ++i) {
        float tx, ty;
        WarpCoord(m_lut[i].xNorm, m_lut[i].yNorm, t, speed, tx, ty);
        float pat;
        if(bl) pat=ShapeField(cur,tx,ty)*ib + ShapeField(tgt,tx,ty)*sb;
        else   pat=ShapeField(cur,tx,ty);
        m_renderBuf[i]=MapToColor(pat, m_lut[i].xNorm, m_lut[i].yNorm, m_beatPhase);
    }
}

// ============================================================================
void MTH_C::Render(HDC hdc, const RECT& rect, const float*, int, const float*, int) {
    int fw=rect.right-rect.left, fh=rect.bottom-rect.top;
    if(fw<=0 || fh<=0) return;
    PrepareFrame(0.016f, m_bpm);
    RebuildLUT(fw, fh);
    int total=m_renderW*m_renderH;

    unsigned nTh=std::thread::hardware_concurrency();
    if(nTh<1)nTh=1; if(nTh>16)nTh=16;
    int cs=total/nTh; if(cs<1)cs=1;
    float t=m_time, speed=m_pc.speed;

    static ThreadPool pool;
    for(unsigned ti=0; ti<nTh; ++ti) {
        int s=ti*cs, e=(ti==nTh-1)?total:s+cs;
        pool.Enqueue([this,s,e,t,speed]{
            RenderChunk(s,e,m_currentForm,m_targetForm,m_formBlend,t,speed);
        });
    }
    pool.Wait();

    BITMAPINFO bmi={};
    bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth=m_renderW; bmi.bmiHeader.biHeight=-(int)m_renderH;
    bmi.bmiHeader.biPlanes=1; bmi.bmiHeader.biBitCount=32; bmi.bmiHeader.biCompression=BI_RGB;
    SetStretchBltMode(hdc, HALFTONE);
    StretchDIBits(hdc,0,0,fw,fh,0,0,m_renderW,m_renderH,m_renderBuf.data(),&bmi,DIB_RGB_COLORS,SRCCOPY);
}
