#pragma once
#include "json.hpp"
#include <string_view>
namespace CloudWalk {
inline nlohmann::json MigrateKeyboardKeys(nlohmann::json keys,int schema){
    if(schema>=6)return keys;
    struct Change {const char* field;int before,after;};
    constexpr Change changes[]={{"toggleKey",67,7},{"previousKey",66,8},{"nextKey",68,9},{"wheelKey",87,47}};
    auto result=keys;
    for(const auto& change:changes)if(keys.contains(change.field)&&keys[change.field]==change.before)result[change.field]=change.after;
    // Explicit custom bindings win over migrating a former default.
    constexpr const char* fields[]={"uiKey","toggleKey","previousKey","nextKey","wheelKey"};
    for(const auto& change:changes){
        if(!keys.contains(change.field)||keys[change.field]!=change.before)continue;
        for(auto field:fields)if(std::string_view(field)!=change.field&&keys.contains(field)&&keys[field]==change.after&&result[field]==keys[field])result[change.field]=change.before;
    }
    return result;
}
}
