#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "../third_party/nlohmann/json.hpp"

namespace meter {
using Json = nlohmann::json;
inline const Json& field(const Json& j, const char* key) {
    static const Json empty;
    if (!j.is_object()) return empty;
    auto it = j.find(key);
    return it == j.end() ? empty : *it;
}
inline std::string str(const Json& j, const char* key, std::string fallback = {}) {
    const auto& v = field(j,key);
    return v.is_string() ? v.get<std::string>() : fallback;
}
inline std::optional<int64_t> integer(const Json& j, const char* key) {
    const auto& v=field(j,key);
    if (!v.is_number_integer()) return {};
    if (v.is_number_unsigned() && v.get<uint64_t>() > static_cast<uint64_t>(INT64_MAX)) return {};
    return v.get<int64_t>();
}
struct Window {
    std::string bucketId, bucketName, slot;
    std::optional<double> remaining;
    std::optional<int64_t> minutes, reset;
    bool weekly() const { return minutes && *minutes == 10080; }
};
struct Credit {
    std::string id, title, status;
    std::optional<int64_t> expires;
};
struct Snapshot {
    std::vector<Window> windows;
    std::optional<int64_t> creditCount;
    bool creditDetailsKnown=false;
    std::vector<Credit> credits;
};
inline Snapshot parseLimits(const Json& result) {
    if (!result.is_object()) throw std::runtime_error("Invalid quota result");
    Snapshot out;
    auto bucket = [&](const Json& value, const std::string& key) {
        if (!value.is_object()) return;
        for (const char* slot : {"primary", "secondary"}) {
            const auto& w=field(value,slot);
            if (!w.is_object()) continue;
            Window b;
            b.bucketId=str(value,"limitId",key);
            b.bucketName=str(value,"limitName");
            b.slot=slot;
            const auto& used=field(w,"usedPercent");
            if (used.is_number()) {
                const double n=used.get<double>();
                if (std::isfinite(n)) b.remaining=std::clamp(100.0-n,0.0,100.0);
            }
            b.minutes=integer(w,"windowDurationMins");
            if (b.minutes && *b.minutes<=0) b.minutes.reset();
            b.reset=integer(w,"resetsAt");
            if (b.reset && *b.reset<=0) b.reset.reset();
            out.windows.push_back(std::move(b));
        }
    };
    const auto& byId=field(result,"rateLimitsByLimitId");
    if (byId.is_object() && !byId.empty()) {
        for (auto it=byId.begin(); it!=byId.end(); ++it) bucket(it.value(),it.key());
    } else bucket(field(result,"rateLimits"),"codex");
    std::stable_sort(out.windows.begin(),out.windows.end(),[](const Window& a,const Window& b) {
        return (a.bucketId=="codex") > (b.bucketId=="codex");
    });
    const auto& resetCredits=field(result,"rateLimitResetCredits");
    out.creditCount=integer(resetCredits,"availableCount");
    if (out.creditCount && *out.creditCount<0) out.creditCount.reset();
    const auto& credits=field(resetCredits,"credits");
    out.creditDetailsKnown=credits.is_array();
    if (credits.is_array()) for (const auto& c:credits) {
        if (!c.is_object()) continue;
        auto expiry=integer(c,"expiresAt");
        if(expiry && *expiry<=0)expiry.reset();
        out.credits.push_back({str(c,"id"),str(c,"title"),str(c,"status"),expiry});
    }
    std::stable_sort(out.credits.begin(),out.credits.end(),[](const Credit& a,const Credit& b){
        return a.expires.value_or(INT64_MAX)<b.expires.value_or(INT64_MAX);
    });
    return out;
}
inline std::string cleanDisplayName(std::string value) {
    const auto first=value.find_first_not_of(" \t\r\n");
    if(first==std::string::npos)return {};
    value=value.substr(first,value.find_last_not_of(" \t\r\n")-first+1);
    // Reject the whole address, never turn an email local part into a username.
    if(value.find('@')!=std::string::npos||value.find("mailto:")!=std::string::npos)return {};
    return value;
}
inline std::string accountDisplayName(const Json& account) {
    for(const char* key:{"name","displayName"}) {
        auto value=cleanDisplayName(str(account,key));
        if(!value.empty())return value;
    }
    return {};
}
inline std::string accountName(const Json& account) {
    // Current account/read may only supply email. Never claim a name was fetched if absent.
    for (const char* key : {"name","displayName","email"}) {
        auto value=str(account,key);
        if (!value.empty()) return value;
    }
    return "ChatGPT";
}
inline std::string accountAvatar(const Json& account) {
    for (const char* key : {"avatarUrl","avatar","picture","imageUrl","profileImageUrl"}) {
        auto value=str(account,key);
        if (!value.empty()) return value;
    }
    return {};
}
inline bool rpcId(const Json& message, int id) {
    auto n=integer(message,"id");
    return n && *n==id;
}
inline const Window* mainWindow(const Snapshot& s) {
    if (s.windows.empty()) return nullptr;
    for (const auto& w:s.windows) if (w.bucketId==s.windows.front().bucketId && !w.weekly()) return &w;
    return s.windows.empty()?nullptr:&s.windows.front();
}
inline const Window* weekWindow(const Snapshot& s) {
    const auto* main=mainWindow(s);
    for (const auto& w:s.windows) if (w.weekly() && (!main || main->bucketId==w.bucketId)) return &w;
    return nullptr;
}
inline bool incompleteCredits(const Snapshot& s) {
    if(!s.creditCount || s.credits.empty())return false;
    auto available=std::count_if(s.credits.begin(),s.credits.end(),[](const Credit& c){return c.status.empty()||c.status=="available";});
    return *s.creditCount>available;
}
struct Profile {
    std::string id, name, plan;
    bool local=false;
    std::string avatar;
    std::string displayName, alias;
};
inline std::string profileDisplayName(const Profile& profile,size_t index) {
    auto name=cleanDisplayName(profile.alias);
    if(name.empty())name=cleanDisplayName(profile.displayName);
    return name.empty()?std::string(u8"账号 ")+std::to_string(index+1):name;
}
inline bool validId(const std::string& id) {
    return !id.empty() && id.size()<=64 && std::all_of(id.begin(),id.end(),[](unsigned char c){
        return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-';
    });
}
}

