#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <random>
#include <stdexcept>
#include <vector>

// Online random motion primitives, parameterized by travelled arc length.
// Heading + rotates +X towards -Z. Curvature is continuous at joins.
namespace SwimPath {
constexpr double Pi=3.14159265358979323846;
constexpr double SafeX=16.0, SafeZ=8.0;
constexpr double MaxCurvature=0.23, Ramp=2.0, DS=0.05, PlanAhead=20.0;
struct Point {double x=0,z=0;};
struct State {Point position{}; float heading=0,turn=0;};
inline double wrap(double a){return std::remainder(a,2*Pi);}
inline double distance(Point a,Point b){return std::hypot(a.x-b.x,a.z-b.z);}
inline double direction(Point a,Point b){return std::atan2(a.z-b.z,b.x-a.x);}
class Wander {
    struct Sample {double s; Point p; double h,k;};
    std::mt19937 rng;
    std::deque<Sample> samples;
    std::deque<Sample> endpoints;
    double arc_=0,speed_=2.4,targetSpeed_=2.4;
    std::uint64_t arrivals_=0;
    Point randomGoal_{};
    double unit(){return double(rng())/4294967296.0;}
    bool inside(Point p)const{return std::abs(p.x)<=SafeX && std::abs(p.z)<=SafeZ;}
    static Sample advanceSample(Sample a,double k,double ds){
        // Integrate linear curvature over ds using midpoint quadrature.
        double hm=a.h+ds*(3*a.k+k)/8.0;
        a.p.x+=ds*std::cos(hm);a.p.z-=ds*std::sin(hm);
        a.h+=ds*(a.k+k)*0.5;a.k=k;a.s+=ds;return a;
    }
    bool build(Sample start,double targetK,double length,std::vector<Sample>& out) const {
        out.clear();Sample a=start;
        const int n=std::max(1,int(std::ceil(length/DS)));
        const double ds=length/n;
        for(int i=1;i<=n;++i){
            double t=std::min(1.0,i*ds/Ramp);
            a=advanceSample(a,start.k+(targetK-start.k)*t,ds);
            if(!inside(a.p))return false;
            out.push_back(a);
        }
        return true;
    }
    bool safeTurn(Sample a,double k) const {
        std::vector<Sample> ramp;
        if(!build(a,k,Ramp,ramp))return false;
        a=ramp.back();
        // A complete constant-curvature circle must fit after the ramp.
        const double r=1/std::abs(k);
        const double cx=a.p.x-std::sin(a.h)/k, cz=a.p.z-std::cos(a.h)/k;
        return std::abs(cx)+r<=SafeX-0.06 && std::abs(cz)+r<=SafeZ-0.06;
    }
    bool viable(Sample a)const{return safeTurn(a,MaxCurvature)||safeTurn(a,-MaxCurvature);}
    void append(){
        const Sample start=samples.back();
        // Generate a fresh random destination in the world. Select a feasible
        // forward primitive toward it; never chase the destination after moving past it.
        randomGoal_={(2*unit()-1)*(SafeX-0.5),(2*unit()-1)*(SafeZ-0.5)};
        std::vector<Sample> trial,best;
        double bestScore=-1e30;
        for(int i=0;i<48;++i){
            double k=i==0?0:(2*unit()-1)*MaxCurvature;
            double len=4.0+3.0*unit();
            if(!build(start,k,len,trial)||!viable(trial.back()))continue;
            const auto& e=trial.back();
            double gain=distance(start.p,randomGoal_)-distance(e.p,randomGoal_);
            // Prefer progress, broad turns and non-retracing headings.
            // Soft anti-retrace penalty: avoid returning to the recent trail.
            // It is not a collision constraint; a safety turn still takes priority.
            double nearest=4.0;
            for(std::size_t j=0;j<samples.size();j+=20){
                const auto& old=samples[j];
                if(old.s>=0 && e.s-old.s>10.0 && e.s-old.s<35.0)
                    nearest=std::min(nearest,distance(e.p,old.p));
            }
            double score=gain-1.1*std::abs(k)/MaxCurvature-2.0*std::max(0.0,3.5-nearest)+0.45*unit();
            if(score>bestScore){bestScore=score;best=trial;}
        }
        if(best.empty()){
            // A viable previous endpoint guarantees at least one safe turn.
            for(double k:{MaxCurvature,-MaxCurvature}){
                if(safeTurn(start,k)&&build(start,k,Ramp,trial)&&viable(trial.back())){best=trial;break;}
            }
        }
        if(best.empty())throw std::runtime_error("No viable trail extension");
        for(const auto& p:best)samples.push_back(p);
        endpoints.push_back(best.back());
    }
    void fill(){while(samples.back().s-arc_<PlanAhead)append();}
    Sample at(double s)const{
        if(s<=samples.front().s)return samples.front();
        auto it=std::lower_bound(samples.begin(),samples.end(),s,[](const Sample&a,double b){return a.s<b;});
        if(it==samples.end())return samples.back();
        auto a=it-1;double t=(s-a->s)/(it->s-a->s);
        return {s,{a->p.x+(it->p.x-a->p.x)*t,a->p.z+(it->p.z-a->p.z)*t},a->h+(it->h-a->h)*t,a->k+(it->k-a->k)*t};
    }
public:
    explicit Wander(std::uint32_t seed=42){reset(seed);}
    void reset(std::uint32_t seed){
        rng.seed(seed);arc_=0;speed_=targetSpeed_;arrivals_=0;samples.clear();endpoints.clear();
        // Choose a feasible initial heading; both full circles need not fit.
        Sample start{0,{0,0},(2*unit()-1)*Pi,0};
        for(int i=0;i<100&&!viable(start);++i)start.h=(2*unit()-1)*Pi;
        if(!viable(start))throw std::runtime_error("No viable initial heading");
        // Straight heading prehistory for a neutral initial rig (not displayed).
        samples.push_back({-64,{ -64*std::cos(start.h),64*std::sin(start.h)},start.h,0});
        samples.push_back(start);fill();
    }
    void retarget(std::uint32_t seed){rng.seed(seed);/* keep committed trail; future extensions use new seed */}
    void setSpeed(double v){targetSpeed_=std::clamp(v,0.35,8.0);}
    State state()const{auto a=at(arc_);return {a.p,float(wrap(a.h)),float(a.k/MaxCurvature)};}
    Point target()const{return endpoints.empty()?at(arc_).p:endpoints.front().p;}
    Point randomGoal()const{return randomGoal_;}
    std::vector<Point> targets()const{std::vector<Point> p;for(const auto& a:endpoints)p.push_back(a.p);return p;}
    std::vector<Point> future()const{std::vector<Point> p{at(arc_).p};for(double s=arc_+0.15;s<samples.back().s;s+=0.15)p.push_back(at(s).p);p.push_back(samples.back().p);return p;}
    double speed()const{return speed_;}
    double curvature()const{return at(arc_).k;}
    double yawRate()const{return speed_*curvature();}
    double ahead()const{return samples.back().s-arc_;}
    std::uint64_t arrivals()const{return arrivals_;}
    Point pointAtOffset(double offset) const {return at(arc_+offset).p;}
    double relativeHeadingBehind(double offset)const{return at(arc_-std::max(0.0,offset)).h-at(arc_).h;}
    void step(double dt){
        dt=std::clamp(dt,0.0,0.1);if(dt==0)return;
        speed_+=std::clamp((targetSpeed_-speed_)*(-std::expm1(-2.0*dt)),-1.5*dt,1.5*dt);
        arc_+=speed_*dt;
        while(!endpoints.empty()&&endpoints.front().s<=arc_){endpoints.pop_front();++arrivals_;}
        while(samples.size()>2&&samples[1].s<arc_-64)samples.pop_front();
        fill();
    }
};
}
