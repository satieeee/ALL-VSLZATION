#version 330 core
// ============================================================================
// Loxodrome —— 等角航线
//
// 【结构照着 rollingsolarsystem 的配方来】—— 那个效果在本工程里已验收过。
// 它头注释那句是关键："按博主那种函数密度搭，不是路线模板那种薄壳"。
//
//   层 0  对数极坐标      → 近处指数放大、远处收敛，中心成为奇点
//   层 1  复合正弦轨道带  → 半径/角度合成相位，sin 出"带"
//   层 2  谐波微扰        → 带的厚薄不规整
//   层 3  ★ 位置相关旋转  → 把【整个坐标空间】转起来，转量随位置变 → 剪切
//   层 4  ★ SDF 雕刻      → 在【被剪切后的空间 q】里求圆盘距离 → 节点骑在臂上
//   层 5  余弦调色 + 三相色散 + 奇点辉光
//
// ★ 我前面几版为什么不行：层 3 只是在相位里加了个角度（等效整体旋转，不改空间），
//   层 4 的"珠子"在【未旋转】的屏幕坐标里求距离 —— 所以永远和臂对不齐、浮在旁边。
//   这两层必须是一个整体：**先转空间，再在这个空间里放物体。**
// ============================================================================

in vec2 vUV; out vec4 fragColor;
uniform float uTime, uBpm, uSpeed; uniform vec2 uResolution;
uniform int uEncode, uDarkBg, uColorScheme;
uniform float uOpacity;
uniform sampler2D uOldEncoded;
uniform float uMorphBlend;

#ifndef FX_STAGE
#define FX_STAGE 4
#endif

// ---- 可扫描参数 -----------------------------------------------------------
#ifndef FX_DENS
#define FX_DENS 3.4      // 带的密度（沿对数半径）
#endif
#ifndef FX_TWIST
#define FX_TWIST 1.15    // 扭转：每单位对数半径转过多少弧度 → 决定螺旋的松紧
#endif
#ifndef FX_SYM
#define FX_SYM 5.0       // 极角倍频（对称阶数）
#endif
#ifndef FX_ROLL
#define FX_ROLL 1.0      // 层 3 的剪切强度
#endif
#ifndef FX_SUN
#define FX_SUN 0.045     // 中心核半径
#endif
#ifndef FX_NODER
#define FX_NODER 0.026   // 节点半径
#endif
#ifndef FX_HUE
#define FX_HUE 0.70      // 色相偏移
#endif
#ifndef FX_VIG
#define FX_VIG 1.75      // 暗角半径
#endif
// ★ 中心亮核（用户 2026-10-17 要求去掉："5，但不要中心亮球"）。
//   它来自两处：−ln|u| 在原点发散，以及层 4 那颗实心 core 盘。
//   关掉之后中心不能再空着 —— 对数极坐标在 r→0 处频率发散，会糊成一圈圈摩尔纹，
//   所以顺手把中心压黑（FX_COREHOLE），比让它亮着干净。
#ifndef FX_COREGLOW
#define FX_COREGLOW 0.0
#endif
#ifndef FX_COREHOLE
#define FX_COREHOLE 0.085   // 中心这个半径以内压黑
#endif

const float PI  = 3.14159265359;
const float TAU = 6.28318530718;

// 复合三角正弦：内层 cos 推动外层相位 → 陡脊平谷。
// sin(a) 在 a∈{1.5,2,3} 上远离零点，无除零风险。
float thc(float a, float b) { return sin(a * cos(b)) / sin(a); }

// 余弦调色板：一行产生连续光谱
vec3 pal(float t, vec3 d) { return 0.5 + 0.5 * sin(TAU * (t + d)); }

// 二维旋转
vec2 rot2(vec2 v, float ang) { float c = cos(ang), s = sin(ang); return mat2(c, -s, s, c) * v; }

// 圆 SDF：取负 → 正值 = 在内部，数值 = 离边界有多深
float disc(vec2 q, float r) { return r - length(q); }

void main()
{
    float T   = uTime + uHitDrive;
    float spd = uBpm / 120.0;

    vec2 p = (gl_FragCoord.xy - 0.5 * uResolution.xy) / uResolution.y;

    float field = 0.0;
    float glow  = 0.0;

    // ================= 层 0 · 对数极坐标 =================
    float rad = max(length(p), 1e-4);
    float th  = atan(p.y, p.x);
    float lr  = log(rad);
    float a   = th * FX_SYM;
    vec2  q   = p;                       // 会被层 3 剪切的坐标系

    // ================= 层 1 · 复合正弦的螺旋带 =================
#if FX_STAGE >= 1
    // 时间激活：atan 缓慢起幅，开篇不突变
    float act = min(1.0, atan(0.2 * T) / 0.95);
    act *= 1.0 + uBassAtt * 0.5;                       // 低频让整团一起呼吸

    // ★ 和 rollingsolarsystem 的区别就在这一行：相位里带上了极角 a，
    //   所以等相位线是【对数螺旋】而不是同心圆。
    float bands = (lr * FX_TWIST + a) * FX_DENS;
    bands *= 0.62 + 0.42 * act * thc(1.5, 3.0 * a + 2.0 * rad * 5.0 - T);
    field = sin(bands);
#endif

    // ================= 层 2 · 谐波微扰 =================
#if FX_STAGE >= 2
    float wob = thc(2.0, lr * 3.5 + a * 1.6 + T * 0.6);   // ★ 1.7→1.6：1.7×5=8.5 不是整数，会在分支切割处裂缝
    field += 0.45 * sin(bands * 1.7 + wob * 2.2);
    field *= 0.85 + 0.30 * thc(3.0, a * 2.4 - lr * 2.0);   // ★ 2.5→2.4：2.5×5=12.5 同上
#endif

    // ================= 层 3 · 位置相关旋转（★ 剪切出在这）=================
#if FX_STAGE >= 3
    // 旋转量随位置变化 → 内圈外圈转速不同 → 空间被剪切拉伸。
    // 【这是把盘变成漩涡的唯一方式，往相位里加角度做不到。】
    float h   = 1.0 + floor(3.0 * fract(0.07 * T));    // 1..3 阶梯：旋臂阶数突变
    float aa  = h * a;
    float ang = T * spd * 0.55 * FX_ROLL
              + 2.0 * aa
              + 5.8 * sin(2.6 * lr * FX_DENS + aa - T * 0.9);   // 差速剪切
    q = rot2(p, ang);
#endif

    // ================= 层 4 · SDF 雕刻（在剪切后的空间里）=================
#if FX_STAGE >= 4
    // 中心核：一个实心盘
    float core = (FX_COREGLOW > 0.5) ? disc(p, FX_SUN) : -1e9;

    // 节点：在【被剪切的空间 q】里布几个小盘 → 它们沿螺旋臂排布，
    // 而不是老实待在圆轨道上。这就是前面"珠子浮在线旁边"的解药。
    float nodes = -1e9;
    for (int i = 0; i < 7; i++) {
        float fi = float(i);
        float ra = 0.20 + 0.098 * fi;                     // 轨道半径
        float pa = fi * 2.399 + T * (0.55 - 0.045 * fi);  // 角速度随半径递减
        vec2  pp = vec2(cos(pa), sin(pa)) * ra;
        nodes = max(nodes, disc(q - pp, FX_NODER - 0.0021 * fi));
    }

    float deep = max(core, nodes);                        // 并集：正值 = 在实体内
    float edge = smoothstep(0.0, 0.006, deep);
    field = mix(field * 0.55, 1.0, edge);                 // 实体吃掉带
#endif

    // ================= 层 5 · 调色 / 色散 / 奇点 =================
    // 三相相位偏移：RGB 各偏 1/3 圈 → 三棱镜色散
    vec3 disp = vec3(0.0, 1.0, 2.0) / 3.0
              + mix(vec3(0.0), vec3(0.06, 0.12, 0.20), float(uColorScheme) / 5.0);

    float pt  = thc(2.0, field * 2.2 + lr * 1.6 - T * 0.7) - 0.5 * T + uTrebAtt * 0.8;
    vec3  col = pal(pt * 0.35 + FX_HUE, disp);

    col *= smoothstep(0.0, 0.15, FX_VIG - length(p));      // 暗角
    col *= smoothstep(0.0, FX_COREHOLE, length(p));        // 中心留黑（替代亮核）

    // 奇点辉光：ln|u| 在原点趋向 −∞ → 中心必然过曝成暖白。
    // 【把数学奇点当光源用】—— 这就是中心那颗星。
    glow  = smoothstep(0.0, 3.2, -lr) * FX_COREGLOW;
    glow  = max(glow, smoothstep(0.012, 0.0, length(p) - FX_SUN) * FX_COREGLOW);
    glow *= 1.0 + uBeatFlash * 0.9 + uHitKick * 1.2;       // 鼓点点亮中心
    col   = mix(col, vec3(1.0, 0.94, 0.88) * 2.0, glow);

#if FX_STAGE >= 4
    // 节点边缘补高光，让它们读得出来
    col += vec3(0.35, 0.55, 0.90) * smoothstep(0.012, 0.0, abs(deep)) * 0.55;
#endif

    // ---- 工程协议（别动）----
    float pat   = clamp(field, -1.0, 1.0);
    vec3  bg    = (uDarkBg > 0) ? vec3(0.0) : vec3(0.95);
    float cover = clamp(max(abs(field) * 0.9, glow), 0.0, 1.0);
    col = mix(bg, col, cover);

        col = audioGrade(col);
    col = cpuFinalize(col, gl_FragCoord.xy, uDitherStrength);
    fragColor = vec4(col, uOpacity);
}
