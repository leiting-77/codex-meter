#include "../src/model.hpp"
#include "../src/ui_geometry.hpp"
#include <iostream>
#include <stdexcept>
#define CHECK(c) do { ++checks; if (!(c)) throw std::runtime_error("Failed: " #c); } while(0)
int main() {
    using namespace meter;
    int checks=0;
    CHECK(quotaSweep(0)==0);
    CHECK(quotaSweep(25)==90);
    CHECK(quotaSweep(50)==180);
    CHECK(quotaSweep(75)==270);
    CHECK(quotaSweep(100)==360);
    CHECK(quotaSweep(-10)==0&&quotaSweep(200)==360);
    CHECK(removeHit(400,60,440,38,34,500));
    CHECK(!removeHit(400,180,440,38,34,500)); // Former full-height delete target.
    CHECK(!removeHit(388,48,440,38,34,500)); // Outside the circular button.
    CHECK(!removeHit(400,30,440,8,34,500)); // Scrolled behind the header.
    CHECK(!removeHit(400,500,440,478,34,500));
    CHECK(edgeSide({0,50,320,190},{0,0,1920,1080},0)==1);
    CHECK(edgeSide({1,50,321,190},{0,0,1920,1080},0)==0);
    CHECK(edgeSide({1600,50,1920,190},{0,0,1920,1080},0)==2);
    CHECK(edgeSide({50,0,370,140},{0,0,1920,1080},0)==3);
    CHECK(edgeSide({50,940,370,1080},{0,0,1920,1080},0)==4);
    CHECK(edgeSide({-1920,50,-1600,190},{-1920,0,0,1080},0)==1);
    CHECK(edgeSide({-300,-300,-100,-100},{0,0,1920,1080},0)==0);
    auto docked=dockExtent(32,1080);
    CHECK(docked.visible==1080&&docked.total>1080);
    CHECK(docked.maxScroll==docked.total-docked.visible);
    CHECK(dockExtent(1,1080).maxScroll==0);
    CHECK(dockExtent(32,600).visible==600);
    auto pinned=pinDock({40,250,124,426},{0,0,1920,1080},1);
    CHECK(pinned.left==0&&pinned.top==250);
    pinned=pinDock({20,1100,104,1276},{0,0,1920,1080},2);
    CHECK(pinned.left==1836&&pinned.top==904);
    pinned=pinDock({-1800,400,-1624,490},{-1920,0,0,1080},3);
    CHECK(pinned.left==-1800&&pinned.top==0);
    pinned=pinDock({2000,30,2176,120},{0,0,1920,1080},4);
    CHECK(pinned.left==1744&&pinned.top==990);
    auto s=parseLimits(Json::parse(R"({
      "rateLimits":{"primary":{"usedPercent":99}},
      "rateLimitsByLimitId":{
        "codex":{"limitName":null,
           "secondary":{"resetsAt":2000,"windowDurationMins":10080,"usedPercent":15.5},
           "primary":{"resetsAt":2000,"usedPercent":25,"windowDurationMins":300}},
        "extra":{"primary":{"resetsAt":2000,"usedPercent":72,"windowDurationMins":60}}
      },
      "rateLimitResetCredits":{"availableCount":3,"credits":[
        {"id":"c","expiresAt":null},{"id":"b","expiresAt":2500},{"id":"a","expiresAt":1500}]}
    })"));
    CHECK(s.windows.size()==3);
    CHECK(*s.windows[0].remaining==75);
    CHECK(*s.windows[1].remaining==84.5);
    CHECK(s.windows[1].weekly());
    CHECK(*s.windows[2].remaining==28);
    CHECK(s.creditCount==3);
    CHECK(s.credits[0].id=="a");
    CHECK(!s.credits[2].expires);
    CHECK(mainWindow(s)->slot=="primary");
    CHECK(weekWindow(s)->slot=="secondary");
    Snapshot mixed;
    mixed.windows={{"codex","","primary",50.0,10080,2000},{"other","","primary",10.0,300,2000}};
    CHECK(mainWindow(mixed)->bucketId=="codex");
    CHECK(mainWindow(mixed)==weekWindow(mixed));
    auto partial=parseLimits(Json::parse(R"({"rateLimitResetCredits":{"availableCount":3,"credits":[{"id":"one","expiresAt":0}]}})"));
    CHECK(incompleteCredits(partial));
    CHECK(!partial.credits[0].expires);
    auto unknown=parseLimits(Json::parse(R"({"rateLimits":{"primary":{"usedPercent":null,"resetsAt":null}},"rateLimitResetCredits":null})"));
    CHECK(!unknown.windows[0].remaining);
    CHECK(!unknown.creditCount);
    CHECK(!unknown.creditDetailsKnown);
    auto zeros=parseLimits(Json::parse(R"({"rateLimits":{"primary":{"usedPercent":100,"windowDurationMins":14400}},"rateLimitResetCredits":{"availableCount":0,"credits":[]}})"));
    CHECK(zeros.creditCount==0);
    CHECK(zeros.creditDetailsKnown);
    CHECK(*zeros.windows[0].remaining==0);
    CHECK(!zeros.windows[0].weekly());
    auto over=parseLimits(Json::parse(R"({"rateLimitsByLimitId":{},"rateLimits":{"primary":{"usedPercent":120},"secondary":{"usedPercent":-2}}})"));
    CHECK(*over.windows[0].remaining==0);
    CHECK(*over.windows[1].remaining==100);
    CHECK(accountName(Json::parse(R"({"name":"\u6d4b\u8bd5\u7528\u6237","email":"test@example.com"})"))==u8"测试用户");
    CHECK(accountName(Json{{"email","test@example.com"}})=="test@example.com");
    CHECK(accountDisplayName(Json{{"email","numeric-user@example.test"}}).empty());
    CHECK(accountDisplayName(Json{{"name","numeric-user@example.test"},{"displayName","Sample User"}})=="Sample User");
    CHECK(accountDisplayName(Json{{"name","  Sample User  "},{"email","test@example.com"}})=="Sample User");
    Profile display{"one","numeric-user@example.test","plus",false};
    CHECK(profileDisplayName(display,0)==u8"账号 1");
    CHECK(profileDisplayName(display,1)==u8"账号 2");
    display.displayName="Sample User";
    CHECK(profileDisplayName(display,0)=="Sample User");
    display.alias=u8"我的账号";
    display.name="different@example.com";display.displayName.clear(); // A refresh must not replace an alias.
    CHECK(profileDisplayName(display,0)==u8"我的账号");
    display.alias="mailto:test@example.com";
    CHECK(profileDisplayName(display,0)==u8"账号 1");
    CHECK(cleanDisplayName("test@example.com").empty());
    CHECK(cleanDisplayName("   ").empty());
    CHECK(accountAvatar(Json{{"avatarUrl","https://example.test/avatar.png"}})=="https://example.test/avatar.png");
    CHECK(!rpcId(Json{{"id",10}},1));
    CHECK(rpcId(Json::parse("{ \"result\":{}, \"id\":1 }"),1));
    CHECK(!rpcId(Json{{"id","1"}},1));
    CHECK(!validId("../escape"));
    CHECK(!validId("a\\b"));
    CHECK(validId("account-012345"));
    bool rejected=false;
    try { auto bad=Json::parse("{broken"); (void)bad; } catch(...) { rejected=true; }
    CHECK(rejected);
    std::cout << checks << " model checks passed\n";
}

