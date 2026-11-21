// ============================================================================
// VXShaders
//
// Involute 渐开线曲面着色器。离线台：_hittest/vx_render_test.cpp。
//
// 【为什么单独一个头】离线台要的是【实际跑的那一份】原文。以前靠 ExtractRaw()
// 从 main.cpp 里抓文本，台子因此依赖 main.cpp 的排版；改成 include 之后两边
// 拿到的是同一个变量，抄错/漏改这一类问题就不存在了。
//
// 【改了这个文件要重编离线台】—— 着色器文本是编进 exe 的。
// ============================================================================
#pragma once

static const char* kVXVS = R"(#version 330 core
layout(location = 0) in vec3 aPos;   // 一个周期的网格（见 VXGeom.h）
layout(location = 1) in vec2 aUV;    // u = 绕横截面 [0,1)，v = 沿 z [0,1)
layout(location = 2) in vec2 aOutward; // 径向单位向量（背离中轴）——第二形态沿它朝画面外分离

uniform float uPhase;      // 推进相位（CPU 累加，永远增长 → 无限推进）
uniform float uLayerZ;     // 【本层整层的 z 平移】——回绕在 CPU 上算好，见下面注释
uniform float uNear, uSpan;// 可见深度区间 [uNear, uNear+uSpan)
uniform float uFormDisp;   // 第二形态进度 0..1（0=原始态，1=分离态）
uniform float uFormDist;   // 分离距离（世界单位）
uniform float uTime;
uniform float uFocal;      // 1/tan(fov/2)
uniform vec2  uResolution;
uniform float uExitTime;   // 出场：整体飞出（与其它 mesh 效果同一语义）
uniform int   uPass;       // 0=背景 1=几何 2=亮部 3=模糊 4=合成

out vec3  vWorld;
out vec2  vUV;
out float vFog;            // 远处淡出（0=太远看不见，1=在眼前）

void main() {
    // ---- 全屏 quad 的那几趟：g_vao 给的是 NDC xy + uv，直通 ----
    // （g_vao 的属性 0 是 vec2，读到 vec3 aPos 时第三分量自动补 0，不影响）
    if (uPass != 1) {
        gl_Position = vec4(aPos.xy, 0.0, 1.0);
        vUV = aUV;
        vWorld = vec3(aUV, 0.0);
        vFog = 1.0;
        return;
    }

    vec3 p = aPos;

    // ---- 第二形态：所有台阶沿【径向】朝画面外分离 ----
    // uFormDisp 由 CPU 在每次鼓点时翻转目标、用 smootherstep 过渡（变速：起步慢-中间快-收尾慢），
    // 分出去之后停住；下一个鼓点收回原始态。
    // 用径向而不是环切向：切向在两端渐开线支上方向相反，会变成"撕开"；
    // 径向让所有台阶一致地背离中轴 → 屏幕上就是一起朝外散。
    // 注意这一句要在【回绕之前】，否则贴到远端的台阶会被回绕切掉一半。
    p.xy += aOutward * (uFormDisp * uFormDist);

    // --- 无限推进：整层【刚性平移】---
    // 【绝对不能在顶点级做 mod() 回绕】——三角形只要有一个顶点跨在回绕边界两侧，
    // 它就会被映射到可见区间的两端，拉成一条贯穿全部纵深的细长片；
    // 相位每推进一步就有另一批三角形跨过边界，画面上就是一条横扫的"扫描线"。
    // 回绕改在 CPU 上算好（整层一个偏移量），这里只平移，三角形永远不可能被拆开。
    p.z += uLayerZ;

    vWorld = p;
    vUV = aUV;

    // ---- 深度淡入淡出 ----
    // 远端：【必须留一个底（0.32）】—— 一路降到 0 的话远处整个变成背景色，
    //       "质心→饱和度"会被雾吃掉（质心 0.85 时实测平均饱和度只剩 0.15）。
    // 远端要降到底（0）——刚性平移的回绕点在远端，必须完全透明才无痕。
    // 但【不能一路就降】：k² 在 z=6（才走了一半）就只剩 0.23，整个画面会被压暗。
    // 用 smoothstep(0, 0.45, k)：前 55% 行程保持全亮，接近远端才收。
    float k = clamp(1.0 - (p.z - uNear) / uSpan, 0.0, 1.0);
    float fadeFar = smoothstep(0.0, 0.45, k);
    // 近端：这一条是"别让我看见曲面被画出来"的关键。
    //   层的回绕点是 uNear(=-9.5)，它在【镜头后面】——所以一整层会从镜头里穿过去再回绕。
    //   远端有雾挡着（回绕本身看不见），但近端原来是完全不透明的，
    //   于是那层辗过画面时就像有人在现场把曲面画出来。
    //   补上近端淡出：z < 1.5 完全透明 → 跨过镜头时早就看不见了，回绕就无痕。
    const float NEAR_FADE_IN = 1.5, NEAR_FADE_FULL = 6.0;
    float fadeNear = clamp((p.z - NEAR_FADE_IN) / (NEAR_FADE_FULL - NEAR_FADE_IN), 0.0, 1.0);
    vFog = fadeFar * fadeNear;   // 两端淡出【相乘】会双重压暗，各用一次即可

    // --- 出场：整体向前飞出（同其它 mesh 效果）---
    if (uExitTime < 0.6) p.z += pow(clamp(uExitTime / 0.6, 0.0, 1.0), 2.0) * 14.0;

    // --- 相机固定在原点朝 +z，w 用深度做透视除法 ---
    float aspect = uResolution.x / uResolution.y;
    float d = max(p.z, 0.05);
    gl_Position = vec4(p.x * uFocal / aspect, p.y * uFocal, 0.0, d);
}
)";

static const char* kVXFS = R"(#version 330 core
in vec3  vWorld;
in vec2  vUV;
in float vFog;
out vec4 fragColor;

uniform float uCentroid;   // 频谱质心 0..1 —— 【控制画面颜色的饱和度】
uniform float uSatAmount;  // 质心→饱和度的强度（0=不响应，1=全范围）
uniform float uSatFloor;   // 最低饱和度（质心为 0 时也不至于完全灰）
uniform vec4  uColA, uColB;// 分面的两端基色（vec4 只是为了走 glUniform4fv ——
                           // 本项目的 GL loader 没加载 glUniform3f/3fv，同 StellarVortex）
// 泛光/合成
uniform int   uPass;
uniform sampler2D uTexScene, uTexBloom;
uniform vec2  uTexel, uBlurDir;
uniform float uBloomAmt, uBloomThresh, uExposure;
uniform vec2  uResolution;
uniform float uTime;
uniform float uOpacity;    // 播放链给的不透明度（交叉淡入用）
// ---- 跨效果 morph 协议（与 Psy/GF 是同一份三个 uniform）----
uniform int   uEncode;         // 1 = 编码趟：把本效果的结构写成标量场
uniform sampler2D uOldEncoded; // R = 上一个效果的标量场；GBA = 它当时的颜色
uniform float uMorphBlend;     // 过渡进度 0..1
uniform float uVXHalfM;        // = VX_HALF*2（横截面采样段数）——用来从 vUV 反推棋盘格
uniform float uVXRings;        // = VX_RINGS（沿 z 的圈数）
uniform float uRimW, uRimI;    // 外缘微光：半宽（占最大半径的比例）与强度
uniform float uVXRMax;         // 外缘的定义半径（= VXMaxRadius() + 第二形态的分离量）

float vxHash(vec2 p){ p = fract(p*vec2(123.34,456.21)); p += dot(p,p+45.32); return fract(p.x*p.y); }

/// 【本效果的标量场】—— 交给搭档去上色的那个 pat，地位等同 Psy 的 evalForm、GF 的 basePattern。
/// 编码趟把它写进 R 通道，搭档读出来喂进自己的调色板。
///
/// 内容是"沿横截面的位置 + 棋盘格凸凹"：
///   · u 那一项是渐开线的定义坐标（叶形的哪一段）；
///   · 棋盘格那一项是硬分面的来源 —— VX_FACET_H 让相邻面板沿径向一凸一凹，
///     这里把同一个 (i+k) 奇偶重新算出来，搭档就能把这层分面也画出来。
/// 直接由 vUV 反推而不新增顶点属性：u = i/M、v = k/RINGS 是逐顶点精确写入的，
/// 三角形内插出来的值 floor 回去仍是原来的 i、k。
/// ⚠ M 和 RINGS 由 uVXHalfM/uVXRings 传进来，别再这里写死 —— 它们来自 VXGeom.h，
///   写死就等着哪天改了 VX_HALF 而这里悄悄对不上。
float vxPattern(vec2 uv) {
    float i = floor(uv.x * uVXHalfM);
    float k = floor(uv.y * uVXRings);
    float facet = (mod(i + k, 2.0) < 0.5) ? -1.0 : 1.0;   // 与 VXGeom.h 的 ((ii+k)&1) 同义
    return clamp((uv.x * 2.0 - 1.0) * 0.72 + facet * 0.22, -1.0, 1.0);
}

/// 程序化深空背景：纵深梯度 + 两层视差星场。
/// 隧道要有一个"尽头"可以消失，纯清屏色太生硬。
vec3 vxBackground(vec2 uv) {
    float d = length(uv - 0.5);
    vec3 col = mix(vec3(0.055, 0.062, 0.115), vec3(0.004, 0.005, 0.012),
                   smoothstep(0.02, 0.62, d));
    float a = uCentroid * 0.5;                 // 质心也稍微染一下背景：亮 → 偏暖
    col += vec3(0.05, 0.03, 0.09) * a;
    for (int L = 0; L < 2; ++L) {
        float fl = float(L);
        vec2 g = uv * (170.0 + fl * 190.0);
        vec2 id = floor(g), f = fract(g) - 0.5;
        float h = vxHash(id + fl * 37.1);
        float d2 = length(f - vec2(vxHash(id + 1.7), vxHash(id + 3.3)) + 0.5);
        float st = smoothstep(0.055 - fl * 0.02, 0.0, d2) * step(0.985 - fl * 0.004, h);
        st *= 0.55 + 0.45 * sin(uTime * 1.6 + h * 60.0);
        col += vec3(0.72, 0.80, 1.0) * st * (0.9 - fl * 0.3);
    }
    return col;
}
uniform int   uDarkBg;     // 1=深色底 0=浅色底（严格负片，同 Cubeeffect3）
                           // 【必须是 int】—— 上传用的是 glUniform1i；
                           // 声明成 float 的话 GL 会报 INVALID_OPERATION 且值写不进去，
                           // 结果就是 uniform 一直是 0（浅色），整帧被反相成一片白。

void main() {
    // ================= 全屏趟：背景 / 亮部 / 模糊 / 合成 =================
    if (uPass == 0) {                       // 背景
        vec3 bg = vxBackground(vUV);
        // 【编码趟也必须写背景】——不写的话背景像素会留着 PLRender 的清屏色
        // (0.5, 0, 0, 1)，而搭档解码读的是 .gba → 解出 (0, 0, 1)【纯蓝】，
        // 过渡第一帧整屏蓝、再淡回去 = 用户报的"背景闪蓝"。
        // 按编码协议写：R = 标量场 0.5（此处无结构），GBA = 颜色。
        fragColor = (uEncode > 0) ? vec4(0.5, bg) : vec4(bg, 1.0);
        return;
    }
    if (uPass == 2) {                       // 亮部提取（渲染进半分辨率 blA）
        vec3 c = texture(uTexScene, vUV).rgb;
        float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
        fragColor = vec4(c * max(0.0, l - uBloomThresh) / max(l, 1e-4), 1.0);
        return;
    }
    if (uPass == 3) {                       // 分离式高斯（横/纵由 uBlurDir 决定）
        vec2 t = uBlurDir * uTexel;
        vec3 s = texture(uTexBloom, vUV).rgb * 0.2270270;
        s += (texture(uTexBloom, vUV + t*1.3846).rgb + texture(uTexBloom, vUV - t*1.3846).rgb) * 0.3162162;
        s += (texture(uTexBloom, vUV + t*3.2308).rgb + texture(uTexBloom, vUV - t*3.2308).rgb) * 0.0702703;
        fragColor = vec4(s, 1.0);
        return;
    }
    if (uPass == 4) {                       // 合成：场景 + 泛光 → 曝光 → ACES → 暗角
        // 泛光必须在曝光/色调映射【之前】加进来，否则高光被压扁、看起来就没泛光
        vec3 col = texture(uTexScene, vUV).rgb + texture(uTexBloom, vUV).rgb * uBloomAmt;
        col *= uExposure;
        // ---- 色调映射：只压【亮度】，颜色按比例缩放 ----
        // 逐通道各自做 ACES 会把颜色拉向白 —— 三个通道被分别压缩，比例被改写：
        //   实测青 0.832→0.667(-20%)、洋红 0.768→0.540(-30%)、混合色 0.359→0.144(-60%)。
        // 这就是"饱和度拉满还是不够鲜艳"的真凶：颜色在色调映射那一步就被洗掉了，
        // 后面再怎么调饱和度都是在近乎白色的残渣上做外插，救不回来。
        // 只对亮度做曲线、颜色等比跟随，色相与饱和度就原样保留。
        float lum0 = max(dot(col, vec3(0.299, 0.587, 0.114)), 1e-4);
        float lumT = clamp((lum0*(2.51*lum0+0.03))/(lum0*(2.43*lum0+0.59)+0.14), 0.0, 1.0);
        col = clamp(col * (lumT / lum0), 0.0, 1.0);
        // ---- 质心 → 饱和度（必须在这里：ACES 之后）----
        // ACES 是胶片曲线，会把高饱和色大幅拉向白（实测 0.36 → 0.145），
        // 所以饱和度是【显示级】调整，只能在色调映射【之后】做，否则做了等于没做。
        // 而且不 clamp 到 1 —— sat > 1 是【外插】，能把颜色推到比原色更艳，
        // 这正是"饱和拉满还是不够鲜艳"的解药。
        float sat = uSatFloor + uCentroid * uSatAmount;
        float lum = dot(col, vec3(0.299, 0.587, 0.114));
        col = clamp(mix(vec3(lum), col, sat), 0.0, 1.0);
        vec2 c = vUV - 0.5;
        col *= 1.0 - dot(c, c) * 0.75;                                             // 暗角
        if (uDarkBg == 0) col = vec3(1.0) - col;   // 浅色 = 严格负片，只在这里取反一次
        // ---- morph 收尾：按 sb 把【搭档的画面】收进来 ----
        // 与 Psy/GF 的 col = mix(oldCol, newCol, sb) 是同一件事，位置也一样放在最后。
        // 没有这一句，过渡一开始画面就是完整的隧道 —— 而协议要求 sb=0 时必须【正好是上一个效果】。
        // 放在负片之后：oldEnc.gba 存的是搭档已经定稿的显示色（它的 uDarkBg 已经算进 mapColor 里了），
        // 而 col 到这一步也才是显示色，两边同一空间。
        if (uMorphBlend > 0.0 && uMorphBlend < 1.0) {
            float sb = uMorphBlend * uMorphBlend * (3.0 - 2.0 * uMorphBlend);
            vec3 oldCol = texture(uOldEncoded, vUV).gba;   // 只取 .gba —— R 是标量场不是颜色
            col = mix(oldCol, col, sb);
        }
        // alpha 用播放链给的不透明度 → 交叉淡入时叠在旧效果上，而不是把它一刀切掉
        fragColor = vec4(col, uOpacity);
        return;
    }

    // ================= 几何趟 =================
    // ---- 硬分面：屏幕空间导数一次给出整个三角形的法线 ----
    vec3 n = normalize(cross(dFdx(vWorld), dFdy(vWorld)));
    if (n.z > 0.0) n = -n;                 // 统一朝镜头（拱道内壁）

    // ---- 两盏方向光 + 环境：凹凸面的明暗差靠这个拉开 ----
    vec3 L1 = normalize(vec3(-0.45, 0.62, -0.64));
    vec3 L2 = normalize(vec3( 0.70, -0.34, -0.63));
    float lam = max(dot(n, L1), 0.0) * 0.85 + max(dot(n, L2), 0.0) * 0.45;
    float sh = 0.10 + lam;                 // 环境 0.10，避免背面死黑

    // ---- 基色：沿横截面与纵深走一条双色渐变 ----
    // 【跨效果 morph】驱动这条渐变的不是固定的 vUV.x，而是"混合后的标量场"：
    //   过渡开头 = 搭档的结构 oldPat，过渡结束 = 本效果自己的结构 (2·vUV.x-1)。
    // 与 Psy/GF 完全同构 —— 那边也是 mapColor(blendedPat)，只是它们的 mapColor 复杂得多。
    // 注意 sb→1 时 patMix 必须精确回到 2·vUV.x-1：morph 分支在 uMorphBlend==1 时不再进入，
    // 两端对不上的话，过渡结束的那一帧画面会跳一下。
    float patMix = vUV.x * 2.0 - 1.0;
    if (uMorphBlend > 0.0 && uMorphBlend < 1.0) {
        float sb = uMorphBlend * uMorphBlend * (3.0 - 2.0 * uMorphBlend);
        float oldPat = texture(uOldEncoded, gl_FragCoord.xy / uResolution).rgb.r * 2.0 - 1.0;
        patMix = oldPat * (1.0 - sb) + patMix * sb;
    }
    vec3 col = mix(uColA.rgb, uColB.rgb, patMix * 0.5 + 0.5);
    col *= 0.65 + 0.35 * vUV.y;            // 沿纵深稍微变化，读得出"在展开"

    // 质心 → 饱和度【不在这里做】：ACES 色调映射会把饱和度砍掉约 60%
    // （实测基色 0.36 → 过完 ACES 只剩 0.145）。放在色调映射之前做等于白做。
    // 见合成趟（uPass==4）—— 饱和度是【显示级】调整，必须在最后一步。
    col *= sh;

    // ---- 外缘微光 ----
    // 渐开线叶形在 u=0.5 收口（t→T 的两端 + 闭合边），那是曲面的外缘，也是离中轴最远的一圈。
    // 只用 vUV.x 就能定位它，不必再算世界半径：u 本来就是"在叶形的哪一段"这个坐标，
    // 而且它对 VX_TWIST 的自转免疫（自转是绕中轴的旋转，不改 u）。
    // 色相跟着当前配色走（否则鼓点换色时这圈光会跟画面脱节），往白里提一点当高光。
    // 【放在 vFog 乘上去之前】：远处那圈不该比近处还亮，雾要一视同仁。
    // 判据用【到中轴的半径】，不是横截面参数 u。
    // 教训：一开始我用 |u - 0.5| 取带子，以为那是叶形的外缘 —— 结果糊掉大半个画面。
    // 原因是渐开线在 t=T 处收口，两个端点分居索引 M/2-1 和 M/2，它们之间的"闭合边"
    // 是一条从 (0.88, +2.45) 直连 (0.88, -2.45) 的长墙，墙中点半径才 0.88、紧贴中轴。
    // 按 u 取带子 = 把这面大墙整面点亮。半径才是"外缘"的正确刻度。
    //
    // 写法上也踩过两个坑：
    //  ① smoothstep(0,0,x) 是 0/0 —— GLSL 未定义，实测吐 NaN；NaN 乘 0 还是 NaN，
    //     col += NaN 会把整个片元涂黑。宽度必须兜底。
    //  ② 反着写 smoothstep(a,b,x)（a>b）想让它是递减的也不行：规范里 edge0>=edge1 未定义。
    //     一并改成"正常方向的 smoothstep 再取补"。
    float outerness = 1.0 - length(vWorld.xy) / uVXRMax;   // 0 = 正好在外缘，越大越靠内
    float rim = 1.0 - smoothstep(0.0, max(uRimW, 1e-4), outerness);
    vec3 rimCol = mix(mix(uColA.rgb, uColB.rgb, 0.5), vec3(1.0), 0.55);
    col += rimCol * (rim * uRimI);

    // ---- 深度淡出：直接衰减到透明，让【背景趟】透出来 ----
    // 之前是混向一个写死的雾色；现在背景是独立的一趟（有纵深梯度和星场），
    // 让它透出来才有"隧道消失在深空里"的感觉。
    // 深浅模式的取反挪到【合成趟】做一次 —— 在这里做会和合成趟重复取反。
    col *= vFog;

    // ---- 编码趟（协议同 Psy/GF）：R = 本效果的结构场，GBA = 颜色 ----
    // 搭档只会拿走 R，用【它自己的】调色板重新上色 —— 这正是 morph 与 crossfade 的分界。
    if (uEncode > 0) {
        if (vFog < 0.02) discard;   // 已淡出到看不见的几何不该在场里留痕
        fragColor = vec4(vxPattern(vUV) * 0.5 + 0.5, col);
        return;
    }
    fragColor = vec4(col, vFog);
}
)";
