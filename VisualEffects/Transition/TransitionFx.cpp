// ============================================================================
// TransitionFx.cpp —— 实现见头文件。所有状态都是本 TU 的 static。
// ============================================================================
#include "TransitionFx.h"

namespace TransitionFx {

namespace {

GLuint s_oldFbo = 0, s_oldTex = 0;
GLuint s_newFbo = 0, s_newTex = 0;
int    s_w = 0, s_h = 0;

GLuint s_prog = 0;
GLuint s_vao = 0, s_vbo = 0;
bool   s_failed = false;

const char* kVS =
    "#version 330 core\n"
    "layout(location=0) in vec2 aPos;\n"
    "out vec2 vUV;\n"
    "void main(){ vUV = aPos*0.5+0.5; gl_Position = vec4(aPos,0.0,1.0); }\n";

// 合成：两个画面按 CosineInterp 混合，采样位置加一个中段最大的共享位移场。
const char* kFS =
    "#version 330 core\n"
    "in vec2 vUV; out vec4 fragColor;\n"
    "uniform sampler2D uOld, uNew;\n"
    "uniform float uP;       // 0..1\n"
    "uniform float uWarp;\n"
    "uniform float uZoom;\n"
    "uniform float uAspect;\n"
    "uniform float uBass;\n"
    "uniform float uTime;\n"
    "uniform float uRot;\n"
    "float hash(vec2 p){ p=fract(p*vec2(123.34,456.21)); p+=dot(p,p+45.32); return fract(p.x*p.y); }\n"
    "float vnoise(vec2 p){\n"
    "  vec2 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);\n"
    "  return mix(mix(hash(i),hash(i+vec2(1,0)),f.x),\n"
    "             mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),f.x),f.y);\n"
    "}\n"
    "void main(){\n"
    "  vec2 uv = clamp(vUV, 0.001, 0.999);\n"
    "  float p  = clamp(uP, 0.0, 1.0);\n"
    "  // 新画面：前 25% 淡入，之后满强度 —— 整个过渡的底座\n"
    "  vec3 nw = texture(uNew, uv).rgb * smoothstep(0.0, 0.25, p);\n"
    "  // 旧画面：放大 + 轻微旋转 + 噪声位移，亮度幂次衰减 → 被吃掉\n"
    "  vec2 q = (uv - 0.5) * vec2(uAspect, 1.0);\n"
    "  float zoom = 1.0 + uZoom * p;\n"
    "  vec2 np = q * 2.3 + vec2(uTime, uTime * 0.7);\n"
    "  vec2 d  = vec2(vnoise(np) - 0.5, vnoise(np + vec2(17.0, 31.0)) - 0.5);\n"
    "  d += vec2(vnoise(np * 2.1 + vec2(5.0, 9.0)) - 0.5,\n"
    "            vnoise(np * 2.1 + vec2(41.0, 3.0)) - 0.5) * 0.5;\n"
    "  vec2 q2 = q / zoom;\n"
    "  q2 += d * uWarp * (1.0 - p) * (0.03 + 0.03 * uBass);\n"
    "  float ang = uRot * p;\n"
    "  float c = cos(ang), s = sin(ang);\n"
    "  q2 = mat2(c, -s, s, c) * q2;\n"
    "  q2.x /= uAspect;\n"
    "  vec3 oldC = texture(uOld, clamp(q2 + 0.5, 0.001, 0.999)).rgb;\n"
    "  oldC *= pow(1.0 - p, 1.5);\n"
    "  // 合成：MilkDrop comp 的 max —— 亮处当前帧赢，暗处留旧画面\n"
    "  fragColor = vec4(max(nw, oldC), 1.0);\n"
    "}\n";


GLuint Compile(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        glGetShaderInfoLog(sh, sizeof(log) - 1, nullptr, log);
        MessageBoxA(nullptr, log, "TransitionFx Shader Error", MB_OK);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

bool BuildProgram() {
    if (s_prog) return true;
    if (s_failed) return false;
    GLuint vs = Compile(GL_VERTEX_SHADER, kVS);
    GLuint fs = Compile(GL_FRAGMENT_SHADER, kFS);
    if (!vs || !fs) { s_failed = true; return false; }
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        glGetProgramInfoLog(p, sizeof(log) - 1, nullptr, log);
        MessageBoxA(nullptr, log, "TransitionFx Link Error", MB_OK);
        glDeleteProgram(p);
        s_failed = true;
        return false;
    }
    s_prog = p;

    const float quad[] = { -1, -1,  3, -1,  -1, 3 };   // 覆盖整个 NDC 的大三角形
    glGenVertexArrays(1, &s_vao);
    glGenBuffers(1, &s_vbo);
    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);
    return true;
}

void MakeTarget(GLuint& fbo, GLuint& tex, int w, int h) {
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void DestroyTarget(GLuint& fbo, GLuint& tex) {
    if (fbo) { glDeleteFramebuffers(1, &fbo); fbo = 0; }
    if (tex) { glDeleteTextures(1, &tex); tex = 0; }
}

} // namespace

bool Ensure(int w, int h) {
    if (w <= 0 || h <= 0) return false;
    if (!BuildProgram()) return false;
    if (w == s_w && h == s_h && s_oldTex && s_newTex) return true;
    DestroyTarget(s_oldFbo, s_oldTex);
    DestroyTarget(s_newFbo, s_newTex);
    s_w = w; s_h = h;
    MakeTarget(s_oldFbo, s_oldTex, w, h);
    MakeTarget(s_newFbo, s_newTex, w, h);
    return true;
}

GLuint FboOld() { return s_oldFbo; }
GLuint FboNew() { return s_newFbo; }

void Composite(int w, int h, float progress, const Params& p) {
    if (!s_prog || !s_oldTex || !s_newTex) return;
    glUseProgram(s_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_oldTex);
    glUniform1i(glGetUniformLocation(s_prog, "uOld"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, s_newTex);
    glUniform1i(glGetUniformLocation(s_prog, "uNew"), 1);
    glActiveTexture(GL_TEXTURE0);

    glUniform1f(glGetUniformLocation(s_prog, "uP"), progress);
    glUniform1f(glGetUniformLocation(s_prog, "uWarp"), p.warp);
    glUniform1f(glGetUniformLocation(s_prog, "uZoom"), p.zoom);
    glUniform1f(glGetUniformLocation(s_prog, "uBass"), p.bass);
    glUniform1f(glGetUniformLocation(s_prog, "uRot"),  p.rot);
    glUniform1f(glGetUniformLocation(s_prog, "uAspect"), (h > 0) ? (float)w / (float)h : 1.0f);
    // 噪声场缓慢流动；用进度当时间轴 → 每段过渡的位移形态都不一样，但不跳
    glUniform1f(glGetUniformLocation(s_prog, "uTime"), progress * 6.0f);

    glBindVertexArray(s_vao);
    glDisable(GL_BLEND);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void Shutdown() {
    DestroyTarget(s_oldFbo, s_oldTex);
    DestroyTarget(s_newFbo, s_newTex);
    s_w = s_h = 0;
    if (s_prog) { glDeleteProgram(s_prog); s_prog = 0; }
    if (s_vbo)  { glDeleteBuffers(1, &s_vbo); s_vbo = 0; }
    if (s_vao)  { glDeleteVertexArrays(1, &s_vao); s_vao = 0; }
    s_failed = false;
}

} // namespace TransitionFx
