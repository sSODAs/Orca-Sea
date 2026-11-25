#pragma once
#include "SwimPath.h"
#include <array>
namespace PodMotion {
constexpr int Count=5;
// Order is near to far in the fixed top-down camera. Scale simulates depth
// under an orthographic camera, while Y establishes actual depth testing.
inline constexpr std::array<float,Count> Sizes{.88f,.66f,.51f,.38f,.29f};
inline constexpr std::array<float,Count> Opacity{1.f,.88f,.75f,.61f,.48f};
inline constexpr std::array<float,Count> Height{14.0f,10.8f,7.6f,4.4f,1.2f};
inline constexpr std::array<float,Count> Rates{1.0f,.94f,.86f,.81f,.76f};
struct Input {const SwimPath::Wander* path; double size,desired;};
inline double gap(const Input& a,const Input& b,double t,double va,double vb,double length){
    // Approximate swept body by three points along the committed trail.
    // This is screen-space clearance, not triangle-mesh collision detection.
    double d=1e9;
    for(double u:{1.1,-1.2,-3.5})for(double v:{1.1,-1.2,-3.5}){
        auto p=a.path->pointAtOffset(t*va+u*a.size*length);
        auto q=b.path->pointAtOffset(t*vb+v*b.size*length);
        d=std::min(d,SwimPath::distance(p,q));
    }
    return d-(a.size+b.size)*1.15;
}
inline std::array<double,Count> speeds(const std::array<Input,Count>& a,double length,bool enabled){
    std::array<double,Count> s{};
    for(int i=0;i<Count;++i){
        s[i]=a[i].desired;
        if(!enabled)continue;
        // The near agent has right of way; the farther agent can yield.
        // Evaluate several candidate speeds over a 1.8 second horizon.
        double best=-1e30,chosen=s[i];
        for(double f:{1.,.8,.55,.30,.15}){
            double v=std::max(.35,s[i]*f),risk=0;
            for(int j=0;j<i;++j)for(double t:{.45,.90,1.35,1.80}){
                double g=gap(a[i],a[j],t,v,s[j],length);
                risk+=std::pow(std::max(0.,.65-g),2);
            }
            double score=f-1.5*risk;
            if(score>best){best=score;chosen=v;}
        }
        s[i]=chosen;
    }
    return s;
}
}
