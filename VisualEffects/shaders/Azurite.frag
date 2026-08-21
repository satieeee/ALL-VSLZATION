#version 330 core
// Azurite GPU — Cartesian heightfield + Phong, 1:1 from CPU Azurite.cpp
in vec2 vUV; out vec4 fragColor;
uniform float uTime,uBpm,uBeatPhase,uSpeed; uniform vec2 uResolution;
uniform int uDarkBg,uForm,uFormB,uColorScheme,uProjection;
uniform float uFormBlend;
uniform float uOpacity;
uniform int uEncode;
uniform sampler2D uOldEncoded;
uniform float uMorphBlend;
const float PI=3.14159265359,EPS=0.002;

vec3 hsl2rgb(float h,float s,float l){
    h=mod(h,360.0);if(h<0.0)h+=360.0;
    float c=(1.0-abs(2.0*l-1.0))*s,hp=h/60.0,x=c*(1.0-abs(mod(hp,2.0)-1.0)),m=l-c*0.5;
    vec3 rgb;if(hp<1.0)rgb=vec3(c,x,0);else if(hp<2.0)rgb=vec3(x,c,0);
    else if(hp<3.0)rgb=vec3(0,c,x);else if(hp<4.0)rgb=vec3(0,x,c);
    else if(hp<5.0)rgb=vec3(x,0,c);else rgb=vec3(c,0,x);return rgb+m;
}

float heightField(int f,float x,float y,float t,float speed){
    switch(f){
    case 0:return sin(y*5.0+t*0.3*speed)*0.5+sin(y*12.0-t*0.2*speed)*0.3+cos(y*20.0)*0.12;
    case 1:return sin(x*6.0+t*0.4*speed)*cos(y*5.0-t*0.35*speed)*0.55+sin(x*9.0-y*8.0+sin(uTime*0.2*uSpeed)*speed)*0.25;
    case 2:{float bx=floor(x*5.0),by=floor(y*5.0);float h=sin(bx*127.1+by*311.7+t*0.7*speed)*0.6+sin(bx*269.5+by*183.3-t*0.5*speed)*0.4;return h+sin(x*3.0+t*0.3*speed)*0.12;}
    case 3:{float r=length(vec2(x,y));return sin(r*8.0+t*0.5*speed)*0.55+cos(r*15.0-t*0.3*speed)*0.25;}
    case 4:return sin(x*5.5+t*0.3*speed)*sin(y*5.5+t*0.25*speed)*0.65+cos(x*11.0)*cos(y*11.0)*0.2;
    case 5:return sin(x*3.0+y*1.5+t*0.4*speed)*cos(y*3.5-x*1.0-t*0.5*speed)*0.45+sin(y*5.0+x*2.0+t*0.25*speed)*0.3+cos(x*6.0-y*4.0+t*0.45*speed)*0.2;
    }return 0.0;
}

vec3 shade(int cs,float h,float nx,float ny,float nz,float phase){
    float Lx=0.55,Ly=0.35,Lz=0.75,dotNL=max(0.0,nx*Lx+ny*Ly+nz*Lz);
    float Hx=Lx,Hy=Ly,Hz=Lz+1.0,hLen=sqrt(Hx*Hx+Hy*Hy+Hz*Hz);Hx/=hLen;Hy/=hLen;Hz/=hLen;
    float dotNH=max(0.0,nx*Hx+ny*Hy+nz*Hz);
    float lighting=clamp(0.1+dotNL*0.55+pow(dotNH,40.0)*0.28-(1.0-nz)*0.07,0.05,1.0);
    float sat=clamp(0.3+h*0.2+lighting*0.15,0.0,1.0),light=clamp(0.35+h*0.25+lighting*0.3,0.0,1.0);
    float hue;float bp=1.0-phase;
    if(cs==0)hue=215.0+h*15.0+bp*8.0;else if(cs==1)hue=185.0+h*20.0+bp*10.0;
    else if(cs==2)hue=240.0+h*12.0+bp*5.0;else if(cs==3)hue=200.0+h*18.0+bp*7.0;
    else if(cs==4)hue=160.0+h*25.0+bp*10.0;else hue=250.0+h*15.0+bp*6.0;
    return hsl2rgb(hue,sat,light);
}

void main(){
    float cx=uResolution.x*0.5,cy=uResolution.y*0.5,mr=min(cx,cy);
    vec2 uv=(gl_FragCoord.xy-vec2(cx,cy))/mr;
    float x=uv.x,y=-uv.y,speed=uBpm/120.0*uSpeed,spd=speed;
    // projection (1:1 from CPU)
    switch(uProjection){
    case 0:break;case 1:x+=sin(y*4.0+sin(uTime*0.3)*spd)*0.08;y=y*0.8+cos(x*5.0-sin(uTime*0.4)*spd)*0.06;break;
    case 2:y+=cos(x*4.0+sin(uTime*0.35)*spd)*0.08;x=x*0.8+sin(y*5.0-sin(uTime*0.45)*spd)*0.06;break;
    case 3:{float d=(x+y)*0.7,e=(x-y)*0.7;x=d+sin(e*3.5+sin(uTime*0.5)*spd)*0.05;y=e+cos(d*4.5-sin(uTime*0.35)*spd)*0.04;break;}
    case 4:{float ang=0.5+sin(uTime*0.25)*0.3,rx=x*cos(ang)-y*sin(ang),ry=x*sin(ang)+y*cos(ang);x=rx+sin(ry*3.0+sin(uTime*0.4)*spd)*0.04;y=ry+cos(rx*4.0-sin(uTime*0.3)*spd)*0.03;break;}
    case 5:x+=sin(y*5.0+sin(uTime*0.5)*spd)*0.06+cos(x*7.0-sin(uTime*0.3)*spd)*0.04;y+=cos(x*5.0+sin(uTime*0.4)*spd)*0.05+sin(y*7.0-sin(uTime*0.35)*spd)*0.04;break;
    }
    float h0,sb;if(uFormBlend<=0.0){h0=heightField(uForm,x,y,uTime,speed);sb=0.0;}else if(uFormBlend>=1.0){h0=heightField(uFormB,x,y,uTime,speed);sb=1.0;}else{sb=uFormBlend*uFormBlend*(3.0-2.0*uFormBlend);h0=heightField(uForm,x,y,uTime,speed)*(1.0-sb)+heightField(uFormB,x,y,uTime,speed)*sb;}
    float hx0=heightField(uForm,x+EPS,y,uTime,speed),hxB=heightField(uFormB,x+EPS,y,uTime,speed);
    float hx=mix(hx0,hxB,sb);
    float hy0=heightField(uForm,x,y+EPS,uTime,speed),hyB=heightField(uFormB,x,y+EPS,uTime,speed);
    float hy=mix(hy0,hyB,sb);
    float nx=-(hx-h0)/EPS,ny=-(hy-h0)/EPS,nz=1.0,nLen=sqrt(nx*nx+ny*ny+nz*nz);
    vec3 col=shade(uColorScheme,h0,nx/nLen,ny/nLen,nz/nLen,uBeatPhase);

    // === Pass 1 编码：R=heightField, GBA=颜色 ===
    if(uEncode>0){fragColor=vec4(h0*0.5+0.5,col);return;}

    // === Pass 2 跨效果 morphing（匹配 CPU TransitionEngine）===
    if(uMorphBlend>0.0&&uMorphBlend<1.0){
        vec2 tuv=gl_FragCoord.xy/uResolution;
        vec4 oldEnc=texture(uOldEncoded,tuv);
        float oldPat=oldEnc.r*2.0-1.0;
        vec3 oldCol=oldEnc.gba;
        float sb=uMorphBlend*uMorphBlend*(3.0-2.0*uMorphBlend);
        float blendedPat=oldPat*(1.0-sb)+h0*sb;
        vec3 newCol=shade(uColorScheme,blendedPat,nx/nLen,ny/nLen,nz/nLen,uBeatPhase);
        col=mix(oldCol,newCol,sb);
    }

    col=cpuFinalize(col,gl_FragCoord.xy,uDitherStrength);
    fragColor=vec4(col,uOpacity);
}
