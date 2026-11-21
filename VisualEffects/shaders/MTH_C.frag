// MTH_C.frag — 种子驱动线条生成器（笛卡尔版，禁止中心/轴对称）
// uForm: 0=ellipse, 1=cubic, 2=cardioid, 3=star；uProjection=fieldType；uColorScheme=配色

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

const float TWIST = 0.75;
const float FREQ_LOW = 0.05;
const float FREQ_HIGH = 3.20;
const float CURVATURE = 0.45;
const float COMPLEXITY = 5.0;
const float THICKNESS = 0.25;
const float SHARPNESS = 0.85;
const float OFFSET_X = -0.05;
const float OFFSET_Y = 0.05;
const float ROTATION = 0.0;
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
float evalForm(int f, vec2 p) {
    float breathe=1.0+0.10*sin(uTime*0.9)+0.06*cos(uTime*0.6);
    float beatPulse=1.0+0.12*sin(uBeatPhase*TAU/3.0);
    float scale=breathe*beatPulse;
    float aRad=(ROTATION+30.0)*PI/180.0;
    float ca=cos(aRad), sa=sin(aRad);
    float wx=(p.x+OFFSET_X+0.30)*scale;
    float wy=(p.y+OFFSET_Y-0.20)*scale;
    float tx=wx*ca-wy*sa+0.4*wy;
    float ty=wx*sa+wy*ca;
    if(uProjection==0){ tx*=(1.0+TWIST*0.3); ty*=(1.0+TWIST*0.3); }
    else if(uProjection==1){ tx*=(1.0-TWIST*0.3); ty*=(1.0-TWIST*0.3); }
    else if(uProjection==2){ tx+=TWIST*sin(ty*FREQ_LOW*4.0); ty+=TWIST*sin(tx*FREQ_HIGH*4.0); }
    else if(uProjection==3){ tx=tx*ty; }
    tx+=TWIST*sin(ty*FREQ_LOW*8.0+1.3+uTime*uBpm/240.0);
    ty+=TWIST*cos(tx*FREQ_HIGH*7.0-0.7+uTime*uBpm/240.0);
    if(f==0)      return tx*tx+0.6*ty*ty+0.4*tx*ty+0.25*tx-0.15*ty-0.20;
    else if(f==1) return tx*tx*tx+0.8*ty*ty*ty+0.5*tx*ty+0.3*tx-0.18;
    else if(f==2){ float r2=tx*tx+ty*ty; return r2*r2-0.5*tx*r2-0.06*ty*ty+0.06*tx; }
    else if(f==3){ float sx=sign(tx)*pow(abs(tx),0.6); float sy=sign(ty)*pow(abs(ty),0.6); return sx+sy-0.35+0.1*tx; }
    else { float a=0.60,b=0.25,c=0.35;
           float r2=tx*tx+ty*ty;
           float ring=r2-0.70*tx-0.10*ty-0.50;
           float weave=a*tx*tx*ty+b*tx*ty*ty-c*tx*tx*tx;
           return ring+0.55*weave+0.30*tx-0.22*ty; }
}

// 配色函数
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
    return audioGrade(hsl2rgb(hue,sat,lig));   // 音频颜色调制：能量亮度 + 频段染色 + 鼓点闪光（与 CPU MapToColor 1:1）
}

void main(){
    float cx=uResolution.x*0.5, cy=uResolution.y*0.5;
    float mr=min(cx,cy);
    vec2 uv=(gl_FragCoord.xy-vec2(cx,cy))/mr;
    vec2 p=uv-vec2(OFFSET_X,OFFSET_Y);
    float r=length(p);

    // 形态混合（uFormBlend）
    float v1a=evalForm(uForm, p);
    float v1b=evalForm(uFormB, p);
    float fsb=uFormBlend*uFormBlend*(3.0-2.0*uFormBlend);
    float v1=mix(v1a, v1b, fsb);
    float v2=100.0;   // 笛卡尔无第二族

    // 多频细节
    int L=int(COMPLEXITY);
    for(int i=0;i<6;i++){ if(i>=L) break;
        float f=mix(FREQ_LOW,FREQ_HIGH,float(i)/max(float(L-1),1.0));
        v1+=sin(f*r*25.0+float(i)*1.7+uTime*CURVATURE)*0.45/float(i+1); }

    // 噪声
    vec2 np=uv*(BETA*4.0+1.0)+NOISE_SEED;
    float n=fbm(np);
    v1+=(n-0.5)*FRACTAL_DIM;

    // 音频驱动（与 CPU RenderChunk 1:1）：低频泵幅度调制最终图案标量
    float audioAmp = audioPump(0.2);
    v1 *= audioAmp;

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
col=cpuFinalize(col, gl_FragCoord.xy, uDitherStrength);
    fragColor=vec4(col, shade);  // alpha=线条掩码：背景透明、线条不透明
}
