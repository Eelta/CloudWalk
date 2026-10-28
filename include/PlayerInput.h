#pragma once
#include <cstdint>
#include <string_view>
namespace CloudWalk {
enum class WindowAction{none,open,close,consume};
inline bool PanelVisible(bool interactive,bool alwaysShow,bool inGame,bool menusClear=true){return inGame&&menusClear&&(interactive||alwaysShow);}
inline bool SettingsMenuBlocks(bool hasStateAPI,bool stateOpen,std::uint64_t sinceRender){return hasStateAPI?stateOpen:sinceRender<200;}
// Built-in passive overlays and our own focus menu never count as other UI.
inline bool PassiveMenu(std::string_view name,bool ownFocus){
    return name=="HUD Menu"||name=="Cursor Menu"||name=="Fader Menu"||
        name=="TitleSequence Menu"||(ownFocus&&name=="PrismaUI_FocusMenu");
}
struct MenuTraits {bool alwaysOpen=false,pauses=false,cursor=false,menuContext=false,modal=false,inventory=false;};
inline bool MenuBlocksOverlay(std::string_view name,bool ownFocus,MenuTraits state={}){
    if(PassiveMenu(name,ownFocus))return false;
    constexpr std::string_view menus[]={"Main Menu","Loading Menu","Console","MapMenu","InventoryMenu","MagicMenu","StatsMenu","Journal Menu","TweenMenu","FavoritesMenu","ContainerMenu","BarterMenu","GiftMenu","Book Menu","Crafting Menu","RaceSex Menu","Lockpicking Menu","Sleep/Wait Menu","Training Menu","MessageBoxMenu","Dialogue Menu"};
    for(auto menu:menus)if(name==menu)return true;
    return !state.alwaysOpen||state.pauses||state.cursor||state.menuContext||state.modal||state.inventory;
}
inline WindowAction WindowKey(bool open,std::uint64_t now,std::uint64_t openedAt,std::uint64_t closedAt,unsigned key,unsigned uiKey,bool canOpen){
    const bool toggle=uiKey&&key==uiKey,escape=key==1;
    if(open&&(escape||toggle))return now-openedAt>220?WindowAction::close:WindowAction::consume;
    if(!open&&now-closedAt<250&&(escape||toggle))return WindowAction::consume;
    if(!open&&toggle&&canOpen)return WindowAction::open;
    return WindowAction::none;
}
}
