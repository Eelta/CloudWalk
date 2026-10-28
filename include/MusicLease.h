#pragma once
#include <algorithm>
#include <vector>
#include <cstdint>
namespace CloudWalk {
class MusicGainLease {
    bool active=false;
    float savedVolume=1;
    std::uint16_t savedStatic=0;
public:
    bool Active() const{return active;}
    void Update(bool on,float& volume,std::uint16_t& staticVolume){
        if(on){
            if(!active){savedVolume=volume;savedStatic=staticVolume;}
            volume=0;staticVolume=0;active=true;
        }else if(active){
            if(volume==0)volume=savedVolume;
            if(staticVolume==0)staticVolume=savedStatic;
            active=false;
        }
    }
};
// Restore only music we paused ourselves and which is still owned by the music manager.
template<class T> class MusicLease {
    std::vector<T*> owned;
public:
    template<class Playing,class Paused,class Pause,class Resume>
    void Update(bool on,const std::vector<T*>& live,Playing playing,Paused paused,Pause pause,Resume resume) {
        if(on){
            for(auto* music:live)if(music&&playing(music)){
                pause(music);
                if(std::find(owned.begin(),owned.end(),music)==owned.end())owned.push_back(music);
            }
        }else{
            for(auto* music:owned)if(std::find(live.begin(),live.end(),music)!=live.end()&&paused(music))resume(music);
            owned.clear();
        }
    }
};
}
