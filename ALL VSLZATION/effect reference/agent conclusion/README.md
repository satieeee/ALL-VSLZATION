# agent conclusion — 风格化数学函数文件

本文件夹存放**其他 agent 总结的风格化数学函数文件**：可复用的图案 / 变换 / 配色 / 缓动 / 噪声等数学函数，用于在生成新的程序化可视化效果时直接复用，避免重复造轮子。

`PROMPT_新效果开发模板.txt`（CPU）和 `PROMPT_GPU效果开发模板.txt`（GPU）的「第零步」都会先检查本文件夹——**只要这里有文件，生成新效果时就会优先复用其中的函数**。

---

## 一、文件格式约定

每个文件 = **一个函数族 / 一种风格**。统一用 Markdown，带 YAML frontmatter（便于程序化检索）：

```markdown
---
name: <kebab-case-短横线命名>
type: pattern | transform | color | easing | noise | utility
language: cpp | glsl | both
source: <来源说明：哪个 agent / 哪种参考风格>
---

# <函数族名>

## 数学定义
（核心公式 + 参数名 + 参数取值范围）

## 参考实现

### C++（可选，CPU GDI 效果用）
```cpp
// 函数签名 + 关键实现
```

### GLSL（可选，GPU shader 用）
```glsl
// 函数签名 + 关键实现
```

## 参数范围与调参建议
（哪些参数调到什么值对应什么视觉感受）

## 使用注意
（数值边界、性能、与归一化坐标的配合等）
```

### frontmatter 字段说明

| 字段 | 取值 | 说明 |
|------|------|------|
| `name` | 短横线命名，如 `kaleido-fold` | 文件名也建议与之一致 |
| `type` | `pattern` / `transform` / `color` / `easing` / `noise` / `utility` | 函数类别 |
| `language` | `cpp` / `glsl` / `both` | 提供哪种语言实现 |
| `source` | 自由文本 | 来源：哪个 agent、基于什么参考风格/图片 |

---

## 二、函数类别（type）

| type | 含义 | 典型示例 |
|------|------|---------|
| `pattern` | 形态/图案函数（EvalForm 主体） | 万花筒折叠、分形、VHS 条纹 |
| `transform` | 坐标/投影变换（MapProjection） | 极坐标、螺旋、隧道深度 |
| `color` | 配色/色调映射（MapColor） | HSL 调色板、白底淡区、色相环 |
| `easing` | 缓动/过渡函数 | smoothstep、弹性、回弹 |
| `noise` | 噪声函数 | value noise、simplex、有序抖动 |
| `utility` | 基础数学工具 | 快速三角逼近、混合模式 |

---

## 三、语言覆盖要求

- 标 `cpp` 的函数供 **CPU GDI 效果**（`VisualEffects/<Name>/*.cpp`）复用。
- 标 `glsl` 的函数供 **GPU shader**（`VisualEffects/shaders/*.frag`）复用。
- 标 `both` 最理想：同一数学定义给出 C++ 和 GLSL 两份实现，保证 CPU/GPU 两版效果视觉一致（本项目强调 CPU/GPU 1:1 对齐）。

---

## 四、命名与坐标约定（重要）

- 所有函数必须**只用归一化坐标**：
  - 极坐标：`r ∈ [0, ~1.4]`、`theta ∈ [0, 1]`（10 楔形折叠后）
  - 笛卡尔：`x, y ∈ [-1, 1]`
- **禁止硬编码像素尺寸**——CPU 渲染分辨率是动态自适应的，GPU 是 1:1，任何假设固定分辨率的函数都会在缩放/降分辨率时错位。
- 时间参数统一用 `t`（秒），速度倍率统一用 `speed`。

---

## 五、最小示例

```markdown
---
name: kaleido-fold
type: transform
language: both
source: 参考 Verdigris 的 10 楔形万花筒折叠
---

# 万花筒折叠 KaleidoFold

## 数学定义
把归一化角度 theta 折叠到 10 楔形（交替镜像），映射到 [0,1]：
  a = theta * 2π            // 还原弧度
  wedge = 2π / 10
  a = mod(a, wedge)
  if (a > wedge/2) a = wedge - a   // 交替镜像
  theta' = a / wedge

## 参考实现

### C++
```cpp
inline float KaleidoFold(float theta) {
    constexpr int N = 10;
    constexpr float wedge = 6.28318530718f / N;
    float a = fmodf(theta * 6.28318530718f, wedge);
    if (a > wedge * 0.5f) a = wedge - a;
    return a / wedge;
}
```

### GLSL
```glsl
float kaleidoFold(float theta) {
    const int N = 10;
    const float wedge = 6.28318530718 / float(N);
    float a = mod(theta * 6.28318530718, wedge);
    if (a > wedge * 0.5) a = wedge - a;
    return a / wedge;
}
```

## 参数范围与调参建议
- N 越大图案越密；10 楔形与 Verdigris/Psychedelic/GlitchForge 当前架构一致。
- 角频率在折叠后需放大（theta 被压到 [0,1]）。

## 使用注意
- CPU 版在 RebuildLUT/RebuildBuf 中预计算，避免每像素重复折叠。
```

---

## 六、与现有资源的关系

- `../VisualEffects/FastMath.h`：快速三角逼近（`fast_sin`/`fast_cos`），属于 `utility` 类，可作为本文件夹内 `fast-trig` 文件的参考来源。
- `.claude/skills/metalheart-*`：Metalheart 风格的建模/渲染规则，属于「风格化」内容的另一种组织形式（面向 3D），可作为本文件夹风格化数学函数的参考范式。
