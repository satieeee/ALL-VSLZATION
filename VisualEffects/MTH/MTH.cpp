/**
 * @file MTH.cpp
 * @brief MTH 极坐标线条 —— SDF 细线渲染（对应 MTH.frag 的 CPU 版）
 */

#include "MTH.h"
#include "../ThreadPool.h"
#include <algorithm>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

MTH::MTH() { memset(&m_pc, 0, sizeof(m_pc)); }

const wchar_t* MTH::GetName() const { return L"MTH 极坐标线条"; }

void MTH::NextForm() {
    m_targetForm = static_cast<Form>((m_targetForm+1)%FORM_COUNT);
    m_formBlend = 0; m_formBlendSpeed = 0.03f + (m_bpm-40)/260.0f*0.12f;
}
void MTH::NextColorScheme() { m_colorScheme = static_cast<ColorScheme>((m_colorScheme+1)%COLOR_COUNT); }
void MTH::NextProjection() { m_projection = static_cast<Projection>((m_projection+1)%PROJ_COUNT); }
void MTH::SyncBeatState(float bpm, float phase, bool tr) {
    m_bpm = bpm; m_beatPhase = phase; m_beatTrigger = tr;
    if (tr) NextForm();
}

const wchar_t* MTH::GetFormName() const {
    static const wchar_t* n[]={L"螺旋",L"玫瑰",L"径向",L"阿基米德"}; return n[m_currentForm];
}
const wchar_t* MTH::GetColorSchemeName() const {
    static const wchar_t* n[]={L"蓝",L"青",L"紫",L"绿",L"红",L"克莱因蓝"}; return n[m_colorScheme];
}
const wchar_t* MTH::GetProjectionName() const {
    static const wchar_t* n[]={L"源",L"汇",L"漩涡",L"鞍点",L"均匀流"}; return n[m_projection];
}

// ============================================================================
float MTH::Smoothstep(float e0, float e1, float x) {
    float t = (x - e0) / (e1 - e0);
    if (t < 0) t = 0; if (t > 1) t = 1;
    return t*t*(3 - 2*t);
}

void MTH::HSLtoRGB(float h, float s, float l, uint8_t& r, uint8_t& g, uint8_t& b) {
    h = fmodf(h, 360.0f); if (h < 0) h += 360;
    float c = (1-fabsf(2*l-1))*s, hp = h/60, x = c*(1-fabsf(fmodf(hp,2)-1)), m = l-c*0.5f;
    float r1,g1,b1;
    if (hp<1) { r1=c; g1=x; b1=0; } else if (hp<2) { r1=x; g1=c; b1=0; }
    else if (hp<3) { r1=0; g1=c; b1=x; } else if (hp<4) { r1=0; g1=x; b1=c; }
    else if (hp<5) { r1=x; g1=0; b1=c; } else { r1=c; g1=0; b1=x; }
    r=(uint8_t)((r1+m)*255); g=(uint8_t)((g1+m)*255); b=(uint8_t)((b1+m)*255);
}

// ============================================================================
// IProceduralEffect
// ============================================================================
void MTH::PrepareFrame(float dt, float bpm) {
    m_time += dt; m_bpm = bpm;
    if (m_currentForm != m_targetForm) {
        m_formBlend += m_formBlendSpeed;
        if (m_formBlend >= 1) { m_formBlend = 1; m_currentForm = m_targetForm; }
    }
    BuildPrecompData(m_pc, m_time, m_bpm);
    m_pc.speed *= m_userSpeed;
}

float MTH::EvalPattern(float xNorm, float yNorm) const {
    float x = xNorm - OFFSET_X;
    float y = yNorm - OFFSET_Y;
    float r = sqrtf(x*x + y*y);
    float th = atan2f(y, x); if (th < 0) th += 2*M_PI;
    th /= 2*M_PI;  // 归一化 [0,1]
    MapProjection(m_projection, r, th, m_time);
    return PatternField(m_currentForm, r, th, m_time, m_pc.speed);
}

uint32_t MTH::MapToColor(float pattern, float, float, float) const {
    float d = fabsf(pattern);
    float hi = THICKNESS + SHARPNESS*0.1f;
    float lo = THICKNESS - SHARPNESS*0.1f;
    float line = 1.0f - Smoothstep(lo, hi, d);           // 线掩码
    float core = 1.0f - Smoothstep(0.0f, THICKNESS, d);  // 线心高光
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
// 形态函数（对应 evalForm，4 形态）
// ============================================================================
float MTH::PatternField(Form f, float r, float theta, float t, float speed) const {
    // 旋转 + 节拍脉冲
    float beatPulse = 1.0f + 0.35f*fast_sin(m_beatPhase*2.0f*M_PI);
    float a = theta*2.0f*M_PI + t*speed*0.025f*beatPulse;
    switch (f) {
    case FORM_SPIRAL:      return fast_sin(ARM_COUNT*a + logf(fmaxf(r,0.01f))*SPIRAL_GROWTH*30.0f);
    case FORM_ARCHIMEDEAN: return fast_sin(ARM_COUNT*a + r*SPIRAL_GROWTH*30.0f);
    case FORM_ROSE:        return fast_cos(ARM_COUNT*a)*(1.0f - 0.5f*r);
    case FORM_RADIAL:      return fast_sin(ARM_COUNT*a);
    default: return 0;
    }
}

// ============================================================================
// 矢量场（对应 fieldType，5 种）
// ============================================================================
void MTH::MapProjection(Projection p, float& r, float& theta, float t) const {
    // 时间域扭曲：径向呼吸 + 角向扭动（液体流动感）
    r = r * (1.0f + 0.15f*fast_sin(r*8.0f + t*0.8f) + 0.12f*fast_cos(r*5.0f - t*0.6f));
    theta = theta + 0.08f*fast_sin(r*10.0f + t*0.9f) + 0.06f*fast_cos(r*6.0f - t*0.7f);
    switch (p) {
    case PROJ_SOURCE:  r = r*(1.0f + TWIST*0.3f); break;
    case PROJ_SINK:    r = r*(1.0f - TWIST*0.3f); break;
    case PROJ_VORTEX:  theta = theta + TWIST*r*0.3f; break;
    case PROJ_SADDLE: {
        float x = r*cosf(theta*2.0f*M_PI);
        float y = r*sinf(theta*2.0f*M_PI);
        r = x*y; break;
    }
    case PROJ_UNIFORM: break;
    }
}

// ============================================================================
void MTH::RebuildLUT(int fw, int fh) {
    int rw=fw/QUALITY, rh=fh/QUALITY;
    if(rw<1)rw=1; if(rh<1)rh=1;
    if(m_fullW==fw && m_fullH==fh && m_renderW==rw && m_renderH==rh) return;
    m_fullW=fw; m_fullH=fh; m_renderW=rw; m_renderH=rh;
    int n=rw*rh; m_lut.resize(n); m_renderBuf.resize(n);
    float cx=fw*.5f, cy=fh*.5f, mr=std::min(cx,cy); if(mr<1)mr=1;
    for(int sy=0; sy<rh; ++sy) {
        float fy=(sy+.5f)*QUALITY, dy=fy-cy;
        for(int sx=0; sx<rw; ++sx) {
            float fx=(sx+.5f)*QUALITY, dx=fx-cx;
            float x=(dx/mr)-OFFSET_X, y=(dy/mr)-OFFSET_Y;
            float r=sqrtf(x*x+y*y);
            float th=atan2f(y,x); if(th<0) th+=2*M_PI;
            int idx=sy*rw+sx;
            m_lut[idx].xNorm=dx/mr; m_lut[idx].yNorm=dy/mr;
            m_lut[idx].r=r; m_lut[idx].theta=th/(2*M_PI);
        }
    }
}

void MTH::RenderChunk(int start, int end, Projection proj, Form cur, Form tgt,
                      float blend, float t, float speed) {
    float sb=blend*blend*(3.0f-2.0f*blend);
    bool bl=(sb<1); float ib=1-sb;
    for(int i=start; i<end; ++i) {
        float r=m_lut[i].r, th=m_lut[i].theta;
        MapProjection(proj, r, th, t);
        float pat;
        if(bl) pat=PatternField(cur,r,th,t,speed)*ib + PatternField(tgt,r,th,t,speed)*sb;
        else   pat=PatternField(cur,r,th,t,speed);
        m_renderBuf[i]=MapToColor(pat, m_lut[i].xNorm, m_lut[i].yNorm, m_beatPhase);
    }
}

// ============================================================================
void MTH::Render(HDC hdc, const RECT& rect, const float*, int, const float*, int) {
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
            RenderChunk(s,e,m_projection,m_currentForm,m_targetForm,m_formBlend,t,speed);
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
