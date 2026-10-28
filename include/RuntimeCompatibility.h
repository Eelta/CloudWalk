#pragma once
#include <array>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <REL/Version.h>
#include <SKSE/Interfaces.h>

namespace CloudWalk {
inline constexpr std::array<REL::Version,14> SupportedRuntimes={
    REL::Version{1,5,97,0},
    REL::Version{1,6,317,0},REL::Version{1,6,318,0},REL::Version{1,6,323,0},
    REL::Version{1,6,342,0},REL::Version{1,6,353,0},REL::Version{1,6,629,0},
    REL::Version{1,6,640,0},REL::Version{1,6,659,0},REL::Version{1,6,1130,0},
    REL::Version{1,6,1170,0},REL::Version{1,6,1179,0},
    REL::Version{1,7,99,0},REL::Version{1,7,104,0}
};
inline constexpr bool SupportsRuntime(REL::Version version){
    return std::find(SupportedRuntimes.begin(),SupportedRuntimes.end(),version)!=SupportedRuntimes.end();
}
inline std::filesystem::path AddressLibraryPath(REL::Version version){
    auto name=version[1]==5?"version-":"versionlib-";
    return std::filesystem::path("Data")/"SKSE"/"Plugins"/(name+version.string("-")+".bin");
}
inline bool AddressLibraryHeaderMatches(const std::filesystem::path& path,REL::Version version){
    std::ifstream file(path,std::ios::binary);
    std::uint32_t header[5]{};
    if(!file.read(reinterpret_cast<char*>(header),sizeof(header)))return false;
    const auto expected=version[1]==5?1u:(version[1]>=7?5u:2u);
    if(header[0]!=expected)return false;
    for(std::size_t i=0;i<4;++i)if(header[i+1]!=version[i])return false;
    return true;
}
inline constexpr SKSE::PluginVersionData PluginVersion(){
    SKSE::PluginVersionData data;
    data.PluginVersion(REL::Version{0,5,7,0});data.PluginName("CloudWalk");data.AuthorName("CloudWalk contributors");
    data.UsesAddressLibrary();data.UsesUpdatedStructs();
    data.versionIndependenceEx|=SKSE::PluginVersionData::kVersionIndependentEx_AddressLibraryV5;
    for(std::size_t i=0;i<SupportedRuntimes.size();++i)data.compatibleVersions[i]=SupportedRuntimes[i].pack();
    return data;
}
}
