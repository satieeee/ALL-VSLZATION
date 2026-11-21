# ALLVSLZATION V2

Windows 上的**实时音乐可视化器**。原生 Win32 + OpenGL 3.3，无第三方依赖，无虚拟声卡要求 —— 直接抓系统正在播放的声音。

- **39 个可视化效果**，分 5 个家族：Normal(17) / Metalheart(4) / Cube(3) / 3D(2) / Layers(13)
  —— 其中 12 个是程序化几何（网格/粒子/线段），27 个是全屏片段着色器
- 效果可以**排成播放链**自动轮播，带跨效果过渡、混合模式、不透明度
- 每个效果吃同一套**音频 ABI**：16 段频谱、波形、底鼓/军鼓瞬态、BPM、鼓点相位、音色描述子
- 运行时读 shader：改 `.frag` 不用重新编译，重开程序即可

---

## 系统要求

| | |
|---|---|
| 系统 | Windows 10 / 11（用到了 WASAPI 环回采集） |
| 显卡 | 支持 OpenGL 3.3 的显卡（10 年前的集显都行） |
| 编译 | Visual Studio 2022 (MSVC)、CMake ≥ 3.15、Ninja |

运行**不需要**安装任何运行库，也不需要装虚拟声卡或"立体声混音"。

---

## 编译

### 方式 A：用 `build.bat`（推荐）

`build.bat` 里写死了本机的工具链路径，**先改这四行**：

```bat
set "PROJ=<你的工程目录>"
call "<VS安装目录>\VC\Auxiliary\Build\vcvars64.bat"
set "CMAKE=<VS安装目录>\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
```

然后双击 `build.bat`，产物是 `out\MAIN.exe`。

> `build.bat` 开头会 `taskkill /IM MAIN.exe /F` —— 程序开着时它会先关掉你的窗口。
> 只想验证"能不能编译"、不想关窗口，用 `_build_noKill.bat`（链接会因为文件占用失败，但编译照跑）。

### 方式 B：手动 CMake

```bat
cd <工程目录>
mkdir out && cd out
cmake .. -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build .
```

---

## 运行

直接跑 `out\MAIN.exe`。

程序按 **exe 所在目录的上一级**去找资源和 shader：

```
<工程根>
├─ out\
│   └─ MAIN.exe          ← 运行这个
└─ VisualEffects\
    ├─ shaders\          ← 27 个 .frag
    ├─ audio_prelude.glsl
    ├─ simulation effect VST\CpuArtifact.glsl
    └─ lib\              ← 可选注入的噪声库
```

所以 **`out\` 目录不要单独拷走**，要和 `VisualEffects\` 保持同级。

---

## 界面上怎么用

### 主窗口

**左栏「可用的可视化效果」** —— 按家族分组的树，条目后面的绿点表示 GPU 可用（当前全部可用）。

1. 点选左边任意一个效果
2. 点 **加入 →**，它进入右边「播放链」
3. `时长(秒)` 决定这一项播多久（默认 30 秒）
4. 点 **▶ 播放链** 开始，会弹出一个独立的置顶播放窗口

**右栏「播放链」** 的操作：

| 控件 | 作用 |
|---|---|
| 加入 → / ← 移除 | 加入选中效果 / 移除选中项 |
| ↑ ↓ | 调整顺序 |
| 混合模式 | 与**上层效果**叠加时的算法：变亮/滤色/叠加/强光/柔光/线性加深/正片叠底 |
| 不透明 | 上层的透明度 |
| 设为上层 / 清除上层 | 给当前项挂一个"第二层效果"，两层同时渲染 |
| 预设 1–5 | 把整条播放链存进/取回一个槽位 |

> 播放链里的一项可以是**两层效果叠加**：先选中播放链里的某项 → 在左树里选另一个效果 → 点「设为上层」。

### 播放窗口

置顶的独立窗口，按 **Esc** 关闭（同时停止播放）。

**切歌/切效果是等鼓点触发的**：到了时长会进入"等待鼓点"状态，下一记鼓才真正切换 —— 这样转场总是踩在拍子上。

### 菜单 → 设置

| 项 | 说明 |
|---|---|
| 速度 | 时间轴整体倍率 0.1× ~ 10×（积分进相位，拖动不会跳） |
| BPM / Tap | 手动指定 BPM，或用 Tap 连点测速 |
| 背景 黑底 | 深色底 / 浅色底 |
| 音频强度 | 所有音频驱动的整体缩放，调小了画面更"静" |
| 随机：数量 / 不透明度 / 时长范围 | 「随机」按钮生成播放链时用的区间 |

---

## 快捷键

播放窗口获得焦点时生效：

| 键 | 作用 |
|---|---|
| **B** | 深色底 ↔ 浅色底 |
| **D** | 颗粒/GDI 质感 开 ↔ 关 |
| **Q** | ⅔ 降采样（画质降级，省显卡） |
| **F** | MilkDrop 式帧反馈（残影）开 ↔ 关 |
| **P** | Metalheart1 的运动单元调试边 |
| **Esc** | 关闭播放窗口 |

---

## 目录结构

```
main.cpp                    引擎 + UI + 播放链（约 5000 行）
AudioCapture/               WASAPI 环回采集 + FFT + 音频分析
BPMdetector/                鼓点检测（kick / snare 两套频谱签名）
SharedState.h               跨进程共享内存（BPM / 相位 / 鼓点）
VisualEffects/
  shaders/                  27 个全屏 .frag 效果（运行时读盘）
  audio_prelude.glsl        音频 uniform 与工具函数（拼在每个 .frag 前面）
  simulation effect VST/    CpuArtifact.glsl —— 抖动/色调/收尾的共用 ABI
  B376 M2 M3 M4 C3 F1 PC SS VX TN XS Transition/
                            各程序化效果的几何数据与渲染器
```

---

## 加一个自己的效果

只要两件事：

1. 写一个 `VisualEffects\shaders\MyFx.frag`（照抄任意现成效果的头部/尾部协议）
2. 在 `main.cpp` 的 `kEffectNames[]` 里加一行 `L"MyFx"`

因为 shader 是运行时读盘的，**第 1 步改完不用重编译**，重开程序就生效；只有第 2 步（注册新名字）需要重新编译。

效果能用的音频 uniform 全在 `VisualEffects\audio_prelude.glsl` 里，同名同义：

```glsl
uniform float uBass, uMid, uTreb;            // 频段能量
uniform float uBassAtt, uMidAtt, uTrebAtt;   // 起音包络（快起慢落）
uniform float uEnergy, uBeatFlash, uBpm;
uniform float uHitKick, uHitSnare;           // 底鼓/军鼓瞬态（尖峰）
uniform float uHitDrive;                     // 瞬态的积分
uniform float uHitFlow, uHitFlowDrive;       // 平滑后的鼓点强度与其积分
uniform float uQ28;                          // MilkDrop 的 per-frame 标量
uniform float uFft[16]; uniform float uWave[];
```

通常**时间轴/缩放这类要连续的量用 `uHitFlow*`**，**闪光/爆点这类要脆的用 `uHitKick`** —— 反过来就会出现"一冲一停"的抽动。

---

## 已知限制

- 只支持 Windows（WASAPI 环回是 Windows 专有）
- **播放窗口是置顶的**（`WS_EX_TOPMOST`）—— 它总是盖在别的窗口上面，不想让它挡着就按 Esc 关掉
- 过渡动画是"等鼓点"的：**没有音乐时不会自动切下一个效果**（这是有意的，转场要踩在拍子上）
- 部分效果依赖烘焙好的几何数据（`VisualEffects/M2|M3|M4/*Mesh.h`，体积占仓库 90%），这些是生成物，改不了也没必要改
