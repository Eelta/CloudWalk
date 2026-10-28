#pragma once
#include <string_view>
#include <string>
namespace CloudWalk {
struct Translation {std::string_view key;const char* zh;const char* en;};
inline constexpr Translation Translations[]={
    {"language","语言","Language"},
    {"enabled","随声听","Enabled"},
    {"always","常显","Always visible"},
    {"slowdown","减缓强度","Slowdown"},
    {"reset","重置布局","Reset layout"},
    {"hotkeys","快捷键","Hotkeys"},
    {"layout","调整界面","Adjust layout"},
    {"power","播放开关","Playback"},
    {"next","下一首","Next track"},
    {"previous","上一首","Previous track"},
    {"wheel","电台轮盘","Radio wheel"},
    {"unbound","未绑定","Unbound"},
    {"capture","请按键…","Press a key…"},
    {"playlists","电台","Stations"},
    {"link","链接 / ID","Link / ID"},
    {"name","名称","Name"},
    {"add","添加","Add"},
    {"failed","操作失败","Action failed"},
    {"controller","手柄","Controller"},
    {"controller_enabled","启用","Enabled"},
    {"deadzone","摇杆死区","Stick deadzone"},
    {"pad_capture","按组合键后松开…","Press a chord, then release…"},
    {"pad_error","绑定无效","Invalid binding"},
    {"help_controller","支持 XInput 手柄。默认 LB+RB 按住开轮盘，右摇杆选台，松键确认；LT / RT 或方向键左 / 右翻页，B 取消，方向键上 / 下缩放。LB+X 打开布局调整。原版或其它模组仍可能占用相同组合键，请按需改绑。","Supports XInput controllers. Hold LB+RB for the wheel, aim with the right stick, release to confirm; LT/RT or D-pad left/right page, B cancels, D-pad up/down scales. LB+X opens layout editing. Skyrim or other mods may share bindings; rebind as needed."},
    {"help_deadzone","摇杆中心忽略范围，默认 0.25；漂移时提高。轮盘开始及翻页后，右摇杆需先回中再选台。回中保留已选电台，松开轮盘组合键确认。","Center deadzone, default 0.25. Increase for drift. Center the right stick after opening or paging before aiming. Centering retains an existing selection; release the wheel chord to confirm."},
    {"help_pad_binding","点击后先松开全部手柄按键，再同时按住最多 3 个键，全部松开后自动保存。B / Back / Start 取消录入，不能绑定；× 禁用。重复及包含其它完整绑定的组合会拒绝。","Click, release all controller buttons, hold up to 3 buttons together, then release all to save. B/Back/Start cancel capture and cannot be bound; × disables. Duplicate or nested bindings are rejected."},
    {"help_pad_layout","打开两个界面：A 切换选中对象，左摇杆移动，右摇杆上 / 下等比缩放；LB / RB 上一首 / 下一首，Y 开关播放，方向键左 / 右调音量，B 退出。自动保存布局。","Opens both UIs: A switches selection, left stick moves, right stick up/down scales uniformly; LB/RB change tracks, Y toggles playback, D-pad left/right adjusts volume, B exits. Layout saves automatically."},
    {"help_pad_error","最多 3 个同时按住的按键；不能包含 B、Back、Start，不能与其它绑定重复或形成包含关系。断开手柄会取消录入。","Use up to 3 simultaneous buttons, without B, Back or Start. Bindings cannot duplicate or include another complete binding. Disconnection cancels capture."},
    {"help_language","立即切换菜单和随声听语言，自动保存。电台自定义名称保留原文。","Changes the menu and player language immediately and saves automatically. Custom station names are kept."},
    {"help_enabled","开启后由网易云播放并接管原版背景音乐；关闭后暂停网易云并恢复本模组接管前的音乐音量。","Uses NetEase playback and suppresses Skyrim background music. Turning it off pauses NetEase and restores the music volume held by CloudWalk."},
    {"help_always","正常游戏时常显随声听 HUD，不占用键鼠；其它游戏菜单打开时隐藏。","Shows a passive player HUD during gameplay. Other game menus hide it."},
    {"help_slowdown","轮盘和调整模式的减缓强度：0 不减速，1 完全暂停，中间值按 1−强度运行。默认 0.90，约 10% 速度。拖动即应用并自动保存。网易云速度不变。","Slowdown strength while the wheel or layout editor is open: 0 = normal, 1 = paused, intermediate speed = 1 - strength. Default 0.90 gives about 10% speed. Changes apply and save immediately. NetEase speed is unchanged."},
    {"help_reset","重置轮盘和随声听的大小与位置，不删除歌单或快捷键。Enter 同时显示两者；左键选中后拖动，滚轮等比例缩放。","Resets both UI sizes and positions, keeping playlists and hotkeys. Enter shows both; left-click a UI to select it, drag to move, and scroll to scale uniformly."},
    {"help_binding","点击键名后按下新的键盘按键；Esc 取消。立即应用并自动保存。不能与其它绑定重复；× 取消绑定。","Click the key name, then press a keyboard key. Escape cancels. Changes apply and save automatically. Duplicate bindings are rejected; × unbinds."},
    {"help_layout","按下后同时显示轮盘和随声听进行布局调整，再按一次或 Esc 关闭。","Shows both the wheel and player for layout editing. Press again or Escape to close."},
    {"help_power","开启 / 关闭随声听，独立于界面是否显示。","Turns playback control on or off independently of UI visibility."},
    {"help_next","请求下一首；需要网易云开放系统媒体控制。","Requests the next track when NetEase exposes media controls."},
    {"help_previous","请求上一首；需要网易云开放系统媒体控制。","Requests the previous track when NetEase exposes media controls."},
    {"help_wheel","按住显示，无光标，用鼠标相对移动选方向，松键确认。左键上一页，右键下一页；滚轮缩放。打开或翻页后从中心重新选台。Esc、菜单或失焦取消。","Hold to open without a cursor, aim using relative mouse movement, and release to confirm. Left/right click pages backward/forward; scroll scales. Opening or paging resets aim. Escape, other menus or focus loss cancel."},
    {"help_clear","立即取消此项快捷键并自动保存。","Unbinds this hotkey and saves automatically."},
    {"help_link","完整 music.163.com / y.music.163.com 歌单或专辑链接。纯数字默认作为歌单 ID；专辑请用完整链接，短链接请先展开。","A full music.163.com / y.music.163.com playlist or album URL. Numeric IDs mean playlists; use full URLs for albums. Expand short links first."},
    {"help_name","可选的电台名称；留空使用资源类型和 ID。名称最多 96 个 UTF-8 字节。","Optional station name. Empty names use the resource type and ID. Maximum 96 UTF-8 bytes."},
    {"help_add","添加后自动保存。歌单和专辑合计最多 48 个电台，每页 8 个；同类型重复 ID 会拒绝。","Adds and saves a station. Up to 48 playlist/album stations, 8 per page. Duplicate IDs of the same type are rejected."},
    {"help_remove","只删除 CloudWalk 本地条目，不修改网易云收藏。自动保存。","Removes only the local CloudWalk entry, keeping NetEase favorites unchanged. Saves automatically."},
};
inline const char* Text(bool english,std::string_view key){for(const auto& entry:Translations)if(entry.key==key)return english?entry.en:entry.zh;return "?";}
inline std::string ErrorText(bool english,const std::string& message){
    if(!english)return message;
    struct Error {std::string_view zh,en;};
    static constexpr Error errors[]={
        {"请先在外部打开网易云并播放一首歌曲","Open NetEase outside the game and play a track first."},
        {"请先在外部打开官方网易云客户端并播放一首歌曲","Open the official NetEase client and play a track first."},
        {"网易云未接受控制指令，请检查客户端系统媒体控制设置","NetEase rejected the command. Check its system media control setting."},
        {"客户端未接受播放开关","NetEase rejected the playback request."},
        {"网易云音量控制不可用，请在客户端调整","Application volume control is unavailable. Adjust it in NetEase."},
        {"这个电台已经在列表中","This station is already in the list."},
        {"最多保存 48 个电台","A maximum of 48 stations is allowed."},
        {"电台名称最多 96 个 UTF-8 字节","Station names are limited to 96 UTF-8 bytes."},
        {"电台已移除，请重新打开轮盘","This station was removed. Reopen the wheel."},
        {"未能发送歌单请求，请检查网易云客户端及 orpheus 协议关联","Could not send the playlist request. Check NetEase and its orpheus association."},
        {"请在游戏前台且松开 Alt 后切换电台","Keep the game in the foreground and release Alt before switching stations."},
        {"无法启用前台保护，已取消歌单切换","Foreground protection could not be enabled. The playlist request was cancelled."},
        {"无法保护游戏窗口，请关闭网易云窗口置顶并确保客户端已打开","Window protection failed. Turn off NetEase always-on-top and keep the client open."},
        {"无法恢复当前网易云队列","Could not resume the current NetEase queue."},
        {"请粘贴完整网易云歌单或专辑链接；纯数字默认作为歌单 ID，短链接请先展开","Use a full NetEase playlist or album URL. Numeric IDs mean playlists. Expand short links first."},
        {"歌单链接过长","The playlist URL is too long."},
        {"设置读取失败，已使用默认设置","Settings could not be loaded; defaults are being used."},
        {"Windows 媒体控制初始化失败，请查看 CloudWalk.log","Windows media initialization failed. Check CloudWalk.log."},
        {"设置未保存：请检查设置范围或快捷键冲突","Settings were not saved. Check ranges and duplicate hotkeys."},
        {"操作失败，请查看 CloudWalk.log","Operation failed. Check CloudWalk.log."},
        {"请在游戏外打开网易云音乐，并先播放一首歌曲","Open NetEase outside the game and play a track first."},
        {"已连接网易云，暂时无法读取歌名","Connected, but track metadata is temporarily unavailable."},
        {"无法读取网易云媒体会话，请检查客户端系统媒体控制设置","Could not read the media session. Check NetEase system media controls."},
    };
    for(const auto& e:errors)if(message==e.zh)return std::string(e.en);
    return message;
}
}
