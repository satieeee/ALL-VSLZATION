/**
 * @file Azurite_c.cpp
 * @brief 蓝铜矿极坐标版 —— 高度场 + 法线 + Phong 光照 + 万花筒
 */

#include "Azurite_c.h"
#include "../ThreadPool.h"
#include <algorithm>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

Azurite_c::Azurite_c()
    : m_currentForm(FORM_STRATA), m_targetForm(FORM_STRATA)
    , m_formBlend(1), m_formBlendSpeed(0.05f)
    , m_colorScheme(COLOR_AZURE_DEEP), m_projection(PROJ_RADIAL)
{ memset(&m_pc, 0, sizeof(m_pc)); }

const wchar_t* Azurite_c::GetName() const { return L"Azurite_c 蓝铜矿·极"; }

void Azurite_c::SetBPM(float v) { m_bpm = v; if (m_bpm < 1) m_bpm = 1; }
void Azurite_c::NextForm() { m_targetForm = static_cast<Form>((m_targetForm+1)%FORM_COUNT); m_formBlend = 0; m_formBlendSpeed = 0.03f + (m_bpm-40)/260.0f*0.12f; }
void Azurite_c::NextColorScheme() { m_colorScheme = static_cast<ColorScheme>((m_colorScheme+1)%COLOR_COUNT); }
void Azurite_c::NextProjection() { m_projection = static_cast<Projection>((m_projection+1)%PROJ_COUNT); }
void Azurite_c::SyncBeatState(float v, float ph, bool tr) {
    m_bpm = v; m_beatPhase = ph; m_beatTrigger = tr;
    if (tr) { m_beatCount++; NextForm(); }
}

const wchar_t* Azurite_c::GetFormName() const {
    static const wchar_t* n[]={L"层理",L"编织",L"块垒",L"涟漪",L"柱林",L"地形"}; return n[m_currentForm];
}
const wchar_t* Azurite_c::GetColorSchemeName() const {
    static const wchar_t* n[]={L"深蓝",L"青霜",L"钴夜",L"冰纹",L"铜绿流",L"靛雾"}; return n[m_colorScheme];
}
const wchar_t* Azurite_c::GetProjectionName() const {
    static const wchar_t* n[]={L"万花筒",L"柔螺旋",L"中心绽放",L"水平映射",L"垂直映射",L"对角映射"};
    return n[m_projection];
}

// ============================================================================
// IProceduralEffect
// ============================================================================
void Azurite_c::PrepareFrame(float dt, float bpm) {
    m_time += dt; m_bpm = bpm;
    if (m_currentForm != m_targetForm) {
        m_formBlend += m_formBlendSpeed;
        if (m_formBlend >= 1) { m_formBlend = 1; m_currentForm = m_targetForm; }
    }
    BuildPrecompData(m_pc, m_time, m_bpm);
    m_pc.speed *= m_userSpeed;
}

float Azurite_c::EvalPattern(float xNorm, float yNorm) const {
    float r = sqrtf(xNorm*xNorm + yNorm*yNorm);
    float th = atan2f(yNorm, xNorm); if (th<0) th+=2*M_PI;
    const float dw=10,wA=2*M_PI/dw;
    float wi=floorf(th/wA), tw=th-wi*wA;
    if (fmodf(wi,2)>=1) tw=wA-tw;
    float theta = tw/wA;
    MapProjection(m_projection, m_pc, r, theta);
    return HeightField(m_currentForm, r, theta, m_time, m_pc.speed, m_pc);
}

uint32_t Azurite_c::MapToColor(float pattern, float xNorm, float yNorm, float beatPhase) const {
    float bright = (pattern + 1.0f) * 0.5f;
    if (bright<0)bright=0;if(bright>1)bright=1;
    uint8_t r8,g8,b8; HSLtoRGB(215+bright*20, 0.35f, 0.50f+bright*0.40f, r8,g8,b8);
    return ARGB(r8,g8,b8);
}

// ============================================================================
void Azurite_c::HSLtoRGB(float h, float s, float l, uint8_t& r, uint8_t& g, uint8_t& b) {
    h=fmodf(h,360.0f);if(h<0)h+=360;
    float c=(1-fabsf(2*l-1))*s,hp=h/60,x=c*(1-fabsf(fmodf(hp,2)-1)),m=l-c*.5f;
    float r1,g1,b1;
    if(hp<1){r1=c;g1=x;b1=0;}else if(hp<2){r1=x;g1=c;b1=0;}
    else if(hp<3){r1=0;g1=c;b1=x;}else if(hp<4){r1=0;g1=x;b1=c;}
    else if(hp<5){r1=x;g1=0;b1=c;}else{r1=c;g1=0;b1=x;}
    r=(uint8_t)((r1+m)*255);g=(uint8_t)((g1+m)*255);b=(uint8_t)((b1+m)*255);
}

// ============================================================================
// 6 高度场 —— 极坐标版（a = theta*2π 为角向弧度）
//   r 径向 [0,~1.4], theta 归一化 [0,1], a 角向弧度 [0,2π]
// ============================================================================
float Azurite_c::HeightField(Form f, float r, float theta, float t, float speed,
                              const PrecompData& pc) const {
    float a = theta * 2.0f * M_PI;
    switch (f) {
    case FORM_STRATA:
        // 同心环带（原水平层理 y→r）
        return fast_sin(r*6.0f + t*0.3f*speed)*0.50f
             + fast_sin(r*14.0f - t*0.2f*speed)*0.28f
             + fast_cos(r*22.0f)*0.12f;

    case FORM_WEAVE:
        // 径向-角向交织（原 x*y 网格 → r*a 干涉）
        return fast_sin(r*7.0f + t*0.4f*speed)*fast_cos(a*4.0f - t*0.35f*speed)*0.50f
             + fast_sin(r*10.0f - a*7.0f + pc.sr[20]*speed)*0.25f;

    case FORM_BLOCKS: {
        // 极坐标扇区块（原 floor(x*5)*floor(y*5) → floor(r*4)*floor(theta*30)）
        float ba = floorf(a*5.0f/M_PI);
        float br = floorf(r*4.0f);
        float h = fast_sin(ba*127.1f + br*311.7f + t*0.7f*speed)*0.55f
                + fast_sin(ba*269.5f + br*183.3f - t*0.5f*speed)*0.35f;
        return h + fast_sin(r*4.0f + t*0.3f*speed)*0.10f;
    }
    case FORM_RIPPLES:
        // 同心波（直接用 r）
        return fast_sin(r*8.0f + t*0.5f*speed)*0.55f
             + fast_cos(r*16.0f - t*0.3f*speed)*0.25f;

    case FORM_PILLARS:
        // 极坐标柱阵（sin(r)*sin(a)）
        return fast_sin(r*6.0f + t*0.3f*speed)*fast_sin(a*5.0f + t*0.25f*speed)*0.60f
             + fast_cos(r*12.0f)*fast_cos(a*10.0f)*0.18f;

    case FORM_TERRAIN:
        // 极坐标流动地形
        return fast_sin(r*3.5f + a*1.2f + t*0.4f*speed)*fast_cos(r*4.0f - a*0.8f - t*0.5f*speed)*0.42f
             + fast_sin(a*3.0f + r*1.5f + t*0.25f*speed)*0.28f
             + fast_cos(r*7.0f - a*3.0f + t*0.45f*speed)*0.18f;

    default: return 0;
    }
}

// ============================================================================
// 6 投影（极坐标操作）
// ============================================================================
void Azurite_c::MapProjection(Projection proj, const PrecompData& pc,
                               float& r, float& theta) const {
    float spd = pc.speed, a = theta*2.0f*M_PI;
    switch (proj) {
    case PROJ_RADIAL: break;
    case PROJ_SOFT_SPIRAL: {
        float spin = r*1.5f, ang = a + spin + pc.sr[30]*spd*0.3f;
        r += fast_sin(ang*2 + pc.sr[50]*spd)*0.01f;
        if(r>1)r=1;if(r<0)r=0;
        theta = fmodf(ang/(2.0f*M_PI), 1.0f); break;
    }
    case PROJ_CENTER_BLOOM: {
        float wt = 1.0f - r*0.5f;
        r = r*wt + fast_sin(a*3 + pc.sr[30]*spd)*0.02f;
        theta += fast_cos(r*4 + pc.sr[45]*spd)*0.03f;
        if(r>1)r=1;if(r<0)r=0; break;
    }
    case PROJ_HORIZON:
        r += fast_sin(a*4.0f + pc.sr[30]*spd)*0.06f;
        theta += fast_cos(r*5.0f - pc.sr[40]*spd)*0.04f/(2.0f*M_PI); break;
    case PROJ_VERTICAL:
        theta += fast_cos(r*4.0f + pc.sr[35]*spd)*0.04f/(2.0f*M_PI);
        r += fast_sin(a*5.0f - pc.sr[45]*spd)*0.06f;
        if(r>1)r=1;if(r<0)r=0; break;
    case PROJ_DIAGONAL: {
        float d=(r+a*0.1f)*0.7f, e=(r-a*0.1f)*0.7f;
        r = d + fast_sin(e*3.5f + pc.sr[50]*spd)*0.04f;
        theta = (e + fast_cos(d*4.5f - pc.sr[35]*spd)*0.03f)/(2.0f*M_PI);
        if(r>1)r=1;if(r<0)r=0; break;
    }
    }
}

// ============================================================================
// Phong 着色（与 Azurite 相同逻辑）
// ============================================================================
uint32_t Azurite_c::Shade(ColorScheme cs, float h, float nx, float ny, float nz,
                           const PrecompData& pc, float phase) const {
    const float Lx=0.55f, Ly=0.35f, Lz=0.75f;
    float dotNL = nx*Lx + ny*Ly + nz*Lz; if(dotNL<0)dotNL=0;
    float Hx=Lx, Hy=Ly, Hz=Lz+1.0f;
    float hLen=sqrtf(Hx*Hx+Hy*Hy+Hz*Hz); Hx/=hLen;Hy/=hLen;Hz/=hLen;
    float dotNH = nx*Hx + ny*Hy + nz*Hz; if(dotNH<0)dotNH=0;
    float ambient=0.10f, diffuse=dotNL*0.55f, specular=powf(dotNH,40.0f)*0.28f;
    float fresnel=(1.0f-nz)*0.07f;
    float lighting=ambient+diffuse+specular-fresnel;
    if(lighting<0.05f)lighting=0.05f;if(lighting>1.0f)lighting=1.0f;

    float sat=0.30f+h*0.20f+lighting*0.15f, light=0.35f+h*0.25f+lighting*0.30f;
    if(sat<0)sat=0;if(sat>1)sat=1;if(light<0)light=0;if(light>1)light=1;
    float bp=1.0f-phase, hue;
    switch(cs){
    default:
    case COLOR_AZURE_DEEP:   hue=215+h*15+bp*8;  break;
    case COLOR_CYAN_FROST:   hue=185+h*20+bp*10; break;
    case COLOR_COBALT_NIGHT: hue=240+h*12+bp*5;  break;
    case COLOR_ICE_VEIN:     hue=200+h*18+bp*7;  break;
    case COLOR_PATINA_FLOW:  hue=160+h*25+bp*10; break;
    case COLOR_INDIGO_MIST:  hue=250+h*15+bp*6;  break;
    }
    uint8_t r8,g8,b8; HSLtoRGB(hue,sat,light,r8,g8,b8); return ARGB(r8,g8,b8);
}

// ============================================================================
void Azurite_c::RebuildLUT(int fw, int fh) {
    int rw=fw/QUALITY, rh=fh/QUALITY;
    if(rw<1)rw=1;if(rh<1)rh=1;
    if(m_fullW==fw&&m_fullH==fh&&m_renderW==rw&&m_renderH==rh)return;
    m_fullW=fw;m_fullH=fh;m_renderW=rw;m_renderH=rh;
    int n=rw*rh;m_lut.resize(n);m_renderBuf.resize(n);
    float cx=fw*.5f,cy=fh*.5f,mr=std::min(cx,cy);if(mr<1)mr=1;
    const float dw=10,wA=2*M_PI/dw;
    for(int sy=0;sy<rh;++sy){
        float fy=(sy+.5f)*QUALITY,dy=fy-cy;
        for(int sx=0;sx<rw;++sx){
            float fx=(sx+.5f)*QUALITY,dx=fx-cx;
            float r=sqrtf(dx*dx+dy*dy)/mr;
            float th=atan2f(dy,dx);if(th<0)th+=2*M_PI;
            float wi=floorf(th/wA),tw=th-wi*wA;
            if(fmodf(wi,2)>=1)tw=wA-tw;
            int idx=sy*rw+sx;
            m_lut[idx].xNorm=dx/mr;m_lut[idx].yNorm=dy/mr;
            m_lut[idx].r=r;m_lut[idx].theta=tw/wA;
        }
    }
}

// ============================================================================
void Azurite_c::RenderChunk(int start, int end,
    Projection proj, Form cur, Form tgt, float blend, float bpm, float phase,
    ColorScheme cs, const PrecompData& pc, float t, float speed) {
    float sb=blend*blend*(3.0f-2.0f*blend);
    bool bl=(sb<1);float ib=1-sb;
    float epsR=NORM_EPS_R, epsT=NORM_EPS_T;

    for(int i=start;i<end;++i){
        float r0=m_lut[i].r, th0=m_lut[i].theta;
        float r=r0, th=th0;
        MapProjection(proj, pc, r, th);

        auto hf=[&](float pr, float pt)->float{
            float h;
            if(bl)h=HeightField(cur,pr,pt,t,speed,pc)*ib
                   +HeightField(tgt,pr,pt,t,speed,pc)*sb;
            else  h=HeightField(cur,pr,pt,t,speed,pc);
            return h;
        };

        float h0=hf(r,th);
        float hr=hf(r+epsR,th);
        float ht=hf(r,th+epsT);

        // 法线：径向梯度 + 角向梯度（角向需除以 r 补偿量纲）
        float nr = -(hr-h0)/epsR;
        float nt = -(ht-h0)/(epsT*(r0+0.01f)); // +0.01 防止 r=0 除零
        float nz = 1.0f;
        float nLen=sqrtf(nr*nr+nt*nt+nz*nz);
        nr/=nLen;nt/=nLen;nz/=nLen;

        m_renderBuf[i]=Shade(cs,h0,nr,nt,nz,pc,phase);
    }
}

// ============================================================================
void Azurite_c::Render(HDC hdc, const RECT& rect, const float*,int,const float*,int){
    int fw=rect.right-rect.left,fh=rect.bottom-rect.top;
    if(fw<=0||fh<=0)return;
    PrepareFrame(0.016f,m_bpm);
    RebuildLUT(fw,fh);
    int total=m_renderW*m_renderH;

    unsigned nTh=std::thread::hardware_concurrency();
    if(nTh<1)nTh=1;if(nTh>16)nTh=16;
    int cs_=total/nTh;if(cs_<1)cs_=1;
    float t=m_time,speed=m_bpm/120.0f;

    static ThreadPool pool;
    for(unsigned ti=0;ti<nTh;++ti){
        int s=ti*cs_,e=(ti==nTh-1)?total:s+cs_;
        pool.Enqueue([this,s,e,t,speed]{
            RenderChunk(s,e,m_projection,m_currentForm,m_targetForm,
                        m_formBlend,m_bpm,m_beatPhase,m_colorScheme,
                        std::cref(m_pc),t,speed);
        });
    }
    pool.Wait();

    BITMAPINFO bmi={};
    bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth=m_renderW;bmi.bmiHeader.biHeight=-(int)m_renderH;
    bmi.bmiHeader.biPlanes=1;bmi.bmiHeader.biBitCount=32;bmi.bmiHeader.biCompression=BI_RGB;
    SetStretchBltMode(hdc,HALFTONE);
    StretchDIBits(hdc,0,0,fw,fh,0,0,m_renderW,m_renderH,m_renderBuf.data(),&bmi,DIB_RGB_COLORS,SRCCOPY);
}
