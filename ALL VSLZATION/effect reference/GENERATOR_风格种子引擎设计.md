# Generator 设计：风格种子 → 线条引擎

## 0. 定位

本 generator 是「风格种子」的解释器：输入 StyleSeed（紧凑 JSON，由视觉 Agent 从图片蒸馏），
输出确定性的线条画面。它是**固定引擎**——图片只决定种子，不决定代码；
同一批图 → 同一种子 → generator 确定性生成同样的线条（类比游戏世界种子）。

## 1. 架构

```
StyleSeed (JSON, ~22 字段)
   │
   ▼
Generator（固定 GLSL/C++ 函数，三段流水线）
   ├─ ① 坐标预处理：极坐标 + 对称折叠 + 矢量场 warp
   ├─ ② 图案场生成：曲线族 base pattern + 多频细节 + 噪声
   └─ ③ 线条渲染：SDF 线宽/锐度 → 配色 → 输出
   │
   ▼
线条（确定性：同种子 → 同线条）
```

## 2. 种子 Schema（对齐 PROMPT_视觉Agent函数生成规范.txt 输出）

```json
{
  "name": "hyper-chrome-vortex",
  "curveFamily": "logarithmic-spiral",   // 曲线族
  "fieldType": "vortex",                  // 矢量场奇点
  "symmetryFold": 8,                      // N 重旋转对称
  "mirror": false,
  "armCount": 8,                          // 臂数/瓣数
  "spiralGrowth": 0.15,                   // 螺旋增长率 b
  "twist": 0.65,                          // 螺旋度（径向↔切向）
  "curvature": 0.30,                      // 曲率
  "freqLow": 0.08,                        // 低频（主结构）
  "freqHigh": 2.80,                       // 高频（细节）
  "complexity": 4,                        // 嵌套层数
  "thickness": 0.18,                      // 线宽
  "sharpness": 0.30,                      // 边缘锐度
  "hue": 210,                             // 主色相（度）
  "satRange": [0.30, 0.70],
  "lightRange": [0.10, 0.90],
  "darkBg": true,                         // 黑底(true)/白底(false)
  "beta": 1.82,                           // 幂律指数（噪声频谱）
  "fractalDim": 1.65,                     // 分形维数（fbm octave）
  "noiseType": "fbm",                     // none/value/fbm
  "noiseSeed": 1234567                    // 确定性随机种子
}
```

## 3. 字段 → 数学映射（核心契约）

| 字段 | 数学含义 | generator 中的操作 |
|------|---------|------------------|
| `curveFamily` | 曲线族 | 选择 base pattern 公式（见 §3.1） |
| `fieldType` | 矢量场奇点 | 选择坐标 warp（见 §3.2） |
| `symmetryFold` N | N 重旋转对称 | 角度折叠：`a = mod(θ·2π, 2π/N)`，镜像则折半 |
| `mirror` | 镜像对称 | 折叠后 `if a>wedge/2: a=wedge-a` |
| `armCount` | 臂数 | 角向频率 `armCount · θ` |
| `spiralGrowth` b | 对数螺旋增长率 | `log(r)·b` 项（r 的指数调制） |
| `twist` | 螺旋度 | 径向↔切向混合 `mix(r, θ_folded, twist)` |
| `curvature` | 曲率 | 细节层的时间/空间弯曲量（振幅/相位） |
| `freqLow` / `freqHigh` | 频率结构 | 多频叠加的频段端点 |
| `complexity` | 嵌套层数 | 循环叠加次数（1~6 层） |
| `thickness` | 线宽 | SDF 线宽（`abs(v)` 的 smoothstep 区间） |
| `sharpness` | 边缘锐度 | smoothstep 过渡宽度 |
| `hue` / `satRange` / `lightRange` | 配色 | HSL→RGB，pattern 值调制色相/明度 |
| `darkBg` | 背景 | 黑底 `vec3(0.02)` / 白底 `vec3(0.97)` |
| `beta` | 幂律指数 | 噪声频率缩放 `uv·(beta·4+1)` |
| `fractalDim` D | 分形维数 | fbm octave 数 `int(D·4+2)`、噪声振幅 |
| `noiseType` | 噪声类型 | none / value / fbm |
| `noiseSeed` | 确定性随机 | hash 种子（注入噪声坐标偏移） |

### 3.1 曲线族 → base pattern 公式

| curveFamily | base pattern |
|-------------|-------------|
| logarithmic-spiral | `sin(armCount·θ·2π + log(max(r,ε))·b·30)` |
| archimedean-spiral | `sin(armCount·θ·2π + r·b·30)` |
| rose | `cos(armCount·θ·2π)·(1−0.5r)` |
| radial | `sin(armCount·θ·2π)` |
| grid | `sin(x·freqLow·25)·sin(y·freqLow·25)` |
| freeform | 样条近似（当前用 grid 退化，待扩展） |

### 3.2 矢量场奇点 → 坐标 warp

| fieldType | warp |
|-----------|------|
| source | 径向：`rr = mix(r, θ_folded, twist·0.5)` |
| sink | 径向反向（r 取反后再同上） |
| vortex | 切向：`rr = mix(r, θ_folded, twist)` |
| saddle | 双曲：`rr = x·y`（简化） |
| uniform | 无 warp（用笛卡尔 uv 直接） |

## 4. 确定性契约

1. 空间结构只由 seed 决定；`uTime` 只驱动动画（相位推进），不改变结构。
2. `noiseSeed` 保证任何噪声都确定（同种子 → 同噪声场）。
3. 同一 seed 在不同实现（GLSL / C++）下应视觉一致——这是 CPU/GPU 1:1 对齐的前提。
4. 种子是可序列化的（JSON），可存盘、可版本化、可复现。

## 5. 参考实现

- **原型（可运行）**：`seed_generator_demo.html` —— WebGL 片段着色器实现三段流水线，
  带滑杆实时调参，用于验证「种子 → 线条」映射是否正确。
- **未来移植**：
  - GPU 效果：把 HTML 里的 fragment shader 按 `PROMPT_GPU效果开发模板.txt` 包装成 `.frag`。
  - CPU 效果：把 GLSL 逻辑逐行翻译成 C++（GDI），对齐 `IProceduralEffect` 接口。

## 6. 已知简化与待扩展

- `freeform` 曲线族、`saddle` 矢量场当前用退化近似，后续单独细化。
- 配色目前是「pattern 值调制单一主色相」，未支持 2~4 个配色变体的插值（seed 的 `satRange/lightRange` 已预留）。
- `curvature` 目前只影响细节层的相位/振幅，尚未精确对应 κ(s) 分布。
