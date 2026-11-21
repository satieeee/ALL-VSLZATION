// C3OrigamiGeom.h —— Cubeeffect3 折纸片系统的【纯几何】部分
//
// 单独抽出来的原因：这几个函数是折纸片能不能正常出现的全部风险所在
// （内缩失败 → 池子是空的 → 一张纸都不会有），而它们不依赖任何全局状态，
// 所以可以在命令行里离线跑一遍验证（见 _hittest/origami_test.cpp），
// 不必启动 GUI 才知道对不对。
//
// 坐标空间：全部在【画布 px】里算。画布是生成 C3Lines.h 时的那张
// W = kC3CanvasH * kC3ImgA、H = kC3CanvasH 的归一化 [0,1] 图。
// 画布 px 是各向同性的（x/y 两个方向 1px 对应的世界距离相同），
// 所以"内缩 N 像素"直接就是一个标量偏移量。
#pragma once
#include "C3Lines.h"
#include <cmath>
#include <vector>

/// 画布宽度（px）
static inline float C3CanvasW() { return kC3CanvasH * kC3ImgA; }

/// 凸多边形的【面积重心】与面积。注意不是顶点平均值 —— 纸片的几何重心是面积重心。
static inline void C3PolyCentroid(const float* vx, const float* vy, int n,
                                  float& cx, float& cy, float& area) {
    float a = 0, sx = 0, sy = 0;
    for (int i = 0; i < n; ++i) {
        int j = (i + 1) % n;
        float cr = vx[i] * vy[j] - vx[j] * vy[i];
        a += cr; sx += (vx[i] + vx[j]) * cr; sy += (vy[i] + vy[j]) * cr;
    }
    a *= 0.5f;
    area = fabsf(a);
    if (fabsf(a) < 1e-9f) { cx = vx[0]; cy = vy[0]; return; }
    cx = sx / (6.0f * a);
    cy = sy / (6.0f * a);
}

/// 从内部点 (cx,cy) 沿单位方向 (ux,uy) 射出，打到凸多边形边界的距离（1e30 = 没打中）
static inline float C3RayHit(const float* vx, const float* vy, int n,
                             float cx, float cy, float ux, float uy) {
    float best = 1e30f;
    for (int i = 0; i < n; ++i) {
        int j = (i + 1) % n;
        float ex = vx[j] - vx[i], ey = vy[j] - vy[i];
        float D = ux * ey - uy * ex;
        if (fabsf(D) < 1e-9f) continue;                 // 与这条边平行
        float qx = vx[i] - cx, qy = vy[i] - cy;
        float t = (qx * ey - ex * qy) / D;              // 沿射线
        float s = (uy * qx - ux * qy) / D;              // 沿边参数，必须落在 [0,1]
        if (t > 1e-4f && s >= -1e-4f && s <= 1.0f + 1e-4f && t < best) best = t;
    }
    return best;
}

/// 凸多边形【整体向内缩 d 像素】。返回顶点数，0 = 失败（退化/自交/太扁）。
/// 做法：每条边沿内法线平移 d，再与相邻平移边求交 → 新顶点。
/// 输出统一为面积 > 0 的绕向，可直接当扇形三角化（扇心 = 重心）用。
static inline int C3InsetConvex(const float* inx, const float* iny, int n, float d,
                                float* outx, float* outy) {
    if (n < 3 || n > 8) return 0;
    // 统一绕向：面积 > 0 时 (-ey, ex) 才是朝内的法线
    float a2 = 0;
    for (int i = 0; i < n; ++i) { int j = (i + 1) % n; a2 += inx[i] * iny[j] - inx[j] * iny[i]; }
    float px[8], py[8];
    for (int i = 0; i < n; ++i) { int k = (a2 < 0) ? (n - 1 - i) : i; px[i] = inx[k]; py[i] = iny[k]; }

    for (int i = 0; i < n; ++i) {
        int j = (i + 1) % n, j2 = (i + 2) % n;
        float e1x = px[j] - px[i],  e1y = py[j] - py[i];
        float e2x = px[j2] - px[j], e2y = py[j2] - py[j];
        float L1 = sqrtf(e1x * e1x + e1y * e1y), L2 = sqrtf(e2x * e2x + e2y * e2y);
        if (L1 < 1e-4f || L2 < 1e-4f) return 0;
        float n1x = -e1y / L1, n1y = e1x / L1;
        float n2x = -e2y / L2, n2y = e2x / L2;
        // 平移后的边 i 与边 i+1 求交 = 内缩后的第 i 个顶点
        float ax = px[i] + n1x * d, ay = py[i] + n1y * d;
        float bx = px[j] + n2x * d, by = py[j] + n2y * d;
        float den = e1x * e2y - e1y * e2x;
        if (fabsf(den) < 1e-9f) return 0;
        float t = ((bx - ax) * e2y - (by - ay) * e2x) / den;
        outx[i] = ax + e1x * t;
        outy[i] = ay + e1y * t;
    }
    // 内缩后必须还是个像样的凸多边形
    float cx, cy, ar;
    C3PolyCentroid(outx, outy, n, cx, cy, ar);
    if (ar < 100.0f) return 0;                 // 10×10px 以下不要
    int sgn = 0;
    for (int i = 0; i < n; ++i) {
        int j = (i + 1) % n, k = (i + 2) % n;
        float cr = (outx[j] - outx[i]) * (outy[k] - outy[j])
                 - (outy[j] - outy[i]) * (outx[k] - outx[j]);
        if (fabsf(cr) < 1e-6f) continue;
        int s = (cr > 0) ? 1 : -1;
        if (sgn == 0) sgn = s; else if (s != sgn) return 0;   // 变成凹的了
    }
    return n;
}

// ---- 折纸片参数（GPU / CPU 两个后端共用，改一份两边同时生效）----
// 生命周期【完全绑定到 hit】：一次 hit 出一批，下一批出现时上一批立刻淡出，
// 不按各自寿命自然消亡（LIFE/FADE 只是"没有下一次 hit"时的兜底）。
const int   C3_ORIGAMI_PER_HIT = kC3PolyN / 2;  // 单次 hit 输出个数 = 全部格子数的 50%
const int   C3_ORIGAMI_MAX     = 560;    // 存活上限（一批 240 + 上一批淡出中的 240，留余量）
const float C3_ORIGAMI_INSET   = 15.0f;  // 与格子边界的固定距离（画布像素）
const float C3_ORIGAMI_REVEAL  = 0.22f;  // 显化时长（秒）—— 必须短于鼓点间隔
const float C3_ORIGAMI_KILL    = 0.12f;  // 被下一批顶掉时的淡出时长（秒）
const float C3_ORIGAMI_LIFE    = 8.0f;   // 兜底寿命（秒）
const float C3_ORIGAMI_FADE    = 0.5f;   // 兜底淡出时长（秒）

// ============================================================================
// 折纸片：一张纸 = 内缩后的格子轮廓 + 两条折痕（双次对折）切出的 4 个"面"
// ============================================================================
struct Origami {
    int   poly;              // 所在格子索引（同批去重用）
    int   n;                 // 纸片轮廓顶点数
    float vx[8], vy[8];      // 内缩后的轮廓（画布 px）
    float cx, cy;            // 面积重心 = 折痕交点（画布 px）
    float dx[2], dy[2];      // 折痕方向（单位向量，两条）
    float rf[2], rb[2];      // 交点沿 ±方向 到纸片边界的距离（画布 px）
    // 两条折痕把纸片切成 4 个面，各面单独打光 —— "折纸看起来是立体的"全靠这个。
    // 每个面是从重心扇形展开的一片：边界点 = [折痕与纸片边界的交点] + [落在该扇区内的
    // 轮廓顶点（按角度排好）] + [另一条折痕与边界的交点]。
    // 必须沿折痕切开：否则三角形会横跨两个面，没法给面上不同的明暗。
    int   pn[4];             // 每个面的边界点数（<3 表示该面无效）
    float px[4][12], py[4][12];
    float shade[4];          // 每个面的明暗系数（1.0 = 中性）
    float spawnT, delay;     // 出生时间 / 同批内错开
    float killT;             // 被下一批顶掉的时间（0 = 还活着）；之后 C3_ORIGAMI_KILL 秒内淡出
};

/// 沿两条折痕把纸片切成 4 个面，并给每个面算明暗。
///
/// 明暗模型：把每个面当成一块【绕折痕转过 SLOPE 角】的平板，用固定平行光做 Lambert。
/// 山折/谷折交替（相邻面朝相反方向翻）—— 这是纸张来回折叠后的真实姿态，
/// 而且能让 4 个面的法线两两不同，在斜向光下得到 4 个互不相同的亮度。
static inline void C3BuildPanels(Origami& o) {
    const float TAU = 6.2831853f;
    float rx[4], ry[4], ra[4];
    rx[0] =  o.dx[0]; ry[0] =  o.dy[0];
    rx[1] =  o.dx[1]; ry[1] =  o.dy[1];
    rx[2] = -o.dx[0]; ry[2] = -o.dy[0];
    rx[3] = -o.dx[1]; ry[3] = -o.dy[1];
    for (int k = 0; k < 4; ++k) { ra[k] = atan2f(ry[k], rx[k]); o.pn[k] = 0; }

    // 按角度绕重心排序（决定 4 个面的环绕顺序）
    int ord[4] = { 0, 1, 2, 3 };
    for (int a = 0; a < 3; ++a)
        for (int b = a + 1; b < 4; ++b)
            if (ra[ord[b]] < ra[ord[a]]) { int t = ord[a]; ord[a] = ord[b]; ord[b] = t; }

    for (int s = 0; s < 4; ++s) {
        o.shade[s] = 1.0f;                    // 万一这个面建不出来也别留垃圾值
        int iA = ord[s], iB = ord[(s + 1) % 4];
        float aA = ra[iA];
        float aB = ra[iB]; if (aB <= aA) aB += TAU;
        float hA = C3RayHit(o.vx, o.vy, o.n, o.cx, o.cy, rx[iA], ry[iA]);
        float hB = C3RayHit(o.vx, o.vy, o.n, o.cx, o.cy, rx[iB], ry[iB]);
        if (hA > 1e29f || hB > 1e29f) continue;

        int m = 0;
        o.px[s][0] = o.cx + rx[iA] * hA; o.py[s][0] = o.cy + ry[iA] * hA; ++m;
        // 落在这个扇区里的轮廓顶点（按角度从小到大插入）
        int idx[8], cnt = 0; float va[8];
        for (int v = 0; v < o.n; ++v) {
            float ux = o.vx[v] - o.cx, uy = o.vy[v] - o.cy;
            if (fabsf(ux) < 1e-4f && fabsf(uy) < 1e-4f) continue;
            float a = atan2f(uy, ux); while (a < aA) a += TAU;
            if (a > aA + 1e-5f && a < aB - 1e-5f) { idx[cnt] = v; va[cnt] = a; ++cnt; }
        }
        for (int x = 0; x < cnt; ++x)
            for (int y = x + 1; y < cnt; ++y)
                if (va[idx[y]] < va[idx[x]]) { int t = idx[x]; idx[x] = idx[y]; idx[y] = t;
                                               float u = va[x]; va[x] = va[y]; va[y] = u; }
        for (int x = 0; x < cnt && m < 11; ++x) {
            o.px[s][m] = o.vx[idx[x]]; o.py[s][m] = o.vy[idx[x]]; ++m;
        }
        o.px[s][m] = o.cx + rx[iB] * hB; o.py[s][m] = o.cy + ry[iB] * hB; ++m;
        o.pn[s] = m;

        // 面的"朝外方向" = 扇区角平分线；山折(s 偶) / 谷折(s 奇) 让法线朝相反一侧偏
        float mid = 0.5f * (aA + aB);
        float ux = cosf(mid), uy = sinf(mid);
        float sg = (s & 1) ? -1.0f : 1.0f;
        const float SLOPE = 0.45f;                 // 折叠角：越大凹凸越明显
        float nx = sg * ux * SLOPE, ny = sg * uy * SLOPE, nz = 1.0f;
        float il = 1.0f / sqrtf(nx * nx + ny * ny + nz * nz);
        nx *= il; ny *= il; nz *= il;
        // 固定平行光：从"左上偏前"打过来（画布 y 向下 → 上方是 -y）
        const float LX = -0.641f, LY = -0.299f, LZ = 0.707f;
        float lam = nx * LX + ny * LY + nz * LZ;
        if (lam < 0.0f) lam = 0.0f;
        float sh = 0.52f + 1.05f * lam;            // 常数项保证暗面不糊成黑
        o.shade[s] = (sh > 1.7f) ? 1.7f : sh;
    }
}

/// 筛出"放得下折纸"的格子（GPU 与 CPU 两个后端共用）：
/// 不跨坐标轴（跨轴的话扭曲时顶点会被夹回原侧、纸片会被压扁）、内缩后仍是像样的凸多边形。
/// 只需算一次，调用方自行缓存。
inline void C3BuildOrigamiPool(std::vector<int>& pool, float inset) {
    pool.clear();
    const float W = C3CanvasW(), H = kC3CanvasH;
    float vx[8], vy[8], ox[8], oy[8];
    for (int p = 0; p < kC3PolyN; ++p) {
        int i0 = kC3PolyOff[p];
        int n = kC3PolyOff[p + 1] - i0;
        if (n < 3 || n > 8) continue;
        for (int k = 0; k < n; ++k) {
            vx[k] = kC3PolyV[i0 + k][0] * W;
            vy[k] = kC3PolyV[i0 + k][1] * H;
        }
        bool straddle = false;
        for (int a = 0; a < n && !straddle; ++a) {
            if (fabsf(vx[a] - W * 0.5f) < 3.0f || fabsf(vy[a] - H * 0.5f) < 3.0f) straddle = true;
            for (int b = a + 1; b < n && !straddle; ++b) {
                if ((vx[a] - W * 0.5f) * (vx[b] - W * 0.5f) < 0.0f) straddle = true;
                if ((vy[a] - H * 0.5f) * (vy[b] - H * 0.5f) < 0.0f) straddle = true;
            }
        }
        if (straddle) continue;
        if (C3InsetConvex(vx, vy, n, inset, ox, oy) < 3) continue;
        pool.push_back(p);
    }
}

/// 由【任意凸多边形】造出一张折纸片（几何部分；出生时间由调用方填）。失败返回 false。
///   v —— 交错存的 x,y（v[k][0]=x, v[k][1]=y，单位随意，但必须与 inset 同量纲）；
///   n —— 顶点数（3..8）。
/// 【为什么不绑死 C3 的画布坐标】Filigree 的隧道内壁剪纸也走这一份 ——
/// 内缩/折痕/分面是"两个用途必须给出同一结果"的逻辑，各写一份迟早漂移
/// （这正是 C3OrigamiGeom.h 当初从 main.cpp 抽出来时立的规矩）。
static inline bool C3MakeOrigamiShape(const float (*v)[2], int n, float inset, Origami& o) {
    if (!v || n < 3 || n > 8) return false;
    float vx[8], vy[8];
    for (int k = 0; k < n; ++k) {
        vx[k] = v[k][0];
        vy[k] = v[k][1];
    }
    o.n = C3InsetConvex(vx, vy, n, inset, o.vx, o.vy);
    if (o.n < 3) return false;
    float ar; C3PolyCentroid(o.vx, o.vy, o.n, o.cx, o.cy, ar);

    // ---- 双次对折的折痕方向 ----
    if (o.n == 4) {
        // 两组对边的中点连线 —— "对折"就是沿中线折。两条中线交于重心（凸四边形恒成立）。
        o.dx[0] = (o.vx[2] + o.vx[3]) * 0.5f - (o.vx[0] + o.vx[1]) * 0.5f;
        o.dy[0] = (o.vy[2] + o.vy[3]) * 0.5f - (o.vy[0] + o.vy[1]) * 0.5f;
        o.dx[1] = (o.vx[3] + o.vx[0]) * 0.5f - (o.vx[1] + o.vx[2]) * 0.5f;
        o.dy[1] = (o.vy[3] + o.vy[0]) * 0.5f - (o.vy[1] + o.vy[2]) * 0.5f;
    } else {
        // 五边形（及更多边）：没有"对边中线"，用顶点协方差主轴代替
        float mx = 0, my = 0;
        for (int k = 0; k < o.n; ++k) { mx += o.vx[k]; my += o.vy[k]; }
        mx /= o.n; my /= o.n;
        float sxx = 0, sxy = 0, syy = 0;
        for (int k = 0; k < o.n; ++k) {
            float ax = o.vx[k] - mx, ay = o.vy[k] - my;
            sxx += ax * ax; sxy += ax * ay; syy += ay * ay;
        }
        float th = 0.5f * atan2f(2.0f * sxy, sxx - syy);
        o.dx[0] = cosf(th); o.dy[0] = sinf(th);
        o.dx[1] = -o.dy[0]; o.dy[1] = o.dx[0];
    }
    for (int k = 0; k < 2; ++k) {
        float L = sqrtf(o.dx[k] * o.dx[k] + o.dy[k] * o.dy[k]);
        if (L < 1e-5f) return false;
        o.dx[k] /= L; o.dy[k] /= L;
        o.rf[k] = C3RayHit(o.vx, o.vy, o.n, o.cx, o.cy, o.dx[k], o.dy[k]);
        o.rb[k] = C3RayHit(o.vx, o.vy, o.n, o.cx, o.cy, -o.dx[k], -o.dy[k]);
        if (o.rf[k] > 1e29f || o.rb[k] > 1e29f) return false;
    }
    C3BuildPanels(o);
    return true;
}

/// C3 用的薄包装：从烘焙好的第 polyIdx 个格子取多边形（换算成画布像素坐标），
/// 行为与重构前【完全一致】。GPU 与 CPU 两个后端都调它 —— 别改成直接调上面那个，
/// 否则画布坐标换算就得在两处各写一份。
static inline bool C3MakeOrigamiShapeFromCell(int polyIdx, float inset, Origami& o) {
    if (polyIdx < 0 || polyIdx >= kC3PolyN) return false;
    const int i0 = kC3PolyOff[polyIdx];
    const int n  = kC3PolyOff[polyIdx + 1] - i0;
    if (n < 3 || n > 8) return false;
    const float W = C3CanvasW(), H = kC3CanvasH;
    float tmp[8][2];
    for (int k = 0; k < n; ++k) {
        tmp[k][0] = kC3PolyV[i0 + k][0] * W;
        tmp[k][1] = kC3PolyV[i0 + k][1] * H;
    }
    if (!C3MakeOrigamiShape(tmp, n, inset, o)) return false;
    o.poly = polyIdx;
    return true;
}
