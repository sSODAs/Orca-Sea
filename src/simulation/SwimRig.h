#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace SwimRig {
constexpr int Count=9;
constexpr float RootX=1.2f, Step=0.8f;
inline float Frequency=0.65f;
inline float LENGTH_SCALE=1.30f;
constexpr float Pi=3.14159265358979323846f;
// Local yaw change per joint. Limits extreme folding during slow turns.
inline float MaxJointYaw=0.32f;
constexpr float FrontJointCap=0.19f; // protect wide pectoral fin region
inline float BendGain=1.15f;
inline float TailAmplitude=0.145f;
inline float AppliedJointYaw=0.0f;
inline float BankDegrees=12.0f;
struct Pose {
    std::array<glm::mat4,Count> skin;
    std::array<glm::vec3,Count> joints;
};
inline float phaseAt(double cycles){
    return static_cast<float>(std::fmod(cycles,1.0)*2.0*Pi);
}
inline float smoothTurn(float current,float target,double dt){
    const float alpha=static_cast<float>(-std::expm1(-3.0*std::max(0.0,dt)));
    return current+(std::clamp(target,-1.0f,1.0f)-current)*alpha;
}
// The array stores desired absolute yaw relative to the root, sampled
// from the travelled path at each joint's distance behind the root.
inline Pose evaluateTrail(double cycles,bool enabled,
                          const std::array<float,Count>& relativeYaw){
    Pose pose{};
    AppliedJointYaw=0.0f;
    glm::vec3 joint(RootX,0,0);
    glm::mat4 rotation(1);
    float yaw=0,pitch=0;
    const float phase=phaseAt(cycles);
    for(int i=0;i<Count;++i){
        const float u=float(i)/float(Count-1);
        if(i>0){
            // Integrate rigid bone segments; no scaling of individual bones.
            joint+=glm::vec3(rotation*glm::vec4(-Step,0,0,0));
            if(enabled){
                const float cap=i<=4?std::min(MaxJointYaw,FrontJointCap):MaxJointYaw;
                const float localYaw=std::clamp(relativeYaw[i]-yaw,-cap,cap);
                yaw+=localYaw;
                AppliedJointYaw=std::max(AppliedJointYaw,std::abs(localYaw));
                // Propulsive motion is vertical, independent of steering.
                pitch+=TailAmplitude*u*std::sin(phase-3.6f*u);
            }
            rotation=glm::rotate(glm::mat4(1),yaw,glm::vec3(0,1,0));
            rotation=glm::rotate(rotation,pitch,glm::vec3(0,0,1));
        }
        pose.joints[i]=joint;
        const float restX=RootX-float(i)*Step;
        pose.skin[i]=glm::translate(glm::mat4(1),joint)*rotation*
                     glm::translate(glm::mat4(1),glm::vec3(-restX,0,0));
    }
    return pose;
}
inline std::array<float,Count> manualYaw(float turn){
    std::array<float,Count> result{};
    for(int i=1;i<Count;++i){
        // A positive root turn leaves the trailing body on the old heading.
        result[i]=result[i-1]-MaxJointYaw*turn*(0.35f+0.65f*float(i)/float(Count-1));
    }
    return result;
}
inline Pose evaluate(double cycles,bool enabled=true,float turn=0){
    return evaluateTrail(cycles,enabled,manualYaw(turn));
}
// position is the ROOT location on the trail, not the mesh centroid.
inline glm::mat4 worldModel(double cycles,bool enabled,float turn,
                            const glm::vec3& position,float heading,float size=1.0f){
    glm::mat4 model=glm::translate(glm::mat4(1),position);
    model=glm::rotate(model,heading,glm::vec3(0,1,0));
    if(enabled){
        turn=std::clamp(turn,-1.0f,1.0f);
        const float phase=phaseAt(cycles);
        const float pitch=glm::radians(1.2f)*std::sin(phase+0.6f);
        // Positive heading turns towards -Z; negative roll banks into that turn.
        const float roll=glm::radians(-BankDegrees)*turn;
        model=glm::rotate(model,pitch,glm::vec3(0,0,1));
        model=glm::rotate(model,roll,glm::vec3(1,0,0));
    }
    model=glm::scale(model,glm::vec3(LENGTH_SCALE*size,size,size));
    return glm::translate(model,glm::vec3(-RootX,0,0));
}
}
