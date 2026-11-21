/**
 * @file VTGeom.h
 * @brief Filigree（隧道 · 细长金属条 · 内壁剪纸）的几何生成 —— 纯数学，不碰 GL
 *
 * 抽成独立头的理由与 VXGeom.h / C3OrigamiGeom.h / C3Veil.h 一致：
 * 这是"GPU 与 CPU 将来必须给出同一结果"的部分，各写一份迟早漂移。
 * 而且它无 GL 依赖 —— _hittest/vt_geom_test.cpp 能直接编译它做离线验证。
 *
 * ---------------------------------------------------------------------------
 * 三块几何
 * ---------------------------------------------------------------------------
 * 1) 隧道壁 —— 断面是带棱角的花瓣形
 *
 *        r(θ) = R · (1 + a·cos(kθ))        k = VT_K 个花瓣
 *
 *    沿 z 复制成管状曲面。与 VXGeom 一样【一个周期只生成一份】，
 *    渲染侧用 LAYERS 份在视深上错开并回绕 —— 几何量不随"无限"增长。
 *
 *    硬分面与 VX 同一套（相邻顶点沿径向棋盘格 ±h → 四边形变鞍面），
 *    但【法线烘进顶点】而不用 dFdx/dFdy 在片元里推 —— 本项目实测后者
 *    在小三角形上逐帧不确定（见 memory/normal-from-derivatives-unreliable，
 *    Involute 正是受害者）。这里对每个三角形算面法线再赋给它的三个顶点。
 *
 * 2) 金属长条（ribbon）—— 沿隧道轴延伸的细长扁条
 *
 *    脊线沿 z 缓慢游走，截面（宽 × 厚）沿长度从"方管"变到"极扁的箔"，
 *    再叠一个沿长度累积的扭转。每个截面吐【前后两个面】各 6 个顶点 ——
 *    背面靠片元着色器翻法线（同 kF1FS 的双面受光写法）。
 *    建好后按 z 分 VT_RIB_BUCKETS 桶在 VBO 里连续存放，渲染侧逐桶提交，
 *    让半透明的远端淡出也按远近排序。
 *
 * 3) 内壁贴点 —— 剪纸花纹贴到壁上用
 *
 *    VTWallPoint(u, v) 把断面参数映射成世界点与【朝内】的法线。
 *    剪纸片必须【先在扁平的 (u,v) 空间里做完内缩与分面，再映射到柱面】：
 *    C3InsetConvex 是"沿 2D 边法线偏移标量 d"，那个前提只在平坦各向同性
 *    平面上成立；先上柱面再内缩会得到自交轮廓（C3 那边记录过）。
 *
 * ---------------------------------------------------------------------------
 * 与渲染侧的约定（改这里必须同步改 main.cpp 的 kTVS / kVTFS）
 * ---------------------------------------------------------------------------
 *   隧道壁  每顶点 8 float = [x, y, z, u, v, nx, ny, nz]
 *   长条    每顶点 9 float = [x, y, z, u, v, nx, ny, nz, phase]
 */
#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include "../C3/C3Veil.h"   // 只借用 C3Rnd —— 不再造第三个同款哈希
                            // （C3 agent 指出 cubeRnd 与 C3Rnd 是字节相同的重复，别扩散）

// ---------------------------------------------------------------------------
// 隧道断面参数（改这里就等于改整个效果的外观）
// ---------------------------------------------------------------------------
const float VT_R        = 1.60f;      // 断面基准半径
const int   VT_K        = 8;          // 花瓣数 —— 棱角的来源
const float VT_A        = 0.12f;      // 花瓣起伏深度（占 R 的比例）
const int   VT_SEGS     = 128;        // 断面采样点数（= 周向四边形数）
const int   VT_RINGS    = 64;         // 沿 z 的环数
const float VT_PERIOD   = 8.0f;       // 一个周期沿 z 的长度（世界单位）
const float VT_FACET_H  = 0.035f;     // 棋盘格分面起伏（沿径向 ±）
const float VT_TWIST    = 6.2831853f; // 一个周期里断面自转角度（2π = 正好一圈，回绕无缝）

// ---------------------------------------------------------------------------
// ribbon 参数
// ---------------------------------------------------------------------------
// 数量随"变细"一起加上去：单根变窄会让覆盖掉下来，靠数量补回密度。
const int   VT_RIBBONS     = 420;   // 长条数量
const int   VT_RIB_SEG     = 40;    // 每根的截面段数
const int   VT_RIB_BUCKETS = 6;     // 按 z 分的提交桶数
// 【长短随机】每根自己抽一个长度。细条是【平行于隧道轴】的长条，
// 长得能从近端一直伸到很远，短的就是一小截 —— 长短差拉开才有"密林"感。
const float VT_RIB_LEN_MIN =  5.0f;
const float VT_RIB_LEN_MAX = 30.0f;
// 细条允许占据的 z 区间。【可见带是近端淡入 1.5 → 远端 ~23】——
// 撒在 z<1.5 的会被近端雾吃掉。z0 再按各自长度收进这个带里。
const float VT_RIB_ZMIN    = -2.0f;
const float VT_RIB_ZMAX    =  26.0f;
const float VT_RIB_RMIN    = 0.12f; // 脊线到中轴的半径范围（避开正中轴，也别贴壁）
const float VT_RIB_RMAX    = 1.30f; // 上限 = RMAX + DRIFT + 最大半宽 要 < VTMaxRadius()，
                                    // 否则会穿壁（离线几何台会直接判 FAIL）
// 每根的横截面尺寸再乘一个随机系数 —— "大小随机"
const float VT_RIB_SIZE_LO = 0.55f;
const float VT_RIB_SIZE_HI = 1.65f;
// 【要"skinny"就得真的细】第一版半宽 0.06~0.26（相对隧道半径 1.6）读起来是
// 飘带/纱幔，不是细长的金属条。压到 1/3。
const float VT_RIB_W0      = 0.026f;// 起始半宽（方管端）
const float VT_RIB_W1      = 0.085f;// 末端半宽（箔端）
const float VT_RIB_TH0     = 0.032f;// 起始半厚
const float VT_RIB_TH1     = 0.006f;// 末端半厚
// 脊线横向游走幅度。【用户明确要的是"与轴方向平行"】——所以这里只留一丁点
// 起伏当作手工感，不是蛇形。别再把这里调大去"让它看得见"：
// 当初看不见的真因是四边形取角写错导致三角形全退化（见 VTRibbonBuild 的注释），
// 与朝向无关 —— 我把两件事混成一件，白绕了一轮。
const float VT_RIB_DRIFT   = 0.045f;
const float VT_RIB_WAVE    = 0.055f;// 渲染时沿 xy 的缓慢摆动幅度（世界单位）——
                                    // 放在这里而不是 main.cpp，因为离线渲染台也要用同一份；
                                    // 写在 main.cpp 就成了第二处真相（台子编译不过就是证据）。
const float VT_RIB_LIFT    = 0.010f;// 贴到壁上时的抬升（避免与壁面 z-fighting）

// ---------------------------------------------------------------------------
// 断面
// ---------------------------------------------------------------------------
/// 半径 r(θ) = R·(1 + a·cos(kθ))
inline float VTProfile(float theta) {
    return VT_R * (1.0f + VT_A * std::cos((float)VT_K * theta));
}
/// 半径的导数 r'(θ) —— 解析法线要用
inline float VTProfileD(float theta) {
    return -VT_R * VT_A * (float)VT_K * std::sin((float)VT_K * theta);
}
/// 曲面上离中轴最远的那一圈的半径（花瓣峰 + 棋盘格外凸半步）。外缘微光靠它定位。
inline float VTMaxRadius() {
    return VT_R * (1.0f + VT_A) + VT_FACET_H;
}

// ---------------------------------------------------------------------------
// 断面上的一个点。radialOffset 是沿径向的额外偏移（棋盘格分面 / 贴纸抬升都用它）。
// 法线【朝内】（指向中轴，也就是相机在的那一侧）。
// ---------------------------------------------------------------------------
inline void VTSurfacePoint(float uu, float vv, float radialOffset,
                           float& x, float& y, float& z,
                           float& nx, float& ny, float& nz)
{
    const float theta = uu * 6.2831853f;
    const float r     = VTProfile(theta);
    const float dr    = VTProfileD(theta);

    // 自转：位置与法线都必须用【转过之后】的量（VXGeom 的教训 —— 转半圈的那一半
    // 用基座标算出来的方向会正好相反、朝内）
    const float ang = VT_TWIST * vv;
    const float ca = std::cos(ang), sa = std::sin(ang);

    const float ux = std::cos(theta), uy = std::sin(theta);
    const float px = (r + radialOffset) * ux;
    const float py = (r + radialOffset) * uy;

    x = px * ca - py * sa;
    y = px * sa + py * ca;
    z = VT_PERIOD * vv;

    // 解析法线：P(u) = (r·cosθ, r·sinθ, z)，∂P/∂z = (0,0,1)
    //   ∂P/∂θ × ∂P/∂z = ( dr·sinθ + r·cosθ , r·sinθ − dr·cosθ , 0 )
    // 圆（dr=0）时得到径向外向；隧道看内壁，取反让它朝【内】。
    float bnx = -(dr * uy + r * ux);
    float bny = -(r * uy - dr * ux);
    float L = std::sqrt(bnx * bnx + bny * bny);
    if (L < 1e-6f) { bnx = -ux; bny = -uy; L = 1.0f; }
    bnx /= L; bny /= L;

    nx = bnx * ca - bny * sa;
    ny = bnx * sa + bny * ca;
    nz = 0.0f;
}

/// 内壁贴点：把 (u,v) 映射到壁上（略微抬起）。剪纸花纹用它。
inline void VTWallPoint(float u, float v,
                        float& x, float& y, float& z,
                        float& nx, float& ny, float& nz) {
    VTSurfacePoint(u, v, VT_FACET_H + VT_RIB_LIFT, x, y, z, nx, ny, nz);
}

// ===========================================================================
// 隧道壁
// ===========================================================================
inline int VTVertexStride() { return 8; }
/// 顶点数：每四边形 2 三角形 × 3 顶点 = 6（不共享 —— 非平面四边形两半法线本就
/// 不同，共享会把分面抹平，同 VXGeom 的取舍）
inline int VTVertexCount() { return VT_SEGS * VT_RINGS * 6; }

/// 生成一个周期的隧道壁。法线逐三角形算 —— 取三个顶点的位置求面法线再赋给它们，
/// 同一四边形拆出的两个三角形各自硬分面，且逐帧确定可复现。
inline void VTBuildMesh(std::vector<float>& out) {
    out.clear();
    out.reserve((size_t)VTVertexCount() * VTVertexStride());

    auto pos = [&](int i, int k, float* p, float& u, float& v) {
        const int ii = ((i % VT_SEGS) + VT_SEGS) % VT_SEGS;
        const int kk = k % (VT_RINGS + 1);
        const float uu = (float)ii / (float)VT_SEGS;
        const float vv = (float)kk / (float)VT_RINGS;
        // 棋盘格：相邻顶点一凸一凹 → 四边形变鞍面 → 硬分面
        const float off = (((ii + kk) & 1) != 0) ? VT_FACET_H : -VT_FACET_H;
        float nx, ny, nz;
        VTSurfacePoint(uu, vv, off, p[0], p[1], p[2], nx, ny, nz);
        u = uu; v = vv;
    };

    float pa[3], pb[3], pc[3], pd[3];
    float ua, va, ub, vb, uc, vc, ud, vd;
    for (int k = 0; k < VT_RINGS; ++k) {
        for (int i = 0; i < VT_SEGS; ++i) {
            pos(i,     k,     pa, ua, va);
            pos(i + 1, k,     pb, ub, vb);
            pos(i + 1, k + 1, pc, uc, vc);
            pos(i,     k + 1, pd, ud, vd);

            // 四边形 → (a,b,c) + (a,c,d)，两个三角形各算各的面法线
            const float* tp[2][3] = { { pa, pb, pc }, { pa, pc, pd } };
            const float  tu[2][3] = { { ua, ub, uc }, { ua, uc, ud } };
            const float  tv[2][3] = { { va, vb, vc }, { va, vc, vd } };

            for (int t = 0; t < 2; ++t) {
                const float* A = tp[t][0];
                const float* B = tp[t][1];
                const float* C = tp[t][2];
                float e1[3] = { B[0]-A[0], B[1]-A[1], B[2]-A[2] };
                float e2[3] = { C[0]-A[0], C[1]-A[1], C[2]-A[2] };
                float n[3] = { e1[1]*e2[2] - e1[2]*e2[1],
                               e1[2]*e2[0] - e1[0]*e2[2],
                               e1[0]*e2[1] - e1[1]*e2[0] };
                float L = std::sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
                if (L < 1e-12f) { n[0] = 0.0f; n[1] = 0.0f; n[2] = -1.0f; }
                else { n[0] /= L; n[1] /= L; n[2] /= L; }

                // 朝外就翻过来。判据：重心相对中轴的水平方向 · 法线 > 0 = 朝外。
                const float gx = (A[0] + B[0] + C[0]) / 3.0f;
                const float gy = (A[1] + B[1] + C[1]) / 3.0f;
                if (n[0]*gx + n[1]*gy > 0.0f) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; }

                for (int q = 0; q < 3; ++q) {
                    out.push_back(tp[t][q][0]);
                    out.push_back(tp[t][q][1]);
                    out.push_back(tp[t][q][2]);
                    out.push_back(tu[t][q]);
                    out.push_back(tv[t][q]);
                    out.push_back(n[0]);
                    out.push_back(n[1]);
                    out.push_back(n[2]);
                }
            }
        }
    }
}

// ===========================================================================
// 金属长条（ribbon）
// ===========================================================================
// 每个顶点 10 个 float：pos3 + uv2 + nrm3 + phase1 + 【本根原本的长度】1。
// 最后那个长度是给顶点着色器用的：频谱要改【针的长短】就必须知道它原来多长 ——
// 细条是沿 z 的直线（p.z = 尾端z + v·len，v 就是 aUV.y），知道 len 就能反推尾端，绕尾端缩放。
inline int VTRibbonStride() { return 13; }
/// 每段 12 顶点 = 前面 6 + 背面 6
inline int VTRibbonVertexCount() { return VT_RIBBONS * VT_RIB_SEG * 12; }

/// 每根 ribbon 的恒定参数（建几何与分桶共用，保证两边一致）
struct VTRibbonSpec {
    float cx, cy;      // 脊线中心（断面坐标）
    float ax, ay;      // 横向游走幅度
    float f1, f2;      // 游走频率
    float ph;          // 随机相位
    float z0;          // 起点 z
    float len;         // 本根的长度（逐根随机）
    float sizeMul;     // 横截面尺寸的随机倍率（逐根随机）
    float w0, w1;      // 起止半宽
    float t0, t1;      // 起止半厚
    float twist;       // 总扭转角（沿长度累积）
    float phase;       // 交给着色器的随机相位 [0,1)
    int   bucket;      // 提交桶（按 z0 排序后等分）
};

/// 生成全部 ribbon 的参数表。确定性（只用 C3Rnd），GPU/CPU 与离线台结果一致。
inline void VTRibbonSpecs(std::vector<VTRibbonSpec>& out) {
    out.resize(VT_RIBBONS);
    for (int i = 0; i < VT_RIBBONS; ++i) {
        VTRibbonSpec& s = out[i];
        const float rr = VT_RIB_RMIN + (VT_RIB_RMAX - VT_RIB_RMIN) * C3Rnd(i, 11);
        const float aa = 6.2831853f * C3Rnd(i, 12);
        s.cx = rr * std::cos(aa);
        s.cy = rr * std::sin(aa);
        s.ax = VT_RIB_DRIFT * (0.45f + 0.55f * C3Rnd(i, 13));
        s.ay = VT_RIB_DRIFT * (0.45f + 0.55f * C3Rnd(i, 14));
        s.f1 = 0.6f + 1.8f * C3Rnd(i, 15);
        s.f2 = 0.6f + 1.8f * C3Rnd(i, 16);
        s.ph = C3Rnd(i, 17);
        // 长短随机；z0 按各自长度收进可见带（长条起点靠前、短条能落在任意深度）
        s.len = VT_RIB_LEN_MIN + (VT_RIB_LEN_MAX - VT_RIB_LEN_MIN) * C3Rnd(i, 23);
        {
            const float span = VT_RIB_ZMAX - VT_RIB_ZMIN - s.len;
            s.z0 = VT_RIB_ZMIN + (span > 0.0f ? span * C3Rnd(i, 18) : 0.0f);
        }
        s.sizeMul = VT_RIB_SIZE_LO + (VT_RIB_SIZE_HI - VT_RIB_SIZE_LO) * C3Rnd(i, 24);
        // 一半的条是"由粗变细"，另一半反过来 —— 避免整片同质
        const bool flip = C3Rnd(i, 19) < 0.5f;
        s.w0 = flip ? VT_RIB_W1 : VT_RIB_W0;
        s.w1 = flip ? VT_RIB_W0 : VT_RIB_W1;
        s.t0 = flip ? VT_RIB_TH1 : VT_RIB_TH0;
        s.t1 = flip ? VT_RIB_TH0 : VT_RIB_TH1;
        s.twist = (C3Rnd(i, 20) < 0.5f ? -1.0f : 1.0f) * 3.1415927f * (0.4f + 1.2f * C3Rnd(i, 21));
        s.phase = C3Rnd(i, 22);
        s.bucket = 0;   // 稍后按 z0 排序填
    }
    // 按 z0 排序 → 排序位次等分成 VT_RIB_BUCKETS 桶，VBO 里就天然连续
    std::vector<int> ord(VT_RIBBONS);
    for (int i = 0; i < VT_RIBBONS; ++i) ord[i] = i;
    std::sort(ord.begin(), ord.end(), [&](int a, int b) { return out[a].z0 < out[b].z0; });
    for (int r = 0; r < VT_RIBBONS; ++r) {
        int b = r * VT_RIB_BUCKETS / VT_RIBBONS;
        if (b >= VT_RIB_BUCKETS) b = VT_RIB_BUCKETS - 1;
        out[ord[r]].bucket = b;
    }
}

/// 生成全部 ribbon 的网格，**按桶连续存放**（先按 z0 排序再建）。
/// bucketVertStart[b] 返回第 b 桶的起始顶点下标（长度 VT_RIB_BUCKETS+1，
/// 末项 = 总顶点数），渲染侧据此逐桶 glDrawArrays。
inline void VTRibbonBuild(std::vector<float>& out, int* bucketVertStart) {
    std::vector<VTRibbonSpec> spec;
    VTRibbonSpecs(spec);

    // 按桶分组：桶内按 z0 排序，保证 VBO 里桶是连续段
    std::vector<int> ord(VT_RIBBONS);
    for (int i = 0; i < VT_RIBBONS; ++i) ord[i] = i;
    std::sort(ord.begin(), ord.end(), [&](int a, int b) {
        if (spec[a].bucket != spec[b].bucket) return spec[a].bucket < spec[b].bucket;
        return spec[a].z0 < spec[b].z0;
    });

    out.clear();
    out.reserve((size_t)VTRibbonVertexCount() * VTRibbonStride());
    for (int b = 0; b <= VT_RIB_BUCKETS; ++b) bucketVertStart[b] = 0;

    // ord 已按 (bucket, z0) 排好，所以桶在 out 里天然是连续段。
    // bucketNow 表示"当前已经进入的桶"；推进到某个还没被触及的桶时，
    // 记下此刻的顶点偏移作为它的起点。
    int bucketNow = -1;
    for (int r = 0; r < VT_RIBBONS; ++r) {
        const VTRibbonSpec& s = spec[ord[r]];
        while (bucketNow < s.bucket) {
            bucketVertStart[++bucketNow] = (int)(out.size() / VTRibbonStride());
        }

        // ---- 采样 N+1 个截面，相邻两个构成一段 ----
        const int N = VT_RIB_SEG;
        std::vector<float> cx(N + 1), cy(N + 1), cz(N + 1);
        std::vector<float> sx(N + 1), sy(N + 1), sz(N + 1);   // 侧向单位向量
        std::vector<float> ux_(N + 1), uy_(N + 1), uz_(N + 1); // 法向单位向量
        std::vector<float> hw(N + 1), ht(N + 1);

        for (int j = 0; j <= N; ++j) {
            const float t = (float)j / (float)N;
            cx[j] = s.cx + s.ax * std::sin(6.2831853f * (s.f1 * t + s.ph));
            cy[j] = s.cy + s.ay * std::sin(6.2831853f * (s.f2 * t + s.ph * 1.7f));
            cz[j] = s.z0 + s.len * t;

            // 切线（解析导数），再把参考"上"取成世界 +z
            const float dx = s.ax * 6.2831853f * s.f1 * std::cos(6.2831853f * (s.f1 * t + s.ph));
            const float dy = s.ay * 6.2831853f * s.f2 * std::cos(6.2831853f * (s.f2 * t + s.ph * 1.7f));
            const float dz = s.len;
            float L = std::sqrt(dx*dx + dy*dy + dz*dz);
            if (L < 1e-6f) L = 1e-6f;
            const float tx = dx/L, ty = dy/L, tz = dz/L;

            // side = normalize(cross(T, (0,0,1))) = normalize(ty, -tx, 0)
            float sl = std::sqrt(ty*ty + tx*tx);
            float sxx, syy;
            if (sl < 1e-6f) { sxx = 1.0f; syy = 0.0f; }
            else { sxx = ty/sl; syy = -tx/sl; }
            // up = normalize(cross(side, T))
            float upx = syy*tz - 0.0f*ty;
            float upy = 0.0f*tx - sxx*tz;
            float upz = sxx*ty - syy*tx;
            float ul = std::sqrt(upx*upx + upy*upy + upz*upz);
            if (ul < 1e-6f) { upx = 0.0f; upy = 0.0f; upz = 1.0f; ul = 1.0f; }
            upx /= ul; upy /= ul; upz /= ul;

            // 沿长度累积的扭转：把 (side, up) 基绕切线转一个角
            const float tw = s.twist * t;
            const float cw = std::cos(tw), sw = std::sin(tw);
            sx[j] = sxx * cw + upx * sw;
            sy[j] = syy * cw + upy * sw;
            sz[j] = 0.0f * cw + upz * sw;
            ux_[j] = -sxx * sw + upx * cw;
            uy_[j] = -syy * sw + upy * cw;
            uz_[j] = -0.0f * sw + upz * cw;

            // 横截面：沿长度的收放 × 逐根的随机倍率（"大小随机"）
            hw[j] = (s.w0 + (s.w1 - s.w0) * t) * s.sizeMul;
            ht[j] = (s.t0 + (s.t1 - s.t0) * t) * s.sizeMul;
        }

        // 四个角点：0=+S+U 1=-S+U 2=+S-U 3=-S-U
        auto corner = [&](int j, int c, float* o) {
            const float ss = (c == 0 || c == 2) ? 1.0f : -1.0f;
            const float uu = (c == 0 || c == 1) ? 1.0f : -1.0f;
            o[0] = cx[j] + sx[j]*hw[j]*ss + ux_[j]*ht[j]*uu;
            o[1] = cy[j] + sy[j]*hw[j]*ss + uy_[j]*ht[j]*uu;
            o[2] = cz[j] + sz[j]*hw[j]*ss + uz_[j]*ht[j]*uu;
        };

        for (int j = 0; j < N; ++j) {
            const float v0 = (float)j / (float)N, v1 = (float)(j+1) / (float)N;
            // 面法线取两端截面法向的平均（长条很扁，逐段平均足够，且逐帧确定）
            float fn[3] = { (ux_[j] + ux_[j+1])*0.5f, (uy_[j] + uy_[j+1])*0.5f, (uz_[j] + uz_[j+1])*0.5f };
            float fl = std::sqrt(fn[0]*fn[0] + fn[1]*fn[1] + fn[2]*fn[2]);
            if (fl < 1e-6f) { fn[0]=0; fn[1]=0; fn[2]=1; fl=1; }
            fn[0]/=fl; fn[1]/=fl; fn[2]/=fl;

            float c0[4][3], c1[4][3];
            for (int c = 0; c < 4; ++c) { corner(j, c, c0[c]); corner(j+1, c, c1[c]); }

            // 前面 = 角 0/1（+U 侧）；背面 = 角 2/3（-U 侧）
            // 四边形 = 【近端两角 + 远端两角】：(c0[ia], c0[ib], c1[ib], c1[ia])
            //
            // 【这里我第一版写错了，代价很大】—— 原来用的是一张查表
            //   q = { c0[fq[0]], c1[fq[1]], c1[fq[2]], c0[fq[3]] }
            // 于是 q[1] 与 q[2] 都指向 c1[*] 的同一个角，两个三角形全退化：
            // 144000 个顶点、桶也对，却一个像素都画不出来（白模实测才定位到）。
            // 几何台当时只验了顶点（数量/法线/半径），没验三角形连通性，所以漏了。
            const float fs[2] = { 1.0f, -1.0f };
            for (int side = 0; side < 2; ++side) {
                const int ia = (side == 0) ? 0 : 2;   // +S 侧
                const int ib = (side == 0) ? 1 : 3;   // -S 侧
                const float* q[4] = { c0[ia], c0[ib], c1[ib], c1[ia] };
                const float qu[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
                const float qv[4] = { v0, v0, v1, v1 };
                const int tri[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };
                for (int t = 0; t < 2; ++t) {
                    for (int qq = 0; qq < 3; ++qq) {
                        const int vi = tri[t][qq];
                        out.push_back(q[vi][0]);
                        out.push_back(q[vi][1]);
                        out.push_back(q[vi][2]);
                        out.push_back(qu[vi]);
                        out.push_back(qv[vi]);
                        out.push_back(fn[0] * fs[side]);
                        out.push_back(fn[1] * fs[side]);
                        out.push_back(fn[2] * fs[side]);
                        out.push_back(s.phase);
                        out.push_back(s.len);   // 本根原本的长度：着色器按频谱缩放【针的长短】要用它
                        // 【中轴（脊线）在这一端的位置】—— 顶点着色器靠 "顶点 − 中轴" 拿到横截面向量，
                        // 于是"绕中轴缩放（粗细）"、"按参数重排中轴（收拢 / 缠绕 / 弯曲）"都成了几行乘法。
                        // 这是"几何由参数决定"的把手：中轴是可控的，横截面只是挂在它上面。
                        // vi = 0/1 属于 j 号截面，2/3 属于 j+1 号（见上面 q[] 的取角方式）。
                        {
                            const int jj = (vi < 2) ? j : (j + 1);
                            out.push_back(cx[jj]); out.push_back(cy[jj]); out.push_back(cz[jj]);
                        }
                    }
                }
            }
        }
    }
    // 收尾：理论上不会有空桶（排序保证每个桶都被触及），但这里仍补齐，
    // 保证 bucketVertStart 单调不减 —— 渲染侧直接就着它逐桶提交。
    while (bucketNow < VT_RIB_BUCKETS) {
        bucketVertStart[++bucketNow] = (int)(out.size() / VTRibbonStride());
    }
}
