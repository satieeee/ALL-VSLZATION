#version 330 core
// Azurite_c GPU — Polar heightfield + Phong + kaleidoscope, 1:1 from CPU Azurite_c.cpp
in vec2 vUV; out vec4 fragColor;
uniform float uTime,uBpm,uBeatPhase,uSpeed; uniform vec2 uResolution;
uniform int uDarkBg,uForm,uFormB,uColorScheme,uProjection;
uniform float uFormBlend;
uniform float uOpacity;
uniform int uEncode;
uniform sampler2D uOldEncoded;
uniform float uMorphBlend;
const float PI=3.14159265359,TAU=6.28318530718,EPS_R=0.003,EPS_T=0.0008;

vec3 hsl2rgb(float h,float s,float l){
    h=mod(h,360.0);if(h<0.0)h+=360.0;
    float c=(1.0-abs(2.0*l-1.0))*s,hp=h/60.0,x=c*(1.0-abs(mod(hp,2.0)-1.0)),m=l-c*0.5;
    vec3 rgb;if(hp<1.0)rgb=vec3(c,x,0);else if(hp<2.0)rgb=vec3(x,c,0);
    else if(hp<3.0)rgb=vec3(0,c,x);else if(hp<4.0)rgb=vec3(0,x,c);
    else if(hp<5.0)rgb=vec3(x,0,c);else rgb=vec3(c,0,x);return rgb+m;
}

float heightField(int f,float r,float theta,float t,float speed){
    float a=theta*TAU;
    switch(f){
    case 0:return sin(r*6.0+t*0.3*speed)*0.5+sin(r*14.0-t*0.2*speed)*0.28+cos(r*22.0)*0.12;
    case 1:return sin(r*7.0+t*0.4*speed)*cos(a*4.0-t*0.35*speed)*0.5+sin(r*10.0-a*7.0+sin(uTime*0.2)*speed)*0.25;
    case 2:{float ba=floor(a*5.0/PI),br=floor(r*4.0);float h=sin(ba*127.1+br*311.7+t*0.7*speed)*0.55+sin(ba*269.5+br*183.3-t*0.5*speed)*0.35;return h+sin(r*4.0+t*0.3*speed)*0.1;}
    case 3:return sin(r*8.0+t*0.5*speed)*0.55+cos(r*16.0-t*0.3*speed)*0.25;
    case 4:return sin(r*6.0+t*0.3*speed)*sin(a*5.0+t*0.25*speed)*0.6+cos(r*12.0)*cos(a*10.0)*0.18;
    case 5:return sin(r*3.5+a*1.2+t*0.4*speed)*cos(r*4.0-a*0.8-t*0.5*speed)*0.42+sin(a*3.0+r*1.5+t*0.25*speed)*0.28+cos(r*7.0-a*3.0+t*0.45*speed)*0.18;
    }return 0.0;
}

vec3 shade(int cs,float h,float nr,float nt,float nz,float phase){
    float Lx=0.55,Ly=0.35,Lz=0.75,dotNL=max(0.0,nr*Lx+nt*Ly+nz*Lz);
    float Hx=Lx,Hy=Ly,Hz=Lz+1.0,hLen=sqrt(Hx*Hx+Hy*Hy+Hz*Hz);Hx/=hLen;Hy/=hLen;Hz/=hLen;
    float dotNH=max(0.0,nr*Hx+nt*Hy+nz*Hz);
    float lighting=clamp(0.1+dotNL*0.55+pow(dotNH,40.0)*0.28-(1.0-nz)*0.07,0.05,1.0);
    float sat=clamp(0.3+h*0.2+lighting*0.15,0.0,1.0),light=clamp(0.35+h*0.35+lighting*0.3,0.0,1.0);
    float hue;float bp=1.0-phase;
    if(cs==0)hue=215.0+h*15.0+bp*8.0;else if(cs==1)hue=185.0+h*20.0+bp*10.0;
    else if(cs==2)hue=240.0+h*12.0+bp*5.0;else if(cs==3)hue=200.0+h*18.0+bp*7.0;
    else if(cs==4)hue=160.0+h*25.0+bp*10.0;else hue=250.0+h*15.0+bp*6.0;
    return audioGrade(hsl2rgb(hue,sat,light));
}

void main(){
    float cx=uResolution.x*0.5,cy=uResolution.y*0.5,mr=min(cx,cy);
    vec2 uv=(gl_FragCoord.xy-vec2(cx,cy))/mr;
    float r=length(uv),th=atan(-uv.y,uv.x);if(th<0.0)th+=TAU;
    float dw=10.0,wA=TAU/dw,wi=floor(th/wA),tw=th-wi*wA;
    if(mod(wi,2.0)>=1.0)tw=wA-tw;
    float theta=tw/wA,r0=r;

    float speed=uBpm/120.0,spd=speed;
    float a=theta*TAU;
    switch(uProjection){
    case 0:break;case 1:{float spin=r*1.5,ang=a+spin+sin(uTime*0.3)*spd*0.3;r=clamp(r+sin(ang*2.0+sin(uTime*0.5)*spd)*0.01,0.0,1.0);theta=mod(ang/TAU,1.0);break;}
    case 2:{float wt=1.0-r*0.5;r=clamp(r*wt+sin(a*3.0+sin(uTime*0.3)*spd)*0.02,0.0,1.0);theta+=cos(r*4.0+sin(uTime*0.45)*spd)*0.03;break;}
    case 3:r+=sin(a*4.0+sin(uTime*0.3)*spd)*0.06;theta+=cos(r*5.0-sin(uTime*0.4)*spd)*0.04/TAU;break;
    case 4:theta+=cos(r*4.0+sin(uTime*0.35)*spd)*0.04/TAU;r=clamp(r+sin(a*5.0-sin(uTime*0.45)*spd)*0.06,0.0,1.0);break;
    case 5:{float d=(r+a*0.1)*0.7,e=(r-a*0.1)*0.7;r=clamp(d+sin(e*3.5+sin(uTime*0.5)*spd)*0.04,0.0,1.0);theta=(e+cos(d*4.5-sin(uTime*0.35)*spd)*0.03)/TAU;break;}
    }

    // === 音频驱动（与 CPU RenderChunk 1:1） ===
    theta += audioWave(uTime * 8.0, 0.05);           // 波形抖动 θ（弱）
    float tAudio = uTime * 0.85 + uAudioPhase;  // 高频驱动细节转速（弱）
    float audioAmp = audioPump(0.2);                 // 低频泵幅度（更弱）

    float h0,sb;if(uFormBlend<=0.0){h0=heightField(uForm,r,theta,tAudio,speed);sb=0.0;}else if(uFormBlend>=1.0){h0=heightField(uFormB,r,theta,tAudio,speed);sb=1.0;}else{sb=uFormBlend*uFormBlend*(3.0-2.0*uFormBlend);h0=heightField(uForm,r,theta,tAudio,speed)*(1.0-sb)+heightField(uFormB,r,theta,tAudio,speed)*sb;}
    float hr0=heightField(uForm,clamp(r+EPS_R,0.0,1.5),theta,tAudio,speed),hrB=heightField(uFormB,clamp(r+EPS_R,0.0,1.5),theta,tAudio,speed);
    float hr=mix(hr0,hrB,sb);
    float ht0=heightField(uForm,r,clamp(theta+EPS_T,0.0,1.0),tAudio,speed),htB=heightField(uFormB,r,clamp(theta+EPS_T,0.0,1.0),tAudio,speed);
    float ht=mix(ht0,htB,sb);
    float nr=-(hr-h0)/EPS_R,nt=-(ht-h0)/(EPS_T*(r0+0.01)),nz=1.0,nLen=sqrt(nr*nr+nt*nt+nz*nz);
    vec3 col=shade(uColorScheme,h0*audioAmp,nr/nLen,nt/nLen,nz/nLen,uBeatPhase); // 只泵高度，不动法线

    // === Pass 1 编码：R=heightField, GBA=颜色 ===
// === Pass 2 跨效果 morphing（匹配 CPU TransitionEngine）===
col=cpuFinalize(col,gl_FragCoord.xy,uDitherStrength);
    fragColor=vec4(col,uOpacity);
}
