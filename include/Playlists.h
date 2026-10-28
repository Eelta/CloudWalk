#pragma once
#include "json.hpp"
#include <algorithm>
#include <cctype>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

namespace CloudWalk {
struct Playlist { std::string id,name,type="playlist"; };
inline bool ValidStationType(const std::string& type){return type=="playlist"||type=="album";}
// Keep existing playlist keys stable; albums have their own ID namespace.
inline std::string StationKey(const Playlist& station){return station.type=="album"?"album:"+station.id:station.id;}
inline bool ValidPlaylistId(const std::string& id){
    return !id.empty()&&id.size()<=18&&id.front()!='0'&&
        std::all_of(id.begin(),id.end(),[](char c){return c>='0'&&c<='9';});
}
inline Playlist ParseStation(const std::string& input){
    if(input.size()>2048)throw std::runtime_error("歌单链接过长");
    const auto first=input.find_first_not_of(" \t\r\n"),last=input.find_last_not_of(" \t\r\n");
    const auto trimmed=first==std::string::npos?std::string{}:input.substr(first,last-first+1);
    if(ValidPlaylistId(trimmed))return {trimmed,{}};
    // Accept full official desktop/mobile URLs, including copied share messages.
    static const std::regex query(R"(https?://(?:music\.163\.com|y\.music\.163\.com)/(?:[^\s?#]*?/)?(?:#/?|m/)?(playlist|album)\?(?:[^\s#]*&)?id=([0-9]+)(?:[&#\s]|$))",std::regex::icase);
    static const std::regex path(R"(https?://(?:music\.163\.com|y\.music\.163\.com)/(?:#/?|m/)?(playlist|album)/([0-9]+)(?:[/?&#\s]|$))",std::regex::icase);
    std::smatch found;
    if((std::regex_search(input,found,query)||std::regex_search(input,found,path))&&ValidPlaylistId(found[2])){
        auto type=found[1].str();std::transform(type.begin(),type.end(),type.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        return {found[2],{},type};
    }
    throw std::runtime_error("请粘贴完整网易云歌单或专辑链接；纯数字默认作为歌单 ID，短链接请先展开");
}
inline nlohmann::json PlaylistJson(const std::vector<Playlist>& lists){
    auto result=nlohmann::json::array();for(const auto& p:lists)result.push_back({{"id",p.id},{"name",p.name},{"type",p.type}});return result;
}
inline nlohmann::json PlaylistViewJson(const std::vector<Playlist>& lists){
    auto result=PlaylistJson(lists);for(std::size_t i=0;i<lists.size();++i)result[i]["id"]=StationKey(lists[i]);return result;
}
inline std::vector<Playlist> ReadPlaylists(const nlohmann::json& value){
    if(!value.is_array()||value.size()>48)throw std::runtime_error("最多保存 48 个电台");
    std::vector<Playlist> result;
    for(const auto& item:value){
        Playlist station{item.at("id").get<std::string>(),item.at("name").get<std::string>(),item.value("type",std::string("playlist"))};
        if(!ValidPlaylistId(station.id)||!ValidStationType(station.type)||station.name.empty()||station.name.size()>96||std::any_of(result.begin(),result.end(),[&](const auto& p){return StationKey(p)==StationKey(station);}))throw std::runtime_error("无效或重复的电台");
        result.push_back(std::move(station));
    }
    return result;
}
inline std::string PlaylistProtocol(const std::string& id,const std::string& type="playlist"){
    if(!ValidPlaylistId(id)||!ValidStationType(type))throw std::runtime_error("无效电台资源");
    const auto message=nlohmann::json{{"cmd","play"},{"type",type},{"id",id}}.dump();
    static constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;unsigned buffer=0;int bits=0;
    for(unsigned char c:message){buffer=(buffer<<8)|c;bits+=8;while(bits>=6){bits-=6;encoded+=alphabet[(buffer>>bits)&63];}}
    if(bits)encoded+=alphabet[(buffer<<(6-bits))&63];
    while(encoded.size()%4)encoded+='=';
    return "orpheus://"+encoded+"/";
}
}
