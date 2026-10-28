#pragma once
#include <cstdint>
#include <string_view>
namespace CloudWalk {
class WheelSession {
public:
    std::uint64_t Begin(){active_=true;requested_=false;return ++token_;}
    void Cancel(){active_=false;requested_=false;++token_;}
    bool RequestRelease(){if(!active_||requested_)return false;requested_=true;return true;}
    bool Accept(std::uint64_t token){if(!active_||token!=token_)return false;active_=false;return true;}
    std::uint64_t Token() const{return token_;}
private:
    std::uint64_t token_=0;
    bool active_=false,requested_=false;
};
inline bool MenusClearForOverlay(unsigned pauseCount,bool ownsPausedView){return pauseCount<=(ownsPausedView?1u:0u);}
inline bool RequestPlaylist(std::string_view selected,std::string_view current){return !selected.empty()&&selected!=current;}
}
