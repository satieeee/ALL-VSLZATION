/**
 * @file VTKnotGeom.h
 * @brief Filigree 中轴纽结（环面纽结）的管状几何 —— 纯数学，不碰 GL。
 *
 * 隧道中轴上、正对消失点的一个 (p,q) 环面纽结，外面套一层管。
 * 配合 kVTFS 里 uMat==3 那支的极端炫彩辉光 —— 它是个【光源】，不是个物体：
 * 颜色会被推到 1 以上（场景 FBO 是 RGBA16F），靠泛光那几趟炸成一大片光晕。
 *
 * ---------------------------------------------------------------------------
 * 管心线：标准的 (p,q) 环面纽结
 * ---------------------------------------------------------------------------
 *      x = (R + r·cos(qθ))·cos(pθ)
 *      y = (R + r·cos(qθ))·sin(pθ)
 *      z =  r·sin(qθ)                      θ ∈ [0, 2π)
 *
 * p=2, q=3 就是三叶结：绕环面轴 2 圈、在管上缠 3 次 —— z 有 3 个波峰，
 * 一眼能认出是"打了个结"。
 *
 * ---------------------------------------------------------------------------
 * 管：沿管心线扫一个圆
 * ---------------------------------------------------------------------------
 * 截面用「切线 × 固定参考轴」构基，参考轴取 (0,0,1)：
 * 这条曲线的切线 z 分量上限约 q·r / (p·R) = 3·0.26 / (2·0.85) ≈ 0.46，
 * 永远不可能与 +z 平行 —— 所以参考轴不会退化，也就不会出现框架翻转的接缝。
 * （一般螺旋线用 Frenet 框架会在拐点翻面，这里用固定参考轴正好绕开。）
 *
 * 法线是解析的：就是截面圆上从管心指向外的那一维，直接烘进顶点，
 * 不用 dFdx/dFdy —— 见 memory/normal-from-derivatives-unreliable。
 */
#pragma once
#include <vector>
#include <cmath>

// ---------------------------------------------------------------------------
// 参数
// ---------------------------------------------------------------------------
const int   KT_SEGS = 220;      // 沿管心线采几段
const int   KT_SIDE = 10;       // 管的截面用几边形
const float KT_R    = 0.85f;    // 环面大半径（结的整体尺寸）
const float KT_r    = 0.26f;    // 环面小半径（管心线绕环面轴的摆动幅度）
const float KT_TUBE = 0.075f;   // 管本身的粗细
const int   KT_P    = 2;
const int   KT_Q    = 3;
// 【位置烘进几何】放在中轴上、离相机多远。9 的话 NDC 半径 ≈ (R+r)·focal/z ≈ 0.28，
// 大约占画面高度的六成 —— 是个压得住场子的主体，又不至于糊满整屏。
// 烘进几何而不是用 uLayerZ：那个 uniform 是壁面分层的，共用会互相打架。
const float KT_Z    = 9.0f;

/// 每顶点 8 float：pos3 + uv2 + nrm3（与隧道壁同布局，loc3/4/5 不用）
inline int VTKnotStride() { return 8; }
/// 顶点数：每段每边一个四边形 = 2 三角形 × 3 顶点 = 6（不共享，与全工程一致）
inline int VTKnotVertexCount() { return KT_SEGS * KT_SIDE * 6; }

/// 管心线上 θ 处的点
inline void VTKnotCurve(float th, float& x, float& y, float& z) {
    const float cq = std::cos((float)KT_Q * th), sq = std::sin((float)KT_Q * th);
    const float cp = std::cos((float)KT_P * th), sp = std::sin((float)KT_P * th);
    const float rad = KT_R + KT_r * cq;
    x = rad * cp;
    y = rad * sp;
    z = KT_r * sq;
}

/// 生成整个管。out 追加写（调用方自己 clear）。返回写入的顶点数。
inline int VTKnotBuild(std::vector<float>& out) {
    out.clear();
    const float TAU = 6.2831853f;
    for (int i = 0; i < KT_SEGS; ++i) {
        const float th0 = TAU * (float)i / (float)KT_SEGS;
        const float th1 = TAU * (float)(i + 1) / (float)KT_SEGS;
        const float h   = TAU / (float)KT_SEGS * 0.5f;

        // 四个端点：段 i 的两端 × 截面圆两角 j、j+1
        float px[2], py[2], pz[2];          // 管心
        float nx[2], ny[2], nz[2];          // 截面基 N
        float bx[2], by[2], bz[2];          // 截面基 B
        const float ths[2] = { th0, th1 };
        for (int e = 0; e < 2; ++e) {
            const float th = ths[e];
            VTKnotCurve(th, px[e], py[e], pz[e]);
            float ax, ay, az, cx, cy, cz;
            VTKnotCurve(th - h, ax, ay, az);
            VTKnotCurve(th + h, cx, cy, cz);
            float tx = cx - ax, ty = cy - ay, tz = cz - az;
            const float tl = std::sqrt(tx*tx + ty*ty + tz*tz);
            if (tl > 1e-9f) { tx /= tl; ty /= tl; tz /= tl; } else { tx = 0; ty = 0; tz = 1; }
            // N = ref × T，ref = (0,0,1)
            float ux = -ty, uy = tx, uz = 0.0f;
            const float ul = std::sqrt(ux*ux + uy*uy + uz*uz);
            if (ul > 1e-9f) { ux /= ul; uy /= ul; uz /= ul; } else { ux = 1; uy = 0; uz = 0; }
            // B = T × N
            nx[e] = ux; ny[e] = uy; nz[e] = uz;
            bx[e] = ty * uz - tz * uy;
            by[e] = tz * ux - tx * uz;
            bz[e] = tx * uy - ty * ux;
        }

        for (int j = 0; j < KT_SIDE; ++j) {
            const int j2 = (j + 1) % KT_SIDE;
            const float f0 = TAU * (float)j    / (float)KT_SIDE;
            const float f1 = TAU * (float)j2   / (float)KT_SIDE;
            // 截面圆上的四点（顺时针，与壁面同样的三角形绕向）
            const float cf[2] = { std::cos(f0), std::cos(f1) };
            const float sf[2] = { std::sin(f0), std::sin(f1) };
            // 四角：(e=0,j) (e=1,j) (e=1,j2) (e=0,j2)
            const int   ei[4] = { 0, 1, 1, 0 };
            const int   fi[4] = { 0, 0, 1, 1 };
            float qx[4], qy[4], qz[4], qnx[4], qny[4], qnz[4], qu[4], qv[4];
            for (int k = 0; k < 4; ++k) {
                const int e = ei[k], f = fi[k];
                const float cc = cf[f], ss = sf[f];
                // 截面圆上的点
                qx[k] = px[e] + KT_TUBE * (nx[e] * cc + bx[e] * ss);
                qy[k] = py[e] + KT_TUBE * (ny[e] * cc + by[e] * ss);
                qz[k] = pz[e] + KT_TUBE * (nz[e] * cc + bz[e] * ss);
                // 解析法线 = 截面上从管心指向外的方向（单位向量，因为 cc²+ss²=1）
                qnx[k] = nx[e] * cc + bx[e] * ss;
                qny[k] = ny[e] * cc + by[e] * ss;
                qnz[k] = nz[e] * cc + bz[e] * ss;
                qu[k]  = (float)f / 2.0f;                       // 绕管一圈 0..0.5
                qv[k]  = ((float)i + (float)e) / (float)KT_SEGS; // 沿管长 0..1
            }
            // 两个三角形：(0,1,2) 与 (0,2,3)
            const int tri[6] = { 0, 1, 2, 0, 2, 3 };
            for (int k = 0; k < 6; ++k) {
                const int a = tri[k];
                out.push_back(qx[a]  + 0.0f);        // 位置（KT_Z 加在下面统一做）
                out.push_back(qy[a]);
                out.push_back(qz[a]  + 0.0f);
                out.push_back(qu[a]);
                out.push_back(qv[a]);
                out.push_back(qnx[a]);
                out.push_back(qny[a]);
                out.push_back(qnz[a]);
            }
        }
    }
    // 【几何以原点为中心】位置由渲染侧用 uLayerZ = KT_Z 摆。
    // 不烘进顶点是为了自转：绕原点转 = 绕结自己的中心转；烘了 KT_Z 再绕原点转，
    // 整个结会被甩离中轴。
    return (int)(out.size() / (size_t)VTKnotStride());
}
