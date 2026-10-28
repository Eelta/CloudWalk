#pragma once
#include <memory>
#include <string>
#include <vector>

namespace CloudWalk {
struct ClientState {
    bool connected = false, playing = false;
    bool canPlay = false, canPause = false, canToggle = false, canNext = false, canPrevious = false;
    bool canShuffle = false, canRepeat = false;
    bool canVolume = false, muted = false;
    float volume = 1;
    std::string title, artist, source, error;
};
class MediaClient {
public:
    MediaClient();
    ~MediaClient();
    ClientState Poll();
    void MaintainBackground();
    bool Control(const std::string& action);
    bool Volume(float value);
    bool Playlist(const std::string& id,const std::string& type,std::string* error=nullptr);
    std::vector<std::string> Sources();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
bool IsNeteaseSource(const std::string& source);
}
