#version 330 core
// Psychedelic GPU — 1:1 port from CPU Psychedelic.cpp
// EvalForm: 6 forms, constants unchanged
// MapColor: 6 schemes, T0/T1/T2/T3/T4 unchanged
// MapProjection: 6 projections, unchanged

in vec2 vUV;
out vec4 fragColor;

uniform float uTime, uBpm, uBeatPhase, uSpeed;
uniform vec2 uResolution;
uniform int uEncode, uDarkBg, uForm, uFormB, uColorScheme, uProjection;
uniform sampler2D uOldEncoded;
uniform float uMorphBlend;
uniform float uFormBlend;
uniform float uOpacity;
uniform int uInvert;

const float PI = 3.14159265359;
const float TAU = 6.28318530718;

vec3 hsl2rgb(float h, float s, float l) {
    h = mod(h, 360.0); if (h < 0.0) h += 360.0;
    float c = (1.0 - abs(2.0 * l - 1.0)) * s;
    float hp = h / 60.0;
    float x = c * (1.0 - abs(mod(hp, 2.0) - 1.0));
    float m = l - c * 0.5;
    vec3 rgb;
    if (hp < 1.0)      rgb = vec3(c, x, 0);
    else if (hp < 2.0) rgb = vec3(x, c, 0);
    else if (hp < 3.0) rgb = vec3(0, c, x);
    else if (hp < 4.0) rgb = vec3(0, x, c);
    else if (hp < 5.0) rgb = vec3(x, 0, c);
    else               rgb = vec3(c, 0, x);
    return rgb + m;
}

// —— EvalForm, 1:1 from CPU ——
float evalForm(int f, float r, float theta, float t, float speed) {
    float v = 0.0, a = theta * TAU;
    float s15 = sin(t * 0.15 * speed);
    float s20 = sin(t * 0.20 * speed);
    float s30 = sin(t * 0.30 * speed);

    switch (f) {
    case 0: { // VEINS
        float f1 = 0.375, f2 = 0.8125, f3 = 1.3125;
        v  = sin(r * 0.05 + a * f1 + t * 0.3 * speed) * cos(r * 0.069 - a * f2 + t * 0.45 * speed);
        v += sin(r * 0.088 + a * f3) * cos(r * 0.038 - a * (f1+f2+f3)/3.0 + t * 0.55 * speed);
        v += cos(r * 0.056 + a * f1 * 0.5 - t * 0.35 * speed) * sin(r * 0.081 - a * f2 * 0.7 + t * 0.5 * speed);
        v += sin(r * 0.106 + a * f3 * 0.4 + t * 0.25 * speed) * cos(r * 0.031 - a * (f1+f3) * 0.6) * 0.7;
        return v / 3.2;
    }
    case 1: { // MYCELIUM
        float p1=0.5, p2=1.0625, p3=1.8125;
        v  = sin(r*0.075 + a*p1 + t*0.4*speed) * sin(r*0.056 - a*p2 - t*0.35*speed);
        v += cos(r*0.094 + a*p3) * cos(r*0.044 - a*p1 + t*0.5*speed);
        v += sin(r*0.125 + a*(p1+p2)/2.0 + t*0.3*speed) * cos(r*0.069 - a*(p2+p3)/2.0) * 0.8;
        v += sin(r*0.05 + a*p3*0.5 - t*0.45*speed) * sin(r*0.088 + a*p1*0.7) * 0.6;
        return v / 3.6;
    }
    case 2: { // TENDRILS
        float w1=0.3125, w2=0.875, w3=1.4375;
        v  = sin(r*0.025 + a*w1 + t*0.25*speed) * cos(r*0.038 - a*w2*0.5);
        v += sin(r*0.056 + a*w2 + t*0.4*speed) * cos(r*0.044 - a*w1*1.5 + t*0.3*speed) * 0.8;
        v += sin(r*0.081 + a*w3*0.7 + t*0.5*speed) * cos(r*0.063 + a*(w1+w2)/2.0) * 0.6;
        v += sin(r*0.1 - a*w3*0.4 + t*0.35*speed) * sin(r*0.031 + a*w2*0.8 - t*0.45*speed) * 0.5;
        return v / 2.9;
    }
    case 3: { // STARBURST
        float rays = 0.5 + s20 * 0.1875;
        v  = cos(a*rays + sin(r*0.125 + t*0.5*speed)*0.156) * (1.0 - r*0.025);
        v += sin(r*0.156 + a*0.375 + t*0.6*speed) * 0.4;
        v += sin(r*0.113 - a*rays*0.7) * cos(r*0.038 + t*0.4*speed) * 0.5;
        v += cos(r*0.031 + a*rays + t*0.3*speed) * r * 0.038;
        return v / 2.5;
    }
    case 4: { // MANDALA
        float petals = 0.313 + s15 * 0.125;
        v  = sin(r*0.044 + t*0.3*speed) * cos(a*petals + r*0.013);
        v += sin(r*0.063 - t*0.45*speed) * cos(a*petals*0.6 + r*0.009);
        v += sin(r*0.088 + a*0.3125 + t*0.35*speed) * 0.5;
        v += cos(r*0.025 - a*petals*0.8) * sin(r*0.053 + t*0.5*speed) * 0.6;
        return v / 2.7;
    }
    case 5: { // FRACTAL
        float br = 0.156 + s30 * 0.094;
        v  = sin(r*0.063 + a*br + t*0.2*speed) * cos(r*0.031 - a*br*1.5);
        v += sin(r*0.125 + a*br*2.5 + t*0.4*speed) * cos(r*0.075 - a*0.25) * 0.7;
        v += sin(r*0.188 + a*br*3.5 + t*0.5*speed) * cos(r*0.113 + a*0.438) * 0.4;
        return v / 2.1;
    }
    default: return 0.0;
    }
}

// —— MapProjection, 1:1 from CPU ——
void mapProjection(int proj, inout float r, inout float theta, float spd) {
    float a = theta * TAU;
    float sr30=sin(uTime*0.30), sr20=sin(uTime*0.20), sr40=sin(uTime*0.40);
    float sr50=sin(uTime*0.50), sr60=sin(uTime*0.60);

    switch (proj) {
    case 0: break; // RADIAL
    case 1: { // LINEAR_WATER
        float ang = 0.7 + sr30*0.3;
        float rx = r*cos(a)*cos(ang) - r*sin(a)*sin(ang);
        float ry = r*cos(a)*sin(ang) + r*sin(a)*cos(ang);
        r = ry*0.5 + 0.5;
        float streams = 6.0 + sr20*3.0;
        theta = rx*0.5 + 0.5 + sin(ry*streams + sr50*spd)*0.15;
        theta += cos(rx*streams*1.7 + ry*3.0 - sr40*spd)*0.08;
        break;
    }
    case 2: { // SPIRAL_IN
        float spin = (1.0-r)*3.0, ang2 = a + spin + sr30*spd;
        r += sin(ang2*4.0 + sr60*spd)*0.02;
        if (r>1.0) r=1.0; if (r<0.0) r=0.0;
        theta = mod(ang2/TAU, 1.0); break;
    }
    case 3: { // SPIRAL_OUT
        float spin = r*3.5, ang2 = a + spin + sr30*spd;
        r += cos(ang2*3.0 - sr50*spd)*0.03;
        if (r>1.0) r=1.0; if (r<0.0) r=0.0;
        theta = mod(ang2/TAU, 1.0); break;
    }
    case 4: { // ZIGZAG
        float sweep = sr40*spd, saw = mod(r*cos(a)*8.0 + sweep, 2.0);
        if (saw>1.0) saw = 2.0 - saw;
        r = r*sin(a)*0.5 + 0.5;
        theta = saw*0.7 + sin(r*sin(a)*6.0 + sr50*spd)*0.2 + cos(r*cos(a)*12.0 - r*sin(a)*3.0 + sr30*spd)*0.1;
        theta = mod(theta, 1.0); if (theta<0.0) theta+=1.0; break;
    }
    case 5: { // TUNNEL_DEPTH
        float dist = sqrt(r*cos(a)*r*cos(a) + r*sin(a)*r*sin(a));
        float depth = 1.0/(dist + 0.15);
        float ang2 = a + depth*0.3 + sr50*spd;
        r = mod(depth*0.15 + sr20*spd, 1.0);
        r += sin(depth*0.5 + sr60*spd)*0.05;
        theta = mod(ang2/TAU, 1.0); if (theta<0.0) theta+=1.0; break;
    }
    }
}

// —— MapColor, 1:1 from CPU ——
vec3 mapColor(int cs, float pattern, float r, float theta, float phase) {
    float p = abs(pattern), bp = 1.0 - phase;
    const float T0=0.20, T1=0.38, T2=0.56, T3=0.74, T4=0.92;
    float brightness;

    if (p < T0) {
        float w = p/T0; w = w*w*w;
        brightness = w * 0.25;
        float hue, sat, light;
        switch (cs) {
        default:
        case 0: hue=115.0+brightness*25.0+bp*15.0; sat=brightness*0.5; light=0.95-brightness*0.12; break;
        case 1: hue=120.0+brightness*180.0; sat=brightness*0.5; light=0.93-brightness*0.10; break;
        case 2: hue=175.0+brightness*40.0; sat=brightness*0.45; light=0.95-brightness*0.12; break;
        case 3: hue=3.0+brightness*47.0; sat=brightness*0.55; light=0.93-brightness*0.10; break;
        case 4: hue=255.0+brightness*60.0; sat=brightness*0.5; light=0.95-brightness*0.12; break;
        case 5: hue=mod(theta*360.0+sin(uTime*0.15)*15.0+phase*40.0, 360.0); sat=brightness*0.5; light=0.93-brightness*0.10; break;
        }
        sat=clamp(sat,0.0,1.0); light=clamp(light,0.0,1.0);
        vec3 col = hsl2rgb(hue,sat,light);
        col = mix(uDarkBg>0?vec3(0.0):vec3(1.0), col, w);
        return audioGrade(col);   // 音频颜色调制：能量亮度 + 频段染色 + 鼓点闪光
    }
    else if (p<T1) brightness=(p-T0)/(T1-T0)*0.30;
    else if (p<T2) brightness=0.30+(p-T1)/(T2-T1)*0.35;
    else if (p<T3) brightness=0.65+(p-T2)/(T3-T2)*0.25;
    else if (p<T4) brightness=0.90+(p-T3)/(T4-T3)*0.10;
    else brightness=1.0;
    brightness+=bp*0.06; brightness=clamp(brightness,0.0,1.0);

    float hue, sat, light;
    switch (cs) {
    default:
    case 0: hue=115.0+brightness*25.0+bp*15.0; sat=brightness*0.7+0.15; light=0.92-brightness*0.20; break;
    case 1: hue=120.0+brightness*180.0; sat=brightness*0.75+0.10; light=0.90-brightness*0.18; break;
    case 2: hue=175.0+brightness*40.0; sat=brightness*0.65+0.15; light=0.92-brightness*0.22; break;
    case 3: hue=3.0+brightness*47.0; sat=brightness*0.85+0.10; light=0.90-brightness*0.20; break;
    case 4: hue=255.0+brightness*60.0; sat=brightness*0.70+0.15; light=0.92-brightness*0.20; break;
    case 5: hue=mod(theta*360.0+sin(uTime*0.15)*15.0+phase*40.0, 360.0); sat=brightness*0.80+0.10; light=0.90-brightness*0.18; break;
    }
    sat=clamp(sat,0.0,1.0); light=clamp(light,0.0,1.0);
    return audioGrade(hsl2rgb(hue,sat,light));   // 音频颜色调制：能量亮度 + 频段染色 + 鼓点闪光
}

void main() {
    float cx = uResolution.x * 0.5, cy = uResolution.y * 0.5;
    float mr = min(cx, cy);
    vec2 uv = (gl_FragCoord.xy - vec2(cx, cy)) / mr;

    // 纯笛卡尔：r=x, theta=y（不使用万花筒折叠）
    float r = uv.x;
    float theta = uv.y;

    float speed = uBpm / 120.0;
    mapProjection(uProjection, r, theta, speed);

    // === 音频驱动（与 CPU RenderChunk 1:1） ===
    theta += audioWave(uTime * 8.0 + r * 0.5, 0.05);       // 波形抖动 θ（弱）
    float tAudio = uTime * 0.85 + uAudioPhase;         // 高频驱动细节转速（弱）
    float audioAmp = audioPump(0.2);                         // 低频泵幅度（更弱）

    float pat;
    if (uFormBlend <= 0.0) pat = evalForm(uForm, r, theta, tAudio, speed);
    else if (uFormBlend >= 1.0) pat = evalForm(uFormB, r, theta, tAudio, speed);
    else { float sb = uFormBlend*uFormBlend*(3.0-2.0*uFormBlend);
        pat = evalForm(uForm, r, theta, tAudio, speed)*(1.0-sb)
            + evalForm(uFormB, r, theta, tAudio, speed)*sb; }
    pat *= audioAmp;

    // 提前计算颜色（CPU版 TransitionEngine 在 encode/morph 之前就有 col）
    vec3 col = mapColor(uColorScheme, pat, r, theta, uBeatPhase);

    // === Pass 1 编码：存储旧效果的 图案+颜色 到 FBO ===
    // R=pattern[0,1], GBA=oldColor
// === Pass 2 跨效果 morphing：匹配 CPU TransitionEngine 逻辑 ===
    // CPU: blendedPat=oldPat*(1-t)+newPat*t
    //      oldCol=oldEffect.MapToColor(blendedPat)
    //      newCol=newEffect.MapToColor(blendedPat)
    //      pixel=LerpRGBA(oldCol,newCol,t)
        col = cpuFinalize(col, gl_FragCoord.xy, uDitherStrength);
    fragColor = vec4(col, uOpacity);
}
