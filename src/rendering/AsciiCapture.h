#pragma once
#include <GL/glew.h>
#include <vector>
#include <stdexcept>
#include <fstream>
#include <algorithm>

// All cameras share the ASCII pass. Export stays fixed at 768 x 288.
// Debug geometry and HUD are composited only in the interactive preview.
namespace AsciiCapture {
constexpr int Width=768, Height=288, FPS=20;
constexpr int MinCell=4, MaxCell=32, DefaultCell=4;
inline const char* vs=R"GLSL(#version 330 core
out vec2 uv;
void main(){ vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2)); uv=p; gl_Position=vec4(p*2.0-1.0,0,1); }
)GLSL";
inline const char* fs=R"GLSL(#version 330 core
in vec2 uv;
uniform sampler2D uImage;
uniform int uCell;
uniform int uMode; // 0 ascii, 1 display rgba over light/dark
uniform bool uDark;
uniform bool uAmbient;
uniform float uForegroundOpacity;
out vec4 frag;
// 5x7 glyphs: . : + x o # % @. Distinct glyphs within each tone band.
int glyph(int n,int y){
    const int rows[56]=int[56](0,0,0,0,0,4,4,
        0,4,4,0,4,4,0,
        0,4,4,31,4,4,0,
        0,0,17,10,4,10,17,
        0,0,14,17,17,17,14,
        10,10,31,10,31,10,10,
        17,2,4,8,16,17,0,
        14,17,23,21,23,16,14);
    return rows[n*7+y];
}
float cellHash(ivec2 p){return fract(sin(dot(vec2(p),vec2(127.1,311.7)))*43758.5453);}
float fieldNoise(vec2 p){
    ivec2 i=ivec2(floor(p)); vec2 f=fract(p); f=f*f*(3.-2.*f);
    return mix(mix(cellHash(i),cellHash(i+ivec2(1,0)),f.x),
               mix(cellHash(i+ivec2(0,1)),cellHash(i+ivec2(1,1)),f.x),f.y);
}
float ambientInk(ivec2 cell, vec2 q){
    if(!uAmbient)return 0.;
    // A glyph in EVERY cell, not a sparse random on/off mask.
    // Noise only modulates intensity; it never removes a cell.
    float cloud=fieldNoise(vec2(cell)*.095+vec2(21.,11.));
    float cluster=smoothstep(.35,.78,cloud);
    float h=cellHash(cell+ivec2(13,117));
    float accent=step(.91,cellHash(cell+ivec2(91,47)))*cluster;
    // Fine dot, dash, colon, cross. Keep strokes visible at 4 px/cell.
    vec2 p=q-vec2(.5);
    // Keep transparent corner gaps even at the minimum 4 px cell.
    if(abs(p.x)>.34 && abs(p.y)>.34)return 0.;
    float dot1=1.-smoothstep(.085,.27,length(p));
    float dash=(1.-smoothstep(.16,.36,abs(p.x)))*(1.-smoothstep(.07,.22,abs(p.y)));
    float colon=max(1.-smoothstep(.075,.24,length(q-vec2(.5,.29))),
                    1.-smoothstep(.075,.24,length(q-vec2(.5,.71))));
    float cross=max(dash,(1.-smoothstep(.07,.22,abs(p.x)))*(1.-smoothstep(.16,.36,abs(p.y))));
    float shape=h<.50?dot1:h<.72?dash:h<.90?colon:cross;
    float strength=mix(.035,.085,cluster)*mix(.80,1.,h)+.075*accent;
    // Extremely faint cell interior; gaps between cells remain transparent.
    float tile=(1.-smoothstep(.32,.48,abs(p.x)))*(1.-smoothstep(.32,.48,abs(p.y)));
    float baseInk=tile*mix(.006,.015,cluster);
    return clamp(baseInk+shape*strength,0.,.18);
}
void main(){
    if(uMode==1){vec4 c=texture(uImage,uv);vec3 bg=uDark?vec3(0.051,0.067,0.09):vec3(1);frag=vec4(mix(bg,c.rgb,c.a),1);return;}
    ivec2 size=textureSize(uImage,0),pixel=ivec2(gl_FragCoord.xy);
    ivec2 cell=pixel/uCell,base=cell*uCell;
    vec2 q=(vec2(pixel-base)+.5)/float(uCell);
    vec3 tint=uDark?vec3(191.,209.,230.)/255.:vec3(43.,59.,77.)/255.;
    float ambient=ambientInk(cell,q);
    float coverage=0.,lum=0.,finPixels=0.,depthInk=0.,water=0.;
    // Decode material tags; all nonzero scene pixels are fully occupied.
    for(int y=0;y<uCell;++y)for(int x=0;x<uCell;++x){
      ivec2 p=base+ivec2(x,y);if(any(greaterThanEqual(p,size)))continue;
      vec4 c=texelFetch(uImage,p,0);
      float occupied=step(.20,c.a);
      float body=step(.55,c.a);
      depthInk+=occupied*clamp(mix((c.a-.25)/.20,(c.a-.60)/.35,body),0.,1.);
      water+=step(.02,c.a)*(1.-occupied)*c.a;
      coverage+=occupied;
      lum+=dot(c.rgb,vec3(.2126,.7152,.0722))*occupied;
      finPixels+=occupied*(1.-body);
    }
    float waterInk=clamp(water*1.8,0.,.15)*(1.-smoothstep(.10,.36,length(q-.5)))*uForegroundOpacity;
    ambient=ambient+waterInk*(1.-ambient);
    if(coverage<0.5){frag=vec4(tint,ambient);return;}
    float depthOpacity=depthInk/coverage;
    float finWeight=finPixels/coverage;
    lum/=coverage;coverage/=float(uCell*uCell);
    // Expand the charcoal tones but preserve white markings as distinct cells.
    float tone=smoothstep(.055,.76,lum);
    float contrast=uDark?mix(.53,.98,tone):mix(.83,.20,tone);
    // Preserve thin fin silhouettes, especially dorsal fin in top-down view.
    // A fin-only cell gets a higher contrast floor and gentle edge recovery.
    float finContrast=max(contrast,uDark?.82:.90);
    contrast=mix(contrast,finContrast,finWeight);
    float silhouetteCoverage=mix(coverage,pow(coverage,.62),finWeight);
    float hash=cellHash(cell);
    float density=clamp(contrast*.82+hash*.18,0.,.999);
    int n=int(floor(density*8.));
    // Keep glyphs slim, with breathing space between adjacent characters.
    vec2 local=(q-vec2(.10,.055))/vec2(.80,.89);
    ivec2 g=ivec2(floor(vec2(local.x,1.-local.y)*vec2(5,7)));
    bool inside=all(greaterThanEqual(local,vec2(0)))&&all(lessThan(local,vec2(1)));
    bool mark=inside&&((glyph(n,clamp(g.y,0,6))&(1<<(4-clamp(g.x,0,4))))!=0);
    // Cells have a faint base and individually legible glyph strokes.
    float baseInk=contrast*(uDark?.50:.52);
    float glyphInk=min(1.,contrast*.93+.08);
    // A subtle cell border, not a heavy square outline.
    bool edge=(pixel.x==base.x||pixel.y==base.y);
    float a=silhouetteCoverage*(edge?baseInk*.18:(mark?glyphInk:baseInk));
    // Both layers share the same ink, so alpha-over is exact in straight RGBA.
    a*=depthOpacity*clamp(uForegroundOpacity,0.,1.);
    frag=vec4(tint,a+ambient*(1.-a));
}
)GLSL";
class Pass {
    GLuint scene=0,sceneTex=0,depth=0,ascii=0,asciiTex=0,program=0,vao=0;
    int width=Width,height=Height;
    static GLuint compile(GLenum type,const char* s){GLuint v=glCreateShader(type);glShaderSource(v,1,&s,nullptr);glCompileShader(v);GLint ok=0;glGetShaderiv(v,GL_COMPILE_STATUS,&ok);if(!ok){char log[4096]{};glGetShaderInfoLog(v,sizeof(log),nullptr,log);glDeleteShader(v);throw std::runtime_error(log);}return v;}
    static void target(GLuint& fbo,GLuint& tex){glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,Width,Height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);}
    static void check(){if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Capture framebuffer incomplete");}
    void quad(GLuint tex,int mode,int cell,bool dark,bool ambient=true,float opacity=1.f){glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glUseProgram(program);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,tex);glUniform1i(glGetUniformLocation(program,"uImage"),0);glUniform1i(glGetUniformLocation(program,"uCell"),cell);glUniform1i(glGetUniformLocation(program,"uMode"),mode);glUniform1i(glGetUniformLocation(program,"uDark"),dark);glUniform1i(glGetUniformLocation(program,"uAmbient"),ambient);glUniform1f(glGetUniformLocation(program,"uForegroundOpacity"),opacity);glBindVertexArray(vao);glDrawArrays(GL_TRIANGLES,0,3);glBindVertexArray(0);glEnable(GL_DEPTH_TEST);}
public:
    Pass(){target(scene,sceneTex);glGenRenderbuffers(1,&depth);glBindRenderbuffer(GL_RENDERBUFFER,depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,Width,Height);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);check();target(ascii,asciiTex);check();glBindFramebuffer(GL_FRAMEBUFFER,0);GLuint v=compile(GL_VERTEX_SHADER,vs),f=compile(GL_FRAGMENT_SHADER,fs);program=glCreateProgram();glAttachShader(program,v);glAttachShader(program,f);glLinkProgram(program);glDeleteShader(v);glDeleteShader(f);GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);if(!ok){char log[4096]{};glGetProgramInfoLog(program,sizeof(log),nullptr,log);throw std::runtime_error(log);}glGenVertexArrays(1,&vao);}
    ~Pass(){glDeleteFramebuffers(1,&scene);glDeleteFramebuffers(1,&ascii);glDeleteTextures(1,&sceneTex);glDeleteTextures(1,&asciiTex);glDeleteRenderbuffers(1,&depth);glDeleteProgram(program);glDeleteVertexArrays(1,&vao);}
    Pass(const Pass&)=delete;Pass& operator=(const Pass&)=delete;
    void resize(int w,int h){
        if(w==width&&h==height)return;
        if(w<=0||h<=0)throw std::runtime_error("Invalid ASCII target size");
        width=w;height=h;
        for(GLuint tex:{sceneTex,asciiTex}){
            glBindTexture(GL_TEXTURE_2D,tex);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        }
        glBindRenderbuffer(GL_RENDERBUFFER,depth);
        glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,width,height);
        glBindFramebuffer(GL_FRAMEBUFFER,scene);check();
        glBindFramebuffer(GL_FRAMEBUFFER,ascii);check();glBindFramebuffer(GL_FRAMEBUFFER,0);
    }
    void begin(int w=Width,int h=Height){resize(w,h);glBindFramebuffer(GL_FRAMEBUFFER,scene);glViewport(0,0,width,height);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDisable(GL_BLEND);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);}
    void finish(int cell,bool dark,bool ambient=true,float opacity=1.f){glBindFramebuffer(GL_FRAMEBUFFER,ascii);glViewport(0,0,width,height);quad(sceneTex,0,std::clamp(cell,MinCell,MaxCell),dark,ambient,opacity);glBindFramebuffer(GL_FRAMEBUFFER,0);}
    void display(int w,int h,bool dark){
        glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,w,h);
        glClearColor(dark?.051f:1.f,dark?.067f:1.f,dark?.09f:1.f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        int vw=w,vh=std::max(1,int(double(w)*height/width));
        if(vh>h){vh=h;vw=std::max(1,int(double(h)*width/height));}
        glViewport((w-vw)/2,(h-vh)/2,vw,vh);quad(asciiTex,1,8,dark);
    }
    void write(std::ostream& out){if(width!=Width||height!=Height)throw std::runtime_error("Export requires fixed resolution");std::vector<unsigned char> pixels(Width*Height*4);glBindFramebuffer(GL_FRAMEBUFFER,ascii);glReadPixels(0,0,Width,Height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());glBindFramebuffer(GL_FRAMEBUFFER,0);out.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());if(!out)throw std::runtime_error("Failed writing capture frames");}

};
}
