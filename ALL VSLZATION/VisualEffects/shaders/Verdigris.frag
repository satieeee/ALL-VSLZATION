#version 330 core
// Verdigris GPU — 1:1 port from CPU Verdigris.cpp
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

float evalForm(int f,float r,float theta,float t,float speed){
    float a=theta*TAU,v=0;
    switch(f){
    case 0:{float w1=0.4,w2=0.9,w3=1.5;
        v=sin(r*0.06+a*w1+t*0.5*speed)*cos(r*0.08-a*w2+t*0.6*speed);
        v+=cos(r*0.10+a*w3+t*0.35*speed)*0.55;
        v+=sin(r*0.07-a*w1*0.6-t*0.55*speed)*0.4;
        v+=cos(r*0.12+a*w2*0.5+t*0.4*speed)*0.3;return v/2.2;}
    case 1:{float w1=0.5,w2=1.1,w3=1.8;
        v=sin(r*0.05+a*w1+t*0.45*speed)*cos(r*0.07-a*w2-t*0.55*speed);
        v+=sin(r*0.09+a*w3+t*0.3*speed)*0.5;
        v+=cos(r*0.06-a*w1*0.7+t*0.6*speed)*0.4;
        v+=sin(r*0.11+a*w2*0.4-t*0.5*speed)*0.3;return v/2.3;}
    case 2:{float petals=0.4+sin(t*0.20*speed)*0.15;
        v=cos(a*petals+sin(r*0.08+t*0.5*speed)*0.15)*(1.0-r*0.10);
        v+=sin(r*0.06+a*0.45+t*0.55*speed)*0.4;
        v+=sin(r*0.05-a*petals*0.7)*cos(r*0.04+t*0.4*speed)*0.35;
        v+=cos(r*0.03+a*petals*1.3)*sin(r*0.07+t*0.6*speed)*0.25;return v/2.0;}
    case 3:{float ang=0.7+sin(uTime*0.25*uSpeed)*0.25;
        float rx=r*cos(a+ang),ry=r*sin(a+ang);
        v=sin(rx*0.10+ry*0.08+t*0.45*speed);
        v+=cos(rx*0.14-ry*0.11-t*0.55*speed)*0.55;
        v+=sin(rx*0.07+ry*0.13+t*0.35*speed)*0.4;
        v+=cos(rx*0.17-ry*0.05+t*0.5*speed)*0.3;return v/2.1;}
    case 4:{float w1=0.35,w2=0.8,w3=1.4;
        v=sin(r*0.04+t*0.35*speed)*cos(r*0.07-a*w1);
        v+=sin(r*0.06+a*w2+t*0.45*speed)*0.5;
        v+=cos(r*0.05-a*w3-t*0.55*speed)*0.4;
        v+=sin(r*0.09+a*w1*1.5+t*0.3*speed)*0.3;return v/2.0;}
    case 5:{float w1=0.6,w2=1.2,w3=2.0;
        v=sin(r*0.06+a*w1+t*0.5*speed)*cos(r*0.08-a*w2-t*0.6*speed);
        v+=sin(r*0.10+a*w3+t*0.35*speed)*0.5;
        v+=cos(r*0.07+a*w1*0.8-t*0.45*speed)*0.4;
        v+=sin(r*0.12-a*w2*0.5+t*0.55*speed)*0.3;return v/2.4;}
    }return 0.0;
}

void mapProjection(int proj,inout float r,inout float theta,float spd){
    float a=theta*TAU;
    switch(proj){
    case 0:break;
    case 1:{float spin=r*1.5,ang=a+spin+sin(uTime*0.30)*spd*0.3;
        r=clamp(r+sin(ang*2.0+sin(uTime*0.50)*spd)*0.01,0.0,1.0);theta=mod(ang/TAU,1.0);break;}
    case 3:{float wt=1.0-r*0.5;r=clamp(r*wt+sin(a*3.0+sin(uTime*0.30)*spd)*0.02,0.0,1.0);
        theta+=cos(r*4.0+sin(uTime*0.45)*spd)*0.03;break;}
    case 4:break;
    case 5:break;
    case 6:{float wave=sin(r*cos(a)*2.5+sin(uTime*0.50)*spd)*0.06+cos(r*sin(a)*3.0-sin(uTime*0.35)*spd)*0.04;
        r=clamp(r+wave,0.0,1.0);theta+=sin(r*5.0+sin(uTime*0.25)*spd)*0.05;break;}
    }
}

vec3 mapColor(int cs,float pattern,float r,float theta,float phase){
    float p=abs(pattern),bp=1.0-phase;const float T0=0.25,T1=0.44,T2=0.62,T3=0.79,T4=0.94;float brightness;
    if(p<T0){float w=p/T0;w=w*w*w;brightness=w*0.2;
        float hue,sat,light;
        if(cs==0){hue=215.0+brightness*25.0+bp*10.0;sat=brightness*0.3;light=0.95-brightness*0.1;}
        else if(cs==1){hue=117.0+brightness*30.0+bp*12.0;sat=brightness*0.28;light=0.95-brightness*0.1;}
        else if(cs==2){hue=200.0+brightness*20.0+bp*6.0;sat=brightness*0.22;light=0.94-brightness*0.09;}
        else if(cs==3){hue=160.0+brightness*35.0+bp*14.0;sat=brightness*0.26;light=0.96-brightness*0.08;}
        else if(cs==4){hue=210.0+brightness*22.0+bp*8.0;sat=brightness*0.35;light=0.93-brightness*0.12;}
        else if(cs==5){hue=93.0+brightness*35.0+bp*12.0;sat=brightness*0.25;light=0.94-brightness*0.1;}
        return mix(uDarkBg>0?vec3(0.0):vec3(1.0),hsl2rgb(hue,clamp(sat,0.0,1.0),clamp(light,0.0,1.0)),w);}
    else{if(p<T1)brightness=(p-T0)/(T1-T0)*0.25;else if(p<T2)brightness=0.25+(p-T1)/(T2-T1)*0.3;
    else if(p<T3)brightness=0.55+(p-T2)/(T3-T2)*0.25;else if(p<T4)brightness=0.8+(p-T3)/(T4-T3)*0.15;
    else brightness=1.0;brightness=clamp(brightness+bp*0.04,0.0,1.0);
    float hue,sat,light;
    if(cs==0){hue=215.0+brightness*25.0+bp*10.0;sat=brightness*0.35+0.12;light=0.82-brightness*0.32;}
    else if(cs==1){hue=117.0+brightness*30.0+bp*12.0;sat=brightness*0.32+0.1;light=0.82-brightness*0.32;}
    else if(cs==2){hue=200.0+brightness*20.0+bp*6.0;sat=brightness*0.25+0.08;light=0.8-brightness*0.3;}
    else if(cs==3){hue=160.0+brightness*35.0+bp*14.0;sat=brightness*0.3+0.1;light=0.85-brightness*0.26;}
    else if(cs==4){hue=210.0+brightness*22.0+bp*8.0;sat=brightness*0.45+0.15;light=0.78-brightness*0.36;}
    else if(cs==5){hue=93.0+brightness*35.0+bp*12.0;sat=brightness*0.3+0.1;light=0.8-brightness*0.3;}
    return hsl2rgb(hue,clamp(sat,0.0,1.0),clamp(light,0.0,1.0));}
}

void main(){
    float cx=uResolution.x*0.5,cy=uResolution.y*0.5,mr=min(cx,cy);
    vec2 uv=(gl_FragCoord.xy-vec2(cx,cy))/mr;
    float r=length(uv),th=atan(uv.y,uv.x);if(th<0.0)th+=TAU;
    float dw=10.0,wA=TAU/dw,wi=floor(th/wA),tw=th-wi*wA;
    if(mod(wi,2.0)>=1.0)tw=wA-tw;
    float theta=tw/wA;
    float speed=uBpm/120.0*uSpeed;
    mapProjection(uProjection,r,theta,speed);
    float pat;if(uFormBlend<=0.0)pat=evalForm(uForm,r,theta,uTime,speed);else if(uFormBlend>=1.0)pat=evalForm(uFormB,r,theta,uTime,speed);else{float sb=uFormBlend*uFormBlend*(3.0-2.0*uFormBlend);pat=evalForm(uForm,r,theta,uTime,speed)*(1.0-sb)+evalForm(uFormB,r,theta,uTime,speed)*sb;}
    vec3 col=mapColor(uColorScheme,pat,r,theta,uBeatPhase);

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
        vec3 newCol=mapColor(uColorScheme,blendedPat,r,theta,uBeatPhase);
        col=mix(oldCol,newCol,sb);}

    col=cpuFinalize(col,gl_FragCoord.xy,uDitherStrength);
    fragColor=vec4(col,uOpacity);
}
