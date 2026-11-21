/**
 * @file SSGeom.h
 * @brief Involute_1（楼梯条）的几何生成 —— 纯数学，不碰 GL
 *
 * 【命名】显示名是 Involute_1（效果树/预设里看到的），代码里一律用 SS/sS 当内部缩写。
 *
 * ---------------------------------------------------------------------------
 * 几何是什么：「把 Involute 的旋转楼梯掰直」
 * ---------------------------------------------------------------------------
 * Involute 是一条绕中轴盘旋而上的【旋转楼梯】。本效果把它摊直成一条
 * **笔直向上的楼梯**：踏面水平、立面竖直，一级比一级高，一级宽、很长。
 *
 *                                          ┌────┐
 *                                    ┌────┤    │
 *                              ┌────┤    │    │
 *                        ┌────┤    │    │    │
 *                  ┌────┤    │    │    │    │
 *            ┌────┤    │    │    │    │    │
 *      ──────┴────┴────┴────┴────┴────┴────┘
 *        一级 = 踏面（水平）+ 立面（竖直）+ 两侧 + 底面，是一个【独立的小块】
 *
 * ---------------------------------------------------------------------------
 * 为什么绕了这么大一圈（这条一定要留着，不然下次又会走回去）
 * ---------------------------------------------------------------------------
 * 前面做过四版都不对，用户的原话是"大矩形套几个小矩形"、"假楼梯"。根因有两条，
 * 都是**"楼梯必须一级比一级高"这个最基本的事实**被我忽略了：
 *
 *   ① 前几版是一块【平的】矩形板，上面铺满小块。平板本身没有"升上去"，天然像板不像梯。
 *   ② 更致命的一条：那些小块的高度是【棋盘格交错】的（相邻一高一低）。
 *      于是板的剖面是 上-下-上-下 的【锯齿】，而楼梯要求单调上升 —— 锯齿永远读不出楼梯。
 *
 * 现在：沿 y 每走 SS_STEP_D 就升高 SS_STEP_R，单调递增，没有任何交错。
 * "台阶各自弹跳"就是每一级整块上下弹 —— 这反而更贴合最初的需求
 * （"台阶上每个小台阶会独立的、上下随机的弹跳"里的"小台阶"，本来就是楼梯的一级）。
 *
 * ---------------------------------------------------------------------------
 * 与渲染侧的约定（改这里必须同步改 main.cpp 的 kSSVS / kSSFS）
 * ---------------------------------------------------------------------------
 *   · 顶点布局：每顶点 8 个 float = [x, y, z, cellU, cellV, nx, ny, nz]
 *       cellU = 本级台阶的归一化序号 (k+0.5)/SS_NX，cellV 恒为 0.5。
 *       着色器靠它反推是第几级 → 哈希出这一级的弹跳方向/幅度/延迟。
 *       配套：main.cpp 传 uCellNX = SS_NX、uCellNY = 1。
 *     非台阶部分用 cellU = -1 当哨兵。别用 0 —— 第 0 级会跟着坏掉。
 *   · 法线【必须烘进顶点】，不要在着色器里用 dFdx/dFdy 推：这台机器上实测
 *     会让同样的参数两次渲染差 32% 的像素，整个离线验证台因此失效。
 *
 * ---------------------------------------------------------------------------
 * 相机（在渲染侧，不在这里）
 * ---------------------------------------------------------------------------
 * 物体几何中心恒在原点（台阶在 y/z 上都以原点对称铺开），相机在半径恒定的球面上绕它转。
 */
#pragma once
#include <vector>
#include <cmath>
// 【要跟 Involute 的管"接得上"，就得知道它的真实尺寸】—— 从这里取，绝不抄常数：
// 抄一份就等着漂移，而这次漂的就是螺距（差 1.66 倍，一眼就断）。
#include "../VX/VXGeom.h"

// ---------------------------------------------------------------------------
// 参数（改这里就等于改整个效果的外观）
// ---------------------------------------------------------------------------
// 【轴的角色】—— 这三行是整件事的核心，全由 Involute 的真实尺寸推出来：
//   x = 宽度  →  卷起来绕【周向】   (= 单臂弧长 R·T^2/2)
//   y = 上升  →  卷起来是【径向】   (= 径向跨度 R*sqrt(1+T^2) - R)
//   z = 梯段方向 → 卷起来是【轴向】 (就是管子伸出去的方向)
// 【级数 × 拷贝数 = 一份到底的 130 级】
// 原来用"26 级 × 5 份拷贝沿 z 错开"来做成无限长 —— 但每一道份与份的接缝都要靠
// "沿 z 挪一个梯段长 + 沿 y 补上升量"来接严，而实测那道接缝【接不齐】（俯视图里
// 能看见横向错位，用户的原话："还是错位"）。
// 接缝是自找的：拷贝只是为了省顶点，而 130 级 × 30 顶点 = 3900 个顶点，
// 对 GPU 来说根本不算什么。直接把楼梯做长，就【不存在接缝】这件事了。
const int   SS_NX      = 130;     // 级数：130 × 0.2308 = 30.0（= Involute 的多层跨度 SPAN）
const int   SS_NY      = 1;       // 恒为 1：本效果是"一级宽"的一条梯
// 【每级进深由 Involute 单次绘制的宽度反推】：
// Involute 一次 draw 画一个周期，长度就是 VX_PERIOD_LEN = 6.0（它画 6 层、每层错开 SPAN/6）。
// 所以整条梯【一份】的长度必须正好是 6.0，卷起来才和它的螺旋台阶宽度重合：
//     每级进深 = 6.0 / 26 = 0.2308
// 原来写死的 0.28 会让一份长 7.28，比 Involute 一次绘制的宽度多出 21%。
// ⚠ 分母是【每周期级数 26】，不是 SS_NX —— 写成 SS_NX 的话，级数一改到 130，
//    每级就变成 0.046，整条梯还是 6.0 长，等于没变长。
const float SS_STEP_D  = VX_PERIOD_LEN / 26.0f;   // 0.2308 → 130 级 = 30.0
const float SS_STEP_R  = 0.159f;  // 每级升高（沿 y）—— 保持原样，不动
const float SS_THICK   = 0.10f;   // 梯段板厚（向下）
// 【宽度 = 整条叶形的弧长，不是单臂】
// 渐开线 P(t)=R(cos t+t sin t, sin t-t cos t)，|P'|=R·t，单臂弧长
//     = ∫0^T R·t dt = R·T²/2 = 1.0×2.4²/2 = 2.88
// 叶形 = 正支 + 镜像支，所以整条 = 5.76。
//
// ⚠ 必须用【整条】：卷曲的转角是 θ = 宽度/rr，内外缘的半径不同、包角差很多 ——
//   用单臂 2.88 时内缘(rr=1.0)只转 ±82°、合计 165°，卷出来是【半张壳】，不是管子
//   （和 Involute 并排渲染实测过：一个占满画面，一个缩成窄条）。
//   用整条 5.76 时内缘转 ±165°、合计 330° ≈ 一整圈，才包得住。
//   用户已确认选整条叶形。
inline float SSMatchWidth() { return VX_R * VX_T * VX_T; }                 // 5.76
const float SS_HALF_W  = 2.88f;   // = SSMatchWidth()/2

/// 相机到几何中心的【恒定】距离（只在平放/轨道视角下用；卷到底时由 RenderInvolute_1
/// 插值到 SS_HALF_H()，那时相机在管轴上）。放在这里而不是 main.cpp，是因为离线台也要用它 ——
/// 两边各写一份迟早对不上。
/// 7.0 → 8.5 → 17.0：板宽 5.76、梯段长 30，7.0 时楼梯是"贴着镜头"的。
/// ⚠ 这个常数【只影响平放的轨道视角】。卷到底时相机在管轴上、距离是 SS_HALF_H()(=15)，
///   由 RenderInvolute_1 插值过去 —— 所以这里可以放心加大，不会动到过渡的终点。
const float SS_CAM_DIST = 17.00f;

/// 沿 z（= 卷起来之后的管轴）的半步长。外缘微光按它判定"在梯的哪一段"。
inline float SS_HALF_H() { return (float)SS_NX * SS_STEP_D * 0.5f; }

// ---------------------------------------------------------------------------
// 【细分】—— 曲变直必须在顶点着色器里把"宽度"卷成叶形，而卷曲是【沿着宽度】发生的。
// 一块踏面如果只有左右两个顶点，卷起来就只有两条边动了，板面中间还是平的 ——
// 看上去就像板被"折"了一下，根本不是卷。所以沿宽度必须细分。
// 沿 z 也要细分：扭转是连续沿 z 长的（见 main.cpp 的 kSSVS），z 上太疏会看出棱。
// 这两个数只影响"卷起来时有多光滑"，平放时的外观完全不受影响（平的面多切几刀还是平的）。
const int SS_SUBX = 24;   // 沿板宽（x）细分段数
const int SS_SUBZ = 2;    // 沿梯段（z）细分段数

/// 顶点布局：每顶点 8 个 float
inline int SSVertexStride() { return 8; }

/// 每级的顶点数：踏面 SUBX*SUBZ + 底面 SUBX*SUBZ + 立面 SUBX + 两侧 2*SUBZ，每个四边形 6 顶点
inline int SSVertsPerStep() {
    return (SS_SUBX * SS_SUBZ * 2 + SS_SUBX + SS_SUBZ * 2) * 6;
}

/// 顶点数
inline int SSVertexCount() { return SS_NX * SS_NY * SSVertsPerStep(); }

/// 生成网格。out 按 [x,y,z,cellU,cellV,nx,ny,nz] 顺序追加。
inline void SSBuildMesh(std::vector<float>& out) {
    out.clear();
    out.reserve((size_t)SSVertexCount() * SSVertexStride());

    auto push = [&](float x, float y, float z, float u, float v,
                    float nx, float ny, float nz) {
        out.push_back(x); out.push_back(y); out.push_back(z);
        out.push_back(u); out.push_back(v);
        out.push_back(nx); out.push_back(ny); out.push_back(nz);
    };

    const float W = SS_HALF_W, D = SS_STEP_D, R = SS_STEP_R, TH = SS_THICK;
    // 【两个方向都要以原点居中】，否则相机绕着原点转、而板子的重心在别处 ——
    // 表现出来就是"楼梯的几何中心和视角中心不在一块"（占着画面一角、其余空着）。
    // 上升原先是从 0 起（0 → 20.5），几何重心在 y≈10 上；现在改成 ±NX·R/2。
    // 顺带把渐变也修对了：vLocal.y（= 上升量）的铺色范围 uYLo/uYHi 现在与它同量纲。
    const float z0 = -(float)SS_NX * D * 0.5f;
    const float y0 = -(float)SS_NX * R * 0.5f;
    for (int k = 0; k < SS_NX; ++k) {
        const float za = z0 + (float)k * D;      // 本级起点
        const float zb = za + D;                 // 本级终点（立面所在处）
        const float yt = y0 + (float)k * R;      // 本级踏面高度（上升，以 0 为中心）
        const float yb = yt - TH;                // 本级底面
        const float u = ((float)k + 0.5f) / (float)SS_NX;
        const float v = 0.5f;

        // 四个角在 [0,1] 上的采样点（含端点，逐段推出小四边形）
        auto at = [](float a, float b, int i, int n) { return a + (b - a) * (float)i / (float)n; };
        // 一个 [xa..xb]×[za..zb] 之类的四边形 → 两个三角形
        auto quad = [&](float x0, float y0, float z0q, float x1, float y1, float z1q,
                        float x2, float y2, float z2q, float x3, float y3, float z3q,
                        float nx, float ny, float nz) {
            push(x0,y0,z0q, u,v, nx,ny,nz); push(x1,y1,z1q, u,v, nx,ny,nz); push(x2,y2,z2q, u,v, nx,ny,nz);
            push(x0,y0,z0q, u,v, nx,ny,nz); push(x2,y2,z2q, u,v, nx,ny,nz); push(x3,y3,z3q, u,v, nx,ny,nz);
        };

        for (int i = 0; i < SS_SUBX; ++i) {
            const float xa = at(-W, W, i, SS_SUBX), xb = at(-W, W, i + 1, SS_SUBX);
            for (int j = 0; j < SS_SUBZ; ++j) {
                const float ta = at(za, zb, j, SS_SUBZ), tb = at(za, zb, j + 1, SS_SUBZ);
                // ---- 踏面（水平，法线 +y）----
                quad(xa,yt,ta, xb,yt,ta, xb,yt,tb, xa,yt,tb,  0,1,0);
                // ---- 底面（法线 -y）----
                quad(xa,yb,ta, xa,yb,tb, xb,yb,tb, xb,yb,ta,  0,-1,0);
            }
            // ---- 立面（竖直，法线 -z）—— "台阶之所以是台阶"就靠这一面 ----
            quad(xa,yt,zb, xb,yt,zb, xb,yb,zb, xa,yb,zb,  0,0,-1);
        }
        for (int j = 0; j < SS_SUBZ; ++j) {
            const float ta = at(za, zb, j, SS_SUBZ), tb = at(za, zb, j + 1, SS_SUBZ);
            // ---- 两侧（法线 ±x）----
            quad(-W,yt,ta, -W,yt,tb, -W,yb,tb, -W,yb,ta,  -1,0,0);
            quad( W,yt,ta,  W,yb,ta,  W,yb,tb,  W,yt,tb,   1,0,0);
        }
    }
}
