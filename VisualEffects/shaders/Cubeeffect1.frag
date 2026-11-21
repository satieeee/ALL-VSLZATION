#version 330 core
// Cubeeffect1 — 无限立方体网格世界（Cube effects 家族首个 GPU 效果）
// 白底/黑底墨线 · 固定正前方缓行 · 整体平移+呼吸+棱线波浪 · 随机断续虚线
// 播放链 shader 协议：结尾 cpuFinalize + uOpacity；encode/morph 收尾与其它全屏效果一致。
//
// 平移对称说明：均匀无限格线沿正前方纯平移时画面不变（视觉静止），
// 故“边的运动”= ① 整体漂移+呼吸缩放（线保持笔直）② 竖直棱线行波弯曲。
// 均用可解析偏移距离场，不破坏细线采样。
in vec2 vUV; out vec4 fragColor;
uniform float uTime,uBpm,uBeatPhase,uSpeed; uniform vec2 uResolution;
uniform float uCubeCamPhase;   // Cubeeffect1 相机 z 相位（CPU 积分：速度 0.47×(1+4×energy)=1x~5x，变速不回跳）
uniform int uEncode,uDarkBg;
uniform float uOpacity;
uniform sampler2D uOldEncoded;
uniform float uMorphBlend;
uniform int uForm,uFormB;            // 形态：0=白底墨线网格+飞蚊, 1=黑底灰网格+粘滴
uniform float uFormBlend;            // 形态过渡 0→1（鼓点触发，normal effect 同款平滑 crossfade）
uniform vec4 uDrops[150];    // 粘滴：xyz=相机系偏移(ro+off=世界pos), w=半径(融边时收小)
uniform vec4 uDropDir[150];  // 粘滴运动方向：xy=单位方向
uniform vec4 uMarks[96];     // 撞边闪光段：xyz=段中心(世界), w=轴向(1沿x/2沿y)+3×flash

// —— 调参（用户确认值 2026-09-08：wave/drift/breath 关、全碎段、超细、强墨）——
// 行进速度已迁到 CPU 积分（uCubeCamPhase：0.47×(1+4×energy)=1x~5x，见 main.cpp UpdateBeat）
const float g_wave     = 0.0;    // 棱线波涌：关闭
const float g_waveSp   = 2.2;    // 波涌速度（关闭时无效）
const float g_drift    = 0.0;    // 整体左右漂移：关闭
const float g_breath   = 0.0;    // 呼吸缩放：关闭
const float g_brSp     = 0.0;    // 呼吸速度（关闭时无效）
const float g_dash     = 1.0;    // 虚线随机断续：全碎段
const float g_under    = 0.0;    // 模糊衬光：关闭
const float g_fog      = 0.0;    // 雾深：关闭
const float g_width    = 1.2;    // 纤细（再翻倍后 thin 系数见 main）
const float g_brt      = 2.0;    // 墨色深度（用户调 2）

float hash1(vec2 p){ return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453); }
float hash13(vec3 p){ return fract(sin(dot(p,vec3(127.1,311.7,74.7)))*43758.5453); }
float dq(float v){ return v-floor(v+0.5); }

// 飞蚊层辅助：线段距离 + 平面旋转（简单平面几何碎片用）
float segD(vec3 p,vec3 a,vec3 b){ vec3 ab=b-a; float h=clamp(dot(p-a,ab)/dot(ab,ab),0.0,1.0); return length(p-a-ab*h); }
vec3 rotZ(vec3 p,float a){ float c=cos(a),s=sin(a); return vec3(c*p.x-s*p.y,s*p.x+c*p.y,p.z); }
vec3 rotX(vec3 p,float a){ float c=cos(a),s=sin(a); return vec3(p.x,c*p.y-s*p.z,s*p.y+c*p.z); }

// 飞蚊层参考值（对应预览滑杆；实际每单体量由 hash 随机基线 × 音频强度调制，见飞蚊 pass）
const float g_flyRef  = 1.0;    // 可见度参考（实际随音频浮现/隐没）
const float g_tremRef = 1.0;    // 震颤参考（随高频/鼓点）
const float g_sizeRef = 1.2;    // 大小参考（随低频）—— ×3（用户 2026-09-09：基础太小）

// 竖直棱线族（沿z）行波弯曲偏移
float zfx(float z){ return g_wave*sin(z*1.3 + uTime*g_waveSp); }
float zfy(float z){ return g_wave*0.7*cos(z*1.05 - uTime*g_waveSp*0.8 + 1.3); }

// 到最近棱线距离 + 沿线参数
vec4 latticeDist(vec3 q, float z){
  float cx=dq(q.x-zfx(z)), cy=dq(q.y-zfy(z));
  float dZ2=cx*cx+cy*cy;                       // 沿z（含弯曲）
  float ay=dq(q.y), az=dq(z);  float dX2=ay*ay+az*az;   // 沿x
  float bx=dq(q.x), bz=dq(z);  float dY2=bx*bx+bz*bz;   // 沿y
  float best,along,fam;
  if(dZ2<=dX2 && dZ2<=dY2){best=dZ2; along=z;   fam=0.0;}
  else if(dX2<=dY2)       {best=dX2; along=q.x; fam=1.0;}
  else                    {best=dY2; along=q.y; fam=2.0;}
  return vec4(sqrt(best),along,fam,0.0);
}

// form0：白底墨线网格 + 飞蚊（原 Cubeeffect1 视觉）
vec3 renderForm0(){
  vec2 uv=(gl_FragCoord.xy-0.5*uResolution)/uResolution.y;
  // 音频 ABI（audio_prelude.glsl 已拼在前声明 uniform；此处直接引用）
  float bassAtt = clamp(uBassAtt, 0.0, 1.0);   // 低频能量
  float trebAtt = clamp(uTrebAtt, 0.0, 1.0);   // 高频能量
  float eng     = clamp(uEnergy, 0.0, 1.0);    // 整体 RMS
  float beat    = uBeatFlash;                  // kick 脉冲 0..1

  // 相机：CPU 积分相位 uCubeCamPhase（速度 0.47×(1+4×energy)，1x~5x，单调不回跳）
  // 不用 shader 内 uTime×常数——CPU 端每帧 pos+=v·dt 才能让速度随音频又不回跳
  float wave = g_wave;                          // g_wave=0：波涌关
  vec3 ro=vec3(0.35,0.32,uCubeCamPhase);
  float f=2.5;
  vec3 rd=normalize(vec3(uv.x, uv.y, f));

  // 白底墨线 / 黑底亮线
  vec3 bg, ink;
  if (uDarkBg != 0) { bg=vec3(0.0,0.0,0.02); ink=vec3(0.45,0.80,1.0); }
  else              { bg=vec3(1.0);          ink=vec3(0.05,0.10,0.30); }

  float thin=52.0+g_width*320.0;   // 纤细翻倍：细一倍（线宽 ~1/276）
  vec3 col=bg;
  float t=0.12;
  const float FAR=24.0;
  for(int i=0;i<340;i++){
    if(t>FAR) break;
    vec3 P=ro+rd*t;
    float fog=exp(-t*g_fog);

    // 整体漂移 + 呼吸缩放（直线运动）
    float drift=g_drift*sin(uTime*0.7);
    float sc=1.0/(1.0+g_breath*0.5*sin(uTime*g_brSp));
    vec3 Q=P;
    Q.x=(P.x+drift)*sc;
    Q.y=P.y*sc;

    vec4 gv=latticeDist(Q, P.z);
    float d=gv.x, al=gv.y, fam=gv.z;

    float core=exp(-d*thin);
    float under=exp(-d*(16.0+g_width*18.0))*g_under*0.35;   // 模糊衬光同步收窄 → 整体细一倍

    // 虚线随机断续
    float lum=core+under;
    if(g_dash>0.001){
      float segLen=0.55;
      float sp2=al/segLen;
      float seg=floor(sp2), loc=fract(sp2);
      float h=hash1(vec2(seg*1.31,fam*7.77));
      float duty=mix(0.15,0.9,h);
      float glow=0.5+0.5*sin(uTime*0.6+seg*1.7+fam*3.1);
      float on=1.0-smoothstep(duty-0.02,duty+0.02,loc);
      float mask=on*mix(0.06,1.0,smoothstep(0.2,0.9,glow));
      lum*=mix(1.0,mask,g_dash);
    }

    // 鼓点闪光
    float b=beat*exp(-t*0.3);
    float a=min(lum*fog*g_brt*(1.0+b*0.6),0.85);
    col=mix(col,ink,a);

    float step=clamp(d*0.6, 0.0006, 0.16+wave*0.12);  // 下限 0.0006：细线(1/436)采样不漏
    t+=step;
  }

  // ========== 飞蚊单体层：平面几何碎片（可见度/震颤/大小 均随音频随机调制） ==========
  // 视觉=飞蚊症：白视野里漂浮的细线小片，随“眼球”游移。
  // 平面几何：三角形/菱形/细线（只画轮廓 segD）。锚定相机前方缓慢漂移 + 翻滚。
  // 音频关系（真实 ABI）：
  //   可见度 ∝ beat/eng —— kick 时单体浮现，弱音半隐没
  //   震颤幅度 ∝ trebAtt —— 高频强则轮廓线高抖
  //   大小 ∝ bassAtt —— 低频推大碎片
  // 每单体又有自己的 hash 随机基线 → 各单体对音频响应不同步（随机相关）
  if(g_flyRef>0.001){
    float tf=0.4;
    const float FLYFAR=13.0;
    for(int j=0;j<260;j++){
      if(tf>FLYFAR) break;
      vec3 P=ro+rd*tf;
      float fogf=exp(-tf*g_fog*0.5);
      vec3 cmin=vec3(1e9);
      // 音频→叠加扰动的总幅度（跨单体一致）：beat 浮现、treb 震颤、bass 推大。
      // 关键：这些只当“幅度/加法”，绝不乘进位置/姿态的累计相位——
      // 乘相位=音频一落几何体瞬跳回（“回到初始态”）。加法幅度回落则平滑回轨迹。
      // 可见度(visMod)保持原量级；其余幅度参数均 ×2（用户 2026-09-09）
      float visMod = 0.25+0.75*eng + beat*0.9;              // 可见度：×1（不变）
      float tremMod = 0.6+trebAtt*2.4 + beat*2.4;           // 震颤幅度：×2
      float sizeMod = 1.0 + bassAtt*1.8;                    // 大小：×2
      float drv = 1.0+eng*3.0+beat*4.0;                     // 音频位移扰动幅度：×2
      for(int k=0;k<7;k++){
        float kf=float(k);
        float h1=hash13(vec3(kf*1.3,3.1,1.7));
        float h2=hash13(vec3(kf*2.7,7.7,2.3));
        float h3=hash13(vec3(kf*5.1,9.1,4.7));
        // 单体级随机基线（音频之上各自错开）
        float visK  = g_flyRef*(0.15+0.85*h1)*visMod;
        float tremK = g_tremRef*(0.3+0.7*h2)*tremMod;
        float sizeK = g_sizeRef*(0.7+0.6*h3)*sizeMod;
        if(visK<0.05) continue;   // 本单体隐没
        // —— 基础漂浮轨迹：相位只乘 uTime×常数频率（单调、不回跳）——
        vec3 base=vec3((h1-0.5)*3.0,(h2-0.5)*2.2, 3.2+float(k)*1.3);
        float wt=uTime*0.31, w2=uTime*0.23, w3=uTime*0.17;
        vec3 pos=base
          + vec3(sin(wt+h1*41.0)*0.9, sin(w2+h2*29.0)*0.7, sin(w3+h3*17.0)*0.5);
        // —— 音频位移扰动：加在轨迹上（幅度随音频，回落平滑；低频漂移+高频微颤）——
        pos += vec3(sin(uTime*1.7+kf*5.0), sin(uTime*1.9+kf*3.0), sin(uTime*1.5+kf*7.0))
               * (0.10*eng + 0.06*drv*trebAtt);   // 位移扰动幅度：×2
        vec3 cen=ro+pos;
        // —— 姿态：基础慢转用 uTime×常数；音频只叠加小抖动幅度 ——
        float yaw  = uTime*(0.12+0.15*h1) + sin(uTime*0.7+kf*2.0)*0.8*trebAtt;    // ×2
        float pitch= uTime*(0.09+0.1*h2) + sin(uTime*0.9+kf*1.0)*0.6*tremMod;    // ×2
        float roll = sin(uTime*0.5+kf*1.1)*(0.4+0.6*trebAtt);                    // ×2
        vec3 lp=P-cen;
        lp=rotZ(lp,yaw); lp=rotX(lp,pitch); lp=rotZ(lp,roll);
        // 碎片半长（bass 推大 = 大小随音频，加法/幅度式：音频回落回基础尺寸）
        float s=(0.10+0.10*h3)*sizeK;
        // 三类平面几何轮廓
        float dd;
        int kk=int(mod(float(k),3.0));   // GLSL ES 3.0 支持整数 %；此处保持与预览一致改用 mod
        if(kk==0){ // 三角形
          vec3 v0=vec3(0.0,s,0.0), v1=vec3(-0.866*s,-0.5*s,0.0), v2=vec3(0.866*s,-0.5*s,0.0);
          dd=min(segD(lp,v0,v1),min(segD(lp,v1,v2),segD(lp,v2,v0)));
        } else if(kk==1){ // 菱形
          vec3 d0=vec3(0.0,s*0.9,0.0), d1=vec3(s*0.6,0.0,0.0), d2=vec3(0.0,-s*0.9,0.0), d3=vec3(-s*0.6,0.0,0.0);
          dd=min(segD(lp,d0,d1),min(segD(lp,d1,d2),min(segD(lp,d2,d3),segD(lp,d3,d0))));
        } else { // 细线
          dd=segD(lp,vec3(-s,0.0,0.0),vec3(s,0.0,0.0))*0.5;
        }
        // 震颤：固定频率高频抖动，幅度随 tremK（音频只调幅度，不调相位→无突变）
        dd += 0.006*tremK*(0.5+sin(uTime*26.0+kf*7.7));   // 震颤幅度：×2
        cmin=min(cmin,vec3(dd,0.0,0.0));
      }
      float dF=cmin.x;
      float lineW=0.0015 + 0.00175*tremMod;  // 线粗：调制项=上版(0.0035)一半，不糊底层网格
      float lumF=exp(-dF/(lineW+1e-4));
      float aF=min(lumF*visMod*g_brt*2.0,0.6);   // 可见度 ∝ beat/eng
      col=mix(col,ink,aF);
      float stepF=clamp(dF*0.6,0.002,0.3);
      tf+=stepF;
    }
  }
  return col;
}

// —— 形态1（粘滴/闪光）辅助：ray–球 / ray–线段 距离 ——
bool hitSphere(vec3 ro, vec3 rd, vec3 c, float r, out float tt){
  vec3 oc=ro-c;
  float b=dot(oc,rd), cc=dot(oc,oc)-r*r;
  float disc=b*b-cc;
  if(disc<0.0) return false;
  float sq=sqrt(disc);
  float t1=-b-sq, t2=-b+sq;
  if(t2<0.12) return false;
  tt = t1>0.12 ? t1 : t2;
  return true;
}
float raySegDist(vec3 ro, vec3 rd, vec3 a, vec3 b){
  vec3 ab=b-a; vec3 w0=ro-a;
  float A=dot(rd,rd), B=dot(rd,ab), C=dot(ab,ab);
  float D=dot(rd,w0), E=dot(ab,w0);
  float denom=A*C-B*B; float t,s;
  if(abs(denom)>1e-6){ t=(B*E-C*D)/denom; s=(A*E-B*D)/denom; }
  else { t=(-D)/A; s=0.0; }
  if(s<0.0)s=0.0; else if(s>1.0)s=1.0;
  vec3 wp=w0+ab*s; t=-dot(wp,rd)/A;
  return length((ro+rd*t)-(a+ab*s));
}
float raySegDistS(vec3 ro, vec3 rd, vec3 a, vec3 b, out float s){
  vec3 ab=b-a; vec3 w0=ro-a;
  float A=dot(rd,rd), B=dot(rd,ab), C=dot(ab,ab);
  float D=dot(rd,w0), E=dot(ab,w0);
  float denom=A*C-B*B; float t;
  if(abs(denom)>1e-6){ t=(B*E-C*D)/denom; s=(A*E-B*D)/denom; }
  else { t=(-D)/A; s=0.0; }
  if(s<0.0)s=0.0; else if(s>1.0)s=1.0;
  vec3 wp=w0+ab*s; t=-dot(wp,rd)/A;
  return length((ro+rd*t)-(a+ab*s));
}

// form1：黑底灰网格 + 纯白粘滴长条 + 撞边闪光（原 Cubeeffect2 视觉）
vec3 renderForm1(){
  vec2 uv=(gl_FragCoord.xy-0.5*uResolution)/uResolution.y;
  vec3 ro=vec3(0.35,0.32,uCubeCamPhase);
  float f=2.5;
  vec3 rd=normalize(vec3(uv.x, uv.y, f));
  const float g_lineOp=0.5;                 // 灰线不透明度（改这里可明显调亮/调暗）
  vec3 bg=vec3(0.0);
  vec3 lineCol=vec3(0.62,0.64,0.70);
  vec3 col=bg;
  float thin=52.0+g_width*320.0;
  float t=0.12; const float FAR=24.0;
  float bestCore=0.0;
  for(int i=0;i<340;i++){
    if(t>FAR) break;
    vec3 P=ro+rd*t;
    vec4 gv=latticeDist(P, P.z);
    float d=gv.x, al=gv.y, fam=gv.z;
    float lum=exp(-d*thin);
    if(g_dash>0.001){
      float segLen=0.55;
      float sp2=al/segLen;
      float seg=floor(sp2), loc=fract(sp2);
      float h=hash1(vec2(seg*1.31,fam*7.77));
      float duty=mix(0.15,0.9,h);
      float glow=0.5+0.5*sin(uTime*0.6+seg*1.7+fam*3.1);
      float on=1.0-smoothstep(duty-0.02,duty+0.02,loc);
      float mask=on*mix(0.06,1.0,smoothstep(0.2,0.9,glow));
      lum*=mix(1.0,mask,g_dash);
    }
    bestCore=max(bestCore,lum);
    float step=clamp(d*0.6,0.0006,0.16);
    t+=step;
  }
  col=mix(col,lineCol,min(bestCore*g_lineOp,1.0));   // 黑底灰虚线网格

  // 纯白粘滴小长条（粘滞拉长形）
  for(int k=0;k<150;k++){
    vec4 dr=uDrops[k];
    if(dr.w<=0.0) continue;
    vec3 cen=ro+dr.xyz;
    vec3 mv=vec3(uDropDir[k].xy,0.0);
    float mlen=length(mv); if(mlen<1e-4f) mv=vec3(1,0,0); else mv/=mlen;
    float tt;
    float L=dr.w*16.0;
    vec3 head=cen+mv*dr.w*1.8f;
    vec3 tail=head-mv*L;
    float s;
    float dd=raySegDistS(ro,rd,head,tail,s);
    float lat=smoothstep(dr.w*1.5,dr.w*0.45,dd);
    float longt=(1.0-s*0.8);
    if(lat>0.001){ col=mix(col,vec3(1.0),lat*longt*0.95f); }
    if(hitSphere(ro,rd,head,dr.w*0.9f,tt)){
      vec3 P=ro+rd*tt;
      float dhp=length(P-head);
      float spec=smoothstep(dr.w*1.0,dr.w*0.15,dhp);
      col=mix(col,vec3(1.0),spec*0.9f);
    }
  }
  // 撞边闪光段（青蓝描边+白芯，随 flash 衰减淡去）
  const float MARK_HALF=0.12, MARK_W=0.008;
  for(int k=0;k<96;k++){
    vec4 mk=uMarks[k];
    if(mk.w<0.0) continue;
    float w=mk.w;
    int ax=int(mod(w,3.0));
    float flash=(w-float(ax))/3.0;
    if(flash<=0.001) continue;
    vec3 c=mk.xyz;
    vec3 dir=(ax==1)?vec3(1,0,0):vec3(0,1,0);
    vec3 a=c-dir*MARK_HALF, b=c+dir*MARK_HALF;
    float d=raySegDist(ro,rd,a,b);
    if(d<MARK_W*7.0){
      float core=smoothstep(MARK_W,MARK_W*0.30,d);
      float halo=smoothstep(MARK_W*7.0,MARK_W*1.2,d);
      vec3 ink=mix(vec3(0.10,0.55,1.00), vec3(0.95,1.00,1.00), core*0.6);
      col=mix(col,ink,clamp((core*1.15+halo*0.25)*flash,0.0,1.0));
    }
  }
  return col;
}

void main(){
  // 形态过渡（normal effect 同款）：uForm 当前形态 / uFormB 目标形态，uFormBlend 0→1 平滑 crossfade
  vec3 col;
  if(uForm==uFormB){
    if(uForm==0) col=renderForm0(); else col=renderForm1();
  } else {
    vec3 colA,colB;
    if(uForm==0) colA=renderForm0(); else colA=renderForm1();
    if(uFormB==0) colB=renderForm0(); else colB=renderForm1();
    float fsb=uFormBlend*uFormBlend*(3.0-2.0*uFormBlend);
    col=mix(colA,colB,fsb);
  }

  // === Pass 1 编码（旧效果被存作 old）：R=墨线密度, GBA=颜色 ===
  float pat = clamp(1.0-max(max(col.r,col.g),col.b), 0.0, 1.0);
// === Pass 2 跨效果 morphing：颜色淡变 ===
col=cpuFinalize(col,gl_FragCoord.xy,uDitherStrength);
  fragColor=vec4(col,uOpacity);
}
