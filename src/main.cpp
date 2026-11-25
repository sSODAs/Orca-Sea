#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "OrcaMesh.h"
#include "HelpOverlay.h"
#include "SwimPath.h"
#include "PodMotion.h"
#include "AsciiCapture.h"
#include <array>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <sstream>
#include <iomanip>
#include <string>
#include <utility>
#include <fstream>
#include <filesystem>

namespace {
constexpr int WINDOW_WIDTH=1280,WINDOW_HEIGHT=720;
constexpr float WORLD_WIDTH=48.0f,WORLD_DEPTH=32.0f;
constexpr float CAPTURE_WIDTH=24.0f,CAPTURE_HEIGHT=9.0f;
constexpr float CAPTURE_ASPECT=CAPTURE_WIDTH/CAPTURE_HEIGHT;
enum class CameraMode { Perspective,TopDown,Capture,MeshInspection,SideInspection };
CameraMode cameraMode=CameraMode::Capture;
bool showGrid=false,showWireframe=false,showHelp=false;
bool paused=false,showSkeleton=false,restPose=false,resetTime=false;
bool autoMove=true, showPath=false, reseed=false;
float swimSpeed=2.4f, overallScale=1.0f;
float cellSize=float(AsciiCapture::DefaultCell); // Integer steps via the live tuning HUD.
bool darkTheme=false,showAmbient=true,showWater=true,avoidOverlap=true;
int currentCellSize(){return std::clamp(int(std::lround(cellSize)),AsciiCapture::MinCell,AsciiCapture::MaxCell);}
struct Parameter { const char* name; float* value; float step,lo,hi,initial; };
Parameter parameters[]={
    {"SPEED", &swimSpeed, 0.1f,0.8f,8.0f,2.4f},
    {"MAX JOINT YAW RAD", &SwimRig::MaxJointYaw,0.01f,0.05f,0.64f,0.32f},
    {"BEND GAIN", &SwimRig::BendGain,0.05f,0.25f,3.30f,1.15f},
    {"BANK DEG", &SwimRig::BankDegrees,1.0f,0.0f,50.0f,12.0f},
    {"TAIL FREQ HZ", &SwimRig::Frequency,0.05f,0.0f,3.0f,0.65f},
    {"TAIL AMP RAD", &SwimRig::TailAmplitude,0.01f,0.0f,0.44f,0.145f},
    {"LENGTH SCALE", &SwimRig::LENGTH_SCALE,0.05f,0.85f,2.80f,1.30f},
    {"POD SIZE MULT", &overallScale,0.05f,0.65f,2.50f,1.0f},
    {"ASCII CELL PX", &cellSize,1.0f,float(AsciiCapture::MinCell),float(AsciiCapture::MaxCell),float(AsciiCapture::DefaultCell)}
};
constexpr int ParamCount=int(sizeof(parameters)/sizeof(parameters[0]));
int selectedParam=1;
void resetParameters(){for(auto& p:parameters)*p.value=p.initial;}
void saveSettings(){
    std::filesystem::create_directories("output");
    std::ofstream out("output/orca-settings.json");
    if(!out){std::cerr<<"Cannot save settings\n";return;}
    const char* names[]={"speed","yaw","bend","bank","frequency","amplitude","length","size","cell"};
    out<<"{\n";
    for(int i=0;i<ParamCount;++i)out<<"  \""<<names[i]<<"\": "<<*parameters[i].value<<(i+1==ParamCount?"\n":",\n");
    out<<"}\n";std::cout<<"Saved output/orca-settings.json\n";
}
std::string fmt(double v,int precision=2){std::ostringstream ss;ss<<std::fixed<<std::setprecision(precision)<<v;return ss.str();}
const char* vertexSource=R"GLSL(#version 330 core
layout(location=0) in vec3 aPosition;
uniform mat4 uViewProjection;
void main(){gl_Position=uViewProjection*vec4(aPosition,1.0);}
)GLSL";
const char* fragmentSource=R"GLSL(#version 330 core
uniform vec3 uColor;
uniform float uTag;
out vec4 FragColor;
void main(){FragColor=vec4(uColor,uTag);}
)GLSL";
GLuint compileShader(GLenum type,const char* source){
    GLuint s=glCreateShader(type);glShaderSource(s,1,&source,nullptr);glCompileShader(s);
    GLint ok=GL_FALSE;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[2048]{};glGetShaderInfoLog(s,sizeof(log),nullptr,log);glDeleteShader(s);throw std::runtime_error(log);}
    return s;
}
GLuint createShaderProgram(){
    GLuint vs=compileShader(GL_VERTEX_SHADER,vertexSource),fs=0;
    try{fs=compileShader(GL_FRAGMENT_SHADER,fragmentSource);}catch(...){glDeleteShader(vs);throw;}
    GLuint p=glCreateProgram();glAttachShader(p,vs);glAttachShader(p,fs);glLinkProgram(p);glDeleteShader(vs);glDeleteShader(fs);
    GLint ok=GL_FALSE;glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok){char log[2048]{};glGetProgramInfoLog(p,sizeof(log),nullptr,log);glDeleteProgram(p);throw std::runtime_error(log);}return p;
}
struct DebugMesh {
    GLuint vao=0,vbo=0;
    GLsizei count=0;
    std::size_t capacity=0;
    DebugMesh()=default;
    DebugMesh(const DebugMesh&)=delete;
    DebugMesh& operator=(const DebugMesh&)=delete;
    ~DebugMesh(){glDeleteBuffers(1,&vbo);glDeleteVertexArrays(1,&vao);}
    void upload(const std::vector<glm::vec3>& v){
        count=static_cast<GLsizei>(v.size());capacity=v.size();glGenVertexArrays(1,&vao);glGenBuffers(1,&vbo);
        glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);
        glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(v.size()*sizeof(glm::vec3)),v.data(),GL_STATIC_DRAW);
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(glm::vec3),nullptr);glEnableVertexAttribArray(0);glBindVertexArray(0);
    }
    void update(const std::vector<glm::vec3>& v){
        count=static_cast<GLsizei>(v.size());
        glBindBuffer(GL_ARRAY_BUFFER,vbo);
        if(v.size()>capacity){
            capacity=std::max(v.size(),capacity*2);
            glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(capacity*sizeof(glm::vec3)),nullptr,GL_DYNAMIC_DRAW);
        }
        if(!v.empty())glBufferSubData(GL_ARRAY_BUFFER,0,static_cast<GLsizeiptr>(v.size()*sizeof(glm::vec3)),v.data());
        glBindBuffer(GL_ARRAY_BUFFER,0);
    }
    void draw(GLenum mode=GL_LINES) const{glBindVertexArray(vao);glDrawArrays(mode,0,count);glBindVertexArray(0);}
};
struct DebugProgram {
    GLuint id=createShaderProgram();
    ~DebugProgram(){glDeleteProgram(id);}
    DebugProgram()=default;DebugProgram(const DebugProgram&)=delete;DebugProgram& operator=(const DebugProgram&)=delete;
};
void line(std::vector<glm::vec3>& v,glm::vec3 a,glm::vec3 b){v.push_back(a);v.push_back(b);}
std::vector<glm::vec3> createGrid(){
    std::vector<glm::vec3> v;
    for(int x=-24;x<=24;++x)line(v,{float(x),0,-16},{float(x),0,16});
    for(int z=-16;z<=16;++z)line(v,{-24,0,float(z)},{24,0,float(z)});
    return v;
}
std::vector<glm::vec3> boundary(float w,float d){
    std::vector<glm::vec3> v;float x=w/2,z=d/2,y=0.025f;
    line(v,{-x,y,-z},{x,y,-z});line(v,{x,y,-z},{x,y,z});
    line(v,{x,y,z},{-x,y,z});line(v,{-x,y,z},{-x,y,-z});return v;
}
glm::mat4 getViewMatrix(const glm::vec3& position,float heading){
    const glm::mat4 rotation=glm::rotate(glm::mat4(1),heading,glm::vec3(0,1,0));
    auto offset=[&](glm::vec3 v){return glm::vec3(rotation*glm::vec4(v,0));};
    if(cameraMode==CameraMode::Perspective)return glm::lookAt(glm::vec3(34,38,38),glm::vec3(0),glm::vec3(0,1,0));
    if(cameraMode==CameraMode::SideInspection)return glm::lookAt(position+offset({0,0,16}),position,glm::vec3(0,1,0));
    if(cameraMode==CameraMode::MeshInspection)return glm::lookAt(position+offset({8,4.5f,12}),position,glm::vec3(0,1,0));
    return glm::lookAt(glm::vec3(0,40,0),glm::vec3(0),glm::vec3(0,0,-1));
}
glm::mat4 getProjectionMatrix(float aspect){
    if(cameraMode==CameraMode::Perspective||cameraMode==CameraMode::MeshInspection||cameraMode==CameraMode::SideInspection)
        return glm::perspective(glm::radians(50.0f),aspect,0.1f,200.0f);
    if(cameraMode==CameraMode::Capture)return glm::ortho(-CAPTURE_WIDTH/2,CAPTURE_WIDTH/2,-CAPTURE_HEIGHT/2,CAPTURE_HEIGHT/2,0.1f,100.0f);
    float hh=std::max(WORLD_DEPTH/2+2,(WORLD_WIDTH/2+2)/aspect),hw=hh*aspect;
    return glm::ortho(-hw,hw,-hh,hh,0.1f,100.0f);
}
void keyCallback(GLFWwindow* window,int key,int,int action,int){
    if(showHelp && (action==GLFW_PRESS || action==GLFW_REPEAT)){
        if(key==GLFW_KEY_UP){selectedParam=(selectedParam+ParamCount-1)%ParamCount;return;}
        if(key==GLFW_KEY_DOWN){selectedParam=(selectedParam+1)%ParamCount;return;}
        if(key==GLFW_KEY_LEFT||key==GLFW_KEY_RIGHT){
            auto& p=parameters[selectedParam];
            *p.value=std::clamp(*p.value+(key==GLFW_KEY_RIGHT?p.step:-p.step),p.lo,p.hi);return;
        }
        if(key==GLFW_KEY_F && action==GLFW_PRESS){resetParameters();return;}
    }
    if(action!=GLFW_PRESS)return;
    switch(key){
    case GLFW_KEY_ESCAPE:glfwSetWindowShouldClose(window,GLFW_TRUE);break;
    case GLFW_KEY_1:cameraMode=CameraMode::Perspective;glfwSetWindowTitle(window,"Orca Sea | ASCII Perspective | TAB: Help");break;
    case GLFW_KEY_2:cameraMode=CameraMode::TopDown;glfwSetWindowTitle(window,"Orca Sea | ASCII Top-down | TAB: Help");break;
    case GLFW_KEY_3:cameraMode=CameraMode::Capture;glfwSetWindowTitle(window,"Orca Sea | ASCII Capture | TAB: Help");break;
    case GLFW_KEY_4:cameraMode=CameraMode::MeshInspection;glfwSetWindowTitle(window,"Orca Sea | ASCII Inspection | TAB: Help");break;
    case GLFW_KEY_5:cameraMode=CameraMode::SideInspection;glfwSetWindowTitle(window,"Orca Sea | ASCII Side | TAB: Help");break;
    case GLFW_KEY_7:darkTheme=!darkTheme;break;
    case GLFW_KEY_H:showAmbient=!showAmbient;break;
    case GLFW_KEY_E:try{saveSettings();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';}break;
    case GLFW_KEY_J:showWater=!showWater;break;
    case GLFW_KEY_K:avoidOverlap=!avoidOverlap;break;
    case GLFW_KEY_SPACE:paused=!paused;break;
    case GLFW_KEY_B:showSkeleton=!showSkeleton;break;
    case GLFW_KEY_R:resetTime=true;break;
    case GLFW_KEY_T:restPose=!restPose;break;
    case GLFW_KEY_M:autoMove=!autoMove;std::cout<<"Autonomous movement: "<<(autoMove?"ON":"OFF")<<'\n';break;
    case GLFW_KEY_P:showPath=!showPath;break;
    case GLFW_KEY_N:reseed=true;break;
    case GLFW_KEY_G:showGrid=!showGrid;break;
    case GLFW_KEY_W:showWireframe=!showWireframe;break;
    case GLFW_KEY_TAB:showHelp=!showHelp;break;
    }
}
struct Agent {
    SwimPath::Wander wander;
    float size,rate,phase,turn=0,blend=1;
    double yieldSpeed=2.4;
    double traceTime=0;
    std::vector<glm::vec3> trail;
    Agent(unsigned seed,float size_,float rate_,float phase_):wander(seed),size(size_),rate(rate_),phase(phase_){}
};
struct DrawAgent { glm::vec3 position; glm::mat4 model; SwimRig::Pose pose; float heading,actualYaw,opacity; };
class Pod {
public:
    std::vector<Agent> agents;
    unsigned seed=42;
    double time=0,planClock=0;
    std::array<double,PodMotion::Count> planned{};
    Pod(){reset(seed);}
    void reset(unsigned newSeed){
        seed=newSeed; agents.clear();time=0;planClock=1;
        const auto& sizes=PodMotion::Sizes;
        const auto& rates=PodMotion::Rates;
        for(int i=0;i<5;++i){
            agents.emplace_back(seed+7919u*unsigned(i),sizes[i],rates[i],.19f*i);
            auto& a=agents.back();
            a.wander.setSpeed(swimSpeed*a.rate);
            // Start at different positions along independently generated trails.
            for(int j=0;j<i*91+35;++j)a.wander.step(.1);
            a.turn=a.wander.state().turn;
            planned[i]=swimSpeed*a.rate;
        }
    }
    void reseedFuture(){++seed;for(size_t i=0;i<agents.size();++i)agents[i].wander.retarget(seed+7919u*unsigned(i));}
    void step(double dt,float manual=0){
        time+=dt;planClock+=dt;
        if(planClock>=.15){
            std::array<PodMotion::Input,PodMotion::Count> input{};
            for(int i=0;i<PodMotion::Count;++i)input[i]={&agents[i].wander,agents[i].size*overallScale,std::min(8.0,double(swimSpeed*agents[i].rate))};
            planned=PodMotion::speeds(input,SwimRig::LENGTH_SCALE,avoidOverlap);planClock=0;
        }
        for(size_t n=0;n<agents.size();++n){auto& a=agents[n];
            a.yieldSpeed=planned[n];a.wander.setSpeed(a.yieldSpeed);
            if(autoMove){a.wander.step(dt);a.traceTime+=dt;
                if(a.traceTime>=.12){a.traceTime-=.12;auto p=a.wander.state().position;
                    if(a.trail.size()>=256)a.trail.erase(a.trail.begin());
                    a.trail.emplace_back(float(p.x),.06f,float(p.z));}}
            a.phase=float(std::fmod(a.phase+dt*SwimRig::Frequency*a.rate,1.0));
            a.turn=SwimRig::smoothTurn(a.turn,autoMove?a.wander.state().turn:manual,dt);
            a.blend+=(float(autoMove)-a.blend)*float(-std::expm1(-3*dt));
        }
    }
    std::vector<DrawAgent> poses() const {
        std::vector<DrawAgent> result;
        for(size_t n=0;n<agents.size();++n){const auto& a=agents[n]; auto st=a.wander.state();
            const float size=a.size*overallScale;
            // Depth staggering provides a visual depth order, not collision avoidance.
            glm::vec3 pos(float(st.position.x),PodMotion::Height[n],float(st.position.z));
            auto yaw=SwimRig::manualYaw(a.turn);
            for(int i=1;i<SwimRig::Count;++i){
                double offset=i*SwimRig::Step*SwimRig::LENGTH_SCALE*size;
                double r=std::clamp(a.wander.relativeHeadingBehind(offset),-1.7,1.7);
                float local=SwimRig::BendGain*float(std::atan2(SwimRig::LENGTH_SCALE*std::sin(r),std::cos(r)));
                yaw[i]+=a.blend*(local-yaw[i]);
            }
            auto pose=SwimRig::evaluateTrail(a.phase,!restPose,yaw);
            result.push_back({pos,SwimRig::worldModel(a.phase,!restPose,a.turn,pos,st.heading,size),pose,st.heading,SwimRig::AppliedJointYaw,PodMotion::Opacity[n]});
        }
        return result;
    }
};
// Water is generated in world space, before ASCII conversion. It never writes
// the whale tag and is occluded by the opaque geometry in the depth buffer.
void renderWater(DebugProgram& program,DebugMesh& points,const Pod& pod,const glm::mat4& vp){
    if(!showWater)return;
    std::vector<glm::vec3> v;
    for(int i=0;i<90;++i){
        double x=std::fmod(i*7.137+pod.time*.08,48.0)-24.0;
        double z=std::fmod(i*3.719+pod.time*.035,32.0)-16.0;
        v.emplace_back(float(x),.25f,float(z));
    }
    for(size_t n=0;n<pod.agents.size();++n){
        const auto& a=pod.agents[n];double size=a.size*overallScale;
        // Sample behind the tail along the already travelled path.
        for(int k=0;k<13;++k){
            double offset=(6.2*SwimRig::LENGTH_SCALE*size)+.35*k;
            auto p=a.wander.pointAtOffset(-offset);
            double h=a.wander.state().heading;
            float drift=float((.10+.035*k)*std::sin(k*2.4+n*1.7));
            v.emplace_back(float(p.x)+drift*float(std::sin(h)),PodMotion::Height[n]-.25f,float(p.z)+drift*float(std::cos(h)));
        }
    }
    points.update(v);glUseProgram(program.id);
    glUniformMatrix4fv(glGetUniformLocation(program.id,"uViewProjection"),1,GL_FALSE,glm::value_ptr(vp));
    glUniform3f(glGetUniformLocation(program.id,"uColor"),.5f,.6f,.7f);
    glUniform1f(glGetUniformLocation(program.id,"uTag"),.085f);
    glPointSize(2.f);points.draw(GL_POINTS);glPointSize(1.f);
}
const glm::mat4 fixedCaptureVP(){return glm::ortho(-CAPTURE_WIDTH/2,CAPTURE_WIDTH/2,-CAPTURE_HEIGHT/2,CAPTURE_HEIGHT/2,.1f,100.f)*glm::lookAt(glm::vec3(0,40,0),glm::vec3(0),glm::vec3(0,0,-1));}
void renderCapture(AsciiCapture::Pass& pass,OrcaMesh& orca,DebugProgram& debug,DebugMesh& water,const Pod& pod,const std::vector<DrawAgent>& poses,bool dark,float opacity=1.f){
    pass.begin();const auto vp=fixedCaptureVP();
    renderWater(debug,water,pod,vp);
    for(const auto& a:poses)orca.draw(vp,a.model,a.pose,false,a.opacity);
    pass.finish(currentCellSize(),dark,true,opacity);
}
void exportFrames(const std::string& file,int seconds,unsigned seed,bool dark,double fade){
    OrcaMesh orca;AsciiCapture::Pass pass;Pod pod;pod.reset(seed);DebugProgram debug;DebugMesh water;water.upload({});
    std::ofstream out(file,std::ios::binary);if(!out)throw std::runtime_error("Cannot open raw export output");
    const int frames=seconds*AsciiCapture::FPS;
    for(int f=0;f<frames;++f){
        if(f>0)for(int sub=0;sub<6;++sub)pod.step(1.0/120.0);
        double opacity=1.0;
        if(fade>0){opacity=std::clamp(std::min(double(f),double(frames-1-f))/(fade*AsciiCapture::FPS),0.0,1.0);opacity=opacity*opacity*(3-2*opacity);}
        renderCapture(pass,orca,debug,water,pod,pod.poses(),dark,float(opacity));pass.write(out);
        if(f%AsciiCapture::FPS==0)std::cout<<"Export "<<f<<" / "<<frames<<'\n';
    }
    out.close();if(!out)throw std::runtime_error("Failed closing export file");
    std::cout<<"Exported "<<frames<<" RGBA frames (bottom-up)\n";
}
void runScene(GLFWwindow* window){
    DebugProgram debug; DebugMesh grid,world,capture,xAxis,zAxis,skeleton,path,targetMarker,futurePath,water;
    grid.upload(createGrid());world.upload(boundary(WORLD_WIDTH,WORLD_DEPTH));capture.upload(boundary(CAPTURE_WIDTH,CAPTURE_HEIGHT));
    xAxis.upload({{-WORLD_WIDTH/2,.03f,0},{WORLD_WIDTH/2,.03f,0}});zAxis.upload({{0,.03f,-WORLD_DEPTH/2},{0,.03f,WORLD_DEPTH/2}});
    skeleton.upload({});path.upload({});targetMarker.upload({});futurePath.upload({});water.upload({});
    HelpOverlay::Hud hud;OrcaMesh orca;AsciiCapture::Pass pass;Pod pod;
    double last=glfwGetTime(),accumulator=0,lastHudUpdate=-1;bool hudWasVisible=false;
    GLint vpLocation=glGetUniformLocation(debug.id,"uViewProjection"),colorLocation=glGetUniformLocation(debug.id,"uColor");
    auto color=[&](float r,float g,float b){glUniform3f(colorLocation,r,g,b);};
    auto matrix=[&](const glm::mat4& m){glUniformMatrix4fv(vpLocation,1,GL_FALSE,glm::value_ptr(m));};
    glEnable(GL_DEPTH_TEST);glEnable(GL_MULTISAMPLE);
    std::cout<<"1-5 ASCII cameras | 7 Theme | H Ambient | Tab HUD (Cell PX)\n";
    while(!glfwWindowShouldClose(window)){
        glfwPollEvents();int w=0,h=0;glfwGetFramebufferSize(window,&w,&h);
        double now=glfwGetTime(),dt=std::clamp(now-last,0.0,.1);last=now;
        if(w<=0||h<=0){glfwWaitEvents();last=glfwGetTime();continue;}
        if(reseed){pod.reseedFuture();reseed=false;}
        if(resetTime){pod.reset(pod.seed);accumulator=0;resetTime=false;}
        else if(!paused&&!restPose){accumulator+=dt;float manual=float(int(glfwGetKey(window,GLFW_KEY_A)==GLFW_PRESS)-int(glfwGetKey(window,GLFW_KEY_D)==GLFW_PRESS));while(accumulator>=1.0/120){pod.step(1.0/120,manual);accumulator-=1.0/120;}}
        const auto poses=pod.poses();const auto& a=pod.agents[0];const auto& lead=poses[0];auto state=a.wander.state();
        // Every camera renders the whales to the same offscreen ASCII pipeline.
        // The fixed capture target is unchanged for reproducible export.
        const bool captureMode=cameraMode==CameraMode::Capture;
        const double previewScale=std::min({1.0,1280.0/w,720.0/h});
        const int rw=captureMode?AsciiCapture::Width:std::max(1,int(w*previewScale));
        const int rh=captureMode?AsciiCapture::Height:std::max(1,int(h*previewScale));
        const glm::mat4 vp=captureMode?fixedCaptureVP():
            getProjectionMatrix(float(rw)/float(rh))*getViewMatrix(lead.position,lead.heading);
        pass.begin(rw,rh);
        renderWater(debug,water,pod,vp);
        for(const auto& agent:poses)orca.draw(vp,agent.model,agent.pose,!captureMode&&showWireframe,agent.opacity);
        pass.finish(currentCellSize(),darkTheme,showAmbient);
        pass.display(w,h,darkTheme);
        // Optional diagnostic overlays are never included in the capture.
        if(!captureMode){
            glDisable(GL_DEPTH_TEST);glUseProgram(debug.id);glUniform1f(glGetUniformLocation(debug.id,"uTag"),1.f);matrix(vp);
            if(showGrid){
                if(darkTheme)color(.16f,.24f,.30f);else color(.86f,.89f,.91f);grid.draw();
                color(.35f,.42f,.48f);world.draw();color(.25f,.65f,.95f);capture.draw();color(.85f,.25f,.25f);xAxis.draw();color(.25f,.45f,.95f);zAxis.draw();
            }
            if(showPath&&(cameraMode==CameraMode::Perspective||cameraMode==CameraMode::TopDown)){
                for(const auto& agent:pod.agents){if(agent.trail.size()>1){path.update(agent.trail);color(.25f,.70f,.56f);path.draw(GL_LINE_STRIP);}
                    std::vector<glm::vec3> planned;for(auto p:agent.wander.future())planned.emplace_back(float(p.x),.08f,float(p.z));futurePath.update(planned);color(.90f,.68f,.22f);futurePath.draw(GL_LINE_STRIP);}
            }
            if(showSkeleton)for(const auto& agent:poses){std::vector<glm::vec3> lines;for(int i=0;i<SwimRig::Count-1;++i)line(lines,agent.pose.joints[i],agent.pose.joints[i+1]);for(auto j:agent.pose.joints)lines.push_back(j);skeleton.update(lines);glUseProgram(debug.id);matrix(vp*agent.model);color(1,.68f,.18f);glBindVertexArray(skeleton.vao);glDrawArrays(GL_LINES,0,2*(SwimRig::Count-1));glPointSize(4);glDrawArrays(GL_POINTS,2*(SwimRig::Count-1),SwimRig::Count);glPointSize(1);glBindVertexArray(0);}
            glEnable(GL_DEPTH_TEST);
        }
        if(showHelp&&w>24&&h>24){if(!hudWasVisible||now-lastHudUpdate>=1.0/15){
            using namespace HelpOverlay;std::vector<Row> rows;auto row=[&](std::string s,Color c=Normal){rows.push_back({std::move(s),c});};
            row("Tab close | Up/Down select | Left/Right adjust",Yellow);row("[CATEGORY: Swim Parameters]",Green);
            for(int i=0;i<ParamCount;++i){const auto& p=parameters[i];row(std::string(i==selectedParam?"> ":"  ")+p.name+": "+fmt(*p.value,p.value==&cellSize?0:(p.value==&SwimRig::TailAmplitude?3:2)),i==selectedParam?Yellow:Blue);}
            row("Front yaw cap: 0.19 rad | F defaults",Muted);row("[CATEGORY: Pod / Lead Agent]",Green);
            row("Count: "+std::to_string(pod.agents.size())+"  Seed: "+std::to_string(pod.seed)+"  "+(autoMove?"AUTO":"POSE")+(paused?" PAUSED":"")+(restPose?" REST":""),Blue);
            row("Pos: "+fmt(state.position.x)+", "+fmt(state.position.z)+"  Speed: "+fmt(autoMove&&!paused&&!restPose?a.wander.speed():0.0),Blue);
            row("Heading: "+fmt(state.heading*180/SwimPath::Pi,1)+"  Curvature: "+fmt(a.wander.curvature(),3),Blue);
            row("Actual joint yaw: "+fmt(lead.actualYaw,3)+" rad",Yellow);
            row(std::string("Avoid: ")+(avoidOverlap?"ON":"OFF")+" Water: "+(showWater?"ON":"OFF"),Blue);
            row("Future: "+fmt(a.wander.ahead(),1)+"  Sections: "+std::to_string(a.wander.arrivals()),Blue);
            row("[CATEGORY: ASCII Rendering]",Green);row(std::string(darkTheme?"Dark":"Light")+" | Cell: "+std::to_string(currentCellSize())+" | "+std::to_string(rw)+"x"+std::to_string(rh),Blue);
            row("1 World 2 Top 3 Capture 4 Inspect 5 Side",Yellow);row("7 Theme H Ambient J Water K Avoid",Yellow);row("W ASCII Wire G Grid",Yellow);row("B Bones P Trail N Future seed",Yellow);row("Space Pause M Pose A/D Bend T Rest",Yellow);row("E Save settings | R Reset Esc Exit",Yellow);
            hud.update(rows);lastHudUpdate=now;}hud.draw(w,h);}
        hudWasVisible=showHelp;glfwSwapBuffers(window);
    }
}
}
int main(int argc,char** argv){
    resetParameters();
    bool exporting=argc>1 && std::string(argv[1])=="--export";
    if(exporting && argc!=6 && argc!=8 && argc!=9 && argc!=16){std::cerr<<"Usage: OrcaSea --export file.rgba seconds seed light|dark [size cell [fade [speed yaw bend bank freq amp length]]]\n";return EXIT_FAILURE;}
    if(!glfwInit()){std::cerr<<"GLFW initialization failed\n";return EXIT_FAILURE;}
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);glfwWindowHint(GLFW_SAMPLES,4);
    if(exporting)glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    GLFWwindow* window=glfwCreateWindow(WINDOW_WIDTH,WINDOW_HEIGHT,"Orca Sea | ASCII Capture | TAB: Help",nullptr,nullptr);
    if(!window){std::cerr<<"Window creation failed\n";glfwTerminate();return EXIT_FAILURE;}
    glfwMakeContextCurrent(window);glfwSwapInterval(exporting?0:1);glewExperimental=GL_TRUE;
    if(glewInit()!=GLEW_OK){std::cerr<<"GLEW initialization failed\n";glfwDestroyWindow(window);glfwTerminate();return EXIT_FAILURE;}
    glGetError();glfwSetKeyCallback(window,keyCallback);
    std::cout<<"Orca Sea | Stage 09 | Depth pod / water / ASCII export\nRenderer: "<<glGetString(GL_RENDERER)<<"\nOpenGL: "<<glGetString(GL_VERSION)<<'\n';
    std::cout<<"Defaults: Cell="<<currentCellSize()<<" Length="<<SwimRig::LENGTH_SCALE<<" JointYaw="<<SwimRig::MaxJointYaw<<'\n';
    int result=EXIT_SUCCESS;
    try{if(exporting){int seconds=std::stoi(argv[3]);if(seconds<1||seconds>120)throw std::runtime_error("Seconds must be 1-120");std::string theme=argv[5];if(theme!="light"&&theme!="dark")throw std::runtime_error("Theme must be light or dark");if(argc>=8){overallScale=std::stof(argv[6]);std::size_t cellParsed=0;int requestedCell=std::stoi(argv[7],&cellParsed);if(cellParsed!=std::string(argv[7]).size()||requestedCell<AsciiCapture::MinCell||requestedCell>AsciiCapture::MaxCell)throw std::runtime_error("Cell size must be an integer from 4 to 32");cellSize=float(requestedCell);if(!std::isfinite(overallScale)||overallScale<.65f||overallScale>2.50f)throw std::runtime_error("Invalid size multiplier or cell size");}if(argc==16){for(int i=0;i<7;++i){float v=std::stof(argv[9+i]);auto& p=parameters[i];if(!std::isfinite(v)||v<p.lo||v>p.hi)throw std::runtime_error(std::string("Invalid parameter: ")+p.name);*p.value=v;}}double fade=argc>=9?std::stod(argv[8]):0.0;if(!std::isfinite(fade)||fade<0||fade>=seconds/2.0)throw std::runtime_error("Invalid fade duration");exportFrames(argv[2],seconds,static_cast<unsigned>(std::stoul(argv[4])),theme=="dark",fade);}else runScene(window);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=EXIT_FAILURE;}
    glfwDestroyWindow(window);glfwTerminate();return result;
}
