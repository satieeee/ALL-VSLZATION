/**
 * @file main.cpp
 * @brief MAIN.exe GPU 版 —— 全 OpenGL 3.3 渲染，播放链 + 预设 + 过渡morphing + 黑底切换
 *
 * 所有可视化效果都是 .frag shader，存在 VisualEffects/shaders/ 下。
 * CPU 只负责：音频采集 → FFT → BPM → 传 uniform 给 shader。
 * 所有像素计算在 GPU 并行完成。
 *
 * 过渡：旧效果渲染到 FBO 纹理，新效果采样旧纹理做 smoothstep 混合。
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#define _UNICODE
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <gl/GL.h>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <cmath>
#include <chrono>

#include "AudioCapture/AudioCapture.h"
#include "BPMdetector/BPMDetector.h"
#include "SharedState.h"

// CPU 渲染后端（从 ALL VSLZATION 项目编译进来的 GDI 效果）
#include "Psychedelic/Psychedelic.h"
#include "GlitchForge/GlitchForge.h"
#include "Verdigris/Verdigris.h"
#include "Azurite/Azurite.h"
#include "Azurite_c/Azurite_c.h"
#include "MTH/MTH.h"
#include "MTH_C/MTH_C.h"
#include "TransitionEngine.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "opengl32.lib")

// ===== GL 3.3 Loader =====
typedef ptrdiff_t GLsizeiptr; typedef char GLchar;
#define GLP(ret,n,args) typedef ret (APIENTRY *PFN_##n)args; static PFN_##n n;
GLP(void,glGenVertexArrays,(GLsizei n,GLuint*a))
GLP(void,glBindVertexArray,(GLuint a))
GLP(void,glDeleteVertexArrays,(GLsizei n,const GLuint*a))
GLP(void,glGenBuffers,(GLsizei n,GLuint*b))
GLP(void,glBindBuffer,(GLenum t,GLuint b))
GLP(void,glBufferData,(GLenum t,GLsizeiptr s,const void*d,GLenum u))
GLP(void,glVertexAttribPointer,(GLuint i,GLint s,GLenum t,GLboolean n,GLsizei st,const void*p))
GLP(void,glEnableVertexAttribArray,(GLuint i))
GLP(GLuint,glCreateShader,(GLenum t))
GLP(void,glShaderSource,(GLuint s,GLsizei c,const char**str,const GLint*len))
GLP(void,glCompileShader,(GLuint s))
GLP(void,glGetShaderiv,(GLuint s,GLenum p,GLint*pr))
GLP(void,glGetShaderInfoLog,(GLuint s,GLsizei b,GLsizei*l,GLchar*log))
GLP(void,glDeleteShader,(GLuint s))
GLP(GLuint,glCreateProgram,())
GLP(void,glAttachShader,(GLuint p,GLuint s))
GLP(void,glLinkProgram,(GLuint p))
GLP(void,glGetProgramiv,(GLuint p,GLenum n,GLint*pr))
GLP(void,glGetProgramInfoLog,(GLuint p,GLsizei b,GLsizei*l,GLchar*log))
GLP(void,glDeleteProgram,(GLuint p))
GLP(void,glUseProgram,(GLuint p))
GLP(GLint,glGetUniformLocation,(GLuint p,const GLchar*n))
GLP(void,glUniform1f,(GLint l,GLfloat v))
GLP(void,glUniform2f,(GLint l,GLfloat x,GLfloat y))
GLP(void,glUniform1i,(GLint l,GLint v))
GLP(void,glGenFramebuffers,(GLsizei n,GLuint*b))
GLP(void,glBindFramebuffer,(GLenum t,GLuint b))
GLP(void,glFramebufferTexture2D,(GLenum t,GLenum a,GLenum tt,GLuint tex,GLint l))
GLP(GLenum,glCheckFramebufferStatus,(GLenum t))
GLP(void,glBlendColor,(GLfloat r,GLfloat g,GLfloat b,GLfloat a))
GLP(void,glDeleteFramebuffers,(GLsizei n,const GLuint*b))
GLP(void,glBlitFramebuffer,(GLint sx0,GLint sy0,GLint sx1,GLint sy1,GLint dx0,GLint dy0,GLint dx1,GLint dy1,GLbitfield mask,GLenum filter))
GLP(void,glActiveTexture,(GLenum t))
#undef GLP
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_LINEAR 0x2601
#define GL_NEAREST 0x2600
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_CONSTANT_ALPHA 0x8003
#define GL_ONE_MINUS_CONSTANT_ALPHA 0x8004
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_COLOR_LOGIC_OP 0x0BF2
#define GL_COPY_INVERTED 0x150C

static void LoadGL(){
#define L(n) n=(PFN_##n)wglGetProcAddress(#n); if(!n) n=(PFN_##n)wglGetProcAddress(#n "ARB");
L(glGenVertexArrays) L(glBindVertexArray) L(glDeleteVertexArrays)
L(glGenBuffers) L(glBindBuffer) L(glBufferData)
L(glVertexAttribPointer) L(glEnableVertexAttribArray)
L(glCreateShader) L(glShaderSource) L(glCompileShader)
L(glGetShaderiv) L(glGetShaderInfoLog) L(glDeleteShader)
L(glCreateProgram) L(glAttachShader) L(glLinkProgram)
L(glGetProgramiv) L(glGetProgramInfoLog) L(glDeleteProgram)
L(glUseProgram) L(glGetUniformLocation)
L(glUniform1f) L(glUniform2f) L(glUniform1i)
L(glGenFramebuffers) L(glBindFramebuffer) L(glFramebufferTexture2D) L(glCheckFramebufferStatus) L(glBlendColor) L(glDeleteFramebuffers)
L(glBlitFramebuffer)
L(glActiveTexture)
#undef L
}

// ===== CPU 渲染副产品模拟: 全局状态 =====
static float g_ditherStrength = 1.0f; // GDI质感强度: 0=纯GPU 0.5=微妙 0.7=标准 1.0=低保真
static GLuint g_halfFbo = 0, g_halfTex = 0;
static int    g_halfW = 0, g_halfH = 0;
static std::string g_artifactCode;    // CpuArtifact.glsl 内容

// ===== GpuEffect ===== 一个 .frag shader 的封装
struct GpuEffect {
    std::wstring name;
    GLuint program = 0;
    std::map<std::string, GLint> uniforms;
    int form = 0, targetForm = 0, color = 0, proj = 0, formCount = 6;
    float formBlend = 1.0f, formBlendSpeed = 0.05f;

    GLint U(const char* n) {
        auto it = uniforms.find(n);
        if (it != uniforms.end() && it->second >= 0) return it->second;
        GLint loc = glGetUniformLocation(program, n);
        uniforms[n] = loc;
        return loc;
    }
    bool Load(const char* src) {
        program = 0; uniforms.clear();
        const char* vs = R"(#version 330 core
layout(location=0)in vec2 p;layout(location=1)in vec2 u;out vec2 vUV;void main(){vUV=u;gl_Position=vec4(p,0,1);})";
        auto comp = [](GLenum t, const char* s) {
            GLuint sh = glCreateShader(t); glShaderSource(sh, 1, &s, 0); glCompileShader(sh);
            GLint ok; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                char log[4096]; GLsizei len = 0;
                glGetShaderInfoLog(sh, 4096, &len, log);
                if (len > 0) {
                    OutputDebugStringA("=== GLSL Compile Error ===\n");
                    OutputDebugStringA(log);
                    OutputDebugStringA("\n=== Source (first 2000 chars) ===\n");
                    size_t srcLen = strlen(s);
                    std::string preview(s, s + (srcLen < 2000 ? srcLen : 2000));
                    OutputDebugStringA(preview.c_str());
                    OutputDebugStringA("\n=============================\n");
                }
                glDeleteShader(sh); return (GLuint)0;
            }
            return sh;
        };
        GLuint vs2 = comp(GL_VERTEX_SHADER, vs), fs = comp(GL_FRAGMENT_SHADER, src);
        if (!vs2 || !fs) return false;
        program = glCreateProgram(); glAttachShader(program, vs2); glAttachShader(program, fs); glLinkProgram(program);
        glDeleteShader(vs2); glDeleteShader(fs);
        GLint ok; glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) { program = 0; uniforms.clear(); return false; }
        uniforms.clear(); return true;
    }
    void BindUniforms(float t, float bpm, float phase, float speed, float w, float h, bool darkBg, float opacity = 1.0f) {
        glUniform1f(U("uTime"), t); glUniform1f(U("uBpm"), bpm); glUniform1f(U("uBeatPhase"), phase);
        glUniform1f(U("uSpeed"), speed); glUniform2f(U("uResolution"), w, h);
        glUniform1i(U("uForm"), form); glUniform1i(U("uFormB"), targetForm);
        glUniform1f(U("uFormBlend"), formBlend);
        glUniform1i(U("uColorScheme"), color); glUniform1i(U("uProjection"), proj);
        glUniform1i(U("uDarkBg"), darkBg ? 1 : 0);
        glUniform1f(U("uOpacity"), opacity);
        glUniform1i(U("uEncode"), 0);
        glUniform1f(U("uMorphBlend"), 0.0f);
        // CPU 渲染质感
        glUniform1f(U("uDitherStrength"), g_ditherStrength);
    }
    void BindTransitionUniforms(float blend) {
        glUniform1f(U("uMorphBlend"), blend);
        glUniform1i(U("uOldEncoded"), 0);
    }
};

static GpuEffect g_effects[7];
static int g_effectCount = 0;
static wchar_t g_shaderDir[MAX_PATH] = {};
static GLuint g_vao = 0;
static GLuint g_oldFbo = 0, g_oldTex = 0;
static GLuint g_blendFbo = 0, g_blendTex = 0;  // 上层效果纹理
static GLuint g_blendProg = 0;  // 混合 shader
static int g_fboW = 0, g_fboH = 0;
static int g_blendMode = 0;
static float g_blendOpacity = 1.0f;

static void InitVAO() {
    float v[] = {-1,-1,0,0, 1,-1,1,0, -1,1,0,1, 1,-1,1,0, -1,1,0,1, 1,1,1,1};
    glGenVertexArrays(1, &g_vao); GLuint vbo;
    glGenBuffers(1, &vbo); glBindVertexArray(g_vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void*)8); glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

static void EnsureFBO(int w, int h) {
    if (w == g_fboW && h == g_fboH) return;
    if (g_oldFbo) { glDeleteFramebuffers(1, &g_oldFbo); glDeleteTextures(1, &g_oldTex); }
    if (g_blendFbo) { glDeleteFramebuffers(1, &g_blendFbo); glDeleteTextures(1, &g_blendTex); }
    g_fboW = w; g_fboH = h;

    auto makeFBO = [](GLuint& fbo, GLuint& tex) {
        glGenFramebuffers(1, &fbo); glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_fboW, g_fboH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        GLenum fbStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (fbStatus != 0x8CD5) { // GL_FRAMEBUFFER_COMPLETE
            char msg[64]; snprintf(msg, 64, "FBO incomplete: 0x%X", fbStatus);
            MessageBoxA(0, msg, "FBO Error", MB_OK);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    };
    makeFBO(g_oldFbo, g_oldTex);
    makeFBO(g_blendFbo, g_blendTex);
}

static void DrawEffect(int idx, float t, float bpm, float phase, float speed, float w, float h, bool darkBg, float opacity = 1.0f) {
    if (idx < 0 || idx >= g_effectCount || !g_effects[idx].program) return;
    glUseProgram(g_effects[idx].program);
    g_effects[idx].BindUniforms(t, bpm, phase, speed, w, h, darkBg, opacity);
    glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

static void DrawTransition(int oldIdx, int newIdx, float blend, float t, float bpm, float phase, float speed, float w, float h) {
    // Pass 1: 编码旧效果到 FBO（图案+颜色，匹配 PLRender BLUR 路径）
    EnsureFBO((int)w, (int)h);
    glBindFramebuffer(GL_FRAMEBUFFER, g_oldFbo);
    glViewport(0, 0, (int)w, (int)h);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(g_effects[oldIdx].program);
    g_effects[oldIdx].BindUniforms(t, bpm, phase, speed, w, h, false);
    glUniform1i(g_effects[oldIdx].U("uEncode"), 1);
    glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Pass 2: 新效果解码旧标量场+旧颜色，做参数级 morph + 双调色板 RGB lerp
    glUseProgram(g_effects[newIdx].program);
    g_effects[newIdx].BindUniforms(t, bpm, phase, speed, w, h, false);
    g_effects[newIdx].BindTransitionUniforms(blend);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_oldTex);
    glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

// ===== 共享数据定义（CPU/GPU 后端共用）=====
enum BlendMode { BM_None=0, BM_Lighten, BM_Screen, BM_Overlay, BM_HardLight, BM_SoftLight, BM_LinearBurn, BM_Multiply, BM_COUNT };

struct PlaylistItem { std::wstring name, name2; BlendMode blendMode=BM_None; float opacity=1.0f; int durationSec=30; };

// ============================================================================
// CPU 渲染后端 —— GDI 效果 + TransitionEngine（匹配 ALL VSLZATION 的 PlaylistManager）
// 通过界面上的 CPU/GPU 切换按钮选择渲染模式
// ============================================================================

// 单通道 Photoshop 混合（移植自 CPU main.cpp 的 BlendChannel）
static inline uint8_t CpuBlendChannel(uint8_t base, uint8_t top, int mode) {
    float a = (float)base / 255.0f, b = (float)top / 255.0f, r;
    switch (mode) {
        case BM_Lighten:    r = (a > b) ? a : b; break;
        case BM_Screen:     r = 1.0f - (1.0f - a) * (1.0f - b); break;
        case BM_Overlay:    r = (a < 0.5f) ? (2.0f * a * b) : (1.0f - 2.0f * (1.0f - a) * (1.0f - b)); break;
        case BM_HardLight:  r = (b < 0.5f) ? (2.0f * a * b) : (1.0f - 2.0f * (1.0f - a) * (1.0f - b)); break;
        case BM_SoftLight: {
            if (b < 0.5f) r = a - (1.0f - 2.0f * b) * a * (1.0f - a);
            else { float g = (a <= 0.25f) ? ((16.0f * a - 12.0f) * a + 4.0f) * a : sqrtf(a); r = a + (2.0f * b - 1.0f) * (g - a); }
            break;
        }
        case BM_LinearBurn: r = a + b - 1.0f; if (r < 0) r = 0; break;
        case BM_Multiply:   r = a * b; break;
        default: r = b; break;
    }
    if (r < 0) r = 0; if (r > 1.0f) r = 1.0f;
    return (uint8_t)(r * 255.0f + 0.5f);
}

// 播放链窗口默认尺寸（CPU 渲染分辨率由动态像素预算决定，与窗口默认尺寸独立）
static const int PLAYER_DEFAULT_W = 512;
static const int PLAYER_DEFAULT_H = 350;

// CPU 播放链管理器（移植自 CPU 版 main.cpp 的 PlaylistManager 类）
class CpuPlaylistManager {
public:
    enum State { STOPPED, PLAYING, WAITING_BEAT, BLUR };

    void AddItem(const std::wstring& name, int durSec) {
        m_items.push_back({name, L"", BM_None, 1.0f, durSec});
    }
    void Clear() { m_items.clear(); Stop(); }
    void LoadItems(const std::vector<PlaylistItem>& items) { m_items = items; }
    bool IsBlackBg() const { return m_blackBg; }
    void ToggleBg() { m_blackBg = !m_blackBg; }
    void SetSpeed(float m) {
        if (m_currentEff)  m_currentEff->SetUserSpeed(m);
        if (m_currentEff2) m_currentEff2->SetUserSpeed(m);
    }
    void SetDarkMode(bool d) { m_blackBg = d; }

    const std::vector<PlaylistItem>& Items() const { return m_items; }
    State GetState() const { return m_state; }
    float ElapsedSec() const { return m_elapsed; }

    void Start() {
        if (m_items.empty()) return;
        m_currentIdx = 0; m_elapsed = 0;
        m_currentEff.reset(); m_currentEff2.reset();
        m_previousEff.reset(); m_previousEff2.reset();
        auto it = s_registry.find(m_items[0].name);
        if (it != s_registry.end()) m_currentEff = it->second();
        if (!m_items[0].name2.empty()) {
            auto it2 = s_registry.find(m_items[0].name2);
            if (it2 != s_registry.end()) m_currentEff2 = it2->second();
        }
        m_state = PLAYING;
    }

    void Stop() {
        m_state = STOPPED;
        m_currentEff.reset(); m_currentEff2.reset();
        m_previousEff.reset(); m_previousEff2.reset();
        if (m_hBmp) { DeleteObject(m_hBmp); m_hBmp = nullptr; m_bmpW = m_bmpH = 0; }
        if (m_hdcMem) { DeleteDC(m_hdcMem); m_hdcMem = nullptr; }
    }

    void Update(float dt, float bpm, float beatPhase, bool beatTriggered) {
        if (m_state == STOPPED || m_items.empty()) return;
        m_bpm = bpm; m_beatPhase = beatPhase;
        if (m_currentEff)  { m_currentEff->SyncBeatState(bpm, beatPhase, beatTriggered); m_currentEff->SetDarkMode(m_blackBg); }
        if (m_currentEff2) { m_currentEff2->SyncBeatState(bpm, beatPhase, beatTriggered); m_currentEff2->SetDarkMode(m_blackBg); }

        if (m_state == PLAYING) {
            m_elapsed += dt;
            float itemDur = (float)m_items[m_currentIdx].durationSec;
            if (itemDur > 0 && m_elapsed >= itemDur) m_state = WAITING_BEAT;
        }
        if (m_state == WAITING_BEAT && beatTriggered) {
            if (m_currentIdx + 1 >= (int)m_items.size()) AdvanceToFirst();
            else AdvanceToNext();
        }
        if (m_state == BLUR) {
            m_blurT += dt / BLUR_SEC;
            if (m_blurT >= 1.0f) {
                m_blurT = 0; m_elapsed = 0;
                m_previousEff.reset(); m_previousEff2.reset();
                m_state = PLAYING;
            }
        }
    }

    void Render(HDC hdc, int fw, int fh) {
        if (fw <= 0 || fh <= 0) return;
        auto t0 = std::chrono::steady_clock::now();

        // 动态分辨率：根据当前像素预算 + 窗口尺寸计算渲染尺寸（保宽高比）
        int rw, rh;
        ComputeRenderSize(fw, fh, rw, rh);

        if (m_state == BLUR) {
            auto* oldProc = dynamic_cast<IProceduralEffect*>(m_previousEff.get());
            auto* newProc = dynamic_cast<IProceduralEffect*>(m_currentEff.get());
            if (oldProc && newProc) {
                RECT fullRect = {0, 0, fw, fh};
                TransitionEngine::RenderTransition(oldProc, newProc, m_blurT, 0.016f, m_bpm, m_beatPhase, hdc, fullRect, rw, rh);
                auto t1 = std::chrono::steady_clock::now();
                AdjustBudget(std::chrono::duration<float, std::milli>(t1 - t0).count());
                return;
            }
        }
        EnsureBufs(rw, rh);
        int n = rw * rh;
        if (m_state != STOPPED && m_currentEff) {
            RenderEffect(m_currentEff.get(), m_outBuf, rw, rh);
        } else {
            uint32_t fill = m_blackBg ? 0xFF000000 : 0xFFFFFFFF;
            for (int i = 0; i < n; ++i) m_outBuf[i] = fill;
        }
        if (m_state != STOPPED && m_currentEff2 && m_state != BLUR) {
            int idx = m_currentIdx;
            if (idx >= 0 && idx < (int)m_items.size()) {
                int mode = (int)m_items[idx].blendMode;
                float opacity = m_items[idx].opacity;
                if (mode != BM_None && opacity > 0.0f) {
                    RenderEffect(m_currentEff2.get(), m_outBuf2, rw, rh);
                    for (int i = 0; i < n; ++i) {
                        uint32_t base = m_outBuf[i], top = m_outBuf2[i];
                        int br = (base >> 16) & 0xFF, bg = (base >> 8) & 0xFF, bb = base & 0xFF;
                        int tr = (top >> 16) & 0xFF,  tg = (top >> 8) & 0xFF,  tb = top & 0xFF;
                        uint8_t mr = CpuBlendChannel((uint8_t)br, (uint8_t)tr, mode);
                        uint8_t mg = CpuBlendChannel((uint8_t)bg, (uint8_t)tg, mode);
                        uint8_t mb = CpuBlendChannel((uint8_t)bb, (uint8_t)tb, mode);
                        int rr = (int)(br + (mr - br) * opacity + 0.5f);
                        int rg = (int)(bg + (mg - bg) * opacity + 0.5f);
                        int rb = (int)(bb + (mb - bb) * opacity + 0.5f);
                        if (rr < 0) rr = 0; if (rr > 255) rr = 255;
                        if (rg < 0) rg = 0; if (rg > 255) rg = 255;
                        if (rb < 0) rb = 0; if (rb > 255) rb = 255;
                        m_outBuf[i] = 0xFF000000 | (rr << 16) | (rg << 8) | rb;
                    }
                }
            }
        }
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = rw; bmi.bmiHeader.biHeight = -(int)rh;
        bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32; bmi.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(hdc, HALFTONE);
        StretchDIBits(hdc, 0, 0, fw, fh, 0, 0, rw, rh, m_outBuf.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);

        auto t1 = std::chrono::steady_clock::now();
        AdjustBudget(std::chrono::duration<float, std::milli>(t1 - t0).count());
    }

    using Factory = std::unique_ptr<IVisualEffect>(*)();
    static void Register(const std::wstring& name, Factory f) { s_registry[name] = f; }
    static const std::map<std::wstring, Factory>& Registry() { return s_registry; }

private:
    static constexpr float BLUR_SEC = 0.70f;
    // 动态分辨率控制：目标帧耗时 + 像素预算上下限
    static constexpr float TARGET_FRAME_MS = 14.0f;
    static constexpr float BUDGET_MIN = 64000.0f;      // ≈ 320×200
    static constexpr float BUDGET_MAX = 3686400.0f;    // ≈ 1920×1920
    float m_pixelBudget = 921600.0f;                   // 初始 ≈ 1280×720
    std::vector<PlaylistItem> m_items;
    State m_state = STOPPED;
    int m_currentIdx = 0;
    float m_elapsed = 0, m_blurT = 0;
    std::unique_ptr<IVisualEffect> m_currentEff, m_currentEff2, m_previousEff, m_previousEff2;
    bool m_blackBg = false;
    float m_bpm = 120, m_beatPhase = 0;
    std::vector<uint32_t> m_outBuf, m_outBuf2;
    int m_rw = 0, m_rh = 0;
    HDC m_hdcMem = nullptr;
    HBITMAP m_hBmp = nullptr;
    void* m_bmpBits = nullptr;
    int m_bmpW = 0, m_bmpH = 0;
    static std::map<std::wstring, Factory> s_registry;

    void EnsureBufs(int w, int h) {
        if (w == m_rw && h == m_rh) return;
        m_rw = w; m_rh = h;
        int n = w * h;
        m_outBuf.resize(n); m_outBuf2.resize(n);
    }

    // 根据窗口尺寸 + 当前像素预算计算渲染分辨率（保宽高比、连续缩放，不跳档）
    void ComputeRenderSize(int fw, int fh, int& rw, int& rh) const {
        float winPx = (float)fw * (float)fh;
        float s = (winPx > m_pixelBudget) ? sqrtf(winPx / m_pixelBudget) : 1.0f;
        rw = (int)((float)fw / s + 0.5f);
        rh = (int)((float)fh / s + 0.5f);
        if (rw < 1) rw = 1;
        if (rh < 1) rh = 1;
    }

    // 帧耗时反馈：卡了就降分辨率、有余力就升分辨率（带滞回，避免震荡）
    void AdjustBudget(float frameMs) {
        if (frameMs > TARGET_FRAME_MS * 1.15f)      m_pixelBudget *= 0.90f;
        else if (frameMs < TARGET_FRAME_MS * 0.85f) m_pixelBudget *= 1.05f;
        if (m_pixelBudget < BUDGET_MIN) m_pixelBudget = BUDGET_MIN;
        if (m_pixelBudget > BUDGET_MAX) m_pixelBudget = BUDGET_MAX;
    }

    void RenderEffect(IVisualEffect* eff, std::vector<uint32_t>& buf, int fw, int fh) {
        if (!eff) return;
        int bmpW = fw, bmpH = fh;
        if (bmpW != m_bmpW || bmpH != m_bmpH) {
            if (m_hBmp) { DeleteObject(m_hBmp); m_hBmp = nullptr; }
            if (m_hdcMem) { DeleteDC(m_hdcMem); m_hdcMem = nullptr; }
            m_bmpW = bmpW; m_bmpH = bmpH;
        }
        if (!m_hdcMem) {
            HDC hdcScreen = GetDC(nullptr);
            m_hdcMem = CreateCompatibleDC(hdcScreen);
            BITMAPINFO bmi = {};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = bmpW; bmi.bmiHeader.biHeight = -(int)bmpH;
            bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32; bmi.bmiHeader.biCompression = BI_RGB;
            m_hBmp = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS, &m_bmpBits, nullptr, 0);
            SelectObject(m_hdcMem, m_hBmp);
            ReleaseDC(nullptr, hdcScreen);
        }
        RECT r = {0, 0, bmpW, bmpH};
        if (m_blackBg) memset(m_bmpBits, 0, bmpW * bmpH * sizeof(uint32_t));
        else {
            HBRUSH bg = CreateSolidBrush(RGB(255, 255, 255));
            FillRect(m_hdcMem, &r, bg);
            DeleteObject(bg);
        }
        eff->Render(m_hdcMem, r, nullptr, 0, nullptr, 0);
        memcpy(buf.data(), m_bmpBits, bmpW * bmpH * sizeof(uint32_t));
    }

    void AdvanceToNext() {
        m_currentIdx++; m_elapsed = 0; m_blurT = 0;
        m_previousEff = std::move(m_currentEff); m_previousEff2 = std::move(m_currentEff2);
        m_currentEff2.reset();
        auto& item = m_items[m_currentIdx];
        auto it = s_registry.find(item.name);
        if (it != s_registry.end()) m_currentEff = it->second();
        if (!item.name2.empty()) {
            auto it2 = s_registry.find(item.name2);
            if (it2 != s_registry.end()) m_currentEff2 = it2->second();
        }
        m_state = BLUR;
    }

    void AdvanceToFirst() {
        m_currentIdx = 0; m_elapsed = 0; m_blurT = 0;
        m_previousEff = std::move(m_currentEff); m_previousEff2 = std::move(m_currentEff2);
        m_currentEff2.reset();
        auto& item = m_items[0];
        auto it = s_registry.find(item.name);
        if (it != s_registry.end()) m_currentEff = it->second();
        if (!item.name2.empty()) {
            auto it2 = s_registry.find(item.name2);
            if (it2 != s_registry.end()) m_currentEff2 = it2->second();
        }
        m_state = BLUR;
    }
};

std::map<std::wstring, CpuPlaylistManager::Factory> CpuPlaylistManager::s_registry;

static void RegisterCpuEffects() {
    CpuPlaylistManager::Register(L"Psychedelic",  []() -> std::unique_ptr<IVisualEffect> { return std::make_unique<Psychedelic>(); });
    CpuPlaylistManager::Register(L"GlitchForge", []() -> std::unique_ptr<IVisualEffect> { return std::make_unique<GlitchForge>(); });
    CpuPlaylistManager::Register(L"Verdigris",   []() -> std::unique_ptr<IVisualEffect> { return std::make_unique<Verdigris>(); });
    CpuPlaylistManager::Register(L"Azurite",     []() -> std::unique_ptr<IVisualEffect> { return std::make_unique<Azurite>(); });
    CpuPlaylistManager::Register(L"Azurite_c",   []() -> std::unique_ptr<IVisualEffect> { return std::make_unique<Azurite_c>(); });
    CpuPlaylistManager::Register(L"MTH",         []() -> std::unique_ptr<IVisualEffect> { return std::make_unique<MTH>(); });
    CpuPlaylistManager::Register(L"MTH_C",       []() -> std::unique_ptr<IVisualEffect> { return std::make_unique<MTH_C>(); });
}

static bool g_cpuMode = false;           // false=GPU渲染, true=CPU GDI渲染
static CpuPlaylistManager g_cpuPlaylist;
static bool g_cpuPlayerMode = false;     // CPU 模式下的播放链
static int g_cpuCurrEffIdx = -1;         // CPU 独立模式下当前选中的效果索引
static std::unique_ptr<IVisualEffect> g_cpuStandaloneEff;  // CPU 独立模式下的效果对象

// ===== 播放链 (GPU) =====
static std::vector<PlaylistItem> g_playlist;
static int g_plIndex = 0, g_plEffIdx[2] = {-1,-1}, g_plOldEffIdx = -1;
static float g_plElapsed = 0, g_plBlurT = 0;
enum PLState { PL_STOPPED, PL_PLAYING, PL_WAITBEAT, PL_BLUR };
static PLState g_plState = PL_STOPPED;
static float g_plTime = 0, g_plOldTime = 0, g_plBpm = 120, g_plPhase = 0;
static bool g_plBlackBg = false;
static float g_userSpeed = 1.0f;
static const float BLUR_SEC = 0.50f;

// 预设
static std::vector<PlaylistItem> g_presets[5];

static int EffectIndex(const std::wstring& name) {
    for (int i = 0; i < g_effectCount; ++i)
        if (g_effects[i].name == name) return i;
    return -1;
}

static void PLStart() {
    if (g_playlist.empty()) return;
    g_plIndex = 0; g_plElapsed = 0; g_plTime = 0;
    g_plEffIdx[0] = EffectIndex(g_playlist[0].name);
    g_plEffIdx[1] = g_playlist[0].name2.empty() ? -1 : EffectIndex(g_playlist[0].name2);
    g_plState = PL_PLAYING;
}

static void PLStop() { g_plState = PL_STOPPED; }

static void PLUpdate(float dt, float bpm, float phase, bool beat) {
    if (g_plState == PL_STOPPED || g_playlist.empty()) return;
    g_plBpm = bpm; g_plPhase = phase; g_plTime += dt;
    // 形态 blend 推进
    if (g_plEffIdx[0] >= 0 && g_plEffIdx[0] < g_effectCount) {
        auto& e = g_effects[g_plEffIdx[0]];
        if (e.formBlend < 1.0f) {
            e.formBlend += e.formBlendSpeed;
            if (e.formBlend >= 1.0f) { e.formBlend = 1.0f; e.form = e.targetForm; }
        }
        // 上层也推进
        if (g_plEffIdx[1] >= 0 && g_plEffIdx[1] < g_effectCount) {
            auto& e2 = g_effects[g_plEffIdx[1]];
            if (e2.formBlend < 1.0f) {
                e2.formBlend += e2.formBlendSpeed;
                if (e2.formBlend >= 1.0f) { e2.formBlend = 1.0f; e2.form = e2.targetForm; }
            }
        }
    }
    // 鼓点触发形态切换（匹配 CPU SyncBeatState：所有状态下都触发）
    if (beat && (g_plState == PL_PLAYING || g_plState == PL_BLUR) && g_plEffIdx[0] >= 0 && g_plEffIdx[0] < g_effectCount) {
        auto& e = g_effects[g_plEffIdx[0]];
        e.targetForm = (e.form + 1) % e.formCount;
        e.formBlend = 0.0f;
        e.formBlendSpeed = 0.03f + (bpm - 40.0f) / 260.0f * 0.12f;
    }

    if (g_plState == PL_PLAYING) {
        g_plElapsed += dt;
        if (g_plElapsed >= g_playlist[g_plIndex].durationSec)
            g_plState = PL_WAITBEAT;
    }
    if (g_plState == PL_WAITBEAT && beat) {
        g_plOldEffIdx = g_plEffIdx[0]; // 保存旧效果
        if (g_plIndex + 1 >= (int)g_playlist.size()) g_plIndex = 0;
        else g_plIndex++;
        g_plElapsed = 0; g_plBlurT = 0;
        g_plEffIdx[0] = EffectIndex(g_playlist[g_plIndex].name);
        g_plEffIdx[1] = g_playlist[g_plIndex].name2.empty() ? -1 : EffectIndex(g_playlist[g_plIndex].name2);
        g_plOldTime = g_plTime; g_plTime = 0; // 新效果时间归零（匹配 CPU new 对象 m_time=0）
        g_plState = PL_BLUR;
        g_fboW = 0; // 强制重建 FBO
    }
    if (g_plState == PL_BLUR) {
        g_plOldTime += dt; // 旧效果过渡中继续动画（匹配 CPU TransitionEngine 每帧 PrepareFrame 两效果）
        g_plBlurT += dt / BLUR_SEC;
        if (g_plBlurT >= 1.0f) {
            g_plBlurT = 0; g_plElapsed = 0;
            g_plState = PL_PLAYING;
        }
    }
}

// ===== 渲染播放链画面 =====
static void DrawArtifactHUD(HDC hdc);
static void DownscaleBlit(int w, int h) {
    int dw = w * 2 / 3, dh = h * 2 / 3;
    if (dw < 1) dw = 1; if (dh < 1) dh = 1;
    if (dw != g_halfW || dh != g_halfH) {
        if (g_halfFbo) { glDeleteFramebuffers(1, &g_halfFbo); glDeleteTextures(1, &g_halfTex); }
        g_halfW = dw; g_halfH = dh;
        glGenFramebuffers(1, &g_halfFbo); glGenTextures(1, &g_halfTex);
        glBindTexture(GL_TEXTURE_2D, g_halfTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, dw, dh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, g_halfFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_halfTex, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_halfFbo);
    glBlitFramebuffer(0, 0, w, h, 0, 0, dw, dh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_halfFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, dw, dh, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void PLRender(HDC hdc, int w, int h) {
    float speed = g_plBpm / 120.0f * g_userSpeed;

    if (g_plState == PL_BLUR) {
        // 传原始线性 t，shader 内部做 smoothstep（匹配 CPU TransitionEngine）
        if (g_plOldEffIdx >= 0 && g_plEffIdx[0] >= 0) {
            // Pass 1: 编码旧效果到 FBO（存储标量场，不是颜色）
            EnsureFBO(w, h);
            glBindFramebuffer(GL_FRAMEBUFFER, g_oldFbo); glViewport(0,0,w,h); glClear(GL_COLOR_BUFFER_BIT);
            glUseProgram(g_effects[g_plOldEffIdx].program);
            g_effects[g_plOldEffIdx].BindUniforms(g_plOldTime, g_plBpm, g_plPhase, speed, (float)w, (float)h, g_plBlackBg, 1.0f);
            glUniform1i(g_effects[g_plOldEffIdx].U("uEncode"), 1); // 编码模式
            glBindVertexArray(g_vao); glDrawArrays(GL_TRIANGLES,0,6);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            // Pass 2: 新效果解码旧标量场做参数级混合
            glUseProgram(g_effects[g_plEffIdx[0]].program);
            g_effects[g_plEffIdx[0]].BindUniforms(g_plTime, g_plBpm, g_plPhase, speed, (float)w, (float)h, g_plBlackBg, 1.0f);
            glUniform1f(g_effects[g_plEffIdx[0]].U("uMorphBlend"), g_plBlurT);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, g_oldTex);
            glUniform1i(g_effects[g_plEffIdx[0]].U("uOldEncoded"), 0);
            glBindVertexArray(g_vao); glDrawArrays(GL_TRIANGLES,0,6);
        } else if (g_plEffIdx[0] >= 0) {
            DrawEffect(g_plEffIdx[0], g_plTime, g_plBpm, g_plPhase, speed, (float)w, (float)h, g_plBlackBg);
        }
        DownscaleBlit(w, h);
        SwapBuffers(hdc);
        DrawArtifactHUD(hdc);
        return;
    }
    if (g_plState != PL_STOPPED && g_plEffIdx[0] >= 0) {
        auto& item = g_playlist[g_plIndex];
        float opacity = item.opacity;
        if (g_plEffIdx[1] >= 0 && opacity > 0.0f) {
            int blendMode = (int)item.blendMode;
            // ── 7 种 Photoshop 混合模式路径（匹配 CPU PlaylistManager::Render）──
            if (blendMode > 0 && g_blendProg) {
                EnsureFBO(w, h);
                // Pass 1: 底层效果 → g_blendFbo
                glBindFramebuffer(GL_FRAMEBUFFER, g_blendFbo);
                glViewport(0, 0, w, h);
                if (g_plBlackBg) { glClearColor(0.0f,0.0f,0.0f,1.0f); }
                else             { glClearColor(1.0f,1.0f,1.0f,1.0f); }
                glClear(GL_COLOR_BUFFER_BIT);
                DrawEffect(g_plEffIdx[0], g_plTime, g_plBpm, g_plPhase, speed, (float)w, (float)h, g_plBlackBg, 1.0f);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                // Pass 2: 上层效果 → g_oldFbo（全不透明度，由合成阶段控制）
                glBindFramebuffer(GL_FRAMEBUFFER, g_oldFbo);
                glViewport(0, 0, w, h);
                if (g_plBlackBg) { glClearColor(0.0f,0.0f,0.0f,1.0f); }
                else             { glClearColor(1.0f,1.0f,1.0f,1.0f); }
                glClear(GL_COLOR_BUFFER_BIT);
                DrawEffect(g_plEffIdx[1], g_plTime, g_plBpm, g_plPhase, speed, (float)w, (float)h, g_plBlackBg, 1.0f);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                // Pass 3: 逐通道 Photoshop 公式合成（=CPU BlendChannel + opacity lerp）
                glViewport(0, 0, w, h);
                glUseProgram(g_blendProg);
                glUniform1i(glGetUniformLocation(g_blendProg, "uMode"), blendMode);
                glUniform1f(glGetUniformLocation(g_blendProg, "uOpacity"), opacity);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, g_blendTex);
                glUniform1i(glGetUniformLocation(g_blendProg, "uBase"), 0);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, g_oldTex);
                glUniform1i(glGetUniformLocation(g_blendProg, "uOverlay"), 1);
                glBindVertexArray(g_vao);
                glDrawArrays(GL_TRIANGLES, 0, 6);
            } else {
                // 退化路径：简单 alpha 混合（blendMode==None 或 shader 不可用时）
                DrawEffect(g_plEffIdx[0], g_plTime, g_plBpm, g_plPhase, speed, (float)w, (float)h, g_plBlackBg, 1.0f);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                DrawEffect(g_plEffIdx[1], g_plTime, g_plBpm, g_plPhase, speed, (float)w, (float)h, g_plBlackBg, opacity);
                glDisable(GL_BLEND);
            }
        } else {
            DrawEffect(g_plEffIdx[0], g_plTime, g_plBpm, g_plPhase, speed, (float)w, (float)h, g_plBlackBg);
        }
    } else {
        glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
    }
    DownscaleBlit(w, h);
    SwapBuffers(hdc);
    DrawArtifactHUD(hdc);
}

// ===== CPU 渲染质感 HUD =====
static void DrawArtifactHUD(HDC hdc) {
    wchar_t buf[256];
    bool loaded = !g_artifactCode.empty();
    bool active = g_ditherStrength > 0.5f;

    HFONT f = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Consolas");
    HFONT of = (HFONT)SelectObject(hdc, f);
    SetBkMode(hdc, TRANSPARENT);

    // 行 1: CpuArtifact 状态 + 降分辨率
    _snwprintf(buf, 256, L"CpuArtifact: %s | 质感: %s | 画质: %s",
        loaded  ? L"已加载" : L"未加载(!)",
        active  ? L"ON" : L"OFF",
        L"⅔");
    RECT tr = {10, 6, 800, 24};
    SetTextColor(hdc, loaded ? (active ? RGB(120, 255, 120) : RGB(180, 180, 180))
                             : RGB(255, 100, 100));
    DrawTextW(hdc, buf, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // 行 2: 过渡状态（仅在过渡期间显示）
    if (g_plState == PL_BLUR && g_plOldEffIdx >= 0 && g_plEffIdx[0] >= 0) {
        const wchar_t* oldName = g_effects[g_plOldEffIdx].name.c_str();
        const wchar_t* newName = g_effects[g_plEffIdx[0]].name.c_str();
        int pct = (int)(g_plBlurT * 100.0f);
        if (pct > 100) pct = 100;
        _snwprintf(buf, 256, L"  → 过渡: %s → %s  [%d%%]",
            oldName, newName, pct);
        RECT tr2 = {10, 26, 800, 46};
        SetTextColor(hdc, RGB(255, 220, 100));  // 金色提示
        DrawTextW(hdc, buf, -1, &tr2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else if (g_plState == PL_WAITBEAT) {
        RECT tr2 = {10, 26, 800, 46};
        SetTextColor(hdc, RGB(255, 200, 100));
        DrawTextW(hdc, L"  → 等待鼓点切换...", -1, &tr2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    SelectObject(hdc, of);
    DeleteObject(f);
}

// ===== Shader 加载 =====
static std::string LoadFile(const wchar_t* path) {
    FILE* f = _wfopen(path, L"rb"); if (!f) return "";
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    std::string s(len, 0); fread(&s[0], 1, len, f); fclose(f); return s;
}

static void LoadEffects() {
    const wchar_t* names[7] = { L"Psychedelic", L"GlitchForge", L"Verdigris", L"Azurite", L"Azurite_c", L"MTH", L"MTH_C" };
    wchar_t base[MAX_PATH]; GetModuleFileNameW(nullptr, base, MAX_PATH);
    wchar_t* p = wcsrchr(base, L'\\'); if (p) *p = L'\0'; // 去 exe 名, "D:\...\out"
    wchar_t* p2 = wcsrchr(base, L'\\');
    if (p2 && _wcsicmp(p2+1, L"out") == 0) *p2 = L'\0'; // 去 out, "D:\...\ALL VSLZATION_GPU"
    _snwprintf(g_shaderDir, MAX_PATH, L"%s\\VisualEffects\\shaders\\", base);

    // 加载 CPU 渲染副产品模拟代码（CpuArtifact.glsl）
    if (g_artifactCode.empty()) {
        wchar_t apath[MAX_PATH];
        _snwprintf(apath, MAX_PATH, L"%s\\VisualEffects\\simulation effect VST\\CpuArtifact.glsl", base);
        g_artifactCode = LoadFile(apath);
    }

    g_effectCount = 0;
    for (int i = 0; i < 7; ++i) {
        wchar_t path[MAX_PATH];
        _snwprintf(path, MAX_PATH, L"%s%s.frag", g_shaderDir, names[i]);
        std::string src = LoadFile(path);
        if (src.empty()) {
            // 回退：exe 同目录
            _snwprintf(path, MAX_PATH, L"%s%s.frag", base, names[i]);
            src = LoadFile(path);
        }
        if (src.empty()) {
            wchar_t msg[256];
            _snwprintf(msg, 256, L"Shader file not found:\n%s.frag", names[i]);
            MessageBoxW(nullptr, msg, L"GPU Shader Missing", MB_OK);
            continue;
        }
        // 拼接 CpuArtifact.glsl（去掉原 #version 行，由 artifact 文件提供）
        if (!g_artifactCode.empty()) {
            auto vp = src.find("#version");
            if (vp != std::string::npos) {
                auto ve = src.find('\n', vp);
                if (ve != std::string::npos) src.erase(vp, ve - vp + 1);
            }
            src = g_artifactCode + "\n" + src;
        }
        if (g_effects[i].Load(src.c_str())) {
            g_effects[i].name = names[i];
            if (wcscmp(names[i], L"Psychedelic") == 0) g_effects[i].proj = 1;
            if (wcscmp(names[i], L"Azurite_c") == 0) g_effects[i].form = g_effects[i].targetForm = 4;
            if (wcscmp(names[i], L"MTH_C") == 0) { g_effects[i].form = g_effects[i].targetForm = 3; g_effects[i].formCount = 5; } // 初始形态：非对称星形，共 5 形态
            g_effectCount++;
        } else {
            wchar_t msg[256];
            _snwprintf(msg, 256, L"Shader compile/link failed:\n%s.frag", names[i]);
            MessageBoxW(nullptr, msg, L"GPU Shader Error", MB_OK);
            g_effects[i].program = 0;
        }
    }
    if (g_effectCount == 0) {
        wchar_t msg[1024];
        _snwprintf(msg, 1024,
            L"No shaders loaded.\nShader dir: %s\nExists: %s",
            g_shaderDir,
            (GetFileAttributesW(g_shaderDir)!=INVALID_FILE_ATTRIBUTES?L"yes":L"no"));
        MessageBoxW(nullptr, msg, L"GPU Load Error", MB_OK);
    }
}

// ===== 播放链子窗口（OpenGL） =====
#define TIMER_PLAYER 3
static HWND g_hPlayer = nullptr;
static HGLRC g_hPlayerRC = nullptr;
static bool g_playerMode = false;

LRESULT CALLBACK PlayerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HDC hdc = GetDC(hwnd);
        if (!g_cpuMode) {
        PIXELFORMATDESCRIPTOR pfd = {sizeof(pfd),1,PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER,
            PFD_TYPE_RGBA,32,0,0,0,0,0,0,0,0,0,0,0,0,0,24,8,0,PFD_MAIN_PLANE,0,0,0,0};
        int pf = ChoosePixelFormat(hdc, &pfd);
        if (!pf || !SetPixelFormat(hdc, pf, &pfd)) {
            MessageBoxW(hwnd, L"SetPixelFormat failed!", L"GL Error", MB_OK);
            DestroyWindow(hwnd); return 0;
        }
        g_hPlayerRC = wglCreateContext(hdc);
        if (!g_hPlayerRC) {
            MessageBoxW(hwnd, L"wglCreateContext failed!", L"GL Error", MB_OK);
            DestroyWindow(hwnd); return 0;
        }
        wglMakeCurrent(hdc, g_hPlayerRC);
        LoadGL();
        LoadEffects();
        if (g_effectCount == 0) {
            MessageBoxW(hwnd, L"Shader load failed in player context!", L"GPU Error", MB_OK);
            DestroyWindow(hwnd); return 0;
        }
        // 编译混合 shader
        {   // 混合 shader
            g_blendProg = 0;  // 先清零，防止用临时上下文的无效句柄
            const char* vs = "#version 330 core\nlayout(location=0)in vec2 p;layout(location=1)in vec2 u;out vec2 vUV;void main(){vUV=u;gl_Position=vec4(p,0,1);}";
            const char* fs = "#version 330 core\n"
                "in vec2 vUV;out vec4 c;"
                "uniform sampler2D uBase,uOverlay;uniform int uMode;uniform float uOpacity;"
                "float ch(float a,float b,int m){"
                "if(m==1)return max(a,b);"
                "if(m==2)return 1.0-(1.0-a)*(1.0-b);"
                "if(m==3)return a<0.5?2.0*a*b:1.0-2.0*(1.0-a)*(1.0-b);"
                "if(m==4)return b<0.5?2.0*a*b:1.0-2.0*(1.0-a)*(1.0-b);"
                "if(m==5){if(b<0.5)return a-(1.0-2.0*b)*a*(1.0-a);else{float g=a<=0.25?((16.0*a-12.0)*a+4.0)*a:sqrt(a);return a+(2.0*b-1.0)*(g-a);}}"
                "if(m==6)return a+b-1.0;"
                "if(m==7)return a*b;"
                "return b;}"
                "void main(){"
                "vec4 a=texture(uBase,vUV),b=texture(uOverlay,vUV);"
                "float mr=ch(a.r,b.r,uMode),mg=ch(a.g,b.g,uMode),mb=ch(a.b,b.b,uMode);"
                "vec3 bl=clamp(vec3(mr,mg,mb),0.0,1.0);float eb=b.a*uOpacity;float onlyA=a.a*(1.0-eb);float onlyB=eb*(1.0-a.a);float both=a.a*eb;vec3 res=a.rgb*onlyA+b.rgb*onlyB+bl*both;float tot=onlyA+onlyB+both;c=vec4(a.rgb*(1.0-tot)+res,1.0);}";
            auto c3=[](GLenum t,const char* s)->GLuint{
                GLuint sh=glCreateShader(t);glShaderSource(sh,1,&s,0);glCompileShader(sh);
                GLint ok;glGetShaderiv(sh,GL_COMPILE_STATUS,&ok);
                if(!ok){char log[512];glGetShaderInfoLog(sh,512,0,log);MessageBoxA(0,log,"Blend Shader Error",MB_OK);glDeleteShader(sh);return 0;}
                return sh;};
            GLuint v3=c3(GL_VERTEX_SHADER,vs),f3=c3(GL_FRAGMENT_SHADER,fs);
            if(v3&&f3){g_blendProg=glCreateProgram();glAttachShader(g_blendProg,v3);glAttachShader(g_blendProg,f3);glLinkProgram(g_blendProg);glDeleteShader(v3);glDeleteShader(f3);
                GLint linkOk;glGetProgramiv(g_blendProg,GL_LINK_STATUS,&linkOk);
                if(!linkOk){char log[512];glGetProgramInfoLog(g_blendProg,512,0,log);MessageBoxA(0,log,"Blend Link Error",MB_OK);g_blendProg=0;}}
        }
        InitVAO();
        } // if (!g_cpuMode) — OpenGL 初始化结束
        ReleaseDC(hwnd, hdc);
        SetTimer(hwnd, TIMER_PLAYER, 16, nullptr);
        return 0;
    }
    case WM_TIMER:
        if (g_cpuMode) {
            if (g_cpuPlayerMode && g_cpuPlaylist.GetState() != CpuPlaylistManager::STOPPED)
                InvalidateRect(hwnd, nullptr, FALSE);
        } else {
            if (g_plState != PL_STOPPED) InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;
        if (g_cpuMode) {
            // CPU GDI 渲染路径
            if (w > 0 && h > 0 && g_cpuPlayerMode) {
                g_cpuPlaylist.Render(hdc, w, h);
            }
        } else {
            if (!g_hPlayerRC) { EndPaint(hwnd, &ps); return 0; }
            wglMakeCurrent(hdc, g_hPlayerRC);
            if (w > 0 && h > 0) {
                glViewport(0, 0, w, h);
                glClearColor(g_plBlackBg?0.0f:1.0f, g_plBlackBg?0.0f:1.0f, g_plBlackBg?0.0f:1.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                PLRender(hdc, w, h);
            }
            wglMakeCurrent(nullptr, nullptr);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
        if (wParam == 'B') { if(g_cpuMode){g_cpuPlaylist.ToggleBg();}else{g_plBlackBg=!g_plBlackBg;} InvalidateRect(hwnd, nullptr, FALSE); return 0; }
        if (wParam == 'D') { if(!g_cpuMode){g_ditherStrength=(g_ditherStrength>0.5f)?0.0f:1.0f;} InvalidateRect(hwnd, nullptr, FALSE); return 0; }
        break;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_PLAYER); g_hPlayer = nullptr;
        if (g_hPlayerRC) { wglMakeCurrent(nullptr, nullptr); wglDeleteContext(g_hPlayerRC); g_hPlayerRC = nullptr; }
        PLStop(); g_cpuPlaylist.Stop(); g_cpuPlayerMode = false; return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ===== 主窗口 =====
#define IDC_LIST          1001
#define IDC_BTN_LAUNCH    1002
#define IDC_BTN_SCAN      1003
#define IDC_BTN_EXIT      1004
#define IDC_STATUS        1005
#define IDC_EDIT_BPM      1006
#define IDC_PLAYLIST      1101
#define IDC_BTN_ADD       1102
#define IDC_BTN_REMOVE    1103
#define IDC_BTN_UP        1104
#define IDC_BTN_DOWN      1105
#define IDC_BTN_CLEAR     1111
#define IDC_EDIT_DURATION 1106
#define IDC_BTN_PLAY      1107
#define IDC_BTN_STOP      1108
#define IDC_BTN_BG        1109
#define IDC_BTN_TAP       1012
#define IDC_TOGGLE_GPU    1015
#define IDC_TOGGLE_CPU    1016
#define IDC_COMBO_BLEND   1110
#define IDC_TRACK_SPEED   1013
#define IDC_STATIC_SPEED  1014
#define IDC_PRESET_BASE   1120
#define IDC_TRACK_OPACITY 1131
#define IDC_STATIC_OPACITY 1132
#define TIMER_MAIN 1
#define TIMER_INIT 2
#define TIMER_CLEAR_HOLD 3
#define CLEAR_HOLD_MS 1000

// 顶部菜单栏 ID
#define IDM_SETTINGS   2000
#define IDM_MODE_GPU   2001

static void ShowSettingsDialog(HWND parent);  // 设置对话框（定义在 WinMain 前）
#define IDM_MODE_CPU   2002
#define IDM_SPEED_50   2101
#define IDM_SPEED_75   2102
#define IDM_SPEED_100  2103
#define IDM_SPEED_150  2104
#define IDM_SPEED_200  2105
#define IDM_SPEED_300  2106
#define IDM_BPM_60     2201
#define IDM_BPM_90     2202
#define IDM_BPM_120    2203
#define IDM_BPM_140    2204
#define IDM_BPM_180    2205
#define IDM_BPM_240    2206

static HWND g_hwnd=nullptr, g_hEditBpm=nullptr, g_hList=nullptr, g_hStatus=nullptr;
static HWND g_hPlaylist=nullptr, g_hEditDur=nullptr, g_hBtnPlay=nullptr, g_hBtnStop=nullptr, g_hBtnBg=nullptr;
static HWND g_hTrackSpeed=nullptr, g_hStaticSpeed=nullptr;
static HWND g_hBtnClear=nullptr;
static bool g_holdingClear=false;
static bool g_holdCompleted=false;
static DWORD g_holdStartTick=0;
static POINT g_holdCursorPt={0,0};
static WNDPROC g_btnClearOldProc=nullptr;
static HMENU g_menuMode=nullptr, g_menuSpeed=nullptr, g_menuBpm=nullptr;
static AudioCapture g_audio;
static BPMDetector g_drums(44100);
static SharedMem g_sharedMem;
static float g_bpm = 120.0f, g_beatPhase = 0.0f;
static DWORD g_beatCount = 0;
static int g_silentFrames = 0;

static void SetBPM(float bpm) { g_bpm = bpm; g_beatPhase = 0.0f; if (g_hEditBpm) { wchar_t s[16]; _snwprintf(s, 16, L"%.0f", g_bpm); SetWindowTextW(g_hEditBpm, s); } }

static void SetSpeed(float s) {
    g_userSpeed = s;
    g_cpuPlaylist.SetSpeed(s);
    if (g_hTrackSpeed) SendMessageW(g_hTrackSpeed, TBM_SETPOS, TRUE, (LPARAM)(int)(s*100.0f));
    if (g_hStaticSpeed) { wchar_t buf[16]; _snwprintf(buf, 16, L"%.1fx", s); SetWindowTextW(g_hStaticSpeed, buf); }
    UINT id = 0;
    if (s == 0.5f) id = IDM_SPEED_50;
    else if (s == 0.75f) id = IDM_SPEED_75;
    else if (s == 1.0f) id = IDM_SPEED_100;
    else if (s == 1.5f) id = IDM_SPEED_150;
    else if (s == 2.0f) id = IDM_SPEED_200;
    else if (s == 3.0f) id = IDM_SPEED_300;
    if (id && g_menuSpeed) CheckMenuRadioItem(g_menuSpeed, IDM_SPEED_50, IDM_SPEED_300, id, MF_BYCOMMAND);
}

static void RefreshPlaylistUI() {
    ListBox_ResetContent(g_hPlaylist);
    const wchar_t* bmNames[] = {L"无",L"变亮",L"滤色",L"叠加",L"强光",L"柔光",L"线性加深",L"正片叠底"};
    for (size_t i = 0; i < g_playlist.size(); ++i) {
        wchar_t s[300]; auto& it = g_playlist[i];
        if (!it.name2.empty())
            _snwprintf(s, 300, L"%d. %s + %s  [%s|%.0f%%]  [%ds]",
                (int)i+1, it.name.c_str(), it.name2.c_str(),
                bmNames[it.blendMode], it.opacity*100.0f, it.durationSec);
        else _snwprintf(s, 300, L"%d. %s  [%ds]", (int)i+1, it.name.c_str(), it.durationSec);
        ListBox_AddString(g_hPlaylist, s);
    }
}

static void PlaylistAdd() {
    int sel = (int)SendMessageW(g_hList, LB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= g_effectCount) return;
    wchar_t buf[16]; GetWindowTextW(g_hEditDur, buf, 16);
    int dur = _wtoi(buf); if (dur < 1) dur = 1;
    PlaylistItem it; it.name = g_effects[sel].name; it.durationSec = dur;
    g_playlist.push_back(it);
    RefreshPlaylistUI();
    ListBox_SetCurSel(g_hPlaylist, (int)g_playlist.size()-1);
}

static void PlaylistRemove() {
    int sel = (int)SendMessageW(g_hPlaylist, LB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int)g_playlist.size()) return;
    g_playlist.erase(g_playlist.begin() + sel);
    RefreshPlaylistUI();
}

static void PlaylistMoveUp() {
    int sel = (int)SendMessageW(g_hPlaylist, LB_GETCURSEL, 0, 0);
    if (sel > 0 && sel < (int)g_playlist.size()) { std::swap(g_playlist[sel], g_playlist[sel-1]); RefreshPlaylistUI(); ListBox_SetCurSel(g_hPlaylist, sel-1); }
}
static void PlaylistMoveDown() {
    int sel = (int)SendMessageW(g_hPlaylist, LB_GETCURSEL, 0, 0);
    if (sel >= 0 && sel < (int)g_playlist.size()-1) { std::swap(g_playlist[sel], g_playlist[sel+1]); RefreshPlaylistUI(); ListBox_SetCurSel(g_hPlaylist, sel+1); }
}

static void PlaylistStart() {
    if (g_playlist.empty()) return;
    if (g_cpuMode) {
        // CPU GDI 模式：使用 CpuPlaylistManager
        g_cpuPlaylist.LoadItems(g_playlist);
        g_cpuPlaylist.Start();
        g_cpuPlayerMode = true;
    } else {
        // GPU 模式：现有逻辑
        PLStart(); g_playerMode = true;
    }
    if (!g_hPlayer) {
        static bool reg = false;
        if (!reg) { WNDCLASSEXW wc={}; wc.cbSize=sizeof(wc); wc.style=CS_HREDRAW|CS_VREDRAW|CS_OWNDC; wc.lpfnWndProc=PlayerWndProc; wc.hInstance=GetModuleHandle(nullptr); wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH); wc.lpszClassName=L"VizPlayerGPU"; RegisterClassExW(&wc); reg=true; }
        const wchar_t* title = g_cpuMode ? L"播放链 [CPU GDI]" : L"播放链 [GPU]";
        g_hPlayer = CreateWindowExW(0, L"VizPlayerGPU", title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,CW_USEDEFAULT,PLAYER_DEFAULT_W,PLAYER_DEFAULT_H, nullptr,nullptr,GetModuleHandle(nullptr),nullptr);
        // 置顶：设 WS_EX_TOPMOST 标志，播放链窗口保持在其他窗口之上
        SetWindowPos(g_hPlayer, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        ShowWindow(g_hPlayer, SW_SHOW); UpdateWindow(g_hPlayer);
    }
    EnableWindow(g_hBtnPlay, FALSE); EnableWindow(g_hBtnStop, TRUE);
    SetWindowTextW(g_hBtnBg, (g_cpuMode ? g_cpuPlaylist.IsBlackBg() : g_plBlackBg) ? L"Bg:黑" : L"Bg:白");
    InvalidateRect(g_hPlayer, nullptr, FALSE);
}

static void PlaylistStop() {
    PLStop(); g_playerMode = false;
    g_cpuPlaylist.Stop(); g_cpuPlayerMode = false;
    if (g_hPlayer) { DestroyWindow(g_hPlayer); g_hPlayer = nullptr; }
    EnableWindow(g_hBtnPlay, TRUE); EnableWindow(g_hBtnStop, FALSE);
}

static void ClearPlaylist() {
    PlaylistStop();
    g_playlist.clear();
    RefreshPlaylistUI();
    if (g_hStatus) SetWindowTextW(g_hStatus, L"  已清空播放链");
}

static void DrawClearRing(HDC hdc, POINT c) {
    DWORD elapsed = GetTickCount() - g_holdStartTick;
    float progress = (float)elapsed / (float)CLEAR_HOLD_MS;
    if (progress < 0) progress = 0; if (progress > 1) progress = 1;
    int r = 13;
    int cx = c.x + 22, cy = c.y - 22;
    HPEN gp = CreatePen(PS_SOLID, 3, RGB(150,150,150));
    HGDIOBJ op = SelectObject(hdc, gp);
    HGDIOBJ ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Ellipse(hdc, cx-r, cy-r, cx+r, cy+r);
    double ang = progress * 2.0 * 3.14159265358979;
    int x1 = cx, y1 = cy - r;
    int x2 = cx + (int)(r * sin(ang));
    int y2 = cy - (int)(r * cos(ang));
    HBRUSH cb = CreateSolidBrush(RGB(80, 200, 120));
    SelectObject(hdc, GetStockObject(NULL_PEN));
    SelectObject(hdc, cb);
    Pie(hdc, cx-r, cy-r, cx+r, cy+r, x1, y1, x2, y2);
    DeleteObject(cb);
    SelectObject(hdc, op);
    SelectObject(hdc, ob);
    DeleteObject(gp);
}

static LRESULT CALLBACK ClearBtnProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_LBUTTONDOWN:
        g_holdingClear = true;
        g_holdCompleted = false;
        g_holdStartTick = GetTickCount();
        GetCursorPos(&g_holdCursorPt);
        SetTimer(g_hwnd, TIMER_CLEAR_HOLD, 16, nullptr);
        InvalidateRect(g_hwnd, nullptr, TRUE);
        break;
    case WM_LBUTTONUP:
        if (g_holdCompleted) {
            g_holdCompleted = false;
            g_holdingClear = false;
            return 0;  // 已清空，吞掉本次点击
        }
        if (g_holdingClear) {
            g_holdingClear = false;
            KillTimer(g_hwnd, TIMER_CLEAR_HOLD);
            InvalidateRect(g_hwnd, nullptr, TRUE);
        }
        break;
    case WM_CAPTURECHANGED:
        if (g_holdingClear && !g_holdCompleted) {
            g_holdingClear = false;
            KillTimer(g_hwnd, TIMER_CLEAR_HOLD);
            InvalidateRect(g_hwnd, nullptr, TRUE);
        }
        break;
    }
    return CallWindowProcW(g_btnClearOldProc, hwnd, msg, wParam, lParam);
}

// 预设存取
static void PresetsSaveToFile() {
    wchar_t path[MAX_PATH]; GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* p = wcsrchr(path, L'\\'); if (p) *(p+1) = L'\0'; wcscat(path, L"presets.dat");
    FILE* f = _wfopen(path, L"w"); if (!f) return;
    for (int s=0;s<5;++s){if(g_presets[s].empty())continue;fwprintf(f,L"SLOT %d\n",s);for(auto&it:g_presets[s])fwprintf(f,L"%s|%s|%d|%.3f|%d\n",it.name.c_str(),it.name2.c_str(),(int)it.blendMode,it.opacity,it.durationSec);}
    fclose(f);
}
static void PresetsLoadFromFile() {
    wchar_t path[MAX_PATH]; GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* p = wcsrchr(path, L'\\'); if (p) *(p+1) = L'\0'; wcscat(path, L"presets.dat");
    FILE* f = _wfopen(path, L"r"); if (!f) return;
    wchar_t line[512]; int cs=-1;
    while(fgetws(line,512,f)){if(wcsncmp(line,L"SLOT ",5)==0){cs=_wtoi(line+5);if(cs<0)cs=0;if(cs>4)cs=4;g_presets[cs].clear();}else if(cs>=0){PlaylistItem it;wchar_t*ctx=nullptr;wchar_t*tok=wcstok(line,L"|\n",&ctx);if(tok)it.name=tok;tok=wcstok(nullptr,L"|\n",&ctx);if(tok)it.name2=tok;tok=wcstok(nullptr,L"|\n",&ctx);if(tok)it.blendMode=(BlendMode)_wtoi(tok);tok=wcstok(nullptr,L"|\n",&ctx);if(tok)it.opacity=(float)_wtof(tok);tok=wcstok(nullptr,L"|\n",&ctx);if(tok)it.durationSec=_wtoi(tok);if(!it.name.empty()&&it.durationSec>0)g_presets[cs].push_back(it);}}
    fclose(f);
}
static void PresetSave(int s) { g_presets[s]=g_playlist; PresetsSaveToFile(); wchar_t st[64];_snwprintf(st,64,L"  已存入预设%d（%d项）",s+1,(int)g_presets[s].size());SetWindowTextW(g_hStatus,st); }
static void PresetLoad(int s) { if(g_presets[s].empty()){SetWindowTextW(g_hStatus,L"  预设为空");return;}for(auto&it:g_presets[s])g_playlist.push_back(it);RefreshPlaylistUI();wchar_t st[64];_snwprintf(st,64,L"  已追加预设%d",s+1);SetWindowTextW(g_hStatus,st); }
static void PresetDelete(int s) { g_presets[s].clear(); PresetsSaveToFile(); }
static void UpdatePresetButtons() { for(int i=0;i<5;++i){int n=(int)g_presets[i].size();wchar_t lbl[32];_snwprintf(lbl,32,L"%d%s",i+1,n>0?L" ✓":L" -");HWND btn=GetDlgItem(g_hwnd,IDC_PRESET_BASE+i);if(btn)SetWindowTextW(btn,lbl);} }

static void UpdateBeat() {
    float dt = 0.016f; g_beatPhase += dt / (60.0f / g_bpm);
    if (g_beatPhase >= 1.0f) g_beatPhase -= 1.0f;
    std::vector<float> buf(4096); int n = 0;
    g_audio.Capture(buf.data(), 4096, n);
    bool hit = false;
    if (n > 0) { g_silentFrames = 0; hit = g_drums.Process(buf.data(), n); }
    else { g_silentFrames++; if (g_silentFrames > 180) { g_silentFrames = 0; g_audio.Shutdown(); if (g_audio.Initialize()) g_drums = BPMDetector(g_audio.GetSampleRate()); } }
    if (hit) g_beatCount++;
    g_sharedMem.Write(g_bpm, g_beatPhase, g_beatCount, hit);
    if (g_playerMode) PLUpdate(dt, g_bpm, g_beatPhase, hit);
    if (g_cpuPlayerMode) g_cpuPlaylist.Update(dt, g_bpm, g_beatPhase, hit);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hwnd = hwnd; g_sharedMem.Create();
        HFONT f = CreateFontW(16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei");

        // 顶部菜单栏（设置 → 打开设置对话框）
        {
            HMENU bar=CreateMenu();
            AppendMenuW(bar,MF_STRING,IDM_SETTINGS,L"设置");
            SetMenu(hwnd,bar);
        }

        int panelH=10;
        CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_ETCHEDHORZ,10,panelH,880,2,hwnd,nullptr,nullptr,nullptr);

        CreateWindowW(L"STATIC",L"可用的可视化效果",WS_CHILD|WS_VISIBLE|SS_LEFT,20,panelH+8,400,20,hwnd,nullptr,nullptr,nullptr);
        g_hList=CreateWindowW(L"LISTBOX",nullptr,WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|LBS_NOTIFY|LBS_HASSTRINGS,20,panelH+30,340,260,hwnd,(HMENU)IDC_LIST,nullptr,nullptr);
        SendMessage(g_hList,WM_SETFONT,(WPARAM)f,TRUE);
        for(int i=0;i<g_effectCount;++i)ListBox_AddString(g_hList,g_effects[i].name.c_str());

        CreateWindowW(L"STATIC",L"播放链",WS_CHILD|WS_VISIBLE|SS_LEFT,380,panelH+8,400,20,hwnd,nullptr,nullptr,nullptr);
        g_hPlaylist=CreateWindowW(L"LISTBOX",nullptr,WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|LBS_NOTIFY|LBS_HASSTRINGS,380,panelH+30,340,190,hwnd,(HMENU)IDC_PLAYLIST,nullptr,nullptr);
        SendMessage(g_hPlaylist,WM_SETFONT,(WPARAM)f,TRUE);

        int py=panelH+230;
        CreateWindowW(L"STATIC",L"时长(秒):",WS_CHILD|WS_VISIBLE|SS_RIGHT,380,py+4,60,22,hwnd,nullptr,nullptr,nullptr);
        g_hEditDur=CreateWindowW(L"EDIT",L"30",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER,445,py+2,45,24,hwnd,(HMENU)IDC_EDIT_DURATION,nullptr,nullptr);
        SendMessage(g_hEditDur,WM_SETFONT,(WPARAM)f,TRUE);SendMessage(g_hEditDur,EM_SETLIMITTEXT,3,0);

        CreateWindowW(L"BUTTON",L"加入→",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,380,py+30,70,28,hwnd,(HMENU)IDC_BTN_ADD,nullptr,nullptr);
        g_hBtnClear=CreateWindowW(L"BUTTON",L"←移除",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,455,py+30,70,28,hwnd,(HMENU)IDC_BTN_REMOVE,nullptr,nullptr);
        SendMessage(g_hBtnClear,WM_SETFONT,(WPARAM)f,TRUE);
        g_btnClearOldProc=(WNDPROC)SetWindowLongPtrW(g_hBtnClear,GWLP_WNDPROC,(LONG_PTR)ClearBtnProc);
        CreateWindowW(L"BUTTON",L"↑",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,535,py+30,35,28,hwnd,(HMENU)IDC_BTN_UP,nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"↓",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,575,py+30,35,28,hwnd,(HMENU)IDC_BTN_DOWN,nullptr,nullptr);

        g_hBtnPlay=CreateWindowW(L"BUTTON",L"▶ 播放链",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,380,py+66,100,32,hwnd,(HMENU)IDC_BTN_PLAY,nullptr,nullptr);
        SendMessage(g_hBtnPlay,WM_SETFONT,(WPARAM)f,TRUE);
        g_hBtnStop=CreateWindowW(L"BUTTON",L"⏹ 停止",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,490,py+66,100,32,hwnd,(HMENU)IDC_BTN_STOP,nullptr,nullptr);
        SendMessage(g_hBtnStop,WM_SETFONT,(WPARAM)f,TRUE);EnableWindow(g_hBtnStop,FALSE);

        // 混合模式
        int blendY=py+108;
        CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_ETCHEDHORZ,380,blendY,370,2,hwnd,nullptr,nullptr,nullptr);
        CreateWindowW(L"STATIC",L"混合模式:",WS_CHILD|WS_VISIBLE|SS_RIGHT,380,blendY+8,70,22,hwnd,nullptr,nullptr,nullptr);
        HWND hCombo=CreateWindowW(L"COMBOBOX",nullptr,WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,455,blendY+6,140,200,hwnd,(HMENU)IDC_COMBO_BLEND,nullptr,nullptr);
        SendMessageW(hCombo,WM_SETFONT,(WPARAM)f,TRUE);
        const wchar_t* bms[]={L"无混合",L"变亮",L"滤色",L"叠加",L"强光",L"柔光",L"线性加深",L"正片叠底"};
        for(int i=0;i<8;++i)SendMessageW(hCombo,CB_ADDSTRING,0,(LPARAM)bms[i]);
        SendMessageW(hCombo,CB_SETCURSEL,0,0);
        CreateWindowW(L"STATIC",L"不透明:",WS_CHILD|WS_VISIBLE|SS_RIGHT,380,blendY+34,70,22,hwnd,nullptr,nullptr,nullptr);
        HWND hOp=CreateWindowW(TRACKBAR_CLASSW,nullptr,WS_CHILD|WS_VISIBLE|TBS_HORZ|TBS_NOTICKS,455,blendY+32,130,28,hwnd,(HMENU)IDC_TRACK_OPACITY,nullptr,nullptr);
        SendMessageW(hOp,TBM_SETRANGE,TRUE,MAKELONG(0,100));SendMessageW(hOp,TBM_SETPOS,TRUE,100);
        CreateWindowW(L"STATIC",L"100%",WS_CHILD|WS_VISIBLE|SS_LEFT,590,blendY+34,50,22,hwnd,(HMENU)IDC_STATIC_OPACITY,nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"设为上层",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,380,blendY+62,80,28,hwnd,(HMENU)1133,nullptr,nullptr);
        CreateWindowW(L"BUTTON",L"清除上层",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,470,blendY+62,80,28,hwnd,(HMENU)1134,nullptr,nullptr);

        // 预设按钮
        int presetX=730;
        CreateWindowW(L"STATIC",L"预设",WS_CHILD|WS_VISIBLE|SS_CENTER,presetX,panelH+8,100,18,hwnd,nullptr,nullptr,nullptr);
        for(int pi=0;pi<5;++pi){wchar_t lbl[32];_snwprintf(lbl,32,L"%d -",pi+1);CreateWindowW(L"BUTTON",lbl,WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,presetX,panelH+28+pi*48,100,40,hwnd,(HMENU)(IDC_PRESET_BASE+pi),nullptr,nullptr);}

        g_hStatus=CreateWindowW(L"STATIC",L"  正在加载 OpenGL shader...",WS_CHILD|WS_VISIBLE|SS_LEFT|SS_SUNKEN,0,panelH+470,900,22,hwnd,(HMENU)IDC_STATUS,nullptr,nullptr);

        DeleteObject(f);
        SetTimer(hwnd, TIMER_INIT, 100, nullptr);
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        DWORD cid=GetDlgCtrlID((HWND)lParam);
        if(cid==IDC_STATUS)return(LRESULT)GetStockObject(NULL_BRUSH);
        HDC hdcStatic=(HDC)wParam;SetBkMode(hdcStatic,OPAQUE);SetBkColor(hdcStatic,RGB(20,20,35));SetTextColor(hdcStatic,RGB(220,220,235));
        static HBRUSH br=CreateSolidBrush(RGB(20,20,35));return(LRESULT)br;
    }

    case WM_TIMER:
        if(wParam==TIMER_INIT){KillTimer(hwnd,TIMER_INIT);
            g_audio.Initialize();if(g_audio.IsInitialized())g_drums=BPMDetector(g_audio.GetSampleRate());
            PresetsLoadFromFile();UpdatePresetButtons();SetTimer(hwnd,TIMER_MAIN,16,nullptr);
            wchar_t st[128];_snwprintf(st,128,L"  就绪 — %d 个效果 | BPM: %.0f",g_effectCount,g_bpm);SetWindowTextW(g_hStatus,st);
            return 0;}
        if(wParam==TIMER_MAIN){UpdateBeat();return 0;}
        if(wParam==TIMER_CLEAR_HOLD && g_holdingClear){
            if(GetTickCount()-g_holdStartTick >= CLEAR_HOLD_MS){
                g_holdingClear=false; g_holdCompleted=true; KillTimer(hwnd,TIMER_CLEAR_HOLD);
                ClearPlaylist(); InvalidateRect(hwnd,nullptr,TRUE);
            } else {
                InvalidateRect(hwnd,nullptr,TRUE);
            }
            return 0;
        }break;

    case WM_PAINT:{PAINTSTRUCT ps;HDC hdc=BeginPaint(hwnd,&ps);DefWindowProc(hwnd,msg,wParam,lParam);if(g_holdingClear){POINT cp=g_holdCursorPt;ScreenToClient(hwnd,&cp);DrawClearRing(hdc,cp);}EndPaint(hwnd,&ps);return 0;}

    case WM_COMMAND: {
        WORD id=LOWORD(wParam);
        switch(id){
        case IDC_BTN_PLAY: PlaylistStart(); break;
        case IDC_BTN_STOP: PlaylistStop(); break;
        case IDC_BTN_ADD: PlaylistAdd(); break;
        case IDC_BTN_REMOVE: PlaylistRemove(); break;
        case IDC_BTN_UP: PlaylistMoveUp(); break;
        case IDC_BTN_DOWN: PlaylistMoveDown(); break;
        case IDC_COMBO_BLEND: if(HIWORD(wParam)==CBN_SELCHANGE){g_blendMode=(int)SendMessageW(GetDlgItem(g_hwnd,IDC_COMBO_BLEND),CB_GETCURSEL,0,0);int ps=(int)SendMessageW(g_hPlaylist,LB_GETCURSEL,0,0);if(ps>=0&&ps<(int)g_playlist.size()){g_playlist[ps].blendMode=(BlendMode)g_blendMode;RefreshPlaylistUI();ListBox_SetCurSel(g_hPlaylist,ps);}} break;
        case 1133: {int effSel=(int)SendMessageW(g_hList,LB_GETCURSEL,0,0);int plSel=(int)SendMessageW(g_hPlaylist,LB_GETCURSEL,0,0);
            if(effSel>=0&&effSel<g_effectCount&&plSel>=0&&plSel<(int)g_playlist.size()){
                g_playlist[plSel].name2=g_effects[effSel].name;
                g_playlist[plSel].blendMode=(BlendMode)g_blendMode;
                g_playlist[plSel].opacity=g_blendOpacity;
                if(g_blendMode==0){g_blendMode=2;g_playlist[plSel].blendMode=BM_Screen;SendMessageW(GetDlgItem(g_hwnd,IDC_COMBO_BLEND),CB_SETCURSEL,2,0);}
                RefreshPlaylistUI();ListBox_SetCurSel(g_hPlaylist,plSel);
                SetWindowTextW(g_hStatus,L"  已设上层效果");}
            else SetWindowTextW(g_hStatus,L"  请先选择左侧效果和播放链条目");}break;
        case 1134: {int plSel=(int)SendMessageW(g_hPlaylist,LB_GETCURSEL,0,0);
            if(plSel>=0&&plSel<(int)g_playlist.size()){g_playlist[plSel].name2.clear();RefreshPlaylistUI();ListBox_SetCurSel(g_hPlaylist,plSel);}
            g_blendMode=0;SendMessageW(GetDlgItem(g_hwnd,IDC_COMBO_BLEND),CB_SETCURSEL,0,0);SetWindowTextW(g_hStatus,L"  已清除上层");}break;
        case IDC_BTN_EXIT: DestroyWindow(hwnd); break;
        case IDC_EDIT_BPM: if(HIWORD(wParam)==EN_KILLFOCUS){wchar_t buf[16];GetWindowTextW(g_hEditBpm,buf,16);int v=_wtoi(buf);if(v>0)SetBPM((float)v);}break;
        case IDC_BTN_TAP:{static float lastTap=0;static int taps=0;float now=(float)GetTickCount()/1000.0f;if(taps>0){float dt=now-lastTap;if(dt>0.1f&&dt<5.0f)SetBPM(60.0f/dt);}lastTap=now;taps++;}break;
        case IDC_TOGGLE_GPU: if(g_cpuMode){g_cpuMode=false;PlaylistStop();g_cpuPlayerMode=false;PLStop();g_cpuPlaylist.Stop();g_cpuStandaloneEff.reset();g_cpuCurrEffIdx=-1;SetWindowTextW(g_hStatus,L"  已切换到 GPU 模式");}break;
        case IDC_TOGGLE_CPU: if(!g_cpuMode){g_cpuMode=true;PlaylistStop();PLStop();g_cpuPlaylist.Stop();SetWindowTextW(g_hStatus,L"  已切换到 CPU GDI 模式");}break;
        case IDM_SETTINGS: ShowSettingsDialog(hwnd); break;
        default:
            if(id>=IDC_PRESET_BASE&&id<=IDC_PRESET_BASE+4){
                int s=id-IDC_PRESET_BASE;
                if(!g_presets[s].empty()) PresetLoad(s);
            }
            break;
        }
        return 0;
    }

    case WM_CONTEXTMENU: {
        HWND from=(HWND)wParam;int id=GetDlgCtrlID(from);
        if(id>=IDC_PRESET_BASE&&id<=IDC_PRESET_BASE+4){int s=id-IDC_PRESET_BASE;HMENU m=CreatePopupMenu();AppendMenuW(m,MF_STRING,1,L"追加到播放链");AppendMenuW(m,MF_STRING,2,L"覆盖保存");AppendMenuW(m,MF_STRING,3,L"删除");POINT pt;GetCursorPos(&pt);int c=TrackPopupMenu(m,TPM_RETURNCMD|TPM_NONOTIFY,pt.x,pt.y,0,hwnd,nullptr);DestroyMenu(m);if(c==1)PresetLoad(s);else if(c==2)PresetSave(s);else if(c==3){PresetDelete(s);UpdatePresetButtons();}}
        return 0;
    }

    case WM_HSCROLL:
        if((HWND)lParam==g_hTrackSpeed){int pos=(int)SendMessageW(g_hTrackSpeed,TBM_GETPOS,0,0);SetSpeed((float)pos/100.0f);}
        else{int op=(int)SendMessageW(GetDlgItem(g_hwnd,IDC_TRACK_OPACITY),TBM_GETPOS,0,0);g_blendOpacity=(float)op/100.0f;int ps=(int)SendMessageW(g_hPlaylist,LB_GETCURSEL,0,0);if(ps>=0&&ps<(int)g_playlist.size()){g_playlist[ps].opacity=g_blendOpacity;}wchar_t buf[16];_snwprintf(buf,16,L"%d%%",op);SetWindowTextW(GetDlgItem(g_hwnd,IDC_STATIC_OPACITY),buf);}return 0;

    case WM_KEYDOWN: if(wParam==VK_ESCAPE)DestroyWindow(hwnd); return 0;
    case WM_SIZE: {RECT rc;GetClientRect(hwnd,&rc);SetWindowPos(g_hStatus,nullptr,0,rc.bottom-22,rc.right,22,SWP_NOZORDER);return 0;}
    case WM_DESTROY: PlaylistStop();KillTimer(hwnd,TIMER_INIT);KillTimer(hwnd,TIMER_MAIN);g_audio.Shutdown();PostQuitMessage(0);return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ===== 设置对话框 =====
#define IDD_MODE_GPU   3001
#define IDD_MODE_CPU   3002
#define IDD_SPEED      3003
#define IDD_SPEED_LBL  3004
#define IDD_BPM        3005
#define IDD_TAP        3006
#define IDD_OK         3007
#define IDD_BG         3008
#define IDD_CNT_MIN    3009
#define IDD_CNT_MAX    3010
#define IDD_OP_MIN     3011
#define IDD_OP_MAX     3012
#define IDD_RANDOM     3013
#define IDD_DUR_MIN    3014
#define IDD_DUR_MAX    3015

static void ApplyRenderMode(bool cpu) {
    if (g_cpuMode == cpu) return;
    g_cpuMode = cpu;
    PlaylistStop(); PLStop(); g_cpuPlaylist.Stop();
    g_cpuPlayerMode = false;
    if (cpu) SetWindowTextW(g_hStatus, L"  已切换到 CPU GDI 模式");
    else { g_cpuStandaloneEff.reset(); g_cpuCurrEffIdx = -1; SetWindowTextW(g_hStatus, L"  已切换到 GPU 模式"); }
}

static void ApplyBackground(bool dark) {
    g_plBlackBg = dark;
    g_cpuPlaylist.SetDarkMode(dark);
    if (g_playerMode) InvalidateRect(g_hPlayer, nullptr, FALSE);
}

static int g_randMinCount = 3, g_randMaxCount = 10;
static float g_randMinOpacity = 0.3f, g_randMaxOpacity = 1.0f;
static int g_randMinDuration = 15, g_randMaxDuration = 45;

static void GenerateRandomPlaylist() {
    if (g_effectCount <= 0) return;
    if (g_randMaxCount < g_randMinCount) { int t=g_randMinCount; g_randMinCount=g_randMaxCount; g_randMaxCount=t; }
    if (g_randMaxOpacity < g_randMinOpacity) { float t=g_randMinOpacity; g_randMinOpacity=g_randMaxOpacity; g_randMaxOpacity=t; }
    if (g_randMaxDuration < g_randMinDuration) { int t=g_randMinDuration; g_randMinDuration=g_randMaxDuration; g_randMaxDuration=t; }
    int range = g_randMaxCount - g_randMinCount + 1;
    int count = g_randMinCount + (range > 1 ? rand() % range : 0);
    int drange = g_randMaxDuration - g_randMinDuration + 1;
    // 单 effect 不够成对时，无法保证每项都有上层 → 限制最大数量
    int maxPairs = (g_effectCount >= 2) ? (g_effectCount * (g_effectCount - 1)) : 0;
    if (maxPairs > 0 && count > maxPairs) count = maxPairs;
    std::wstring prevBase, prevOver;
    for (int i = 0; i < count; ++i) {
        PlaylistItem item;
        // 底层 + 上层必不同，且与上一条目组合不重复（多次尝试）
        int bi = -1, ti = -1;
        for (int tries = 0; tries < 32; ++tries) {
            bi = rand() % g_effectCount;
            ti = rand() % g_effectCount;
            if (bi == ti) continue;                       // 上下层必不同
            if (g_effects[bi].name == prevBase && g_effects[ti].name == prevOver) continue; // 相邻不重复
            break;
        }
        if (bi < 0) bi = 0; if (ti < 0 || ti == bi) { ti = (bi + 1) % g_effectCount; }
        item.name = g_effects[bi].name;
        item.name2 = g_effects[ti].name;
        prevBase = item.name; prevOver = item.name2;
        item.blendMode = (BlendMode)(rand() % BM_COUNT);
        float r = (float)rand() / (float)RAND_MAX;
        float op = g_randMinOpacity + r * (g_randMaxOpacity - g_randMinOpacity);
        if (op < 0) op = 0; if (op > 1) op = 1;
        item.opacity = op;
        item.durationSec = g_randMinDuration + (drange > 1 ? rand() % drange : 0);
        g_playlist.push_back(item);
    }
    RefreshPlaylistUI();
    if (g_hStatus) SetWindowTextW(g_hStatus, L"  已随机生成播放链");
}

static LRESULT CALLBACK SettingsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HFONT f = CreateFontW(16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei");
        int y=14;
        CreateWindowW(L"STATIC",L"渲染模式:",WS_CHILD|WS_VISIBLE|SS_RIGHT,10,y+4,80,22,hwnd,nullptr,nullptr,nullptr);
        HWND rg=CreateWindowW(L"BUTTON",L"GPU",WS_CHILD|WS_VISIBLE|BS_AUTORADIOBUTTON|WS_GROUP,100,y,60,24,hwnd,(HMENU)IDD_MODE_GPU,nullptr,nullptr);
        HWND rc=CreateWindowW(L"BUTTON",L"CPU",WS_CHILD|WS_VISIBLE|BS_AUTORADIOBUTTON,170,y,60,24,hwnd,(HMENU)IDD_MODE_CPU,nullptr,nullptr);
        SendMessageW(rg,WM_SETFONT,(WPARAM)f,TRUE); SendMessageW(rc,WM_SETFONT,(WPARAM)f,TRUE);
        CheckRadioButton(hwnd, IDD_MODE_GPU, IDD_MODE_CPU, g_cpuMode?IDD_MODE_CPU:IDD_MODE_GPU);
        y+=40;
        CreateWindowW(L"STATIC",L"速度:",WS_CHILD|WS_VISIBLE|SS_RIGHT,10,y+4,80,22,hwnd,nullptr,nullptr,nullptr);
        HWND sp=CreateWindowW(TRACKBAR_CLASSW,nullptr,WS_CHILD|WS_VISIBLE|TBS_HORZ|TBS_NOTICKS,100,y,130,28,hwnd,(HMENU)IDD_SPEED,nullptr,nullptr);
        SendMessageW(sp,TBM_SETRANGE,TRUE,MAKELONG(10,1000)); SendMessageW(sp,TBM_SETPOS,TRUE,(int)(g_userSpeed*100.0f));
        HWND sl=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_LEFT,240,y+4,50,22,hwnd,(HMENU)IDD_SPEED_LBL,nullptr,nullptr);
        wchar_t sb[16]; _snwprintf(sb,16,L"%.1fx",g_userSpeed); SetWindowTextW(sl,sb);
        y+=40;
        CreateWindowW(L"STATIC",L"BPM:",WS_CHILD|WS_VISIBLE|SS_RIGHT,10,y+4,80,22,hwnd,nullptr,nullptr,nullptr);
        HWND bpm=CreateWindowW(L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER,100,y,60,24,hwnd,(HMENU)IDD_BPM,nullptr,nullptr);
        SendMessageW(bpm,WM_SETFONT,(WPARAM)f,TRUE); SendMessageW(bpm,EM_SETLIMITTEXT,5,0);
        wchar_t bb[16]; _snwprintf(bb,16,L"%.0f",g_bpm); SetWindowTextW(bpm,bb);
        HWND tap=CreateWindowW(L"BUTTON",L"Tap",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,170,y,50,24,hwnd,(HMENU)IDD_TAP,nullptr,nullptr);
        SendMessageW(tap,WM_SETFONT,(WPARAM)f,TRUE);
        y+=40;
        CreateWindowW(L"STATIC",L"背景:",WS_CHILD|WS_VISIBLE|SS_RIGHT,10,y+4,80,22,hwnd,nullptr,nullptr,nullptr);
        HWND bg=CreateWindowW(L"BUTTON",L"黑底",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,100,y+2,80,24,hwnd,(HMENU)IDD_BG,nullptr,nullptr);
        SendMessageW(bg,WM_SETFONT,(WPARAM)f,TRUE);
        SendMessageW(bg,BM_SETCHECK,(g_cpuMode?g_cpuPlaylist.IsBlackBg():g_plBlackBg)?BST_CHECKED:BST_UNCHECKED,0);
        y+=40;
        CreateWindowW(L"STATIC",L"数量范围:",WS_CHILD|WS_VISIBLE|SS_RIGHT,10,y+4,80,22,hwnd,nullptr,nullptr,nullptr);
        HWND cmin=CreateWindowW(L"EDIT",L"3",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER,100,y,40,24,hwnd,(HMENU)IDD_CNT_MIN,nullptr,nullptr);
        SendMessageW(cmin,WM_SETFONT,(WPARAM)f,TRUE);
        CreateWindowW(L"STATIC",L"~",WS_CHILD|WS_VISIBLE|SS_CENTER,145,y+4,20,22,hwnd,nullptr,nullptr,nullptr);
        HWND cmax=CreateWindowW(L"EDIT",L"10",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER,170,y,40,24,hwnd,(HMENU)IDD_CNT_MAX,nullptr,nullptr);
        SendMessageW(cmax,WM_SETFONT,(WPARAM)f,TRUE);
        y+=40;
        CreateWindowW(L"STATIC",L"不透明度:",WS_CHILD|WS_VISIBLE|SS_RIGHT,10,y+4,80,22,hwnd,nullptr,nullptr,nullptr);
        HWND omin=CreateWindowW(L"EDIT",L"0.3",WS_CHILD|WS_VISIBLE|WS_BORDER,100,y,40,24,hwnd,(HMENU)IDD_OP_MIN,nullptr,nullptr);
        SendMessageW(omin,WM_SETFONT,(WPARAM)f,TRUE);
        CreateWindowW(L"STATIC",L"~",WS_CHILD|WS_VISIBLE|SS_CENTER,145,y+4,20,22,hwnd,nullptr,nullptr,nullptr);
        HWND omax=CreateWindowW(L"EDIT",L"1.0",WS_CHILD|WS_VISIBLE|WS_BORDER,170,y,40,24,hwnd,(HMENU)IDD_OP_MAX,nullptr,nullptr);
        SendMessageW(omax,WM_SETFONT,(WPARAM)f,TRUE);
        y+=40;
        CreateWindowW(L"STATIC",L"时长(秒):",WS_CHILD|WS_VISIBLE|SS_RIGHT,10,y+4,80,22,hwnd,nullptr,nullptr,nullptr);
        HWND dmin=CreateWindowW(L"EDIT",L"15",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER,100,y,40,24,hwnd,(HMENU)IDD_DUR_MIN,nullptr,nullptr);
        SendMessageW(dmin,WM_SETFONT,(WPARAM)f,TRUE);
        CreateWindowW(L"STATIC",L"~",WS_CHILD|WS_VISIBLE|SS_CENTER,145,y+4,20,22,hwnd,nullptr,nullptr,nullptr);
        HWND dmax=CreateWindowW(L"EDIT",L"45",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER,170,y,40,24,hwnd,(HMENU)IDD_DUR_MAX,nullptr,nullptr);
        SendMessageW(dmax,WM_SETFONT,(WPARAM)f,TRUE);
        y+=40;
        HWND rnd=CreateWindowW(L"BUTTON",L"随机生成播放链",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,10,y,150,30,hwnd,(HMENU)IDD_RANDOM,nullptr,nullptr);
        SendMessageW(rnd,WM_SETFONT,(WPARAM)f,TRUE);
        y+=40;
        HWND ok=CreateWindowW(L"BUTTON",L"确定",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,10,y,90,30,hwnd,(HMENU)IDD_OK,nullptr,nullptr);
        SendMessageW(ok,WM_SETFONT,(WPARAM)f,TRUE);
        DeleteObject(f);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDD_MODE_GPU: ApplyRenderMode(false); break;
        case IDD_MODE_CPU: ApplyRenderMode(true); break;
        case IDD_TAP: { static float lastTap=0; static int taps=0; float now=(float)GetTickCount()/1000.0f; if(taps>0){float dt=now-lastTap; if(dt>0.1f&&dt<5.0f){SetBPM(60.0f/dt); wchar_t b[16]; _snwprintf(b,16,L"%.0f",g_bpm); SetDlgItemTextW(hwnd,IDD_BPM,b);} } lastTap=now; taps++; } break;
        case IDD_BG: ApplyBackground(SendMessageW(GetDlgItem(hwnd,IDD_BG),BM_GETCHECK,0,0)==BST_CHECKED); break;
        case IDD_RANDOM: {
            wchar_t buf[16];
            GetDlgItemTextW(hwnd,IDD_CNT_MIN,buf,16); int mn=_wtoi(buf);
            GetDlgItemTextW(hwnd,IDD_CNT_MAX,buf,16); int mx=_wtoi(buf);
            GetDlgItemTextW(hwnd,IDD_OP_MIN,buf,16); float omin=(float)_wtof(buf);
            GetDlgItemTextW(hwnd,IDD_OP_MAX,buf,16); float omax=(float)_wtof(buf);
            GetDlgItemTextW(hwnd,IDD_DUR_MIN,buf,16); int dmin=_wtoi(buf);
            GetDlgItemTextW(hwnd,IDD_DUR_MAX,buf,16); int dmax=_wtoi(buf);
            if (mn < 1) mn = 1; if (mx < mn) mx = mn;
            if (omin < 0) omin = 0; if (omax > 1) omax = 1; if (omax < omin) omax = omin;
            if (dmin < 1) dmin = 1; if (dmax < dmin) dmax = dmin;
            g_randMinCount = mn; g_randMaxCount = mx;
            g_randMinOpacity = omin; g_randMaxOpacity = omax;
            g_randMinDuration = dmin; g_randMaxDuration = dmax;
            GenerateRandomPlaylist();
            break;
        }
        case IDD_OK: case IDCANCEL: DestroyWindow(hwnd); return 0;
        case IDD_BPM: if(HIWORD(wParam)==EN_KILLFOCUS){wchar_t buf[16];GetDlgItemTextW(hwnd,IDD_BPM,buf,16);int v=_wtoi(buf);if(v>0)SetBPM((float)v);} break;
        }
        return 0;
    case WM_HSCROLL:
        if((HWND)lParam==GetDlgItem(hwnd,IDD_SPEED)){int pos=(int)SendMessageW(GetDlgItem(hwnd,IDD_SPEED),TBM_GETPOS,0,0);SetSpeed((float)pos/100.0f);wchar_t b[16];_snwprintf(b,16,L"%.1fx",g_userSpeed);SetDlgItemTextW(hwnd,IDD_SPEED_LBL,b);}
        return 0;
    case WM_KEYDOWN: if(wParam==VK_ESCAPE){DestroyWindow(hwnd);return 0;} break;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    }
    return DefWindowProc(hwnd,msg,wParam,lParam);
}

static void ShowSettingsDialog(HWND parent) {
    static bool reg=false;
    if(!reg){WNDCLASSEXW wc={};wc.cbSize=sizeof(wc);wc.lpfnWndProc=SettingsDlgProc;wc.hInstance=GetModuleHandle(nullptr);wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);wc.lpszClassName=L"MTHSettingsDlg";RegisterClassExW(&wc);reg=true;}
    HWND dlg=CreateWindowExW(0,L"MTHSettingsDlg",L"设置",WS_POPUP|WS_CAPTION|WS_SYSMENU,100,100,310,420,parent,nullptr,GetModuleHandle(nullptr),nullptr);
    if(!dlg) return;
    RECT rp,rd; GetWindowRect(parent,&rp); GetWindowRect(dlg,&rd);
    int x=rp.left+(rp.right-rp.left)/2-(rd.right-rd.left)/2;
    int y=rp.top+(rp.bottom-rp.top)/2-(rd.bottom-rd.top)/2;
    SetWindowPos(dlg,nullptr,x,y,0,0,SWP_NOSIZE|SWP_NOZORDER);
    ShowWindow(dlg,SW_SHOW);
    EnableWindow(parent,FALSE);
    MSG msg;
    while(IsWindow(dlg) && GetMessageW(&msg,nullptr,0,0)){
        if(!IsDialogMessageW(dlg,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
    }
    EnableWindow(parent,TRUE);
    SetForegroundWindow(parent);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow) {
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    srand((unsigned)GetTickCount());

    // 提前加载 shader（需要临时 OpenGL 上下文加载 WGL 函数）
    {   WNDCLASSEXW wc={}; wc.cbSize=sizeof(wc); wc.lpfnWndProc=DefWindowProc; wc.hInstance=hInst; wc.lpszClassName=L"VizGLInit"; RegisterClassExW(&wc);
        HWND tmpHwnd = CreateWindowExW(0, L"VizGLInit", L"", 0, 0,0,1,1, nullptr,nullptr,hInst,nullptr);
        HDC tmpHdc = GetDC(tmpHwnd);
        PIXELFORMATDESCRIPTOR pfd={sizeof(pfd),1,PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER,PFD_TYPE_RGBA,32,0,0,0,0,0,0,0,0,0,0,0,0,0,24,8,0,PFD_MAIN_PLANE,0,0,0,0};
        SetPixelFormat(tmpHdc, ChoosePixelFormat(tmpHdc, &pfd), &pfd);
        HGLRC tmpRC = wglCreateContext(tmpHdc);
        if (tmpRC) {
            wglMakeCurrent(tmpHdc, tmpRC);
            LoadGL();
            LoadEffects(); RegisterCpuEffects();
            // 主初始化时也编译 blend shader（播放前就能用）
            {   // 极简混合 shader：只有 linear mix
                const char* vs = "#version 330 core\nlayout(location=0)in vec2 p;layout(location=1)in vec2 u;out vec2 vUV;void main(){vUV=u;gl_Position=vec4(p,0,1);}";
                const char* fs = "#version 330 core\n"
                    "in vec2 vUV;out vec4 c;"
                    "uniform sampler2D uBase,uOverlay;uniform int uMode;uniform float uOpacity;"
                    "float ch(float a,float b,int m){"
                    "if(m==1)return max(a,b);"
                    "if(m==2)return 1.0-(1.0-a)*(1.0-b);"
                    "if(m==3)return a<0.5?2.0*a*b:1.0-2.0*(1.0-a)*(1.0-b);"
                    "if(m==4)return b<0.5?2.0*a*b:1.0-2.0*(1.0-a)*(1.0-b);"
                    "if(m==5){if(b<0.5)return a-(1.0-2.0*b)*a*(1.0-a);else{float g=a<=0.25?((16.0*a-12.0)*a+4.0)*a:sqrt(a);return a+(2.0*b-1.0)*(g-a);}}"
                    "if(m==6)return a+b-1.0;"
                    "if(m==7)return a*b;"
                    "return b;}"
                    "void main(){"
                    "vec4 a=texture(uBase,vUV),b=texture(uOverlay,vUV);"
                    "float mr=ch(a.r,b.r,uMode),mg=ch(a.g,b.g,uMode),mb=ch(a.b,b.b,uMode);"
                    "vec3 bl=clamp(vec3(mr,mg,mb),0.0,1.0);float eb=b.a*uOpacity;float onlyA=a.a*(1.0-eb);float onlyB=eb*(1.0-a.a);float both=a.a*eb;vec3 res=a.rgb*onlyA+b.rgb*onlyB+bl*both;float tot=onlyA+onlyB+both;c=vec4(a.rgb*(1.0-tot)+res,1.0);}";
                auto c3=[](GLenum t,const char* s)->GLuint{
                    GLuint sh=glCreateShader(t);glShaderSource(sh,1,&s,0);glCompileShader(sh);
                    GLint ok;glGetShaderiv(sh,GL_COMPILE_STATUS,&ok);
                    if(!ok)return 0;return sh;};
                GLuint v3=c3(GL_VERTEX_SHADER,vs),f3=c3(GL_FRAGMENT_SHADER,fs);
                if(v3&&f3){g_blendProg=glCreateProgram();glAttachShader(g_blendProg,v3);glAttachShader(g_blendProg,f3);glLinkProgram(g_blendProg);glDeleteShader(v3);glDeleteShader(f3);
                    GLint linkOk;glGetProgramiv(g_blendProg,GL_LINK_STATUS,&linkOk);
                    if(!linkOk){g_blendProg=0;}}
            }
            InitVAO();
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(tmpRC);
        }
        ReleaseDC(tmpHwnd, tmpHdc); DestroyWindow(tmpHwnd);
    }

    WNDCLASSEXW wc={}; wc.cbSize=sizeof(wc); wc.style=CS_HREDRAW|CS_VREDRAW; wc.lpfnWndProc=WndProc; wc.hInstance=hInst;
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.hbrBackground=CreateSolidBrush(RGB(20,20,35)); wc.lpszClassName=L"AudioVizGPU";
    RegisterClassExW(&wc);
    HWND hwnd=CreateWindowExW(0,L"AudioVizGPU",L"MAIN [GPU] — 音频可视化",WS_OVERLAPPEDWINDOW&~WS_MAXIMIZEBOX&~WS_THICKFRAME|WS_CLIPCHILDREN,
        CW_USEDEFAULT,CW_USEDEFAULT,920,750,nullptr,nullptr,hInst,nullptr);
    if(!hwnd)return 1;
    ShowWindow(hwnd,nShow);UpdateWindow(hwnd);
    MSG msg;while(GetMessageW(&msg,nullptr,0,0)){TranslateMessage(&msg);DispatchMessageW(&msg);}
    DeleteObject(wc.hbrBackground);
    return(int)msg.wParam;
}
