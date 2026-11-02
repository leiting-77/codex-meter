#pragma once
#include "platform.hpp"
#include "usage.hpp"
namespace meter {
struct UsageCache { TokenUsage usage; int64_t updated=0; };
inline fs::path usageCachePath(const fs::path& dir,const std::string& id) {
    if(!validId(id))throw std::runtime_error("Invalid usage cache account");
    return dir/L"usage-cache"/(wide(id)+L".json");
}
inline std::optional<UsageCache> loadUsageCache(const fs::path& dir,const Profile& profile) {
    try {
        auto path=usageCachePath(dir,profile.id);
        if(!fs::exists(path)||fs::file_size(path)>128*1024)return {};
        std::ifstream file(path);Json value;file>>value;
        auto updated=integer(value,"updated");
        if(integer(value,"version")!=1||str(value,"identity")!=profile.name||!updated||*updated<=0)return {};
        auto usage=parseUsage(value);
        if(!usage.available)return {};
        while(usage.days.size()>30)usage.days.erase(usage.days.begin());
        return UsageCache{std::move(usage),*updated};
    }catch(...){return {};}
}
inline void saveUsageCache(const fs::path& dir,const Profile& profile,const TokenUsage& usage,int64_t updated) {
    if(!usage.available||updated<=0)throw std::runtime_error("Invalid usage cache data");
    auto path=usageCachePath(dir,profile.id);fs::create_directories(path.parent_path());
    Json value={{"version",1},{"identity",profile.name},{"updated",updated},{"dailyUsageBuckets",Json::array()}};
    auto start=usage.days.begin();for(size_t count=usage.days.size();count>30;--count)++start;
    for(auto it=start;it!=usage.days.end();++it)value["dailyUsageBuckets"].push_back({{"startDate",it->first},{"tokens",it->second}});
    auto temp=path;temp+=L".tmp";
    {std::ofstream file(temp,std::ios::binary|std::ios::trunc);file<<value.dump();file.flush();if(!file)throw std::runtime_error("Cannot save usage cache");}
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace usage cache");
}
inline void removeUsageCache(const fs::path& dir,const std::string& id) {
    auto path=usageCachePath(dir,id);std::error_code error;fs::remove(path,error);
    path+=L".tmp";fs::remove(path,error);
}
}
