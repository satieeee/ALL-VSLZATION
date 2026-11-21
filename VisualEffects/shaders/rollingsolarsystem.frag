#version 330 core
// ============================================================================
// rollingsolarsystem —— 滚动太阳系
//
// 按【博主那种函数密度】搭，不是路线模板那种薄壳。五层各自干一件重活：
//
//   01 对数极坐标      r = ln|u|            → 轨道 = 同心环，近处指数放大
//   02 复合正弦 thc    thc(a,b)=sin(a·cos b)/sin a
//                                           → 轨道带非线性、陡脊平谷
//   03 位置相关旋转    u' = R(T + 2hθ + A·sin(k·lr·D + θ − T))·u
//                                           → ★ 空间被剪切，"滚动"就出在这层
//   04 SDF 雕刻        恒星 + 行星节点，在【旋转后的空间】里求距离
//   05 余弦调色板 + 三相色散 + 奇点辉光
//
// 与路线模板的差别就在 02 和 03：那两层才是"液态"的来源。少任何一层，
// 出来都是"一个盘"，不是"滚动的星系"。
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

// ---- 可扫描参数 ----
#ifndef FX_SCALE
#define FX_SCALE 1.0        // 整体尺度（实体的）
#endif
#ifndef FX_DENS
#define FX_DENS 6.0         // 轨道密度
#endif
#ifndef FX_ROLL
#define FX_ROLL 1.0         // 滚动强度：层 3 的剪切量
#endif
#ifndef FX_HUE
#define FX_HUE 0.0          // 色相偏移
#endif
#ifndef FX_SUN
#define FX_SUN 0.075        // 恒星半径
#endif

const float PI  = 3.14159265359;
const float TAU = 6.28318530718;

// ---- 算子 -----------------------------------------------------------------
// 复合三角正弦：内层 cos 驱动外层相位 → 陡峭峰脊 + 平缓谷底。
// 这是把普通正弦变成流体的关键。sin(a) 在 a∈{1,1.5,2,3,4} 上远离零点，无除零。
float thc(float a, float b) { return sin(a * cos(b)) / sin(a); }

// 余弦调色板（IQ）：一行产生连续无限的彩虹光谱
vec3 pal(float t, vec3 d) { return 0.5 + 0.5 * sin(TAU * (t + d)); }

// 二维旋转
vec2 rot2(vec2 v, float ang) { float c = cos(ang), s = sin(ang); return mat2(c, -s, s, c) * v; }

// 圆 SDF。取负 → 正值 = 在内部，数值 = 离边界有多深。
float disc(vec2 q, float r) { return r - length(q); }

void main()
{
    float T   = uTime + uHitDrive;                 // ← 抽插变速：累积相位，不跳回
    float spd = uBpm / 120.0;

    vec2 p = (gl_FragCoord.xy - 0.5 * uResolution.xy) / uResolution.y;

    float field = 0.0;
    float glow  = 0.0;

    // ================= 层 0 · 对数极坐标 =================
    // 把平面换成对数极坐标平面：近处指数放大、远处平滑收敛，中心成为引力奇点。
    // 轨道 = 同心环 —— 这是"星系"唯一自然的坐标系。
    float rad = max(length(p), 1e-4);
    float th  = atan(p.y, p.x);
    float lr  = log(rad);
    float a   = th * 2.0;                          // 极角倍频 → 二重对称
    vec2  q   = p;                                 // 会被层 3 剪切的空间

    // ================= 层 1 · 复合正弦的轨道带 =================
#if FX_STAGE >= 1
    // 时间激活：atan 缓慢起幅，开篇不突变（博主的工程细节）
    float act = min(1.0, atan(0.2 * T) / 0.95);
    act *= 1.0 + uBassAtt * 0.5;                   // ← 低频：整团轨道一起呼吸

    // thc 让半径振荡变成非线性 → 轨道有"厚薄"，不是等宽的印刷圈
    float bands = lr * FX_DENS;
    bands *= 0.62 + 0.42 * act * thc(1.5, 3.0 * a + 2.0 * rad * 5.0 - T);
    field = sin(bands);
#endif

    // ================= 层 2 · 谐波微扰（轨道不规整）=================
#if FX_STAGE >= 2
    float wob = thc(2.0, lr * 3.5 + a * 1.7 + T * 0.6);
    field += 0.45 * sin(bands * 1.7 + wob * 2.2);
    field *= 0.85 + 0.30 * thc(3.0, a * 2.5 - lr * 2.0);
#endif

    // ================= 层 3 · 位置相关旋转（"滚动"出在这）=================
#if FX_STAGE >= 3
    // 旋转量随位置变化 → 内圈外圈转速不同 → 空间被剪切拉伸。
    // 【这是把盘变成漩涡的唯一方式，正弦叠加做不到。】
    float h   = 1.0 + floor(3.0 * fract(0.07 * T));        // 1..3 阶梯：旋臂阶数突变
    float aa  = h * a;
    float ang = T * spd * 0.55 * FX_ROLL
              + 2.0 * aa
              + 5.8 * sin(2.6 * lr * FX_DENS + aa - T * 0.9);   // ← 差速剪切
    q = rot2(p, ang);
#endif

    // ================= 层 4 · SDF 雕刻（在剪切后的空间里）=================
#if FX_STAGE >= 4
    // 恒星：中心实心盘
    float sun = disc(p, FX_SUN * FX_SCALE);

    // 行星节点：在【被剪切的空间】里布几个小盘 → 它们会沿旋臂排布，
    // 而不是老实待在圆轨道上
    float planets = -1e9;
    for (int i = 0; i < 6; i++) {
        float fi = float(i);
        float ra = 0.22 + 0.105 * fi;                       // 轨道半径
        float pa = fi * 2.399 + T * (0.55 - 0.045 * fi);    // 角速度随轨道递减
        vec2  pp = vec2(cos(pa), sin(pa)) * ra;
        planets  = max(planets, disc(q - pp, 0.030 - 0.0022 * fi));
    }

    float deep = max(sun, planets);          // 并集：正值 = 在实体内
    float edge = smoothstep(0.0, 0.006, deep);
    field = mix(field * 0.55, 1.0, edge);    // 实体吃掉轨道场
#endif

    // ================= 层 5 · 余弦调色 + 色散 + 奇点辉光 =================
    // 三相相位偏移：RGB 各偏 1/3 圈 → 镭射 / 薄膜干涉 / 三棱镜色散
    vec3 disp = vec3(0.0, 1.0, 2.0) / 3.0
              + mix(vec3(0.0), vec3(0.06, 0.12, 0.20), float(uColorScheme) / 5.0);

    float pt  = thc(2.0, field * 2.2 + lr * 1.6 - T * 0.7) - 0.5 * T + uTrebAtt * 0.8;
    vec3  col = pal(pt * 0.35 + FX_HUE, disp);

    col *= smoothstep(0.0, 0.15, 1.75 - length(p));          // 暗角：远场收束

    // 奇点辉光：lr = ln|u| 在原点趋向 -∞ → 中心必然过曝成暖白。
    // 【把数学奇点当光源用】—— 这就是"恒星"。
    glow  = smoothstep(0.0, 3.2, -lr);
    glow  = max(glow, smoothstep(0.012, 0.0, length(p) - FX_SUN * FX_SCALE));
    glow *= 1.0 + uBeatFlash * 0.9 + uHitKick * 1.2;         // ← 鼓点点亮恒星
    col   = mix(col, vec3(1.0, 0.94, 0.88) * 2.0, glow);

#if FX_STAGE >= 4
    // 实体边缘补高光，让行星读得出来
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
