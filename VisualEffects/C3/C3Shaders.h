// ============================================================================
// C3Shaders
//
// Cubeeffect3 线段/折纸片着色器。离线台：_hittest/shader_test.cpp。
//
// 【为什么单独一个头】离线台要的是【实际跑的那一份】原文。以前靠 ExtractRaw()
// 从 main.cpp 里抓文本，台子因此依赖 main.cpp 的排版；改成 include 之后两边
// 拿到的是同一个变量，抄错/漏改这一类问题就不存在了。
//
// 【改了这个文件要重编离线台】—— 着色器文本是编进 exe 的。
// ============================================================================
#pragma once

static const char* kC3LineVS = R"(#version 330 core
layout(location=0) in vec2 aP;      // 归一化图像坐标 [0,1]，y 向下
layout(location=1) in float aKind;  // 整数部分=类型，小数部分=透明度
                                    //   0.x 网格线 | 1.x 坐标轴(不参与任何扭曲)
                                    //   2.x 折纸片纸面 | 3.x 纸片折痕   (x = 该顶点自己的透明度)
layout(location=2) in float aShade; // 折纸片【所在面的明暗系数】(线段那套 VAO 里此属性关闭，
                                    //   仅在 kind>=2 时读取 → 常量占位值不会被用到)
uniform float uAspect;              // 窗口宽高比 w/h
uniform float uImgA;                // 图像宽高比 W/H
uniform float uTime;
uniform float uExitTime;            // 出场计时（mesh 同款：切出时整体飞出）
uniform vec4  uVeil[32];            // 锚点：xyz=世界坐标, w=spawnT
uniform vec4  uVeilDir[32];         // 锚点：xyz=位移方向, w=幅度
uniform float uLayerPhase;          // 前进相位（CPU 积分；音频驱动 → 静音不前进）
uniform int   uLayer;               // 本层索引 0..LAYERS-1
uniform float uLayerZoom;           // 图层缩放（画布比参考大几倍 → 窗口只看中央 1/zoom，格子屏上大小不变）
uniform vec2  uLayerOfs;            // 【本层】在画布上取样窗口的偏移 → 各层看到迷宫的不同区域
out vec2 vP;                        // 传给 FS 做屏幕空间距离(世界平面坐标)
out float vAlpha;                   // 层深度淡入淡出 × 折纸片自身透明度
flat out int vKind;                 // 类型（不插值）：0 网格线 / 1 坐标轴 / 2 纸面 / 3 折痕
flat out float vShade;              // 折纸片所在面的明暗（不插值 → 面与面之间是硬边，正是折痕）
uniform int uIsBg;                  // 1=背景全屏三角（NDC 直通），0=线段

const int   LAYERS = 4;             // 同时可见的层数
const float FAR_Z  = -6.0;          // 最远层 z（浮现处）
const float NEAR_Z =  2.4;          // 最近层 z（飞过处；相机在 3.5）
const float SPAN   = NEAR_Z - FAR_Z;

void main(){
    // 背景层：g_vao 已是 NDC 全屏三角，直通
    if (uIsBg == 1) { gl_Position = vec4(aP, 0.0, 1.0); vP = aP; vAlpha = 1.0; vKind = 0; vShade = 1.0; return; }
    vShade = aShade;   // 线段顶点没这个属性 → 常量占位值，但只有 kind>=2 的片元会用它
    // 解包 aKind：floor 取类型（折纸片的编码恒 >= 2.0，浮点加法只会向上取整，不会掉到 1.x）
    int   kind   = int(floor(aKind));
    float ownA   = aKind - float(kind);      // 折纸片自己的透明度（网格线/轴为 0，不使用）
    bool  isAxis = (kind == 1);
    bool  isOrig = (kind >= 2);
    vKind = kind;
    // 图像坐标 → 世界平面（保持图像宽高比；翻转 y → 摆正）
    // uLayerZoom：画布比"参考尺寸"大几倍 → 只把中央 1/uLayerZoom 映到窗口，
    // 于是【单个格子屏上大小不变】，而图层向窗口外延伸（看不到网格边界）。
    vec2 worldScale = vec2(2.0, 2.0 / uImgA);
    vec2 pos = vec2(aP.x, 1.0 - aP.y);
    pos = (pos - 0.5) * uLayerZoom + 0.5;
    // 每层取迷宫【不同的窗口】：只做平面平移，不改变朝向/倾斜 ——
    // 于是 4 层看到的是迷宫的不同区域，而不是同一片网格叠 4 次。
    // 坐标轴不参与：它必须全局唯一、相对静止。
    if (!isAxis) pos += uLayerOfs;
    vec3 world = vec3((pos - 0.5) * worldScale, 0.0);

    // —— 坐标轴线段：全局唯一、固定在 z=0、不参与层推进、永不扭曲 ——
    //    （uLayer<0 时整个 draw 只画轴；见 RenderLines 的独立轴 pass）
    float z;
    if (isAxis) {
        z = 0.0;                                  // 轴固定在自己平面 → 相对静止
    } else {
        // 扭曲网 / 折纸片：本层 z 由相位循环决定（远浮现 → 近飞过 → 回到最远）。
        // 不做任何平面偏移/旋转 → 各层只是纵深不同，网格朝向一致。
        z = FAR_Z + mod(uLayerPhase + float(uLayer) * SPAN / float(LAYERS), SPAN);
    }

    // 鼓点：端点各自位移（锚点附近 → 折线）。
    // 坐标轴【不参与任何扭曲】；网格线与折纸片共用同一个扭曲场
    // → 格子被弹起时里面的纸片跟着一起动，不会和格子边线穿帮。
    if (!isAxis) {
        // R=0.24：与 4×3 网格间距(~0.5)匹配 → 相邻锚点的鼓包相互重叠，全屏无缝覆盖。
        const float R2 = 0.24*0.24;
        vec3 disp = vec3(0.0);
        const float R2OUT = R2 * 9.0;          // exp(-9)≈1e-4，再远可忽略 → 提前跳出（省 exp）
        for(int k=0;k<32;k++){
            vec4 v = uVeil[k];
            if(v.w <= 0.0) continue;
            vec2 dP = world.xy - v.xy;
            float d2 = dot(dP,dP);
            if(d2 > R2OUT) continue;
            vec3 dir = uVeilDir[k].xyz;
            if(length(dir) < 1e-4f) continue;
            float g = exp(-d2/R2);
            // age 必须夹到 >= 0：换效果时 CPU 会把效果时间归零，如果锚点还带着旧时间轴的
            // spawnT，age 就成了大负数 → exp(-age*4) 爆炸（age=-19.5 时 ≈ 7.6e33），
            // 整张网格被甩到无穷远 —— 表现就是切进去的瞬间全屏图案乱掉。
            // 夹住之后 trem ∈ [-1,1]，最坏情况只是留一个静态鼓包，绝不会爆。
            float age = max(uTime - v.w, 0.0);
            // 【先颤后定】的颤抖衰减：exp(-age × TREMBLE_DECAY)。
            // 只有这一项随时间变化（下面 1.0+trem 里的 1.0 是那个持久的新朝向，不衰减），
            // 所以"尾巴拖多长"完全由这个衰减率决定：
            //   4.0 → 时间常数 0.25s，到 0.75s 还剩 5%、1.0s 还剩 1.8%（肉眼约 1 秒才收干净）
            //   7.0 → 时间常数 0.143s，0.43s 就剩 5%、0.57s 剩 1.8% —— 尾巴缩短约 40%
            // 调大=颤得更短促，调小=颤得更久。
            const float TREMBLE_DECAY = 7.0;
            float trem = exp(-age*TREMBLE_DECAY)*sin(age*6.0+sin(age*13.0)*2.0);   // 先颤后定
            disp += dir * g * uVeilDir[k].w * (1.0 + trem);
        }
        world += disp * 0.18;
    }
    // 折痕【不再】做 z 偏移：纸面和折痕是同一趟里"先画三角、后画线"，深度测试没开，
    // 后画的自然压在上面。之前加的 z-=0.010 反而让折痕的透视缩放和纸面不同 → 又偏一点。
    world.z = z;

    // 深度淡入淡出：远处【很缓慢地】透过透明浮现、近处同理缓慢消失。
    // 用 smoothstep + 较长的过渡距离 → 渐入渐出都很柔和，不突兀。
    // 坐标轴恒定不淡（坐标系本身应该稳定可见）。
    if (isAxis) {
        vAlpha = 1.0;
    } else if (isOrig) {
        // 折纸片【不透明】：不跟图层深度淡入淡出，只用它自己的显化/淡出动画
        // （从折痕交点长出来 = 出现方式，被下一批顶掉 = 消失方式），
        // 所以纸片始终是一张实心纸，不会透出背后的网格。
        vAlpha = ownA;
    } else {
        // 过渡距离 2.5（原来 5.0）—— 整个深度跨度才 8.4，5.0 会导致【没有任何 z 能让
        // 两个 smoothstep 同时饱和】：vAlpha 的极限只有 0.867，即每一层都永远处在
        // 半透明过渡区 → 网格线被摊薄成灰糊。2.5 时 z∈[-3.5,-0.1] 这一段是全不透明，
        // 线才真正"实"起来（首尾仍然柔和地浮现/消失）。
        float fFar  = smoothstep(0.0, 2.5, z - FAR_Z);
        float fNear = smoothstep(0.0, 2.5, NEAR_Z - z);
        vAlpha = fFar * fNear;
    }

    // 出场动画：整体沿 +x 飞出（同 mesh 的切出语义）
    float dispOut = 0.0;
    if (uExitTime < 0.6) dispOut = pow(clamp(uExitTime/0.6, 0.0, 1.0), 2.0);
    world.x += dispOut * 6.0;

    // 透视相机（同 B376：camZ=3.5，NDC 缩放 k/m 用 contain-fit 贴窗口）
    float camZ = 3.5f;
    float k, m;
    float fitK = 3.5f, fitM = 3.5f * uImgA;
    if (uAspect * fitK >= fitM) { m = fitM;      k = m / uAspect; }
    else                        { k = fitK;      m = k * uAspect; }
    vec3 v = world - vec3(0.0, 0.0, camZ);
    float n = 1.0, far = 8.0;
    gl_Position = vec4(v.x*k, v.y*m, (far+n)/(n-far)*v.z + 2.0*far*n/(n-far), -v.z);
    vP = world.xy;
}
)";

static const char* kC3LineFS = R"(#version 330 core
in vec2 vP;
in float vAlpha;            // 层深度淡入淡出（远浮现/近飞过）× 折纸片自身透明度
flat in int vKind;          // 0 网格线 / 1 坐标轴 / 2 折纸片纸面 / 3 折痕
flat in float vShade;       // 折纸片所在面的明暗系数
out vec4 fragColor;
uniform int   uIsBg;        // 1=画背景底(全屏三角), 0=画线段
uniform float uOpacity;
uniform vec2  uResolution;
// 背景/线条颜色按 after.png 实测校准：
//   背景是【径向渐变：中心黑(0.138) → 四周泛灰(0.276)】，中性灰无偏色（实测分带 0.138→0.276）。
//   线条 均值 0.362（低对比、若隐若现）。
// 径向渐变按 after.png 实测分带拟合：中心 0.137 → 边缘 0.276（r≥0.78 后饱和）。
//   拟合式 lum = 0.137 + 0.139 * (r/0.78)^1.44，与实测各分带误差 <0.01。
uniform int uLightMode;     // 1 = Cubeeffect3 (light) 浅色模式；0 = (dark) 深色模式
// —— 两套调色板 ——
// 深色：背景近黑、线是浅灰（比背景亮）、纸比背景亮。
// 浅色：背景近白、线是深灰（比背景暗）、纸比背景暗 —— 关系完全反过来，
//       所以纸看起来像白桌面上的纸，面板明暗和折痕的读法和深色模式一模一样。
// 线条颜色【向背景靠拢】：线是半透明叠在背景上的，看的是叠加后的实际亮度差，
// 不是颜色常数本身。按 kLineOpacity=0.45 算，实际亮度 = 线色×0.45 + 背景×0.55：
//   改前 dark 0.196（bg 0.030 → 差 0.166）、light 0.635（bg 0.975 → 差 0.340）
//   —— 浅色模式的对比度是深色模式的两倍，两边本来就不一致，画面显得杂乱。
//   改后 dark 0.150（差 0.120）、light 0.855（差 0.120），两边拉平且整体更收敛。
// 还嫌扎眼就继续往背景靠：dark 往下调、light 往上调（折纸片走另一个分支，不受影响）。
const vec3 LINE_COL_D  = vec3(0.30, 0.30, 0.31);      // 深色模式线色（背景 ~0.030~0.100）
const vec3 LINE_COL_L  = vec3(0.70, 0.70, 0.69);      // 浅色模式线色 = 1 - LINE_COL_D（严格反相）
const vec3 PAPER_COL_D = vec3(0.62, 0.62, 0.64);      // 纸面基色（纸面和折痕都乘自己的 vShade）
void main(){
    if (uIsBg == 1) {
        // "中心黑、四周泛灰"的径向渐变，以短边为基准的圆。
        // 梯度形状按 after.png 实测拟合（pow 1.44 与饱和半径 0.78），但【整体压暗】：
        // 原来中心 0.137 / 边缘 0.276 —— 画面上没有任何真正的黑，一切都"抬着"，
        // 加上细线本来就暗，整个画面被挤在 35~70 这一小段里 → 发闷、发灰。
        // 现在中心 0.03 / 边缘 0.10，动态范围才打得开。
        vec2 p = (gl_FragCoord.xy - 0.5 * uResolution) / uResolution.y;   // y∈[-0.5,0.5]
        float r = clamp(length(p) * 2.0, 0.0, 1.0);                       // 0=中心 1=边缘
        float t = clamp(r / 0.78, 0.0, 1.0);
        float g = pow(t, 1.44);
        // 深色：中心最黑 0.030 → 边缘 0.100。浅色 = 严格反相：0.970 - 0.070g。
        float lum = (uLightMode == 1) ? (0.970 - 0.070 * g) : (0.030 + 0.070 * g);
        fragColor = vec4(vec3(lum), uOpacity);
        return;
    }
    // 折纸片：不透明实心纸。纸面与折痕用同一条公式，区别只在 vShade ——
    // 纸面 = 所在面的明暗；折痕 = 四个面平均值 × 0.34（更深）→ 折痕是【折痕阴影】不是亮线。
    //
    // 浅色模式 = 深色模式的【严格负片】：整张纸（纸面和折痕）一起取反 1-x。
    //   · 亮面变暗面、暗面变亮面 → 光的方向跟着镜像；
    //   · 折痕从"纸上的暗线"变成"纸上的亮线"，也是反相的结果；
    //   · 纸片落在 0.03~0.68，压在 0.90~0.97 的背景上 = 深色模式亮纸压暗底的镜像。
    // 之前只是把基色 0.62 改成 0.55，两种模式下的纸几乎是同一个灰，等于没反色。
    if (vKind >= 2) {
        vec3 pc = (uLightMode == 1) ? (vec3(1.0) - PAPER_COL_D * vShade)
                                    : (PAPER_COL_D * vShade);
        fragColor = vec4(pc, uOpacity * vAlpha);
        return;
    }
    // 扭曲线与坐标轴：在原有基础上【再加 50% 透明度】（0.9 → 0.45）。
    // 坐标轴的 vAlpha 恒为 1，所以这一条同时覆盖"所有扭曲线"和"坐标轴"。
    // 折纸片走上面的分支，不受影响。
    const float kLineOpacity = 0.45;
    fragColor = vec4((uLightMode == 1) ? LINE_COL_L : LINE_COL_D,
                     uOpacity * kLineOpacity * vAlpha);
}
)";
