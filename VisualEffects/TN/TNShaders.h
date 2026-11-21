// ============================================================================
// Filigree 的着色器原文（kTVS / kVTFS）
//
// 【为什么从 main.cpp 搬出来】离线验证台要的是【实际跑的那一份】原文。
// 以前靠 ExtractRaw() 从 main.cpp 里按 `const char* kTVS = R"(` 抓文本 ——
// 台子因此依赖 main.cpp 的排版，每加一个效果还要在台子里再写一遍抓取代码。
// 现在台子直接 #include 这个头，拿到的是同一个变量。
//
// 【改了这个文件要重编台子】—— 着色器文本是编进 exe 的（以前抓文本时反而不用）。
// ============================================================================
#pragma once

static const char* kTVS = R"(#version 330 core
layout(location = 0) in vec3  aPos;
layout(location = 1) in vec2  aUV;
layout(location = 2) in vec3  aNrm;    // 几何里【烘好】的法线（不用 dFdx/dFdy 推）
layout(location = 3) in float aPhase;  // 细条：每根的随机相位；壁/纸 = 0
layout(location = 6) in float aRibLen; // 细条：本根【原本的长度】（世界单位）；壁/纸 = 0
                                       //   频谱要按响度缩放针的长短，就得知道它原来多长
layout(location = 7) in vec3  aSpine;  // 细条：本顶点对应的【中轴（脊线）位置】；壁/纸 = 0
                                       //   "几何由参数决定"的把手：顶点 − 中轴 = 横截面向量,
                                       //   于是绕中轴缩放/重排都成了几行乘法
layout(location = 4) in float aShade;  // 剪纸片：面明暗（CPU 烘）；壁/条 = 0
layout(location = 5) in float aAlpha;  // 剪纸片：出生/消亡透明度；壁/条 = 1

uniform int   uPass;        // 0=背景 1=几何 2=亮部 3=模糊 4=合成
uniform int   uMat;         // 0=壁 1=细条 2=剪纸片
uniform float uLayerZ;      // 本层的 z 平移（无限隧道）
uniform float uTime;
uniform float uRibWave;
uniform float uNear, uSpan;
uniform float uFadeIn, uFadeFull;   // 近端淡入区间（由 C++ 侧的 TN_FADE_* 传进来，
                                    // 别在 shader 里写死 —— 写死就成第二份真相了）
uniform vec2  uResolution;          // 投影要做宽高比修正
uniform float uFocal;               // 焦距（fov≈48°，与其它 mesh 效果一致）

// ---- 手持镜头（鼓点驱动，见 RenderFiligree 里那段"手持镜头"）----
// 把 3x3 旋转拆成 3 个 vec4：这个工程的 GL 加载器只到 glUniform4fv，没有 Matrix3fv，
// 拆成三行正好用现成的接口传，变换端三次点积就完事。
// 【必须是单位矩阵起步】—— 全零的话 p 会被压到原点，整个场景什么都不剩。
uniform vec4  uCamR0, uCamR1, uCamR2;
// 手持镜头的【平移】（视图空间，xyz；w 补 0 只为凑 glUniform4fv）。
// 【它和旋转做的不是一回事】旋转动的是画面上每一点的方向 → 会移动灭点；
// 平移给的是【视差】—— 近处的几何相对远处横扫，而灭点（远处的像）一动不动。
// 原来只有旋转，等于相机在原地转圈，整幅图就是"刚性转 + 平移 2%"，看着像贴在窗口上的二维光。
uniform vec4  uCamT;

// 音乐频谱：顶点端用它缩放【针的长短】、片元端用它定【亮度与颜色】。
// 【上传的是归一化过的值】（CPU 每帧除以当前最响的频段）—— 所以这里不用再归一化，
// 顶点端也就不需要那个 16 次循环。
uniform float uFft[16];
uniform float uRibLenBase, uRibLenMax;  // 静止/最响时的长度倍率（见 TNParams.h）
// ---- 形状参数（默认必须全是【恒等】，否则默认画面就不是原来那张）----
uniform float uRibWidth;   // 粗细倍率：横截面绕中轴缩放（1 = 原样）
uniform float uRibSpread;  // 径向收拢/散开：中轴到隧道轴的距离倍率（1 = 原样）
uniform float uRibCurl;    // 绕隧道轴缠绕：每单位 z 转多少弧度（0 = 原样 = 直条）

// 【隧道空间的坐标】= 转镜头【之前】的位置（细条的摆动/层平移都算进去了）。
// 色相/角度这类"属于这根条自己"的量必须用它 —— 见 kVTFS 里 rimAng 那段。
out vec3  vObj;
out vec3  vWorld;
out vec3  vNrm;
out vec2  vUV;
out float vFog;
out float vShade;
out float vAlpha;
out float vPhase;

void main() {
    // ---- 全屏趟：直通 ----
    // 【uv 取 location 1】g_vao 的 location 0 是 NDC(±1)、1 才是 uv(0..1)。
    // 写成 vUV = aPos.xy 会拿 NDC 当 uv 去采样场景纹理，画面会缩在右上四分之一。
    if (uPass != 1) {
        gl_Position = vec4(aPos.xy, 0.0, 1.0);
        vUV    = aUV;
        vObj   = vec3(aPos.xy, 0.0);
        vWorld = vec3(aPos.xy, 0.0);
        vNrm   = vec3(0.0, 0.0, 1.0);
        vFog = 1.0; vShade = 0.0; vAlpha = 1.0; vPhase = 0.0;
        return;
    }

    vec3 p = aPos;
    if (uMat == 1) {
        // 细条沿 xy 缓慢摆动。【必须是连续量，不能取模】—— 顶点级的 mod
        // 会把跨边界的三角形撕成横贯屏幕的碎片（VXGeom 记过这一条）。
        float ph = aPhase * 6.2831853;
        p.xy += vec2(sin(uTime * 0.70 + ph), cos(uTime * 0.90 + ph * 1.3)) * uRibWave;

        // ---- 音乐频谱：改【针的长短】----
        // 细条是沿 z 的直线：p.z = 尾端z + v·len（v 就是 aUV.y，len 是烘进顶点的原长）。
        // 所以知道 len 就能反推出另一端，绕它缩放长度：
        // 【静止 = 原来的长度】，响的频段才朝镜头伸出来（基准见 TNParams.h 的 BASE）。
        // （当初只能改长度是因为着色器拿不到中轴；现在顶点带了 aSpine，粗细/缠绕也能做了 ——
        //   见下面"几何由参数决定"那一段。）
        //
        // 频段 = 这根条在隧道里的方位，与 kVTFS 里那套【必须完全一致】（同一个 16 段）：
        // 不一致的话"针有多长"和"针有多亮"会对不上。
        float ang = atan(p.y, p.x) / 6.2831853;          // 断面角 → [-0.5, 0.5]
        int   bnd = int(fract(ang) * 16.0);
        bnd = (bnd < 0) ? 0 : ((bnd > 15) ? 15 : bnd);
        float spc = clamp(uFft[bnd], 0.0, 1.0);          // 已由 CPU 归一化（最响的段 = 1）
        float lenScale = mix(uRibLenBase, uRibLenMax, spc);   // 静止 = 1.0 = 原始图形

        // ---- 几何由参数决定：中轴可控，横截面挂在它上面 ----
        // 【默认参数下这三行是恒等变换】—— 画面必须与加这套之前逐像素一致（台子会比对）。
        // ① 中轴【收拢/散开】：径向缩放。1 = 原样，<1 = 收成细束，>1 = 散成宽锥
        vec3 sp = vec3(aSpine.xy * uRibSpread, aSpine.z);
        // ② 中轴【绕隧道轴缠绕】：相位随 z 增长 → 直条被拧成螺旋。0 = 原样（cos0=1, sin0=0，恒等）
        {
            // 【不能写 const】—— GLSL 的 const 要求编译期常量表达式，吃 uRibCurl/uSpine 会报
            // "non constant expression in initialization"（本文件 uMat==3 那段早就记过这条）。
            float a = uRibCurl * sp.z, c = cos(a), s2 = sin(a);
            sp = vec3(c * sp.x - s2 * sp.y, s2 * sp.x + c * sp.y, sp.z);
        }
        // ③ 把顶点重新挂到新中轴上，并绕中轴缩放截面（粗细）。1 = 原样
        p = sp + (p - aSpine) * uRibWidth;
        // 【锚在"头"（远端 v=1），不是锚在尾端】—— 这是第一版看不出变化的原因：
        // 锚尾端时长度变化发生在【远端】，而远端正好被雾淡掉（fadeFar），改了看不见。
        // 锚远端之后动的是【近端】：响的频段那几根针朝镜头方向伸出来、安静的缩回去，
        // 而近端是画面最亮、最靠中间的那一段 —— 一眼就看得到。
        // 【基准取中轴而不是顶点自己】—— 一个横截面的四个角点 z 各不同（截面有厚度/扭转），
        // 各自为基准的话"缩放长度"就不是刚性变换。取中轴之后同一 v 共享同一个头端。
        float headZ = sp.z + (1.0 - aUV.y) * aRibLen;     // 头端（v=1）的 z
        p.z = headZ + (p.z - headZ) * lenScale;
    }
    // ---- 中轴纽结自转 + 固定倾斜（只对 uMat==3）----
    // 几何以原点为中心建【一次】，转就转在这里 —— 不用每帧重建 VBO。
    //
    // 【必须夹在 aPos 与 uLayerZ 之间】纽结的 uLayerZ 就是它在轴上的位置 KT_Z。
    // 我第一版插在了 `p.z += uLayerZ` 后面，于是变成"先平移到 (0,0,9)、再绕原点转 41°" ——
    // 整个结被甩到 NDC y ≈ -2.1，直接出屏。当时的表现是"倾角 0 看得见、一加倾角就没了"，
    // 因为倾角 0 时旋转是恒等，恰好抵消了这个顺序错误。
    //
    // 【不能写 const】GLSL 的 const 要求编译期常量表达式，吃 uTime 会报
    // "non constant expression in initialization" —— 片元着色器那边已经踩过一次了。
    if (uMat == 3) {
        // ① 绕结自己的轴自转（在结的本地坐标系里，此时它还在原点）
        float ca = cos(uTime * 0.55), sa = sin(uTime * 0.55);
        p = vec3(ca * p.x - sa * p.y, sa * p.x + ca * p.y, p.z);
        // ② 固定倾斜。【不倾不行】(p,q) 环面纽结的 z 起伏只有 r=0.26，而环半径 R=0.85，
        //    正对着隧道轴看它就是个平甜甜圈，完全没有"打了个结"的立体感。
        //    绕 x 倾 ~41°、再绕 y 倾 ~22°，就是标准的 3/4 视角。
        float ax = 0.72f, ay = 0.38f;
        float cx = cos(ax), sx = sin(ax);
        float cy = cos(ay), sy = sin(ay);
        p = vec3(p.x, cx * p.y - sx * p.z, sx * p.y + cx * p.z);
        p = vec3(cy * p.x + sy * p.z, p.y, -sy * p.x + cy * p.z);
    }

    p.z += uLayerZ;

    // 【隧道空间快照】—— 转镜头与平移【之前】。要在 p.z += uLayerZ 之后取：
    // 剪纸的 v 是"几个隧道周期"、细条的层平移也在这里，取早了色相就会随着层漂。
    vObj = p;

    // ---- 手持镜头：把世界转到相机空间 ----
    // 【必须在雾与投影之前】这样 vWorld、vFog、gl_Position 全落在同一个视图空间里；
    // 放到投影之后就会出现"位置转了、雾没转"的错位（远处的墙跟着转、明暗却不跟着）。
    // 全屏趟不受影响 —— 它在上面就提前 return 了，而几何趟那个专用程序把整块都删掉了。
    p = vec3(dot(uCamR0.xyz, p), dot(uCamR1.xyz, p), dot(uCamR2.xyz, p));
    // 【平移】放在旋转之后：p_view = R·p_world − T，即"相机往 +T 挪了"。
    // x/y 是手持镜头的横向漂移；【z 专门给鼓点那一刻的瞬间推进】（TN_HIT_PUSH）。
    // 往 +z 走会让近端顶点落到相机【后面】，被 d = max(p.z, 0.05) 放大成很大的三角形 ——
    // 挡住它们的是近端淡出（fadeNear 在 z<1.5 时为 0，见下面雾那段）。
    // 所以横向量一直很小、推进量也别超过 ~1.5，否则边角会糊过来。
    p -= uCamT.xyz;

    // 深浅色模式：细条与纸片的 alpha 直接给，壁面用雾当 alpha（远端淡出）
    vShade = aShade;
    vAlpha = aAlpha;
    vPhase = aPhase;

    vWorld = p;
    vNrm   = aNrm;
    vUV    = aUV;

    // ---- 雾：两端都要淡出 ----
    // 远端必须正好到 0（环绕点在那里），近端必须淡（环绕点 TN_NEAR_Z 在相机【后面】，
    // 不淡的话会有一层几何从镜头里穿过去）。
    float k = clamp(1.0 - (p.z - uNear) / uSpan, 0.0, 1.0);
    float fadeFar  = smoothstep(0.0, 0.45, k);
    float fadeNear = clamp((p.z - uFadeIn) / max(uFadeFull - uFadeIn, 1e-4), 0.0, 1.0);
    // 【纸片不吃近端淡出】fadeNear 存在的理由是"壁面/细条沿 z 环绕时别从镜头里穿过去"。
    // 纸片是自由漂浮的、下一个鼓点就退场，永远不会环绕 —— 对它用近端淡出等于把
    // "离镜头最近、最该看清"的那批全压黑，画面上就只剩远处挤在消失点的一坨。
    vFog = fadeFar * ((uMat == 2) ? 1.0 : fadeNear);

    // ---- 相机在中轴上朝 +z 看：w = 视深做透视除法 ----
    float d = max(p.z, 0.05);
    float aspect = uResolution.x / uResolution.y;
    gl_Position = vec4(p.x * uFocal / aspect, p.y * uFocal, 0.0, d);
}
)";

static const char* kVTFS = R"(#version 330 core
in vec3  vObj;      // 隧道空间（转镜头之前）—— 色相/方位这类"属于这根条自己"的量用它
in vec3  vWorld;
in vec3  vNrm;
in vec2  vUV;
in float vFog;
in float vShade;
in float vAlpha;
in float vPhase;
out vec4 fragColor;

uniform int   uPass;
uniform int   uMat;
uniform float uTime;
uniform float uOpacity;
// 【必须是 int】——上传走 glUniform1i。写成 float 会 GL_INVALID_OPERATION、
// 值写不进去恒为 0，浅色模式的取反就永远生效（kF1FS 刚踩过这个，别再犯）。
uniform int   uDarkBg;
uniform float uRMax;         // 隧道最大半径（外缘微光定位用）
uniform float uRimW, uRimI;
uniform vec4  uWallA, uWallB, uMetalA, uMetalB, uPaperA, uPaperB, uLaceC, uRimC;
uniform sampler2D uTexScene, uTexBloom;
uniform vec2  uTexel, uBlurDir, uResolution;
uniform float uBloomAmt, uBloomThresh, uExposure, uSat;
// 中轴纽结的整体辉光强度。由 CPU 每帧算（常驻底值 + 鼓点冲击），
// 【会远大于 1】—— 场景 FBO 是 RGBA16F，亮部提取按"亮度 − uBloomThresh"取，
// 只有推到 7~20 这个量级，泛光才会真的炸开。见 uMat==3 那一支。
uniform float uKnotGlow;
// 隧道壁【棱脊】的炫彩辉光强度。走的是壁面本来就有的"外缘微光"那条判据
// （到中轴的半径 → 八瓣断面的棱），只是把单色弱光换成彩虹 + 远超 1 的辐射值。
// 和 uKnotGlow 一样靠 HDR 溢出炸泛光，见 uMat==0 那一支。
uniform float uEdgeGlow;
// 金属细条【两条长边】的炫彩辉光：强度与带宽。壁删掉之后辉光就落在这里
// （uEdgeGlow 那条支路已经没有任何绘制会走到）。见 uMat==1 那一支。
uniform float uRibGlow, uRibGlowW;
// 音乐频谱：16 个频段贴在细条两条长边上（见 uMat==1 那一支）。
// 【和别的效果同名同约定】uFft 是工程里已有的音频 ABI（AudioAnalyzer 的 fft[16]），
// 上传走 glUniform1fv(loc, kAudioBands, g_af.fft)。
uniform float uFft[16];
// 随机色的种子：每下鼓点换一个（CPU 按鼓点序号散列，同一次鼓点内保持不变 ——
// 逐帧随机会变成雪花噪点，那不叫"颜色变化"）。
uniform float uBeatSeed;
uniform float uSpecAmt, uSpecFloor;   // 频谱响度对应的亮度倍率：静=Floor(1.0) 最响=Amt
uniform float uRibWide;               // 边宽额外倍率：静止 = 原带宽，最响 = 1+uRibWide 倍

float tvhash(vec2 p){ vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }

/// 彩虹：HSV 里 S=V=1 的那个截面，h 绕一圈正好回到起点。
/// 三段折线的标准写法，比手搓 6 个 if 干净，而且天然饱和、不会过曝成白。
vec3 tvHue(float h) {
    vec3 p = abs(fract(h + vec3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return clamp(p - 1.0, 0.0, 1.0);
}

/// 程序化深空背景（同 Involute / Involute_1 / Function1 的做法）
vec3 tvBackground(vec2 uv) {
    float d = length(uv - 0.5);
    vec3 col = mix(vec3(0.035, 0.042, 0.082), vec3(0.003, 0.004, 0.010),
                   smoothstep(0.02, 0.62, d));
    for (int L = 0; L < 2; ++L) {
        float fl = float(L);
        vec2 g = uv * (160.0 + fl * 210.0);
        vec2 id = floor(g), f = fract(g) - 0.5;
        float h = tvhash(id + fl * 41.7);
        float d2 = length(f - vec2(tvhash(id + 2.3), tvhash(id + 5.1)) + 0.5);
        float st = smoothstep(0.050 - fl * 0.018, 0.0, d2) * step(0.986 - fl * 0.004, h);
        st *= 0.55 + 0.45 * sin(uTime * 1.5 + h * 62.0);
        col += vec3(0.72, 0.80, 1.0) * st * (0.9 - fl * 0.3);
    }
    return col;
}

void main() {
    if (uPass == 0) { fragColor = vec4(tvBackground(vUV), 1.0); return; }
    if (uPass == 2) {
        vec3 c = texture(uTexScene, vUV).rgb;
        float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
        fragColor = vec4(c * max(0.0, l - uBloomThresh) / max(l, 1e-4), 1.0);
        return;
    }
    if (uPass == 3) {
        vec2 t = uBlurDir * uTexel;
        vec3 s = texture(uTexBloom, vUV).rgb * 0.2270270;
        s += (texture(uTexBloom, vUV + t*1.3846).rgb + texture(uTexBloom, vUV - t*1.3846).rgb) * 0.3162162;
        s += (texture(uTexBloom, vUV + t*3.2308).rgb + texture(uTexBloom, vUV - t*3.2308).rgb) * 0.0702703;
        fragColor = vec4(s, 1.0);
        return;
    }
    if (uPass == 4) {
        vec3 col = texture(uTexScene, vUV).rgb + texture(uTexBloom, vUV).rgb * uBloomAmt;
        col *= uExposure;
        // 只压亮度、颜色等比跟随（逐通道 ACES 会把颜色拉向白，饱和度全丢）
        float lum0 = max(dot(col, vec3(0.299, 0.587, 0.114)), 1e-4);
        float lumT = clamp((lum0*(2.51*lum0+0.03))/(lum0*(2.43*lum0+0.59)+0.14), 0.0, 1.0);
        col = clamp(col * (lumT / lum0), 0.0, 1.0);
        float lum = dot(col, vec3(0.299, 0.587, 0.114));
        col = clamp(mix(vec3(lum), col, uSat), 0.0, 1.0);
        vec2 c = vUV - 0.5;
        col *= 1.0 - dot(c, c) * 0.75;
        if (uDarkBg == 0) col = vec3(1.0) - col;
        fragColor = vec4(col, uOpacity);
        return;
    }

    // ================= 几何趟 =================
    vec3 n = normalize(vNrm);
    vec3 V = normalize(-vWorld);              // 相机在原点，视线由点指向相机
    // 【NaN 兜底 —— 剪纸片就是被这个坑掉的，别再删】
    // vNrm 在两个方向相反的顶点法线之间插值时可能插出接近零的向量，normalize 之后就是 NaN；
    // 而 GLSL 里 `0.0 * NaN` 仍然是 NaN，所以哪怕乘的是全零的 uRimC 也救不回来 ——
    // 整条 col 一旦是 NaN，片元写进场景 FBO 就是黑的，画面上表现成"这一片什么都没有"。
    // Filigree 的剪纸片正是如此：几何、属性、uniform 全部正确，却一个像素都看不见。
    if (any(isnan(n))) n = vec3(0.0, 0.0, 1.0);
    if (any(isnan(V))) V = vec3(0.0, 0.0, 1.0);
    // 双面受光：细条与剪纸片两面都看得到，背面靠翻法线才不至于死黑
    if (dot(n, V) < 0.0) n = -n;
    // 光源在相机这一侧（z 为负）。抄 VX 那套是给"相机朝 +z 看"的，
    // 这里同样是朝 +z 看，所以沿用同一组方向。
    vec3 L1 = normalize(vec3(-0.45,  0.62, -0.64));
    vec3 L2 = normalize(vec3( 0.70, -0.34, -0.63));
    float lam = max(dot(n, L1), 0.0) * 0.80 + max(dot(n, L2), 0.0) * 0.42;
    float sh  = 0.10 + lam;

    vec3 col;
    if (uMat == 3) {
        // ---- 中轴纽结：极端炫彩辉光 ----
        // 【它是光源，不是物体】—— 这里输出的是【辐射值】，会被推到远超 1 的量级。
        // 场景 FBO 是 RGBA16F（8 位在 1.0 截断，泛光就没有源头了），亮部提取那一趟按
        // "亮度 − uBloomThresh" 取，再被两趟大半径高斯抹开、乘 uBloomAmt 加回合成的画面。
        // 所以只要这里给得够大，泛光就会从消失点炸开一大片彩色光晕 —— 这是"极度夸张"
        // 唯一能成立的做法：靠 HDR 溢出，不是靠画一圈更大更亮的边。
        float hue = fract(vUV.y * 2.0 + uTime * 0.11);      // 沿管长跑彩虹
        vec3  rb  = tvHue(hue);
        rb += 0.35 * tvHue(fract(hue + 0.5));               // 补色叠加 → 更"炫彩"、更脏的饱和
        // 绕管一圈的亮带：让"这是一根管"读得出来，不然只是一团彩光
        float band = 0.55 + 0.45 * sin(vUV.x * 6.2831853 * 2.0 + uTime * 2.0);
        col = rb * (uKnotGlow * band);
    } else if (uMat == 2) {
        // ---- 内壁剪纸片：面明暗已由 CPU 烘进 vShade，这里只上色 ----
        // 棱要硬 —— vShade 是逐面的常量，绝不能被插值抹平（C3 的 flat 做法同理）。
        // 写成显式 lerp：与 mix() 数学等价，但避开了这条支路上一连串"值明明对、结果却是 0"
        // 的怪现象（见 _hittest/vt_render_test.cpp 的 mode 30~35 记录）。
        // 【别用 pow()】—— 四次连乘是确定行为；也别用 max() 挡 NaN，它在部分驱动上会放 NaN 过去。
        float sh = (vShade > 0.0) ? vShade : 1.0;
        vec3  pc = uPaperA.rgb + (uPaperB.rgb - uPaperA.rgb) * clamp(vUV.y, 0.0, 1.0);
        float rimd = dot(n, V);
        rimd = (rimd > 0.0) ? rimd : 0.0;
        float rim2 = rimd * rimd;          // 【不能加 const】GLSL 的 const 要求常量表达式
        col = (pc + uRimC.rgb * (rim2 * rim2) * 0.18) * sh;
    } else if (uMat == 1) {
        // ---- 金属细条：沿长度的高光带 + 明细的金属反光 ----
        vec3 base = mix(uMetalA.rgb, uMetalB.rgb, clamp(vUV.x, 0.0, 1.0));
        base *= 0.45 + 0.90 * sh;
        float band = 0.5 + 0.5 * sin(vUV.y * 26.0 - uTime * 1.8 + vPhase * 6.2831853);
        base += vec3(1.0) * pow(band, 8.0) * 0.45 * max(dot(n, V), 0.0);

        // ---- 两条长边的炫彩辉光：辉光现在的落点 ----
        // 【自发光项，不乘 sh】—— 它不是"被照到的面"，是"这根丝自己在发亮"；
        // 乘上明暗会让背光面的辉光被掐掉一半，两条边一亮一暗。
        //
        // 判据用 vUV.x：细条是扁箔，u 横跨【宽】的那一维（VTRibbonBuild 里 qu = 0/1
        // 分别写在 +S / -S 两个侧面上），所以 u≈0 与 u≈1 就是它的两条长边。
        // 【必须取 min(u, 1-u)】只判 u < W 的话只有一条边亮、另一条边是死的。
        float e = min(vUV.x, 1.0 - vUV.x);
        // ---- 断面角：色相与【频段分配】都用它 ----
        // 【必须用 vObj（隧道空间），不能用 vWorld（视图空间）】
        // 这一条第一版写错过，代价是一处很难描述的怪现象：vWorld 是转完镜头之后的坐标，
        // 于是"按角度"这件事挂在【屏幕角】上 —— 镜头 roll 时几何体转 −φ、色相跟着 +φ，
        // 颜色图案绕窗口中心转，转速看着是几何体的两倍，整块光就像贴在屏幕上。
        // 实测：老写法下镜头 roll 0.2 rad，色相随屏幕角的分布只动了 0.023 rad（=没动）；
        // 换成 vObj 之后才跟着转到 0.2。颜色/频段从此属于隧道：某根条永远是同一个色。
        float rimAng = atan(vObj.y, vObj.x) / 6.2831853;   // 断面角 → [-0.5, 0.5]

        // ---- 音乐频谱：每根针 = 一个频段（绕隧道一圈就是一台环形等化器）----
        // 【为什么不再沿条长摊开】实机上细条在屏幕上只有几个像素宽，沿长度摊成 16 段之后
        // 一段还不到一个像素，"梳齿"根本没地方长（实测反馈"太不明显"）。改按【方位角】
        // 分频段之后，每根针整体就是它那一段的亮度、粗细和颜色 —— 与视角远近无关，
        // 近看远看都是同一台等化器。
        // 【不能叫 band】—— 这条分支上面第 95 行已经有个 `float band`（金属高光带），
        // 同名会被 GLSL 判成重复声明（编译直接失败，台子会报"kVTFS 编译失败"）。
        int specBand = int(fract(rimAng) * 16.0);
        specBand = (specBand < 0) ? 0 : ((specBand > 15) ? 15 : specBand);
        float spec = uFft[specBand];

        // 【归一化已经挪到 CPU】（每帧除以当前最响的频段），这里拿到的就是归过一的 ——
        // 顶点端也要用同一个值（缩放针的长短），放 CPU 只做一次最省事。
        spec = sqrt(spec);          // 提亮中段：不处理的话安静的段几乎全黑

        // 边宽跟着响度走：安静的段只是一条窄边，最响的段整根针都在发光。
        // 【静止 = 原带宽（1.0）】，最响 = 1+uRibWide 倍。
        float ew  = uRibGlowW * (1.0 + uRibWide * spec);
        float rim = 1.0 - smoothstep(0.0, max(ew, 1e-4), e);
        // 彩虹辉光：色相按【断面角】分布，相邻的条共用一段色相，所以整体是一圈彩虹
        // 在缓慢绕，而不是每根各给一个随机色（那看着是彩纸屑，不是光）。（rimAng 见上）
        vec3  rc = tvHue(fract(rimAng + uTime * 0.07));
        rc += 0.35 * tvHue(fract(rimAng + uTime * 0.07 + 0.5));   // 补色叠加，更炫、更"脏"

        // 【频谱用"乘"而不是"加"】—— 这一条是第一版"针的变化太小"的正解。
        // 浅色模式里彩虹辉光本身就顶到 3.0（已经过曝），再往上【加】频谱时：
        //   安静段 3.0+0.6 = 3.6、最响段 3.0+3.1 = 6.1 —— 两个都已经超过 1，被 ACES 压成
        //   同一个白，反相之后同样是黑，**根本没有变化**。
        // 改成乘：响度直接缩放辉光本身，安静的段暗到几乎没有、最响的段冲进 HDR。
        // 同时颜色也跟着走：响的段用"这一拍随机色"，安静的段还是彩虹底。
        vec3  specCol = tvHue(fract(tvhash(vec2(float(specBand) * 4.1, uBeatSeed)) + uTime * 0.02));
        vec3  edgeCol = mix(rc, specCol, spec);
        float specGain = mix(uSpecFloor, uSpecAmt, spec);   // 安静=floor，最响=amt
        col = mix(base, edgeCol * (uRibGlow * specGain), rim);
        // 【必须是 mix（替换），不能是 base + rc*rim*glow（相加）】
        // 浅色模式最后是 `1 - col`：想要"深底上一条饱和的彩边"，反相【前】就得是
        // "一个通道顶到 1、另外两个明显低"。相加会把金属底色（0.72/0.80/0.92 —— 本来就亮）
        // 整份带进来，三个通道一起抬高，色相被稀释成脏橄榄色；实机第一版就是这么出来的。
        // 替换成纯自发光色，通道差才拉得开。（深色模式下 6 倍的自发光同样过曝，泛光照样炸。）
        //
    } else {
        // ---- 隧道壁：渐变 + 细密的镂空暗刻 + 外缘微光 ----
        col = mix(uWallA.rgb, uWallB.rgb, clamp(vUV.x, 0.0, 1.0));
        col *= sh;
        // 两层不同频率的正弦网格叠出"贴在内壁的暗刻花纹"
        float g1 = sin(vUV.x * 6.2831853 * 24.0);
        float g2 = sin(vUV.y * 6.2831853 * 9.0 + vUV.x * 6.2831853 * 3.0);
        float lace = smoothstep(0.55, 0.95, g1 * g2);
        col += uLaceC.rgb * lace * 0.22;
        // 外缘微光 → 【超级无敌炫彩辉光】：判据不变（到中轴的半径，不是断面参数；
        // 按 u 取带会把整面长墙点亮、糊掉半个画面 —— VXGeom 记过这条）。
        // 判据不变意味着点亮的还是那八条纵向棱脊 —— 它们从画面外圈一路收向消失点，
        // 所以辉光天然集中在画面中间。
        //
        // 与旧版的区别只有两点：
        //  ① 单色 uRimC → 沿【断面角】分布的彩虹（八条棱各一色），随时间流动
        //  ② 强度从 uRimI(≈1) 提到 uEdgeGlow（常驻 5、鼓点顶到 ~25）——
        //     远超 1 才有 HDR 溢出，泛光才会真的炸开；和 uMat==3 那条同理。
        float outerness = 1.0 - length(vWorld.xy) / uRMax;
        float rim = 1.0 - smoothstep(0.0, max(uRimW, 1e-4), outerness);
        float rimAng = atan(vWorld.y, vWorld.x) / 6.2831853;   // 断面角 → [-0.5, 0.5]
        vec3  rimCol = tvHue(fract(rimAng + uTime * 0.07));
        rimCol += 0.35 * tvHue(fract(rimAng + uTime * 0.07 + 0.5));   // 补色叠加，更炫
        col += rimCol * (rim * uEdgeGlow);
    }

    col *= vFog;
    // 壁面靠雾做 alpha（远端淡出到背景里）；纸片用自己的出生/消亡透明度
    fragColor = vec4(col, (uMat == 2) ? (vAlpha * vFog) : vFog);
}
)";
