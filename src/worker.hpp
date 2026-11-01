#pragma once
#include "rpc.hpp"
#include <condition_variable>
#include <mutex>
#include <queue>
namespace meter {
enum class Action { Refresh, Login, Remove };
struct Task { Profile profile; Action action=Action::Refresh; std::wstring exe; fs::path data; };
struct Event {
    std::string id,status;
    std::optional<QueryResult> data;
    bool finished=false,removed=false,cancelled=false;
};
class Worker {
    HWND window;UINT message;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Task> tasks,logins;
    std::deque<Event> events;
    bool stopping=false,queryActive=false,loginActive=false;
    std::atomic_bool cancelQuery{false},cancelLogin{false};
    std::thread queryThread,loginThread;
    std::function<bool(const std::string&)> openUrl;
    void emit(Event e) {
        {std::lock_guard<std::mutex> lock(mutex);events.push_back(std::move(e));}
        if(window)PostMessageW(window,message,0,0);
    }
    void run(bool loginLane) {
        auto& queue=loginLane?logins:tasks;
        auto& cancel=loginLane?cancelLogin:cancelQuery;
        auto& active=loginLane?loginActive:queryActive;
        while(true) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock,[&]{return stopping||!queue.empty();});
                if(stopping)return;
                task=std::move(queue.front());queue.pop_front();cancel=false;active=true;
            }
            emit({task.profile.id,loginLane?u8"正在准备登录…":u8"正在同步…"});
            Event result;result.id=task.profile.id;result.finished=true;
            try {
                if(loginLane) {
                    {
                        RpcSession rpc(task.exe,task.profile,task.data,cancel);rpc.initialize();
                        rpc.login([&](const std::string& status){emit({task.profile.id,status});},openUrl);
                    }
                    // Start fresh, as a manual refresh does, after login persists credentials.
                    for(int attempt=0;attempt<3;++attempt) {
                        if(cancel)throw RpcCancelled();
                        emit({task.profile.id,u8"登录成功，正在读取额度…"});
                        try {
                            RpcSession fresh(task.exe,task.profile,task.data,cancel);fresh.initialize();
                            result.data=query(fresh,true);break;
                        } catch(const RpcCancelled&) {throw;}
                        catch(const RpcFailure&) {
                            if(attempt==2)throw;
                            for(int tick=0;tick<10;++tick){if(cancel)throw RpcCancelled();Sleep(100);}
                        }
                    }
                } else {
                    RpcSession rpc(task.exe,task.profile,task.data,cancel);rpc.initialize();
                    if(task.action==Action::Remove) {
                        if(task.profile.local)throw RpcFailure(u8"本机账号只解除监测，不退出 Codex");
                        rpc.call("account/logout");result.removed=true;
                    } else result.data=query(rpc);
                }
            } catch(const RpcCancelled& e) {result.status=e.what();result.cancelled=true;}
            catch(const RpcFailure& e) {result.status=e.what();}
            catch(...) {result.status=u8"无法读取数据，请检查 Codex 版本或重试";}
            {std::lock_guard<std::mutex> lock(mutex);active=false;}
            emit(std::move(result));
        }
    }
public:
    Worker(HWND w,UINT msg,std::function<bool(const std::string&)> opener={}):window(w),message(msg),openUrl(std::move(opener)) {
        try {queryThread=std::thread([this]{run(false);});loginThread=std::thread([this]{run(true);});}
        catch(...){shutdown();throw;}
    }
    ~Worker(){shutdown();}
    void enqueue(Task t) {
        {std::lock_guard<std::mutex> lock(mutex);if(stopping)return;
            (t.action==Action::Login?logins:tasks).push_back(std::move(t));}wake.notify_all();
    }
    int activeKind(){std::lock_guard<std::mutex> lock(mutex);return loginActive?2:queryActive?1:0;}
    void cancelCurrent(){std::lock_guard<std::mutex> lock(mutex);if(loginActive)cancelLogin=true;else if(queryActive)cancelQuery=true;}
    std::deque<Event> take() {
        std::lock_guard<std::mutex> lock(mutex);std::deque<Event> out;out.swap(events);return out;
    }
    void shutdown() {
        {std::lock_guard<std::mutex> lock(mutex);stopping=true;tasks.clear();logins.clear();cancelQuery=true;cancelLogin=true;}
        wake.notify_all();
        if(queryThread.joinable())queryThread.join();if(loginThread.joinable())loginThread.join();
    }
};
}
