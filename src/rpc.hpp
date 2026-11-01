#pragma once
#include "platform.hpp"
#include "version.h"
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <thread>
namespace meter {
using Clock=std::chrono::steady_clock;
struct RpcFailure:std::runtime_error {using std::runtime_error::runtime_error;};
struct RpcCancelled:RpcFailure { RpcCancelled():RpcFailure(u8"已暂停；手动刷新或重新登录可继续"){} };
inline std::string safeRpcError(const Json& error) {
    auto m=str(error,"message"); std::transform(m.begin(),m.end(),m.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(m.find("auth")!=std::string::npos||m.find("login")!=std::string::npos||m.find("401")!=std::string::npos)return u8"需要重新登录；右键账号可登录";
    if(m.find("keyring")!=std::string::npos||m.find("credential")!=std::string::npos)return u8"系统凭据库不可用，请检查 Windows 凭据服务";
    if(m.find("429")!=std::string::npos)return u8"请求过于频繁，稍后重试";
    if(m.find("method")!=std::string::npos)return u8"当前 Codex 版本不支持此接口，请更新 Codex";
    if(m.find("network")!=std::string::npos||m.find("connect")!=std::string::npos)return u8"网络连接失败，请检查网络或代理";
    return u8"Codex 返回错误，请更新 Codex 或重新登录";
}
inline bool officialLoginUrl(const std::string& url) {
    for(const auto* prefix:{"https://auth.openai.com/","https://chatgpt.com/"})
        if(url.rfind(prefix,0)==0)return true;
    return false;
}
class RpcSession {
    Handle output,input,errors,process,job;
    std::string pending;
    std::deque<Json> notifications;
    std::atomic_bool& cancel;
    int nextId=1;
    void checkCancel(){if(cancel)throw RpcCancelled();}
public:
    RpcSession(const std::wstring& exe,const Profile& profile,const fs::path& data,std::atomic_bool& stop):cancel(stop) {
        if(exe.empty())throw RpcFailure(u8"未找到 Codex；右键菜单可选择 codex.exe");
        fs::path cwd=data/L"work"; fs::create_directories(cwd);
        fs::path home;
        if(!profile.local) {
            if(!validId(profile.id))throw RpcFailure(u8"无效账号标识");
            home=data/L"profiles"/wide(profile.id);fs::create_directories(home);
        }
        std::vector<std::wstring> entries;
        LPWCH original=GetEnvironmentStringsW();
        if(!original)throw RpcFailure(u8"无法获取运行环境");
        for(const wchar_t* p=original;*p;p+=wcslen(p)+1) {
            std::wstring value(p);
            if(!profile.local && (_wcsnicmp(p,L"CODEX_HOME=",11)==0 || _wcsnicmp(p,L"CODEX_SQLITE_HOME=",18)==0 ||
                _wcsnicmp(p,L"CODEX_ACCESS_TOKEN=",19)==0 || _wcsnicmp(p,L"OPENAI_API_KEY=",15)==0 || _wcsnicmp(p,L"CODEX_API_KEY=",14)==0))continue;
            entries.push_back(value);
        }
        FreeEnvironmentStringsW(original);
        if(!profile.local)entries.push_back(L"CODEX_HOME="+home.wstring());
        std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){return _wcsicmp(a.c_str(),b.c_str())<0;});
        std::vector<wchar_t> environment;
        for(const auto& e:entries){environment.insert(environment.end(),e.begin(),e.end());environment.push_back(0);}
        environment.push_back(0);
        SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
        HANDLE r=nullptr,w=nullptr;
        if(!CreatePipe(&r,&w,&sa,0))throw RpcFailure(u8"无法建立通信管道");
        output.reset(r);Handle outChild(w);
        if(!CreatePipe(&r,&w,&sa,0))throw RpcFailure(u8"无法建立通信管道");
        Handle inChild(r);input.reset(w);
        if(!CreatePipe(&r,&w,&sa,0))throw RpcFailure(u8"无法建立通信管道");
        errors.reset(r);Handle errChild(w);
        for(auto h:{output.get(),input.get(),errors.get()})SetHandleInformation(h,HANDLE_FLAG_INHERIT,0);
        job.reset(CreateJobObjectW(nullptr,nullptr));
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!job||!SetInformationJobObject(job.get(),JobObjectExtendedLimitInformation,&limits,sizeof(limits)))
            throw RpcFailure(u8"无法建立后台进程管理");
        SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
        std::vector<unsigned char> storage(bytes);
        auto attrs=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if(!InitializeProcThreadAttributeList(attrs,1,0,&bytes))throw RpcFailure(u8"无法准备进程属性");
        HANDLE inherited[]={inChild.get(),outChild.get(),errChild.get()};
        if(!UpdateProcThreadAttribute(attrs,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr)){
            DeleteProcThreadAttributeList(attrs);throw RpcFailure(u8"无法限制进程句柄");
        }
        STARTUPINFOEXW start{};start.StartupInfo.cb=sizeof(start);
        start.StartupInfo.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;
        start.StartupInfo.wShowWindow=SW_HIDE;
        start.StartupInfo.hStdInput=inChild.get();start.StartupInfo.hStdOutput=outChild.get();start.StartupInfo.hStdError=errChild.get();
        start.lpAttributeList=attrs;
        std::wstring cmd=quote(exe)+L" app-server --listen stdio://";
        if(!profile.local)cmd+=L" -c "+quote(L"cli_auth_credentials_store=\"keyring\"");
        PROCESS_INFORMATION pi{};
        BOOL ok=CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,TRUE,
            CREATE_NO_WINDOW|CREATE_SUSPENDED|CREATE_UNICODE_ENVIRONMENT|EXTENDED_STARTUPINFO_PRESENT,
            environment.data(),cwd.c_str(),&start.StartupInfo,&pi);
        DeleteProcThreadAttributeList(attrs);
        if(!ok)throw RpcFailure(u8"无法启动 Codex，请重新选择可执行文件");
        process.reset(pi.hProcess);Handle thread(pi.hThread);
        if(!AssignProcessToJobObject(job.get(),process.get())) {
            TerminateProcess(process.get(),1);throw RpcFailure(u8"无法管理 Codex 子进程");
        }
        ResumeThread(thread.get());
        inChild.reset();outChild.reset();errChild.reset();
    }
    ~RpcSession() {
        input.reset();
        if(process)WaitForSingleObject(process.get(),200);
        if(job)TerminateJobObject(job.get(),0);
        if(process)WaitForSingleObject(process.get(),500);
    }
    void send(const Json& value) {
        checkCancel();auto data=value.dump()+"\n";size_t offset=0;
        while(offset<data.size()) {
            DWORD n=0;
            if(!WriteFile(input.get(),data.data()+offset,static_cast<DWORD>(data.size()-offset),&n,nullptr)||n==0)
                throw RpcFailure(u8"Codex 通信中断");
            offset+=n;
        }
    }
    Json read(Clock::time_point deadline) {
        while(Clock::now()<deadline) {
            checkCancel();
            auto newline=pending.find('\n');
            if(newline!=std::string::npos) {
                auto line=pending.substr(0,newline);pending.erase(0,newline+1);
                if(line.empty())continue;
                auto parsed=Json::parse(line,nullptr,false);
                if(parsed.is_discarded()||!parsed.is_object())throw RpcFailure(u8"Codex 返回了无效 JSON");
                return parsed;
            }
            // Drain stderr without logging credentials, URLs or user data.
            char buffer[8192];DWORD n=0,available=0;
            if(PeekNamedPipe(errors.get(),nullptr,0,nullptr,&available,nullptr)&&available)
                ReadFile(errors.get(),buffer,std::min<DWORD>(available,sizeof(buffer)),&n,nullptr);
            if(!PeekNamedPipe(output.get(),nullptr,0,nullptr,&available,nullptr))throw RpcFailure(u8"Codex 进程已退出");
            if(available) {
                if(!ReadFile(output.get(),buffer,std::min<DWORD>(available,sizeof(buffer)),&n,nullptr))throw RpcFailure(u8"Codex 通信中断");
                pending.append(buffer,n);
                if(pending.size()>2*1024*1024)throw RpcFailure(u8"Codex 响应过大");
            } else {
                if(WaitForSingleObject(process.get(),0)==WAIT_OBJECT_0)throw RpcFailure(u8"Codex 启动失败；请检查版本、配置和凭据库");
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            }
        }
        throw RpcFailure(u8"连接超时，请检查网络；可手动刷新");
    }
    Json call(const std::string& method,const Json& params=Json::object(),int seconds=30) {
        int id=nextId++;send({{"id",id},{"method",method},{"params",params}});
        auto deadline=Clock::now()+std::chrono::seconds(seconds);
        while(true) {
            auto msg=read(deadline);
            if(rpcId(msg,id)) {
                if(msg.contains("error"))throw RpcFailure(safeRpcError(msg["error"]));
                if(!msg.contains("result"))throw RpcFailure(u8"Codex 响应缺少结果");
                return msg["result"];
            }
            if(msg.contains("method")&&!msg.contains("id")) {
                if(notifications.size()>=128)notifications.pop_front();
                notifications.push_back(std::move(msg));
            } else if(msg.contains("method")&&msg.contains("id")) {
                send({{"id",msg["id"]},{"error",{{"code",-32601},{"message","Monitor does not support this request"}}}});
            }
        }
    }
    void initialize() {
        call("initialize",{{"clientInfo",{{"name","codex_meter"},{"title","Codex Meter"},{"version",METER_VERSION}}}},30);
        send({{"method","initialized"},{"params",Json::object()}});
    }
    void login(const std::function<void(const std::string&)>& status,
               const std::function<bool(const std::string&)>& openUrl={}) {
        auto begin=call("account/login/start",{{"type","chatgpt"}},45);
        auto loginId=str(begin,"loginId"),url=str(begin,"authUrl");
        if(loginId.empty()||!officialLoginUrl(url))throw RpcFailure(u8"登录地址无效；请更新 Codex");
        const bool opened=openUrl?openUrl(url):reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",wide(url).c_str(),nullptr,nullptr,SW_SHOWNORMAL))>32;
        if(!opened)throw RpcFailure(u8"无法打开浏览器，请检查默认浏览器");
        status(u8"请在浏览器完成登录（右键可取消）");
        auto deadline=Clock::now()+std::chrono::minutes(5);
        try {
            while(true) {
                Json message;
                if(!notifications.empty()){message=std::move(notifications.front());notifications.pop_front();}
                else message=read(deadline);
                if(str(message,"method")!="account/login/completed")continue;
                const auto& p=field(message,"params");
                if(str(p,"loginId")!=loginId)continue;
                if(field(p,"success")==true)return;
                throw RpcFailure(u8"登录未完成，请重试");
            }
        } catch(...) {
            // Cancellation closes the dedicated session and its callback listener.
            throw;
        }
    }
};
struct QueryResult {
    std::string name,plan,avatar,displayName;
    Snapshot snapshot;
};
inline QueryResult query(RpcSession& rpc,bool refreshToken=false) {
    auto response=rpc.call("account/read",{{"refreshToken",refreshToken}});
    const auto& account=field(response,"account");
    if(account.is_null())throw RpcFailure(u8"尚未登录；右键账号可登录");
    if(str(account,"type")!="chatgpt")throw RpcFailure(u8"此账号不是 ChatGPT 登录，无法读取订阅额度");
    QueryResult result;
    result.name=accountName(account);result.plan=str(account,"planType","ChatGPT");result.avatar=accountAvatar(account);
    result.displayName=accountDisplayName(account);
    result.snapshot=parseLimits(rpc.call("account/rateLimits/read"));
    return result;
}
}

