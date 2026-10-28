#include "PCH.h"
#include <ShlObj.h>
#include <Xinput.h>
#include <winrt/base.h>
#include <condition_variable>
#include "MediaClient.h"
#include "PlayerOptions.h"
#include "SettingsMigration.h"
#include "MusicLease.h"
#include "PlayerInput.h"
#include "Playlists.h"
#include "RuntimeCompatibility.h"
#include "WheelSession.h"
#include "WheelControls.h"
#include "OverlayTime.h"
#include "Localization.h"
#include "SKSEMenuFramework.h"
#include "PrismaUI_API.h"

using Json = nlohmann::json;
using namespace std::chrono_literals;
namespace CW {
using Options=CloudWalk::PlayerOptions;
inline PRISMA_UI_API::IVPrismaUI1* prisma = nullptr;
inline std::atomic<PrismaView> view{0};
inline std::atomic<bool> viewReady{false}, viewOpen{false};
inline std::atomic<bool> viewVisible{false},alwaysVisible{false},allowHud{false},wheelOpen{false};
inline std::atomic<bool> uiStateNeeded{true};
inline std::atomic<ULONGLONG> openedAt{0};
inline std::atomic<ULONGLONG> closedAt{0},settingsRenderedAt{0};
inline bool (*settingsOpen)()=nullptr;
inline CloudWalk::WheelSession wheelSession;
inline CloudWalk::WheelControls wheelControls;
inline bool mouseGuardReady=false;
void ToggleWheelControls(std::uint32_t flags,bool enabled){
    if(auto* controls=RE::ControlMap::GetSingleton())controls->ToggleControls(static_cast<RE::UserEvents::USER_EVENT_FLAG>(flags),enabled,false);
}
inline unsigned heldWheelKey=0;
inline bool controllerOverlay=false,padAimReady=false;
inline CloudWalk::PadButtons heldPadChord=0;
inline int overlayPadDevice=-1;
inline std::uint64_t overlayEpoch=0;
inline constexpr const char* padSettingNames[]={"padLayout","padToggle","padNext","padPrevious","padWheel"};
CloudWalk::PadSnapshot ReadController(int device=-1){
    for(int i=device<0?0:device;i<(device<0?4:device+1);++i){
        XINPUT_STATE state{};if(XInputGetState(i,&state)!=ERROR_SUCCESS)continue;
        const auto& g=state.Gamepad;
        CloudWalk::PadSnapshot sample;sample.connected=true;sample.device=i;
        sample.buttons=g.wButtons&0xF3FF;
        if(g.bLeftTrigger>=128)sample.buttons|=CloudWalk::PadLT;
        if(g.bRightTrigger>=128)sample.buttons|=CloudWalk::PadRT;
        const auto axis=[](SHORT value){return value<0?value/32768.f:value/32767.f;};
        sample.lx=axis(g.sThumbLX);sample.ly=axis(g.sThumbLY);sample.rx=axis(g.sThumbRX);sample.ry=axis(g.sThumbRY);
        return sample;
    }
    return {};
}
inline ULONGLONG wheelReleaseAt=0;
inline std::atomic<float> overlaySlowdown{.9f};
inline std::atomic<bool> english{false};
inline CloudWalk::PauseLease overlayPause;
inline CloudWalk::TimeScaleLease overlayTime;
unsigned VirtualKey(unsigned scan){
    if(scan==197)return VK_PAUSE;if(scan==183)return VK_SNAPSHOT;if(scan==69)return VK_NUMLOCK;
    return MapVirtualKeyW((scan&0x7f)|((scan&0x80)?0xe000:0),MAPVK_VSC_TO_VK_EX);
}
bool PhysicalKeyDown(unsigned scan){const auto vk=VirtualKey(scan);return vk&&(GetAsyncKeyState(vk)&0x8000)!=0;}
bool GameForeground(){DWORD id=0;GetWindowThreadProcessId(GetForegroundWindow(),&id);return id==GetCurrentProcessId();}
void UpdateOverlayTiming(){
    const float strength=overlaySlowdown.load();const bool on=viewOpen&&GameForeground();
    const bool slow=on&&strength>0&&strength<1;
    if(slow||overlayTime.Active())if(auto* timer=RE::BSTimer::GetSingleton()){
        const CloudWalk::TimePair now{RE::BSTimer::QGlobalTimeMultiplier(),RE::BSTimer::QGlobalTimeMultiplierTarget()};
        overlayTime.Update(slow,1-strength,now,[timer](CloudWalk::TimePair value){
            timer->SetGlobalTimeMultiplier(value.current,true);
            if(timer->useGlobalTimeMultiplierTarget&&value.target!=value.current)timer->SetGlobalTimeMultiplier(value.target,false);
        });
    }
    if(auto* ui=RE::UI::GetSingleton())overlayPause.Update(on&&strength>=1,ui->numPausesGame);
}
void ReleaseWheel(){
    if(wheelOpen&&wheelSession.RequestRelease()&&prisma){wheelReleaseAt=GetTickCount64();prisma->InteropCall(view,"cloudwalkReleaseWheel",std::to_string(wheelSession.Token()).c_str());}
}
Json Config(const Options& o){
    Json value={{"uiKey",o.uiKey},{"toggleKey",o.toggleKey},{"nextKey",o.nextKey},{"previousKey",o.previousKey},{"wheelKey",o.wheelKey},{"alwaysShow",o.alwaysShow},{"wheelSlowdown",o.wheelSlowdown},{"language",o.language},{"controllerEnabled",o.controllerEnabled},{"controllerDeadzone",o.controllerDeadzone}};
    for(unsigned i=0;i<5;++i)value[padSettingNames[i]]=o.padBindings[i];return value;
}
Options Patch(const Options& original,const Json& patch){
    if(!patch.is_object())throw std::runtime_error("Settings must be an object");
    auto values=Config(original);
    for(const auto& [key,value]:patch.items()){
        if(!values.contains(key))throw std::runtime_error("Invalid setting");
        const bool valid=(key=="alwaysShow"||key=="controllerEnabled")?value.is_boolean():(key=="wheelSlowdown"||key=="controllerDeadzone")?value.is_number():key=="language"?value.is_string():value.is_number_integer();
        if(!valid)throw std::runtime_error("Invalid setting");
        if(key.starts_with("pad")&&(value<0||value>0x3FFFF))throw std::runtime_error("Invalid controller chord");
        values[key]=value;
    }
    Options o;o.uiKey=values.at("uiKey");o.toggleKey=values.at("toggleKey");o.nextKey=values.at("nextKey");o.previousKey=values.at("previousKey");o.wheelKey=values.at("wheelKey");o.alwaysShow=values.at("alwaysShow");o.wheelSlowdown=values.at("wheelSlowdown");o.language=values.at("language");
    o.controllerEnabled=values.at("controllerEnabled");o.controllerDeadzone=values.at("controllerDeadzone");
    for(unsigned i=0;i<5;++i)o.padBindings[i]=values.at(padSettingNames[i]);
    if(!o.Valid())throw std::runtime_error("Invalid hotkeys");return o;
}
void SetPanelVisible(bool show){
    if(!prisma||!viewReady||viewVisible.exchange(show)==show)return;
    if(show){uiStateNeeded=true;prisma->Show(view);if(!viewOpen)prisma->InteropCall(view,"cloudwalkInteraction","0");}
    else prisma->Hide(view);
}
void ClosePlayer() {
    if (!prisma || !view || !viewOpen.exchange(false)) return;
    const bool ownedFocus=!wheelOpen;
    wheelOpen=false;wheelSession.Cancel();heldWheelKey=0;wheelReleaseAt=0;
    wheelControls.Release(ToggleWheelControls);
    controllerOverlay=false;heldPadChord=0;overlayPadDevice=-1;++overlayEpoch;
    closedAt=GetTickCount64();UpdateOverlayTiming();
    // Queue cleanup even when an earlier Focus operation has not executed yet.
    if(ownedFocus)prisma->Unfocus(view);prisma->InteropCall(view,"cloudwalkClosed","");
    if(alwaysVisible&&allowHud){SetPanelVisible(true);prisma->InteropCall(view,"cloudwalkInteraction","0");}
    else SetPanelVisible(false);
}
std::string KeyLabel(int key);
bool SettingsBlocks(){return CloudWalk::SettingsMenuBlocks(settingsOpen!=nullptr,settingsOpen&&settingsOpen(),GetTickCount64()-settingsRenderedAt.load());}
CloudWalk::MenuTraits MenuState(const RE::IMenu& menu){
    using Flag=RE::UI_MENU_FLAGS;using Context=RE::UserEvents::INPUT_CONTEXT_ID;
    bool context=menu.UsesMenuContext()&&menu.inputContext.get()!=Context::kGameplay;
    switch(menu.inputContext.get()){
    case Context::kMenuMode:case Context::kConsole:case Context::kItemMenu:case Context::kInventory:case Context::kFavorites:
    case Context::kMap:case Context::kStats:case Context::kCursor:case Context::kBook:case Context::kJournal:case Context::kLockpicking:context=true;break;
    default:break;
    }
    return {menu.menuFlags.any(Flag::kAlwaysOpen),menu.PausesGame(),menu.menuFlags.any(Flag::kUsesCursor),context,menu.menuFlags.any(Flag::kModal),menu.InventoryItemMenu()};
}
bool CanOpenPlayer(std::string* reason=nullptr) {
    const auto blocked=[reason](const char* text){if(reason)*reason=text;return false;};
    const auto ui=RE::UI::GetSingleton();
    if(!ui)return blocked("UI unavailable");
    if(!CloudWalk::MenusClearForOverlay(ui->numPausesGame,overlayPause.Active()))return blocked("another pause owner");
    if(!ui->IsShowingMenus())return blocked("HUD hidden");
    if(SettingsBlocks())return blocked("SKSE settings open");
    const auto camera=RE::PlayerCamera::GetSingleton();
    if(camera&&camera->currentState&&camera->currentState->id==RE::CameraState::kFree)return blocked("free camera");
    const bool ownFocus=viewOpen.load();
    for(const auto& [name,entry]:ui->menuMap){
        if(entry.menu&&entry.menu->OnStack()&&CloudWalk::MenuBlocksOverlay(name.c_str(),ownFocus,MenuState(*entry.menu))){
            if(reason)*reason=std::string("menu: ")+name.c_str()+" flags="+std::to_string(entry.menu->menuFlags.underlying())+" context="+std::to_string(static_cast<unsigned>(entry.menu->inputContext.get()));
            return false;
        }
    }
    // SKSE Menu Framework and other Prisma views may not have a Scaleform menu.
    if(prisma&&prisma->HasAnyActiveFocus()&&!prisma->HasFocus(view))return blocked("another Prisma view owns focus");
    return true;
}
void OpenOverlay(bool wheel,unsigned scan=0,bool controller=false,int device=-1,CloudWalk::PadButtons chord=0) {
    if(!prisma||!viewReady){SKSE::log::warn("Overlay open ignored: Prisma view not ready");return;}
    if(viewOpen&&wheelOpen==wheel)return;
    // Finish layout editing before holding the wheel key. A focused Prisma
    // view draws its own native cursor, independently of CSS cursor:none.
    if(viewOpen&&!wheelOpen&&wheel)return;
    if(wheel&&(!mouseGuardReady||!RE::ControlMap::GetSingleton())){SKSE::log::warn("Wheel unavailable: relative mouse guard or control map missing");return;}
    if(!viewOpen){
        std::string reason;
        if(!CanOpenPlayer(&reason)){SKSE::log::info("Overlay open blocked: {}",reason);return;}
        if(!allowHud||GetTickCount64()-closedAt.load()<250||prisma->HasAnyActiveFocus()){SKSE::log::info("Overlay open deferred: hudAllowed={} focus={}",allowHud.load(),prisma->HasAnyActiveFocus());return;}
        SetPanelVisible(true);
        if(!wheel&&!prisma->Focus(view,false)){SKSE::log::warn("Prisma rejected focus request");SetPanelVisible(alwaysVisible&&allowHud);return;}
        viewOpen=true;
    }else if(!wheel){
        if(!prisma->Focus(view,false))return;
        wheelControls.Release(ToggleWheelControls);
    }
    SKSE::log::info("Opening {}: keyboardScan={} controller={}",wheel?"wheel":"layout",scan,controller);
    openedAt=GetTickCount64();wheelOpen=wheel;controllerOverlay=controller;overlayPadDevice=device;heldPadChord=chord;padAimReady=false;++overlayEpoch;UpdateOverlayTiming();
    if(wheel){
        wheelControls.Acquire(RE::ControlMap::GetSingleton()->GetRuntimeData().enabledControls.underlying(),ToggleWheelControls);
        heldWheelKey=scan;wheelReleaseAt=0;const auto token=wheelSession.Begin();
        const auto payload=Json{{"token",token},{"key",scan},{"controller",controller},{"epoch",overlayEpoch}}.dump();
        prisma->InteropCall(view,"cloudwalkOpenWheel",payload.c_str());
    }else{
        wheelSession.Cancel();heldWheelKey=0;
        const auto payload=Json{{"epoch",overlayEpoch}}.dump();prisma->InteropCall(view,"cloudwalkOpened",payload.c_str());
    }
}
void ToggleLayout(){if(viewOpen)ClosePlayer();else OpenOverlay(false);}
class Runtime : public RE::BSTEventSink<RE::InputEvent*>, public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
public:
    std::mutex mutex;
    std::condition_variable commandReady;
    std::string lastUiState;
    Json state={{"config",Config(Options{})},{"connected",false},{"capabilities",Json::object()},{"now",nullptr}};
    std::deque<Json> commands;
    std::atomic<bool> running{false},taskPending{false},loaded{false},inGame{false},paused{true},focused{true};
    std::atomic<bool> enabled{false},vanillaMuted{false};
    std::atomic<int> toggleKey{7},nextKey{9},uiKey{28},previousKey{8},wheelKey{47},captureKey{-1};
    RE::BGSSoundCategory* music=nullptr;
    bool muted=false;CloudWalk::MusicGainLease gainLease;
    CloudWalk::MusicLease<RE::BSIMusicType> musicLease;
    Options options;
    std::vector<CloudWalk::Playlist> playlists;
    std::string requestedPlaylist;
    std::atomic<bool> controllerEnabled{true},controllerTaskPending{false};
    std::atomic<float> controllerDeadzone{.25f};
    std::array<std::atomic<CloudWalk::PadButtons>,5> padBindings{{0x4100,0x1100,0x108,0x104,0x300}};
    std::atomic<int> capturePad{-1};
    CloudWalk::PadEdges padEdges;
    ULONGLONG lastPadTick=0;
    bool padWasAiming=false,padDrain=false;
    int polledPadDevice=-1;std::atomic<ULONGLONG> nextPadSearch{0};std::atomic<bool> controllerPresent{false};CloudWalk::PadSnapshot lastPadSample;
    inline static const char* keyNames[]={"uiKey","toggleKey","nextKey","previousKey","wheelKey"};
    std::filesystem::path settingsFile;
    std::string commandError;
    static Runtime& Get() { static auto* instance=new Runtime();return *instance; }
    Json Snapshot() { std::scoped_lock lock(mutex);return state; }
    void Command(const char* requested,Json payload=Json::object()) {
        std::string action=requested;
        if(action=="toggle"||action=="play"||action=="pause"){
            const bool power=action=="toggle"?!enabled.load():action=="play";
            enabled=power;payload={{"on",power}};action="power";
            SKSE::GetTaskInterface()->AddTask([this]{GameTick();});
        }
        if(action=="settings"){
            if(payload.contains("controllerEnabled")&&payload["controllerEnabled"].is_boolean())controllerEnabled=payload["controllerEnabled"].get<bool>();
            if(payload.contains("controllerDeadzone")&&payload["controllerDeadzone"].is_number()){
                const float value=payload["controllerDeadzone"].get<float>();if(std::isfinite(value)&&value>=.1f&&value<=.8f)controllerDeadzone=value;
            }
            if(payload.contains("alwaysShow")&&payload["alwaysShow"].is_boolean())alwaysVisible=payload["alwaysShow"].get<bool>();
            if(payload.contains("wheelSlowdown")&&payload["wheelSlowdown"].is_number()){
                const float value=payload["wheelSlowdown"].get<float>();
                if(std::isfinite(value)&&value>=0&&value<=1){overlaySlowdown=value;SKSE::GetTaskInterface()->AddUITask([]{UpdateOverlayTiming();});}
            }
            if(payload.contains("language")&&payload["language"].is_string()){
                const auto value=payload["language"].get<std::string>();if(value=="en"||value=="zh")english=value=="en";
            }
        }
        std::scoped_lock lock(mutex);
        if(action=="settings"&&payload.size()==1&&!commands.empty()&&commands.back().value("action","")=="settings"&&commands.back()["payload"].size()==1&&commands.back()["payload"].contains(payload.begin().key())){commands.back()["payload"]=payload;return;}
        if(commands.size()<64){commands.push_back({{"action",action},{"payload",std::move(payload)},{"queuedAt",GetTickCount64()}});commandReady.notify_one();}
    }
    void ReadSettings() {
        PWSTR folder=nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DEFAULT,nullptr,&folder))) throw std::runtime_error("LocalAppData is unavailable");
        settingsFile=std::filesystem::path(folder)/L"CloudWalk"/L"external-settings.json";CoTaskMemFree(folder);
        bool persist=true;
        if (std::filesystem::exists(settingsFile)) {
            std::ifstream file(settingsFile);const auto saved=Json::parse(file);
            Json keys=Json::object();for(const auto name:{"uiKey","toggleKey","nextKey","previousKey","wheelKey","alwaysShow","wheelSlowdown","language","controllerEnabled","controllerDeadzone","padLayout","padToggle","padNext","padPrevious","padWheel"})if(saved.at("config").contains(name))keys[name]=saved.at("config")[name];// Preserve older custom bindings if they use the new defaults.
            const int schema=saved.value("schema",0);persist=schema<6||keys.size()!=Config(Options{}).size();
            keys=CloudWalk::MigrateKeyboardKeys(std::move(keys),schema);
            auto defaults=Options{};
            for(const auto oldKey:{"uiKey","toggleKey","nextKey"})if(keys.contains(oldKey)){
                if(keys[oldKey]==defaults.previousKey&&!keys.contains("previousKey"))defaults.previousKey=0;
                if(keys[oldKey]==defaults.wheelKey&&!keys.contains("wheelKey"))defaults.wheelKey=0;
            }
            options=Patch(defaults,keys);
            if(saved.contains("playlists"))playlists=CloudWalk::ReadPlaylists(saved["playlists"]);
        }
        if(persist){
            try{SaveSettings(options,playlists);SKSE::log::info("Settings upgraded to schema 6 with explicit keyboard/controller bindings");}
            catch(const std::exception& e){SKSE::log::warn("Settings upgrade write failed: {}",e.what());}
        }
        SKSE::log::info("Effective settings: file={} config={}",winrt::to_string(settingsFile.wstring()),Config(options).dump());
    }
    void SaveSettings(const Options& value,const std::vector<CloudWalk::Playlist>& lists) {
        if (settingsFile.empty()) throw std::runtime_error("Settings path is unavailable");
        std::filesystem::create_directories(settingsFile.parent_path());
        auto temporary=settingsFile;temporary+=L".tmp";
        { std::ofstream file(temporary,std::ios::binary|std::ios::trunc);auto saved=Config(value);file<<Json{{"schema",6},{"backend","official-client"},{"config",saved},{"playlists",CloudWalk::PlaylistJson(lists)}}.dump(2);file.flush();if(!file)throw std::runtime_error("Settings write failed"); }
        if(!MoveFileExW(temporary.c_str(),settingsFile.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Settings replace failed");
    }
    void Publish(const CloudWalk::ClientState& media) {
        auto cfg=Config(options);cfg["language"]=english?"en":"zh";cfg["wheelSlowdown"]=overlaySlowdown.load();cfg["enabled"]=enabled.load();cfg["volume"]=media.volume;
        Json snapshot={{"service","CloudWalk"},{"version","0.5.7"},{"backend","official-client"},{"connected",media.connected},
            {"config",cfg},{"shouldPlay",media.connected&&media.playing},
            {"capabilities",{{"toggle",true},{"play",media.canPlay},{"pause",media.canPause},{"next",media.canNext},{"previous",media.canPrevious},{"volume",media.canVolume}}},
            {"playlists",CloudWalk::PlaylistViewJson(playlists)},{"requestedPlaylist",requestedPlaylist},
            {"status",media.connected?(media.playing?(english?"NetEase is playing":"网易云客户端正在播放"):(english?"NetEase is paused":"网易云客户端已暂停")):(english?"Waiting for NetEase":"等待官方网易云客户端")},
            {"error",CloudWalk::ErrorText(english,media.error)},{"commandError",CloudWalk::ErrorText(english,commandError)},{"source",media.source},{"now",nullptr}};
        if(media.connected) snapshot["now"]={{"title",media.title.empty()?(english?"NetEase Cloud Music":"网易云音乐"):media.title},{"artist",media.artist}};
        toggleKey=options.toggleKey;nextKey=options.nextKey;uiKey=options.uiKey;previousKey=options.previousKey;wheelKey=options.wheelKey;
        std::string encoded;if(prisma&&viewReady&&viewVisible)encoded=snapshot.dump();
        { std::scoped_lock lock(mutex);state=std::move(snapshot); }
        if(!encoded.empty()&&(uiStateNeeded.exchange(false)||encoded!=lastUiState)){
            prisma->InteropCall(view,"cloudwalkState",encoded.c_str());lastUiState=std::move(encoded);
        }
    }
    void GameTick() {
        UpdateOverlayTiming();
        const auto ui=RE::UI::GetSingleton();const auto player=RE::PlayerCharacter::GetSingleton();
        const bool active=loaded&&player&&player->Is3DLoaded()&&ui&&!ui->IsMenuOpen(RE::MainMenu::MENU_NAME)&&!ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME);
        inGame=active;paused=!ui||ui->GameIsPaused();
        const bool clear=active&&CanOpenPlayer()&&GameForeground();
        allowHud=clear;
        if(viewOpen&&!clear){std::string reason;CanOpenPlayer(&reason);SKSE::log::info("Closing overlay: active={} foreground={} reason={}",active,GameForeground(),reason);ClosePlayer();}
        SetPanelVisible(CloudWalk::PanelVisible(viewOpen.load(),alwaysVisible.load(),active,clear));
        DWORD process=0;GetWindowThreadProcessId(GetForegroundWindow(),&process);focused=process==GetCurrentProcessId();
        if(!music)music=RE::TESForm::LookupByID<RE::BGSSoundCategory>(0x00071E64);
        const bool wantMute=active&&enabled;
        std::vector<RE::BSIMusicType*> live;
        if(auto manager=RE::BSMusicManager::GetSingleton()){
            if(manager->current)live.push_back(manager->current);
            for(auto* type:manager->musicQueue)if(type&&std::find(live.begin(),live.end(),type)==live.end())live.push_back(type);
        }
        musicLease.Update(wantMute,live,
            [](auto* t){return t->typeStatus.get()==RE::BSIMusicType::MUSIC_STATUS::kPlaying;},
            [](auto* t){return t->typeStatus.get()==RE::BSIMusicType::MUSIC_STATUS::kPaused;},
            [](auto* t){t->DoPause();},[](auto* t){t->DoPlay();});
        if(music){
            const bool held=gainLease.Active();
            gainLease.Update(wantMute,music->volumeMult,music->staticMult);
            if(wantMute||held)music->SetCategoryVolume(music->volumeMult);
        }
        if(wantMute!=muted)SKSE::log::info("Walkman original music mute={} category={} musicTypes={}",wantMute,music!=nullptr,live.size());
        muted=wantMute;vanillaMuted=wantMute;
        taskPending=false;
    }
    void SyncControllerOptions(){
        controllerEnabled=options.controllerEnabled;controllerDeadzone=options.controllerDeadzone;
        for(unsigned i=0;i<5;++i)padBindings[i]=options.padBindings[i];
    }
    void SendController(Json data){
        if(!prisma||!viewReady||!viewOpen)return;
        data["epoch"]=overlayEpoch;if(wheelOpen)data["token"]=wheelSession.Token();
        prisma->InteropCall(view,"cloudwalkController",data.dump().c_str());
    }
    void PadTick(const CloudWalk::PadSnapshot& sample){
        lastPadSample=sample;
        const auto now=GetTickCount64();const float dt=lastPadTick?std::min((now-lastPadTick)/1000.f,.05f):0;lastPadTick=now;
        if(viewOpen){
            UpdateOverlayTiming();
            if(!GameForeground()||!loaded||!CanOpenPlayer()||(wheelReleaseAt&&now-wheelReleaseAt>500))ClosePlayer();
        }
        if(controllerOverlay&&(!sample.connected||sample.device!=overlayPadDevice||!controllerEnabled)){ClosePlayer();padDrain=true;}
        if(!sample.connected&&!viewOpen&&capturePad<0){padEdges.Block();return;}
        const bool settings=SettingsBlocks();
        if(capturePad>=0&&!settings)capturePad=-1;
        const bool allowed=controllerEnabled&&GameForeground()&&inGame&&loaded&&!settings&&capturePad<0&&CanOpenPlayer();
        if(padDrain){padEdges.Block();if(!sample.buttons)padDrain=false;}
        padEdges.Sample(sample,allowed&&!padDrain);
        if(!allowed||!sample.connected)return;
        const auto rising=padEdges.Rising();
        if(viewOpen&&(rising&(CloudWalk::PadB|CloudWalk::PadBack|CloudWalk::PadStart))){ClosePlayer();padDrain=true;return;}
        if(wheelOpen){
            if(!controllerOverlay)return;
            if(!CloudWalk::Held(sample.buttons,heldPadChord)){ReleaseWheel();return;}
            if(wheelReleaseAt)return;
            Json data=Json::object();const auto controls=rising&~heldPadChord;
            if(controls&(CloudWalk::PadLT|CloudWalk::PadRT|12)){
                data["page"]=(controls&(CloudWalk::PadRT|8))?1:-1;padAimReady=false;
            }
            if(controls&3)data["scaleStep"]=(controls&1)?1:-1;
            const auto aim=CloudWalk::PadStick(sample.rx,sample.ry,controllerDeadzone);
            const bool active=aim.x!=0||aim.y!=0;
            if(!active)padAimReady=true;
            if(!data.contains("page")&&padAimReady&&(active||padWasAiming)){data["x"]=aim.x;data["y"]=aim.y;}
            padWasAiming=active;
            if(!data.empty())SendController(std::move(data));return;
        }
        if(viewOpen){
            Json data=Json::object();
            if(rising&0x1000)data["select"]=true;
            const auto move=CloudWalk::PadStick(sample.lx,sample.ly,controllerDeadzone);
            const auto size=CloudWalk::PadStick(sample.rx,sample.ry,controllerDeadzone);
            if(move.x||move.y){data["dx"]=move.x*300*dt;data["dy"]=-move.y*300*dt;}
            if(size.y)data["scale"]=size.y*1.3f*dt;
            if(rising&4)data["volumeStep"]=-.05f;else if(rising&8)data["volumeStep"]=.05f;
            if(rising&0x8000)Command("toggle");
            if(enabled){if(rising&0x100)Command("previous");else if(rising&0x200)Command("next");}
            if(!data.empty())SendController(std::move(data));return;
        }
        if(paused||!allowHud)return;
        if(padEdges.Pressed(padBindings[4])){
            OpenOverlay(true,0,true,sample.device,padBindings[4]);padWasAiming=false;return;
        }
        if(padEdges.Pressed(padBindings[0])){OpenOverlay(false,0,true,sample.device);return;}
        if(padEdges.Pressed(padBindings[1]))Command("toggle");
        else if(enabled&&padEdges.Pressed(padBindings[2]))Command("next");
        else if(enabled&&padEdges.Pressed(padBindings[3]))Command("previous");
    }
    void PollController(){
        if(!controllerEnabled){PadTick({});return;}
        const auto now=GetTickCount64();
        if(polledPadDevice<0&&now<nextPadSearch){PadTick({});return;}
        const auto sample=ReadController(polledPadDevice);polledPadDevice=sample.connected?sample.device:-1;
        controllerPresent=sample.connected;
        if(!sample.connected)nextPadSearch=now+1000;PadTick(sample);
    }
    CloudWalk::PadButtons ControllerClaims(){
        if(!controllerEnabled||!padEdges.Ready()||!GameForeground()||!inGame||!loaded||paused||!CanOpenPlayer())return 0;
        CloudWalk::PadButtons result=0;for(const auto& value:padBindings)if(CloudWalk::Held(lastPadSample.buttons,value))result|=value.load();return result;
    }
    void ControllerLoop(){
        while(running){
            // Read on the UI task, not before queuing, so an older sample can never
            // look like a release after a newer engine poll sample.
            const bool idle=!viewOpen&&capturePad<0&&(!controllerEnabled||(!controllerPresent&&GetTickCount64()<nextPadSearch.load()));
            if(!idle&&!controllerTaskPending.exchange(true))SKSE::GetTaskInterface()->AddUITask([this]{PollController();controllerTaskPending=false;});
            std::this_thread::sleep_for(idle?50ms:16ms);
        }
    }
    RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* events,RE::BSTEventSource<RE::InputEvent*>*) override {
        if(viewOpen){
            UpdateOverlayTiming();
            if(!GameForeground()||SettingsBlocks()||(wheelReleaseAt&&GetTickCount64()-wheelReleaseAt>500))ClosePlayer();
            else if(wheelOpen&&heldWheelKey&&GetTickCount64()-openedAt.load()>40&&!PhysicalKeyDown(heldWheelKey))ReleaseWheel();
        }
        if(!events)return RE::BSEventNotifyControl::kContinue;
        for(auto event=*events;event;event=event->next){
            const auto button=event->AsButtonEvent();if(!button||button->GetDevice()!=RE::INPUT_DEVICE::kKeyboard)continue;
            const auto scan=button->GetIDCode();
            if(wheelOpen&&scan==heldWheelKey&&button->IsUp()){ReleaseWheel();return RE::BSEventNotifyControl::kStop;}
            if(!button->IsDown())continue;
            const auto key=button->GetIDCode();
            if(!SettingsBlocks())captureKey=-1;
            if(captureKey>=0){
                const int index=captureKey.exchange(-1);
                if(key!=1&&index>=0&&index<5)Command("settings",{{keyNames[index],key}});
                return RE::BSEventNotifyControl::kStop;
            }
            if(wheelOpen&&key==1){padDrain=true;ClosePlayer();return RE::BSEventNotifyControl::kStop;}
            const bool bound=key==static_cast<unsigned>(uiKey.load())||key==static_cast<unsigned>(wheelKey.load())||key==static_cast<unsigned>(toggleKey.load())||key==static_cast<unsigned>(nextKey.load())||key==static_cast<unsigned>(previousKey.load());
            if(bound){std::string reason;const bool clear=CanOpenPlayer(&reason);SKSE::log::info("Keyboard scan={} label={} inGame={} paused={} focused={} clear={} reason={}",key,KeyLabel(key),inGame.load(),paused.load(),focused.load(),clear,reason);}
            const bool wheel=wheelKey&&key==static_cast<unsigned>(wheelKey.load());
            if(wheel){
                if(viewOpen||(!paused&&inGame&&focused&&CanOpenPlayer())){OpenOverlay(true,key);return RE::BSEventNotifyControl::kStop;}
                continue;
            }
            if(wheelOpen)continue;
            const bool canOpen=!viewOpen&&inGame&&!paused&&focused&&CanOpenPlayer();
            const auto action=CloudWalk::WindowKey(viewOpen.load(),GetTickCount64(),openedAt.load(),closedAt.load(),key,uiKey.load(),canOpen);
            if(action==CloudWalk::WindowAction::close){ClosePlayer();return RE::BSEventNotifyControl::kStop;}
            if(action==CloudWalk::WindowAction::consume)return RE::BSEventNotifyControl::kStop;
            if(action==CloudWalk::WindowAction::open){OpenOverlay(false);return RE::BSEventNotifyControl::kStop;}
            if(viewOpen||!canOpen)continue;
            if(toggleKey&&key==static_cast<unsigned>(toggleKey.load()))Command("toggle");
            else if(nextKey&&key==static_cast<unsigned>(nextKey.load())&&enabled)Command("next");
            else if(previousKey&&key==static_cast<unsigned>(previousKey.load())&&enabled)Command("previous");
        }
        return RE::BSEventNotifyControl::kContinue;
    }
    RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event,RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override {
        if(event&&event->opening){
            const auto ui=RE::UI::GetSingleton();const auto menu=ui?ui->GetMenu(event->menuName.c_str()):nullptr;
            if(!CloudWalk::MenuBlocksOverlay(event->menuName.c_str(),viewOpen.load(),menu?MenuState(*menu):CloudWalk::MenuTraits{}))return RE::BSEventNotifyControl::kContinue;
            allowHud=false;ClosePlayer();SetPanelVisible(false);
        }
        return RE::BSEventNotifyControl::kContinue;
    }
    void Start() {
        if(running.exchange(true))return;
        try{ReadSettings();}catch(...){commandError="设置读取失败，已使用默认设置";SKSE::log::warn("External client settings unavailable or invalid");}
        SKSE::log::info("Keyboard labels: playback={} previous={} next={} wheel={} layout={}",KeyLabel(options.toggleKey),KeyLabel(options.previousKey),KeyLabel(options.nextKey),KeyLabel(options.wheelKey),KeyLabel(options.uiKey));
        enabled=false;
        alwaysVisible=options.alwaysShow;overlaySlowdown=options.wheelSlowdown;english=options.language=="en";SyncControllerOptions();
        if(auto input=RE::BSInputDeviceManager::GetSingleton())input->AddEventSink(this);
        if(auto ui=RE::UI::GetSingleton())ui->AddEventSink<RE::MenuOpenCloseEvent>(this);
        std::thread([this]{Worker();}).detach();
        std::thread([this]{ControllerLoop();}).detach();
    }
    void Apply(const Json& command,CloudWalk::MediaClient& client,const CloudWalk::ClientState& media) {
        const auto action=command.value("action","");const auto payload=command.value("payload",Json::object());
        commandError.clear();
        try{
            if(action=="refresh")return;
            if(action=="power"){
                const bool on=payload.value("on",false);
                const bool ok=media.connected&&client.Control(on?"play":"pause");
                SKSE::log::info("Walkman power={} clientAccepted={}",on,ok);
                if(!ok&&media.connected)commandError="客户端未接受播放开关";
                return;
            }
            if(action=="settings"){
                Json local=payload;
                const bool changeVolume=local.contains("volume");
                if(changeVolume&&!local["volume"].is_number())throw std::runtime_error("Invalid volume");
                const float volume=changeVolume?local["volume"].get<float>():0;
                local.erase("volume");
                const auto candidate=Patch(options,local);
                if(!local.empty()){SaveSettings(candidate,playlists);options=candidate;alwaysVisible=options.alwaysShow;SyncControllerOptions();}
                if(changeVolume&&(!media.connected||!media.canVolume||!client.Volume(volume)))commandError="网易云音量控制不可用，请在客户端调整";
                return;
            }
            if(action=="addPlaylist"){
                auto next=playlists;auto station=CloudWalk::ParseStation(payload.at("url").get<std::string>());
                station.name=payload.value("name",std::string{});
                if(station.name.empty())station.name=(station.type=="album"?(english?"Album ":"专辑 "):(english?"Playlist ":"歌单 "))+station.id;
                if(station.name.size()>96)throw std::runtime_error("电台名称最多 96 个 UTF-8 字节");
                if(next.size()>=48)throw std::runtime_error("最多保存 48 个电台");
                if(std::any_of(next.begin(),next.end(),[&](const auto& p){return CloudWalk::StationKey(p)==CloudWalk::StationKey(station);}))throw std::runtime_error("这个电台已经在列表中");
                next.push_back(station);SaveSettings(options,next);playlists=std::move(next);
                SKSE::log::info("Station saved: type={} id={} count={}",station.type,station.id,playlists.size());return;
            }
            if(action=="removePlaylist"){
                const auto id=payload.at("id").get<std::string>();auto next=playlists;
                std::erase_if(next,[&](const auto& p){return CloudWalk::StationKey(p)==id;});
                SaveSettings(options,next);playlists=std::move(next);if(requestedPlaylist==id)requestedPlaylist.clear();return;
            }
            if(action=="selectPlaylist"){
                const auto id=payload.at("id").get<std::string>();
                const auto station=std::find_if(playlists.begin(),playlists.end(),[&](const auto& p){return CloudWalk::StationKey(p)==id;});
                if(station==playlists.end())throw std::runtime_error("电台已移除，请重新打开轮盘");
                if(!media.connected)throw std::runtime_error("请先在外部打开网易云并播放一首歌曲");
                if(!CloudWalk::RequestPlaylist(id,requestedPlaylist)){
                    enabled=true;
                    if(!client.Control("play"))commandError="无法恢复当前网易云队列";
                    SKSE::GetTaskInterface()->AddTask([this]{GameTick();});return;
                }
                std::string error;
                const auto started=GetTickCount64();const auto queued=command.value("queuedAt",started);
                if(!client.Playlist(station->id,station->type,&error))throw std::runtime_error(error);
                SKSE::log::info("Station request dispatched with foreground and window-order protection; type={} id={} gameForeground={} queueMs={} dispatchMs={}",station->type,station->id,GameForeground(),started-queued,GetTickCount64()-started);
                requestedPlaylist=id;enabled=true;SKSE::GetTaskInterface()->AddTask([this]{GameTick();});return;
            }
            if(action=="play"||action=="pause"||action=="toggle"||action=="next"||action=="previous"){
                
                if(!media.connected){commandError="请先在外部打开官方网易云客户端并播放一首歌曲";return;}
                const bool ok=client.Control(action);
                SKSE::log::info("External client command {} accepted={}",action,ok);
                if(!ok)commandError="网易云未接受控制指令，请检查客户端系统媒体控制设置";
            }
        }catch(const std::exception& e){
            SKSE::log::warn("Command {} failed: {}",action,e.what());
            commandError=(action=="settings")?"设置未保存：请检查设置范围或快捷键冲突":e.what();
        }catch(...){commandError="操作失败，请查看 CloudWalk.log";SKSE::log::warn("Command {} failed with an unknown exception",action);}
    }
    void Worker() {
        try{
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            {
                CloudWalk::MediaClient client;CloudWalk::ClientState media;
                auto nextPoll=std::chrono::steady_clock::now(),nextGameTick=nextPoll;bool wasConnected=false;
                while(running){
                    client.MaintainBackground();
                    const auto tick=std::chrono::steady_clock::now();
                    if(tick>=nextGameTick&&!taskPending.exchange(true)){nextGameTick=tick+100ms;SKSE::GetTaskInterface()->AddTask([this]{GameTick();});}
                    Json command;{std::scoped_lock lock(mutex);if(!commands.empty()){command=commands.front();commands.pop_front();}}
                    const auto now=std::chrono::steady_clock::now();
                    if(!command.is_null()&&command.value("action","")=="selectPlaylist"&&media.connected){
                        // Send the station first; metadata/volume polling must
                        // not delay a request when the session is already known.
                        Apply(command,client,media);Publish(media);
                    }else if(now>=nextPoll||!command.is_null()){
                        media=client.Poll();nextPoll=now+500ms;
                        if(media.connected!=wasConnected){SKSE::log::info("NetEase Windows media session connected={}",media.connected);wasConnected=media.connected;}
                        if(!command.is_null()){Apply(command,client,media);media=client.Poll();}
                        Publish(media);
                    }
                    std::unique_lock lock(mutex);commandReady.wait_for(lock,100ms,[this]{return !commands.empty()||!running;});
                }
            }
            winrt::uninit_apartment();
        }catch(...){SKSE::log::error("External media worker initialization failed");std::scoped_lock lock(mutex);state["commandError"]="Windows 媒体控制初始化失败，请查看 CloudWalk.log";}
    }
};

// Chain the existing delegate poll, then filter only owned gamepad events before
// Skyrim dispatches its queue. The UI heartbeat remains a paused-game fallback.
using PadPoll=void(*)(RE::BSPCGamepadDeviceHandler*,float);
inline std::atomic<PadPoll> previousPadPoll{nullptr};
inline CloudWalk::PadInputLease padInputLease;
using MousePoll=void(*)(RE::BSWin32MouseDevice*,float);
inline std::atomic<MousePoll> previousMousePoll{nullptr};
inline CloudWalk::PadInputLease mouseInputLease;
void GuardMousePoll(RE::BSWin32MouseDevice* device,float delta){
    const auto previous=previousMousePoll.load();if(!previous)return;previous(device,delta);
    const bool capture=viewOpen&&wheelOpen&&GameForeground();
    int dx=0,dy=0,page=0,scale=0;
    if(auto* queue=RE::BSInputEventQueue::GetSingleton())CloudWalk::FilterPadQueue(queue->GetQueueHead(),queue->GetQueueTail(),[&](RE::InputEvent* event){
        if(event->GetDevice()!=RE::INPUT_DEVICE::kMouse)return false;
        if(auto* move=event->AsMouseMoveEvent()){
            if(capture){dx+=move->mouseInputX;dy+=move->mouseInputY;}return capture;
        }
        if(auto* button=event->AsButtonEvent()){
            const auto id=button->GetIDCode();
            if(capture&&button->IsDown()){
                if(id<2)page=id==1?1:-1;
                else if(id==8||id==9)scale+=id==8?1:-1;
            }
            if(id<8)return mouseInputLease.Filter(1u<<id,button->IsDown(),button->IsUp(),capture);
            return capture;
        }
        return capture;
    });
    if(!capture||controllerOverlay||wheelReleaseAt||!prisma||!(dx||dy||page||scale))return;
    const auto data=Json{{"epoch",overlayEpoch},{"token",wheelSession.Token()},{"dx",dx},{"dy",dy},{"page",page},{"scaleStep",scale}}.dump();
    prisma->InteropCall(view,"cloudwalkMouse",data.c_str());
}
CloudWalk::PadButtons EventPadBit(unsigned id){
    if(id==9)return CloudWalk::PadLT;if(id==10)return CloudWalk::PadRT;
    return id&&std::has_single_bit(id)?id&0xF3FF:0;
}
void GuardPadPoll(RE::BSPCGamepadDeviceHandler* device,float delta){
    const auto previous=previousPadPoll.load();if(!previous)return;previous(device,delta);
    auto& runtime=Runtime::Get();if(!runtime.running)return;
    runtime.PollController();
    const bool capture=runtime.capturePad>=0;
    const bool overlay=viewOpen.load();const auto claimed=runtime.ControllerClaims();
    if(auto* queue=RE::BSInputEventQueue::GetSingleton()){
        CloudWalk::FilterPadQueue(queue->GetQueueHead(),queue->GetQueueTail(),[=](RE::InputEvent* event){
            if(event->GetDevice()!=RE::INPUT_DEVICE::kGamepad)return false;
            if(auto* button=event->AsButtonEvent()){
                const auto bit=EventPadBit(button->GetIDCode());
                return padInputLease.Filter(bit,button->IsDown(),button->IsUp(),capture||overlay||(claimed&bit));
            }
            return (capture||overlay||claimed)&&event->AsThumbstickEvent()!=nullptr;
        });
    }
}
template<class Poll>bool InstallInputGuard(REL::VariantID id,std::atomic<Poll>& previous,Poll handler){
    REL::Relocation<std::uintptr_t> table{id};
    const auto slot=table.address()+2*sizeof(std::uintptr_t);
    const auto prior=*reinterpret_cast<const std::uintptr_t*>(slot);
    MEMORY_BASIC_INFORMATION info{};
    if(!prior||!VirtualQuery(reinterpret_cast<const void*>(prior),&info,sizeof(info))||info.State!=MEM_COMMIT||
        (info.Protect&(PAGE_GUARD|PAGE_NOACCESS))||!(info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
    previous=reinterpret_cast<Poll>(prior);
    const auto replacement=reinterpret_cast<std::uintptr_t>(handler);
    if(!REL::safe_write(slot,&replacement,sizeof(replacement),&prior,sizeof(prior))){previous=nullptr;return false;}
    return true;
}
bool InstallControllerGuard(){
    const bool ok=InstallInputGuard(RE::VTABLE_BSPCGamepadDeviceHandler[0],previousPadPoll,&GuardPadPoll);
    if(ok)SKSE::log::info("CloudWalk controller poll guard installed; previous input handler chained");return ok;
}
bool InstallMouseGuard(){
    mouseGuardReady=InstallInputGuard(RE::VTABLE_BSWin32MouseDevice[0],previousMousePoll,&GuardMousePoll);
    SKSE::log::info("CloudWalk relative mouse guard ready={}; wheel uses passive view without Prisma cursor",mouseGuardReady);return mouseGuardReady;
}

std::string KeyLabel(int key){
    if(!key)return CloudWalk::Text(english,"unbound");
    wchar_t label[128]{};
    const LONG scan=((key&0x7f)<<16)|((key&0x80)?(1<<24):0);
    if(GetKeyNameTextW(scan,label,128))return winrt::to_string(label);
    return std::string(english?"Scan ":"扫描码 ")+std::to_string(key);
}
unsigned ImGuiScan(int key){
    static constexpr unsigned navigation[]={15,203,205,200,208,201,209,199,207,210,211,14,57,28,1,29,42,56,219,157,54,184,220,221};
    if(key>=ImGuiKey_Tab&&key<=ImGuiKey_Menu)return navigation[key-ImGuiKey_Tab];
    static constexpr unsigned digits[]={11,2,3,4,5,6,7,8,9,10};
    if(key>=ImGuiKey_0&&key<=ImGuiKey_9)return digits[key-ImGuiKey_0];
    static constexpr unsigned letters[]={30,48,46,32,18,33,34,35,23,36,37,38,50,49,24,25,16,19,31,20,22,47,17,45,21,44};
    if(key>=ImGuiKey_A&&key<=ImGuiKey_Z)return letters[key-ImGuiKey_A];
    if(key>=ImGuiKey_F1&&key<=ImGuiKey_F10)return 59+key-ImGuiKey_F1;
    if(key==ImGuiKey_F11)return 87;if(key==ImGuiKey_F12)return 88;
    static constexpr unsigned other[]={40,51,12,52,53,39,13,26,43,27,41,58,70,69,183,197,82,79,80,81,75,76,77,71,72,73,83,181,55,74,78,156,141};
    if(key>=ImGuiKey_Apostrophe&&key<=ImGuiKey_KeypadEqual)return other[key-ImGuiKey_Apostrophe];
    return 0;
}
const char* Tr(std::string_view key){return CloudWalk::Text(english,key);}
std::string Label(std::string_view key,const char* id){return std::string(Tr(key))+"###"+id;}
void Help(std::string_view key){
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled|ImGuiHoveredFlags_NoNavOverride)){
        ImGui::BeginTooltip();ImGui::PushTextWrapPos(ImGui::GetFontSize()*30);
        ImGui::TextUnformatted(Tr(key));ImGui::PopTextWrapPos();ImGui::EndTooltip();
    }
}
void __stdcall Render(){
    settingsRenderedAt=GetTickCount64();
    auto& runtime=Runtime::Get();const auto snapshot=runtime.Snapshot();const auto cfg=snapshot.at("config");
    static std::string loggedKeys;
    const auto keys=Json{{"layout",cfg.value("uiKey",0)},{"playback",cfg.value("toggleKey",0)},{"previous",cfg.value("previousKey",0)},{"next",cfg.value("nextKey",0)},{"wheel",cfg.value("wheelKey",0)}}.dump();
    if(keys!=loggedKeys){loggedKeys=keys;SKSE::log::info("Settings page keyboard scan codes: {}",keys);}
    static CloudWalk::PadCapture padRecorder;static bool padInvalid=false;
    if(runtime.capturePad>=0){
        const int index=runtime.capturePad.load();
        const auto result=ImGui::IsKeyPressed(ImGuiKey_Escape,false)?CloudWalk::PadCaptureResult::cancelled:padRecorder.Sample(ReadController());
        if(result!=CloudWalk::PadCaptureResult::waiting){
            runtime.capturePad=-1;
            if(result==CloudWalk::PadCaptureResult::captured){
                std::array<CloudWalk::PadButtons,5> proposed{};for(unsigned i=0;i<5;++i)proposed[i]=runtime.padBindings[i];
                if(index>=0&&index<5){proposed[index]=padRecorder.value;
                    if(CloudWalk::ValidPadBindings(proposed))runtime.Command("settings",{{padSettingNames[index],padRecorder.value}});
                    else padInvalid=true;
                }
            }else if(result==CloudWalk::PadCaptureResult::invalid)padInvalid=true;
        }
    }
    // The settings framework filters SKSE input events while open. Capture from
    // its own ImGui context so rebinding also works with that filter enabled.
    if(runtime.captureKey>=0){
        if(ImGui::IsKeyPressed(ImGuiKey_Escape,false))runtime.captureKey=-1;
        else for(int key=ImGuiKey_Tab;key<=ImGuiKey_KeypadEqual;++key){
            const auto scan=ImGuiScan(key);
            if(scan&&ImGui::IsKeyPressed(static_cast<ImGuiKey>(key),false)){
                const int index=runtime.captureKey.exchange(-1);
                if(index>=0&&index<5)runtime.Command("settings",{{Runtime::keyNames[index],scan}});
                break;
            }
        }
    }
    ImGui::TextUnformatted("CloudWalk");
    const char* languages[]={"简体中文","English"};int language=english?1:0;
    if(ImGui::Combo(Label("language","language").c_str(),&language,languages,2))runtime.Command("settings",{{"language",language?"en":"zh"}});
    Help("help_language");
    bool power=runtime.enabled.load();
    if(ImGui::Checkbox(Label("enabled","enabled").c_str(),&power))runtime.Command(power?"play":"pause");Help("help_enabled");
    bool pinned=alwaysVisible.load();
    if(ImGui::Checkbox(Label("always","always").c_str(),&pinned))runtime.Command("settings",{{"alwaysShow",pinned}});Help("help_always");
    float strength=overlaySlowdown.load();
    if(ImGui::SliderFloat(Label("slowdown","slowdown").c_str(),&strength,0,1,"%.2f"))runtime.Command("settings",{{"wheelSlowdown",std::round(strength*100)/100}});Help("help_slowdown");
    if(ImGui::Button(Label("reset","reset").c_str())&&prisma&&viewReady)prisma->InteropCall(view,"cloudwalkResetLayout","");Help("help_reset");
    ImGui::Separator();ImGui::TextUnformatted(Tr("hotkeys"));
    const char* labels[]={"layout","power","next","previous","wheel"};
    const char* help[]={"help_layout","help_power","help_next","help_previous","help_wheel"};
    for(int i=0;i<5;++i){
        ImGui::PushID(i);
        const auto text=runtime.captureKey==i?std::string(Tr("capture")):KeyLabel(cfg.value(Runtime::keyNames[i],0));
        if(ImGui::Button((text+"###binding").c_str())){runtime.captureKey=i;runtime.capturePad=-1;}Help("help_binding");
        ImGui::SameLine();ImGui::TextUnformatted(Tr(labels[i]));Help(help[i]);ImGui::SameLine();
        if(ImGui::Button("×###clear")){runtime.captureKey=-1;runtime.Command("settings",{{Runtime::keyNames[i],0}});}Help("help_clear");ImGui::PopID();
    }
    ImGui::Separator();
    const bool controllerExpanded=ImGui::CollapsingHeader(Label("controller","controller").c_str(),0);Help("help_controller");
    if(controllerExpanded){
        bool enabled=runtime.controllerEnabled;
        if(ImGui::Checkbox(Label("controller_enabled","controllerEnabled").c_str(),&enabled))runtime.Command("settings",{{"controllerEnabled",enabled}});Help("help_controller");
        float zone=runtime.controllerDeadzone;
        if(ImGui::SliderFloat(Label("deadzone","controllerDeadzone").c_str(),&zone,.1f,.8f,"%.2f"))runtime.Command("settings",{{"controllerDeadzone",std::round(zone*100)/100}});Help("help_deadzone");
        for(unsigned i=0;i<5;++i){
            ImGui::PushID(static_cast<int>(i)+100);
            const auto value=runtime.padBindings[i].load();
            const auto text=runtime.capturePad==static_cast<int>(i)?(padRecorder.value?CloudWalk::PadLabel(padRecorder.value):std::string(Tr("pad_capture"))):(value?CloudWalk::PadLabel(value):std::string(Tr("unbound")));
            if(ImGui::Button((text+"###padBinding").c_str())){runtime.capturePad=i;runtime.captureKey=-1;padRecorder.Begin();padInvalid=false;}Help("help_pad_binding");
            ImGui::SameLine();ImGui::TextUnformatted(Tr(labels[i]));Help(i==0?"help_pad_layout":i==4?"help_controller":help[i]);ImGui::SameLine();
            if(ImGui::Button("×###clearPad")){runtime.capturePad=-1;runtime.Command("settings",{{padSettingNames[i],0}});}Help("help_clear");ImGui::PopID();
        }
        if(padInvalid){ImGui::TextUnformatted(Tr("pad_error"));Help("help_pad_error");}
    }else runtime.capturePad=-1;
    ImGui::Separator();ImGui::TextUnformatted(Tr("playlists"));
    static char url[2049]{},name[97]{};
    ImGui::InputText(Label("link","link").c_str(),url,sizeof(url));Help("help_link");
    ImGui::InputText(Label("name","name").c_str(),name,sizeof(name));Help("help_name");
    if(ImGui::Button(Label("add","add").c_str()))runtime.Command("addPlaylist",{{"url",url},{"name",name}});Help("help_add");
    if(snapshot.contains("playlists"))for(const auto& item:snapshot["playlists"]){
        const auto id=item.at("id").get<std::string>(),title=item.at("name").get<std::string>();
        ImGui::PushID(id.c_str());ImGui::TextUnformatted(title.c_str());ImGui::SameLine();
        if(ImGui::Button("×###remove"))runtime.Command("removePlaylist",{{"id",id}});Help("help_remove");ImGui::PopID();
    }
    const auto error=snapshot.value("commandError",std::string{});
    if(!error.empty()){
        ImGui::TextUnformatted(Tr("failed"));
        if(ImGui::IsItemHovered()){ImGui::BeginTooltip();ImGui::TextUnformatted(CloudWalk::ErrorText(english,error).c_str());ImGui::EndTooltip();}
    }
}
void Message(SKSE::MessagingInterface::Message* message) {
    auto& runtime=Runtime::Get();
    switch(message->type){
    case SKSE::MessagingInterface::kDataLoaded:
        prisma=PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI1>();
        if(prisma){
            view=prisma->CreateView("CloudWalk/index.html",[](PrismaView readyView){view=readyView;prisma->Hide(readyView);viewReady=true;SKSE::log::info("CloudWalk external-client Prisma player ready");
                prisma->Invoke(readyView,"JSON.stringify({width:innerWidth,height:innerHeight,layout:typeof cloudwalkOpened,wheel:typeof cloudwalkOpenWheel,mouse:typeof cloudwalkMouse,state:typeof cloudwalkState})",[](const char* result){SKSE::log::info("Prisma page handshake: {}",result?result:"null");});});
            prisma->Hide(view);
            prisma->RegisterJSListener(view,"cloudwalkCommand",[](const char* input){
                if(!input||strlen(input)>2048)return;
                try{const auto command=Json::parse(input);const auto action=command.value("action","");
                    if(action=="close"||action=="wheel"||action=="player"){
                        SKSE::GetTaskInterface()->AddUITask([action]{
                            if(action=="close")ClosePlayer();
                            else if(action=="wheel"){
                                const auto key=Runtime::Get().wheelKey.load();
                                if(key&&PhysicalKeyDown(key))OpenOverlay(true,key);
                            }else ToggleLayout();
                        });return;
                    }
                    if(action=="cancelWheel"){
                        const auto token=command.at("payload").at("token").get<std::uint64_t>();
                        SKSE::GetTaskInterface()->AddUITask([token]{if(wheelOpen&&wheelSession.Accept(token))ClosePlayer();});return;
                    }
                    if(action=="finishWheel"){
                        const auto payload=command.value("payload",Json::object());
                        const auto token=payload.at("token").get<std::uint64_t>();const auto id=payload.value("id",std::string{});
                        SKSE::GetTaskInterface()->AddUITask([token,id]{
                            if(!viewOpen||!wheelOpen||!wheelSession.Accept(token))return;
                            const bool valid=GameForeground()&&CanOpenPlayer();
                            ClosePlayer();
                            if(valid&&!id.empty())Runtime::Get().Command("selectPlaylist",{{"id",id}});
                        });return;
                    }
                    if(action=="toggle"||action=="play"||action=="pause"||action=="next"||action=="previous"||action=="settings"||action=="refresh"||action=="addPlaylist"||action=="removePlaylist")Runtime::Get().Command(action.c_str(),command.value("payload",Json::object()));
                }catch(...){SKSE::log::warn("Invalid CloudWalk UI command");}
            });
        }else SKSE::log::warn("Prisma UI missing; overlay unavailable");
        menuFramework=GetModuleHandleW(L"SKSEMenuFramework.dll");
        if(menuFramework&&GetProcAddress(menuFramework,"AddSectionItem")){settingsOpen=reinterpret_cast<bool(*)()>(GetProcAddress(menuFramework,"IsAnyBlockingWindowOpened"));SKSEMenuFramework::SetSection("CloudWalk");SKSEMenuFramework::AddSectionItem("CloudWalk",Render);}
        if(!InstallControllerGuard())SKSE::log::warn("Controller event guard unavailable; Prisma focus and XInput heartbeat retained");
        InstallMouseGuard();
        runtime.Start();break;
    case SKSE::MessagingInterface::kPreLoadGame:runtime.loaded=false;runtime.inGame=false;allowHud=false;ClosePlayer();SetPanelVisible(false);break;
    case SKSE::MessagingInterface::kPostLoadGame:
    case SKSE::MessagingInterface::kNewGame:runtime.loaded=true;break;
    default:break;
    }
}
}
extern "C" __declspec(dllexport) constinit auto SKSEPlugin_Version=CloudWalk::PluginVersion();
extern "C" __declspec(dllexport) bool SKSEPlugin_Query(const SKSE::QueryInterface* skse,SKSE::PluginInfo* info){
    info->infoVersion=SKSE::PluginInfo::kVersion;info->name="CloudWalk";info->version=REL::Version{0,5,7,0}.pack();
    return skse&&!skse->IsEditor()&&CloudWalk::SupportsRuntime(skse->RuntimeVersion());
}
extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const SKSE::LoadInterface* skse){
    if(auto directory=SKSE::log::log_directory()){
        std::filesystem::create_directories(*directory);
        auto log=std::make_shared<spdlog::logger>("CloudWalk",std::make_shared<spdlog::sinks::basic_file_sink_mt>((*directory/"CloudWalk.log").string(),true));
        spdlog::set_default_logger(log);spdlog::flush_on(spdlog::level::info);
    }
    if(!skse||skse->IsEditor())return false;
    const auto version=skse->RuntimeVersion();
    if(!CloudWalk::SupportsRuntime(version)){
        SKSE::log::error("Unsupported Skyrim runtime {}. CloudWalk targets the documented 14 SE/AE runtimes.",version.string("."));return false;
    }
    const auto database=CloudWalk::AddressLibraryPath(version);
    if(!CloudWalk::AddressLibraryHeaderMatches(database,version)){
        SKSE::log::error("Missing, mismatched, or unsupported-format Address Library: {} (runtime {}). Install the database for this exact runtime.",database.string(),version.string("."));return false;
    }
    SKSE::Init(skse,{.log=false});
    SKSE::log::info("CloudWalk 0.5.7; runtime={}; CommonLibSSE-NG=12.2.0; database={}",version.string("."),database.string());
    const auto messages=SKSE::GetMessagingInterface();
    if(!messages||!SKSE::GetTaskInterface()){
        SKSE::log::error("SKSE messaging/task interface unavailable; install SKSE for the exact game runtime.");return false;
    }
    return messages->RegisterListener(CW::Message);
}
