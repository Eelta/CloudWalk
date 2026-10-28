#pragma once
#include <cstdint>
namespace CloudWalk {
enum class ForegroundDispatch {accepted,background,lockFailed,dispatchFailed};
// Covers delayed single-instance IPC without blocking the media worker. Windows
// releases its foreground lock for user-initiated switching; we never pull focus back.
class ForegroundGuard {
public:
    bool Active()const{return active_;}
    void Retain(std::uint64_t now){if(active_)until_=now+10000;}
    template<class Lock,class Unlock,class Send>
    ForegroundDispatch Request(std::uint64_t now,bool foreground,Lock lock,Unlock unlock,Send send){
        if(!foreground){Release(unlock);return ForegroundDispatch::background;}
        // Renew an owned OS lock explicitly; do not assume repeat locks work. The
        // caller keeps window-order protection while releasing/reacquiring only
        // the OS focus lock. ALT may also have released it between polls.
        if(!lock(active_)){Release(unlock);return ForegroundDispatch::lockFailed;}
        active_=true;Retain(now);
        try{
            if(send())return ForegroundDispatch::accepted;
        }catch(...){Release(unlock);throw;}
        Release(unlock);return ForegroundDispatch::dispatchFailed;
    }
    template<class Unlock>void Tick(std::uint64_t now,bool foreground,Unlock unlock){
        if(active_&&(!foreground||now>=until_))Release(unlock);
    }
    template<class Unlock>void Release(Unlock unlock){
        if(active_){active_=false;until_=0;unlock();}
    }
private:
    bool active_=false;std::uint64_t until_=0;
};
}
