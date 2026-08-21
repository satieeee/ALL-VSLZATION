// MTH.frag — 种子驱动线条生成器（极坐标版）
// uForm: 0=spiral, 1=rose, 2=radial, 3=archimedean；uProjection=fieldType；uColorScheme=配色

in vec2 vUV;
out vec4 fragColor;

uniform float uTime, uBpm, uBeatPhase, uSpeed;
uniform vec2 uResolution;
uniform int uForm, uFormB, uColorScheme, uProjection;
uniform float uFormBlend;
uniform int uDarkBg;
uniform float uOpacity;
uniform int uEncode;
uniform float uMorphBlend;
uniform sampler2D uOldEncoded;

#define PI 3.14159265359
#define TAU 6.28318530718

const float ARM_COUNT = 8.0;
const float SPIRAL_GROWTH = 0.22;
const float TWIST = 0.75;
const float CURVATURE = 0.45;
const float FREQ_LOW = 0.05;
const float FREQ_HIGH = 3.20;
const float COMPLEXITY = 5.0;
const float THICKNESS = 0.25;
const float SHARPNESS = 0.85;
const float OFFSET_X = -0.05;
const float OFFSET_Y = 0.05;
const float BETA = 1.95;
const float FRACTAL_DIM = 1.72;
const float NOISE_SEED = 8842109.0;

float hash21(vec2 p){ p=fract(p*vec2(123.34,456.21)); p+=dot(p,p+45.32); return fract(p.x*p.y); }
float vnoise(vec2 p){ vec2 i=floor(p),f=fract(p); f=f*f*(3.0-2.0*f);
  return mix(mix(hash21(i),hash21(i+vec2(1.,0.)),f.x),mix(hash21(i+vec2(0.,1.)),hash21(i+vec2(1.,1.)),f.x),f.y); }
float fbm(vec2 p){ float v=0.,a=.5; for(int i=0;i<8;i++){ v+=a*vnoise(p); p*=2.03; a*=.5; } return v; }

vec3 hsl2rgb(float h,float s,float l){ h=mod(h,360.); if(h<0.)h+=360.;
  float c=(1.-abs(2.*l-1.))*s, hp=h/60., x=c*(1.-abs(mod(hp,2.)-1.));
  vec3 rgb;
  if(hp<1.)rgb=vec3(c,x,0.); else if(hp<2.)rgb=vec3(x,c,0.);
  else if(hp<3.)rgb=vec3(0.,c,x); else if(hp<4.)rgb=vec3(0.,x,c);
  else if(hp<5.)rgb=vec3(x,0.,c); else rgb=vec3(c,0.,x);
  return rgb+(l-c*.5); }

// 形态函数（返回图案值，供形态混合用）
float evalForm(int f, vec2 p, float r, float theta) {
    float th=theta;
    float rr=r;
    // 时间域扭曲：径向呼吸 + 角向扭动（液体流动感）
    rr = rr * (1.0 + 0.15*sin(r*8.0 + uTime*0.8) + 0.12*cos(r*5.0 - uTime*0.6));
    th = th + 0.08*sin(r*10.0 + uTime*0.9) + 0.06*cos(r*6.0 - uTime*0.7);
    // 矢量场
    if(uProjection==0)      rr=rr*(1.0+TWIST*0.3);
    else if(uProjection==1) rr=rr*(1.0-TWIST*0.3);
    else if(uProjection==2){ th=th+TWIST*rr*0.3; }
    else if(uProjection==3) rr=p.x*p.y;
    // 旋转 + 节拍脉冲
    float beatPulse = 1.0 + 0.35*sin(uBeatPhase*TAU);
    float ang=th*TAU + uTime*uBpm/4800.0*uSpeed*beatPulse;
    if(f==0)      return sin(ARM_COUNT*ang+log(max(rr,0.01))*SPIRAL_GROWTH*30.0);
    else if(f==3) return sin(ARM_COUNT*ang+rr*SPIRAL_GROWTH*30.0);
    else if(f==1) return cos(ARM_COUNT*ang)*(1.0-0.5*rr);
    else          return sin(ARM_COUNT*ang);
}

// 配色函数（图案值 → 颜色，供图案级 morphing 用）
vec3 mapColor(float pattern) {
    float hueBase;
    if(uColorScheme==0)      hueBase=210.0;
    else if(uColorScheme==1) hueBase=185.0;
    else if(uColorScheme==2) hueBase=240.0;
    else if(uColorScheme==3) hueBase=165.0;
    else if(uColorScheme==4) hueBase=0.0;
    else                     hueBase=225.0;
    float hue=hueBase+pattern*30.0;
    float sat=mix(0.10,0.85,fract(pattern*2.7));
    float lig=mix(0.05,0.95,0.5+0.5*sin(pattern*4.0));
    return hsl2rgb(hue,sat,lig);
}

void main(){
    float cx=uResolution.x*0.5, cy=uResolution.y*0.5;
    float mr=min(cx,cy);
    vec2 uv=(gl_FragCoord.xy-vec2(cx,cy))/mr;
    vec2 p=uv-vec2(OFFSET_X,OFFSET_Y);
    float r=length(p);
    float theta=atan(p.y,p.x)/TAU+0.5;

    // 形态混合（uFormBlend）
    float v1a=evalForm(uForm, p, r, theta);
    float v1b=evalForm(uFormB, p, r, theta);
    float fsb=uFormBlend*uFormBlend*(3.0-2.0*uFormBlend);
    float v1=mix(v1a, v1b, fsb);

    // 第二族（交错）：固定反向螺旋
    float v2=sin(-ARM_COUNT*theta*TAU+log(max(r,0.01))*SPIRAL_GROWTH*30.0+uTime*uBpm/4800.0*uSpeed);

    // 多频细节
    int L=int(COMPLEXITY);
    for(int i=0;i<6;i++){ if(i>=L) break;
        float f=mix(FREQ_LOW,FREQ_HIGH,float(i)/max(float(L-1),1.0));
        v1+=sin(f*r*25.0+float(i)*1.7+uTime*CURVATURE)*0.45/float(i+1); }

    // 噪声
    vec2 np=uv*(BETA*4.0+1.0)+NOISE_SEED;
    float n=fbm(np);
    v1+=(n-0.5)*FRACTAL_DIM;

    // 线条渲染（管状高光）
    float d1=abs(v1), d2=abs(v2);
    float line1=smoothstep(THICKNESS+SHARPNESS*0.1, THICKNESS-SHARPNESS*0.1, d1);
    float line2=smoothstep(THICKNESS+SHARPNESS*0.1, THICKNESS-SHARPNESS*0.1, d2);
    float core1=1.0-smoothstep(0.0,THICKNESS,d1);
    float core2=1.0-smoothstep(0.0,THICKNESS,d2);
    float tube1=0.25+0.75*core1;
    float tube2=0.25+0.75*core2;
    float shade=clamp(line1*tube1+line2*tube2, 0.0, 1.0);

    // 配色
    vec3 col=mapColor(v1);
    vec3 bg=(uDarkBg>0)?vec3(0.02):vec3(0.97);
    col=mix(bg,col,shade);

    // 跨效果 morphing（图案级混合）
    float pat=clamp(v1,-1.0,1.0);
    if(uEncode>0){ fragColor=vec4(pat*0.5+0.5,col); return; }
    if(uMorphBlend>0.0 && uMorphBlend<1.0){
        vec2 tuv=gl_FragCoord.xy/uResolution;
        vec4 oldEnc=texture(uOldEncoded,tuv);
        float oldPat=oldEnc.r*2.0-1.0;
        vec3 oldCol=oldEnc.gba;
        float sb=uMorphBlend*uMorphBlend*(3.0-2.0*uMorphBlend);
        float blendedPat=oldPat*(1.0-sb)+pat*sb;
        vec3 newCol=mix(bg,mapColor(blendedPat),shade);
        col=mix(oldCol,newCol,sb);
    }

    col=cpuFinalize(col, gl_FragCoord.xy, uDitherStrength);
    fragColor=vec4(col, shade);  // alpha=线条掩码：背景透明、线条不透明
}
