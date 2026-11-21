#version 330 core
// ============================================================================
// blackhole1 —— 倾斜视角 + 视界环由粒子构成
//
// 上一版的毛病是"看得见盘子、看不见环"：它在极坐标正弦上叠了一层黑洞剪影，
// 结果黑洞只是个被挖掉的黑圆 —— 黑洞之所以是黑洞，在于【洞前面有东西】。
// 这一版把结构反过来：先有粒子的环，洞是环围出来的那块空。
//
// ── 为什么是"倾斜视角" ────────────────────────────────────────────────
//   正对着看的环是个圆，和"挖掉一个圆"分不出来。倾斜之后环投影成椭圆，
//   近侧压到洞的下方、远侧被引力抬到洞的上方 —— 洞的形状才被环【框】出来。
//
// ── 为什么视界环由【粒子】构成 ────────────────────────────────────────
//   实心的环一眼看得出是画上去的；粒子有疏密、有随机的半径抖动，
//   环就有了"在转"的重量。视界本身是球的剪影，任何角度看都是正圆，
//   所以它那圈粒子【不乘倾斜】—— 倾斜只由内层的盘体现。
//
// ── 三个踩过的坑（改这一版之前先读，别再犯）─────────────────────────
//
// ①【遮挡是"层"的操作，不是"屏"的操作】
//   曾经写成 acc *= smoothstep(FX_HZ, FX_HZ+0.008, length(p)) ——
//   这是一把整屏的刀，不问东西在洞【前】还是洞【后】，半径小于视界的一律抹掉。
//   倾斜椭圆的半短轴 = FX_R×tilt 落在视界内 → 上下两段被切光，
//   只有左右两个尖端活着（尖端在 0.105×520≈55px 处，正是当时图里那两簇孤点）。
//   → 遮挡只作用于【远侧】。近侧的环在洞前面，本来就该压在黑洞上画。
//   → 过渡带还要【窄】（1px）。给到 5px 会把珠子靠内的半边啃掉，每颗变彗星。
//
// ②【粒子被放到格子的边界上】
//       slot = floor(phi/TAU*n + 0.5);
//       cphi = (slot + 0.5)/n*TAU;      ← 错
//   floor(x+0.5) 已经把 slot 对齐到格子【中心】了，再加 0.5 就落到了边界上：
//   珠子的圆盘一半落进隔壁格子，隔壁画的是【另一颗】粒子，于是每颗只剩半边
//   → 朝外的楔形/火焰。正确是 cphi = slot/n*TAU  。这是历次"彗星"的真根子。
//
// ③【珠子半径被格子剪】
//   珠子必须量"离它自己的【屏幕坐标】多远"，而且半径要【小于格子切向半宽】，
//   否则切向被格子卡死、径向却按半径长 → 又被拉成径向的丝。
//   珠子大小【从颗数推】—— 想珠子大就减 FX_DENS，不要加大半径系数（会越界）。
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
#ifndef FX_ZOOM
#define FX_ZOOM 2.60     // 整体放大。1.0 时黑洞只占画面 1/5，像贴纸
#endif
#ifndef FX_TILT
#define FX_TILT 0.65     // 盘的倾斜（1=正视，0=侧视）。倾斜才看得见洞
#endif
#ifndef FX_DENS
#define FX_DENS 38.0     // 视界环的珠子颗数（@zoom=1）。珠子大小从这里推
#endif
#ifndef FX_HZ
#define FX_HZ 0.105      // 视界半径
#endif
#ifndef FX_R
#define FX_R 0.170       // 吸积盘半径（纸条环 + 尘埃带都跟着走）
#endif
#ifndef FX_HUE
#define FX_HUE 0.0       // 色相偏移
#endif
#ifndef FX_SPIN
#define FX_SPIN 1.0      // 自转速度倍率
#endif
#ifndef FX_GAIN
#define FX_GAIN 3.4      // 整体曝光。黑洞是黑底上的细碎光点，默认偏暗。
                         // 珠子在 1.0 时已饱和成纯白 —— 再往上加，变亮的是尘埃盘
#endif
#ifndef FX_BARLEN
#define FX_BARLEN 0.060  // 频谱条的最大行程（沿环平面径向向外）。0 = 珠子不跳
#endif
#ifndef FX_PERSP
#define FX_PERSP 1.2     // 洞周粒子环的透视强度。0 = 正交（屏幕正圆，珠子同速同大）。
                         // >0 时轨道有深度：近侧珠子更大更亮更散，远侧更小更挤更暗。
#endif
#ifndef FX_GAP
#define FX_GAP 0.40      // 纸条之间【留缝】的比例。0 = 首尾严丝合缝（会糊成一片），
                         // 0.25 = 每张只占格子的 75%，剩下 25% 是清晰的缝。
#endif
#ifndef FX_ROLL
#define FX_ROLL 20.0     // 【画面内的滚转】（度）—— 把整个盘【斜过来】摆。
                         // ★ 和 FX_TILT 是两回事，别混：
                         //   FX_TILT 是【前后倾】：盘绕水平轴转、y 被压扁，
                         //            椭圆的长轴永远是【水平】的。
                         //   FX_ROLL 是【画面内滚】：整个画面绕视线转一个角，
                         //            椭圆的长轴就【斜】过来了。
#endif
#ifndef FX_ORB
#define FX_ORB 0.30      // 角向几列。★ 列数少 = 每张纸条长，别调大
#endif
#ifndef FX_BAND
#define FX_BAND 0.050    // 环的【径向半宽】—— 纸条铺满的那条带子有多宽
#endif
#ifndef FX_LAYERS
#define FX_LAYERS 12.0   // 径向分几层。层数越多，纸条越细
#endif
#ifndef FX_LENMIN
#define FX_LENMIN 0.0417 // 最短的纸条 = 1/24 圆
#endif
#ifndef FX_LENMAX
#define FX_LENMAX 0.2500 // 最长的纸条 = 1/4 圆
#endif
#ifndef FX_SBRIGHT
#define FX_SBRIGHT 0.60  // 纸条亮度
                         // 发光面积比细线版大十几倍，所以要比它低一个数量级
#endif

const float TAU = 6.28318530718;

float hash11(float x){ x = fract(x*0.1031); x *= x+33.33; x *= x+x; return fract(x); }

// 吸积盘的温度色：内热白 → 橙 → 外圈暗红
vec3 diskTemp(float t)
{
    vec3 hot  = vec3(1.00, 0.96, 0.90);
    vec3 mid  = vec3(1.00, 0.58, 0.16);
    vec3 cool = vec3(0.45, 0.07, 0.03);
    return (t < 0.5) ? mix(hot, mid, t*2.0) : mix(mid, cool, (t-0.5)*2.0);
}

// ---------------------------------------------------------------------------
// 倾斜轨道粒子。倾斜角为 tilt 的圆轨道投影到屏幕是个椭圆。
// 采样：屏幕点 p → 盘方位角 → 定出最近的粒子 → 算出【那颗粒子的屏幕位置】
//       → d = |p − 那个位置|。珠子必定是正圆，形状与格子无关。
// gate: 0=全画  1=只近侧(屏幕下方)  2=只远侧
// ---------------------------------------------------------------------------
vec3 tiltRing(vec2 p, float spin, float tilt, float R, float num,
              float jit, float boost, int gate)
{
    float t0 = max(tilt, 0.05);
    float phi  = atan(p.y / t0, p.x) - spin;      // 屏幕 → 盘，再减自转
    float slot = floor(phi / TAU * num + 0.5);
    // ★ 格子是 [(slot−0.5),(slot+0.5))/num·TAU，中心在 slot/num·TAU。
    //   floor 已经把 slot 对齐到中心了 —— 这里再加 0.5 就是把粒子放到格子【边界】上，
    //   圆盘一半会被隔壁那颗粒子顶掉。见文件头 ②。
    float cphi = slot / num * TAU;

    float h1 = hash11(slot*1.7), h2 = hash11(slot*3.1+11.0), h3 = hash11(slot*5.3+29.0);

    float rp = R * (1.0 + jit*(h1 - 0.5));         // 半径抖动 → 环有厚度
    float ga = cphi + spin;                        // 盘方位角 → 屏幕方位角
    vec2  Pc = vec2(rp*cos(ga), rp*sin(ga)*t0);    // 粒子的【屏幕】位置
    float d  = length(p - Pc);                     // 屏幕距离 → 正圆

    // 珠子半径 = 相邻粒子屏幕间距的一半的一部分。k 必须 < 1/(1+TAU/(2·num))，
    // 否则圆盘越出格子、被格子剪成月牙（见文件头 ③）。想珠子大就减 num。
    float tang = length(vec2(sin(ga), cos(ga)*t0));   // 切向在该处的透视压缩
    float ps   = TAU/(2.0*num) * rp * tang * (0.42 + 0.28*h2);

    float keep = step(0.18, h3);                   // 22% 的格子空缺，环才透气
    float core = 1.0 - smoothstep(ps*0.72, ps, d); // 实心盘 —— 不是从圆心渐暗的球
    float halo = (1.0 - smoothstep(ps, ps*2.2, d)) * 0.10;
    float m    = (core + halo) * keep;

    // 用【粒子自己的】位置判前后，整颗珠子才一致（按像素判会把珠子劈开）
    float py = Pc.y;
    if      (gate == 1) m *= smoothstep(0.0, -0.004, py);
    else if (gate == 2) m *= smoothstep(0.0,  0.004, py);

    m *= 0.40 + 0.60*(0.5 - 0.5*sin(ga));          // 近侧更亮（透视）
    return m * diskTemp(clamp((rp - FX_HZ)/(R*1.35 - FX_HZ), 0.0, 1.0)) * boost;
}

// ---------------------------------------------------------------------------
// 屏幕正圆粒子环。球的剪影在任何角度都是圆，所以【不乘倾斜】。
// upperOnly: 1 = 只取上半
// ---------------------------------------------------------------------------
vec3 orbitRing(vec2 p, float spin, float tilt, float R, float num,
               float jit, float persp, float barLen, float boost)
{
    float t0 = max(tilt, 0.05);
    float sz = sqrt(max(0.0, 1.0 - t0*t0));      // 深度分量：倾角越小，轨道越"深"
    float D  = 0.60 / max(persp, 1e-3);          // 透视距离；persp→0 退化成正交

    // 挑最近的那颗珠子仍用正交近似（按方位角）—— 够准。
    // 【画】出来的位置和大小才是带透视的，这两件事分开。
    float phi  = atan(p.y / t0, p.x) - spin;
    float slot = floor(phi / TAU * num + 0.5);
    float cphi = slot / num * TAU;               // 格子中心（见 tiltRing 的注释）

    float h1 = hash11(slot*2.7+ 5.0), h2 = hash11(slot*4.1+41.0), h3 = hash11(slot*7.3+83.0);

    float rp0 = R * (1.0 + jit*(h1 - 0.5));      // 静止半径

    // ★ 频谱跳：每颗珠子沿【所在环平面的径向】向外弹。
    //   两条【不成比例】的正弦叠出无规律的跳 —— 单条正弦太规律，像呼吸不像频谱。
    //   没有音频时也看得见在动（靠时间），有音频时被推得更高。
    float T   = uTime + uHitDrive;
    float ph  = h1 * TAU;
    float lvl = 0.5 + 0.5*sin(T*1.9 + ph) * sin(T*0.73 + ph*2.3 + 1.7);
    lvl = pow(clamp(lvl, 0.0, 1.0), 2.0);        // 大部分时间贴着环，偶尔冲出去
    lvl *= 0.45 + 1.3*uBassAtt + 0.7*uTrebAtt;   // 音频再推一把
    float rp  = rp0 + max(lvl, 0.0) * barLen;    // 沿环平面径向向外

    float g  = cphi + spin;

    // 深度符号【按老版本保留】：Z = +rp·sin(g)·sz（和屏幕 y 同号）。
    // 这意味着透视把【屏幕上半】放大。物理上不对（近侧该是下半），
    // 但用户要的是昨天那个位置，所以这里不"修正"。
    float Ze = rp * sin(g) * sz;

    float k  = (persp > 0.0) ? D / max(D - Ze, 1e-3) : 1.0;  // 透视系数
    vec2  Pc = vec2(rp*cos(g), rp*sin(g)*t0) * k;            // 屏幕位置（带透视）

    float d  = length(p - Pc);
    // 珠子大小按【静止半径】算 —— 跳出去时珠子本身不变大。
    // 变大的话就成了"伸出来的柱子"，不是"沿径向跳的点"。
    float ps = TAU/(2.0*num) * rp0 * (0.42 + 0.28*h2) * k;
    float keep = step(0.14, h3);
    float core = 1.0 - smoothstep(ps*0.72, ps, d);
    float halo = (1.0 - smoothstep(ps, ps*2.2, d)) * 0.10;
    float m    = (core + halo) * keep;

    // ★ 真深度遮挡：这颗珠子【真的在洞后面】(Ze<0) 且【它的投影落在剪影内】→ 抹掉。
    //   两个条件都按【这颗珠子自己的几何】判 —— 不再切屏幕半平面。
    //   老写法 `mix(1.0, hzMask, smoothstep(-0.004,0.004,p.y))` 是一条横贯画面的
    //   半平面硬切线：它与盘的几何无关，所以会在环上斜着划出一道缝。
    // 遮挡：用【珠子自己的屏幕 y】判前后，和上面透视的符号【故意不一致】——
    // 老版本就是这个不一致（透视把上半当近、遮挡把上半当远），要复刻昨天的观感就得留着。
    // 若哪天要改成物理正确的，两处一起翻（透视用 -rp·sin(g)·sz，这里判 yb<0 为近）。
    float sil = smoothstep(FX_HZ - 0.0016, FX_HZ + 0.0016, length(Pc));  // 1 = 在剪影外
    float yb  = rp * sin(g) * t0;                                        // 珠子的屏幕 y
    float beh = smoothstep(0.0020, -0.0020, yb);                         // y<0(下半) → 1 → 可见
    m *= max(sil, beh);                                                  // 在剪影外 或 在前面 → 可见

    // 近侧更亮 —— 和"近侧更大"是同一件事的两面
    m *= clamp(k, 0.45, 1.6);
    return m * diskTemp(0.10 + 0.30*h1) * boost;
}

// ---------------------------------------------------------------------------
// 吸积盘：沿环【切向】的细线，不是珠子。
//
// 珠子和线的区别在"怎么量偏移"：
//   珠子 —— 量到圆心的距离，一个标量
//   线条 —— 分别量【角向】和【径向】两个方向，阈值差一个数量级
// 所以线天然贴着环的走向：角向长、径向极薄。
//
// 径向厚度要给【屏幕】单位：径向在屏幕上被 tilt 压扁过，
// 盘坐标里得先除以压缩系数，屏幕上才是等厚的。
// ---------------------------------------------------------------------------
vec3 tiltStrips(vec2 p, float spin, float tilt, float R, float num,
                float band, float layers, float lmin, float lmax, float boost)
{
    float t0 = max(tilt, 0.05);
    float sz = sqrt(max(0.0, 1.0 - t0*t0));   // 深度分量，真深度遮挡要用（同 orbitRing）
    vec2  q   = vec2(p.x, p.y / t0);
    float rad = length(q);
    float phi = atan(q.y, q.x) - spin;

    // ★ 格子宽度必须用【实际列数】算，不能用 num。
    //   num=29.64 时 floor(x+0.5) 在这段角区间上只能取到 30 个整数值 —— 一圈就是 30 列。
    //   若用 TAU/29.64 当间距，30 列铺出来是 1.012 圈：
    //     ① 多出的 1.2% 让切割线两侧的纸条中心错开 4.4°（位置对不上）
    //     ② 列号跨线时跳的不是整数格 → hash 拿到不同的种子（身份对不上）
    //   改用 TAU/Ncols → 正好铺满一圈，两侧位置和身份都接得上。
    float Ncols = floor(num + 0.5);              // 一圈实际几列
    float cellU = TAU / Ncols;                   // 纸条【中心】的角向间距
    float dv    = (2.0 * band) / layers;         // 径向行距
    float half  = (layers - 1.0) * 0.5;

    // ---- 径向：先定「第几层」（必须卡死，见下）----
    float v  = (rad - R) / dv + half;
    float k  = floor(v + 0.5);
    if (k < -0.5 || k > layers - 0.5) return vec3(0.0);
    float rc = R + (k - half) * dv;

    // 每层的接缝用黄金分割错开，否则所有缝连成一条竖线
    float offs = fract(k * 0.61803) * cellU;

    // 半长（弧度）：短 = 1/24 圆，长 = 1/4 圆
    float hMin = TAU * lmin * 0.5;
    float hMax = TAU * lmax * 0.5;

    float m = 0.0;

    // ★ 必须找遍【所有可能盖住这个点】的纸条。
    //   纸条长短不一之后，"最近的那一格"不再是答案：那一格的纸条可能很短、
    //   盖不住它自己的范围，而隔壁很长的那根却被跳过 → 画面出空洞。
    //   取 max 而不是相加 —— 重叠处才不会越叠越亮。
    // ★ 一圈【实际有几列】：floor(x+0.5) 在这段角区间上只能取到这么多个整数值。
    //   它和 num 不一样！num=29.64 时实际是 30 列、间距按 29.64 算 —— 于是多出 1.2% 的一圈。
    //   跨过 atan 的 ±π 切割线时列号跳的是 30，而 hash 非周期 → 同一个物理位置
    //   两侧拿到两套不同的随机数 → 那里就是那道断口（"起点和绕回来的终点对不上"）。

    for (int n = -8; n <= 8; ++n) {
        // 位置用【连续列号】，身份用【绕回 0 的列号】—— 两件事分开。
        // 位置不能绕：绕了会因为那 1.2% 把纸条挪位。身份必须绕：不绕就接不上。
        float jraw = floor((phi - offs) / cellU + 0.5) + float(n);
        float jj   = mod(jraw, Ncols);
        float hh = hash11(k*13.7 + jj*1.7);
        float hlen = mix(hMin, hMax, hh);        // 这根纸条多长
        // ★ 同层里每根【各自偏一点半径】。
        //   不偏的话，一层的纸条全落在同一个半径上，重叠起来糊成一条光滑的环
        //   （像土星环），长短根本看不出来。散开一根根才显形。
        float h3 = hash11(k*7.3 + jj*5.9);
        float rcj = rc + (h3 - 0.5) * dv * 0.85;
        float pc   = jraw * cellU + offs;        // 位置：用连续列号

        float dA = phi - pc;
        dA -= TAU * floor(dA / TAU + 0.5);       // 长纸条会跨 ±π，绕回来

        float gc = pc + spin;
        float tg = length(vec2(sin(gc), cos(gc)*t0));
        float rs = max(length(vec2(cos(gc), sin(gc)*t0)), 1e-3);

        // ★ 这张纸条【自己】的屏幕位置与深度（和 orbitRing 同一套约定：下近上远）
        vec2  Psc = vec2(rcj*cos(gc), rcj*sin(gc)*t0);
        float Zsc = -rcj * sin(gc) * sz;          // sz = sqrt(1-t0^2)，函数开头已算
        // 真深度遮挡：真的在洞后面 且 投影落在剪影内 → 这张抹掉
        float occ = max(smoothstep(FX_HZ - 0.0016, FX_HZ + 0.0016, length(Psc)),
                        smoothstep(-0.0020, 0.0020, Zsc));

        float aT = dA         * rcj * tg;        // 切向偏移（屏幕单位）
        float aR = (rad - rcj) * rs;             // 径向偏移（屏幕单位）
        // ★ 留缝：纸条【不占满】格子。占满的话相邻两张直接贴上，
        //   只靠明暗分界 —— 远看就是糊成一片（用户原话："那么糊"）。
        float fill = 1.0 - FX_GAP;
        float lenS   = hlen * rcj * tg * fill;   // 这张纸条的半长
        float thickS = (dv * 0.5) * rs * fill;   // 这张纸条的半厚
        if (lenS < 1e-6 || thickS < 1e-6) continue;

        // 圆角矩形 SDF（纸条两头是方的）
        float rr = min(thickS, lenS) * 0.25;
        vec2  d2 = abs(vec2(aT, aR)) - (vec2(lenS, thickS) - rr);
        float sd = length(max(d2, 0.0)) + min(max(d2.x, d2.y), 0.0) - rr;

        // ★ 实心条 + 【窄】抗锯齿 = 四条边清楚。
        //   之前两个都在毁边：
        //     ① 圆条着色把边缘按 eU/eV 压暗（角上只剩 0.1）→ 边本来就是暗的；
        //     ② 抗锯齿带 -0.0004..0.0006 = 1.8px → 再糊一道。
        //   现在里面是平的，只留一格抗锯齿。
        float aa = min(0.0006, min(thickS, lenS) * 0.40);
        float mask = 1.0 - smoothstep(-aa, aa, sd);
        if (mask <= 0.0) continue;

        // 每张纸条【整体】深浅不同 —— 靠这个区分，不靠把边压暗。
        float h2 = hash11(k*3.1 + jj*11.0);
        m = max(m, mask * (0.80 + 0.30*h2) * occ);
    }

    m *= 0.40 + 0.60*(0.5 - 0.5*sin(phi + spin));   // 近侧更亮（透视）
    return m * diskTemp(clamp((rc - FX_HZ)/(R*1.35 - FX_HZ), 0.0, 1.0)) * boost;
}

void main()
{
    float T = uTime + uHitDrive;              // 鼓点驱动的"抽插变速"也作用在自转上
    vec2  p = (gl_FragCoord.xy - 0.5 * uResolution.xy) / uResolution.y;
    p /= FX_ZOOM;

    // 画面内滚转。必须在算任何几何【之前】—— 它转的是整个画面。
    // （视界、光子环是正圆，转了看不出来；真正被转斜的是那个倾斜的吸积盘。）
    {
        float cr = cos(radians(FX_ROLL)), sr = sin(radians(FX_ROLL));
        p = vec2(cr*p.x - sr*p.y, sr*p.x + cr*p.y);
    }

    // 转速跟 MAIN 的 speed 滑块走。约定和同族效果一致（Azurite / Blessingsky 都是这句）：
    //   spd = uBpm/120 × uSpeed
    // 【为什么必须在这里自己乘】引擎的 g_plTime += dt 是【真实时间】，不跟 speed ——
    // 倍率是单独放进 uSpeed 传进来的，不算的话环的转速就永远恒定。
    // （ShaderLab 的离线台绑的是 uSpeed=1.0 / uBpm=120，所以预览里 spd=1.0，两边一致。）
    float spd  = uBpm / 120.0;
    float spin = -T * 0.12 * FX_SPIN * spd;
    float radP = length(p);

    // ★ 珠子颗数必须跟着缩放走：珠子半径是从「半格角 × 半径」推的，
    //   环变大而颗数不变 → 格子按比例变宽 → 珠子跟着变大变粗糙。
    float num = FX_DENS * FX_ZOOM;
    float kick = 1.0 + uHitKick * 1.1 + uBassAtt * 0.6;   // 鼓点把环点亮

    vec3 acc = vec3(0.0);

#if FX_STAGE >= 2
    // ---- 视界环：屏幕正圆，粒子排成 ----
    acc += orbitRing(p, spin, FX_TILT, FX_HZ*1.20, num, 0.030,
                     FX_PERSP, FX_BARLEN, 7.0 * kick);
#endif

#if FX_STAGE >= 3
    // ---- 吸积盘：压扁的倾斜环，由【沿环切向的细线】排成，不是珠子 ----
    // 画【整环】，前后遮挡交给下面那段统一处理：近侧（屏幕下方）在洞前面照画；
    // 远侧落进洞的剪影里的那截被遮掉，只有探出剪影的左右两段露出来。
    acc += tiltStrips(p, spin, FX_TILT, FX_R*1.10, num*FX_ORB,
                      FX_BAND, FX_LAYERS, FX_LENMIN, FX_LENMAX, FX_SBRIGHT * kick);
#endif

    // ========================================================================
    // ★ 遮挡【已经下沉到每个元素自己身上】了 —— orbitRing / tiltStrips 各按
    //   「自己的深度 Ze < 0 且在剪影内」判，见那两处的 `max(sil, beh)`。
    //
    //   原来这里是：
    //       float farSide = smoothstep(-0.004, 0.004, p.y);
    //       acc *= mix(1.0, hzMask, farSide);
    //   —— 一条【横贯整个画面的半平面硬切线】。它和盘的几何无关，所以会在环上
    //   斜着划出一道缝（用户报的"割裂感"就是这个）。改成逐元素按真深度判之后，
    //   边界跟着盘自己的几何走：在剪影【外】的远侧根本不会被抹，只有真的在
    //   洞后面、且投影落进剪影里的那些才消失。
    // ========================================================================

#if FX_STAGE >= 4
    // ---- 光子环：贴着剪影给一道底线，不抢粒子的戏 ----
    acc += vec3(0.85,0.90,1.00) * smoothstep(0.0016, 0.0, abs(radP - FX_HZ*1.03))
                                 * 0.42 * (1.0 + uHitKick*0.8);
#endif

    // 曝光。放在遮挡【之后】—— 不然会把被洞挡住的远侧一起点亮。
    acc *= FX_GAIN;
    acc = clamp(acc, 0.0, 1.0);
    // 色相旋转（只动 RG 平面，够用且不引进 hsl 依赖）。
    // 【不能写成 #if FX_HUE != 0.0】—— GLSL 的 #if 只吃整数常量表达式，
    // 浮点会报 "Syntax error in #if"。FX_HUE=0 时这里本来就是恒等变换。
    {
        float c = cos(FX_HUE), s = sin(FX_HUE);
        acc = vec3(acc.r*c - acc.g*s, acc.r*s + acc.g*c, acc.b);
    }

    // ---- 工程协议（别动）----
    float lum   = dot(acc, vec3(0.299, 0.587, 0.114));
    float pat   = clamp(lum * 2.4 - 0.35, -1.0, 1.0);   // 编码趟吐的标量场（给 morph）
    float cover = clamp(max(acc.r, max(acc.g, acc.b)), 0.0, 1.0);
    vec3  bg    = (uDarkBg > 0) ? vec3(0.0) : vec3(0.95);
    vec3  col   = mix(bg, acc, cover);

    // ★ 视界里头【不铺背景色】—— 洞是"没有光"，不是"没画东西"。
    //   而 cover 是从亮度反推的 alpha（工程协议），两者在洞里没法区分：
    //   洞内 acc=0 → cover=0 → mix 直接把 bg 露出来。
    //   深色模式 bg 本来就是黑 → 这一行在暗色下【完全等价于原样】，一点不改观感；
    //   只有浅色模式（bg=0.95）才起作用，把洞里压回黑，不然黑洞就成了"白洞"。
    {
        float shadow = 1.0 - smoothstep(FX_HZ - 0.0020, FX_HZ, radP);
        col = mix(col, mix(vec3(0.0), acc, cover), shadow);
    }

        col = audioGrade(col);
    col = cpuFinalize(col, gl_FragCoord.xy, uDitherStrength);
    fragColor = vec4(col, uOpacity);
}
