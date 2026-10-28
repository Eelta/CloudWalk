#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <string>
namespace CloudWalk {
// Standard XInput button bits, plus two virtual trigger bits. No game offsets.
using PadButtons=std::uint32_t;
inline constexpr PadButtons PadB=0x2000,PadBack=0x20,PadStart=0x10;
inline constexpr PadButtons PadLT=0x10000,PadRT=0x20000,PadAllowed=0x3D3CF;
struct PadName {PadButtons bit;const char* name;};
inline constexpr PadName PadNames[]={{1,"↑"},{2,"↓"},{4,"←"},{8,"→"},{0x40,"LS"},{0x80,"RS"},
    {0x100,"LB"},{0x200,"RB"},{0x1000,"A"},{0x4000,"X"},{0x8000,"Y"},{PadLT,"LT"},{PadRT,"RT"}};
inline bool ValidPadChord(PadButtons value){return !(value&~PadAllowed)&&std::popcount(value)<=3;}
inline bool Held(PadButtons buttons,PadButtons chord){return chord&&(buttons&chord)==chord;}
inline bool ValidPadBindings(const std::array<PadButtons,5>& values){
    for(auto a:values)if(!ValidPadChord(a))return false;
    for(unsigned i=0;i<values.size();++i)for(unsigned j=i+1;j<values.size();++j)
        if(Held(values[i],values[j])||Held(values[j],values[i]))return false;
    return true;
}
inline std::string PadLabel(PadButtons value){
    std::string result;
    for(const auto& item:PadNames)if(value&item.bit){if(!result.empty())result+='+';result+=item.name;}
    return result;
}
struct PadSnapshot {
    bool connected=false;int device=-1;PadButtons buttons=0;
    float lx=0,ly=0,rx=0,ry=0;
};
struct PadAxis {float x=0,y=0;};
inline PadAxis PadStick(float x,float y,float deadzone){
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(deadzone))return {};
    const float radius=std::hypot(x,y);deadzone=std::clamp(deadzone,.1f,.8f);
    if(radius<=deadzone)return {};
    const float strength=(std::min(radius,1.f)-deadzone)/(1-deadzone);
    return {x/radius*strength,y/radius*strength};
}
// Focus/menu transitions and reconnection require a neutral sample before use.
class PadEdges {
public:
    void Block(){ready_=false;before_=now_=0;device_=-1;}
    void Sample(const PadSnapshot& value,bool allowed){
        before_=now_;now_=value.buttons;
        if(!allowed||!value.connected||value.device!=device_){
            ready_=false;before_=now_;device_=value.connected?value.device:-1;
        }
        if(allowed&&value.connected&&!value.buttons)ready_=true;
    }
    bool Pressed(PadButtons chord)const{return ready_&&Held(now_,chord)&&!Held(before_,chord);}
    PadButtons Rising()const{return ready_?now_&~before_:0;}
    bool Ready()const{return ready_;}
private:
    bool ready_=false;int device_=-1;PadButtons before_=0,now_=0;
};
enum class PadCaptureResult {waiting,captured,cancelled,invalid};
class PadCapture {
public:
    PadButtons value=0;
    void Begin(){armed_=false;value=0;device_=-1;}
    PadCaptureResult Sample(const PadSnapshot& sample){
        if(!sample.connected||(sample.buttons&(PadB|PadBack|PadStart)))return PadCaptureResult::cancelled;
        if(device_>=0&&sample.device!=device_)return PadCaptureResult::cancelled;
        device_=sample.device;
        if(!armed_){if(!sample.buttons)armed_=true;return PadCaptureResult::waiting;}
        if(!ValidPadChord(sample.buttons))return PadCaptureResult::invalid;
        if(!sample.buttons)return value?PadCaptureResult::captured:PadCaptureResult::waiting;
        // A chord may grow or be released, but rolling disjoint chords are rejected.
        if(value&&(sample.buttons&~value)&&!Held(sample.buttons,value))return PadCaptureResult::invalid;
        if(std::popcount(sample.buttons)>std::popcount(value))value=sample.buttons;
        return PadCaptureResult::waiting;
    }
private:
    bool armed_=false;int device_=-1;
};
// Preserve releases for buttons Skyrim saw before our overlay acquired focus.
class PadInputLease {
public:
    bool Filter(PadButtons bit,bool down,bool up,bool claim){
        if(!bit)return false;
        if(down){native_&=~bit;owned_&=~bit;}
        if(up){const bool consume=(owned_&bit)&&!(native_&bit);owned_&=~bit;native_&=~bit;return consume;}
        if(claim)owned_|=bit;
        const bool consume=(owned_&bit)!=0;
        if(down&&!consume)native_|=bit;
        return consume;
    }
private:
    PadButtons native_=0,owned_=0;
};
template<class Node,class Predicate>void FilterPadQueue(Node*& head,Node*& tail,Predicate remove){
    Node* previous=nullptr;auto* current=head;
    while(current){auto* next=current->next;
        if(remove(current)){if(previous)previous->next=next;else head=next;}
        else previous=current;
        current=next;
    }
    tail=previous;if(tail)tail->next=nullptr;
}
}
