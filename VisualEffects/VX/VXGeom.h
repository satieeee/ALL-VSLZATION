/**
 * @file VXGeom.h
 * @brief Involute（渐开线曲面）的几何生成 —— 纯数学，不碰 GL
 *
 * 抽成独立头的理由与 C3Veil.h / C3OrigamiGeom.h 一致：
 * 这是"GPU 与 CPU 将来必须给出同一结果"的部分，各写一份迟早漂移。
 *
 * ---------------------------------------------------------------------------
 * 几何是什么
 * ---------------------------------------------------------------------------
 * 「圆的渐开线」= 把绕在半径 R 的圆上的一根线拉直，线端点画出的轨迹：
 *
 *      P(t) = R·(cos t + t·sin t ,  sin t − t·cos t)
 *
 * t 是"已经解开的角"，越大半径越大（r = R·√(1+t²)）—— 这正是"无限展开"：
 * 它不需要任何循环/平铺技巧，曲线本身就永远向外张开。齿轮的齿廓就是它。
 *
 * 本效果把它做成一条【无限长的拱道】：
 *   · 横截面 = 两段渐开线镜像拼成的闭合"叶形"（t: 0→T 与它的镜像）；
 *   · 沿 z 复制成管状曲面，镜头沿 +z 无限推进 → 永远在展开新的部分；
 *   · 相邻顶点沿【径向】交替推出/收进（棋盘格 ±h）→ 四边形变成非平面的鞍面，
 *     拆成两个三角形后各自的法线不同 → 硬的凹凸分面。
 *
 * ---------------------------------------------------------------------------
 * 与渲染侧的约定（改这里必须同步改 main.cpp 的 kVXVS / kVXFS）
 * ---------------------------------------------------------------------------
 *   · 每个"周期"的几何只生成一份（VX_PERIOD_LEN 长），渲染时用 LAYERS 份
 *     在视深上错开并回绕 —— 与 Cubeeffect3 的多层推进同一套路，几何量不随"无限"增长。
 *   · 顶点布局：每顶点 5 个 float = [x, y, z, u, v]
 *       u = 绕横截面的归一化位置 [0,1)，v = 沿 z 的归一化位置 [0,1)
 *     u/v 给片元着色器做配色与细节。
 */
#pragma once
#include <vector>
#include <cmath>
#include <cstring>

// ---------------------------------------------------------------------------
// 参数（改这里就等于改整个效果的外观；渲染侧的 shader 只负责打光与上色）
// ---------------------------------------------------------------------------
const float VX_R          = 1.0f;    // 渐开线的基圆半径
const float VX_T          = 2.4f;    // 渐开线参数上限（越大叶形越长、越向外张开）
const int   VX_HALF       = 64;      // 单侧渐开线的采样点数 → 横截面共 VX_HALF*2 个点
// 沿 z 的圈数要够密：面板太细长（周向 0.09 × 纵向 0.38）的话，
// 棋盘格偏移产生的倾斜会被拉长方向摊平，"凹凸分面"就看不出来了。
// 取 40 让面板接近正方形（0.09 × 0.15）。
const int   VX_RINGS      = 40;
const float VX_PERIOD_LEN = 6.0f;    // 一个周期沿 z 的长度（世界单位）
const float VX_FACET_H    = 0.085f;  // 凹凸分面的起伏幅度（沿径向 ±）
const float VX_TWIST      = 6.2831853f; // 一个周期里横截面自转的角度（2π = 正好一圈，回绕无缝）

/// 圆的渐开线：参数 t（解开的角）→ 平面点
inline void VXInvolute(float t, float R, float& x, float& y) {
    float c = std::cos(t), s = std::sin(t);
    x = R * (c + t * s);
    y = R * (s - t * c);
}

/// 横截面闭合轮廓：正支 t:0→T，再镜像支 t:T→0 拼回来，共 2*VX_HALF 个点。
/// 起点是基圆上的 (R,0)（渐开线的尖点），末端由一条直线段闭合。
inline void VXCrossSection(std::vector<float>& out /* [x,y] 交错 */) {
    const int N = VX_HALF;
    out.resize((size_t)N * 2 * 2);
    for (int i = 0; i < N; ++i) {
        float t = VX_T * (float)i / (float)(N - 1);
        float x, y;
        VXInvolute(t, VX_R, x, y);
        out[(size_t)i * 2 + 0] = x;
        out[(size_t)i * 2 + 1] = y;
        // 镜像支：反向填，形成闭合环（上支正向 → 下支反向）
        int j = 2 * N - 1 - i;
        out[(size_t)j * 2 + 0] = x;
        out[(size_t)j * 2 + 1] = -y;
    }
}

/// 顶点数：每个四边形 2 个三角形 × 3 顶点 = 6 个顶点（不共享 —— 非平面四边形
/// 的两半法线本就不同，共享反而会把分面抹平）
inline int VXVertexCount() {
    const int M = VX_HALF * 2;                 // 横截面点数 = 四边形数
    return M * VX_RINGS * 6;
}

/// 曲面上【离中轴最远】的那一圈的半径 —— 也就是"外缘"的定义半径。
/// R·√(1+T²) 是渐开线自身的最大半径（在 t=T 处取到），再加上棋盘格向外凸的那半步 VX_FACET_H。
/// 外缘微光要靠它来定位：**用 u 找外缘是错的**，因为 t→T 的两个端点分居横截面索引
/// M/2-1 与 M/2，它们之间的"闭合边"是一条从 (0.88, +2.45) 直连到 (0.88, -2.45) 的长墙
/// —— 墙中点半径只有 0.88、贴着中轴。按 u≈0.5 取带子会把整面墙点亮，糊掉半个画面。
inline float VXMaxRadius() {
    return VX_R * std::sqrt(1.0f + VX_T * VX_T) + VX_FACET_H;
}

/// 每个顶点 7 个 float：[x, y, z, u, v, ox, oy]
/// 后两个是【径向单位向量】（从隧道中轴指向该点）—— 第二形态让所有台阶沿它
/// 一致地朝画面外分离。在几何里算好比在着色器里现推简单。
inline int VXVertexStride() { return 7; }

/// 生成一个周期的网格。out 按 [x,y,z,u,v] 顺序追加，容量用 VXVertexCount()*5。
inline void VXBuildMesh(std::vector<float>& out) {
    std::vector<float> cs;
    VXCrossSection(cs);
    const int M = (int)(cs.size() / 2);        // 横截面点数
    const float h = VX_FACET_H;

    out.clear();
    out.reserve((size_t)VXVertexCount() * VXVertexStride());

    // 取横截面第 i 个点，并在其【径向】上按棋盘格符号偏移 ±h。
    // 棋盘格 (i + k) 的奇偶 → 相邻顶点一凸一凹 → 四边形变鞍面 → 硬分面。
    auto vert = [&](int i, int k, float& x, float& y, float& z, float& u, float& v,
                    float& tx, float& ty) {
        int ii = ((i % M) + M) % M;
        float px = cs[(size_t)ii * 2 + 0], py = cs[(size_t)ii * 2 + 1];
        float len = std::sqrt(px * px + py * py);
        if (len < 1e-6f) len = 1e-6f;
        float nx = px / len, ny = py / len;             // 径向单位向量
        float sgn = ((ii + k) & 1) ? 1.0f : -1.0f;      // 棋盘格：凸 / 凹
        z = VX_PERIOD_LEN * (float)k / (float)VX_RINGS;
        // 横截面随 z 自转 → 拱道是拧着的，"展开"感来自这里
        float ang = VX_TWIST * (float)k / (float)VX_RINGS;
        float ca = std::cos(ang), sa = std::sin(ang);
        float rx = px + nx * h * sgn;
        float ry = py + ny * h * sgn;
        x = rx * ca - ry * sa;
        y = rx * sa + ry * ca;
        // 【方向必须用自转之后的径向】——顶点位置被 VX_TWIST 转过，而 VX_TWIST 正好是 2π，
        // 于是"转过半圈"的那一半圈上，用基座标算出来的方向正好相反（朝内）。
        // 这就是"只有一半在往外撑"的原因。方向跟着一起转，整条隧道才一致朝外。
        tx = nx * ca - ny * sa;
        ty = nx * sa + ny * ca;
        u = (float)ii / (float)M;
        v = (float)k / (float)VX_RINGS;
    };

    float ax, ay, az, au, av, atx, aty, bx, by, bz, bu, bv, btx, bty;
    float cx, cy, cz, cu, cv, ctx, cty, dx, dy, dz, du, dv, dtx, dty;
    for (int k = 0; k < VX_RINGS; ++k) {
        for (int i = 0; i < M; ++i) {
            vert(i,     k,     ax, ay, az, au, av, atx, aty);
            vert(i + 1, k,     bx, by, bz, bu, bv, btx, bty);
            vert(i + 1, k + 1, cx, cy, cz, cu, cv, ctx, cty);
            vert(i,     k + 1, dx, dy, dz, du, dv, dtx, dty);
            // 四边形 → 两个三角形（同一绕向，保证法线一致朝外）
            const float tri[6][7] = {
                {ax,ay,az,au,av,atx,aty}, {bx,by,bz,bu,bv,btx,bty}, {cx,cy,cz,cu,cv,ctx,cty},
                {ax,ay,az,au,av,atx,aty}, {cx,cy,cz,cu,cv,ctx,cty}, {dx,dy,dz,du,dv,dtx,dty},
            };
            for (int q = 0; q < 6; ++q)
                for (int c = 0; c < VXVertexStride(); ++c) out.push_back(tri[q][c]);
        }
    }
}
