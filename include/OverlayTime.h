#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace CloudWalk {
class PauseLease {
public:
    bool Active() const{return active_;}
    void Update(bool on,std::uint32_t& count){
        if(on&&!active_){++count;active_=true;}
        else if(!on&&active_){if(count)--count;active_=false;}
    }
private:
    bool active_=false;
};
struct TimePair {float current,target;};
class TimeScaleLease {
public:
    bool Active() const{return active_;}
    template<class Setter>void Update(bool on,float factor,TimePair now,Setter set){
        if(!on){
            if(active_&&!displaced_&&Near(now.current,applied_.current)&&Near(now.target,applied_.target))set(saved_);
            active_=false;displaced_=false;return;
        }
        if(!active_){
            if(!std::isfinite(now.current)||!std::isfinite(now.target)||now.current<=0||now.target<0)return;
            saved_=applied_=now;active_=true;displaced_=false;
        }
        if(!Near(now.current,applied_.current)||!Near(now.target,applied_.target))displaced_=true;
        if(displaced_)return;
        const float value=saved_.current*std::clamp(factor,0.000001f,1.0f);
        if(!Near(value,applied_.current)||!Near(value,applied_.target)){applied_={value,value};set(applied_);}
    }
private:
    static bool Near(float a,float b){return std::abs(a-b)<=0.000001f*std::max({1.0f,std::abs(a),std::abs(b)});}
    bool active_=false,displaced_=false;
    TimePair saved_{1,1},applied_{1,1};
};
}
