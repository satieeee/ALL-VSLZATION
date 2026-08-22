/**
 * @file ImageStyleAnalyzer.cpp
 * @brief 参考图风格分析器 —— 逐像素采集 + 边缘函数识别 + 色彩/纹理统计
 *
 * 用途：分析用户指定的参考图片，输出结构化风格数据，
 *       供后续创建与之匹配的程序化可视化效果（新的 IProceduralEffect）。
 *
 * 输出维度：
 *   1. 色彩 —— 主色调、饱和度/亮度分布、HSL 直方图
 *   2. 边缘 —— Sobel 梯度幅值/方向分布、边缘密度
 *   3. 纹理 —— 局部对比度、高频/低频能量比
 *   4. 构图 —— 亮度重心、色彩空间分布
 *
 * 用法：
 *   单张：ImageStyleAnalyzer.exe <图片路径>
 *   批量：ImageStyleAnalyzer.exe <图1> <图2> <图3> ...
 *   文件夹：ImageStyleAnalyzer.exe "D:\refs\*.png"
 *
 *   多张同风格图片 → 输出每张单独报告 + 汇总平均报告
 *   （汇总报告过滤单张偶然特征，保留多张共识特征——更准确还原风格）
 *
 * 编译（需要 GDI+）：
 *   cl -EHsc -O2 -std:c++17 -utf-8 ImageStyleAnalyzer.cpp gdiplus.lib
 */

#define NOMINMAX
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <gdiplus.h>
#include <vector>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <string>
#include <map>
#include <sstream>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;
#include "json.hpp"
using json = nlohmann::json;

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// ============================================================================
// HSL 颜色
// ============================================================================
struct HSL { float h, s, l; };
struct RGB { uint8_t r, g, b; };

static HSL RGBtoHSL(uint8_t r, uint8_t g, uint8_t b) {
    float rf = r / 255.0f, gf = g / 255.0f, bf = b / 255.0f;
    float mx = (std::max)({rf, gf, bf});
    float mn = (std::min)({rf, gf, bf});
    float d = mx - mn;
    HSL hsl;
    hsl.l = (mx + mn) / 2.0f;
    if (d < 0.0001f) { hsl.h = 0; hsl.s = 0; }
    else {
        hsl.s = (hsl.l > 0.5f) ? d / (2.0f - mx - mn) : d / (mx + mn);
        if      (mx == rf) hsl.h = 60.0f * fmodf((gf - bf) / d + 6.0f, 6.0f);
        else if (mx == gf) hsl.h = 60.0f * ((bf - rf) / d + 2.0f);
        else               hsl.h = 60.0f * ((rf - gf) / d + 4.0f);
    }
    return hsl;
}

// ============================================================================
// 分析结果容器
// ============================================================================
struct StyleProfile {
    // ---- 图像基本信息 ----
    int width = 0, height = 0, totalPixels = 0;

    // ---- 色彩 ----
    float avgHue = 0, avgSat = 0, avgLight = 0;
    float hueSpread = 0, satSpread = 0, lightSpread = 0;
    int   hueHist[36] = {};      // 每 10° 一个桶
    int   satHist[10] = {};      // 每 0.1 一个桶
    int   lightHist[10] = {};    // 每 0.1 一个桶
    float dominantHue = 0;       // 最大桶的中心色调
    int   dominantHueCount = 0;
    float paletteDiversity = 0;  // 调色板丰富度 (0=单色, 1=全谱)

    // ---- 边缘 ----
    float edgeDensity = 0;       // 边缘像素占比 (梯度>阈值)
    float avgEdgeMag = 0;        // 平均边缘强度
    float edgeMagMax = 0;        // 最大边缘强度
    float edgeOrientHist[8] = {}; // 8 方向分布 (0°→360°, 每45°)
    int   edgeMagHist[10] = {};  // 边缘强度直方图
    float dominantOrient = 0;    // 主边缘方向
    float orientConcentration = 0; // 方向集中度 (0=均匀, 1=单一方向)

    // ---- 纹理 ----
    float localContrast = 0;     // 平均局部对比度
    float highFreqEnergy = 0;    // 高频能量占比
    float textureComplexity = 0; // 纹理复杂度 (0=平滑, 1=极复杂)

    // ---- 构图 ----
    float brightCenterX = 0, brightCenterY = 0;
    float colorSpreadX = 0, colorSpreadY = 0;

    // ---- 线条：几何形态 ----
    int   lineCount = 0;
    float lineDensity = 0;
    float avgLineLength = 0, minLineLength = 0, maxLineLength = 0, totalLineLength = 0;
    float stddevLineLength = 0;
    float avgCurvature = 0, maxCurvature = 0, stddevCurvature = 0;
    int   straightLines = 0, curvedLines = 0, jaggedLines = 0;
    float orientationEntropy = 0;  // 方向熵 (0=单一方向, 1=完全均匀)

    // ---- 线条：拓扑结构 ----
    int   junctionCount2 = 0, junctionCount3 = 0, junctionCount4plus = 0;
    float isolatedLineRatio = 0;   // 孤立线段占比（无交点）

    // ---- 线条：空间统计 ----
    float spatialDensityGrid[8][8] = {};  // 8×8 网格线密度
    int   angleHist[18] = {};             // 0-180° 每 10° 线方向直方图

    // ---- 线条：风格 ----
    float avgSmoothness = 0;
    float hSymmetry = 0, vSymmetry = 0;

    // ---- 闭合回路 ----
    int   closedLoopCount = 0;
    float avgCircularity = 0, avgRectangularity = 0;

    // ---- 周期性 ----
    float periodicityX = 0, periodicityY = 0;
    float regularityScore = 0;
};

// ============================================================================
// 核心分析引擎
// ============================================================================
class ImageStyleAnalyzer {
public:
    StyleProfile Analyze(const wchar_t* imagePath);

private:
    std::vector<uint32_t> m_pixels;
    int m_w = 0, m_h = 0;

    void LoadImage(const wchar_t* path);
    void AnalyzeColors(StyleProfile& p);
    void AnalyzeEdges(StyleProfile& p);
    void AnalyzeTexture(StyleProfile& p);
    void AnalyzeComposition(StyleProfile& p);
    void AnalyzeLines(StyleProfile& p);

    // Sobel 梯度
    struct Gradient { float mag; float orient; };
    Gradient SobelAt(int x, int y) const;
    uint32_t Sample(int x, int y) const {
        if (x < 0) x = 0; if (x >= m_w) x = m_w - 1;
        if (y < 0) y = 0; if (y >= m_h) y = m_h - 1;
        return m_pixels[y * m_w + x];
    }
};

// ============================================================================
void ImageStyleAnalyzer::LoadImage(const wchar_t* path) {
    Bitmap bmp(path);
    if (bmp.GetLastStatus() != Ok) {
        wprintf(L"ERROR: Cannot load image: %s\n", path);
        return;
    }

    m_w = bmp.GetWidth();
    m_h = bmp.GetHeight();
    m_pixels.resize(m_w * m_h);

    // 逐像素读取
    for (int y = 0; y < m_h; ++y) {
        for (int x = 0; x < m_w; ++x) {
            Color c;
            bmp.GetPixel(x, y, &c);
            m_pixels[y * m_w + x] = c.GetValue();
        }
    }

}

// ============================================================================
ImageStyleAnalyzer::Gradient ImageStyleAnalyzer::SobelAt(int x, int y) const {
    // 3×3 Sobel 核
    auto lum = [this](int px, int py) -> float {
        uint32_t c = Sample(px, py);
        float r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
        return 0.299f * r + 0.587f * g + 0.114f * b;  // 感知亮度
    };

    float gx = -lum(x-1,y-1) + lum(x+1,y-1)
               -2*lum(x-1,y)  + 2*lum(x+1,y)
               -lum(x-1,y+1) + lum(x+1,y+1);

    float gy = -lum(x-1,y-1) - 2*lum(x,y-1) - lum(x+1,y-1)
               +lum(x-1,y+1) + 2*lum(x,y+1) + lum(x+1,y+1);

    Gradient grad;
    grad.mag = sqrtf(gx * gx + gy * gy);  // 0 ~ 1442
    grad.orient = atan2f(gy, gx);          // -PI ~ PI
    return grad;
}

// ============================================================================
void ImageStyleAnalyzer::AnalyzeColors(StyleProfile& p) {
    p.totalPixels = m_w * m_h;
    float sumH = 0, sumS = 0, sumL = 0;
    std::vector<float> allH, allS, allL;
    allH.reserve((std::min)(p.totalPixels, 50000));
    allS.reserve((std::min)(p.totalPixels, 50000));
    allL.reserve((std::min)(p.totalPixels, 50000));

    int step = (std::max)(1, (int)sqrtf((float)p.totalPixels / 50000.0f));

    for (int y = 0; y < m_h; y += step) {
        for (int x = 0; x < m_w; x += step) {
            uint32_t c = m_pixels[y * m_w + x];
            uint8_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;

            HSL hsl = RGBtoHSL(r, g, b);
            allH.push_back(hsl.h);
            allS.push_back(hsl.s);
            allL.push_back(hsl.l);
            sumH += hsl.h; sumS += hsl.s; sumL += hsl.l;

            // 直方图
            int hBin = (int)(hsl.h / 10.0f) % 36;
            int sBin = (std::min)(9, (int)(hsl.s * 10.0f));
            int lBin = (std::min)(9, (int)(hsl.l * 10.0f));
            p.hueHist[hBin]++;
            p.satHist[sBin]++;
            p.lightHist[lBin]++;
        }
    }

    int n = (int)allH.size();
    if (n == 0) return;

    p.avgHue = sumH / n;
    p.avgSat = sumS / n;
    p.avgLight = sumL / n;

    // 离散度
    float varH = 0, varS = 0, varL = 0;
    for (int i = 0; i < n; ++i) {
        float dH = allH[i] - p.avgHue;
        if (dH > 180) dH -= 360; if (dH < -180) dH += 360;
        varH += dH * dH;
        varS += (allS[i] - p.avgSat) * (allS[i] - p.avgSat);
        varL += (allL[i] - p.avgLight) * (allL[i] - p.avgLight);
    }
    p.hueSpread = sqrtf(varH / n);
    p.satSpread = sqrtf(varS / n);
    p.lightSpread = sqrtf(varL / n);

    // 主色调
    for (int i = 0; i < 36; ++i) {
        if (p.hueHist[i] > p.dominantHueCount) {
            p.dominantHueCount = p.hueHist[i];
            p.dominantHue = i * 10.0f + 5.0f;
        }
    }

    // 调色板丰富度 (着色像素占比 × 饱和度)
    int colored = 0;
    for (int i = 1; i < 10; ++i) colored += p.satHist[i];  // 排除 sat≈0
    float coloredRatio = (float)colored / (float)n;
    p.paletteDiversity = coloredRatio * p.avgSat;
}

// ============================================================================
void ImageStyleAnalyzer::AnalyzeEdges(StyleProfile& p) {
    float sumMag = 0, maxMag = 0;
    int edgePixels = 0;
    std::vector<float> allMags;
    allMags.reserve((std::min)(p.totalPixels, 50000));

    int step = (std::max)(1, (int)sqrtf((float)p.totalPixels / 30000.0f));
    float magThreshold = 30.0f;  // Sobel 梯度边缘判定阈值

    for (int y = 1; y < m_h - 1; y += step) {
        for (int x = 1; x < m_w - 1; x += step) {
            Gradient g = SobelAt(x, y);
            allMags.push_back(g.mag);
            sumMag += g.mag;
            if (g.mag > maxMag) maxMag = g.mag;

            if (g.mag > magThreshold) {
                edgePixels++;

                // 方向分桶 (8 方向: -PI→PI, 每 PI/4)
                float ori = g.orient;
                int oBin = (int)((ori + M_PI) / (M_PI / 4.0f));
                if (oBin < 0) oBin = 0; if (oBin > 7) oBin = 7;
                p.edgeOrientHist[oBin]++;

                // 强度分桶
                int mBin = (std::min)(9, (int)(g.mag / 160.0f));
                p.edgeMagHist[mBin]++;
            }
        }
    }

    int n = (int)allMags.size();
    if (n == 0) return;
    p.avgEdgeMag = sumMag / n;
    p.edgeMagMax = maxMag;
    p.edgeDensity = (float)edgePixels / (float)n;

    // 主方向
    float maxOrient = 0;
    int maxOBin = 0;
    for (int i = 0; i < 8; ++i) {
        if (p.edgeOrientHist[i] > maxOrient) {
            maxOrient = p.edgeOrientHist[i];
            maxOBin = i;
        }
    }
    p.dominantOrient = (maxOBin * 45.0f) - 180.0f;  // 转换为角度
    float totalOrient = 0;
    for (int i = 0; i < 8; ++i) totalOrient += p.edgeOrientHist[i];
    p.orientConcentration = (totalOrient > 0) ? maxOrient / totalOrient : 0;
}

// ============================================================================
void ImageStyleAnalyzer::AnalyzeTexture(StyleProfile& p) {
    float sumContrast = 0;
    float sumHighFreq = 0;
    int count = 0;

    int step = (std::max)(2, (int)sqrtf((float)p.totalPixels / 20000.0f));

    for (int y = step; y < m_h - step; y += step) {
        for (int x = step; x < m_w - step; x += step) {
            // 局部对比度: 5×5 窗口内亮度标准差
            float sumL = 0, sumL2 = 0;
            int wc = 0;
            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    uint32_t c = Sample(x + dx, y + dy);
                    float L = (0.299f * ((c>>16)&0xFF) + 0.587f * ((c>>8)&0xFF) + 0.114f * (c&0xFF));
                    sumL += L; sumL2 += L * L; wc++;
                }
            }
            float mean = sumL / wc;
            float var = sumL2 / wc - mean * mean;
            if (var < 0.0f) var = 0.0f;
            float stddev = sqrtf(var);
            sumContrast += stddev;
            count++;

            // 高频能量: Sobel 梯度作为高频 proxy
            Gradient g = SobelAt(x, y);
            if (g.mag > 80.0f) sumHighFreq += g.mag;
        }
    }

    if (count > 0) {
        p.localContrast = sumContrast / (count * 255.0f);
        p.highFreqEnergy = sumHighFreq / (count * 1442.0f);
        // 纹理复杂度 = 对比度 × 高频能量归一化组合
        p.textureComplexity = sqrtf(p.localContrast * p.highFreqEnergy) * 3.0f;
        if (p.textureComplexity > 1.0f) p.textureComplexity = 1.0f;
    }
}

// ============================================================================
void ImageStyleAnalyzer::AnalyzeComposition(StyleProfile& p) {
    float sumLX = 0, sumLY = 0, sumL = 0;
    float sumSX = 0, sumSY = 0;

    int step = (std::max)(1, (int)sqrtf((float)p.totalPixels / 10000.0f));

    for (int y = 0; y < m_h; y += step) {
        for (int x = 0; x < m_w; x += step) {
            uint32_t c = m_pixels[y * m_w + x];
            uint8_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
            float L = 0.299f * r + 0.587f * g + 0.114f * b;
            float xn = (float)x / m_w;
            float yn = (float)y / m_h;

            sumLX += L * xn; sumLY += L * yn; sumL += L;

            HSL hsl = RGBtoHSL(r, g, b);
            sumSX += hsl.s * xn; sumSY += hsl.s * yn;
        }
    }

    if (sumL > 0) {
        p.brightCenterX = sumLX / sumL;
        p.brightCenterY = sumLY / sumL;
    }
    // 色彩散布（简化：以中心为原点计算饱和加权半径）
    float totalSat = 0;
    for (int i = 0; i < 10; ++i) totalSat += p.satHist[i];
    if (totalSat > 0) {
        p.colorSpreadX = 0.3f + p.satSpread * 0.5f;
        p.colorSpreadY = 0.3f + p.satSpread * 0.5f;
    }
}

// ============================================================================
StyleProfile ImageStyleAnalyzer::Analyze(const wchar_t* imagePath) {
    StyleProfile p;
    LoadImage(imagePath);
    if (m_pixels.empty()) return p;

    p.width = m_w;
    p.height = m_h;
    p.totalPixels = m_w * m_h;

    wprintf(L"\n  Analyzing %d×%d (%d pixels)...\n", m_w, m_h, p.totalPixels);

    // 采样步长（大图自动降低采样密度）
    int maxSamples = 80000;
    int step = (std::max)(1, (int)sqrtf((float)p.totalPixels / (float)maxSamples));
    wprintf(L"  Sampling step: %d (~%d samples)\n", step, p.totalPixels / (step*step));

    AnalyzeColors(p);
    AnalyzeEdges(p);
    AnalyzeTexture(p);
    AnalyzeComposition(p);
    AnalyzeLines(p);

    return p;
}

// ============================================================================
// AnalyzeLines —— 多维度线条特征提取
//   几何形态：线型/曲率/长度/方向熵/端点
//   拓扑结构：连通性/节点度数/孤立线
//   空间统计：密度网格/角度直方图
//   风格：流畅度/对称性
// ============================================================================
void ImageStyleAnalyzer::AnalyzeLines(StyleProfile& p) {
    int step = (std::max)(1, (int)sqrtf((float)p.totalPixels / 30000.0f));
    int gw = (m_w + step - 1) / step;
    int gh = (m_h + step - 1) / step;
    if (gw < 5 || gh < 5) return;

    float hiThreshold = 80.0f;
    float loThreshold = 25.0f;

    // ── 1. 边缘检测 + 非极大值抑制 ──
    std::vector<float> magMap(gw * gh, 0.0f);
    std::vector<uint8_t> edgeMap(gw * gh, 0);
    std::vector<int> compId(gw * gh, -1);  // 连通分量 ID

    for (int gy = 1; gy < gh - 1; ++gy) {
        for (int gx = 1; gx < gw - 1; ++gx) {
            int px = (std::min)(m_w - 2, gx * step);
            int py = (std::min)(m_h - 2, gy * step);
            Gradient g = SobelAt(px, py);
            magMap[gy * gw + gx] = g.mag;
            if (g.mag < loThreshold) continue;

            float ad = fabsf(g.orient);
            int d = (ad < M_PI/8.0f || ad > 7.0f*M_PI/8.0f) ? 0
                  : (ad > 3.0f*M_PI/8.0f && ad < 5.0f*M_PI/8.0f) ? 2
                  : (g.orient > 0) ? 1 : 3;
            float n1 = 0, n2 = 0;
            switch (d) {
            case 0: n1=(gy>0&&gx>0)?magMap[gy*gw+(gx-1)]:0; n2=(gy>0&&gx<gw-1)?magMap[gy*gw+(gx+1)]:0; break;
            case 2: n1=(gy>1)?magMap[(gy-1)*gw+gx]:0; n2=(gy<gh-1)?magMap[(gy+1)*gw+gx]:0; break;
            case 1: n1=(gy>0&&gx>0)?magMap[(gy-1)*gw+(gx-1)]:0; n2=(gy<gh-1&&gx<gw-1)?magMap[(gy+1)*gw+(gx+1)]:0; break;
            case 3: n1=(gy<gh-1&&gx>0)?magMap[(gy+1)*gw+(gx-1)]:0; n2=(gy>0&&gx<gw-1)?magMap[(gy-1)*gw+(gx+1)]:0; break;
            }
            if (g.mag < n1 || g.mag < n2) continue;
            edgeMap[gy * gw + gx] = (g.mag > hiThreshold) ? 2 : 1;
        }
    }

    // ── 2. 连通分量追踪 (BFS)，每线段计算长度/曲率/流畅度/方向 ──
    std::vector<uint8_t> visited(gw * gh, 0);
    std::vector<int> lengths;  std::vector<float> curvatures, smoothnessVals, lineOrients;
    int minLen = 4;
    const int dx8[8] = {-1,0,1,-1,1,-1,0,1};
    const int dy8[8] = {-1,-1,-1,0,0,1,1,1};

    int compIdx = 0;
    for (int gy = 1; gy < gh - 1; ++gy) {
        for (int gx = 1; gx < gw - 1; ++gx) {
            int idx = gy * gw + gx;
            if (visited[idx] || edgeMap[idx] < 2) continue;

            std::vector<int> tx, ty;
            std::vector<int> queue;
            queue.push_back(idx); visited[idx] = 1;

            while (!queue.empty()) {
                int cur = queue.back(); queue.pop_back();
                int cx = cur % gw, cy = cur / gw;
                tx.push_back(cx); ty.push_back(cy);
                compId[cur] = compIdx;
                for (int k = 0; k < 8; ++k) {
                    int nx = cx + dx8[k], ny = cy + dy8[k];
                    if (nx<0||nx>=gw||ny<0||ny>=gh) continue;
                    int nidx = ny * gw + nx;
                    if (!visited[nidx] && edgeMap[nidx] >= 1) {
                        visited[nidx] = 1; queue.push_back(nidx);
                    }
                }
            }
            compIdx++;

            int len = (int)tx.size();
            if (len < minLen) continue;

            // 曲率 + 流畅度 (2 阶差分方差)
            float totalTurn = 0, sumJitter = 0;
            int turnCount = 0, jitterCount = 0;
            for (int k = 2; k < len - 2; k += 2) {
                float dx1=(float)(tx[k]-tx[k-2]), dy1=(float)(ty[k]-ty[k-2]);
                float dx2=(float)(tx[k+2]-tx[k]), dy2=(float)(ty[k+2]-ty[k]);
                float len1=sqrtf(dx1*dx1+dy1*dy1), len2=sqrtf(dx2*dx2+dy2*dy2);
                if (len1<0.5f||len2<0.5f) continue;
                float dot = (dx1*dx2+dy1*dy2)/(len1*len2);
                if (dot>1)dot=1; if (dot<-1)dot=-1;
                totalTurn += acosf(dot); turnCount++;
                // 2 阶差分 = 方向变化的变化
                if (k >= 4) {
                    float oldTurn = 0;
                    float odx1=(float)(tx[k-2]-tx[k-4]), ody1=(float)(ty[k-2]-ty[k-4]);
                    float odx2=(float)(tx[k]-tx[k-2]), ody2=(float)(ty[k]-ty[k-2]);
                    float olen1=sqrtf(odx1*odx1+ody1*ody1), olen2=sqrtf(odx2*odx2+ody2*ody2);
                    if (olen1>0.5f&&olen2>0.5f) {
                        float odot=(odx1*odx2+ody1*ody2)/(olen1*olen2);
                        if (odot>1)odot=1; if (odot<-1)odot=-1;
                        float oldAng = acosf(odot);
                        sumJitter += fabsf(acosf(dot) - oldAng);
                        jitterCount++;
                    }
                }
            }
            float curv = (turnCount>0) ? totalTurn/(float)turnCount : 0;
            float smooth = (jitterCount>0) ? 1.0f/(1.0f + sumJitter/(float)jitterCount) : 1.0f;

            // 线方向 (首尾连线方向)
            float ldx=(float)(tx.back()-tx[0]), ldy=(float)(ty.back()-ty[0]);
            float lori = atan2f(fabsf(ldy), fabsf(ldx));  // 映射到 [0, π/2]
            if (sqrtf(ldx*ldx+ldy*ldy) > 1.0f) lineOrients.push_back(lori);

            lengths.push_back(len); curvatures.push_back(curv);
            smoothnessVals.push_back(smooth);
            if (curv < 0.15f) p.straightLines++;
            else if (curv < 0.5f) p.curvedLines++;
            else p.jaggedLines++;

            // 闭合回路检测：首尾距离 < 3px
            float ex=tx.back()-tx[0], ey=ty.back()-ty[0];
            if (sqrtf(ex*ex+ey*ey) < 3.0f && len > 10) {
                p.closedLoopCount++;
                // 面积 (Shoelace) + 周长
                float area=0, perim=0;
                for (int k=0; k<len; ++k) {
                    int k2=(k+1)%len;
                    area+=(float)(tx[k]*ty[k2] - tx[k2]*ty[k]);
                    float ddx=tx[k2]-tx[k], ddy=ty[k2]-ty[k];
                    perim+=sqrtf(ddx*ddx+ddy*ddy);
                }
                area = fabsf(area)*0.5f;
                if (perim > 0) p.avgCircularity += (4.0f*M_PI*area)/(perim*perim);
                // 矩形度
                int minX=tx[0],maxX=tx[0],minY=ty[0],maxY=ty[0];
                for (int k=1; k<len; ++k) {
                    if (tx[k]<minX) minX=tx[k]; if (tx[k]>maxX) maxX=tx[k];
                    if (ty[k]<minY) minY=ty[k]; if (ty[k]>maxY) maxY=ty[k];
                }
                float bbox=(float)((maxX-minX+1)*(maxY-minY+1));
                if (bbox>0) p.avgRectangularity += area/bbox;
            }

            // 角度直方图 (0-180°, 每 10°)
            float deg = lori * 180.0f / M_PI;
            if (deg < 0) deg += 180; if (deg >= 180) deg -= 180;
            int aBin = (std::min)(17, (int)(deg / 10.0f));
            p.angleHist[aBin]++;
        }
    }

    p.lineCount = (int)lengths.size();
    if (p.lineCount == 0) return;

    // ── 3. 长度统计 ──
    float sumLen=0, sumCurv=0, sumSmooth=0;
    p.minLineLength = 1e9f; p.maxLineLength = 0;
    for (int i=0; i<p.lineCount; ++i) {
        float L=(float)lengths[i];
        sumLen+=L; p.totalLineLength+=L;
        if (L<p.minLineLength) p.minLineLength=L;
        if (L>p.maxLineLength) p.maxLineLength=L;
        sumCurv+=curvatures[i]; sumSmooth+=smoothnessVals[i];
    }
    p.avgLineLength = sumLen/(float)p.lineCount;
    p.avgCurvature = sumCurv/(float)p.lineCount;
    p.avgSmoothness = sumSmooth/(float)p.lineCount;
    if (p.avgSmoothness > 1) p.avgSmoothness = 1;

    // 长度标准差
    float varLen=0;
    for (int i=0; i<p.lineCount; ++i) {
        float d=(float)lengths[i]-p.avgLineLength;
        varLen+=d*d;
    }
    p.stddevLineLength = sqrtf(varLen/(float)p.lineCount);

    // 曲率标准差 + 最大值
    float varCurv=0; p.maxCurvature=0;
    for (int i=0; i<p.lineCount; ++i) {
        if (curvatures[i]>p.maxCurvature) p.maxCurvature=curvatures[i];
        float d=curvatures[i]-p.avgCurvature;
        varCurv+=d*d;
    }
    p.stddevCurvature = sqrtf(varCurv/(float)p.lineCount);

    // 方向熵
    float oriEnt=0;
    for (int i=0; i<18; ++i) {
        float prob=(float)p.angleHist[i]/(float)p.lineCount;
        if (prob>0) oriEnt -= prob * logf(prob);
    }
    p.orientationEntropy = oriEnt / logf(18.0f);  // 归一化

    p.lineDensity = (float)p.lineCount/(float)(gw*gh)*1000.0f;

    // ── 4. 节点检测：扫描边图中度 > 2 的交点 ──
    for (int gy=2; gy<gh-2; ++gy) {
        for (int gx=2; gx<gw-2; ++gx) {
            if (edgeMap[gy*gw+gx] < 1) continue;
            // 统计 8 邻域中从 0→1 的跳变次数 (穿越数)
            int nbr[8], trans=0;
            for (int k=0; k<8; ++k) nbr[k]=(edgeMap[(gy+dy8[k])*gw+(gx+dx8[k])]>=1)?1:0;
            for (int k=0; k<8; ++k) if (nbr[k]==0 && nbr[(k+1)&7]==1) trans++;
            if (trans >= 3) {
                if (trans == 3) p.junctionCount3++;
                else p.junctionCount4plus++;
            } else if (trans == 2) {
                p.junctionCount2++;
            }
        }
    }

    // 孤立线段比例：标端点（8 邻域仅 1 个邻居），两端都是端点的线为孤立线
    int isolated=0, totalWithEnds=0;
    for (int gy=1; gy<gh-1; ++gy) {
        for (int gx=1; gx<gw-1; ++gx) {
            if (edgeMap[gy*gw+gx] < 1) continue;
            int nb=0; for (int k=0;k<8;++k) if (edgeMap[(gy+dy8[k])*gw+(gx+dx8[k])]>=1) nb++;
            if (nb == 1) {  // 端点
                int cid = compId[gy*gw+gx];
                if (cid >= 0) {
                    // 检查该分量是否在 BFS 中仅有两个端点
                    // 简化：端点密度作为 proxy
                }
            }
        }
    }
    int totalJunctions = p.junctionCount2+p.junctionCount3+p.junctionCount4plus;
    p.isolatedLineRatio = (totalJunctions == 0 && p.lineCount > 0) ? 1.0f
                        : (float)(std::max)(0, p.lineCount-totalJunctions)/(float)p.lineCount;

    // ── 5. 空间密度网格 8×8 ──
    int cellW = gw/8, cellH = gh/8;
    if (cellW>0 && cellH>0) {
        for (int gy=0; gy<gh; ++gy) {
            int cy = (std::min)(7, gy/cellH);
            for (int gx=0; gx<gw; ++gx) {
                if (edgeMap[gy*gw+gx] >= 1) {
                    int cx = (std::min)(7, gx/cellW);
                    p.spatialDensityGrid[cy][cx] += 1.0f;
                }
            }
        }
        float cellArea = (float)(cellW*cellH);
        for (int y=0; y<8; ++y)
            for (int x=0; x<8; ++x)
                p.spatialDensityGrid[y][x] /= cellArea;
    }

    // ── 6. 对称性：比较边缘图与翻转图的重叠率 ──
    int overlapH=0, overlapV=0, totalEdge=0;
    for (int gy=0; gy<gh; ++gy) {
        for (int gx=0; gx<gw; ++gx) {
            if (edgeMap[gy*gw+gx] >= 1) {
                totalEdge++;
                int mx = gw-1-gx, my = gh-1-gy;
                if (edgeMap[gy*gw+mx] >= 1) overlapH++;
                if (edgeMap[my*gw+gx] >= 1) overlapV++;
            }
        }
    }
    if (totalEdge > 0) {
        p.hSymmetry = (float)overlapH/(float)totalEdge;
        p.vSymmetry = (float)overlapV/(float)totalEdge;
    }

    // 闭合回路统计平均
    if (p.closedLoopCount > 0) {
        p.avgCircularity /= (float)p.closedLoopCount;
        p.avgRectangularity /= (float)p.closedLoopCount;
    }

    // ── 7. 自相关周期检测 ──
    // 将边缘图投影到 x 和 y 轴，做 1D 自相关
    std::vector<float> projX(gw, 0), projY(gh, 0);
    for (int gy=0; gy<gh; ++gy)
        for (int gx=0; gx<gw; ++gx)
            if (edgeMap[gy*gw+gx]>=1) { projX[gx]++; projY[gy]++; }

    auto autocorPeak = [](const std::vector<float>& v) -> float {
        int n=(int)v.size(); if (n<8) return 0;
        float mean=0; for (float f:v) mean+=f; mean/=n;
        std::vector<float> ac(n/2,0);
        for (int lag=0; lag<n/2; ++lag) {
            float sum=0; int cnt=0;
            for (int i=0; i+lag<n; ++i) { sum+=(v[i]-mean)*(v[i+lag]-mean); cnt++; }
            ac[lag]=sum/cnt;
        }
        // 找第一个局部峰值（排除 lag=0）
        float maxPeak=0; int peakLag=0;
        for (int lag=2; lag<n/2-1; ++lag) {
            if (ac[lag]>ac[lag-1] && ac[lag]>ac[lag+1] && ac[lag]>maxPeak) {
                maxPeak=ac[lag]; peakLag=lag;
            }
        }
        if (maxPeak < ac[0]*0.1f) return 0;  // 峰值太弱，无周期
        return (float)peakLag;
    };

    p.periodicityX = autocorPeak(projX);
    p.periodicityY = autocorPeak(projY);
    // 规律性 = 周期性强弱归一化
    float px=(p.periodicityX>0)?1.0f:0, py=(p.periodicityY>0)?1.0f:0;
    p.regularityScore = (px+py)*0.5f;

    // 如果两个方向都有周期且接近，规律性更高
    if (p.periodicityX>0 && p.periodicityY>0) {
        float ratio = p.periodicityX/p.periodicityY;
        if (ratio>0.7f && ratio<1.4f) p.regularityScore = 0.8f;
    }
}

// ============================================================================
// JSON 序列化
// ============================================================================
static json ProfileToJSON(const StyleProfile& p, const wchar_t* path) {
    char fname[512]; WideCharToMultiByte(CP_UTF8,0,path,-1,fname,512,nullptr,nullptr);
    json j;
    j["file"] = fname;
    j["size"] = {{"width",p.width},{"height",p.height},{"pixels",p.totalPixels}};

    j["color"] = {
        {"dominantHue", p.dominantHue}, {"avgHue",p.avgHue},{"hueSpread",p.hueSpread},
        {"avgSaturation",p.avgSat},{"satSpread",p.satSpread},
        {"avgLightness",p.avgLight},{"lightSpread",p.lightSpread},
        {"paletteDiversity",p.paletteDiversity}
    };

    j["edge"] = {
        {"density",p.edgeDensity},{"avgMagnitude",p.avgEdgeMag},{"maxMagnitude",p.edgeMagMax},
        {"dominantOrient",p.dominantOrient},{"orientConcentration",p.orientConcentration}
    };

    j["texture"] = {
        {"localContrast",p.localContrast},{"highFreqEnergy",p.highFreqEnergy},
        {"textureComplexity",p.textureComplexity}
    };

    j["composition"] = {
        {"brightCenterX",p.brightCenterX},{"brightCenterY",p.brightCenterY}
    };

    if (p.lineCount > 0) {
        int total = p.straightLines+p.curvedLines+p.jaggedLines;
        j["line"] = {
            {"count",p.lineCount},{"density",p.lineDensity},
            {"avgLength",p.avgLineLength},{"minLength",p.minLineLength},
            {"maxLength",p.maxLineLength},{"totalLength",p.totalLineLength},
            {"stddevLength",p.stddevLineLength},
            {"avgCurvature",p.avgCurvature},{"maxCurvature",p.maxCurvature},
            {"stddevCurvature",p.stddevCurvature},
            {"straightPct",(total>0)?100.0f*p.straightLines/total:0},
            {"curvedPct",(total>0)?100.0f*p.curvedLines/total:0},
            {"jaggedPct",(total>0)?100.0f*p.jaggedLines/total:0},
            {"orientEntropy",p.orientationEntropy},
            {"junctions2way",p.junctionCount2},
            {"junctions3way",p.junctionCount3},
            {"junctions4plus",p.junctionCount4plus},
            {"isolatedRatio",p.isolatedLineRatio},
            {"avgSmoothness",p.avgSmoothness},
            {"hSymmetry",p.hSymmetry},{"vSymmetry",p.vSymmetry},
            {"closedLoops",p.closedLoopCount},
            {"avgCircularity",p.avgCircularity},
            {"avgRectangularity",p.avgRectangularity},
            {"periodicityX",p.periodicityX},{"periodicityY",p.periodicityY},
            {"regularityScore",p.regularityScore}
        };
    }
    return j;
}

// ============================================================================
// 报告输出
// ============================================================================
static void PrintReport(const StyleProfile& p, const wchar_t* path) {
    // Use fprintf + ASCII-safe formatting to avoid wide-char terminal issues
    #define R(fmt, ...) fprintf(stdout, fmt "\n", ##__VA_ARGS__)

    auto hueName = [](float h) -> const char* {
        if (h < 30 || h > 330)  return "Red/Orange";
        if (h < 60)             return "Yellow";
        if (h < 150)            return "Green";
        if (h < 210)            return "Cyan";
        if (h < 270)            return "Blue";
        if (h < 300)            return "Purple";
        return "Magenta";
    };
    auto satLabel = [](float s) -> const char* {
        if (s < 0.2f) return "Low-Sat/Muted";
        if (s < 0.5f) return "Mid-Sat";
        return "High-Sat/Vibrant";
    };
    auto lightLabel = [](float l) -> const char* {
        if (l < 0.3f) return "Dark/Low-Key";
        if (l < 0.7f) return "Mid-Tone";
        return "Bright/High-Key";
    };
    auto divLabel = [](float d) -> const char* {
        if (d < 0.1f) return "Monochromatic";
        if (d < 0.3f) return "Limited Palette";
        if (d < 0.5f) return "Moderate";
        return "Rich/Polychromatic";
    };
    auto edgeLabel = [](float e) -> const char* {
        if (e < 0.05f) return "Very Soft/Blurry";
        if (e < 0.15f) return "Soft";
        if (e < 0.30f) return "Moderate";
        if (e < 0.50f) return "Crisp/Sharp";
        return "Very Hard/Harsh";
    };
    auto orientLabel = [](float o) -> const char* {
        if (fabsf(o) < 15.0f)                    return "Horizontal";
        if (fabsf(o - 90) < 15.0f || fabsf(o + 90) < 15.0f) return "Vertical";
        if (fabsf(o - 45) < 15.0f || fabsf(o + 135) < 15.0f) return "Diagonal(/)";
        if (fabsf(o + 45) < 15.0f || fabsf(o - 135) < 15.0f) return "Anti-Diagonal(\\)";
        return "Omnidirectional";
    };
    auto concLabel = [](float c) -> const char* {
        if (c > 0.5f) return "Strongly Directed";
        if (c > 0.3f) return "Moderate";
        return "Isotropic";
    };
    auto contrastLabel = [](float c) -> const char* {
        if (c < 0.05f) return "Smooth/Flat";
        if (c < 0.15f) return "Gentle";
        if (c < 0.30f) return "Textured";
        return "Rough/Grainy";
    };
    auto texLabel = [](float t) -> const char* {
        if (t < 0.2f) return "Simple/Clean";
        if (t < 0.5f) return "Moderate";
        if (t < 0.8f) return "Complex/Detailed";
        return "Very Complex/Chaotic";
    };

    R("\n==============================================================");
    R("  IMAGE STYLE PROFILE");
    R("==============================================================");
    wprintf(L"  File: %s\n", path);
    R("  Size: %d x %d  (%d pixels)", p.width, p.height, p.totalPixels);
    R("");

    // -- Color --
    R("--- COLOR ---");
    R("  Dominant Hue    : %6.1f deg  (%s)", p.dominantHue, hueName(p.dominantHue));
    R("  Avg Hue         : %6.1f deg  (spread: %.1f deg)", p.avgHue, p.hueSpread);
    R("  Avg Saturation  :   %.3f     (spread: %.3f)  [%s]", p.avgSat, p.satSpread, satLabel(p.avgSat));
    R("  Avg Lightness   :   %.3f     (spread: %.3f)  [%s]", p.avgLight, p.lightSpread, lightLabel(p.avgLight));
    R("  Palette Diversity:  %.3f     [%s]", p.paletteDiversity, divLabel(p.paletteDiversity));

    // Hue histogram as ASCII bars
    fprintf(stdout, "  Hue Histogram (10deg/bin, 0=Red):\n  ");
    for (int i = 0; i < 36; ++i) {
        int bar = p.hueHist[i] * 40 / (std::max)(1, p.dominantHueCount);
        fprintf(stdout, "%c", " .-:=+*#@"[(std::min)(7, bar)]);
        if (i % 18 == 17) fprintf(stdout, "\n  ");
    }
    R("");

    // -- Edge --
    R("--- EDGE ---");
    R("  Edge Density    :   %.3f     [%s]", p.edgeDensity, edgeLabel(p.edgeDensity));
    R("  Avg Edge Magnitude: %7.1f  (Max: %.0f)", p.avgEdgeMag, p.edgeMagMax);
    R("  Dominant Orient : %6.0f deg  [%s]", p.dominantOrient, orientLabel(p.dominantOrient));
    R("  Orient Concentr :   %.3f     [%s]", p.orientConcentration, concLabel(p.orientConcentration));

    fprintf(stdout, "  Edge Orient Histogram (8 dirs, -180 to +180):\n  ");
    float maxO = 1;
    for (int i = 0; i < 8; ++i) if (p.edgeOrientHist[i] > maxO) maxO = p.edgeOrientHist[i];
    for (int i = 0; i < 8; ++i) {
        int bar = (int)(p.edgeOrientHist[i] * 20.0f / maxO);
        fprintf(stdout, "%c", " .-:=+*#@"[(std::min)(7, bar)]);
    }
    R("");

    // -- Texture --
    R("--- TEXTURE ---");
    R("  Local Contrast  :   %.3f     [%s]", p.localContrast, contrastLabel(p.localContrast));
    R("  High-Freq Energy:  %.4f", p.highFreqEnergy);
    R("  Texture Complexity: %.3f     [%s]", p.textureComplexity, texLabel(p.textureComplexity));

    // -- Composition --
    R("--- COMPOSITION ---");
    R("  Brightness Center: (%.3f, %.3f)", p.brightCenterX, p.brightCenterY);
    const char* cxPos = (p.brightCenterX < 0.4f) ? "Left" :
                        (p.brightCenterX > 0.6f) ? "Right" : "Centered";
    const char* cyPos = (p.brightCenterY < 0.4f) ? "Top" :
                        (p.brightCenterY > 0.6f) ? "Bottom" : "Centered";
    R("  Position         : %s / %s", cxPos, cyPos);

    // -- Line --
    R("--- LINE: Geometry ---");
    if (p.lineCount > 0) {
        auto lineStyle = [](float curv) -> const char* {
            if (curv < 0.15f) return "Straight/Geometric";
            if (curv < 0.5f)  return "Curved/Organic";
            return "Jagged/Chaotic";
        };
        int total = p.straightLines + p.curvedLines + p.jaggedLines;
        float sPct = (total>0) ? 100.0f*p.straightLines/total : 0;
        float cPct = (total>0) ? 100.0f*p.curvedLines/total : 0;
        float jPct = (total>0) ? 100.0f*p.jaggedLines/total : 0;

        R("  Line Count      : %d", p.lineCount);
        R("  Line Density    : %.3f lines/kpx", p.lineDensity);
        R("  Avg Length      : %.1f px  (min: %.0f  max: %.0f  stddev: %.1f)",
            p.avgLineLength, p.minLineLength, p.maxLineLength, p.stddevLineLength);
        R("  Total Length    : %.0f px", p.totalLineLength);
        R("  Avg Curvature   : %.3f  (max: %.3f  stddev: %.3f)  [%s]",
            p.avgCurvature, p.maxCurvature, p.stddevCurvature, lineStyle(p.avgCurvature));
        R("  Line Types      : Straight:%.0f%%  Curved:%.0f%%  Jagged:%.0f%%", sPct, cPct, jPct);
        R("  Orient Entropy  : %.3f  %s", p.orientationEntropy,
            p.orientationEntropy < 0.3f ? "[Highly Directed]" :
            p.orientationEntropy > 0.7f ? "[Uniform/Isotropic]" : "[Moderate]");

        fprintf(stdout, "  Angle Histogram (0-180, 10deg/bin):\n  ");
        int maxA=1; for (int i=0;i<18;++i) if(p.angleHist[i]>maxA) maxA=p.angleHist[i];
        for (int i=0;i<18;++i) {
            int bar = p.angleHist[i]*20/(std::max)(1,maxA);
            fprintf(stdout,"%c"," .-:=+*#@"[(std::min)(7,bar)]);
            if (i==8) fprintf(stdout,"\n  ");
        }

        R("\n--- LINE: Topology ---");
        R("  Junctions       : 2-way:%d  3-way:%d  4+way:%d",
            p.junctionCount2, p.junctionCount3, p.junctionCount4plus);
        R("  Isolated Ratio  : %.3f %s", p.isolatedLineRatio,
            p.isolatedLineRatio > 0.7f ? "[Mostly isolated strokes]" :
            p.isolatedLineRatio > 0.3f ? "[Mixed]" : "[Highly connected network]");

        R("--- LINE: Spatial Density (8x8 grid, row-major) ---");
        fprintf(stdout, "  ");
        for (int y=0; y<8; ++y) {
            for (int x=0; x<8; ++x) {
                float d = p.spatialDensityGrid[y][x];
                fprintf(stdout, "%c", " .-:=+*#@"[(std::min)(7, (int)(d*20))]);
            }
            if (y<7) fprintf(stdout, "\n  ");
        }

        R("\n--- LINE: Style ---");
        R("  Smoothness      : %.3f  %s", p.avgSmoothness,
            p.avgSmoothness > 0.7f ? "[Smooth/Fluid]" :
            p.avgSmoothness > 0.4f ? "[Moderate]" : "[Jittery/Rough]");
        R("  H-Symmetry      : %.3f", p.hSymmetry);
        R("  V-Symmetry      : %.3f", p.vSymmetry);

        if (p.closedLoopCount > 0) {
            R("--- LINE: Closed Loops ---");
            R("  Loop Count      : %d", p.closedLoopCount);
            R("  Avg Circularity : %.3f  %s", p.avgCircularity,
                p.avgCircularity > 0.8f ? "[Near-circular]" :
                p.avgCircularity > 0.5f ? "[Elliptical]" : "[Irregular]");
            R("  Avg Rectangularity: %.3f  %s", p.avgRectangularity,
                p.avgRectangularity > 0.8f ? "[Box-like]" :
                p.avgRectangularity > 0.5f ? "[Rounded-rect]" : "[Non-rectangular]");
        }
        R("--- LINE: Periodicity ---");
        R("  Period X        : %.0f px  %s", p.periodicityX,
            p.periodicityX > 0 ? "" : "(no period detected)");
        R("  Period Y        : %.0f px  %s", p.periodicityY,
            p.periodicityY > 0 ? "" : "(no period detected)");
        R("  Regularity      : %.3f  %s", p.regularityScore,
            p.regularityScore > 0.6f ? "[Regular/Grid-like]" :
            p.regularityScore > 0.3f ? "[Partially regular]" : "[Random/Aperiodic]");
    } else {
        R("  (no continuous lines detected)");
    }

    // -- Style Sketch --
    R("--- STYLE SKETCH ---");
    std::string sketch = "  ";
    if (p.avgSat < 0.15f) sketch += "Near-monochrome, ";
    else if (p.paletteDiversity < 0.2f) sketch += "Tight palette, ";
    else if (p.paletteDiversity > 0.5f) sketch += "Rich polychrome, ";
    else sketch += "Moderate palette, ";

    if (p.avgLight < 0.3f) sketch += "Low-key, ";
    else if (p.avgLight > 0.7f) sketch += "High-key, ";
    else sketch += "Mid-tone, ";

    if (p.edgeDensity < 0.08f) sketch += "soft/blurry edges, ";
    else if (p.edgeDensity > 0.35f) sketch += "hard/crisp edges, ";
    else sketch += "moderate edges, ";

    if (p.textureComplexity < 0.15f) sketch += "clean/smooth, ";
    else if (p.textureComplexity > 0.6f) sketch += "dense texture, ";
    else sketch += "light texture, ";

    if (p.lineCount > 0) {
        if (p.avgCurvature < 0.15f) sketch += "straight/geometric lines.";
        else if (p.avgCurvature < 0.5f) sketch += "curved/organic lines.";
        else sketch += "jagged/chaotic lines.";
    } else {
        sketch += "no clear lines.";
    }

    R("%s", sketch.c_str());
    R("\n==============================================================\n");
    #undef R
}

// ============================================================================
// 多图汇总 —— 对多张同风格图的 StyleProfile 取平均，过滤单张偶然特征
// ============================================================================
static StyleProfile MergeProfiles(const std::vector<StyleProfile>& profiles) {
    StyleProfile m;
    int N = (int)profiles.size();
    if (N == 0) return m;

    // 标量取平均
    for (auto& p : profiles) {
        m.width += p.width; m.height += p.height; m.totalPixels += p.totalPixels;

        m.avgHue     += p.avgHue;
        m.avgSat     += p.avgSat;
        m.avgLight   += p.avgLight;
        m.hueSpread  += p.hueSpread;
        m.satSpread  += p.satSpread;
        m.lightSpread += p.lightSpread;
        m.paletteDiversity += p.paletteDiversity;

        m.edgeDensity      += p.edgeDensity;
        m.avgEdgeMag       += p.avgEdgeMag;
        m.edgeMagMax       += p.edgeMagMax;
        m.orientConcentration += p.orientConcentration;

        m.localContrast    += p.localContrast;
        m.highFreqEnergy   += p.highFreqEnergy;
        m.textureComplexity += p.textureComplexity;

        m.brightCenterX += p.brightCenterX;
        m.brightCenterY += p.brightCenterY;
        m.colorSpreadX  += p.colorSpreadX;
        m.colorSpreadY  += p.colorSpreadY;

        m.lineCount    += p.lineCount;
        m.lineDensity  += p.lineDensity;
        m.avgLineLength += p.avgLineLength;
        m.minLineLength += p.minLineLength;
        m.maxLineLength += p.maxLineLength;
        m.totalLineLength += p.totalLineLength;
        m.stddevLineLength += p.stddevLineLength;
        m.avgCurvature += p.avgCurvature;
        m.maxCurvature += p.maxCurvature;
        m.stddevCurvature += p.stddevCurvature;
        m.straightLines += p.straightLines;
        m.curvedLines   += p.curvedLines;
        m.jaggedLines   += p.jaggedLines;
        m.orientationEntropy += p.orientationEntropy;

        m.junctionCount2 += p.junctionCount2;
        m.junctionCount3 += p.junctionCount3;
        m.junctionCount4plus += p.junctionCount4plus;
        m.isolatedLineRatio += p.isolatedLineRatio;

        for (int y=0; y<8; ++y) for (int x=0; x<8; ++x)
            m.spatialDensityGrid[y][x] += p.spatialDensityGrid[y][x];
        for (int i=0; i<18; ++i) m.angleHist[i] += p.angleHist[i];

        m.avgSmoothness += p.avgSmoothness;
        m.hSymmetry += p.hSymmetry;
        m.vSymmetry += p.vSymmetry;
        m.closedLoopCount += p.closedLoopCount;
        m.avgCircularity += p.avgCircularity;
        m.avgRectangularity += p.avgRectangularity;
        m.periodicityX += p.periodicityX;
        m.periodicityY += p.periodicityY;
        m.regularityScore += p.regularityScore;

        // 直方图累加（汇总后重新找主值）
        for (int i = 0; i < 36; ++i) m.hueHist[i]    += p.hueHist[i];
        for (int i = 0; i < 10; ++i) m.satHist[i]    += p.satHist[i];
        for (int i = 0; i < 10; ++i) m.lightHist[i]  += p.lightHist[i];
        for (int i = 0; i <  8; ++i) m.edgeOrientHist[i] += p.edgeOrientHist[i];
        for (int i = 0; i < 10; ++i) m.edgeMagHist[i]  += p.edgeMagHist[i];
    }

    float invN = 1.0f / N;
    m.width /= N; m.height /= N; m.totalPixels /= N;

    m.avgHue /= N; m.avgSat /= N; m.avgLight /= N;
    m.hueSpread /= N; m.satSpread /= N; m.lightSpread /= N;
    m.paletteDiversity /= N;

    m.edgeDensity /= N; m.avgEdgeMag /= N; m.edgeMagMax /= N;
    m.orientConcentration /= N;

    m.localContrast /= N; m.highFreqEnergy /= N; m.textureComplexity /= N;

    m.brightCenterX /= N; m.brightCenterY /= N;
    m.colorSpreadX /= N; m.colorSpreadY /= N;

    m.lineDensity /= N; m.avgLineLength /= N; m.minLineLength /= N;
    m.maxLineLength /= N; m.totalLineLength /= N; m.stddevLineLength /= N;
    m.avgCurvature /= N; m.maxCurvature /= N; m.stddevCurvature /= N;
    m.orientationEntropy /= N;
    m.junctionCount2 /= N; m.junctionCount3 /= N; m.junctionCount4plus /= N;
    m.isolatedLineRatio /= N;
    for (int y=0; y<8; ++y) for (int x=0; x<8; ++x)
        m.spatialDensityGrid[y][x] /= N;
    m.avgSmoothness /= N; m.hSymmetry /= N; m.vSymmetry /= N;
    m.avgCircularity /= N; m.avgRectangularity /= N;
    m.periodicityX /= N; m.periodicityY /= N; m.regularityScore /= N;

    // 从累加直方图重新推导主色调
    m.dominantHueCount = 0;
    for (int i = 0; i < 36; ++i) {
        if (m.hueHist[i] > m.dominantHueCount) {
            m.dominantHueCount = m.hueHist[i];
            m.dominantHue = i * 10.0f + 5.0f;
        }
    }

    // 从累加直方图重新推导主边缘方向
    float maxOrient = 0; int maxOBin = 0;
    for (int i = 0; i < 8; ++i) {
        if (m.edgeOrientHist[i] > maxOrient) {
            maxOrient = m.edgeOrientHist[i]; maxOBin = i;
        }
    }
    m.dominantOrient = (maxOBin * 45.0f) - 180.0f;
    float totalOrient = 0;
    for (int i = 0; i < 8; ++i) totalOrient += m.edgeOrientHist[i];
    m.orientConcentration = (totalOrient > 0) ? maxOrient / totalOrient : 0;

    return m;
}

// ============================================================================
// 通配符展开：将 "path\*.ext" 展开为文件列表
// ============================================================================
static std::vector<std::wstring> ExpandWildcard(const wchar_t* pattern) {
    std::vector<std::wstring> files;
    // 只有包含通配符才展开，否则直接当作普通路径
    if (!wcschr(pattern, L'*') && !wcschr(pattern, L'?')) {
        files.push_back(pattern);
        return files;
    }
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return files;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            // 重建完整路径
            std::wstring dir(pattern);
            size_t bs = dir.find_last_of(L"\\/");
            if (bs != std::wstring::npos)
                files.push_back(dir.substr(0, bs + 1) + fd.cFileName);
            else
                files.push_back(fd.cFileName);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return files;
}

// ============================================================================
int wmain(int argc, wchar_t* argv[]) {
    if (argc < 2) {
        wprintf(L"Image Style Analyzer — 参考图风格分析器\n\n");
        wprintf(L"Usage:\n");
        wprintf(L"  单张:  ImageStyleAnalyzer.exe <图片> [--json]\n");
        wprintf(L"  批量:  ImageStyleAnalyzer.exe <图1> <图2> ... [--json]\n");
        wprintf(L"  通配:  ImageStyleAnalyzer.exe \"*.png\" [--json]\n");
        wprintf(L"\n  --json  输出结构化 JSON 代替 ASCII 报告\n");
        wprintf(L"\nDimensions: Color / Edge / Texture / Composition / Line / Periodicity\n");
        return 1;
    }

    // 解析 --json 标志
    bool useJSON = false;
    std::vector<std::wstring> rawFiles;
    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"--json") == 0) useJSON = true;
        else rawFiles.push_back(argv[i]);
    }

    // GDI+ 进程级初始化
    GdiplusStartupInput gdiSI;
    gdiSI.GdiplusVersion = 1;
    ULONG_PTR gdiToken;
    if (GdiplusStartup(&gdiToken, &gdiSI, nullptr) != Ok) {
        wprintf(L"ERROR: Failed to initialize GDI+.\n");
        return 1;
    }

    // 收集所有输入文件（展开通配符）
    std::vector<std::wstring> files;
    for (auto& f : rawFiles)
        for (auto& ff : ExpandWildcard(f.c_str()))
            files.push_back(ff);

    if (files.empty()) {
        wprintf(L"ERROR: No valid image files found.\n");
        return 1;
    }

    // 检查文件存在性
    std::vector<std::wstring> validFiles;
    for (auto& f : files) {
        if (GetFileAttributesW(f.c_str()) != INVALID_FILE_ATTRIBUTES)
            validFiles.push_back(f);
        else
            wprintf(L"WARNING: File not found, skipped: %s\n", f.c_str());
    }
    if (validFiles.empty()) {
        wprintf(L"ERROR: None of the specified files exist.\n");
        return 1;
    }

    bool batch = (validFiles.size() > 1);
    if (batch)
        wprintf(L"\n*** BATCH MODE: %zu images ***\n", validFiles.size());

    // 逐张分析
    std::vector<StyleProfile> profiles;
    ImageStyleAnalyzer analyzer;
    for (size_t i = 0; i < validFiles.size(); ++i) {
        if (!useJSON && batch) wprintf(L"\n── [%zu/%zu] ───────────────────────────────\n", i + 1, validFiles.size());
        auto p = analyzer.Analyze(validFiles[i].c_str());
        if (p.totalPixels > 0) {
            if (!useJSON) PrintReport(p, validFiles[i].c_str());
            profiles.push_back(p);
        }
    }

    if (useJSON) {
        // JSON 输出
        json out;
        out["imageCount"] = profiles.size();
        json arr = json::array();
        for (size_t i = 0; i < profiles.size(); ++i)
            arr.push_back(ProfileToJSON(profiles[i], validFiles[i].c_str()));
        out["images"] = arr;

        if (batch && profiles.size() >= 2) {
            auto merged = MergeProfiles(profiles);
            out["batchSummary"] = ProfileToJSON(merged, L"(batch average)");

            // 简单聚类 (k=2)：基于曲率+方向熵+边缘密度
            if (profiles.size() >= 4) {
                std::vector<float> feat(profiles.size());
                for (size_t i=0; i<profiles.size(); ++i)
                    feat[i] = profiles[i].avgCurvature*0.5f + profiles[i].orientationEntropy*0.3f
                            + profiles[i].edgeDensity*0.2f;
                float fmin=feat[0], fmax=feat[0];
                for (float f:feat) { if(f<fmin)fmin=f; if(f>fmax)fmax=f; }
                float thresh = (fmin+fmax)*0.5f;
                json clusters = json::array();
                for (size_t i=0; i<profiles.size(); ++i) {
                    int cid = (feat[i] > thresh) ? 1 : 0;
                    bool found=false;
                    for (auto& c:clusters) { if(c["id"]==cid) { c["indices"].push_back(i); found=true; break; } }
                    if (!found) {
                        json c; c["id"]=cid; c["indices"]=json::array(); c["indices"].push_back(i);
                        clusters.push_back(c);
                    }
                }
                out["clusters"] = clusters;
            }
        }
        printf("%s\n", out.dump(2).c_str());
    } else if (batch && profiles.size() >= 2) {
        auto merged = MergeProfiles(profiles);
        wprintf(L"\n");
        wprintf(L"╔══════════════════════════════════════════════════════╗\n");
        wprintf(L"║     ★ BATCH SUMMARY — %zu 张图片的汇总平均报告 ★     ║\n", profiles.size());
        wprintf(L"║     多张同风格图的统计共识——消除单张偶然噪声        ║\n");
        PrintReport(merged, L"(batch average)");
    }

    GdiplusShutdown(gdiToken);
    return 0;
}
