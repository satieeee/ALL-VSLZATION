#version 330 core
// GlitchForge GPU — 1:1 port from CPU GlitchForge.cpp
in vec2 vUV; out vec4 fragColor;
uniform float uTime,uBpm,uBeatPhase,uSpeed; uniform vec2 uResolution;
uniform int uEncode,uDarkBg,uForm,uFormB,uColorScheme,uProjection;
uniform float uFormBlend;
uniform float uOpacity;
uniform sampler2D uOldEncoded;
uniform float uMorphBlend;
const float PI=3.14159265359,TAU=6.28318530718;

vec3 hsl2rgb(float h,float s,float l){
    h=mod(h,360.0);if(h<0.0)h+=360.0;
    float c=(1.0-abs(2.0*l-1.0))*s,hp=h/60.0,x=c*(1.0-abs(mod(hp,2.0)-1.0)),m=l-c*0.5;
    vec3 rgb;if(hp<1.0)rgb=vec3(c,x,0);else if(hp<2.0)rgb=vec3(x,c,0);
    else if(hp<3.0)rgb=vec3(0,c,x);else if(hp<4.0)rgb=vec3(0,x,c);
    else if(hp<5.0)rgb=vec3(x,0,c);else rgb=vec3(c,0,x);return rgb+m;
}

float basePattern(float r,float theta,float t,float speed){
    float a=theta*TAU,v=0;
    v+=sin(a*3.0+t*0.25*speed)*0.5;
    v+=sin(a*7.0-t*0.18*speed)*0.3;
    v+=cos(r*2.5+a*1.2+t*0.35*speed)*0.2;
    v+=sin(r*3.0+t*0.4*speed)*cos(a*1.5-t*0.3*speed)*0.25;
    v+=sin(r*6.0+a*4.0+sin(uTime*0.25)*speed)*0.12;
    return v;
}

void glitchDisplace(int m,float r,float theta,float t,float sp,
    inout float dr,inout float dt,inout float rR,inout float rB){
    float a=theta*TAU;dr=r;dt=theta;rR=0;rB=0;
    if(m==0){
        dr=r+sin(a*12.0+t*1.3*sp)*0.08+sin(a*28.0+t*1.7*sp)*0.04+cos(a*6.0-t*2.1*sp)*0.025;
        float spike=sin(a*50.0+sin(uTime*0.35)*sp);if(spike>0.92)dr+=(spike-0.92)*0.6;
        dr=clamp(dr,0.0,1.5);
    }else if(m==1){
        rR=sin(a*9.0+t*1.5*sp)*0.03+cos(a*15.0-t*2.0*sp)*0.015;
        rB=-rR*0.7+sin(a*7.0+t*1.9*sp)*0.01;
        dr=r+sin(a*5.0+t*0.8*sp)*0.015;
    }else if(m==2){
        float ba=floor(a*3.0/PI),br=floor(r*3.0);
        float ha=sin(ba*127.1+br*311.7+t*0.7*sp)*0.5+sin(ba*269.5+br*183.3-t*0.5*sp)*0.3+cos(ba*53.0-br*91.0+t*1.1*sp)*0.2;
        float hr=cos(ba*89.0+br*173.0+t*0.9*sp)*0.4+sin(ba*211.0-br*57.0+t*0.6*sp)*0.3;
        dr=clamp(r+hr*0.1,0.0,1.5);dt=theta+ha*0.015;if(abs(ha)>0.8)dt+=ha*0.025;
    }else if(m==3){
        dr=clamp(r+sin(a*5.0+t*1.0*sp)*0.06+cos(r*6.0+a*3.0+t*1.6*sp)*0.035+sin(r*10.0-a*7.0+sin(uTime*0.4)*sp)*0.025,0.0,1.5);
        dt=theta+(cos(r*8.0+t*1.2*sp)*0.035+sin(a*10.0+r*4.0-t*1.4*sp)*0.02)/TAU;
        rR=sin(a*13.0-t*1.8*sp)*0.012;rB=-rR;
    }
}

vec3 mapGlitchColor(int cs,float pat,float r,float theta,float t,float phase){
    float p=abs(pat);if(p<0.42)return uDarkBg>0?vec3(0.0):vec3(1.0);
    float T0=0.42,T1=0.56,T2=0.70,T3=0.84,T4=0.96,brightness;
    if(p<T1)brightness=(p-T0)/(T1-T0)*0.35;else if(p<T2)brightness=0.35+(p-T1)/(T2-T1)*0.35;
    else if(p<T3)brightness=0.70+(p-T2)/(T3-T2)*0.20;else if(p<T4)brightness=0.90+(p-T3)/(T4-T3)*0.08;
    else brightness=1.0;
    float bp=1.0-phase,sp=uBpm/120.0;brightness=clamp(brightness+bp*0.06,0.0,1.0);
    float hue,sat,light=0.92-brightness*0.22;
    if(cs==0){hue=105.0+brightness*50.0+bp*10.0;sat=brightness*0.5+0.1;}
    else if(cs==1){hue=brightness<0.5?115.0+brightness*30.0:270.0+(brightness-0.5)*40.0;sat=brightness*0.55+0.1;}
    else if(cs==2){hue=180.0+cos(r*15.0+t*sp)*30.0+bp*15.0;sat=brightness*0.5+0.1;}
    else if(cs==3){hue=3.0+brightness*50.0+bp*10.0;sat=brightness*0.6+0.1;}
    else if(cs==4){hue=mod(310.0+brightness*100.0+r*10.0+t*20.0*sp,360.0);sat=brightness*0.55+0.1;}
    else if(cs==5){hue=mod(theta*360.0+r*180.0+sin(uTime*0.15)*15.0+phase*40.0,360.0);sat=brightness*0.55+0.1;}
    return hsl2rgb(hue,clamp(sat,0.0,1.0),clamp(light,0.0,1.0));
}

void main(){
    float cx=uResolution.x*0.5,cy=uResolution.y*0.5,mr=min(cx,cy);
    vec2 uv=(gl_FragCoord.xy-vec2(cx,cy))/mr;
    float r=length(uv),th=atan(uv.y,uv.x);if(th<0.0)th+=TAU;
    float dw=10.0,wA=TAU/dw,wi=floor(th/wA),tw=th-wi*wA;
    if(mod(wi,2.0)>=1.0)tw=wA-tw;
    float theta=tw/wA;

    float sp=uSpeed,speed=uBpm/120.0*sp;
    float dr,dt,rDispR,rDispB;
    if(uFormBlend<=0.0){dr=r;dt=theta;rDispR=0;rDispB=0;glitchDisplace(uForm,r,theta,uTime,speed,dr,dt,rDispR,rDispB);}
    else if(uFormBlend>=1.0){dr=r;dt=theta;rDispR=0;rDispB=0;glitchDisplace(uFormB,r,theta,uTime,speed,dr,dt,rDispR,rDispB);}
    else{
        float sb=uFormBlend*uFormBlend*(3.0-2.0*uFormBlend);
        float dr1=r,dt1=theta,rR1=0,rB1=0,dr2=r,dt2=theta,rR2=0,rB2=0;
        glitchDisplace(uForm,r,theta,uTime,speed,dr1,dt1,rR1,rB1);
        glitchDisplace(uFormB,r,theta,uTime,speed,dr2,dt2,rR2,rB2);
        dr=dr1*(1.0-sb)+dr2*sb;dt=dt1*(1.0-sb)+dt2*sb;
        rDispR=rR1*(1.0-sb)+rR2*sb;rDispB=rB1*(1.0-sb)+rB2*sb;
    }

    float pat;
    vec3 col;
    if(abs(rDispR)>0.0005||abs(rDispB)>0.0005){
        float pR=basePattern(r+rDispR,theta,uTime,speed);
        float pG=basePattern(r,theta,uTime,speed);
        float pB=basePattern(r+rDispB,theta,uTime,speed);
        vec3 cR=mapGlitchColor(uColorScheme,pR,r+rDispR,theta,uTime,uBeatPhase);
        vec3 cG=mapGlitchColor(uColorScheme,pG,r,theta,uTime,uBeatPhase);
        vec3 cB=mapGlitchColor(uColorScheme,pB,r+rDispB,theta,uTime,uBeatPhase);
        col=vec3(cR.r,cG.g,cB.b);
        pat=(pR+pG+pB)/3.0;
    }else{
        pat=basePattern(dr,dt,uTime,speed);
        col=mapGlitchColor(uColorScheme,pat,dr,dt,uTime,uBeatPhase);
    }

    // === Pass 1 编码：R=pattern, GBA=颜色 ===
    if(uEncode>0){fragColor=vec4(pat*0.5+0.5,col);return;}

    // === Pass 2 跨效果 morphing（匹配 CPU TransitionEngine）===
    if(uMorphBlend>0.0&&uMorphBlend<1.0){
        vec2 tuv=gl_FragCoord.xy/uResolution;
        vec4 oldEnc=texture(uOldEncoded,tuv);
        float oldPat=oldEnc.r*2.0-1.0;
        vec3 oldCol=oldEnc.gba;
        float sb=uMorphBlend*uMorphBlend*(3.0-2.0*uMorphBlend);
        float blendedPat=oldPat*(1.0-sb)+pat*sb;
        vec3 newCol=mapGlitchColor(uColorScheme,blendedPat,dr,dt,uTime,uBeatPhase);
        col=mix(oldCol,newCol,sb);
    }

    // === GDI HALFTONE 模拟：边缘色散 ===
    // GDI SetStretchBltMode(HALFTONE) 低分辨率→全屏拉伸时
    // 对 RGB 三通道独立做 dither，高对比边缘处自然产生品红/紫色偏
    // 这里检测图案梯度，在梯度大的区域对各通道做微小扰动来模拟
    float gradEps=0.002;
    float gx=basePattern(clamp(dr+gradEps,0.0,1.5),dt,uTime,speed);
    float gy=basePattern(dr,clamp(dt+gradEps,0.0,1.0),uTime,speed);
    float edgeGrad=length(vec2(gx-pat,gy-pat))/gradEps;
    float fringe=smoothstep(0.05,0.32,edgeGrad)*0.018;
    if(fringe>0.0005){
        float h=fract(sin(dot(gl_FragCoord.xy,vec2(127.1,311.7)))*43758.5453);
        float pR=pat+(h-0.5)*fringe;
        float pB=pat+(fract(h*7.13)-0.5)*fringe;
        col.r=mix(col.r,mapGlitchColor(uColorScheme,pR,dr,dt,uTime,uBeatPhase).r,0.22);
        col.b=mix(col.b,mapGlitchColor(uColorScheme,pB,dr,dt,uTime,uBeatPhase).b,0.22);
    }

    col=cpuFinalize(col,gl_FragCoord.xy,uDitherStrength);
    fragColor=vec4(col,uOpacity);
}
