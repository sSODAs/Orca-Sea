#ifdef _WIN32
#pragma once

// Windows GDI text HUD, adapted from the Day-Dream-Garden debug style.
// Render Consolas into a coverage mask, then composite it into an RGBA texture.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/glew.h>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <stdexcept>

#ifdef _MSC_VER
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#endif

namespace HelpOverlay {
struct Color { unsigned char r,g,b; };
inline constexpr Color Normal{219,224,232}, Blue{163,199,255},
    Green{46,255,71}, Yellow{245,212,92}, Muted{166,179,190};
struct Row { std::string text; Color color=Normal; };
inline constexpr int FontPx=12, LineHeight=14, Width=364, Padding=8;
inline constexpr unsigned char BackgroundAlpha=118; // 46.3% opaque

class Hud {
public:
    Hud() {
        try {
            dc=CreateCompatibleDC(nullptr);
            if(!dc) throw std::runtime_error("HUD: CreateCompatibleDC failed");
            font=CreateFontA(-FontPx,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
                ANSI_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
                ANTIALIASED_QUALITY,FIXED_PITCH|FF_MODERN,"Consolas");
            if(!font) throw std::runtime_error("HUD: CreateFont failed");
            oldFont=SelectObject(dc,font);
            if(!oldFont || oldFont==HGDI_ERROR) {oldFont=nullptr;throw std::runtime_error("HUD: font selection failed");}
            SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(255,255,255));
            program=makeProgram();
            glGenVertexArrays(1,&vao); glGenTextures(1,&texture);
            glBindTexture(GL_TEXTURE_2D,texture);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D,0);
        } catch(...) { cleanup(); throw; }
    }
    ~Hud(){cleanup();}
    Hud(const Hud&)=delete; Hud& operator=(const Hud&)=delete;

    void update(const std::vector<Row>& rows) {
        const int height=2*Padding+static_cast<int>(rows.size())*LineHeight;
        if(height!=bitmapHeight) resizeBitmap(height);
        // DIB is BGRA with a top-down row order. Draw white on black to
        // obtain grayscale glyph coverage, avoiding ClearType color fringes.
        GdiFlush();
        auto* mask=static_cast<unsigned char*>(bits);
        std::fill(mask,mask+std::size_t(Width)*height*4,0);
        for(std::size_t i=0;i<rows.size();++i) {
            RECT clip{Padding,Padding+int(i)*LineHeight,Width-Padding,Padding+int(i+1)*LineHeight};
            ExtTextOutA(dc,Padding,clip.top,ETO_CLIPPED,&clip,rows[i].text.c_str(),
                static_cast<UINT>(rows[i].text.size()),nullptr);
        }
        GdiFlush();
        rgba.resize(std::size_t(Width)*height*4);
        constexpr Color bg{9,13,18};
        const float ba=BackgroundAlpha/255.0f;
        auto byte=[](float x){return static_cast<unsigned char>((std::max)(0.0f,(std::min)(255.0f,std::round(x))));};
        for(int y=0;y<height;++y) {
            const int row=(y-Padding)/LineHeight;
            const Color fg=(y>=Padding && row<int(rows.size()))?rows[row].color:Normal;
            for(int x=0;x<Width;++x) {
                const std::size_t j=4*(std::size_t(y)*Width+x);
                if(y<2){rgba[j]=196;rgba[j+1]=154;rgba[j+2]=45;rgba[j+3]=255;continue;}
                const float a=(mask[j]+mask[j+1]+mask[j+2])/(3.0f*255.0f);
                const float outA=a+ba*(1.0f-a);
                rgba[j]=byte((fg.r*a+bg.r*ba*(1-a))/outA);
                rgba[j+1]=byte((fg.g*a+bg.g*ba*(1-a))/outA);
                rgba[j+2]=byte((fg.b*a+bg.b*ba*(1-a))/outA);
                rgba[j+3]=byte(outA*255.0f);
            }
        }
        GLint active=0,binding=0,alignment=0;
        glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);
        glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);
        glBindTexture(GL_TEXTURE_2D,texture);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        if(textureHeight!=height){
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,Width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
            textureHeight=height;
        } else glTexSubImage2D(GL_TEXTURE_2D,0,0,0,Width,height,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);glBindTexture(GL_TEXTURE_2D,GLuint(binding));glActiveTexture(GLenum(active));
    }

    void draw(int w,int h) const {
        if(textureHeight<=0||w<=24||h<=24)return;
        // Never enlarge above native font pixels; only shrink to fit a small window.
        const float scale=(std::min)({1.0f,float(w-24)/Width,float(h-24)/textureHeight});
        GLint vp[4],oldProgram=0,oldVao=0,oldActive=0,oldTexture=0;
        GLint srcRGB=0,dstRGB=0,srcA=0,dstA=0,eqRGB=0,eqA=0;
        const GLboolean depth=glIsEnabled(GL_DEPTH_TEST),blend=glIsEnabled(GL_BLEND);
        const GLboolean cull=glIsEnabled(GL_CULL_FACE),scissor=glIsEnabled(GL_SCISSOR_TEST);
        glGetIntegerv(GL_VIEWPORT,vp);glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&oldVao);glGetIntegerv(GL_ACTIVE_TEXTURE,&oldActive);
        glGetIntegerv(GL_BLEND_SRC_RGB,&srcRGB);glGetIntegerv(GL_BLEND_DST_RGB,&dstRGB);
        glGetIntegerv(GL_BLEND_SRC_ALPHA,&srcA);glGetIntegerv(GL_BLEND_DST_ALPHA,&dstA);
        glGetIntegerv(GL_BLEND_EQUATION_RGB,&eqRGB);glGetIntegerv(GL_BLEND_EQUATION_ALPHA,&eqA);
        glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&oldTexture);
        glViewport(0,0,w,h);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);
        glEnable(GL_BLEND);glBlendEquation(GL_FUNC_ADD);
        glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(program);glBindVertexArray(vao);glBindTexture(GL_TEXTURE_2D,texture);
        glUniform2f(glGetUniformLocation(program,"uScreen"),float(w),float(h));
        glUniform4f(glGetUniformLocation(program,"uRect"),12,12,Width*scale,textureHeight*scale);
        glUniform1i(glGetUniformLocation(program,"uTexture"),0);
        glDrawArrays(GL_TRIANGLE_STRIP,0,4);
        glBindTexture(GL_TEXTURE_2D,GLuint(oldTexture));glActiveTexture(GLenum(oldActive));
        glBindVertexArray(GLuint(oldVao));glUseProgram(GLuint(oldProgram));
        glBlendFuncSeparate(GLenum(srcRGB),GLenum(dstRGB),GLenum(srcA),GLenum(dstA));
        glBlendEquationSeparate(GLenum(eqRGB),GLenum(eqA));
        if(!blend)glDisable(GL_BLEND);if(depth)glEnable(GL_DEPTH_TEST);
        if(cull)glEnable(GL_CULL_FACE);if(scissor)glEnable(GL_SCISSOR_TEST);
        glViewport(vp[0],vp[1],vp[2],vp[3]);
    }
private:
    HDC dc=nullptr;HFONT font=nullptr;HBITMAP bitmap=nullptr;
    HGDIOBJ oldFont=nullptr,oldBitmap=nullptr;void* bits=nullptr;
    int bitmapHeight=0,textureHeight=0;
    GLuint program=0,vao=0,texture=0;
    std::vector<unsigned char> rgba;
    void resizeBitmap(int height){
        if(bitmap){SelectObject(dc,oldBitmap);DeleteObject(bitmap);bitmap=nullptr;oldBitmap=nullptr;}
        BITMAPINFO bmi{};bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth=Width;bmi.bmiHeader.biHeight=-height;
        bmi.bmiHeader.biPlanes=1;bmi.bmiHeader.biBitCount=32;bmi.bmiHeader.biCompression=BI_RGB;
        bitmap=CreateDIBSection(dc,&bmi,DIB_RGB_COLORS,&bits,nullptr,0);
        if(!bitmap||!bits)throw std::runtime_error("HUD: CreateDIBSection failed");
        oldBitmap=SelectObject(dc,bitmap);
        if(!oldBitmap||oldBitmap==HGDI_ERROR){oldBitmap=nullptr;throw std::runtime_error("HUD: bitmap selection failed");}
        bitmapHeight=height;
    }
    void cleanup(){
        if(texture)glDeleteTextures(1,&texture);if(vao)glDeleteVertexArrays(1,&vao);if(program)glDeleteProgram(program);
        if(dc&&oldBitmap)SelectObject(dc,oldBitmap);if(bitmap)DeleteObject(bitmap);
        if(dc&&oldFont)SelectObject(dc,oldFont);if(font)DeleteObject(font);if(dc)DeleteDC(dc);
    }
    static GLuint compile(GLenum type,const char* src){
        GLuint s=glCreateShader(type);glShaderSource(s,1,&src,nullptr);glCompileShader(s);
        GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
        if(!ok){char log[2048]{};glGetShaderInfoLog(s,sizeof(log),nullptr,log);glDeleteShader(s);throw std::runtime_error(log);}return s;
    }
    static GLuint makeProgram(){
        const char* vs=R"GLSL(#version 330 core
uniform vec2 uScreen;
uniform vec4 uRect;
out vec2 uv;
void main(){
    vec2 q=vec2(float(gl_VertexID & 1),float((gl_VertexID >> 1) & 1));
    vec2 p=uRect.xy+q*uRect.zw;
    gl_Position=vec4(2.0*p.x/uScreen.x-1.0,1.0-2.0*p.y/uScreen.y,0.0,1.0);
    uv=q;
}
)GLSL";
        const char* fs=R"GLSL(#version 330 core
in vec2 uv;
uniform sampler2D uTexture;
out vec4 FragColor;
void main(){FragColor=texture(uTexture,uv);}
)GLSL";
        GLuint v=compile(GL_VERTEX_SHADER,vs),f=0;
        try{f=compile(GL_FRAGMENT_SHADER,fs);}catch(...){glDeleteShader(v);throw;}
        GLuint p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);glDeleteShader(v);glDeleteShader(f);
        GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);
        if(!ok){char log[2048]{};glGetProgramInfoLog(p,sizeof(log),nullptr,log);glDeleteProgram(p);throw std::runtime_error(log);}return p;
    }
};
}

#else
#pragma once
#include <string>
#include <vector>
namespace HelpOverlay { struct Color {unsigned char r,g,b;}; inline constexpr Color Normal{219,224,232},Blue{163,199,255},Green{46,255,71},Yellow{245,212,92},Muted{166,179,190}; struct Row {std::string text;Color color=Normal;}; class Hud {public: void update(const std::vector<Row>&){} void draw(int,int)const{} }; }
#endif
