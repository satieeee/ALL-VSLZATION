/**
 * @file XSGeom.h
 * @brief Shardstorm —— 高速碎片风暴的几何生成（纯数学，不碰 GL）
 *
 * 与 VTGeom/PCurve 等同一套约定：这里只有"顶点从哪来"，GL 侧在 XSRender.cpp。
 * 抽成独立头的理由也一样 —— 离线验证台（_hittest/xs_render_test.cpp）要跑【同一份】。
 *
 * ---------------------------------------------------------------------------
 * 一个碎片 = 一片【薄板】，绕自己的中轴高速自转，同时沿涡旋轨道公转
 * ---------------------------------------------------------------------------
 * 每片 24 个顶点：前后面各 12（4 个角 × 2 个三角形 × 3 —— 不共享顶点，
 * 因为要硬边：金属碎片不能有平滑法线，那样会看着像塑料）。
 *
 * 【为什么是"碎片"而不是"条"】项目里已经有细条（Filigree）、曲面（Involute）、
 * 台阶板（Involute_1）。碎片是唯一能同时做到"密"和"燥"的形状：
 *   · 密 —— 几千片独立小面铺满画面 → 留白度低
 *   · 燥 —— 每片有自己的轨道速度与自转速度，逐帧位移大 → 变化幅度大
 *
 * 【每片必须自带自己的参数】相位/角速度/半径/尺寸/色相全部逐片随机（C3Rnd 派生，
 * 确定性），否则所有碎片同步运动 = 一块整板在转，那是"机械"不是"风暴"。
 *
 * ---------------------------------------------------------------------------
 * 与渲染侧的约定（改这里必须同步改 XSShaders.h）
 * ---------------------------------------------------------------------------
 *   每顶点 16 float = [x, y, z, u, v, nx, ny, nz, id, phase, spin, radius, elev, size, hue, alpha]
 *     · u,v      贴在薄板上的局部坐标（0..1，用来做边缘高光与花纹）
 *     · id       第几片（0..N-1）—— 片元端要按片取值（色相/闪烁相位）
 *     · phase    轨道相位（0..1）
 *     · spin     自转角速度（rad/s，可正可负 —— 一半反过来才有对流感）
 *     · radius   轨道半径
 *     · elev     轨道高度（z 方向的基准）
 *     · size     这片的尺寸倍率
 *     · hue      色相基准（0..1）
 *     · alpha    恒 1（留出来给以后的出生/消亡）
 */
#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include "../C3/C3Veil.h"   // 只借用 C3Rnd —— 全项目共用一个确定性哈希

// ---------------------------------------------------------------------------
// 参数（改这里就等于改整个效果的外观）
// ---------------------------------------------------------------------------
const int   XS_SHARDS      = 14000;   // 碎片数。【这是"密"的来源】—— 少了画面就漏底
const int   XS_VERTS_PER   = 24;     // 每片 24 顶点（前后各 12）
const int   XS_BUCKETS     = 8;      // 按轨道半径分的提交桶（由内到外，远的先画）

const float XS_RMIN        = 0.12f;  // 轨道半径范围 —— 【必须很小】：0.55 那版中心是个黑洞，
                                     //   既是留白也是视觉缺陷；压到 0.12 之后中心被内圈碎片填满
const float XS_RMAX        = 4.20f;  // 外圈要超出可见半径，保证画面边缘也是满的
const float XS_ZMIN        =  0.5f;  // 沿隧道轴的分布（相机在原点朝 +z 看）
const float XS_ZMAX        = 26.0f;
const float XS_SIZE_LO     = 0.030f; // 单片半宽下限
const float XS_SIZE_HI     = 0.115f; // 上限 —— 拉开差距才有前后层次
const float XS_THICK       = 0.16f;  // 厚度 / 半宽 的比值（碎片要"薄"，但不是纸）
const float XS_SPIN_LO     = 1.20f;  // 自转角速度范围（rad/s）—— 高频，逐帧转得动
const float XS_SPIN_HI     = 6.50f;

/// 一个碎片的恒定参数（建几何与分桶共用，保证两边一致）
struct XSShard {
    float phase;    // 轨道相位
    float omega;    // 公转角速度（rad/s，可正可负）
    float radius;   // 轨道半径
    float elev;     // 轨道平面高度（z 基准）
    float plane;    // 轨道平面倾角（弧度）—— 不是所有碎片都在同一平面转
    float spin;     // 自转角速度
    float spin0;    // 自转初相
    float size;     // 尺寸倍率
    float hue;      // 色相基准
    float alpha;    // 透明度（留作出生/消亡）
    int   bucket;   // 提交桶
};

/// 生成全部碎片的参数表。确定性（只用 C3Rnd），GPU/CPU 与离线台结果一致。
inline void XSShardParams(std::vector<XSShard>& out) {
    out.resize(XS_SHARDS);
    for (int i = 0; i < XS_SHARDS; ++i) {
        XSShard& s = out[i];
        s.phase  = C3Rnd(i, 101);
        // 【公转速度：内圈快、外圈慢】—— 开普勒式的差速旋转。
        // 全部同速的话整个盘像一块刚体在转，"风暴"就没了；
        // 差速 + 逐片随机才是"每一片各转各的"。
        const float t = C3Rnd(i, 102);
        s.omega  = (0.55f + 1.35f * (1.0f - t)) * (C3Rnd(i, 103) < 0.5f ? -1.0f : 1.0f);
        s.radius = XS_RMIN + (XS_RMAX - XS_RMIN) * std::sqrt(C3Rnd(i, 104));  // sqrt = 面积均匀
        s.elev   = XS_ZMIN + (XS_ZMAX - XS_ZMIN) * C3Rnd(i, 105);
        s.plane  = (C3Rnd(i, 106) - 0.5f) * 0.55f;                            // 平面轻微倾斜
        s.spin   = XS_SPIN_LO + (XS_SPIN_HI - XS_SPIN_LO) * C3Rnd(i, 107);
        if (C3Rnd(i, 108) < 0.5f) s.spin = -s.spin;
        s.spin0  = C3Rnd(i, 109) * 6.2831853f;
        s.size   = XS_SIZE_LO + (XS_SIZE_HI - XS_SIZE_LO) * C3Rnd(i, 110);
        s.hue    = C3Rnd(i, 111);
        s.alpha  = 1.0f;
        s.bucket = 0;
    }
    // 按半径分桶 → 由内到外提交，远的（外圈）先画，近的压上去
    std::vector<int> ord(XS_SHARDS);
    for (int i = 0; i < XS_SHARDS; ++i) ord[i] = i;
    std::sort(ord.begin(), ord.end(),
              [&](int a, int b) { return out[a].radius > out[b].radius; });
    for (int r = 0; r < XS_SHARDS; ++r) {
        int b = r * XS_BUCKETS / XS_SHARDS;
        if (b >= XS_BUCKETS) b = XS_BUCKETS - 1;
        out[ord[r]].bucket = b;
    }
}

// ---------------------------------------------------------------------------
// 运动参数（【唯一来源】）—— 渲染器与离线台都从这一份取。
// 【为什么放这里】原先写在 XSRender.cpp 里，而离线台有自己的默认值 → 台子量出来的
// "变化幅度"根本不是实机的（台子默认 orbit=1.0，实机是 0.25，差 4 倍）。
// 这就是记忆里那条"台子抄了一份 main.cpp 的值"的老毛病，别再犯。
// 2026-09-29 用户要求"这个效果的 movement 减慢十倍"→ ORBIT/SPIN/BAND 各 ÷10。
// ---------------------------------------------------------------------------
const float XS_ORBIT_BASE  = 0.025f;  // 公转速度倍率（"变化幅度"的总闸）
const float XS_ORBIT_HIT   = 0.055f;  // 鼓点那一刻再加这么多
const float XS_SPIN_BASE   = 0.10f;   // 自转速度倍率
const float XS_BAND_SPEED  = 0.035f;  // 沿隧道轴的流动（fraction/秒）
const float XS_STROBE      = 0.00f;   // 逐片闪烁：默认关（靠运动本身，不靠闪）
const float XS_SIZE_MUL    = 1.00f;   // 碎片尺寸倍率
const float XS_SPREAD_BASE = 1.00f;   // 轨道半径倍率

inline int XSStride() { return 16; }
inline int XSVertexCount() { return XS_SHARDS * XS_VERTS_PER; }

/// 生成全部碎片，**按桶连续存放**（先按半径排序再建），渲染侧逐桶提交。
inline void XSBuild(std::vector<float>& out, int* bucketVertStart) {
    std::vector<XSShard> spec;
    XSShardParams(spec);

    std::vector<int> ord(XS_SHARDS);
    for (int i = 0; i < XS_SHARDS; ++i) ord[i] = i;
    std::sort(ord.begin(), ord.end(), [&](int a, int b) {
        if (spec[a].bucket != spec[b].bucket) return spec[a].bucket < spec[b].bucket;
        return spec[a].radius > spec[b].radius;
    });

    out.clear();
    out.reserve((size_t)XSVertexCount() * XSStride());
    for (int b = 0; b <= XS_BUCKETS; ++b) bucketVertStart[b] = 0;

    int bucketNow = -1;
    for (int r = 0; r < XS_SHARDS; ++r) {
        const int si = ord[r];
        const XSShard& s = spec[si];
        while (bucketNow < s.bucket) bucketVertStart[++bucketNow] = (int)(out.size() / XSStride());

        // 碎片在自己的轨道平面里的两个基向量：切向 T 与径向 R。
        // 板子沿着 (T, R) 铺开 —— 这样它始终"迎着轨道方向"，
        // 在公转里看起来是在飞，而不是在原地打转。
        const float cp = std::cos(s.plane), sp2 = std::sin(s.plane);
        // 局部平面（以碎片当前轨道角为基准）
        const float ang = s.phase * 6.2831853f;
        const float ca = std::cos(ang), sa = std::sin(ang);
        // 径向单位向量（含平面倾角）
        const float rx = ca, ry = sa * cp;
        // 切向单位向量
        const float tx = -sa, ty = ca * cp;

        // 四个角：±切向 × ±径向（板面法线取轨道平面的法线）
        const float hw = s.size, hh = s.size * 0.62f;
        const float nx = -sa * sp2, ny = ca * sp2, nz = cp;

        // 角点：(tx,ty) 方向 ±hw，(rx,ry) 方向 ±hh
        float cx[4], cy[4];
        for (int c = 0; c < 4; ++c) {
            const float st = (c == 0 || c == 3) ? +hw : -hw;
            const float sr = (c == 0 || c == 1) ? +hh : -hh;
            cx[c] = tx * st + rx * sr;
            cy[c] = ty * st + ry * sr;
        }
        // 前后两面：法线相反。四个角的 u,v 固定（做边缘高光用）
        const float qu[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
        const float qv[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
        const int tri[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };
        for (int side = 0; side < 2; ++side) {
            const float f = (side == 0) ? 1.0f : -1.0f;
            for (int t2 = 0; t2 < 2; ++t2) {
                for (int q = 0; q < 3; ++q) {
                    const int ci = tri[t2][q];
                    out.push_back(cx[ci]);    // 相对位置（x）
                    out.push_back(cy[ci]);    // 相对位置（y）
                    out.push_back(0.0f);      // z 由着色器按轨道算，这里留 0
                    out.push_back(qu[ci]);
                    out.push_back(qv[ci]);
                    out.push_back(nx * f); out.push_back(ny * f); out.push_back(nz * f);
                    out.push_back((float)si);
                    out.push_back(s.phase);
                    out.push_back(s.omega);
                    out.push_back(s.radius);
                    out.push_back(s.elev);
                    out.push_back(s.size);
                    out.push_back(s.hue);
                    out.push_back(s.alpha);
                }
            }
        }
    }
    while (bucketNow < XS_BUCKETS) bucketVertStart[++bucketNow] = (int)(out.size() / XSStride());
}
