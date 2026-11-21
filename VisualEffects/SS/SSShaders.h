// ============================================================================
// SSShaders
//
// Involute_1 台阶板着色器。离线台：_hittest/ss_render_test.cpp。
//
// 【为什么单独一个头】离线台要的是【实际跑的那一份】原文。以前靠 ExtractRaw()
// 从 main.cpp 里抓文本，台子因此依赖 main.cpp 的排版；改成 include 之后两边
// 拿到的是同一个变量，抄错/漏改这一类问题就不存在了。
//
// 【改了这个文件要重编离线台】—— 着色器文本是编进 exe 的。
// ============================================================================
#pragma once

static const char* kSSVS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aCell;  // 台阶：格子的归一化中心 (cellU,cellV)；板体 = (-1,-1)
layout(location = 2) in vec3 aNrm;   // 【烘进顶点的面法线】—— 见 SSGeom.h 里那段说明，
                                     // 小三角形上用 dFdx/dFdy 推法线会把渲染变成不确定的

uniform float uCamYaw, uCamPitch, uCamDist;   // 球面相机（距离恒定）
uniform float uFocal;
uniform vec2  uResolution;
uniform float uExitTime;
uniform int   uPass;        // 0=背景 1=几何+编码 2=亮部 3=模糊 4=合成
// 鼓点弹跳
uniform float uHitAge;      // 距上次 hit 的秒数（CPU 累加；没 hit 过给一个大数）
uniform float uHitIndex;    // = g_beatCount，当随机种子（同一次 hit 结果可复现）
// 【上一拍】的序号与间隔（秒）。用来把上一拍的余波一起算进来 —— 见下面"两拍叠加"那段。
uniform float uPrevHitIdx;
uniform float uPrevGap;
uniform float uCellNX, uCellNY;   // = SS_NX / SS_NY，由 SSGeom.h 唯一定义
uniform float uBounceAmp, uBounceSpread;
// 无限延伸（整条楼梯沿 y 流动 + 回绕）
uniform float uPhase;                       // 流动相位（CPU 积分，只增不减）
uniform float uStepD, uStepR;               // 每级踏面进深 / 立面高度
uniform float uStepLen;                     // 梯段总长 = 级数 × 踏面进深
uniform float uYMin;                        // 梯段低端的 y
uniform float uRepShift;                    // 【本份拷贝】沿 y 的整体平移（错开整数个梯段长）
// ===========================================================================
// 【曲变直】—— 这个映射的推导写在 RenderInvolute_1 上面的长注释里，这里只列结论：
//   宽度 x  →  断面上的弧长位置 u      （一条直线段  <->  渐开线叶形的两臂）
//   上升 y  →  绕 z 轴的转角（扭转）    （由【连续的 z】给，不是由台阶序号给！）
//   长度 z  →  管轴坐标
// uMix = 0 时逐点还原成直楼梯；uMix = 1 时逐点等于 Involute 的曲面。
// ⚠【扭转必须由连续的 z 给】。我第一版是把每级的"上升量"当转角，结果每一级台阶是一块
//   平板（同级的 30 个顶点转角相同），卷起来是一叠「风扇叶片」，不是管子 —— 因为扭转
//   全部集中在立面那 0.23 的缝里，而 Involute 的扭转是【沿 z 均匀分布】的。
//   用连续的 z 之后两者才对得上（每 6.0 转一整圈，与 Involute 的 VX_TWIST 同率）。
uniform float uMix;      // 0 = 直楼梯，1 = 卷成 Involute 的管
uniform float uLeafR;    // 渐开线基圆半径（= VX_R），卷起来的断面由它定
uniform float uPeriod;   // 扭转的螺距（= VX_PERIOD_LEN），每走这么长转一整圈
// 【推进量的对齐】= Involute 的相位 − 本效果已经自己走掉的流动相位（见 RenderInvolute_1）。
// 对螺旋来说"沿轴平移 Δ"和"整体转 Δ·2π/周期"是同一件事，所以把它加进扭转里，
// 卷起来的那条管子的推进速率就和 Involute 完全一致 —— 交叉淡化时才不会叠出重影。
uniform float uAdvance;
// Involute 的【第二形态】分离距离（= VXFormDisp()·kVXFormDist）。同样是"过渡时和 Involute 对齐"：
// 它在鼓点沿径向整个炸开一次，这边不跟着分就对不上。uMix=0 时不生效。
uniform float uRadialPush;
// 板半宽（= SS_HALF_W）。卷曲要用它反推断面上的弧长位置，所以 VS 这边也要一份 ——
// 同名 uniform 在 VS/FS 之间是共享的，不会多占一个槽。
uniform float uHalfW;

out vec3  vWorld;   // 已旋转的坐标（光照用）
out vec3  vNrm;     // 已旋转的面法线
out vec2  vLocal;   // 【未旋转】的板面坐标（外缘微光要用它定位在板的哪一边）
out vec2  vCell;
out vec2  vUV;
out float vBounce;  // 本级台阶当前的弹跳位移（归一化到 ±1）—— 见下面"为什么要传它"
out float vStep;    // 本级台阶的【基准高低】（0 = 低的一档，1 = 高的一档）
out float vFog;     // 卷起来之后的远近淡出（平放时恒为 1，见下面）
// 【材质过渡用】这一片元对应到 Involute 曲面上的纹理坐标（vUV 的等价量）。
// 见片元着色器里"材质随扭曲一起过渡"那段：过渡要"边扭曲边过渡"，
// 就必须把 Involute 的材质算在【同一张几何】上，而不是叠两张图（那会出重影）。
out float vVxU;
out float vVxV;

// 弹跳包络用的两个常数（阻尼振荡）。decay 越大停得越快，freq 越大弹得越急。
const float SS_BOUNCE_DECAY = 5.5;
const float SS_BOUNCE_FREQ  = 13.0;

/// 标准 hash12：稳定、无 NaN 风险（不要用 sin(dot(...)) 那种，精度差会出条纹）
float ssHash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    // ---- 全屏那几趟：g_vao 给的是 NDC xy + uv，直通 ----
    if (uPass != 1) {
        gl_Position = vec4(aPos.xy, 0.0, 1.0);
        vUV = aCell;                 // 全屏趟里 location 1 就是 quad 的 uv
        vWorld = vec3(aCell, 0.0);
        vNrm = vec3(0.0, 0.0, 1.0);
        vLocal = aCell;
        vCell = vec2(-1.0);
        vBounce = 0.0;
        vStep = 0.0;
        vFog = 1.0;
        vVxU = aCell.x;
        vVxV = aCell.y;
        return;
    }

    vec3 p = aPos;

    // ---- 鼓点：每一级台阶各自独立、上下随机地弹 ----
    // 不建逐实例缓冲（loader 里没有 glBufferSubData），只靠 (格子号, hit序号) 哈希出来。
    // 板体用 aCell.x = -1 当哨兵，不参与弹跳。
    if (aCell.x >= 0.0) {
        float i = floor(aCell.x * uCellNX);

        // ---- 无限延伸：整条楼梯沿 y 流动，走到头的那一级从另一头接回来 ----
        // 【整级一起搬，绝不能让一级的 30 个顶点各自取模】——
        // 跨在回绕边界上的那一级会被撕成两半：一半在这头、一半在那头，
        // 画面上就是一条贯穿整个梯段的细片扫过去（Involute 早期踩过同样的坑）。
        // 这里位移量只由【级号 i】决定，所以同一级的 30 个顶点算出同一个值 ✓ 刚性 ✓。
        // 沿 z（= 卷起来之后的管轴方向）流动并回绕。
        // 新映射下"上升"在 y 上、和 z 无关，所以不再需要以前那条"高度跟着位置走"的补偿。
        // 【取模前必须先减掉基准】——baseZ 有一半是负数，而 mod(负数, L) 不是原样返回：
        // 它会把 -2 变成 L-2，也就是把那一级整整推移一个梯段长。
        // 结果是【一半台阶被挪走】→ 楼梯从中间断成几截（用户的原话："还是分段的"）。
        // 先搬到 [0, L) 再取模，取模的结果才真的是"绕回"。
        float baseZ  = uYMin + i * uStepD;
        float rel    = baseZ - uYMin;                       // 落在 [0, uStepLen)
        rel = mod(rel + uPhase, uStepLen);
        float dz = (uYMin + rel) - baseZ;                   // 本级沿 z 的位移（含回绕那一下）
        p.z += dz;
        // 【上升必须跟着一起搬】。楼梯的上升是沿梯段【累积】的（y = (z−zMin)·R/D），
        // 沿 z 位移却不补 y 的话：没回绕时看着还行（整条梯沿自身斜面平移），
        // 一旦某一级回绕（z 跳 −30）而 y 留在原地，那一级就跑到了【梯段底端的高处】——
        // 与旁边的台阶整整错开一个高度，用户的原话就是"新生成的台阶与原来的错位"。
        p.y += dz * (uStepR / uStepD);

        // ================= 【两拍叠加】=================
        // 只算"当前这一拍"的话，鼓点那一瞬间 age 归零 → env = exp(0)·|sin(0)| = 0 ——
        // 【所有台阶被同时拍回平面】，然后各自按新方向重新弹。包络本身没有错，
        // 错在"上一拍的余波没地方放"：实测鼓点间隔 0.3~0.4 秒时上一拍还剩 10~13% 的位移，
        // amp=4.00 下就是 0.4~0.53 世界单位（2~3 级高），画面上是"啪"地弹回原点。
        // （这个洞一直在，只是 amp 小的时候残留只有 0.04、看不见；幅度一放大它就露出来了。）
        // 现在把【上一拍】也当成一项加起来：新的一拍起步时它那一项是 0，
        // 但上一拍那一项正好接着衰减，合起来是连续的 —— 台阶从当前位移继续被推。
        // 上一拍的序号和间隔都由 CPU 给（uPrevHitIdx / uPrevGap）：不能想当然用 uHitIndex−1，
        // 因为漏拍的节拍会让"真正的上一拍"不是上一拍。
        float k = floor(aCell.y * uCellNY);
        // 这一拍
        float h1 = ssHash(vec2(i, k) + uHitIndex * 17.13);
        float h2 = ssHash(vec2(i, k) + uHitIndex * 53.71 + 7.7);
        float sgn   = (h1 < 0.35) ? -1.0 : 1.0;              // 三成下沉、七成弹起
        float amp   = uBounceAmp * (0.25 + 0.75 * h2);       // 幅度也随机
        float delay = h1 * uBounceSpread;                    // 错开起跳，不是整块板一起弹
        // 上一拍
        float p1 = ssHash(vec2(i, k) + uPrevHitIdx * 17.13);
        float p2 = ssHash(vec2(i, k) + uPrevHitIdx * 53.71 + 7.7);
        float sgnP   = (p1 < 0.35) ? -1.0 : 1.0;
        float ampP   = uBounceAmp * (0.25 + 0.75 * p2);
        float delayP = p1 * uBounceSpread;
        // 【必须夹下界】：换效果时 uHitAge 会跳到很大，台阶带着旧时间轴就会算出
        // 大负数年龄，exp(-age*D) 直接爆炸把整块板甩飞（C3 踩过这个坑）。
        // 【上下界都要夹】下界见上面那段注释（负年龄会让 exp 爆炸）；
        // 上界是为了避开大参数：exp(-5494) 会下溢成 0/denormal、sin(12987) 的
        // 参数归约精度各家驱动不一样 —— 实测这会带来【进程之间 6% 像素、约 17% 亮度】
        // 的随机差异。夹到 6 秒之后 exp(-33) 已是 4.6e-15，效果上就是"早就停了"，
        // 但参数是规规矩矩的小数，结果才稳定可复现。
        // 上一拍的年龄 = 这一拍的年龄 + 两拍之间的间隔（间隔由 CPU 记录，是恒等式不是近似）
        float age  = clamp(uHitAge - delay,  0.0, 6.0);
        float ageP = clamp(uHitAge + uPrevGap - delayP, 0.0, 6.0);
        float env  = exp(-age  * SS_BOUNCE_DECAY) * abs(sin(age  * SS_BOUNCE_FREQ));
        float envP = exp(-ageP * SS_BOUNCE_DECAY) * abs(sin(ageP * SS_BOUNCE_FREQ));
        // 向下的幅度只取 45%：台阶下沉太多会陷进下一级里（本级立面高 SS_STEP_R = 0.159）
        if (sgn  < 0.0) amp  *= 0.45;
        if (sgnP < 0.0) ampP *= 0.45;
        // 【弹跳是垂直的，所以加在 y 上 —— 加在 z 上是错的】。
        // 这里原来写的是 p.z，而 z 是【梯段长度方向】（"轴改名"之前 z 才是上升，
        // 改完之后这条一直没跟着改）：于是"上下弹跳"实际变成了"沿梯段来回抽动"，
        // 用户看到的就是"弹跳范围小"—— 台阶在滑，不是在弹。
        // 加在 y 上之后：法线不变、板面渐变跟着变，配合 vBounce 的明暗才是"在弹"。
        p.y += sgn * amp * env + sgnP * ampP * envP;
        // 【为什么要把它传给片元着色器】台阶整体平移时，顶面的法线没变、板面渐变坐标也没变，
        // 所以"只看几何"的话弹跳几乎看不出来（实测只有 1.3% 的像素在变）——
        // 弹起来的那几级必须【自己变亮】才能读得出"在弹"。这一手也是把它做得像"活物"的关键。
        // 亮暗也同样叠两拍，否则几何在动、明暗却在归零那一刻闪一下。
        vBounce = sgn * env * (0.25 + 0.75 * h2) + sgnP * envP * (0.25 + 0.75 * p2);
        // 基准高低：格子不再交错（见 SSGeom.h），这一项留着但恒为 0。
        vStep = 0.0;
    }

    // 【无限长】本份拷贝整体平移。加在【回绕之后】，所以份与份之间严丝合缝 ——
    // 回绕本身是无缝的，再按整数个梯段长错开，接起来就是一条看不出接头的长梯。
    // 几份拷贝沿 z 错开一个梯段长 —— z 就是卷起来之后的管轴，接起来是一条长楼梯。
    // 【上升必须跟着一起续上】：楼梯的上升是沿着梯段【累积】的，沿 z 走 6.0 就该升
    //     26 级 × 0.159 ≈ 4.13
    // 上一轮我误以为"新映射下上升与 z 无关"、把这条补偿删了 —— 结果每 6.0 就掉回底部，
    // 画面上是【一段一段断开的楼梯】（用户的原话："还是分段的"）。
    p.z += uRepShift;
    p.y += uRepShift * (uStepR / uStepD);

    // =======================================================================
    // 【曲变直】：断面卷成渐开线叶形 + 上面那把上升折成绕 z 的扭转
    // =======================================================================
    const float PI = 3.14159265358979;
    float W = 2.0 * uHalfW;               // 板宽 = 整条叶形两臂的弧长（不能用 const：它依赖 uniform）

    // ---- ① 板宽 x → 断面上一点的【弧长】位置 uw ----
    float uw = clamp((p.x + uHalfW) / max(W, 1e-6), 0.0, 1.0);
    // 底面在卷到底时会【并到踏面上】（板厚被卷没了）。不处理的话就是同一块面画两遍 ——
    // 几何趟开着混合，重合的两块面会叠成两倍亮度，管壁上会多出一层亮带。
    // 把它的宽度收到 0（收到叶形的尖点上），它就变成一条零面积的线，什么都不画。
    if (aNrm.y < -0.5) uw = mix(uw, 0.5, uMix);

    // ---- ② 断面：一条直线段  <->  渐开线叶形（上下两臂）----
    // 叶形按弧长参数化：从尖点(t=0)走到臂端(t=T)，弧长 s = R·t²/2 → t = √(2s/R)。
    // 板宽的两端都落在【尖点】上，正中落在【臂端】—— 也就是卷起来时板的两条长边合拢成
    // 叶形的尖点、板中线展开成最外缘，断面刚好绕一圈包住，接缝天然闭合。
    float sw = W * min(uw, 1.0 - uw);                       // 距尖点的弧长（两端都是尖点）
    float sg = (uw <= 0.5) ? 1.0 : -1.0;                    // 上臂 / 下臂
    float tt = sqrt(max(0.0, 2.0 * sw / max(uLeafR, 1e-6))); // 渐开线参数 t∈[0,T]
    // 【材质对齐用】这一片元对应到 Involute 曲面上的横截面参数 vUV.x：
    // Involute 的 vUV.x 是【按参数 t 均匀】的（i/M，i 均匀采样 t），
    // 而本效果的断面是按【弧长】参数的（弧长 s = R·t²/2）—— 两者不是一回事，必须换算，
    // 否则混过去的材质会沿着断面错位（同一处颜色对不上）。
    {
        // 臂端对应的 t：由"单臂弧长 = 半宽"反解 —— s = R·t²/2 = uHalfW → t = √(2·uHalfW/R)，
        // 也就是 VX_T。不写死常数，宽度或基圆一改它自己跟着走。
        float Tt = sqrt(2.0 * max(uHalfW, 1e-6) / max(uLeafR, 1e-6));
        float tNorm = clamp(tt / max(Tt, 1e-6), 0.0, 1.0);
        vVxU = (uw <= 0.5) ? (0.5 * tNorm) : (1.0 - 0.5 * tNorm);
    }
    // 纵向参数：Involute 的 v = fract(z/周期)，它沿纵深给一点点明暗变化（0.65+0.35v）
    vVxV = fract((p.z - uYMin) / max(uPeriod, 1e-6));
    float ct = cos(tt), st = sin(tt);
    float lx = uLeafR * (ct + tt * st);                     // 渐开线（尖点在 (R,0)）
    float ly = sg * uLeafR * (st - tt * ct);
    float fx = W * uw - uHalfW;                             // 平直态：就是 x 本身
    float Xc = mix(fx, lx, uMix);
    float Yc = mix(0.0, ly, uMix);
    // 【第二形态要跟着 Involute 一起分】—— 每次鼓点 Involute 会沿径向整个炸开一次，
    // 过渡那一秒两边交叉淡化，一边在炸、另一边纹丝不动的话就是两坨对不上的东西叠着。
    // 沿断面自身的径向推（和 Involute 的 aOutward 同义、同一个距离），uMix=0 时恒为 0。
    // 平移不影响法线，所以不用动 nrm。
    if (uRadialPush != 0.0) {
        vec2 rv = vec2(Xc, Yc);
        float rl = max(length(rv), 1e-4);
        Xc += rv.x / rl * (uRadialPush * uMix);
        Yc += rv.y / rl * (uRadialPush * uMix);
    }
    // 断面法线转过的角：平直态 0；叶形态上臂 +t、下臂 π−t（下臂是镜像，走向相反）
    float phi = uMix * ((sg > 0.0) ? tt : (PI - tt));

    // ---- ③ 上升 → 扭转。转角由【连续的 z】给（不是台阶序号）----
    // 每走 uPeriod 转一整圈 —— 与 Involute 的 VX_TWIST 完全同一个螺距，
    // 所以卷到底时逐点重合，不需要再对任何相位。
    // 【符号是负的】：管那趟的相机在原点朝 【−z】 看，而 Involute 朝 +z 看；
    // 取负号之后"离相机越远转得越多"的方向才一致，否则螺旋是镜像的。
    // −uYMin 是让摊平时相位为 0；+uAdvance 是把推进量对到 Involute 上（见 uniform 处的说明）。
    float th = -uMix * 2.0 * PI * (p.z - uYMin + uAdvance) / max(uPeriod, 1e-6);
    float cth = cos(th), sth = sin(th);
    float pyy = mix(p.y, Yc, uMix);                         // 上升在卷起时被扭转吃掉

    vec3 q = vec3(Xc * cth - pyy * sth, Xc * sth + pyy * cth, p.z);

    // ---- ④ 法线跟着一起转（卷曲 + 扭转都是刚性转动，平移不影响法线）----
    vec3 nrm = aNrm;
    // 【底面要并到踏面上】：卷到底时底面和踏面落在同一点（板厚被吃掉了），
    // 如果不把法线也一起并过去，后画的底面会用 −y 的受光盖掉刚画好的踏面（整块发黑）。
    if (aNrm.y < -0.5) nrm = mix(aNrm, vec3(0.0, 1.0, 0.0), uMix);
    nrm.xy = mat2(cos(phi), -sin(phi), sin(phi), cos(phi)) * nrm.xy;
    nrm.xy = mat2(cth, -sth, sth, cth) * nrm.xy;

    // 自然坐标要在【相机旋转之前】取：vLocal 是"在梯段上的位置"，
    // 用它做外缘判定和蓝→琥珀的渐变，必须跟着梯段走、不能跟着相机转。
    // y 取【流动之后】的 p.y —— 这样渐变是【位置性】的：不管哪一级流到这里，
    // 低端永远是蓝的，梯子往上流看起来才有"源源不断"的感觉。
    // 用 aPos.y 就错了：那一级搬到哪儿，它的颜色就跟到哪儿 —— 回绕处会蓝一条、琥珀一条。
    vLocal = vec2(aPos.x, p.y);

    // ---- 相机：绕原点转两个角（= 相机在球面上跑），再沿 z 推远 ----
    // 物体中心恒在原点、视线恒沿 -z → 【视角中心永远是板的几何中心，距离恒为 uCamDist】。
    float cy = cos(uCamYaw), sy = sin(uCamYaw);
    q.xz = mat2(cy, -sy, sy, cy) * q.xz;
    float cp = cos(uCamPitch), sp = sin(uCamPitch);
    q.yz = mat2(cp, -sp, sp, cp) * q.yz;
    nrm.xz = mat2(cy, -sy, sy, cy) * nrm.xz;
    nrm.yz = mat2(cp, -sp, sp, cp) * nrm.yz;

    vWorld = q;
    vCell  = aCell;
    vNrm   = nrm;

    vec3 v = vec3(q.x, q.y, q.z - uCamDist);
    // 出场：整块板朝画面外侧飞出（在视图空间里加，所以永远朝右飞，与相机角度无关）
    if (uExitTime < 0.6) v.x += pow(clamp(uExitTime / 0.6, 0.0, 1.0), 2.0) * 16.0;

    // ---- 雾：只在【卷起来之后】开 ----
    // 平放的楼梯是实体、有明确的两端，加雾只会把它糊掉；卷成管子之后相机在管里往前看，
    // 管子有尽头（30 长），不淡出就会看见"隧道口"，而 Involute 是无限长的。
    // 常数抄 Involute 的（uNear=-9.5 / uSpan=30 → 20.5 处淡没），
    // 于是交接那一刻两边的纵深观感一致，切换看不出来。
    float dist = max(-v.z, 0.0);
    float kf = clamp(1.0 - (dist + 9.5) / 30.0, 0.0, 1.0);
    float fadeFar  = smoothstep(0.0, 0.45, kf);            // 远端淡出（回绕点在远端，必须到 0）
    float fadeNear = clamp((dist - 1.5) / 4.5, 0.0, 1.0);  // 近端淡出（别让管子从镜头里穿过去）
    vFog = mix(1.0, fadeFar * fadeNear, uMix);

    // 近裁剪面：平放时 0.35（相机在板外面），卷到底时贴到 0.05（与 Involute 一致）
    float d = max(-v.z, mix(0.35, 0.05, uMix));
    float aspect = uResolution.x / uResolution.y;
    gl_Position = vec4(v.x * uFocal / aspect, v.y * uFocal, 0.0, d);
}
)";

static const char* kSSFS = R"(#version 330 core
in vec3 vWorld;
in vec3 vNrm;
in vec2 vLocal;
in vec2 vCell;
in vec2 vUV;
in float vBounce;      // 本级台阶当前的弹跳位移（±1）
in float vStep;        // 本级台阶的基准高低（0/1）
in float vFog;         // 卷起来之后的远近淡出（平放时恒为 1）
in float vVxU;         // 对应到 Involute 曲面上的横截面参数（材质过渡用）
in float vVxV;         // 对应到 Involute 曲面上的纵向参数
out vec4 fragColor;

uniform vec4  uColA, uColB;      // 板面双色渐变的两端（vec4 只是为走 glUniform4fv）
uniform float uHalfW, uHalfH;    // 板半宽/半高（= SS_HALF_W / SS_HALF_H）
uniform float uMix;              // 曲变直进度（VS 里同名，同一个 uniform；这里是材质过渡的驱动量）
// 【材质过渡用】Involute 那一侧的材质参数。它鼓点会换配色，所以必须用【它的当前配色】，
// 不能拿本效果自己的 uColA/uColB 顶替 —— 否则过渡到一半颜色对不上，反而更割裂。
uniform vec4  uVXColA, uVXColB;
uniform float uVXRMax;           // Involute 的外缘定义半径（= VXMaxRadius() + 第二形态分离量）
uniform float uYLo, uYHi;        // 渐变要铺满的范围（含所有拷贝）
uniform float uCellNX, uCellNY;
uniform float uSat;              // 固定饱和度（本效果不接质心）
// 外缘微光
uniform float uRimW, uRimI;
// 泛光/合成
uniform int   uPass;
uniform sampler2D uTexScene, uTexBloom;
uniform vec2  uTexel, uBlurDir;
uniform float uBloomAmt, uBloomThresh, uExposure;
uniform vec2  uResolution;
uniform float uTime;
uniform float uOpacity;
// 跨效果 morph 协议（与 Psy/GF 同一份三个 uniform）
uniform int   uEncode;
uniform sampler2D uOldEncoded;
uniform float uMorphBlend;
uniform int   uDarkBg;

float ssHash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

/// 交给搭档去上色的标量场（协议同 Psy 的 evalForm）。板面上就是"在板的哪一段 + 台阶棋盘格"。
float ssPattern() {
    float i = floor(vCell.x * uCellNX);
    float k = floor(vCell.y * uCellNY);
    float facet = (mod(i + k, 2.0) < 0.5) ? -1.0 : 1.0;   // 与 SSGeom.h 的 ((i+k)&1) 同义
    float g = clamp(vLocal.x / uHalfW, -1.0, 1.0);
    return clamp(g * 0.7 + facet * 0.25, -1.0, 1.0);
}

/// 程序化深空背景：纵深梯度 + 两层视差星场（同 Involute）
vec3 ssBackground(vec2 uv) {
    float d = length(uv - 0.5);
    vec3 col = mix(vec3(0.050, 0.056, 0.104), vec3(0.004, 0.005, 0.012),
                   smoothstep(0.02, 0.62, d));
    for (int L = 0; L < 2; ++L) {
        float fl = float(L);
        vec2 g = uv * (170.0 + fl * 190.0);
        vec2 id = floor(g), f = fract(g) - 0.5;
        float h = ssHash(id + fl * 37.1);
        float d2 = length(f - vec2(ssHash(id + 1.7), ssHash(id + 3.3)) + 0.5);
        float st = smoothstep(0.055 - fl * 0.02, 0.0, d2) * step(0.985 - fl * 0.004, h);
        st *= 0.55 + 0.45 * sin(uTime * 1.6 + h * 60.0);
        col += vec3(0.72, 0.80, 1.0) * st * (0.9 - fl * 0.3);
    }
    return col;
}

void main() {
    // ================= 全屏趟 =================
    if (uPass == 0) {                       // 背景（编码趟同样要写，理由见 VX 那处说明）
        vec3 bg = ssBackground(vUV);
        fragColor = (uEncode > 0) ? vec4(0.5, bg) : vec4(bg, 1.0);
        return;
    }
    if (uPass == 2) {
        vec3 c = texture(uTexScene, vUV).rgb;
        float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
        fragColor = vec4(c * max(0.0, l - uBloomThresh) / max(l, 1e-4), 1.0);
        return;
    }
    if (uPass == 3) {
        vec2 t = uBlurDir * uTexel;
        vec3 s = texture(uTexBloom, vUV).rgb * 0.2270270;
        s += (texture(uTexBloom, vUV + t*1.3846).rgb + texture(uTexBloom, vUV - t*1.3846).rgb) * 0.3162162;
        s += (texture(uTexBloom, vUV + t*3.2308).rgb + texture(uTexBloom, vUV - t*3.2308).rgb) * 0.0702703;
        fragColor = vec4(s, 1.0);
        return;
    }
    if (uPass == 4) {
        vec3 col = texture(uTexScene, vUV).rgb + texture(uTexBloom, vUV).rgb * uBloomAmt;
        col *= uExposure;
        // 只压亮度、颜色等比跟随（逐通道 ACES 会把颜色拉向白，饱和度全丢）
        float lum0 = max(dot(col, vec3(0.299, 0.587, 0.114)), 1e-4);
        float lumT = clamp((lum0*(2.51*lum0+0.03))/(lum0*(2.43*lum0+0.59)+0.14), 0.0, 1.0);
        col = clamp(col * (lumT / lum0), 0.0, 1.0);
        float lum = dot(col, vec3(0.299, 0.587, 0.114));
        col = clamp(mix(vec3(lum), col, uSat), 0.0, 1.0);
        vec2 c = vUV - 0.5;
        col *= 1.0 - dot(c, c) * 0.75;
        if (uDarkBg == 0) col = vec3(1.0) - col;
        // morph 收尾：sb=0 时必须【正好是上一个效果】
        if (uMorphBlend > 0.0 && uMorphBlend < 1.0) {
            float sb = uMorphBlend * uMorphBlend * (3.0 - 2.0 * uMorphBlend);
            vec3 oldCol = texture(uOldEncoded, vUV).gba;
            col = mix(oldCol, col, sb);
        }
        fragColor = vec4(col, uOpacity);
        return;
    }

    // ================= 几何趟 / 编码趟 =================
    // 面法线直接用【烘进顶点】的那份（顶上/底面 +z/-z，四个侧面 ±x/±y）。
    // 不要退回 dFdx/dFdy：台阶只有 5~10 像素，2×2 像素块经常横跨两个三角形，
    // 那里的导数是未定义的，实测会让同样的参数两次渲染差 32% 的像素。
    vec3 n = normalize(vNrm);
    // 板是实体，镜头绕着它转 —— 用【两盏灯取绝对值】而不是"统一朝镜头"：
    // 台阶的立面朝向四面八方，取绝对值才不会有一半面死黑。离线预览台验证过这个组合。
    vec3 L1 = normalize(vec3(-0.45, 0.62, -0.64));
    vec3 L2 = normalize(vec3( 0.70, -0.34, -0.63));
    // 权重之和必须 < 1/曝光(1.15)，否则最亮的面会顶到 255 —— 顶了就【和微光的亮部糊在一起】，
    // 外缘那道边反而看不出来（实测过：改之前整个画面最亮恒为 255）。
    float lam = abs(dot(n, L1)) * 0.78 + abs(dot(n, L2)) * 0.38;
    float sh = 0.10 + lam;                     // 环境 0.10，避免背面死黑

    // 基色：横跨板面的双色渐变。morph 时改由"混合后的场"驱动（与 Psy/GF 同构）。
    // 【渐变要顺着"升上去"的方向走】。原来我按 x/y 混合取色，渐变是横跨板面的 ——
    // 眼睛读的是那条横跨的色带，根本注意不到板面是斜的（用户的原话："还是和改之前一样"）。
    // 现在 85% 由 y（= 板子升高的方向）决定：低端蓝、高端琥珀，一眼就是"一条升上去的楼梯"。
    // 渐变按【最终 y】铺满所有拷贝的范围：跨拷贝是连续的，不会一格一色地重复，
    // 看起来就是一条望不到头的长梯从蓝爬到琥珀。
    float ty = clamp((vLocal.y - uYLo) / max(uYHi - uYLo, 1e-4), 0.0, 1.0);
    float t = ty * 0.85 + clamp((vLocal.x / uHalfW) * 0.5 + 0.5, 0.0, 1.0) * 0.15;
    if (uMorphBlend > 0.0 && uMorphBlend < 1.0) {
        float sb = uMorphBlend * uMorphBlend * (3.0 - 2.0 * uMorphBlend);
        float oldPat = texture(uOldEncoded, gl_FragCoord.xy / uResolution).rgb.r * 2.0 - 1.0;
        t = oldPat * (1.0 - sb) + (ssPattern() * 0.5 + 0.5) * sb;
    }
    vec3 col = mix(uColA.rgb, uColB.rgb, t);
    col *= sh;
    // 弹起来的那几级自己变亮、沉下去的压暗 —— 否则台阶只是平移，顶面法线与配色都没变，
    // 画面上几乎看不出它在弹（实测：只有 1.3% 的像素会变）。
    // 但也不能给太狠：到 2.6 倍时 2560 级台阶各弹各的，画面会糊成一片雪花。
    // 0.7 倍幅、上限 1.9，让【几何位移】为主、【明暗】只做加重。
    col *= clamp(1.0 + vBounce * 0.7, 0.5, 1.9);
    // 高的那一档台阶亮一点。这一手是【为了让台阶在正面视角也读得出来】：
    // 所有顶面法线都是 +z、受光一模一样，正面看过去"高低"是没有任何明暗证据的
    // （用户看到的"没改"就是这么来的）。给高低两档一点亮度差，楼梯的形状才立得住。
    col *= 1.0 + vStep * 0.30;

    // ---- 外缘微光：只按【宽度】判 ----
    // 梯段沿长度方向是【无限】的，本来就没有"两端"；再按 y 判的话，
    // 每个回绕周期都会亮出一道横向的环，反而暴露了它是拼出来的。
    // 用未旋转的 vLocal，所以它永远贴着板自己的四条边，不会跟着相机转。
    // smoothstep 的宽度必须兜底（0 宽 = 0/0 → NaN → 整帧涂黑），且只能"正常方向 + 取补"。
    float outerness = abs(vLocal.x) / uHalfW;   // 1.0 = 正好在梯段边上
    float rim = 1.0 - smoothstep(0.0, max(uRimW, 1e-4), 1.0 - outerness);
    vec3 rimCol = mix(mix(uColA.rgb, uColB.rgb, 0.5), vec3(1.0), 0.55);
    // 【卷起来之后要把微光淡掉】：板的两条长边卷起来正好合拢成叶形的【尖点】，
    // 而 Involute 的微光打在【外缘】—— 两条亮线不在同一个地方。留着的话，
    // 材质交叉淡化那一刻会多出一道 Involute 没有的亮线，看得出来。
    col += rimCol * (rim * uRimI * (1.0 - uMix));

    // =======================================================================
    // 【材质随扭曲一起过渡】
    // =======================================================================
    // 用户要的是"边扭曲边过渡"，不是原来的两段式（先换表面质感、再变形）。
    // 【为什么不能直接交叉淡化两张图】过渡中途两边的【形状】是不一样的
    // （Involute 是完整的管、楼梯是卷到一半的形态），叠起来就是重影。
    // 所以改成：在楼梯自己的着色里，把【同一个片元落在 Involute 曲面上的样子】算出来，
    // 再按 uMix 混过去 —— 材质和几何共用同一个进度、同一张几何，这才是真正的"边扭曲边过渡"。
    // uMix→1 时这段材质就等于 Involute 的材质，交叉淡化那一下几乎看不出来。
    if (uMix > 0.0) {
        vec3 nv = normalize(vNrm);
        vec3 V1 = normalize(vec3(-0.45, 0.62, -0.64));
        vec3 V2 = normalize(vec3( 0.70, -0.34, -0.63));
        // 【单面光 max，不是 abs】—— Involute 用单面光；楼梯自己用 abs 是为了让朝下的
        // 立面不死黑。混过去的这一份要跟 Involute 逐点对得上，所以照抄它的写法。
        float lamV = max(dot(nv, V1), 0.0) * 0.85 + max(dot(nv, V2), 0.0) * 0.45;
        float shV  = 0.10 + lamV;
        // 基色：沿横截面走的双色渐变 + 沿纵深的一点变化（系数与 kVXFS 逐字一致）
        vec3 cV = mix(uVXColA.rgb, uVXColB.rgb, vVxU);          // patMix = vVxU*2-1 → *0.5+0.5 化简
        cV *= 0.65 + 0.35 * vVxV;
        cV *= shV;
        // 外缘微光：判据是【到中轴的半径】，不是断面参数（Involute 那边踩过这个坑）
        float outerV = 1.0 - length(vWorld.xy) / max(uVXRMax, 1e-4);
        float rimV   = 1.0 - smoothstep(0.0, max(uRimW, 1e-4), outerV);
        cV += mix(mix(uVXColA.rgb, uVXColB.rgb, 0.5), vec3(1.0), 0.55) * (rimV * uRimI);
        col = mix(col, cV, uMix);
    }

    // 纵深淡出。必须放在编码趟【之前】但只乘颜色部分 ——
    // 编码出去的是"场"（.r），被雾乘一下的话搭档拿到的场就不是原样了。
    col *= vFog;

    if (uEncode > 0) {
        if (vFog < 0.02) discard;   // 已淡出到看不见的几何不该在场里留痕（同 Involute）
        fragColor = vec4(ssPattern() * 0.5 + 0.5, col);
        return;
    }
    // 【alpha 必须写 vFog，不能写死 1.0】——
    // 平放的楼梯是实体、vFog 恒为 1，写死看不出区别；可卷成管子之后相机在管里往前看，
    // 管子的近端就在镜头上、远端在雾里。alpha 写死 1.0 的话这些"看不见"的几何仍然会
    // 用黑盖住背景 —— 而浅色底是严格负片，黑变成【纯白】，画面上就是正中间一坨白斑。
    // Involute 那边一直是这么写的（fragColor = vec4(col, vFog) + 几何趟开混合），照抄。
    fragColor = vec4(col, vFog);
}
)";
