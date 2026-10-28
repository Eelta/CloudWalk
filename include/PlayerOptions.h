#pragma once
#include <cmath>
#include <string>
#include "ControllerInput.h"
namespace CloudWalk {
struct PlayerOptions {
    bool alwaysShow=false;
    float wheelSlowdown=.9f;
    std::string language="zh";
    bool controllerEnabled=true;
    float controllerDeadzone=.25f;
    std::array<PadButtons,5> padBindings{0x4100,0x1100,0x108,0x104,0x300};
    int uiKey=28,toggleKey=7,nextKey=9,previousKey=8,wheelKey=47;
    bool Valid() const {
        if(!std::isfinite(wheelSlowdown)||wheelSlowdown<0||wheelSlowdown>1||(language!="zh"&&language!="en"))return false;
        if(!std::isfinite(controllerDeadzone)||controllerDeadzone<.1f||controllerDeadzone>.8f||!ValidPadBindings(padBindings))return false;
        const int keys[]={uiKey,toggleKey,nextKey,previousKey,wheelKey};
        for(int i=0;i<5;++i){
            if(keys[i]<0||keys[i]>255||keys[i]==1)return false;
            for(int j=i+1;j<5;++j)if(keys[i]&&keys[i]==keys[j])return false;
        }
        return true;
    }
};
}
