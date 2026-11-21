// C3Veil.h —— Cubeeffect3 的鼓点锚点：【GPU 与 CPU 两个后端共用这一份】
//
// 抽出来的原因和 C3OrigamiGeom.h 一样：这是"两个后端必须给出同一结果"的逻辑
// （锚点撒在哪、朝哪个方向推、什么时候出生），各写一份迟早会漂移。
// 这里全部是纯函数，不碰 GL，也不依赖 main.cpp 的全局状态。
#pragma once
#include <vector>
#include <cmath>
#include <cstring>

/// 每层在画布上取样窗口的偏移。层与层【朝向必须一致】（用户明确要求：双轴相对静止、
/// 只有扭曲网在动），所以不能靠旋转/倾斜区分，改用平移 —— 4 层看到迷宫的不同区域。
/// 两条硬约束：
///   · |偏移| ≤ 1   → 窗口 [0,1]+ofs 仍落在画布 pos 范围 [-1,2] 内，不会露出没格子的空白；
///   · |偏移| > 0.6 → 画布中心线处生成时留了 ~4px 断口用来避开双轴，它映射到 world = ofs×2，
///                    偏移不够大就会把这个断口露在屏幕里。
/// 第 0 层偏移取 0 —— 只有这样它才和那双固定坐标轴对齐。必须与 main.cpp 的 kLayerOfs 一致。
const float C3_LAYER_OFS[4][2] = {
    {  0.00f,  0.00f },
    {  0.86f,  0.88f },
    { -0.83f,  0.90f },
    { -0.91f, -0.85f },
};

// ---------------------------------------------------------------------------
// 图层前进速度（= 镜头推进速度）—— 【GPU 与 CPU 共用这一份】
//
// 由【高频能量】驱动（铉片 / hi-hat，1.5k~20kHz），下限 1×、上限 20×。
//
// · 为什么用高频而不是整体 energy：一是"明亮度"直接对应推进的冲劲；
//   二是频段数据在 AudioAnalyzer 里已按 3 秒衰减的参考峰值归一化，
//   【不受系统音量影响】—— 而 energy 是绝对 RMS，把音量调小速度就跟着慢下来。
// · 用的是 trebRelAtt（音高频【自身】峰值归一化那一路），不是 trebAtt。
//   原来那路拿【全频谱最响的段】当分母：鼓重的曲子分母被低频顶高，高频整体被压低，
//   同一个镲片在低频猛的曲子里读数能低好几倍。换成自身基准后与低频强弱脱钩。
// · 映射是【凸】的（e 的 x 次方那种感觉），不是线性：
//   常见的中低值都压在低速区，只有很少出现的高值才推到高倍速 ——
//   即"较难触发的高数据段"才对应高倍速，日常听歌镜头是慢的、偶尔窜一下。
//   曲线 = (e^(k·x) − 1)/(e^k − 1)，0→0、1→1，中间凸起。
//   k 越大越凸（越难触发高倍速）。
// · 满量程【自适应】而不是写死：_hittest/audio_probe.exe 实测两首 30 秒素材，
//   trebRelAtt 的峰值一次 0.602、一次 0.333 —— 差近一倍。写死满量程的话，
//   换首安静点的曲子就永远够不到高倍速。所以跟踪"最近见过的峰值"当满量程。
//   效果：x = 当前值 / 近期峰值，于是最高倍速只有【逼近峰值的那一刻】才触发，
//   日常值都落在曲线的低端 —— 正是"难触发的高数据段对应高倍速"。
// · C3_TREB_CURVE 是曲线陡度，越大越凸、高倍速越难触发。
// · C3_LAYER_BASE_SPD 是 1× 的基准速度（世界单位/秒）。
//
// 抽到这个共享头的理由：这段公式曾经在 main.cpp 和 C3Cpu.cpp 里各写了一遍，
// 改一处漏一处就会让两个后端的观感不一致。现在两边都只调这一个函数。
// ---------------------------------------------------------------------------
const float C3_TREB_CURVE      = 4.0f;   // 曲线陡度：越大越凸、高倍速越难触发
const float C3_TREB_REF_FLOOR  = 0.30f;  // 满量程下限（太安静时不至于把底噪当满量程放大）
const float C3_TREB_REF_TAU    = 15.0f;  // 峰值记忆的回落时间常数（秒）
const float C3_LAYER_BASE_SPD  = 0.55f;  // 1× 的基准推进速度（世界单位/秒）
const float C3_LAYER_SPEED_MAX = 20.0f;  // 上限倍速（下限恒为 1×）

/// 满量程自适应跟踪器 —— 实例在 main.cpp 的 g_c3TrebRef（CPU 后端已移除）。
/// 立刻跟上新峰值、然后以 C3_TREB_REF_TAU 缓慢回落到下限。
struct C3TrebRefTracker {
    float ref = C3_TREB_REF_FLOOR;
    void Update(float v, float dt) {
        if (v > ref) ref = v;
        else ref += (C3_TREB_REF_FLOOR - ref) * (1.0f - std::exp(-dt / C3_TREB_REF_TAU));
        if (ref < C3_TREB_REF_FLOOR) ref = C3_TREB_REF_FLOOR;
    }
};

/// 本帧应推进的世界单位数（乘 dt，加到 layerPhase 上）
inline float C3LayerAdvance(float trebRelAtt, float ref, float dt) {
    float x = trebRelAtt / ref;
    x = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
    // 归一化指数曲线：(e^(k·x) − 1)/(e^k − 1)。0→0、1→1，中间【凸】。
    const float e = (std::exp(C3_TREB_CURVE * x) - 1.0f)
                  / (std::exp(C3_TREB_CURVE) - 1.0f);
    return C3_LAYER_BASE_SPD * (1.0f + (C3_LAYER_SPEED_MAX - 1.0f) * e) * dt;
}

/// 一个锚点 = 蛛网上的一个扰动源：世界坐标 + 新朝向 + 出生时间
struct SpiderVeil {
    float x, y, z;          // 锚点世界坐标
    float dx, dy, dz;       // 新朝向单位向量（剪切方向）
    float spawnT;           // 出生时间
};

/// 单次 hit 的锚点数上限。锚点按【世界坐标的固定间距】铺满当前可见区域，
/// 窗口宽高比越极端需要的锚点越多：16:9 需 4×3=12，21:9 需 6×3=18，32:9 需 8×3=24。
const int C3_ANCH_PER_HIT = 24;

/// 同时存活的锚点上限 + 锚点寿命（秒）。必须与 main.cpp 的 CUBE_VEIL_MAX / LIFE 一致：
/// 上限太小会让上一批提前被顶掉，太大则旧的扭曲残留过久。
const int   C3_VEIL_MAX  = 32;
const float C3_VEIL_LIFE = 8.0f;

/// 可复现随机（与 main.cpp 的 cubeRnd 同一个 hash）—— 两边用同一个种子就能得到同一批锚点。
inline float C3Rnd(int a, int b) {
    unsigned v = (unsigned)(a * 73856093u ^ b * 19349663u);
    v = (v ^ (v >> 16)) * 0x7feb352du;
    v = (v ^ (v >> 13)) * 0x846ca68bu;
    v ^= (v >> 16);
    return (v & 0xffffff) / 16777216.0f;
}

/// 在整个【可见屏幕】范围内撒一组锚点（抖动网格 + 各自随机方向）。
/// halfX/halfY = 可见世界半宽/半高，由调用方按当前窗口宽高比算（contain-fit 投影，见两端各自的 Render）。
/// 锚点间距固定为世界单位 → 无论窗口多大鼓包重叠程度一致（R=0.24，间距 0.5 → 相邻鼓包重叠）。
inline void C3SpawnVeils(std::vector<SpiderVeil>& out, float halfX, float halfY,
                         float now, unsigned seed, int maxCount) {
    const float STEP = 0.5f;
    int GX = (int)ceilf(2.0f * halfX / STEP); if (GX < 2) GX = 2;
    int GY = (int)ceilf(2.0f * halfY / STEP); if (GY < 2) GY = 2;
    while (GX * GY > C3_ANCH_PER_HIT) { if (GX >= GY) --GX; else --GY; }   // 极端宽高比封顶

    for (int gy = 0; gy < GY; ++gy) {
        for (int gx = 0; gx < GX; ++gx) {
            SpiderVeil v; memset(&v, 0, sizeof v);
            int sd = (gy * GX + gx) * 23;
            // 网格单元中心 + 抖动（幅度 = 半个格）→ 分布均匀但不呆板
            float u = (gx + 0.5f) / GX + (C3Rnd((int)seed, 1 + sd) - 0.5f) / GX;
            float t = (gy + 0.5f) / GY + (C3Rnd((int)seed, 2 + sd) - 0.5f) / GY;
            v.x = (u - 0.5f) * 2.0f * halfX;
            v.y = (t - 0.5f) * 2.0f * halfY;
            v.z = 0.0f;
            float ax = C3Rnd((int)seed, 4 + sd) * 6.28318f;
            float ay = C3Rnd((int)seed, 5 + sd) * 6.28318f;
            v.dx = cosf(ax) * 0.8f; v.dy = sinf(ax) * 0.8f; v.dz = sinf(ay) * 0.3f;
            float il = 1.0f / (sqrtf(v.dx * v.dx + v.dy * v.dy + v.dz * v.dz) + 1e-6f);
            v.dx *= il; v.dy *= il; v.dz *= il;
            v.spawnT = now;
            if ((int)out.size() < maxCount) out.push_back(v);
            else { out.erase(out.begin()); out.push_back(v); }   // 满则替换最旧
        }
    }
}

/// 高斯的"先颤后定"：只有这一项随时间变化（1.0 那一半是持久的新朝向，不衰减），
/// 所以"尾巴拖多长"完全由这个衰减率决定。必须和 kC3LineVS 里的 TREMBLE_DECAY 一致。
const float C3_TREMBLE_DECAY = 7.0f;

/// 算一个锚点在某点产生的位移（含颤抖）。返回是否命中（未命中时不动）。
inline bool C3VeilDisp(const SpiderVeil& v, float wx, float wy, float time,
                       float& dx, float& dy) {
    const float R2 = 0.24f * 0.24f;
    float px = wx - v.x, py = wy - v.y;
    float d2 = px * px + py * py;
    if (d2 > R2 * 9.0f) return false;                 // exp(-9)≈1e-4，再远可忽略
    float g = expf(-d2 / R2);
    float age = time - v.spawnT;
    if (age < 0.0f) age = 0.0f;                       // 夹住：负年龄会让 exp 爆掉
    float trem = expf(-age * C3_TREMBLE_DECAY) * sinf(age * 6.0f + sinf(age * 13.0f) * 2.0f);
    dx = v.dx * g * (1.0f + trem);
    dy = v.dy * g * (1.0f + trem);
    return true;
}
