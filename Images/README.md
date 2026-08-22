# Images 文件夹

此文件夹用于存放可视化效果中可能用到的图片资源。

## 用途

- 背景图片（background.png / bg.jpg）
- 粒子纹理（particle.png）
- Logo / 水印图片
- UI 图标

## 当前状态

**暂无图片。** 请根据需要自行添加图片文件到此文件夹。

## 在代码中使用

```cpp
// 加载图片示例（使用 GDI+）
#include <gdiplus.h>
Gdiplus::Image img(L"Images/background.png");
graphics.DrawImage(&img, 0, 0, width, height);
```

> 注意：如果编译时需要 GDI+，请在 CMakeLists.txt 中添加 `gdiplus` 库链接。
