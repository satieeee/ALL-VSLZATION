// ============================================================================
// GL 3.3 函数指针 + 本工程用到的 GL 常量 —— 【唯一声明处】
//
// 【为什么要这么写】以前这些东西（typedef + `static PFN_xxx` + 常量 #define）全写在
// main.cpp 里，函数指针是 `static`（内部链接）→ 效果代码一旦搬进 VisualEffects/<效果>/，
// 就一个 GL 函数都调不到，于是所有效果只能堆在 main.cpp 里。
//
// 现在：本文件是【清单】，宏按谁 include 决定展开成什么 ——
//   · main.cpp   ：先 `#define GLFUNCS_DEFINE` 再 include → 展开成【定义】，全局一份；
//   · 其它 .cpp  ：直接 include → 展开成 `extern` 声明，链到 main.cpp 那份。
// 所以清单只有一份，不存在"加了函数忘了改另一处"。
//
// 【谁负责填值】main.cpp 的 LoadGL()（wglGetProcAddress）。它在建好 GL 上下文之后调用，
// 所以任何渲染代码跑到的时候指针都已经就位。新增函数记得往 LoadGL() 里加一行 L(...)。
//
// 【include 顺序】`#define GLFUNCS_DEFINE` + include 必须是本 TU 【第一次】看到本文件的地方
// （因为用了 #pragma once）。main.cpp 里它排在效果头之前，所以效果头放在它后面 include 即可。
// 万一顺序搞错：不会静默，链接期会报"未解析的外部符号 glXxx"，一眼就能认出来。
// ============================================================================
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/gl.h>
#include <cstddef>

typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;
typedef char GLchar;

#ifdef GLFUNCS_DEFINE
#define GLP(ret,n,args) typedef ret (APIENTRY *PFN_##n)args; PFN_##n n;
#else
#define GLP(ret,n,args) typedef ret (APIENTRY *PFN_##n)args; extern PFN_##n n;
#endif

GLP(void,glGenVertexArrays,(GLsizei n,GLuint*a))
GLP(void,glBindVertexArray,(GLuint a))
GLP(void,glDeleteVertexArrays,(GLsizei n,const GLuint*a))
GLP(void,glGenBuffers,(GLsizei n,GLuint*b))
GLP(void,glBindBuffer,(GLenum t,GLuint b))
GLP(void,glBufferData,(GLenum t,GLsizeiptr s,const void*d,GLenum u))
GLP(void,glBufferSubData,(GLenum t,GLintptr o,GLsizeiptr s,const void*d))
GLP(void,glDeleteBuffers,(GLsizei n,const GLuint*b))
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
GLP(void,glUniform1fv,(GLint l,GLsizei c,const GLfloat*v))
GLP(void,glUniform4fv,(GLint l,GLsizei c,const GLfloat*v))
GLP(void,glGenFramebuffers,(GLsizei n,GLuint*b))
GLP(void,glBindFramebuffer,(GLenum t,GLuint b))
GLP(void,glFramebufferTexture2D,(GLenum t,GLenum a,GLenum tt,GLuint tex,GLint l))
GLP(GLenum,glCheckFramebufferStatus,(GLenum t))
GLP(void,glGetFramebufferAttachmentParameteriv,(GLenum t,GLenum a,GLenum p,GLint*v))
GLP(void,glBlendColor,(GLfloat r,GLfloat g,GLfloat b,GLfloat a))
GLP(void,glDeleteFramebuffers,(GLsizei n,const GLuint*b))
GLP(void,glBlitFramebuffer,(GLint sx0,GLint sy0,GLint sx1,GLint sy1,GLint dx0,GLint dy0,GLint dx1,GLint dy1,GLbitfield mask,GLenum filter))
GLP(void,glActiveTexture,(GLenum t))
#undef GLP

// ---- 本工程用到的 GL 常量 ----
// Windows 自带的 gl.h 只有 GL 1.1，这些得自己定义（值与规范一致）。
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_DEPTH_COMPONENT 0x1902
#define GL_UNSIGNED_INT 0x1405
#define GL_NONE 0
#define GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE 0x8CD0
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE2 0x84C2
#define GL_TEXTURE3 0x84C3
#define GL_TEXTURE4 0x84C4
#define GL_TEXTURE5 0x84C5
#define GL_TEXTURE6 0x84C6
#define GL_R32F 0x822E
#define GL_R8 0x8229
#define GL_RGBA32F 0x8814
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
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
// gl_PointSize 要生效必须开这一位（GL 2.0 常量，Windows 的 gl.h 只有 1.1，得自己定义）。
// GL_POINTS / GL_FLOAT / GL_DST_COLOR 在 gl.h 里已有，值相同不用重复定义。
#define GL_PROGRAM_POINT_SIZE 0x8642
// 同理：本项目用 wglCreateContext 建的是【兼容性 profile】上下文，而点精灵
// （gl_PointCoord）在兼容性 profile 下默认是关的 —— 不开这一位，片元着色器里
// gl_PointCoord 是废值，粒子会一个像素都画不出来且不报任何 GL 错误。
// 离线台 _hittest/particle_render_test.cpp 抓到过这个坑。
#define GL_POINT_SPRITE 0x8861
// StellarVortex 的泛光管线：场景渲染进 RGBA16F 离屏缓冲（保留 >1 的高光供亮部提取），
// 再半分辨率模糊、加色合成。8 位缓冲会在 1.0 处截断，泛光就没了源头。
#define GL_RGBA16F 0x881A
#define GL_HALF_FLOAT 0x140B
#define GL_COPY_INVERTED 0x150C
