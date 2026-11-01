#include "../src/platform.hpp"
#include <iostream>
using namespace meter;
int main(int argc,char** argv) {
    const auto home=fs::path(env(L"CODEX_HOME"));
    const auto scenario=utf8(home.filename().wstring());
    bool keyring=false;
    for(int i=0;i<argc;++i)if(std::string(argv[i])=="cli_auth_credentials_store=\"keyring\"")keyring=true;
    bool init=false,ready=false;
    const bool persisted=fs::exists(home/L"login.fixture");
    for(std::string line;std::getline(std::cin,line);) {
        auto req=Json::parse(line);
        auto method=str(req,"method");
        if(method=="initialized"){ready=init;continue;}
        auto id=req["id"];
        if(method=="initialize"){
            init=true;std::cout<<Json({{"id",id},{"result",Json::object()}}).dump()<<std::endl;continue;
        }
        if(!ready)return 11;
        if(scenario=="hang"){Sleep(60000);return 0;}
        if(scenario=="crash")return 13;
        if(scenario=="bad"){std::cout<<"{invalid json}\n"<<std::flush;continue;}
        if(method=="account/login/start"){
            if(scenario=="loginfresh") {std::ofstream file(home/L"login.fixture");file<<"fixture";}
            if(scenario!="loginhang")std::cout<<Json({{"method","account/login/completed"},{"params",{{"loginId","fixture-login"},{"success",scenario!="loginfail"}}}}).dump()<<std::endl;
            std::cout<<Json({{"id",id},{"result",{{"loginId","fixture-login"},{"authUrl","https://auth.openai.com/fixture"}}}}).dump()<<std::endl;
        }else if(method=="account/read"){
            std::cout<<Json({{"method","account/updated"},{"params",{{"authMode","chatgpt"}}}}).dump()<<std::endl;
            Json account={{"type","chatgpt"},{"name",u8"测试-"+scenario},{"email","fixture@example.com"},{"planType","plus"}};
            if(scenario=="signedout")account=nullptr;
            if(!keyring)account["name"]="missing-keyring";
            if(!env(L"OPENAI_API_KEY").empty())account["name"]="inherited-key";
            if(scenario=="loginfresh"&&!persisted)account=nullptr; // This session cannot see the new login.
            std::cout<<Json({{"id",id},{"result",{{"account",account}}}}).dump()<<std::endl;
        }else if(method=="account/rateLimits/read"){
            if(scenario=="error"){
                std::cout<<Json({{"id",id},{"error",{{"code",-1},{"message","authentication required"}}}}).dump()<<std::endl;continue;
            }
            Json result={{"rateLimits",{{"limitId","codex"},
                {"primary",{{"usedPercent",25},{"windowDurationMins",300},{"resetsAt",1794000000}}},
                {"secondary",{{"usedPercent",40},{"windowDurationMins",10080},{"resetsAt",1794400000}}}}},
                {"rateLimitResetCredits",{{"availableCount",2},{"credits",nullptr}}}};
            auto payload=Json({{"result",result},{"id",id}}).dump();
            // Test JSONL assembled across arbitrary pipe chunks.
            std::cout<<payload.substr(0,payload.size()/2)<<std::flush;
            Sleep(10);std::cout<<payload.substr(payload.size()/2)<<"\n"<<std::flush;
        }else if(method=="account/logout"){
            std::cout<<Json({{"id",id},{"result",Json::object()}}).dump()<<std::endl;
        }
    }
}

