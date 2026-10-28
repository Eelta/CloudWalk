#pragma once
#include <cstdint>
namespace CloudWalk {
// Own only controls that were enabled when the passive wheel opened. Do not
// overwrite Skyrim's stored-control snapshot or enable pre-disabled controls.
class WheelControls {
public:
    template<class Toggle>void Acquire(std::uint32_t enabled,Toggle toggle){
        if(active_)return;
        active_=true;owned_=enabled;if(owned_)toggle(owned_,false);
    }
    template<class Toggle>void Release(Toggle toggle){
        if(!active_)return;
        const auto owned=owned_;active_=false;owned_=0;if(owned)toggle(owned,true);
    }
    bool Active()const{return active_;}
private:
    bool active_=false;std::uint32_t owned_=0;
};
}
