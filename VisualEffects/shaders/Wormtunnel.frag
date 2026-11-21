#version 330 core
// ============================================================================
// Wormtunnel —— 飞船穿越扭曲星际虫洞隧道
//
// ── 隧道：光线步进 SDF（真 3D）──────────────────────────────────────
//   相机在原点、沿 +z 看，隧道是一根【倾斜 + 弯曲】的管子：
//     · 倾斜  →  灭点偏离画面中心  = 对角构图（拿 FX_TILTX/FX_TILTY 调）
//     · 弯曲  →  管子扭起来        = 虫洞感
//     · 半径呼吸  →  喉部一收一放
//
// ── 隧道壁碎片场：真 3D 矩形面片（不是屏幕极坐标画条）────────────────
//   背景 = 贴在管壁上的一层【矩形板】。每块在世界里定成四个角，四角投影到
//   屏幕后做"点在凸四边形内"的判定 —— 平面矩形透视后仍是凸四边形，所以
//   这是精确的，不需要步进。近大远小、所有边收向同一个灭点，都是投影的副产品。
//
//   ★ 编队必须打在【屏幕极坐标 (u, θ)】上，不是打在管壁的 (θ, z) 上：
//     · 按 z 均匀撒 → 屏幕面积密度 ∝ 1/z³，板全堆在灭点，外侧是大洞；
//     · 按 θ 铺满整圈 → 相机只看得到约 1.5 弧度的扇区，76% 的板生在画外。
//     两个坑都踩过。打在 (u, θ) 上覆盖才是【按定义】的，每格解出自己的深度。
//
//   ★ 灭点是【恒定出画】的：管轴带 FX_TILT（模长 0.78），所以可见管壁
//     只落在 z ≈ 1.5~8（t = 2.2/|p·FOV − tilt|）。深度铺到 90 是撒在画外。
//
// ── 一层层看 ─────────────────────────────────────────────────────────
//   --stage 1  只有隧道本体（明暗，没有碎片）
//   --stage 2  + 壁面碎片场 + 光照场
//   --stage 3  + 飞船、尾焰、机翼激光（hit 时）
//   --stage 4  + 曝光与调色（完整）
// ============================================================================

in vec2 vUV; out vec4 fragColor;
uniform float uTime, uBpm, uSpeed; uniform vec2 uResolution;
uniform int uEncode, uDarkBg, uColorScheme;
uniform float uOpacity;
uniform sampler2D uOldEncoded;
uniform sampler2D uShipTex;      // 飞船贴图集（32 帧横排，由 main.cpp 加载 32 位 BMP）
uniform float uMorphBlend;

#ifndef FX_STAGE
#define FX_STAGE 4
#endif

// ---- 隧道形状 ----
#ifndef FX_TUBER
#define FX_TUBER 2.20    // 隧道半径。★ 开大 → 墙近乎平、条近乎平行（参考图就是这个）
                         //   开小 → 细管子，条会明显收向灭点
#endif
#ifndef FX_FOV
#define FX_FOV 0.60      // 视野。★ 越小越像【长焦】：条越平行、越接近参考图。
                         //   1.0 = 广角（26°半角），条会明显收向灭点；0.3 ≈ 8°。
#endif
#ifndef FX_TILTX
#define FX_TILTX -0.508   // 灭点方向 = atan2(TILTY, TILTX)。
#endif
#ifndef FX_TILTY
// ★ 灭点是【照参考图量的】：结构张量测出参考图拖星走向 49.4°(图像坐标)
//   → 灭点在左上，方向 130.6°；强度 0.78 让灭点足够出画 → 条近乎平行。
//   改这两个数就要重算 FX_SHIPROT（见那里的注释）。
#endif
#ifndef FX_TILTY
#define FX_TILTY 0.592   // 轴的纵向倾斜 → 灭点偏上/下
#endif
#ifndef FX_BEND
#define FX_BEND 0.10     // 轴的弯曲幅度 → 扭曲感
#endif

// ---- 隧道壁碎片（真 3D 矩形面片场）----
//   每块板 = 深度 zc、环向角 θ、离壁漂浮量 dr、轴向半长 L、环向弧半宽 W、切平面内自转 ρ。
//   四角投影到屏幕 → 屏幕空间判定 → 透视是【算出来的】，不是调出来的。
#ifndef FX_NPANEL
#define FX_NPANEL 360     // 面片总数 = FX_NCOL × 径向槽数（编译期常量 = 循环上界）
#endif
#ifndef FX_NCOL
#define FX_NCOL 60.0      // 环向分几【列】（铺满那个可见扇区，不是铺满整圈）
                          //   ★ 列数决定【条有多细】；径向槽数 = 总数/列数，决定【条有多长】
#endif
#ifndef FX_WEDGE
#define FX_WEDGE 1.75     // ★ 相机实际看得到的管壁【扇区宽度】（弧度）。
                          //   几何上算出来约 1.5；给点余量。铺满整圈的话
                          //   76% 的板生在画外 —— 那才是"铺不满"的真正原因。
#endif
#ifndef FX_PU0
#define FX_PU0 0.35       // 离灭点的屏幕半径：远端（≈0.35 挨着灭点）
#endif
#ifndef FX_PU1
#define FX_PU1 2.20       // 离灭点的屏幕半径：近端（≈2.2 顶到画框）
#endif
#ifndef FX_PLFILL
#define FX_PLFILL 1.25    // 板占它那条【径向槽】的比例 —— 这才定"条有多长"。
                          //   长宽比不是旋钮：等于 (槽的半径比 − 1)/列角，是算出来的
#endif
#ifndef FX_PWMIN
#define FX_PWMIN 0.06     // ★ 最细的条 = 列宽 × 它（发丝）
#endif
#ifndef FX_PWMAX
#define FX_PWMAX 2.00     // ★ 最宽的条 = 列宽 × 它（宽带）。只有极少数才到这个量级
#endif
#ifndef FX_PWPOW
#define FX_PWPOW 8.00     // 尾巴多重：1 = 均匀；越大越"大部分极细 + 偶发一条很宽"
#endif
//   ★ 宽度的分布形态，参考图实测（除掉透视后）p10=0.8 / 中位 4.0 / p90=51 px。
//     p90/中位 = 12.8 而 p10/中位 = 0.20 —— 这是【重尾】，不是对称分布。
//     两个都试过：
//       · "列宽 ± 20%"          → 跨度 1.67×，每根条一样粗（用户一眼看出）
//       · 对称的对数正态 exp()  → 跨度够了，但平均覆盖率 1.19，宽板一重叠
//                                 亮段就并成一条更宽的 → 实测中位反而从 7.8 涨到 13.5
//     必须在【对数】上撒 + 【重尾】：宽度 = 列宽 × (PWMIN + PWMAX·h^PWPOW)。
//     均值 = PWMIN + PWMAX/(PWPOW+1) —— 这个才是覆盖率，别用中位去估。
#ifndef FX_PLSIZE
#define FX_PLSIZE 1.60    // 长度的离散度（同上，指数撒点）
#endif
#ifndef FX_PEMITRATE
#define FX_PEMITRATE 0.55 // ★ 有多少比例的【细】条是【发光体】。概率 ∝ sqrt(最细/本宽)：
                          //   最细的那些约 55% 会发光，最宽的那些只有 ~9%。
                          //   参考图里"宽度≤3px 且峰值≥0.6"的像素有 257 个，我原来是 0 个。
#endif
#ifndef FX_PEMITBOOST
#define FX_PEMITBOOST 1.55// 发光体的亮度倍率
#endif
#ifndef FX_PEMITGLOW
#define FX_PEMITGLOW 2.00 // 发光体的【溢光半径】倍率。荧光要渗得远：
                          //   参考图离亮段 40~80px 处还有 0.106 的底，我只剩 0.051；
                          //   很远的地方参考 0.086、我 0.024（缝是死的）。
#endif
#ifndef FX_PGLOWK
#define FX_PGLOWK 1.60    // 溢光半径 = 板的【屏幕宽度】× 它（比例化，不是屏幕固定值）
#endif
#ifndef FX_PFOG
#define FX_PFOG 0.30      // ★ 距离雾。灭点方向的管壁【很深】(z≈10)，
                          //   而荧光的溢光是会【累加】的：几十列的荧光全堆进灭点附近
                          //   那块趋近于 0 的屏幕面积 → 过曝成一团白。
                          //   参考图灭点那边恰恰是最暗的（块亮度 0.09~0.20），靠雾压下去。
#endif
#ifndef FX_PGLOW
#define FX_PGLOW 7.00     // 溢光半径 = 抗锯齿带 × 它。★ 这个数【会改量到的宽度】：
                          //   溢光铺开 7×1.8px ≈ 13px，把每根细条两侧各加宽几像素 →
                          //   量出来的宽度被系统性放大。所以"量到的中位宽度"里有一部分
                          //   是溢光不是条。调宽度分布之前要先把这个定死。
                          //   用户在两个候选里挑了大溢光（7.0）不要小的（2.5）。
#endif
#ifndef FX_PDR
#define FX_PDR 0.60       // ★ 游离：板离开壁面、往管【内】浮的最大距离
#endif
#ifndef FX_PROLL
#define FX_PROLL 0.00     // 板绕自身法线的自转幅度（弧度）。★★ 默认必须是 0。
                          //   每块板给【独立随机】的自转 = 定向场变成白噪声 →
                          //   画面上到处都是交叉的棒子，而参考图里【没有交叉】
                          //   （放大看是一族平行条 + 一个缓慢的扇形）。
                          //   这个坑藏得很深：角度的【直方图】和参考图几乎一样
                          //   （散度 28.5° vs 29.5°），分布对上了，场却不对 ——
                          //   参考图的角度变化是【空间平滑】的（同一个 3D 方向的投影），
                          //   邻居必然平行。判据要用「相邻像素定向差」，不是角度直方图。
#endif
#ifndef FX_PFLOW
#define FX_PFLOW 1.00     // 板往相机冲的速度倍率
#endif
#ifndef FX_PEXPO
#define FX_PEXPO 5.50     // 面片整体曝光
#endif
#ifndef FX_DISP
#define FX_DISP 0.0035    // 每通道绕灭点的旋转角（弧度）→ 色散边缘
#endif

// ---- 运动 ----
#ifndef FX_SPEED
#define FX_SPEED 1.00    // 前进速度倍率（加速感的主旋钮）
#endif

// ---- 飞船 ----
#ifndef FX_SHIPX
#define FX_SHIPX 0.430  // 位置（屏幕单位）。★ 这六个值是用 shiptune\index.html 调出来的
#endif
#ifndef FX_SHIPY
#define FX_SHIPY -0.085
#endif
#ifndef FX_SHIPZ
#define FX_SHIPZ 1.95    // 离相机多远 → 决定它多大
#endif
#ifndef FX_SHIPSZ
#define FX_SHIPSZ 0.364  // 机身尺寸【屏幕单位】★ 0.320 → 0.364 (2026-10-16)：
                         //   图集里船占格子的比例从 0.50 收到 0.44 给辉光留边，
                         //   乘 0.50/0.44 补回来，船在屏幕上的实际大小没变。
#endif
#ifndef FX_SHIPYAW
#define FX_SHIPYAW 0.34  // 偏航：机头朝灭点那边偏
#endif
#ifndef FX_SHIPPITCH
#define FX_SHIPPITCH 0.06
#endif
#ifndef FX_SHIPROLL
#define FX_SHIPROLL 0.10
#endif
#ifndef FX_SHIPVIEW
#define FX_SHIPVIEW -0.198 // 相机绕飞船的方位（0 = 正后方）→ 选图集里的哪一【列】
#endif
#ifndef FX_SHIPELEV
#define FX_SHIPELEV 0.00  // ★ 仰角档 0..1 → 选图集里的哪一【行】（0 = 从下往上看）
                          //   0.40 → 第 2 行 = 20°。以前图集只有一行、仰角根本调不了。
#endif
#ifndef FX_SHIPNEON
#define FX_SHIPNEON 4.0   // 霓虹增益（1.0 = 关）。★ 只放大【亮部】，暗甲板原样不动。
                          //   为什么不能用整体亮度增益：图集是 8bit（最高只能存 1.0），
                          //   而隧道背景本身就压在 0.8~1.0，霓虹不放大就永远压不过背景。
                          //   但整体乘 K 会连【暗部一起抬】（0.02×4.5→ACEZ 0.11 → 发灰），
                          //   而且把已经偏亮的材质（铬）直接吹白。
                          //   所以用【按最大分量的软阈值】放大：甲板纹丝不动，
                          //   只有本来就亮的霓虹被顶进 HDR，过层 4 的 ACES 后
                          //   亮度 0.80→0.98，色相还保住（是乘倍数，不是加白光）。
#endif
#ifndef FX_FLAMELEN
#define FX_FLAMELEN 1.5  // 尾焰长度（飞船本地坐标：机身约 2 单位高）
#endif
#ifndef FX_FLAMEW
#define FX_FLAMEW 0.28   // 尾焰出口半径
#endif
// ---- 机翼激光（hit 时发射）----
//   机身局部坐标：机头 = +y，两翼尖 = ±x（用标记点投回画面验证过：
//   ±x 正好落在两个翼尖上，+y 落在机身外 —— 正面盯着看的那个窄高块是【尾翼】，
//   图集第 16 列是正后方视图，所以是【从船尾后方看】，机头朝画面里）。
//   ★ 机头方向 = 朝灭点 = 管轴方向 = 管壁方向 —— 所以"向机身前方"和
//     "平行于管壁"是同一件事，两个要求不冲突。
#ifndef FX_LB_ON
#define FX_LB_ON 1.0      // 总开关（0 = 完全不画，方便对比）
#endif
#ifndef FX_LBX
#define FX_LBX 0.92       // 炮口在翼尖的局部 x（±）
#endif
#ifndef FX_LBY
#define FX_LBY 0.02       // 炮口的局部 y（0 = 翼展最宽处，往 +y 是机头方向）
#endif
#ifndef FX_LBN
#define FX_LBN 4          // ★ 每侧【同时在飞】几颗子弹
#endif
#ifndef FX_LBRATE
#define FX_LBRATE 1.55    // 子弹的飞行速率（每颗自己跑完全程用的秒数的倒数）
#endif
#ifndef FX_LBRANGE
#define FX_LBRANGE 2.20   // 子弹能飞出多远（世界单位）。
                          //   ★ 不能开大：灭点在【画框上方】(y=+0.99 vs 框顶 0.5)，
                          //     弹道朝灭点走，ph>0.25 就飞出上边框了 —— 射程给 7 时
                          //     只有刚出膛的那一颗看得见。2.2 正好让它从翼尖飞到框边收尾。
#endif
#ifndef FX_LBLEN
#define FX_LBLEN 0.075    // 子弹在屏幕上的长度（炮口处；越远按透视收缩）
#endif
#ifndef FX_LBW
#define FX_LBW 0.0150     // 子弹在屏幕上的半宽（炮口处）。1.0 屏幕单位 = 画面高，
                          //   所以 0.015 ≈ 7px 半宽 / 14px 粗。（原 0.0090 ≈ 8px 粗）
#endif
#ifndef FX_LBGLOW
#define FX_LBGLOW 4.20    // 子弹整体亮度。（原 2.80）
                          //   ★ 注意 ACES 会压高光：2.8 → 4.2 出来只亮 3%，看着变亮
                          //   主要靠【变粗】和【边缘变软】，不是靠这个数。
#endif

#ifndef FX_SHIPROT
#define FX_SHIPROT -0.493 // 机身在图里的倾角（弧度）。
                          // ★ 这个值不是随便给的：要让【机头指向灭点】。
                          //   灭点在 (FX_TILTX, FX_TILTY)，飞船在 (FX_SHIPX, FX_SHIPY)，
                          //   两者连线方向 = atan2(TILTY-SHIPY, TILTX-SHIPX)。
                          //   改了灭点或飞船位置，这个也要跟着算。
#endif

#ifndef FX_BANDTINT
#define FX_BANDTINT 0.0   // 保留多少 ABI 的【频段染色】（audioGrade 里 R/G/B 各吃一个频段）。
                          //   0 = 关（默认）：本效果是暖白+冷蓝的单色调，绿的瞬态会整幅闪。
                          //   1 = 完全保留，跟其它效果一致。
#endif
#ifndef FX_HUE
#define FX_HUE 0.0
#endif

const float TAU = 6.28318530718;

float hash11(float x){ x = fract(x*0.1031); x *= x+33.33; x *= x+x; return fract(x); }

// 点到【凸四边形】的有符号距离（内部为正），屏幕单位。
// 平面矩形透视投影后仍是凸四边形（投影保持凸性），所以"取四条边半平面距离的最小值"
// 就是边界的有符号距离 —— 正负号由四边形的绕向决定，自动适应朝前/朝后看的板。
// 点到线段的最短距离 —— 机翼激光的每一束都是一条线段
float sdSeg(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    return length(pa - ba * h);
}

float sdQuad(vec2 pt, vec2 A, vec2 B, vec2 Q, vec2 D) {
    float a2 = (B.x-A.x)*(D.y-A.y) - (B.y-A.y)*(D.x-A.x)
             + (Q.x-A.x)*(D.y-A.y) - (Q.y-A.y)*(D.x-A.x);
    float o  = (a2 >= 0.0) ? 1.0 : -1.0;
    float d0 = o * ((B.x-A.x)*(pt.y-A.y) - (B.y-A.y)*(pt.x-A.x)) / max(length(B-A), 1e-6);
    float d1 = o * ((Q.x-B.x)*(pt.y-B.y) - (Q.y-B.y)*(pt.x-B.x)) / max(length(Q-B), 1e-6);
    float d2 = o * ((D.x-Q.x)*(pt.y-Q.y) - (D.y-Q.y)*(pt.x-Q.x)) / max(length(D-Q), 1e-6);
    float d3 = o * ((A.x-D.x)*(pt.y-D.y) - (A.y-D.y)*(pt.x-D.x)) / max(length(A-D), 1e-6);
    return min(min(d0, d1), min(d2, d3));
}

// ---------------------------------------------------------------------------
// 隧道轴：一条【倾斜 + 弯曲】的空间曲线。只要 xy 关于 z 的函数。
//   · 倾斜项  FX_TILT* z  →  管子斜着伸出去 → 灭点离开画面中心
//   · 弯曲项  sin/cos(z)  →  管子扭动 → 虫洞感
//   注意 T 只进弯曲项：管子本身该是【固定的】，是相机在穿过它。
// ---------------------------------------------------------------------------
// 管轴的世界曲线（只看 xy）
vec2 bendCurve(float z, float T) {
    return vec2(FX_BEND * sin(z * 0.30 + T * 0.50),
                FX_BEND * cos(z * 0.24 + T * 0.40) * 0.85);
}
// ★ 轴【相对相机】的位置，z = 相对相机的深度。
//   相机在轴上随 travel 前进，所以相对轴 = curve(travel+z) − curve(travel)。
//   这样 z=0 处轴恒为 0 → 相机永远在管子【里面】（原来写成"管子斜着伸出去"，
//   相机会跑到管外，画面下方那块黑就是看到了管外）。
//   对角构图来自【前方的弯曲】+ 一点残余倾斜，不是靠把管子推走。
vec2 tunnelAxis(float z, float T, float travel) {
    vec2 s = bendCurve(travel + z, T) - bendCurve(travel, T);
    return s + vec2(FX_TILTX * z, FX_TILTY * z);
}
float tunnelRad(float z, float T, float travel) {   // 半径随轴向呼吸 → 喉部一收一放
    return FX_TUBER * (1.0 + 0.22 * sin((travel + z) * 0.31 + T * 0.55));
}
// 距离场。用"到 z 处轴心的距离 − 半径"近似 —— 轴弯得缓时够准，且极便宜。
float map(vec3 q, float T, float travel) {
    vec2 c = tunnelAxis(q.z, T, travel);
    return length(q.xy - c) - tunnelRad(q.z, T, travel);
}

void main()
{
    float T   = uTime + uHitDrive;              // hit 累积的额外相位也进来 → 鼓点抽插
    float spd = uBpm / 120.0;                    // 用户速度已由时间轴缩放

    vec2 p = (gl_FragCoord.xy - 0.5 * uResolution.xy) / uResolution.y;

    vec3 ro = vec3(0.0);
    vec3 rd = normalize(vec3(p * FX_FOV, 1.0));   // 窄视野 = 长焦

    // 【前进】：相机沿 +z 推进会一直走出管子，所以反过来 —— 把【世界】往 −z 挪。
    //   等价于相机前进，但管子永远在，不需要重造几何。
    float travel = T * spd * FX_SPEED * 1.6;   // 相机沿轴前进的距离

    float field = 0.0;
    vec3  col   = vec3(0.0);
    float cover = 1.0;                          // 隧道铺满全屏，不靠 field 推

    // 步进的结果要【跨层用】—— 提到层外声明，别锁在层 1 的块里
    // （踩过：写在 #if 块里，层 4 就取不到，报 undeclared identifier）
    float hit = -1.0;
    vec2  dv  = vec2(0.0);
    float fog = 0.0;
    float throat = 0.0;

    // ================= 层 1 · 隧道本体 =================
#if FX_STAGE >= 1
    float t = 0.05;
    for (int i = 0; i < 110; ++i) {
        vec3 q = ro + rd * t;
        float d = map(q, T, travel);
        if (d < 0.0015 * t) { hit = t; break; }
        t += max(d * 0.85, 0.003);
        if (t > 70.0) break;
    }

    // 命中点（世界坐标，已把 travel 加回去）
    vec3 q  = ro + rd * max(hit, 0.05);
    vec2 c  = tunnelAxis(q.z, T, travel);
    dv = q.xy - c;
    float rr = max(length(dv), 1e-5);
    vec2  nrm = dv / rr;                        // 墙面法线（朝轴心为负）

    // 墙面明暗：法线朝光源（左上方）的一侧亮，背光侧沉进深蓝
    float lit  = 0.5 + 0.5 * dot(nrm, normalize(vec2(-0.55, 0.62)));
    // 距离雾：越深越暗 —— 隧道尽头收进黑蓝
    fog    = exp(-max(hit, 0.0) * 0.085);
    // 喉部高光：半径收窄处更亮，像管子在发光的细腰
    throat = smoothstep(0.86, 1.14, tunnelRad(q.z, T, travel) / FX_TUBER);

    // ★ 管壁本色改成【深灰中性】，不是蓝 ——
    //   条与条之间的缝里露出的是这一层，它一蓝，整幅图就被染蓝（踩过）。
    //   参考图的缝是近黑的深灰。
    vec3 wallDeep = vec3(0.020, 0.019, 0.019);
    vec3 wallMid  = vec3(0.075, 0.072, 0.070);
    vec3 wallHot  = vec3(0.90,  0.86,  0.82);   // 喉部高光：暖白

    col = mix(wallDeep, wallMid, lit * 0.85 + 0.15);
    col = mix(col, wallHot, throat * 0.35);
    col *= fog;
#endif

    // ================= 层 2 · 隧道壁碎片场（真 3D 矩形面片）=================
#if FX_STAGE >= 2
    {
        // ====================================================================
        // ★ 路线 B：在管子里【真的摆 N 块矩形板】，走真透视投影。
        //
        //   为什么不是"屏幕极坐标里画条"（上一版）：那样画出来的东西永远是
        //   【从灭点出发的射线】，形状被极坐标锁死 —— 画得出长条，画不出
        //   "一块板斜着切过来"。参考图里那些方头方脑、带直边的亮块，
        //   是【平面四边形被透视投过来的像】。
        //
        //   做法：每块板在世界里定成四个角 → 四角投影到屏幕 → 屏幕空间做
        //   点在凸四边形内的判定。平面矩形透视后仍是凸四边形，所以这一步
        //   是【精确】的，不需要步进。近的大、远的小、所有边收向同一个灭点，
        //   这三件事是同一套投影的副产品，不是三个旋钮。
        //
        //   板【往相机冲】：每块一个相位 ph，随 travel 减小 → 由远及近掠过，
        //   到近端回收。所以场是活的，不是贴在墙上的静物。
        //
        //   "游离" = 每块的漂浮量 dr 不同：多数贴壁，一部分离开壁面浮在管中。
        // ====================================================================

        vec2  vpS = vec2(FX_TILTX, FX_TILTY) / FX_FOV;   // 灭点（屏幕坐标，可出画）
        vec2  rvF = p - vpS;

        float aa  = 1.8 / uResolution.y;                 // 抗锯齿带（屏幕单位）
        float glw = aa * FX_PGLOW;                       // 溢光半径（× 抗锯齿带）

        vec3  acc2 = vec3(0.0);
        vec3  Ldir = normalize(vec3(-0.55, 0.62, 0.10)); // 壁面主光方向

        // ★ 编队：网格打在【屏幕极坐标 (u, θ)】上，不是打在管壁的 (θ, z) 上。
        //
        //   为什么：屏幕上的面积元 ∝ u·du·dθ（u = 离灭点的屏幕半径），而 u = rad/(z·FOV)。
        //   · 按 z 均匀撒 → 面积密度 ∝ 1/z³，板全堆在灭点（那里屏幕面积趋近 0），外侧是大洞；
        //   · 按 θ 铺满整圈 → 相机只看得到约 1.5 弧度的扇区，76% 的板生在画外。
        //   两个坑都踩过。打在 (u, θ) 上，覆盖是【按定义】的 —— 每格解出自己的深度。
        const float NZS  = float(FX_NPANEL) / FX_NCOL;  // 径向槽数
        // 一条槽在 u 上跨多远（比例）。板长就取它 —— 板长和槽距绑死，缝才是均匀的；
        // 用一个独立的"长宽比"旋钮去定长，槽和板就对不上，要么叠要么留洞。
        const float USLO = pow(FX_PU1 / FX_PU0, 1.0 / NZS) - 1.0;

        // θ 的中心：屏幕中心那条射线打到墙上时的环向角。
        //   相机在轴上、沿 +z 看，轴朝 +FX_TILT 偏 → 屏幕中心打到的是轴的【另一侧】。
        float thC = atan(-FX_TILTY, -FX_TILTX);

        for (int i = 0; i < FX_NPANEL; ++i) {
            float fi = float(i);
            float h0 = hash11(fi * 1.37 + 0.71);
            float h1 = hash11(fi * 2.71 + 1.13);
            float h2 = hash11(fi * 4.19 + 2.37);
            float h3 = hash11(fi * 6.83 + 3.11);
            float h4 = hash11(fi * 8.71 + 4.29);
            float h5 = hash11(fi * 3.91 + 5.53);
            float h6 = hash11(fi * 11.7 + 6.13);

            float colI = mod(fi, FX_NCOL);          // 第几列（环向）
            float slot = floor(fi / FX_NCOL);       // 第几槽（径向）
            float hcol = hash11(colI * 7.13 + 0.37);

            // ---- 屏幕极坐标：u 沿径向扫，整列一起往外推 ----
            //   列相位 hcol 让相邻列错开：不错开的话同一槽的整圈板锁在一个半径上，
            //   会看出"一圈一圈的环"在同步往外挤。h4 再给每块一点自己的抖动。
            float tt = fract((slot + 0.5 + (h4 - 0.5) * 0.7) / NZS
                             + hcol * 0.9 + travel * FX_PFLOW * 0.20);
            float u  = FX_PU0 * pow(FX_PU1 / FX_PU0, tt);
            float th = thC + (colI + 0.5 - FX_NCOL * 0.5 + (h0 - 0.5) * 0.55)
                            / FX_NCOL * FX_WEDGE;

            // ---- 反解深度：u = rad(z)/(z·FOV)。rad 本身随 z 呼吸，迭代一次就够 ----
            float z0  = FX_TUBER / (u * FX_FOV);
            float zc  = tunnelRad(z0, T, travel) * z0 / FX_TUBER;
            float rad = tunnelRad(zc, T, travel);

            // ---- 尺寸（世界单位）----
            //   屏幕上的长度 Δu = USLO·u；代回 z = rad/(u·FOV) 得
            //   Δz = zc·USLO —— 板的世界长度正比于深度，于是屏幕上看到的长度是恒定的。
            //   ★ 宽度：列宽 × 重尾倍数。列网格只管【位置】，不管宽度 ——
            //     宽度必须自己是一条重尾分布，否则整面墙的条一样粗。
            float dth   = FX_WEDGE / FX_NCOL;
            float wMul  = FX_PWMIN + FX_PWMAX * pow(h1, FX_PWPOW);
            float halfW = 0.5 * dth * rad * wMul;

            // ★ 发光体：细条才是【光源】。参考图里那些发荧光的细线就是这一族。
            //   概率 ∝ sqrt(最细/本宽) —— 越细越可能是发光体，宽条基本是【被照亮的墙】。
            //   这一层加上之后，画的亮度就【来自条本身】，不再是一层打上去的光照场。
            float emitP = FX_PEMITRATE * sqrt(FX_PWMIN / max(wMul, 1e-4));
            float isEm  = step(1.0 - emitP, h6);
            // ★ 溢光半径必须【跟着板的屏幕尺寸走】，不能是屏幕固定值。
            //   固定值（原来 aa*7≈13px）在灭点附近会出事：那里一块板只有 1~2px，
            //   却拖着 13px 的光晕，几十列的荧光全灌进那块趋近 0 的屏幕面积 →
            //   灭点方向过曝成一团白，而参考图那边【最暗】。半径比例化之后就对了。
            float wScr  = 2.0 * halfW / max(zc * FX_FOV, 1e-3);
            float glwI  = max(aa * 1.5, wScr * FX_PGLOWK * (1.0 + isEm * FX_PEMITGLOW));
            float halfL = 0.5 * zc * USLO * FX_PLFILL * exp((h5 - 0.5) * FX_PLSIZE);

            // ---- 游离：多数贴壁（drk=0），少数整块浮进管里 ----
            float drk  = max(h2 - 0.62, 0.0) / 0.38;
            float radF = max(rad - FX_PDR * drk, 0.15);

            // ---- 中心（世界坐标）----
            vec2  axC  = tunnelAxis(zc, T, travel);
            vec2  dirT = vec2(cos(th), sin(th));
            vec3  Cc   = vec3(axC + dirT * radF, zc);

            // ---- 局部基：轴向切向 tA、切平面内的环向 tT ----
            //   轴向切向要用【管子自己的切线】，不能图省事用 +z：
            //   管轴带 FX_TILT 的斜率和 FX_BEND 的弯曲，用 +z 的话板会和管子错开一个角度。
            vec2  am = tunnelAxis(zc - 0.35, T, travel);
            vec2  ap = tunnelAxis(zc + 0.35, T, travel);
            vec3  tA = normalize(vec3(ap - am, 0.70));
            vec3  nr = normalize(vec3(dirT, 0.0));       // 板法线（径向）
            vec3  tT = normalize(cross(tA, nr));

            // ---- 切平面内自转 ----
            float rho = (h3 - 0.5) * 2.0 * FX_PROLL;
            float cr = cos(rho), sr = sin(rho);
            vec3  eW =  tT * cr + tA * sr;               // 宽方向（局部 +u）
            vec3  eL = -tT * sr + tA * cr;               // 长方向（局部 +v）

            // ---- 四角 ----
            vec3 c0 = Cc - eW * halfW - eL * halfL;
            vec3 c1 = Cc + eW * halfW - eL * halfL;
            vec3 c2 = Cc + eW * halfW + eL * halfL;
            vec3 c3 = Cc - eW * halfW + eL * halfL;

            // 跨过相机平面的板直接丢 —— 它已经几乎全在画面外了
            //（管壁半径 2.2、视野 0.6：z 小到 1.7 的壁面就已经出画）
            if (min(min(c0.z, c1.z), min(c2.z, c3.z)) < 0.10) continue;

            vec2 P0 = c0.xy / c0.z / FX_FOV;
            vec2 P1 = c1.xy / c1.z / FX_FOV;
            vec2 P2 = c2.xy / c2.z / FX_FOV;
            vec2 P3 = c3.xy / c3.z / FX_FOV;

            // ---- 屏幕包围圆：绝大多数像素在这里就被剔掉 ----
            vec2  Cs = (P0 + P1 + P2 + P3) * 0.25;
            float Rs = max(max(length(P0 - Cs), length(P1 - Cs)),
                           max(length(P2 - Cs), length(P3 - Cs)));
            // ★ 剔除半径要给到溢光的【几倍】：exp(-d/glwI) 在 d=glwI 处还有 0.37，
            //   按 1× 切会把荧光切成一个硬边圆弧（踩过，画面上能看见一圈圈的弧）。
            if (length(p - Cs) > Rs + glwI * 4.0 + 0.02) continue;

            // ---- 色散：每通道把测试点绕【灭点】转一点点 ----
            //   参考图里每根长条的旁边都贴着一道红边和一道青边，且与长条平行 ——
            //   那就是"绕画面中心的角度错位"，不是"整体缩放"。
            float ca = cos(FX_DISP), sa = sin(FX_DISP);
            vec2 pR = vpS + vec2(ca * rvF.x - sa * rvF.y, sa * rvF.x + ca * rvF.y);
            vec2 pB = vpS + vec2(ca * rvF.x + sa * rvF.y, -sa * rvF.x + ca * rvF.y);

            float dR = sdQuad(pR, P0, P1, P2, P3);
            float dG = sdQuad(p,  P0, P1, P2, P3);
            float dB = sdQuad(pB, P0, P1, P2, P3);

            vec3  mvec = vec3(smoothstep(-aa, aa, dR),
                              smoothstep(-aa, aa, dG),
                              smoothstep(-aa, aa, dB));

            // ---- 颜色：暖白/粉为主，少数冷蓝 ----
            // ★ 暖色底【不能是饱和的琥珀】。参考图最亮的 10% 是 (0.857,0.791,0.786)，
            //   色相 11°、饱和度只有 0.099 —— 是【白粉】，不是橙。
            //   原来用 (1.00,0.74,0.50)：乘上 PEXPO 之后 R、G 双双削到 1.0、B 留在 0.75，
            //   色相被推到 60° = 黄，整幅偏琥珀。低饱和的暖色削顶后趋向【白】而不是黄。
            //   中性那档也不能偏绿：参考图最亮区 R-G = +0.066，我原来是 −0.023。
            float pick = hash11(fi * 23.3 + 0.71);
            vec3  base = (pick < 0.46) ? mix(vec3(0.98,0.94,0.92), vec3(1.00,0.86,0.80), h4)
                       : (pick < 0.86) ? vec3(0.85,0.83,0.87)
                                       : vec3(0.55,0.68,0.95);

            // 回卷的板在【近端淡出】再回收：u 大到 2.2 时它已经顶到画框，
            //   不淡出就会看见一块亮板"啪"地原地消失。
            // ★ 淡出窗口必须【很窄】：u = PU0·(PU1/PU0)^tt，tt 从 0.90 到 1.0
            //   跨的是 u 1.85→2.2 —— 那是画框外圈的【一大片】。开在这里等于把
            //   画框边缘整圈压暗，而参考图最亮的大宽带恰恰在边缘（块亮度差 0.26→0.22）。
            float fade = smoothstep(0.0, 0.07, tt) * (1.0 - smoothstep(0.972, 1.0, tt));

            float lit = 0.60 + 0.40 * dot(nr, Ldir);     // 朝光的那侧亮
            float br  = (0.20 + 0.80 * h2 * h2) * lit * fade
                      * (1.0 + isEm * FX_PEMITBOOST);    // 发光体显著更亮
            float fz  = exp(-zc * FX_PFOG);              // 越远越被雾吃掉（灭点方向压暗）

            acc2 += base * br * fz * FX_PEXPO * mvec;
            // 荧光外溢：光源外面漫出去的一圈。发光体的这一圈要大得多 ——
            //   参考图离亮段 40~80px 处还有 0.106 的底，缝不是黑的。
            acc2 += base * br * fz * FX_PEXPO * (exp(-max(-dG, 0.0) / glwI) * 0.22);
        }

        col = col * 0.30 + acc2;
    }
#endif

    // ================= 层 3 · 色温分层（只剩这个了）=================
    // ★ 这里原来还有一颗【屏幕空间的椭圆亮核】 col *= 0.22 + 0.42*core —— 已删。
    //   用户 2026-10-16 指出："光源不是灭点，是那些较细的矩形条打出的荧光"。
    //   那颗亮核是"打在画面上的光"，和碎片自己发光的模型是两套东西；
    //   两者叠在一起的结果是：灭点方向被亮核和荧光一起推爆（块亮度 1.00），
    //   而参考图那边恰恰是最暗的（0.09~0.20）。光现在全部来自 acc2。
    //
    //   保留下来的只有【上暖下冷】：这条是照参考图量的、真实存在的
    //   （参考图右下 B=0.496 明显大于 R=0.398，左中则 R>B）。
    //   ★ 但原来冷端写成 (0.86,0.93,1.08) —— B 高、G 也高，是【蓝绿】。
    //     叠在暖色上把最亮区的 R-G 拉到 −0.023，参考图是 +0.066。
    //     冷应该是纯粹的【蓝】，不该带绿。
#if FX_STAGE >= 2
    {
        vec3  tint  = mix(vec3(0.91, 0.94, 1.07),                  // 下：冷（蓝，不带绿）
                          vec3(1.06, 1.00, 0.95),                  // 上：暖（暖白，不偏橙）
                          smoothstep(-0.55, 0.45, p.y));
        col *= tint;
    }
#endif

    // ================= 层 4 · 飞船 =================
#if FX_STAGE >= 3
    {
        // ---- 飞船：贴图 sprite（写实机械体，不是线框了）----
        // 屏幕空间里一块【旋转过的矩形】，采样 uShipTex，用它的 alpha 当遮罩。
        // 素材是 Python 画的 32 位 BMP（带 alpha），main.cpp 现读 —— 见那边的注释。
        float kS = FX_FOV * FX_SHIPZ;
        float bob = 0.018 * sin(T * 1.7);
        vec2  sc  = vec2(FX_SHIPX, FX_SHIPY + bob);          // 屏幕上的中心
        float szP = FX_SHIPSZ;                                // 屏幕上的半高

        // 先把周围压暗一点，飞船才从亮条里跳出来
        // ★ 这圈"把周围压暗让飞船跳出来"是旧背景（细光束场）的配套：那时船会糊进线里。
        //   现在背景是【自己发光的碎片】，而且参考图里飞船是【坐在亮区里】的。
        //   留着它在画面正中挖出一个黑洞 —— 中区亮度 0.319 vs 参考图 0.663。
        //   压到几乎没有：只留一点点接触阴影。
        float pad = length((p - sc) / (szP * 2.6));
        col *= mix(0.88, 1.0, smoothstep(0.55, 1.2, pad));

        vec2  s = p - sc;
        float ct = cos(FX_SHIPROT), st = sin(FX_SHIPROT);
        vec2  q  = vec2(ct * s.x - st * s.y, st * s.x + ct * s.y) / szP;   // 转到机身坐标，[-1,1]
        // 贴图里机头朝上 → 屏幕上方对应 V=0
        vec2  uv = vec2(q.x * 0.5 + 0.5, 0.5 - q.y * 0.5);

        // ★ 从【图集】里按方位角取一帧 —— 这就是"真 3D 模型 + 镜头能绕"的实现：
        //   离线把 OBJ 从 32 个方位各烘一张，运行时按角度选。
        //   不需要引擎的网格通道，隧道还能留在同一个 .frag 里。
        // 图集是【二维】的：32 列（方位）× 5 行（仰角）
        const float SHIP_NY = 32.0;
        const float SHIP_NP = 5.0;
        float fX = mod(floor(fract(-FX_SHIPVIEW / TAU + 0.5) * SHIP_NY + 0.5), SHIP_NY);
        float fY = clamp(floor(FX_SHIPELEV * SHIP_NP), 0.0, SHIP_NP - 1.0);
        vec2  suv = vec2((fX + clamp(uv.x, 0.0, 1.0)) / SHIP_NY,
                         (fY + clamp(uv.y, 0.0, 1.0)) / SHIP_NP);

        vec4  tx = texture(uShipTex, suv);
        float inside = step(0.0, uv.x) * step(uv.x, 1.0) * step(0.0, uv.y) * step(uv.y, 1.0);
        // ★ 霓虹增益只能打【亮部】。整体乘 K 会连暗甲板一起抬（0.02×4.5 → ACES 后 0.11，
        //   整条船发灰），还会把本来就偏亮的材质直接吹白。所以按【最大分量】做软阈值：
        //   甲板 m≈0.03 → 增益 1；霓虹 m≈1.0 → 增益 1+FX_SHIPNEON。是乘倍数不是加白光，
        //   所以色相不会被洗掉。
        float shM = max(max(tx.r, tx.g), tx.b);
        vec3  shN = tx.rgb * (1.0 + FX_SHIPNEON * smoothstep(0.40, 1.00, shM));
        col = mix(col, shN, clamp(tx.a * inside, 0.0, 1.0));

        // ---- 机翼激光【子弹】：hit 时两侧翼尖各射出一串红弹 ----
        //
        //   飞行方向 = 朝【灭点】。机头朝画面里（从船尾后方看），所以这就是机身前方；
        //   灭点又在管轴上，所以弹道也是【平行于管壁】的直线 —— 两个要求是同一件事。
        //
        //   每一颗有【自己的运动函数】：
        //     ph  = fract(uHitDrive·RATE + 这颗的身份偏移)   ← 相位，各自独立地跑
        //     s   = ph · RANGE                                ← 已飞出的世界距离
        //     sc2 = z0/(z0+s)                                 ← 透视收缩，z0 = 炮口深度
        //     屏幕位置 = 灭点 + (炮口 − 灭点)·sc2              ← 顺着那条直线收向灭点
        //     长度/宽度 ×= sc2                                 ← 越飞越小
        //   位移不是线性插值，是按【深度】算的 —— 所以它真的会"往画面里钻"。
        //
        //   ★ 为什么相位挂在 uHitDrive 上而不是 uHitKick 上：
        //     uHitKick 是【瞬时衰减值】，拿它当"距上次开火多久"用，弹道快慢就被
        //     引擎的衰减曲线绑架了（曲线一改速度就变）。uHitDrive 是 ABI 里
        //     按 (1+0.55·kick+0.18·snare) 积分的【单调额外时间】—— 它才是"时间"。
        //     uHitKick 只当【开火门】（有鼓点才吐弹）。
        {
            vec2  vpS  = vec2(FX_TILTX, FX_TILTY) / FX_FOV;
            float gate = clamp(uHitKick, 0.0, 1.0) * FX_LB_ON;
            if (gate > 0.002) {
                float z0  = FX_SHIPZ;                  // 炮口深度（和飞船同一个深度）
                vec3  accL = vec3(0.0);
                const int NL = FX_LBN * 2;             // 两侧一起数
                for (int k = 0; k < NL; ++k) {
                    float fk = float(k);
                    float sd = (mod(fk, 2.0) < 0.5) ? -1.0 : 1.0;   // 哪一侧
                    float j  = floor(fk * 0.5);                     // 这一侧的第几颗
                    vec2  qk = vec2(sd * FX_LBX, FX_LBY);
                    // 局部 -> 屏幕（和上面取贴图用的是同一套变换）
                    vec2  mk = sc + szP * vec2(ct * qk.x + st * qk.y,
                                               -st * qk.x + ct * qk.y);
                    // ---- 这颗自己的运动 ----
                    float ph  = fract(uHitDrive * FX_LBRATE
                                      + j / max(float(FX_LBN), 1.0));   // 两侧齐射，不错相位
                    float s   = ph * FX_LBRANGE;
                    float sc2 = z0 / (z0 + s);
                    vec2  bp  = vpS + (mk - vpS) * sc2;             // 屏幕位置
                    float bl  = FX_LBLEN * sc2;                     // 屏上长度
                    float bw  = max(FX_LBW * sc2, 1e-5);            // 屏上半宽
                    // 弹体：沿飞行方向的一小段（不是无限长的光条）
                    vec2  bd  = normalize(vpS - bp);
                    float d   = sdSeg(p, bp - bd * bl * 0.5, bp + bd * bl * 0.5) / bw;
                    // 芯 + 一圈软晕。★ 只加亮度没用：ACES 会把 2.8 和 4.2 压到差不多，
                    //   "看着更亮"靠的是【更粗】+【边缘更软】+【外加一圈晕】。
                    float m   = (exp(-d * d * 1.15) + 0.30 * exp(-d * d * 0.28))
                              * pow(1.0 - ph, 0.7) * (0.35 + 0.65 * sc2);
                    // 炮口：这一颗刚出锅时在翼尖上闪一下。
                    // ★ 半径要按 bw 算：`bw*bw*90` 是 9.5 倍半宽 ≈ 40px 的大光球，
                    //   而且 LBN=3 时总有一颗在 ph≈0 → 闪光几乎常亮、看着像静止的白团。
                    //   改成 3 倍半宽 + 很窄的相位窗。
                    //   子弹变粗之后这里要同步收：半径是 3·bw，bw 涨了 1.7 倍，
                    //   乘子从 9 收到 5 才让炮口闪光和原来差不多大。
                    float mo  = exp(-dot(p - mk, p - mk) / (bw * bw * 5.0))
                              * (1.0 - smoothstep(0.0, 0.07, ph)) * 0.55;
                    // 红：G/B 必须压到接近 0。ACES 高光软过渡会去饱和，
                    //   但只要 G、B 本身是 0，出来的仍是纯红；留 0.07 就会洗成橙粉。
                    accL += vec3(1.0, 0.015, 0.008) * m + vec3(1.0, 0.22, 0.12) * mo;
                }
                col += accL * gate * FX_LBGLOW;
            }
        }

        // ---- 尾焰：【粒子】，不是一道渐变锥 ----
        //   上一版是一整条平滑的锥形渐变 —— 那不是喷流，那是喷了一束光。
        //   真尾焰是一颗颗被喷出来的粒子：各自的位置、大小、亮度都不同，
        //   而且【沿程往后流】。粒子的位置全在 q.y < 喷口 那一侧，
        //   所以不会像更早那版那样刷到船身上（那次是暖芯漏了区域门槛）。
        float burst = uHitKick;
        float Lf    = FX_FLAMELEN * (1.0 + burst * 2.6);   // hit 时焰更长
        float spdF  = 0.90 + burst * 3.0;                  // hit 时流得更快

        vec3  accC = vec3(0.0);
        // ★ 循环开 200 个格子，但【平时只画前 80 颗】——
        //   第 80~200 颗的身份（相位/横向/大小）一直算着，hit 时按 burst 逐颗"放出来"。
        //   这样 hit 是【真的多喷出粒子】，而不是把原来那 80 颗拉长（那只是变长不变多）。
        for (int k = 0; k < 200; ++k) {
            float fk = float(k);
            if (fk > 80.0 + burst * 120.0) break;
            float h1 = hash11(fk*1.73 + 0.31);
            float h2 = hash11(fk*3.37 + 1.13);
            float h3 = hash11(fk*5.19 + 2.71);

            float tt    = fract(h1 + T * spdF * (0.30 + 0.45*h2));   // 0=喷口 1=焰梢
            float along = tt * Lf;
            float wa    = FX_FLAMEW * (0.30 + 2.10*tt);              // 越远越散开
            vec2  Pq    = vec2((h2 - 0.5) * 2.4 * wa, -(0.62 + along));

            float rad = (0.016 + 0.052*h3) * (1.0 - 0.45*tt);        // 越远越小（调小：原来 0.045~0.175）
            float dd  = length((q - Pq) / max(rad, 1e-4));
            float m   = (1.0 - smoothstep(0.40, 1.0, dd))
                      * (1.0 - smoothstep(0.30, 1.0, tt))            // 尾端渐隐
                      * (0.40 + 0.60*h3);
            // 颜色：近喷口暖白、越远越冷蓝（真尾焰就是这条温度梯度）
            accC += mix(vec3(1.00, 0.88, 0.70), vec3(0.50, 0.70, 1.00),
                        smoothstep(0.0, 0.55, tt)) * m;
        }
        col += accC * (0.85 + burst * 1.5);
    }
#endif

    // ================= 层 4 · （已清空）=================
    // 这里原来有一层【放射状速度线】：以灭点为心画同心条纹、相位带 T*3.0*spd。
    // 它是"3D 管子"那套的配套，背景重做成放射条纹场之后就成了多余的 ——
    // 而且它是全图【唯一随时间转】的东西（用户看到"底图有一层线在旋转"就是它）。
    // 删掉。

    // ---- 曝光 / 色相 ----
    col *= 1.45;
    {
        float ch = cos(FX_HUE), sh = sin(FX_HUE);
        col = vec3(col.r*ch - col.g*sh, col.r*sh + col.g*ch, col.b);
    }
    // ★ 高光【软过渡】，不能直接用 clamp 削顶：
    //   硬削顶会把最亮的暖色推成 (1,1,0.75) = 黄（色相 60°），再推到纯白（0°）——
    //   参考图最亮的 10% 是 (0.857,0.791,0.786)，色相 11°：它【没有削顶】，
    //   是压出来的暖白。软过渡保住色相，硬 clamp 保不住。
    //   ABI 里（CpuArtifact.glsl）只有量化/抖动/锐化，没有色调映射，不会重复施加。
    col = (col * (2.51 * col + 0.03)) / (col * (2.43 * col + 0.59) + 0.14);

    // ---- 浅色模式：走【真正的反相】，不是提亮。
    //      原来那句 `mix(白, col*0.55+0.30, 0.55)` 只是把整幅拉成灰蓝 —— 全糊。
    //      这里只翻【明暗】、保留蓝色相：
    //        深的隧道壁 → 白（浅色模式该"亮"）
    //        亮的缝线/飞船 → 蓝（浅色模式该"深"）
    //      正好是"蓝白色调"该有的样子：白底 + 蓝线。
    vec3 outp;
    if (uDarkBg > 0) {
        outp = col;                                  // 黑背景 → 黑蓝调（原色）
    } else {
        float L   = clamp(dot(col, vec3(0.299, 0.587, 0.114)), 0.0, 1.0);
        float inv = pow(1.0 - L, 0.75);              // 反相 + 提对比，免得又糊成一片
        outp = mix(vec3(0.16, 0.34, 0.72), vec3(0.985, 0.992, 1.00), inv);
    }

    // ---- 工程协议（别动）----
    field = col.r * 0.6 + col.g * 0.3 - col.b * 0.5;
    float pat = clamp(field, -1.0, 1.0);
    vec3  bg  = (uDarkBg > 0) ? vec3(0.0) : vec3(0.95);
    col = mix(bg, outp, cover);

        col = audioGrade(col);
    // ★ 关掉 ABI 的【频段染色】。
    //   audioGrade 里有这么一句（audio_prelude.glsl）：
    //       c += vec3(audioBand(2), audioBand(8), audioBand(14)) * 0.12;
    //   也就是 R/G/B 各自吃一个频带，**绿单独加的是 uFft[8]**。那个频带一有瞬态
    //   （镲、齿音、中高频合成器），整幅画面就统一 +0.12 绿 —— 用户实测就是
    //   "整体画面时不时有淡淡的绿色闪一下"。
    //   这是全局统一行为（13 个效果都调 audioGrade），在彩色效果里看不出来；
    //   但本效果是【暖白 + 冷蓝】的单色调，任何单通道偏移都会破相，所以这里减回去。
    //   能量压亮度、鼓点白闪（三通道同值）都保留 —— 只去掉"偏色"那一项。
    //   ⚠ 0.12 是从 ABI 抄来的常量。哪天 audio_prelude 里改了系数，这里要跟着改。
    col = clamp(col - vec3(audioBand(2), audioBand(8), audioBand(14))
                      * 0.12 * (1.0 - FX_BANDTINT), 0.0, 1.0);
    col = cpuFinalize(col, gl_FragCoord.xy, uDitherStrength);
    fragColor = vec4(col, uOpacity);
}
