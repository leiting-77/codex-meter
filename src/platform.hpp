#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include "model.hpp"
namespace meter {
namespace fs=std::filesystem;
inline std::wstring wide(const std::string& s) {
    if(s.empty()) return {};
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    if(!n) return L"无效文本";
    std::wstring out(n,L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n);
    return out;
}
inline std::string utf8(const std::wstring& s) {
    if(s.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    std::string out(n,'\0');
    WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr);
    return out;
}
inline std::wstring env(const wchar_t* name) {
    DWORD n=GetEnvironmentVariableW(name,nullptr,0);
    if(!n) return {};
    std::wstring out(n,L'\0');
    GetEnvironmentVariableW(name,out.data(),n); out.resize(n-1); return out;
}
inline fs::path exeDir() {
    std::wstring p(32768,L'\0');
    auto n=GetModuleFileNameW(nullptr,p.data(),static_cast<DWORD>(p.size()));
    p.resize(n); return fs::path(p).parent_path();
}
inline fs::path defaultDataDir() {
    PWSTR p=nullptr;
    if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&p))) throw std::runtime_error("LocalAppData unavailable");
    fs::path out(p); CoTaskMemFree(p); return out/L"CodexMeter";
}
inline std::string newId() {
    GUID id{}; if(FAILED(CoCreateGuid(&id))) throw std::runtime_error("Cannot create profile");
    wchar_t s[40]{}; StringFromGUID2(id,s,40);
    auto t=utf8(s); return t.substr(1,t.size()-2);
}
inline std::wstring quote(const std::wstring& s) {
    std::wstring out=L"\""; size_t slash=0;
    for(wchar_t c:s) {
        if(c==L'\\') { ++slash; continue; }
        if(c==L'"') { out.append(slash*2+1,L'\\'); out+=c; }
        else { out.append(slash,L'\\'); out+=c; }
        slash=0;
    }
    out.append(slash*2,L'\\'); return out+L"\"";
}
class Handle {
    HANDLE h=nullptr;
public:
    Handle()=default;
    explicit Handle(HANDLE value):h(value){}
    ~Handle(){reset();}
    Handle(const Handle&)=delete; Handle& operator=(const Handle&)=delete;
    Handle(Handle&& o) noexcept:h(o.h){o.h=nullptr;}
    Handle& operator=(Handle&& o) noexcept {if(this!=&o){reset();h=o.h;o.h=nullptr;}return *this;}
    void reset(HANDLE n=nullptr){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);h=n;}
    HANDLE get() const {return h;}
    HANDLE release(){auto n=h;h=nullptr;return n;}
    explicit operator bool() const {return h&&h!=INVALID_HANDLE_VALUE;}
};
struct Settings {
    int x=INT_MIN,y=INT_MIN,interval=60,opacity=242;
    bool topmost=true,docked=false;
    bool glass=true,taskbar=false,closeToTray=false,startExpanded=false,restoreExpanded=false;
    int restoreX=INT_MIN,restoreY=INT_MIN;
    int dockSide=0;
    std::wstring codex;
    std::vector<Profile> profiles;
};
inline Settings loadSettings(const fs::path& dir) {
    Settings out;
    auto path=dir/L"settings.json";
    if(!fs::exists(path))return out;
    if(fs::file_size(path)>1024*1024)throw std::runtime_error("Settings too large");
    std::ifstream file(path); Json j; file>>j;
    if(!j.is_object())throw std::runtime_error("Invalid settings object");
    auto intVal=[&](const char* k,int def){auto v=integer(j,k);return v&&*v>=INT_MIN&&*v<=INT_MAX?static_cast<int>(*v):def;};
    out.x=intVal("x",INT_MIN);out.y=intVal("y",INT_MIN);
    out.restoreX=intVal("restoreX",INT_MIN);out.restoreY=intVal("restoreY",INT_MIN);
    auto boolVal=[&](const char* k,bool def){const auto& v=field(j,k);return v.is_boolean()?v.get<bool>():def;};
    out.glass=boolVal("glass",true);out.taskbar=boolVal("taskbar",false);
    out.closeToTray=boolVal("closeToTray",false);out.startExpanded=boolVal("startExpanded",false);
    out.restoreExpanded=boolVal("restoreExpanded",false);
    const int version=intVal("version",1);
    out.interval=std::clamp(intVal("interval",60),30,900);
    out.opacity=std::clamp(intVal("opacity",242),160,255);
    if(version<2&&out.opacity==255)out.opacity=238;
    if(version<3&&out.opacity==238)out.opacity=228;
    if(version<4&&out.opacity==228)out.opacity=242;
    const auto& t=field(j,"topmost");out.topmost=t.is_boolean()?t.get<bool>():true;
    const auto& d=field(j,"docked");out.docked=d.is_boolean()?d.get<bool>():false;
    out.dockSide=std::clamp(intVal("dockSide",0),0,4);
    out.codex=wide(str(j,"codex"));
    const auto& list=field(j,"profiles");
    if(list.is_array())for(const auto& p:list) {
        auto id=str(p,"id");
        if(!validId(id)||out.profiles.size()>=32)continue;
        if(std::any_of(out.profiles.begin(),out.profiles.end(),[&](const Profile& x){return x.id==id;}))continue;
        out.profiles.push_back({id,str(p,"name"),str(p,"plan"),id=="local"});
        auto& profile=out.profiles.back();
        profile.alias=cleanDisplayName(str(p,"alias"));
        profile.displayName=cleanDisplayName(str(p,"displayName"));
        if(!p.contains("displayName")) {
            // Older versions stored either the full email or the returned name.
            auto legacy=cleanDisplayName(profile.name);
            if(legacy!="ChatGPT"&&legacy!=u8"新账号"&&legacy!=u8"本机 Codex")profile.displayName=legacy;
        }
    }
    return out;
}
inline void saveSettings(const fs::path& dir,const Settings& s) {
    fs::create_directories(dir);
    Json j={{"version",5},{"x",s.x},{"y",s.y},{"interval",s.interval},{"opacity",s.opacity},
            {"topmost",s.topmost},{"docked",s.docked},{"dockSide",s.dockSide},
            {"glass",s.glass},{"taskbar",s.taskbar},{"closeToTray",s.closeToTray},{"startExpanded",s.startExpanded},
            {"restoreX",s.restoreX},{"restoreY",s.restoreY},{"restoreExpanded",s.restoreExpanded},
            {"codex",utf8(s.codex)},{"profiles",Json::array()}};
    for(const auto& p:s.profiles)j["profiles"].push_back({{"id",p.id},{"name",p.name},{"plan",p.plan},
        {"displayName",p.displayName},{"alias",p.alias}});
    auto temp=dir/L"settings.json.tmp";
    {std::ofstream f(temp,std::ios::binary|std::ios::trunc);f<<j.dump(2);f.flush();if(!f)throw std::runtime_error("Save failed");}
    if(!MoveFileExW(temp.c_str(),(dir/L"settings.json").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace settings");
}
inline std::wstring findCodex(const Settings& s) {
    std::error_code ec;
    auto exists=[&](const fs::path& p){ec.clear();return fs::is_regular_file(p,ec);};
    if(!s.codex.empty()&&exists(s.codex))return s.codex;
    auto bundled=exeDir()/L"runtime"/L"codex.exe";
    if(exists(bundled))return bundled.wstring();
    wchar_t path[32768]{};
    if(SearchPathW(nullptr,L"codex.exe",nullptr,32768,path,nullptr))return path;
    auto installed=fs::path(env(L"LOCALAPPDATA"))/L"Programs"/L"OpenAI"/L"Codex"/L"bin"/L"codex.exe";
    if(exists(installed))return installed.wstring();
    const auto modern=fs::path(env(L"LOCALAPPDATA"))/L"OpenAI"/L"Codex"/L"bin";
    if(exists(modern/L"codex.exe"))return (modern/L"codex.exe").wstring();
    // Desktop installations may keep the native executable in a version directory.
    fs::path newest;fs::file_time_type newestTime=fs::file_time_type::min();
    ec.clear();fs::directory_iterator it(modern,fs::directory_options::skip_permission_denied,ec),end;
    for(;!ec&&it!=end;it.increment(ec)) {
        auto candidate=it->path()/L"codex.exe";
        if(exists(candidate)) {
            std::error_code timeError;auto time=fs::last_write_time(candidate,timeError);
            if(!timeError&&time>=newestTime){newest=candidate;newestTime=time;}
        }
    }
    if(!newest.empty())return newest.wstring();
    return {};
}
}

