#include "../src/worker.hpp"
#include <iostream>
#include <tlhelp32.h>
using namespace meter;
#define CHECK(c) do{++checks;if(!(c))throw std::runtime_error("Failed: " #c);}while(0)
int main(int argc,char** argv) {
    int checks=0;
    try {
        auto root=exeDir()/L"ipc test 中文";
        fs::create_directories(root);
        std::wstring exe=(exeDir()/L"mock_codex.exe").wstring();
        std::atomic_bool stop{false};
        // No real credential used; the child must strip this fixture marker.
        const auto priorKey=env(L"OPENAI_API_KEY");
        SetEnvironmentVariableW(L"OPENAI_API_KEY",L"not-a-real-key");
        for(const auto* id:{"alice","bob"}) {
            RpcSession r(exe,{id,"","",false},root,stop);r.initialize();auto q=query(r);
            CHECK(q.name==std::string(u8"测试-")+id);
            CHECK(q.snapshot.windows.size()==2);
            CHECK(q.snapshot.windows[0].remaining==75);
            CHECK(q.snapshot.windows[1].remaining==60);
            CHECK(q.snapshot.creditCount==2);
            CHECK(!q.snapshot.creditDetailsKnown);
        }
        for(const auto* id:{"bad","crash","signedout","error"}) {
            bool caught=false;
            try{RpcSession r(exe,{id,"","",false},root,stop);r.initialize();query(r);}
            catch(const RpcFailure&){caught=true;}
            CHECK(caught);
        }
        {
            RpcSession r(exe,{"hang","","",false},root,stop);r.initialize();
            auto begin=Clock::now();bool caught=false;
            std::thread canceller([&]{Sleep(100);stop=true;});
            try{query(r);}catch(const RpcFailure&){caught=true;}
            canceller.join();
            CHECK(caught);
            CHECK(Clock::now()-begin<std::chrono::seconds(2));
        }
        stop=false;
        {
            RpcSession r(exe,{"hang","","",false},root,stop);r.initialize();
            bool caught=false;try{r.call("account/read",Json::object(),1);}catch(const RpcFailure&){caught=true;}
            CHECK(caught);
        }
        CHECK(officialLoginUrl("https://auth.openai.com/oauth/authorize?x=y"));
        CHECK(!officialLoginUrl("https://auth.openai.com.evil.example/login"));
        CHECK(!officialLoginUrl("file:///C:/example.exe"));
        Settings s;s.interval=300;s.docked=true;s.dockSide=2;
        s.profiles={{"alice",u8"用户甲","plus",false},{"bob",u8"用户乙","pro",false}};
        s.profiles[0].displayName="Sample User";s.profiles[0].alias=u8"我的昵称";
        saveSettings(root,s);auto loaded=loadSettings(root);
        CHECK(loaded.profiles.size()==2);
        CHECK(loaded.profiles[0].name==u8"用户甲");
        CHECK(loaded.profiles[0].displayName=="Sample User");
        CHECK(loaded.profiles[0].alias==u8"我的昵称");
        CHECK(loaded.interval==300);
        CHECK(loaded.docked&&loaded.dockSide==2);
        s.glass=false;s.taskbar=true;s.closeToTray=true;s.startExpanded=true;s.restoreX=-800;s.restoreY=120;s.restoreExpanded=true;
        saveSettings(root,s);loaded=loadSettings(root);
        CHECK(!loaded.glass&&loaded.taskbar&&loaded.closeToTray&&loaded.startExpanded);
        CHECK(loaded.restoreX==-800&&loaded.restoreY==120&&loaded.restoreExpanded);
        {
            auto legacy=root/L"legacy-settings";fs::create_directories(legacy);
            {std::ofstream f(legacy/L"settings.json");f<<R"({"version":1,"opacity":255})";}
            CHECK(loadSettings(legacy).opacity==242);
            {std::ofstream f(legacy/L"settings.json");f<<R"({"profiles":[{"id":"old","name":"numeric-user@example.test"},{"id":"named","name":"Sample User"}]})";}
            auto migrated=loadSettings(legacy);
            CHECK(migrated.profiles[0].displayName.empty());
            CHECK(profileDisplayName(migrated.profiles[0],0)==u8"账号 1");
            CHECK(profileDisplayName(migrated.profiles[1],1)=="Sample User");
        }
        {
            auto invalid=root/L"invalid-settings";fs::create_directories(invalid);
            {std::ofstream f(invalid/L"settings.json");f<<"[]";}
            bool rejected=false;try{loadSettings(invalid);}catch(...){rejected=true;}
            CHECK(rejected);
        }
        {
            std::ifstream f(root/L"settings.json");std::string raw((std::istreambuf_iterator<char>(f)),{});
            CHECK(raw.find("token")==std::string::npos);
        }
        // Offline login completion uses a fake opener: no browser or actual credentials.
        {
            std::error_code ec;fs::remove(root/L"profiles"/L"loginfresh"/L"login.fixture",ec);
            std::atomic_int opens{0};
            Worker worker(nullptr,0,[&](const std::string& url){++opens;return officialLoginUrl(url);});
            auto waitEvent=[&](const std::string& id,bool finished) {
                auto deadline=Clock::now()+std::chrono::seconds(8);
                while(Clock::now()<deadline) {
                    for(auto& e:worker.take())if(e.id==id&&e.finished==finished)return e;
                    Sleep(10);
                }
                throw std::runtime_error("Worker event timed out");
            };
            worker.enqueue({{"loginfresh","","",false},Action::Login,exe,root});
            auto logged=waitEvent("loginfresh",true);
            CHECK(logged.data.has_value()&&!logged.cancelled);
            CHECK(logged.data->snapshot.windows.size()==2);
            CHECK(opens==1); // Login notification before response is also handled.
            worker.enqueue({{"loginhang","","",false},Action::Login,exe,root});
            waitEvent("loginhang",false);
            worker.enqueue({{"alice","","",false},Action::Refresh,exe,root});
            CHECK(waitEvent("alice",true).data.has_value()); // Refresh is not held up by the login.
            CHECK(worker.activeKind()==2);
            worker.cancelCurrent();auto cancelled=waitEvent("loginhang",true);
            CHECK(cancelled.cancelled&&!cancelled.data);
            CHECK(worker.activeKind()==0);
            worker.cancelCurrent(); // Cancelling while idle must not cancel the next request.
            worker.enqueue({{"bob","","",false},Action::Refresh,exe,root});
            CHECK(waitEvent("bob",true).data.has_value());
            worker.enqueue({{"loginfail","","",false},Action::Login,exe,root});
            auto failed=waitEvent("loginfail",true);CHECK(!failed.data&&!failed.cancelled&&!failed.status.empty());
            worker.enqueue({{"hang","","",false},Action::Refresh,exe,root});waitEvent("hang",false);
            worker.cancelCurrent();CHECK(waitEvent("hang",true).cancelled);
            worker.enqueue({{"loginhang","","",false},Action::Login,exe,root});waitEvent("loginhang",false);
            const auto before=Clock::now();worker.shutdown();CHECK(Clock::now()-before<std::chrono::seconds(2));
        }
        // Sessions should have left no helper process after normal/error/cancel paths.
        Sleep(50);
        Handle processes(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));
        PROCESSENTRY32W p{};p.dwSize=sizeof(p);bool orphan=false;
        if(Process32FirstW(processes.get(),&p))do{if(_wcsicmp(p.szExeFile,L"mock_codex.exe")==0)orphan=true;}while(Process32NextW(processes.get(),&p));
        CHECK(!orphan);
        std::cout<<checks<<" RPC/storage checks passed\n";
        if(argc>1&&std::string(argv[1])=="--real") {
            SetEnvironmentVariableW(L"OPENAI_API_KEY",priorKey.empty()?nullptr:priorKey.c_str());
            auto found=findCodex(Settings{});CHECK(!found.empty());
            Profile local{"local","","",true};RpcSession r(found,local,root,stop);r.initialize();
            auto q=query(r);
            std::cout<<"Real account connected; windows="<<q.snapshot.windows.size()<<"; credits field="<<q.snapshot.creditCount.has_value()<<"\n";
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}

