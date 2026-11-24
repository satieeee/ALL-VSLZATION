# ALLVSLZATION V2

Windows 上的**实时音乐可视化器**。原生 Win32 + OpenGL 3.3，无第三方依赖，无虚拟声卡要求 —— 直接抓系统正在播放的声音。

- **40 个可视化效果**，分 5 个家族：Normal(13) / Metalheart(4) / Cube(3) / 3D(2) / Layers(18)
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

**上半部分：效果画廊** —— 40 个效果以卡片网格显示，每张卡是**实时渲染出来的缩略图**（启动后约 1 秒内逐个补齐）。

1. 顶部一排 **家族筛选**：全部 / 常规 / Metalheart / Cube / 3D / Layers
2. **点卡片 = 直接加入播放链**（用当前「时长」）
3. 滚轮在画廊里滚动；鼠标移上去有高亮

**下半部分：播放链 dock**

| 控件 | 作用 |
|---|---|
| 播放链行 | 点一下选中；超过 3 项时右侧出现滚动条，在 dock 上滚滚轮翻页 |
| 右键某一行 | 上移 / 下移 / 删除这一项 / 清除上层效果 |
| 右键某张卡片 | 设为**选中播放链项的上层** / 加到播放链末尾（左键仍是直接加） |
| 时长 Ns | 点一下弹出输入框，填任意秒数（1–3600） |
| 清空 | **长按 1 秒**才生效（按住时按钮会填红），清掉整条播放链，防误触 |
| ▶ 播放链 / ■ 停止 | 开始 / 结束播放 |
| 混合 | 点一下**展开列表**选 8 种 Photoshop 算法之一（Esc / 点别处收起） |
| 不透明度 | 点轨道上任意位置设定 |
| 预设 1–5 | 左键载入；**右键**可「追加到播放链 / 覆盖保存当前播放链 / 清空」 |

> 关于「上层效果」：混合算法只对**有上层的那一条**生效。用法：先在播放链里选中一项（底层），
> 再右键画廊里想叠在上面的效果卡片 →「设为选中项的上层」。上层只接受常规 / Layers 族
> （与 Metalheart / Cube / 3D 共用整屏 shader 路径才能叠帧），且不能与底层是同一条。
> 设上层时会自动给一个默认算法（滤色），之后用「混合」下拉改。行副标题会显示实际的上层名。

### 播放窗口

置顶的独立窗口，按 **Esc** 关闭（同时停止播放）。

**切歌/切效果是等鼓点触发的**：到了时长会进入"等待鼓点"状态，下一记鼓才真正切换 —— 这样转场总是踩在拍子上。

### 设置（主窗口右上角按钮）

| 项 | 说明 |
|---|---|
| 速度 | 时间轴整体倍率 0.1× ~ 10×（积分进相位，拖动不会跳） |
| BPM / Tap | 手动指定 BPM，或用 Tap 连点测速 |
| 背景 黑底 | 深色底 / 浅色底 |
| 窗口置顶 | 播放窗口是否总在最前面（默认开，改了立刻生效） |
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
- **播放窗口默认置顶**，会盖在别的窗口上面。不想要就打开 **设置**，把 **窗口置顶** 的勾去掉（立刻生效，不用重播）
- 过渡动画是"等鼓点"的：**没有音乐时不会自动切下一个效果**（这是有意的，转场要踩在拍子上）
