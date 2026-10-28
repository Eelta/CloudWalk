#pragma once
#include <Windows.h>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

namespace CloudWalk {
// The official client's web-message handler explicitly shows its window. A
// foreground lock alone cannot prevent a non-active window covering a game.
// Protect the game's z-order BEFORE dispatch, and leave the client minimized.
// No activation, transparency, window-region, or permanent style changes.
class BackgroundWindowGuard {
public:
    explicit BackgroundWindowGuard(bool monitor=true){
        if(monitor)monitor_=std::jthread([this](std::stop_token stop){
            while(!stop.stop_requested()){
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                std::scoped_lock lock(mutex_);
                if(!game_)continue;
                DWORD process=0;GetWindowThreadProcessId(GetForegroundWindow(),&process);
                if(process!=GetCurrentProcessId()||(GetAsyncKeyState(VK_MENU)&0x8000))ReleaseLocked();
                else MaintainLocked();
            }
        });
    }
    BackgroundWindowGuard(const BackgroundWindowGuard&)=delete;
    BackgroundWindowGuard& operator=(const BackgroundWindowGuard&)=delete;
    ~BackgroundWindowGuard(){if(monitor_.joinable()){monitor_.request_stop();monitor_.join();}Release();}
    bool Begin(HWND game,const std::vector<HWND>& targets){
        std::scoped_lock lock(mutex_);
        DWORD owner=0;GetWindowThreadProcessId(game,&owner);
        if(!game||owner!=GetCurrentProcessId()||!IsWindowVisible(game)||IsIconic(game)||targets.empty())return false;
        // Do not compete with the player's explicit "always on top" setting.
        for(auto window:targets)if(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST)return false;
        if(game_&&game_!=game)ReleaseLocked();
        if(!game_){
            const bool wasTopmost=(GetWindowLongPtrW(game,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;
            if(!wasTopmost&&!SetWindowPos(game,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE))return false;
            if(!(GetWindowLongPtrW(game,GWL_EXSTYLE)&WS_EX_TOPMOST))return false;
            game_=game;changed_=!wasTopmost;
        }
        ObserveLocked(targets);
        if(targets_.empty()){ReleaseLocked();return false;}
        MaintainLocked();return true;
    }
    void Observe(const std::vector<HWND>& targets){
        std::scoped_lock lock(mutex_);ObserveLocked(targets);
    }
    bool Maintain(){std::scoped_lock lock(mutex_);return MaintainLocked();}
    void Release(){std::scoped_lock lock(mutex_);ReleaseLocked();}
    bool Active()const{std::scoped_lock lock(mutex_);return game_!=nullptr;}
private:
    void ObserveLocked(const std::vector<HWND>& targets){
        if(!game_)return;
        for(auto window:targets){
            DWORD process=0;const auto thread=GetWindowThreadProcessId(window,&process);
            if(!process||process==GetCurrentProcessId())continue;
            bool known=false;for(const auto& target:targets_)if(target.window==window&&target.process==process&&target.thread==thread)known=true;
            if(!known)targets_.push_back({window,process,thread});
        }
    }
    // False means the async minimize has not completed; do not remove the
    // game's temporary protection yet. Never send WM_SYSCOMMAND/activate.
    bool MaintainLocked(){
        bool ready=true;
        for(const auto& target:targets_){
            if(!Valid(target)||!IsWindowVisible(target.window)||IsIconic(target.window))continue;
            ready=false;ShowWindowAsync(target.window,SW_SHOWMINNOACTIVE);
        }
        return ready;
    }
    void ReleaseLocked(){
        if(game_&&changed_){
            DWORD owner=0;GetWindowThreadProcessId(game_,&owner);
            // This also runs on the monitor thread. Do not wait on the game's
            // message queue while holding our mutex (or during worker teardown).
            if(owner==GetCurrentProcessId())SetWindowPos(game_,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_ASYNCWINDOWPOS);
        }
        game_=nullptr;changed_=false;targets_.clear();
    }
    struct Target{HWND window;DWORD process,thread;};
    static bool Valid(const Target& target){
        DWORD process=0;const auto thread=GetWindowThreadProcessId(target.window,&process);
        return process==target.process&&thread==target.thread;
    }
    HWND game_=nullptr;bool changed_=false;std::vector<Target> targets_;
    mutable std::mutex mutex_;std::jthread monitor_;
};
}
