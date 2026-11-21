/**
 * @file TNChipGeom.h
 * @brief Filigree 剪纸片 —— 池子筛选 / 生成 / 漂移 / 逐帧顶点。纯数学，不碰 GL。
 *
 * 单独抽出来的理由与 C3OrigamiGeom.h / C3Veil.h 抽出来时一模一样：
 * 这是"实机与离线台必须给出同一结果"的部分。Filigree 第一版把这段写在 main.cpp 里，
 * 结果是【唯一没验证过的路径】—— 剪纸只在鼓点上生成，实机一放音频才走到，
 * 而离线台够不着它（读不到 main.cpp 的静态全局量）。
 *
 * ---------------------------------------------------------------------------
 * 【2026-09-23 改版】纸片从"贴在内壁上"改成"在三维空间里自由漂浮"
 * ---------------------------------------------------------------------------
 * 旧版用 VTSurfacePoint 把扁平的折纸几何映射到隧道柱面上，纸片永远贴着壁、沿着
 * (u,v) 漂移。问题是它们看起来像"壁纸上印的花"，而不是"在隧道里飞的纸"。
 *
 * 新版：每片有自己的
 *   · 世界坐标位置（半径带 × 角度 × z 带，三段都是随机）
 *   · 一组正交基 (bp, bq, nrm) —— 均匀随机朝向，纸面可以朝任何方向
 *   · 三维漂移速度
 * 扁平几何直接在纸片自己的平面里展开，不再经过柱面映射。
 *
 * 生命周期没变，也正好是需求：**hit 生成一批，下一个 hit 全部退场**。
 *
 * ---------------------------------------------------------------------------
 * 数据流（每帧）
 * ---------------------------------------------------------------------------
 *   鼓点 → TNSpawnChips   ：旧的标记 killT，新的补一批进来（C3 的批次位移模式）
 *        → TNUpdateChips  ：三维平移 + z 带回绕 + 回收淡完的
 *        → TNBuildChipGeometry ：把每片折纸的 4 个面扇形展开成三角形
 *
 * ---------------------------------------------------------------------------
 * 两个必须记住的坑
 * ---------------------------------------------------------------------------
 * 1) 【内缩必须在 canvas px 里做】—— C3InsetConvex 里有个写死的面积下限 ar < 100.0f，
 *    喂归一化坐标（面积 ~1e-3）会把所有格子都判死、一张纸都生不出来。
 *    所以走 C3MakeOrigamiShapeFromCell（它负责 ×W/×H），拿到结果后再缩到世界单位。
 *
 * 2) 【alpha 上限必须 < 1.0】—— 打包进 loc5 的值一旦等于 1.0 会被后续解成别的含义
 *    （C3 用的是 0.98，同源）。
 */
#pragma once
#include "../C3/C3OrigamiGeom.h"   // Origami / C3MakeOrigamiShapeFromCell / C3InsetConvex / C3Rnd
#include "VTGeom.h"                // 只用来核对"纸片半径带上限别穿出内壁"（见 TN_CHIP_RMAX 注释）
#include <vector>
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------------------------
// 一片剪纸
// ---------------------------------------------------------------------------
struct TNChip {
    Origami shape;              // C3 的折纸片几何（内缩/折痕/4 个受力面），canvas px
    float px, py, pz;           // 世界坐标位置（隧道原点在相机处）
    float bp[3], bq[3];         // 纸面内的两个正交单位轴
    float nrm[3];               // 纸面法线（单位）= bp × bq
    float dvx, dvy, dvz;        // 三维漂移速度（世界单位/秒）
    float s;                    // canvas px → 世界单位的换算比
    float rad;                  // 纸片半径（世界单位）—— 折面起伏按它取比例
    float spawnT, killT;        // killT == 0 表示还活着
};

// ---------------------------------------------------------------------------
// 参数
// ---------------------------------------------------------------------------
// 【每拍几片】不再跟 C3 的比例（kC3PolyN/2 = 240）—— 那个数是给"贴壁的小片"定的。
// 改成自由三维之后纸片离镜头可以很近、在屏幕上摊得很开，240 片会把隧道整个盖住。
const int   TN_CHIP_PER_HIT   = 110;
const int   TN_CHIP_MAX       = 560;

// 【尺寸是世界单位】旧的 TN_CHIP_SCALE_* 是"占断面周长的比例"，绕着柱面算的。
// 隧道半径 VT_R ≈ 1.60 作参照：0.045~0.15 大约是隧道的 3%~9%，小而密才像剪纸。
const float TN_CHIP_SIZE_LO   = 0.045f;
const float TN_CHIP_SIZE_HI   = 0.15f;

// 出现位置：半径带 / z 带。
// 【上限别穿墙】内壁最小半径 = VT_R*(1 - VT_A) = 1.60*0.88 = 1.408。
// 纸片最外缘 = TN_CHIP_RMAX + TN_CHIP_SIZE_HI，必须小于它 —— 1.00 + 0.30 = 1.30 ✓
const float TN_CHIP_RMIN      = 0.12f;
const float TN_CHIP_RMAX      = 1.00f;
// z 带。范围比壁面的可见区【更靠近镜头】—— 纸片不吃近端淡出（见 kTVS 里 vFog 那行），
// 所以可以放到 z=1.0 也不会被压黑；放近了它们才会出现在画面外圈、而且大。
// 远端仍受 fadeFar 约束，18 之外基本没影了。
// 【下限别低于 ~0.6】投影里 d = max(p.z, 0.05)，太近会被放大到失控。
const float TN_CHIP_ZMIN      = 1.6f;
const float TN_CHIP_ZMAX      = 18.0f;

const float TN_CHIP_DRIFT_LO  = 0.03f;          // 漂移速度（世界单位/秒）
const float TN_CHIP_DRIFT_HI  = 0.22f;
const float TN_CHIP_LIFT_FRAC = 0.18f;          // 折面起伏 = 该比例 × 该片的半径（山/谷交替）
const float TN_CHIP_REVEAL    = 0.30f;          // 展开时长
const float TN_CHIP_KILL      = 0.55f;          // 被下一批顶掉时的淡出时长

const int   TN_CHIP_STRIDE    = 10;             // 每顶点 10 float：pos3 + uv2 + nrm3 + shade1 + alpha1

// ---------------------------------------------------------------------------
// 小工具：三维点积 / 叉乘 / 归一化（文件内用，不引 glm）
// ---------------------------------------------------------------------------
inline void TNCross(const float* a, const float* b, float* o) {
    o[0] = a[1]*b[2] - a[2]*b[1];
    o[1] = a[2]*b[0] - a[0]*b[2];
    o[2] = a[0]*b[1] - a[1]*b[0];
}
inline void TNNormalize3(float* v) {
    const float L = std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    if (L > 1e-8f) { v[0] /= L; v[1] /= L; v[2] /= L; }
    else           { v[0] = 0.0f; v[1] = 0.0f; v[2] = 1.0f; }
}

/// 可用格子池（内缩失败的一律剔掉）。一次性算好，调用方自行缓存。
inline void TNChipPoolBuild(std::vector<int>& pool) {
    pool.clear();
    const float W = C3CanvasW(), H = kC3CanvasH;
    float vx[8], vy[8], ox[8], oy[8];
    for (int p = 0; p < kC3PolyN; ++p) {
        const int i0 = kC3PolyOff[p];
        const int n  = kC3PolyOff[p + 1] - i0;
        if (n < 3 || n > 8) continue;
        for (int k = 0; k < n; ++k) {
            vx[k] = kC3PolyV[i0 + k][0] * W;
            vy[k] = kC3PolyV[i0 + k][1] * H;
        }
        if (C3InsetConvex(vx, vy, n, C3_ORIGAMI_INSET, ox, oy) < 3) continue;
        pool.push_back(p);
    }
}

/// 造一片：折纸几何 + 随机的三维位置、朝向、漂移。失败返回 false。
inline bool TNMakeChip(int polyIdx, float now, TNChip& c) {
    if (!C3MakeOrigamiShapeFromCell(polyIdx, C3_ORIGAMI_INSET, c.shape)) return false;

    // 特征尺寸 = 顶点到质心的最大距离（canvas px），用它把 px 换算成世界单位
    float R = 1e-3f;
    for (int k = 0; k < c.shape.n; ++k) {
        const float dx = c.shape.vx[k] - c.shape.cx, dy = c.shape.vy[k] - c.shape.cy;
        const float d = std::sqrt(dx*dx + dy*dy);
        if (d > R) R = d;
    }
    const float size = TN_CHIP_SIZE_LO + (TN_CHIP_SIZE_HI - TN_CHIP_SIZE_LO) * C3Rnd(polyIdx, 77);
    c.s   = size / R;                     // canvas px → 世界单位
    c.rad = size;                         // 纸片半径（世界单位）

    // ---- 位置：半径带 × 角度 × z 带，三段独立随机 ----
    // 【两处都不能用线性均匀】
    //  · z 用【对数均匀】：线性均匀的话远的段数占绝大多数，投影到屏幕上全挤在
    //    消失点周围一小块（近处的屏幕面积大得多），看着像"一坨"而不是"撒开"。
    //    对数均匀 = 每个距离倍程差不多一样多，屏幕上才铺得开。
    //  · 半径用【面积均匀】(sqrt)：圆环面积 ∝ r，不 sqrt 的话靠轴的纸片过多，
    //    而靠轴 = 投影在画面中心，又是往中间挤。
    {
        const float ru = C3Rnd(polyIdx, 78);
        const float r  = std::sqrt(TN_CHIP_RMIN*TN_CHIP_RMIN +
                                   (TN_CHIP_RMAX*TN_CHIP_RMAX - TN_CHIP_RMIN*TN_CHIP_RMIN) * ru);
        const float th = 6.2831853f * C3Rnd(polyIdx, 94);
        c.px = r * std::cos(th);
        c.py = r * std::sin(th);
        const float lz = std::log(TN_CHIP_ZMIN) +
                         (std::log(TN_CHIP_ZMAX) - std::log(TN_CHIP_ZMIN)) * C3Rnd(polyIdx, 95);
        c.pz = std::exp(lz);
    }

    // ---- 朝向：球面均匀随机
    // 用 z 分量均匀取样再配随机方位角，这样在球面上是【面积均匀】的；
    // 直接对两个角都均匀取样会在两极堆积。
    {
        const float az = 6.2831853f * C3Rnd(polyIdx, 91);
        const float cz = 2.0f * C3Rnd(polyIdx, 92) - 1.0f;
        const float sz = std::sqrt(std::max(0.0f, 1.0f - cz * cz));
        c.nrm[0] = sz * std::cos(az);
        c.nrm[1] = cz;
        c.nrm[2] = sz * std::sin(az);
        // 取一个与法线不平行的参考轴，叉乘出纸面内的第一根轴。
        // |nrm.z| 大的时候法线几乎与 z 平行，这时改用 x 轴当参考 —— 两者不会同时失效。
        const float rf[3] = { (std::fabs(c.nrm[2]) < 0.9f) ? 0.0f : 1.0f,
                              0.0f,
                              (std::fabs(c.nrm[2]) < 0.9f) ? 1.0f : 0.0f };
        TNCross(rf, c.nrm, c.bp);
        TNNormalize3(c.bp);
        TNCross(c.nrm, c.bp, c.bq);       // nrm、bp、bq 构成右手正交基
        // 绕法线再自转一个随机角 —— 否则"纸面朝向"随机了但"纸面内的方向"是确定的
        const float rot = 6.2831853f * C3Rnd(polyIdx, 93);
        const float cr = std::cos(rot), sr = std::sin(rot);
        float np[3], nq[3];
        for (int k = 0; k < 3; ++k) {
            np[k] = c.bp[k] * cr + c.bq[k] * sr;
            nq[k] = -c.bp[k] * sr + c.bq[k] * cr;
        }
        for (int k = 0; k < 3; ++k) { c.bp[k] = np[k]; c.bq[k] = nq[k]; }
    }

    // ---- 漂移：方向同样球面均匀，速度随机 ----
    {
        const float az = 6.2831853f * C3Rnd(polyIdx, 80);
        const float cz = 2.0f * C3Rnd(polyIdx, 96) - 1.0f;
        const float sz = std::sqrt(std::max(0.0f, 1.0f - cz * cz));
        const float spd = TN_CHIP_DRIFT_LO + (TN_CHIP_DRIFT_HI - TN_CHIP_DRIFT_LO) * C3Rnd(polyIdx, 81);
        c.dvx = sz * std::cos(az) * spd;
        c.dvy = cz * spd;
        c.dvz = sz * std::sin(az) * spd;
    }

    c.spawnT = now;
    c.killT  = 0.0f;
    return true;
}

/// 鼓点：把还活着的标记为淡出，再补一批新的进来（C3 的批次位移模式）
inline void TNSpawnChips(std::vector<TNChip>& chips, const std::vector<int>& pool,
                         float now, unsigned seed) {
    if (pool.empty()) return;
    for (auto& c : chips) if (c.killT <= 0.0f) c.killT = now;
    const int NP = (int)pool.size();
    int want = TN_CHIP_PER_HIT; if (want > NP) want = NP;
    // Fisher-Yates：用 C3Rnd 保证确定性（离线台与实机同结果）
    std::vector<int> order(NP);
    for (int i = 0; i < NP; ++i) order[i] = i;
    for (int i = NP - 1; i > 0; --i) {
        const int j = (int)(C3Rnd((int)(seed + (unsigned)i * 2654435761u), 9000 + (i & 1023)) * (float)(i + 1));
        if (j <= i) std::swap(order[i], order[j < 0 ? 0 : j]);
    }
    for (int k = 0; k < want; ++k) {
        if ((int)chips.size() >= TN_CHIP_MAX) break;
        TNChip c;
        if (!TNMakeChip(pool[order[k]], now, c)) continue;
        chips.push_back(c);
    }
}

/// 三维平移 + 回收。
/// 【为什么 z 要回绕】纸片理论上只活一个鼓点间隔，但慢曲子里间隔可能好几秒，
/// 加上 speed 缩放后的 dt 可能很大 —— 不兜底的话会有纸片一路飘进黑洞。
/// 半径方向不回绕：飘不出内壁（速度本来就小），回绕反而会在画面上"瞬移"。
inline void TNUpdateChips(std::vector<TNChip>& chips, float dt, float now) {
    const float zspan = TN_CHIP_ZMAX - TN_CHIP_ZMIN;
    for (auto& c : chips) {
        c.px += c.dvx * dt;
        c.py += c.dvy * dt;
        c.pz += c.dvz * dt;
        if (zspan > 1e-3f) {
            while (c.pz < TN_CHIP_ZMIN) c.pz += zspan;
            while (c.pz > TN_CHIP_ZMAX) c.pz -= zspan;
        }
    }
    // 清理已经完全淡掉的
    chips.erase(std::remove_if(chips.begin(), chips.end(),
        [&](const TNChip& c) { return c.killT > 0.0f && (now - c.killT) > TN_CHIP_KILL; }),
        chips.end());
}

/// 重建剪纸片的顶点缓冲（CPU 每帧一次）。返回写入的顶点数。
/// 扁平几何直接在纸片自己的平面里展开：world = 位置 + bp·局部x + bq·局部y + nrm·起伏
inline int TNBuildChipGeometry(const std::vector<TNChip>& chips, float now, std::vector<float>& out) {
    out.clear();
    for (const auto& c : chips) {
        const float age = now - c.spawnT;
        float p = age / TN_CHIP_REVEAL;
        if (p < 0.0f) p = 0.0f; if (p > 1.0f) p = 1.0f;
        const float e = 1.0f - (1.0f - p) * (1.0f - p) * (1.0f - p);   // 从折痕交点展开
        float aIn = (p < 0.001f) ? 0.0f : (p < 0.30f ? p / 0.30f : 1.0f);
        float aOut = 1.0f;
        if (c.killT > 0.0f) {
            aOut = 1.0f - (now - c.killT) / TN_CHIP_KILL;
            if (aOut < 0.0f) aOut = 0.0f;
        }
        // 【上限必须 < 1.0】—— 1.0 会被后续的 kind/alpha 打包解成别的含义（C3 的 0.98 同源）
        const float alpha = std::min(aIn * aOut, 0.98f);
        if (alpha <= 0.002f) continue;

        for (int s = 0; s < 4; ++s) {
            const int pn = c.shape.pn[s];
            if (pn < 2) continue;
            // 折面起伏沿【纸面法线】，比例于该片半径 —— 大纸片折痕才看得出来
            const float lift = ((s & 1) ? 1.0f : -1.0f) * TN_CHIP_LIFT_FRAC * c.rad;
            for (int vq = 0; vq + 1 < pn; ++vq) {
                for (int t = 0; t < 3; ++t) {
                    float fx, fy;
                    if (t == 0)      { fx = c.shape.cx;      fy = c.shape.cy; }
                    else if (t == 1) { fx = c.shape.px[s][vq]; fy = c.shape.py[s][vq]; }
                    else             { fx = c.shape.px[s][vq+1]; fy = c.shape.py[s][vq+1]; }
                    // 展开动画：绕质心缩放
                    fx = c.shape.cx + (fx - c.shape.cx) * e;
                    fy = c.shape.cy + (fy - c.shape.cy) * e;
                    // canvas px → 世界单位（相对质心）
                    const float lx = (fx - c.shape.cx) * c.s;
                    const float ly = (fy - c.shape.cy) * c.s;
                    // 放到纸片自己的平面上
                    const float x = c.px + c.bp[0]*lx + c.bq[0]*ly + c.nrm[0]*lift;
                    const float y = c.py + c.bp[1]*lx + c.bq[1]*ly + c.nrm[1]*lift;
                    const float z = c.pz + c.bp[2]*lx + c.bq[2]*ly + c.nrm[2]*lift;
                    // uv 用【归一化后的平面坐标】，给片元做纸色渐变
                    const float cu = lx / TN_CHIP_SIZE_HI + 0.5f;
                    const float cv = ly / TN_CHIP_SIZE_HI + 0.5f;
                    out.push_back(x);  out.push_back(y);  out.push_back(z);
                    out.push_back(cu); out.push_back(cv);
                    out.push_back(c.nrm[0]); out.push_back(c.nrm[1]); out.push_back(c.nrm[2]);
                    out.push_back(c.shape.shade[s]);   // loc4：面明暗（硬棱的来源）
                    out.push_back(alpha);              // loc5：透明度
                }
            }
        }
    }
    return (int)(out.size() / (size_t)TN_CHIP_STRIDE);
}
