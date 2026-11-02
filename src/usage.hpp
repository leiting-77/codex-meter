#pragma once
#include "model.hpp"
#include <map>
namespace meter {
struct TokenUsage {
    std::map<std::string,int64_t> days;
    bool available=false;
    std::string message;
};
inline bool validUsageDate(const std::string& s) {
    if(s.size()!=10||s[4]!='-'||s[7]!='-')return false;
    for(size_t i=0;i<s.size();++i)if(i!=4&&i!=7&&(s[i]<'0'||s[i]>'9'))return false;
    int year=std::stoi(s.substr(0,4)),month=std::stoi(s.substr(5,2)),day=std::stoi(s.substr(8,2));
    if(year<1970||month<1||month>12)return false;
    const int lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};
    int maxDay=lengths[month-1]+(month==2&&year%4==0&&(year%100!=0||year%400==0));
    return day>=1&&day<=maxDay;
}
inline TokenUsage parseUsage(const Json& response) {
    TokenUsage result;
    const auto& buckets=field(response,"dailyUsageBuckets");
    if(!buckets.is_array()){result.message=u8"服务暂未提供每日用量";return result;}
    if(buckets.size()>3660){result.message=u8"用量数据格式异常";return result;}
    result.available=true;
    for(const auto& bucket:buckets) {
        auto date=str(bucket,"startDate");auto tokens=integer(bucket,"tokens");
        if(!validUsageDate(date)||!tokens||*tokens<0||result.days.count(date)) {
            result.available=false;result.days.clear();result.message=u8"用量数据格式异常";return result;
        }
        result.days.emplace(date,*tokens);
    }
    while(result.days.size()>30)result.days.erase(result.days.begin());
    return result;
}
}
