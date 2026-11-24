// ============================================================================
// audio_prelude.glsl —— 音频驱动 ABI（v1）
// main.cpp 在 LoadEffects 时拼接进每个 effect .frag 之前（在 CpuArtifact.glsl 之后）。
// 与 VisualEffects/audio_helpers.h 严格同源（CPU 版同名同公式），效果两端必须 1:1。
//
// 中性默认：乘→1.0、加→0.0、系数→0.5；静音时未显式使用音频的效果保持原行为。
// 例外：audioGrade 亮度绑定整体能量（静音会变暗）——这是音频可视化的本意。
// ============================================================================

uniform float uFft[16];            // 攻击平滑频段 0..1
uniform float uWave[32];           // 下采样波形 -1..1
uniform float uBass, uMid, uTreb;
uniform float uBassAtt, uMidAtt, uTrebAtt;
uniform float uBassDec, uMidDec, uTrebDec;
uniform float uEnergy;             // 整体 RMS（典型≈0.5）
uniform float uBeatFlash;          // kick 命中=1，~0.18s 衰减
uniform float uAudioPhase;       // 音频驱动旋转相位（CPU 累计：变速不跳回）

// ---- 鼓点分路瞬态包络（2026-09-30 新增）----
// BPMDetector 里 kick 与 snare 是两套独立的频谱签名判定，但过去只把二者合并成
// 一个布尔传下来（IsKick/IsSnare 从未被调用）。这两路把分路接出来，快起慢落：
//   hitKick  τ≈0.28s 深而慢 —— 主抽插（"顶"）
//   hitSnare τ≈0.12s 浅而快 —— 次级颤动（"啪"）
// uHitDrive 是按 (1 + 0.55·hitKick + 0.18·hitSnare) 积分的【额外时间】。
// 把它加到效果自己的时间上就是抽插变速；CPU 端积分，所以变速时不跳回。
//
// 【单次运动周期通道】上面两条是尖峰（attack 几毫秒、τ=0.28/0.12s）。直接拿 uHitDrive
// 推时间轴，它的导数是方波 —— 画面只能"猛冲一下、停住、再猛冲"（鬼畜抽动）。
// 这条通道的规则是：
//   · 没有 hit            → 速率极低（0.03），画面几乎是冻的
//   · 一记 hit            → 开一个 1.2s 的周期：速率按 sin(πp)^0.7 从 idle 涨到正常速度再落回来
//   · 周期进行中又来 hit  → 忽略（所以连击不会叠加）
// uHitFlow      = 当前速率归一化 0..1（想用"现在多用力"就用它，如亮度）
// uHitFlowDrive = 它的积分（秒），推时间轴用这个 —— 单调，不回弹
uniform float uHitKick, uHitSnare, uHitDrive;
uniform float uHitFlow, uHitFlowDrive;

// ---- layers.html 兼容别名 ----
// 工具页的 HEAD 里有一个合并包络 uHit = max(hitKick, hitSnare * 0.75)。
// 从 layers.html 直接抄过来的配方都按这个名字写，所以这里给它一个同名 uniform，
// 由 main.cpp 每帧按同一公式上传，配方不用改名就能编译。
// 注意：工具页【没接音频】时会用合成脉冲兜底；引擎这边没有音频就是 0（不伪造鼓点）。
uniform float uHit;

// uHit 的平滑版（一阶低通：起 τ=0.10s / 落 τ=0.55s）。瞬时包络直接推亮度/半径会抖，
// 要"有惯性"的驱动就用它。与 layers.html 的 uHitt 同名同义（那边 JS 每帧同样做一次低通）。
uniform float uHitt;

// ============================================================================
// 频谱形状描述子（2026-09-14 新增）—— 描述【音色】而不是响度
//
// 上面 uFft[16] 回答"每个频段多响"；这一组回答"这个声音长什么样"。
// 全部是比值 → 【不受系统音量影响】（与 uEnergy/uBass 那些绝对量相反）。
// 与 VisualEffects/audio_helpers.h 的同名函数严格同源。
// ============================================================================
uniform float uCentroid;   // 质心  0..1：亮度 —— 能量偏低频还是高频
uniform float uSpread;     // 扩散  0..1：宽窄 —— 纯音窄、噪声/失真铺得宽
uniform float uRolloff;    // 滚降  0..1：频谱【上边界】在哪（85% 能量以下）
uniform float uFlatness;   // 平坦度 0..1：噪声性(→1) vs 音调性(→0)
uniform float uFlux;       // 通量  0..1：起音强度（瞬间，只算频谱增长）
uniform float uFluxAtt;    // 通量的攻击包络（快起慢落）
uniform float uChroma[12]; // 色度：12 个半音的能量【占比】，12 个加起来 = 1

/// 某个半音的能量占比（0..1）。i: 0=C, 1=C#, …, 11=B
float audioChroma(int i) { return clamp(uChroma[clamp(i, 0, 11)], 0.0, 1.0); }

float audioBand(int i)      { return clamp(uFft[i], 0.0, 1.0); }
float audioPump(float amt)  { return 1.0 + uBassAtt * amt; }        // 乘法，中性=1
float audioPumpDec(float amt){ return 1.0 + uBassDec * amt; }
float audioDrive(float amt) { return 0.5 + (uMidAtt - 0.5) * amt; } // 系数，中性=0.5
float audioAdd(float amt)   { return uTrebAtt * amt; }              // 加法，中性=0
float audioWave(float x, float amt) {
    float f = fract(x);
    float fi = f * 31.0;
    int ia = int(fi), ib = min(ia + 1, 31);
    return mix(uWave[ia], uWave[ib], fi - float(ia)) * amt;
}
float audioHitKick()  { return clamp(uHitKick,  0.0, 1.0); }
float audioHitSnare() { return clamp(uHitSnare, 0.0, 1.0); }
/// 抽插速度倍率（乘法，中性=1）。两个 amt 是各自要注入的强度。
float audioHitPump(float kickAmt, float snareAmt) {
    return 1.0 + uHitKick * kickAmt + uHitSnare * snareAmt;
}

vec3 audioGrade(vec3 c) {           // 颜色最终调制（与 CPU audioGrade 同源）
    c *= 0.85 + uEnergy * 0.3;      // 能量 0.5 → 中性 1.0，峰值仅 ~1.15，不爆不暗
    c += vec3(audioBand(2), audioBand(8), audioBand(14)) * 0.12;   // 弱频段染色
    c += vec3(uBeatFlash * 0.03);   // 极弱鼓点闪光（防频闪/压黑底）
    return clamp(c, 0.0, 1.0);
}
