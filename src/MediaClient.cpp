#include <Windows.h>
#include <shellapi.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Media.Control.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <stdexcept>
#include "MediaClient.h"
#include "Playlists.h"
#include "ForegroundGuard.h"
#include "BackgroundWindowGuard.h"

namespace CloudWalk {
using namespace std::chrono_literals;
using namespace winrt::Windows::Media::Control;
using Microsoft::WRL::ComPtr;
namespace {
bool CallerForeground(){
    DWORD id=0;GetWindowThreadProcessId(GetForegroundWindow(),&id);
    return id==GetCurrentProcessId()&&!(GetAsyncKeyState(VK_MENU)&0x8000);
}
void UnlockForeground(){LockSetForegroundWindow(LSFW_UNLOCK);}
template<class Operation> auto Await(Operation operation) {
    if (operation.wait_for(1500ms) != winrt::Windows::Foundation::AsyncStatus::Completed) {
        operation.Cancel(); throw std::runtime_error("Media session request timed out");
    }
    return operation.GetResults();
}
bool NeteaseProcess(DWORD id) {
    if (!id) return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id);
    if (!process) return false;
    wchar_t path[32768]{}; DWORD count = static_cast<DWORD>(std::size(path));
    const bool ok = QueryFullProcessImageNameW(process, 0, path, &count);
    CloseHandle(process);
    if (!ok) return false;
    const auto name = wcsrchr(path, L'\\');
    return _wcsicmp(name ? name + 1 : path, L"cloudmusic.exe") == 0;
}
std::vector<HWND> NeteaseWindows(){
    std::vector<HWND> windows;
    EnumWindows([](HWND window,LPARAM data)->BOOL{
        // Only independent application windows; leave tool windows (lyrics,
        // tray helpers), child windows, and other applications alone.
        if(GetWindow(window,GW_OWNER)||(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOOLWINDOW))return TRUE;
        DWORD id=0;GetWindowThreadProcessId(window,&id);
        if(NeteaseProcess(id))reinterpret_cast<std::vector<HWND>*>(data)->push_back(window);
        return TRUE;
    },reinterpret_cast<LPARAM>(&windows));
    return windows;
}
std::vector<ComPtr<ISimpleAudioVolume>> Volumes() {
    std::vector<ComPtr<ISimpleAudioVolume>> result;
    ComPtr<IMMDeviceEnumerator> devices;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&devices)))) return result;
    ComPtr<IMMDeviceCollection> endpoints;
    if (FAILED(devices->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &endpoints))) return result;
    UINT count = 0; endpoints->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device; ComPtr<IAudioSessionManager2> manager; ComPtr<IAudioSessionEnumerator> sessions;
        if (FAILED(endpoints->Item(i, &device)) || FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, &manager)) || FAILED(manager->GetSessionEnumerator(&sessions))) continue;
        int n = 0; sessions->GetCount(&n);
        for (int j = 0; j < n; ++j) {
            ComPtr<IAudioSessionControl> control; ComPtr<IAudioSessionControl2> info; ComPtr<ISimpleAudioVolume> volume;
            DWORD pid = 0;
            if (FAILED(sessions->GetSession(j, &control)) || FAILED(control.As(&info)) || FAILED(info->GetProcessId(&pid)) || !NeteaseProcess(pid)) continue;
            if (SUCCEEDED(control.As(&volume))) result.push_back(std::move(volume));
        }
    }
    return result;
}
}
bool IsNeteaseSource(const std::string& source) {
    std::string name = source;
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : static_cast<char>(c); });
    return name.find("cloudmusic") != std::string::npos || name.find("neteasemusic") != std::string::npos ||
        name.find("netease.music") != std::string::npos || name.find("music.163") != std::string::npos || name.find("网易云音乐") != std::string::npos;
}
struct MediaClient::Impl {
    ForegroundGuard foreground;
    BackgroundWindowGuard windows;
    void Release(){windows.Release();UnlockForeground();}
    ~Impl(){foreground.Release([this]{Release();});}
    GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
    GlobalSystemMediaTransportControlsSession session{nullptr};
    void Refresh() {
        if (!manager) manager = Await(GlobalSystemMediaTransportControlsSessionManager::RequestAsync());
        const auto current = manager.GetCurrentSession();
        if (current && IsNeteaseSource(winrt::to_string(current.SourceAppUserModelId()))) { session = current; return; }
        session = nullptr;
        for (const auto& candidate : manager.GetSessions()) {
            if (IsNeteaseSource(winrt::to_string(candidate.SourceAppUserModelId()))) { session = candidate; break; }
        }
    }
};
MediaClient::MediaClient() : impl(std::make_unique<Impl>()) {}
MediaClient::~MediaClient() = default;
void MediaClient::MaintainBackground(){
    if(!impl->foreground.Active())return;
    const auto now=GetTickCount64();const bool foreground=CallerForeground();
    // User switching always wins. Otherwise wait for the asynchronous minimize
    // before dropping z-order protection, including after a slow client reply.
    if(foreground){
        impl->windows.Observe(NeteaseWindows());
        if(!impl->windows.Maintain())impl->foreground.Retain(now);
    }
    impl->foreground.Tick(now,foreground,[this]{impl->Release();});
}
ClientState MediaClient::Poll() {
    ClientState result;
    MaintainBackground();
    try {
        impl->Refresh();
        if (!impl->session) { result.error = "请在游戏外打开网易云音乐，并先播放一首歌曲"; return result; }
        const auto session = impl->session;
        const auto playback = session.GetPlaybackInfo();
        const auto controls = playback.Controls();
        result.source = winrt::to_string(session.SourceAppUserModelId());
        result.connected = true;
        result.playing = playback.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
        result.canPlay = controls.IsPlayEnabled(); result.canPause = controls.IsPauseEnabled();
        result.canToggle = controls.IsPlayPauseToggleEnabled() || (result.playing ? result.canPause : result.canPlay);
        result.canNext = controls.IsNextEnabled(); result.canPrevious = controls.IsPreviousEnabled();
        result.canShuffle=controls.IsShuffleEnabled();result.canRepeat=controls.IsRepeatEnabled();
        try {
            auto properties = Await(session.TryGetMediaPropertiesAsync());
            result.title = winrt::to_string(properties.Title()); result.artist = winrt::to_string(properties.Artist());
        } catch (...) { result.error = "已连接网易云，暂时无法读取歌名"; }
        auto volumes = Volumes();
        if (!volumes.empty() && SUCCEEDED(volumes.front()->GetMasterVolume(&result.volume))) {
            result.canVolume = true; BOOL muted = FALSE;
            if (SUCCEEDED(volumes.front()->GetMute(&muted))) result.muted = muted != FALSE;
        }
    } catch (...) { result = ClientState{}; result.error = "无法读取网易云媒体会话，请检查客户端系统媒体控制设置"; impl->session = nullptr; }
    return result;
}
bool MediaClient::Control(const std::string& action) {
    try {
        impl->Refresh(); if (!impl->session) return false;
        auto s = impl->session;
        auto info = s.GetPlaybackInfo();
        auto controls = info.Controls();
        const auto status=info.PlaybackStatus();
        if(action=="play"&&status==GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing)return true;
        if(action=="pause"&&(status==GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused||status==GlobalSystemMediaTransportControlsSessionPlaybackStatus::Stopped||status==GlobalSystemMediaTransportControlsSessionPlaybackStatus::Closed))return true;
        if (action == "play" && controls.IsPlayEnabled()) return Await(s.TryPlayAsync());
        if (action == "pause" && controls.IsPauseEnabled()) return Await(s.TryPauseAsync());
        if (action == "toggle") {
            if (controls.IsPlayPauseToggleEnabled()) return Await(s.TryTogglePlayPauseAsync());
            if (s.GetPlaybackInfo().PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing && controls.IsPauseEnabled()) return Await(s.TryPauseAsync());
            if (controls.IsPlayEnabled()) return Await(s.TryPlayAsync());
        }
        if (action == "next" && controls.IsNextEnabled()) return Await(s.TrySkipNextAsync());
        if (action == "previous" && controls.IsPreviousEnabled()) return Await(s.TrySkipPreviousAsync());
    } catch (...) { impl->session = nullptr; }
    return false;
}
bool MediaClient::Volume(float value) {
    if (!std::isfinite(value) || value < 0 || value > 1) return false;
    auto volumes = Volumes(); bool ok = !volumes.empty();
    for (auto& volume : volumes) ok = SUCCEEDED(volume->SetMasterVolume(value, nullptr)) && ok;
    return ok;
}
bool MediaClient::Playlist(const std::string& id,const std::string& type,std::string* error) {
    if(error)*error="未能发送歌单请求，请检查网易云客户端及 orpheus 协议关联";
    try {
        // Protocol delivery uses validated client windows, not a media-session
        // operation. Do not block station changes on metadata/session refresh.
        // The installed official PC client's web-command parser accepts base64 JSON.
        // Never execute a user-supplied URI: build the command from a validated ID.
        const auto uri=winrt::to_hstring(PlaylistProtocol(id,type));
        SHELLEXECUTEINFOW request{};request.cbSize=sizeof(request);
        request.fMask=SEE_MASK_FLAG_NO_UI|SEE_MASK_NOASYNC;
        request.lpVerb=L"open";request.lpFile=uri.c_str();request.nShow=SW_SHOWNOACTIVATE;
        bool lostForeground=false,windowProtectionFailed=false;
        const auto result=impl->foreground.Request(GetTickCount64(),CallerForeground(),
            [](bool renewal){if(renewal)UnlockForeground();return LockSetForegroundWindow(LSFW_LOCK)!=FALSE;},[this]{impl->Release();},
            [&]{
                if(!CallerForeground()){lostForeground=true;return false;}
                if(!impl->windows.Begin(GetForegroundWindow(),NeteaseWindows())){windowProtectionFailed=true;return false;}
                if(!CallerForeground()){lostForeground=true;return false;}
                return ShellExecuteExW(&request)!=FALSE;
            });
        if((result==ForegroundDispatch::background||lostForeground)&&error)*error="请在游戏前台且松开 Alt 后切换电台";
        if(result==ForegroundDispatch::lockFailed&&error)*error="无法启用前台保护，已取消歌单切换";
        if(windowProtectionFailed&&error)*error="无法保护游戏窗口，请关闭网易云窗口置顶并确保客户端已打开";
        if(result==ForegroundDispatch::accepted){impl->foreground.Retain(GetTickCount64());if(error)error->clear();return true;}
        return false;
    }catch(...){return false;}
}
std::vector<std::string> MediaClient::Sources() {
    std::vector<std::string> sources;
    try { impl->Refresh(); for (const auto& s : impl->manager.GetSessions()) sources.push_back(winrt::to_string(s.SourceAppUserModelId())); } catch (...) {}
    return sources;
}
}
