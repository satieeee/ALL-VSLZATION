#version 330 core
// ============================================================================
// Quicksilver（水银）—— 对数极坐标 · 复合正弦 · 非线性涡旋 · SDF 花瓣 · 余弦调色
//
// 移植自 @samuelyan  #Shader_240422_4  的"五阶段构建法"。
//
// 【单形态】本效果固定跑完整五阶段管线，呈现 STAGE 05 的终态。
// 五阶段是一次【建构过程】（原作者的 tutorial 顺序），不是并列的五个观感 ——
// 所以 formCount = 1，不参与引擎的"每拍切形态"（见 main.cpp:3313）。
// 中间四态只能通过离线渲染台看（_hittest/qs_render.cpp，用 -DQS_STAGE=n）。
//
//   STAGE 01  对数螺线与空间极化   r = ln|u|,  θ = 2·atan2
//   STAGE 02  复合正弦的流体谐波   thc(a,b) = sin(a·cos b)/sin a
//   STAGE 03  非线性正弦旋转矩阵   u' = R(t + 2θ + 6.1415·sin(3r+θ-t))·u
//   STAGE 04  组合符号距离场花瓣   sdEquilateralTriangle + sdBox
//   STAGE 05  余弦调色与奇点流光   pal() + 三相相位色散 + 奇点辉光   ← 终态
//
// 【与原作者的差异 —— 本工程是音频可视化，不是纯时间动画】
//   · 涡旋角速度 / 谐波泵 / 调色板相位 改由音频驱动（uBassAtt / uTrebAtt / uBeatFlash）
//   · 末尾接 audioGrade + cpuFinalize + uOpacity，与其它 .frag 同协议
//   · 实现 uEncode / uOldEncoded / uMorphBlend，支持跨效果参数场级过渡
//
// 【对原代码的一处修正】原作者 STAGE 03 的
//       h = 1.0 + floor(0.25 * fract(0.1 * u_time));
//   恒等于 1.0 —— 因为 0.25*fract(x) ∈ [0, 0.25)，floor 永远是 0，
//   所谓"动态相角乘子"是死代码。这里按他文字里描述的意图
//   （"随周期阶梯跳变，调控旋臂分支对称阶数"）实现为 1..4 阶梯。
//
// 【关于 form 0..2 的着色】原图只给了 5 张结果，中间三张的着色公式无法反推。
//   这里的取舍是：让每个阶段暴露它自己那一层的特征场 ——
//     form 0/1 用对数半径 lr（→ 同心环 / 花瓣）
//     form 2   用旋转后的极角 ca（→ 涡旋剪切）
//     form 3/4 用 SDF 骨架强度 s（→ 雕花 / 虹彩）
//   同时色调从单色逐级过渡到全色散，复现原图 01→05 的视觉演进。
// ============================================================================

in vec2 vUV; out vec4 fragColor;
uniform float uTime, uBpm, uSpeed; uniform vec2 uResolution;
uniform int uEncode, uDarkBg, uColorScheme;
uniform float uOpacity;
uniform sampler2D uOldEncoded;
uniform float uMorphBlend;

// 管线深度。4 = 跑完 STAGE 01..05，这是本效果的【唯一形态】。
// 这五个阶段是一次"建构"（原作者的 tutorial 顺序），不是五个并列观感，
// 所以不设 formCount、不参与每拍切形态。
// 离线渲染台用 -DQS_STAGE=n 覆盖，出分阶段对照图（见 _hittest/qs_render.cpp）。
#ifndef QS_STAGE
#define QS_STAGE 4
#endif

const float PI  = 3.14159265359;
const float TAU = 6.28318530718;

// ---------------------------------------------------------------------------
// 原作者的两个核心算子
// ---------------------------------------------------------------------------

// 复合三角正弦：内层 cos 驱动外层相位 → "陡峭峰脊 + 平缓谷底"的非线性浪。
// 这是把普通正弦变成流体的关键；sin(a) 在 a∈{1,2,3,4} 上均远离零点，无除零风险。
float thc(float a, float b) { return sin(a * cos(b)) / sin(a); }

// 余弦调色板（Inigo Quilez）：一行产生连续无限的彩虹光谱，无需查表。
// a=偏置 b=幅度 c=频率 d=相位
vec3 pal(float t, vec3 a, vec3 b, vec3 c, vec3 d) { return a + b * sin(TAU * (c * t + d)); }

// 二维欧拉旋转矩阵
vec2 rot2(vec2 v, float ang) { float c = cos(ang), s = sin(ang); return mat2(c, -s, s, c) * v; }

// ---------------------------------------------------------------------------
// SDF（Inigo Quilez 标准实现）
// ---------------------------------------------------------------------------
float sdEquilateralTriangle(vec2 p, float r) {
    const float k = 1.7320508;                       // sqrt(3)
    p.x = abs(p.x) - r;
    p.y = p.y + r / k;
    if (p.x + k * p.y > 0.0) p = vec2(p.x - k * p.y, -k * p.x - p.y) * 0.5;
    p.x -= clamp(p.x, -2.0 * r, 0.0);
    return -length(p) * sign(p.y);
}
float sdBox(vec2 p, vec2 b) {
    vec2 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

// ---------------------------------------------------------------------------
// 配色方案：6 套，差别只在调色板相位（与其它 .frag 的 uColorScheme 语义一致）
// ---------------------------------------------------------------------------
vec3 schemePhase(int cs) {
    if (cs == 0) return vec3(0.00, 1.00, 2.00) / 3.0;   // 相位三相偏移（原作者的色散）
    if (cs == 1) return vec3(0.10, 1.10, 2.10) / 3.0;
    if (cs == 2) return vec3(0.25, 0.85, 1.45) / 3.0;
    if (cs == 3) return vec3(0.00, 0.50, 1.00) / 3.0;
    if (cs == 4) return vec3(0.33, 0.66, 1.00) / 3.0;
    return              vec3(0.60, 0.20, 0.80) / 3.0;
}

// ---------------------------------------------------------------------------
// 一条完整管线：depth 决定跑到第几阶段
//   out pat —— 标量场（-1..1），供 uEncode 编码 / 跨效果 morph
//   out col —— 颜色
// ---------------------------------------------------------------------------
void evalPipeline(int depth, float T, float spd, float bass, float treb,
                  out float pat, out vec3 col)
{
    // 居中 + 高度归一化（原作者用 /u_resolution.y，保证圆形不随宽高比变形）
    vec2 p = (gl_FragCoord.xy - 0.5 * uResolution.xy) / uResolution.y;

    // ===================== STAGE 01 · 对数螺线与空间极化 =====================
    // 把笛卡尔平面换成对数极坐标平面：距中心近处被指数级放大，远处平滑收敛，
    // 视窗中心成为引力奇点。这是后面所有螺旋/花瓣形态的唯一自然表达方式。
    float rad0 = max(length(p), 1e-4);
    float a     = atan(p.y, p.x);
    float lr    = log(rad0);            // r = ln|u|
    a *= 2.0;                           // θ = 2·atan2（极角倍频，给花瓣打底）

    // ===================== STAGE 02 · 复合正弦的流体谐波 =====================
    if (depth >= 1) {
        // 时间激活算子：atan 缓慢起幅 → 动画开篇不突变（原作者的工程细节）
        float l = min(1.0, atan(0.2 * T) / 0.95);
        l *= 1.0 + bass * 0.45;                          // ← 音频：低频泵
        // 径向对数扰动：把 3θ 与距离耦合再减时间 → 形成向外扩散的脉冲
        lr *= 0.6 + 0.25 * l * thc(1.0, 3.0 * a + 2.0 * rad0 - T);
    }

    // ===================== STAGE 03 · 非线性正弦旋转矩阵 =====================
    float ca = a;
    if (depth >= 2) {
        // 【修正】原作者此处 floor(0.25*fract(0.1*T)) 恒为 0 → h 恒为 1.0（死代码）。
        // 按文字意图实现为 1..4 阶梯：每 2.5 秒跳一级，旋臂分支对称阶数随之突变。
        float h  = 1.0 + floor(4.0 * fract(0.1 * T));
        float aa = h * a;
        // 旋转角随半径变化 → 内圈外圈转速不同 → 空间被剪切拉伸成涡旋
        float ang = T * spd + 2.0 * aa + 6.1415 * sin(3.0 * lr + aa - T);
        p  = rot2(p, ang);
        ca = atan(p.y, p.x);                             // 旋转后的极角：涡旋的着色依据
    }

    // ===================== STAGE 04 · 组合符号距离场花瓣 =====================
    // 前 3 步都是全局连续的空间变换，SDF 是局部有边界的实体：
    // 先用连续变换铺好流体场，再用 SDF 把骨架"钉"进去。顺序反了做不出来。
    float s = 0.0;
    if (depth >= 3) {
        float hh = 0.25;
        // 同一条谐波同时控制三角形的位置偏移与矩形尺寸 → 骨架伸长 / 收缩
        hh += 0.5 * thc(4.0, -length(p) + 2.0 * atan(p.y, p.x) + T);
        hh += 0.5 * (0.5 + 0.5 * thc(3.0, length(p) * 3.0 - T));

        // 半径要够大：p 只跨 ±0.5（高度归一化），而 hh 被 thc(3,·) 推到 ±7
        // （thc 除以 sin(3)=0.141，取值范围极大），形状会被甩出画面。
        // 半径 2.6 才能保证"即使被推开，画面仍大部分落在三角形内" → s≈1 铺满全屏，
        // 这是原图 stage 04/05 "整幅大理石纹"而非"几根细线"的成因。
        float d  = sdEquilateralTriangle(p - vec2(0.0, 0.001 - hh), 2.6);
        s  = 1.0 - smoothstep(-0.1, 0.1, d + 0.5);       // 反转软遮罩 → 硬 SDF 边界带水墨晕
        float d2 = sdBox(p - vec2(0.0, -hh), vec2(0.05, 0.2));
        s += 1.0 - smoothstep(-0.5, 0.5, d2);            // 两个 SDF 相加 = 并集
        s  = clamp(s, 0.0, 1.0);
    }

    // ===================== STAGE 05 · 余弦调色与奇点流光 =====================
    // 每个阶段暴露它自己那一层的特征场（原图只给了 5 张结果，中间三张的着色
    // 公式无法反推，这里取能让各阶段视觉可辨的最小选择）：
    //   0/1 → 对数螺旋环（lr 主导，掺一点 θ 让它像原图那样"卷"起来）
    //   2   → 旋转后的极角（涡旋剪切）
    //   3/4 → SDF 骨架强度
    // 【接缝注意】atan2 在 θ=±π 处有分支切割，极角每绕一圈跳 2π。
    // 要让 sin(field*M) 跨过切割线仍然连续，必须满足：角向系数 k × 掩膜频率 M ∈ 整数。
    // 这里取 k=1.0 / K=2.0, M=3.0，否则画面左侧会出现一条竖直亮缝。
    float field, pt, mask;
    if (depth <= 1) {
        float radial = lr * 4.0;
        field = radial + a * 1.0;                   // 对数螺线：lr ∝ θ  (1.0 × 3.0 = 3 ✓)
        pt    = radial - T * 0.35;                  // 调色板只吃径向 → 无分支切割
        mask  = pow(0.5 + 0.5 * sin(field * 3.0), 5.0);   // 细亮环，其余全黑
    } else if (depth == 2) {
        field = ca * 2.0 + lr * 1.5;                // (2.0 × 3.0 = 6 ✓)
        pt    = lr * 1.5 - T * 0.85;                // 同上：调色板不吃极角
        mask  = pow(0.5 + 0.5 * sin(field * 3.0), 4.0);
    } else {
        field = s * 1.6 + lr * 0.5;
        // 原作者的 stage 05：pal 的相位被 thc 驱动，再三相偏移出色散。
        // 【去掉了他公式里的 +theta】：极角会让 pal 在 θ=±π 处裂开一条竖缝，
        // 而 s + 2·ln|u| 本身已经足够产生大理石纹。
        pt    = thc(2.0, s + 2.0 * lr - T) - 0.5 * T + treb * 0.6;
        mask  = s;
    }

    // 单色 → 全色散：复现原图 01(蓝环) → 02(青花) → 03(白涡) → 04(墨花) → 05(虹彩)
    float chroma = clamp(float(depth) / 4.0, 0.0, 1.0);
    vec3  disp   = mix(vec3(0.0), schemePhase(uColorScheme), chroma);
    vec3  base   = pal(pt, vec3(1.0), vec3(1.0), vec3(1.0), disp);        // 0..2
    vec3  tint   = mix(vec3(0.55, 0.75, 1.0), vec3(1.0), chroma);         // 冷调 → 中性

    // 骨架/环带有多强，颜色就有多亮 —— 背景严格为 0，这是原图"黑底发光"的成因
    vec3 content = mask * (0.25 + base) * tint;

    // 暗角：距中心超过 2.25 的区域压暗（原作者 stage 05）
    content *= smoothstep(0.0, 0.1, 2.25 - length(p));

    // 奇点辉光：lr = ln|u| 在原点趋向 -∞，smoothstep(0,3.5,-lr) → 1 → 正中心过曝成暖白。
    // 这是把数学奇点当光源用的手法。
    float core = smoothstep(0.0, 3.5, -lr);
    // ← 音频：鼓点点亮花蕊。uBeatFlash 是合并的软脉冲；hitKick 是分路出来的
    //    硬瞬态（τ=0.28s），叠上去让"顶"的那一下更锐。
    core *= 1.0 + uBeatFlash * 0.8 + audioHitKick() * 1.4;
    content = mix(content, vec3(1.0, 0.93, 0.92) * 2.0, core);

    // 底色：黑底 / 白底（与 Azurite 等一致的 mix(bg, content, coverage) 语义）
    vec3 bg = (uDarkBg > 0) ? vec3(0.0) : vec3(0.95);
    col = mix(bg, content, max(mask, core));

    pat = clamp(field * 0.4 + s * 0.6, -1.0, 1.0);
}

void main()
{
    // ★ 抽插变速：画面的内部时钟 = 真实时间 + hit 累积的额外相位。
    //
    // 为什么不是 uTime * audioHitPump(...)：
    //   uTime 是个很大的绝对值（跑一会儿就上百秒），乘一个 1→1.6 的倍率会让
    //   整个相位瞬间平移（100 → 160），画面直接瞬移。
    //   uHitDrive 是 CPU 端把"速度倍率"【积分】出来的相位偏移 —— 只是这段时间
    //   走得快些，变化连续。这个做法与工程里的 uAudioPhase / g_b376FlowPhase
    //   / g_cubeCamPhase 完全一致（见 main.cpp 的 UpdateBeat）。
    //
    // 效果：kick 满格时画面以 9.5× 实时速度推进（snare 叠上去再 ×5.7），
    //       τ=0.28s 内回落 —— 底鼓"顶"一下，军鼓"啪"一下，就是抽插。
    float T   = uTime + uHitDrive;
    float spd = uBpm / 120.0;                                // 用户速度已由时间轴缩放

    // 单形态：永远跑完整五阶段管线（STAGE 01 → 05 累进）。
    // 这里刻意不做 uForm/uFormB 混合 —— 五阶段是建构过程，不是并列观感。
    // 鼓点不切形态，走的是 uHitDrive（抽插变速）与 uHitKick（花蕊过曝）。
    float pat; vec3 col;
    evalPipeline(QS_STAGE, T, spd, uBassAtt, uTrebAtt, pat, col);

    // === Pass 1 编码：R=pattern, GBA=颜色 ===
// === Pass 2 跨效果 morphing（匹配 CPU TransitionEngine）===
        col = audioGrade(col);                               // 与其它 .frag 同源的能量/染色/鼓点调制
    col = cpuFinalize(col, gl_FragCoord.xy, uDitherStrength);
    fragColor = vec4(col, uOpacity);
}
