#include "worker.hpp"
#include "resource.h"
#include "ui_geometry.hpp"
#include "ring_renderer.hpp"
#include "usage_cache.hpp"
#include "surface_renderer.hpp"
#include <windowsx.h>
#include <commdlg.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <map>
#include <memory>
#include <ctime>
#include <cctype>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include <array>
using namespace meter;
namespace {
constexpr UINT WM_RESULTS=WM_APP+1,WM_TRAY=WM_APP+2,WM_DOCK=WM_APP+3;
constexpr int HEADER=34,FOOTER=42;
constexpr int DETAIL_WIDTH=380,DETAIL_GAP=8,USAGE_HEIGHT=116,USAGE_SPACE=130;
constexpr COLORREF BG=RGB(243,248,248),SURFACE=RGB(248,240,226),CARD=RGB(218,204,239),
    BUTTON=RGB(220,207,232),TRACK=RGB(232,231,239),INK=RGB(24,23,68),
    MUTED=RGB(106,99,128),LINE=RGB(250,246,235),ACCENT=RGB(129,113,224),
    LOW=RGB(232,96,88),OK=RGB(69,182,77),OLD=RGB(151,145,171),WARM=RGB(242,201,76),
    TRAFFIC_RED=RGB(238,99,88),TRAFFIC_YELLOW=RGB(237,184,63),TRAFFIC_GREEN=RGB(57,181,74);
enum Command { Expand=101,Refresh,Add,LinkLocal,ChooseExe,Cancel,Hide,Quit,Topmost,About,
    Interval30,Interval60,Interval300,Opaque,Translucent,ProfileRefresh=1000,ProfileLogin=2000,ProfileRemove=3000,
    Preferences=120,WindowClose=4000,WindowCollapse,WindowExpand,DockToggle=4100,ProfileRename=5000 };
struct View {
    Profile profile;
    Snapshot snapshot;
    TokenUsage usage;
    time_t usageChecked=0;
    int64_t usageUpdated=0;
    std::string status;
    bool busy=false,failed=false,paused=false;
    int failures=0;
    time_t synced=0;
    Clock::time_point next=Clock::now();
};
HWND window=nullptr,closeButton=nullptr,collapseButton=nullptr,expandButton=nullptr,addButton=nullptr,refreshButton=nullptr;
Settings settings;fs::path dataDir;
std::vector<View> views;
std::unique_ptr<Worker> worker;
bool expanded=false,demo=false,closing=false,layoutBusy=false;
bool glassActive=false,dragging=false;
bool dockPressed=false;
bool dockDragging=false;
POINT dockPointerStart{};
RECT dockWindowStart{};
int pendingDock=0;
RECT pendingDockRect{};
std::string pressedRemove;
HWND tooltip=nullptr;
std::wstring tooltipText;
int hotRemove=-1;
int hotButton=0;
double scale=1;
int scrollY=0,totalContent=0,viewContent=0;
UINT taskbarCreated=0;
NOTIFYICONDATAW tray{};
ULONG_PTR graphicsToken=0;
std::map<std::pair<int,int>,HFONT> fonts;
int px(int v){return static_cast<int>(std::lround(v*scale));}
int logical(int v){return static_cast<int>(std::lround(v/scale));}
HFONT font(int size,int weight=FW_NORMAL) {
    auto key=std::make_pair(size,weight);auto it=fonts.find(key);if(it!=fonts.end())return it->second;
    auto f=CreateFontW(-px(size),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    fonts.emplace(key,f);return f;
}
void clearFonts(){for(auto& x:fonts)DeleteObject(x.second);fonts.clear();}
void text(HDC dc,const std::wstring& s,int x,int y,int w,int h,COLORREF color=INK,int size=12,
          int weight=FW_NORMAL,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS) {
    auto old=SelectObject(dc,font(size,weight));SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);
    RECT r{px(x),px(y),px(x+w),px(y+h)};DrawTextW(dc,s.c_str(),static_cast<int>(s.size()),&r,flags|DT_NOPREFIX);
    SelectObject(dc,old);
}
void box(HDC dc,int x,int y,int w,int h,COLORREF fill,int radius=0,COLORREF border=CLR_INVALID){
    if(radius&&graphicsToken) {
        if(paintSurfaceShape(dc,px(x),px(y),px(w),px(h),static_cast<float>(px(radius)),fill,border,BG,glassActive))return;
        Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        auto color=[](COLORREF value){return Gdiplus::Color(255,GetRValue(value),GetGValue(value),GetBValue(value));};
        const float left=static_cast<float>(px(x))+.5f,top=static_cast<float>(px(y))+.5f;
        const float width=static_cast<float>(px(w))-1,height=static_cast<float>(px(h))-1;
        const float diameter=std::min({static_cast<float>(px(radius)),width,height});
        if(diameter<=0)return;
        Gdiplus::GraphicsPath path;
        path.AddArc(left,top,diameter,diameter,180,90);
        path.AddArc(left+width-diameter,top,diameter,diameter,270,90);
        path.AddArc(left+width-diameter,top+height-diameter,diameter,diameter,0,90);
        path.AddArc(left,top+height-diameter,diameter,diameter,90,90);path.CloseFigure();
        Gdiplus::SolidBrush brush(color(fill));graphics.FillPath(&brush,&path);
        if(border!=CLR_INVALID&&border!=fill){Gdiplus::Pen pen(color(border),1);graphics.DrawPath(&pen,&path);}
        return;
    }
    auto brush=CreateSolidBrush(fill);auto pen=CreatePen(PS_SOLID,1,border==CLR_INVALID?fill:border);
    auto ob=SelectObject(dc,brush),op=SelectObject(dc,pen);
    if(radius)RoundRect(dc,px(x),px(y),px(x+w),px(y+h),px(radius),px(radius));
    else Rectangle(dc,px(x),px(y),px(x+w),px(y+h));
    SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(brush);DeleteObject(pen);
}
std::wstring percent(const std::optional<double>& v) {
    if(!v)return L"—";
    std::wostringstream s;s<<std::fixed<<std::setprecision(0)<<*v<<L"%";return s.str();
}
std::wstring compactName(const Profile& profile) {
    auto it=std::find_if(views.begin(),views.end(),[&](const View& v){return v.profile.id==profile.id;});
    return wide(profileDisplayName(profile,static_cast<size_t>(it-views.begin())));
}
std::wstring avatarText(const Profile& profile) {
    auto name=compactName(profile);
    if(name.empty())return L"?";
    if(name.size()>=2&&name[0]<128&&name[1]<128) {
        wchar_t a=name[0],b=name[1];
        if(a>=L'a'&&a<=L'z')a=static_cast<wchar_t>(a-L'a'+L'A');
        if(b>=L'a'&&b<=L'z')b=static_cast<wchar_t>(b-L'a'+L'A');
        return std::wstring{a,b};
    }
    return name.substr(0,name[0]>=0xD800&&name[0]<=0xDBFF&&name.size()>1?2:1);
}
void avatar(HDC dc,int cx,int cy,int radius,const std::wstring& label) {
    auto brush=CreateSolidBrush(ACCENT);auto pen=CreatePen(PS_SOLID,1,ACCENT);
    auto ob=SelectObject(dc,brush),op=SelectObject(dc,pen);
    Ellipse(dc,px(cx-radius),px(cy-radius),px(cx+radius),px(cy+radius));
    SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(brush);DeleteObject(pen);
    text(dc,label,cx-radius,cy-radius/2,radius*2,radius, SURFACE, radius<=10?8:10,FW_BOLD,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
}
std::wstring duration(const Window& w) {
    if(w.weekly())return L"周额度";
    if(!w.minutes)return w.slot=="primary"?L"当前窗口":L"次级窗口";
    if(*w.minutes%1440==0)return std::to_wstring(*w.minutes/1440)+L" 天";
    if(*w.minutes%60==0)return std::to_wstring(*w.minutes/60)+L" 小时";
    return std::to_wstring(*w.minutes)+L" 分钟";
}
std::wstring date(const std::optional<int64_t>& t) {
    if(!t)return L"未提供";
    time_t time=static_cast<time_t>(*t);std::tm tm{};
    if(localtime_s(&tm,&time)!=0)return L"未提供";
    wchar_t b[64]{};wcsftime(b,64,L"%m-%d %H:%M",&tm);return b;
}
std::wstring reset(const Window& b) {
    if(!b.reset)return L"重置时间未提供";
    auto diff=*b.reset-std::time(nullptr);
    if(diff<=0)return L"等待服务器确认重置";
    auto mins=(diff+59)/60;
    std::wstring left;
    if(mins>=1440)left=std::to_wstring(mins/1440)+L"天 "+std::to_wstring((mins%1440)/60)+L"小时";
    else if(mins>=60)left=std::to_wstring(mins/60)+L"小时 "+std::to_wstring(mins%60)+L"分";
    else left=std::to_wstring(mins)+L"分";
    return left+L"后重置 · "+date(b.reset);
}
bool stale(const View& v){return v.failed||(v.synced&&std::time(nullptr)-v.synced>settings.interval*2+60);}
void progress(HDC dc,int x,int y,int w,int h,const std::optional<double>& value,bool old){
    box(dc,x,y,w,h,TRACK,h);
    if(!value||*value<=0)return;
    int n=static_cast<int>(std::lround(w*std::clamp(*value,0.0,100.0)/100));
    if(n>0)box(dc,x,y,n,h,old?OLD:(*value<20?LOW:WARM),std::min(h,n));
}
void ringColor(HDC dc,int cx,int cy,int radius,const std::optional<double>& value,bool old,COLORREF preferred) {
    auto color=old?(preferred==WARM?RGB(189,178,138):RGB(154,146,183)):preferred;
    drawQuotaRing(dc,px(cx),px(cy),px(radius),px(5),value,TRACK,color);
}
void ring(HDC dc,int cx,int cy,int radius,const std::optional<double>& value,bool old) {
    ringColor(dc,cx,cy,radius,value,old,WARM);
}
int cardHeight(const View& v) {
    int rows=std::max(1,static_cast<int>(v.snapshot.windows.size()));
    int credits=std::max(1,static_cast<int>(v.snapshot.credits.size()));
    return 54+rows*45+24+credits*20+(incompleteCredits(v.snapshot)?20:0)+(v.failed?36:0)+12+USAGE_SPACE;
}
int contentHeight(){
    if(views.empty())return 100;
    int h=4;for(const auto& v:views)h+=expanded?cardHeight(v)+DETAIL_GAP:50;return h+4;
}
void persist() {
    if(demo)return;
    settings.profiles.clear();for(const auto& v:views)settings.profiles.push_back(v.profile);
    try{saveSettings(dataDir,settings);}catch(...){MessageBoxW(window,L"无法保存设置。请检查数据目录是否可写。",L"Codex Meter",MB_ICONWARNING);}
}
void applyGlass() {
    // Supported Windows 11 backdrop; older systems retain an opaque readable surface.
    // Numeric attribute constants allow building with older Windows SDK headers.
    HIGHCONTRASTW contrast{sizeof(contrast)};SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0);
    const bool want=settings.glass&&!(contrast.dwFlags&HCF_HIGHCONTRASTON);
    const int backdrop=want?3:1; // DWMSBT_TRANSIENTWINDOW / DWMSBT_NONE
    glassActive=want&&SUCCEEDED(DwmSetWindowAttribute(window,38,&backdrop,sizeof(backdrop)));
    if(!want)DwmSetWindowAttribute(window,38,&backdrop,sizeof(backdrop));
    MARGINS margins=glassActive?MARGINS{-1,-1,-1,-1}:MARGINS{};
    if(FAILED(DwmExtendFrameIntoClientArea(window,&margins)))glassActive=false;
}
void shapeWindow(int width,int height) {
    const int round=2; // DWMWCP_ROUND: let the compositor antialias the outer edge.
    if(SUCCEEDED(DwmSetWindowAttribute(window,33,&round,sizeof(round)))) {
        SetWindowRgn(window,nullptr,TRUE);
        const COLORREF noBorder=0xFFFFFFFEu;
        DwmSetWindowAttribute(window,34,&noBorder,sizeof(noBorder));
    } else {
        HRGN region=CreateRoundRectRgn(0,0,px(width)+1,px(height)+1,px(24),px(24));
        if(!SetWindowRgn(window,region,TRUE))DeleteObject(region);
    }
}
uint32_t surfacePixel(uint32_t pixel) {
    return resolveSurfacePixel(pixel,BG,glassActive);
}
void applyStyle(){
    applyGlass();
    SetWindowPos(window,settings.topmost?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    auto ex=GetWindowLongPtrW(window,GWL_EXSTYLE);
    const bool taskbarChanged=static_cast<bool>(ex&WS_EX_APPWINDOW)!=settings.taskbar;
    const bool restoreVisibility=taskbarChanged&&IsWindowVisible(window);
    if(restoreVisibility)ShowWindow(window,SW_HIDE);
    ex&=~(WS_EX_TOOLWINDOW|WS_EX_APPWINDOW|WS_EX_LAYERED);
    ex|=settings.taskbar?WS_EX_APPWINDOW:WS_EX_TOOLWINDOW;
    SetWindowLongPtrW(window,GWL_EXSTYLE,ex);
    SetWindowPos(window,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
    if(restoreVisibility)ShowWindow(window,SW_SHOWNOACTIVATE);
    SetWindowTextW(closeButton,settings.closeToTray?L"隐藏到托盘":L"退出应用");
    InvalidateRect(window,nullptr,FALSE);
}
int dockSideFor(const RECT& r,const RECT& work,int threshold,bool requireTouch) {
    return edgeSide({r.left,r.top,r.right,r.bottom},{work.left,work.top,work.right,work.bottom},threshold,requireTouch);
}
void layout(bool initial=false) {
    if(!window||layoutBusy)return;
    layoutBusy=true;
    RECT old{};GetWindowRect(window,&old);
    POINT anchor{initial?settings.x:old.left,initial?settings.y:old.top};
    bool unspecified=anchor.x==INT_MIN||anchor.y==INT_MIN;
    if(unspecified)anchor={0,0};
    RECT reference{anchor.x,anchor.y,anchor.x+std::max<LONG>(1,old.right-old.left),anchor.y+std::max<LONG>(1,old.bottom-old.top)};
    HMONITOR monitor=MonitorFromRect(&reference,MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};GetMonitorInfoW(monitor,&info);
    if(settings.docked) {
        const bool horizontal=settings.dockSide==3||settings.dockSide==4;
        const int count=std::max(1,static_cast<int>(views.size()));
        auto extent=dockExtent(count,logical(horizontal?info.rcWork.right-info.rcWork.left:info.rcWork.bottom-info.rcWork.top));
        const int width=horizontal?extent.visible:84;
        const int height=horizontal?90:extent.visible;
        int x=anchor.x,y=anchor.y;
        if(settings.dockSide==1)x=info.rcWork.left;
        else if(settings.dockSide==2)x=info.rcWork.right-px(width);
        else x=std::clamp<LONG>(x,info.rcWork.left,std::max(info.rcWork.left,info.rcWork.right-px(width)));
        if(settings.dockSide==3)y=info.rcWork.top;
        else if(settings.dockSide==4)y=info.rcWork.bottom-px(height);
        else y=std::clamp<LONG>(y,info.rcWork.top,std::max(info.rcWork.top,info.rcWork.bottom-px(height)));
        totalContent=extent.total;viewContent=extent.visible;scrollY=std::clamp(scrollY,0,extent.maxScroll);
        SetWindowPos(window,nullptr,x,y,px(width),px(height),SWP_NOZORDER|SWP_NOACTIVATE);
        shapeWindow(width,height);
        for(HWND b:{closeButton,collapseButton,expandButton,addButton,refreshButton})ShowWindow(b,SW_HIDE);
        InvalidateRect(window,nullptr,FALSE);layoutBusy=false;return;
    }
    totalContent=contentHeight();
    int width=expanded?DETAIL_WIDTH:320;
    int maxHeight=std::max(90,logical(info.rcWork.bottom-info.rcWork.top)-80);
    int footer=(expanded||views.empty())?FOOTER:0;
    int height=HEADER+std::min(totalContent,expanded?maxHeight-HEADER-footer:208)+footer;
    viewContent=height-HEADER-footer;
    scrollY=std::clamp(scrollY,0,std::max(0,totalContent-viewContent));
    int x=unspecified?info.rcWork.right-px(width)-20:anchor.x;
    int y=unspecified?info.rcWork.top+80:anchor.y;
    x=std::clamp<LONG>(x,info.rcWork.left,std::max(info.rcWork.left,info.rcWork.right-px(width)));
    y=std::clamp<LONG>(y,info.rcWork.top,std::max(info.rcWork.top,info.rcWork.bottom-px(height)));
    SetWindowPos(window,nullptr,x,y,px(width),px(height),SWP_NOZORDER|SWP_NOACTIVATE);
    shapeWindow(width,height);
    for(HWND b:{closeButton,collapseButton,expandButton})ShowWindow(b,SW_SHOW);
    MoveWindow(closeButton,px(width-75),px(9),px(18),px(18),TRUE);
    MoveWindow(collapseButton,px(width-51),px(9),px(18),px(18),TRUE);
    MoveWindow(expandButton,px(width-27),px(9),px(18),px(18),TRUE);
    ShowWindow(addButton,footer?SW_SHOW:SW_HIDE);ShowWindow(refreshButton,footer?SW_SHOW:SW_HIDE);
    MoveWindow(addButton,px(width-104),px(height-35),px(92),px(28),TRUE);
    MoveWindow(refreshButton,px(12),px(height-35),px(60),px(28),TRUE);
    for(HWND b:{closeButton,collapseButton,expandButton,addButton,refreshButton})
        SendMessageW(b,WM_SETFONT,reinterpret_cast<WPARAM>(font(12)),TRUE);
    InvalidateRect(window,nullptr,FALSE);layoutBusy=false;
}
void undock(){
    if(!settings.docked)return;
    settings.docked=false;expanded=settings.restoreExpanded;scrollY=0;
    if(settings.restoreX!=INT_MIN&&settings.restoreY!=INT_MIN)
        SetWindowPos(window,nullptr,settings.restoreX,settings.restoreY,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    layout();persist();
}
void dock(int side,const RECT& rect){
    if(settings.docked)return;
    settings.restoreX=rect.left;settings.restoreY=rect.top;settings.restoreExpanded=expanded;
    settings.dockSide=side;settings.docked=true;scrollY=0;
    SetWindowPos(window,nullptr,rect.left,rect.top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    layout();persist();
}
void refreshAll();
void toggle(){if(settings.docked)undock();expanded=!expanded;scrollY=0;layout();if(expanded&&!demo)refreshAll();}
void queue(View& v,Action action=Action::Refresh){
    if(demo||v.busy||!worker)return;
    if(action==Action::Login){v.usage={};v.usageChecked=0;v.usageUpdated=0;removeUsageCache(dataDir,v.profile.id);}
    v.busy=true;v.paused=false;v.status=action==Action::Login?u8"准备登录…":u8"等待同步…";
    worker->enqueue({v.profile,action,findCodex(settings),dataDir,
        expanded&&!settings.docked&&IsWindowVisible(window)&&std::time(nullptr)-v.usageChecked>=300});
    InvalidateRect(window,nullptr,FALSE);
}
void refreshAll(){for(auto& v:views){v.usageChecked=0;queue(v);}}
void preferences();
void addAccount(){
    if(demo){MessageBoxW(window,L"当前为演示模式。退出后正常启动，即可添加真实账号。",L"Codex Meter",MB_OK);return;}
    if(findCodex(settings).empty()){preferences();if(findCodex(settings).empty())return;}
    if(views.size()>=32){MessageBoxW(window,L"最多保存 32 个账号。",L"Codex Meter",MB_OK);return;}
    try{
        View v;v.profile.id=newId();v.profile.name=u8"新账号";
        views.push_back(std::move(v));persist();layout();queue(views.back(),Action::Login);
    }catch(...){MessageBoxW(window,L"无法创建账号配置。",L"Codex Meter",MB_ICONWARNING);}
}
void linkLocal(){
    if(demo)return;
    if(findCodex(settings).empty()){preferences();if(findCodex(settings).empty())return;}
    auto it=std::find_if(views.begin(),views.end(),[](const View& v){return v.profile.local;});
    if(it!=views.end()){queue(*it);return;}
    if(views.size()>=32){MessageBoxW(window,L"最多保存 32 个账号。",L"Codex Meter",MB_OK);return;}
    View v;v.profile={"local",u8"本机 Codex","",true};views.push_back(std::move(v));
    persist();layout();queue(views.back());
}
void chooseExe(){
    wchar_t file[32768]{};OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner=window;ofn.lpstrFilter=L"Codex executable (*.exe)\0*.exe\0\0";
    ofn.lpstrFile=file;ofn.nMaxFile=32768;ofn.lpstrTitle=L"选择 Codex CLI 的 codex.exe";
    ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(GetOpenFileNameW(&ofn)){settings.codex=file;persist();refreshAll();}
}
void compact(HDC dc,const View& v,int y,int w) {
    box(dc,8,y,w-16,46,CARD,18);
    bool old=stale(v);
    auto name=compactName(v.profile);
    avatar(dc,30,y+23,13,avatarText(v.profile));
    text(dc,name,50,y+4,53,22,INK,12,FW_MEDIUM);
    auto main=mainWindow(v.snapshot),week=weekWindow(v.snapshot);
    if(!main) {
        text(dc,L"—",w-50,y+3,38,22,MUTED,13,FW_MEDIUM,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        text(dc,wide(v.status.empty()?(v.synced?u8"服务未返回额度":u8"尚未同步"):v.status),106,y+4,w-166,20,v.failed?LOW:MUTED,11);
        return;
    }
    auto shortLabel=main->weekly()?L"周":(!main->minutes?L"额":*main->minutes%60==0?
        std::to_wstring(*main->minutes/60)+L"h":std::to_wstring(*main->minutes)+L"m");
    text(dc,shortLabel,104,y+1,25,18,MUTED,10);
    progress(dc,132,y+7,w-188,7,main->remaining,old);
    text(dc,percent(main->remaining),w-53,y,42,20,old?MUTED:INK,12,FW_MEDIUM,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    if(week&&week!=main) {
        text(dc,L"周",104,y+20,25,18,MUTED,10);
        progress(dc,132,y+26,w-188,6,week->remaining,old);
        text(dc,percent(week->remaining),w-53,y+19,42,20,MUTED,11,FW_NORMAL,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    } else text(dc,v.paused?L"已暂停":old?L"旧数据":L"剩余额度",132,y+19,w-142,18,old?LOW:MUTED,10);
    if(old)text(dc,L"!",88,y+6,10,16,LOW,12,FW_BOLD);
}
void removeGlyph(HDC dc,int x,int y,bool hover) {
    // Keep the 24px hit target while drawing only a quiet, rounded cross.
    const auto color=hover?LOW:MUTED;
    Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::Pen pen(Gdiplus::Color(255,GetRValue(color),GetGValue(color),GetBValue(color)),static_cast<float>(1.4*scale));
    pen.SetStartCap(Gdiplus::LineCapRound);pen.SetEndCap(Gdiplus::LineCapRound);
    graphics.DrawLine(&pen,static_cast<float>(px(x+8)),static_cast<float>(px(y+8)),
        static_cast<float>(px(x+16)),static_cast<float>(px(y+16)));
    graphics.DrawLine(&pen,static_cast<float>(px(x+16)),static_cast<float>(px(y+8)),
        static_cast<float>(px(x+8)),static_cast<float>(px(y+16)));
}
std::array<std::string,7> usageDates() {
    std::array<std::string,7> dates;
    auto now=std::time(nullptr);std::tm today{};localtime_s(&today,&now);
    for(int i=0;i<7;++i) {
        auto day=today;day.tm_hour=12;day.tm_min=day.tm_sec=0;day.tm_mday-=6-i;day.tm_isdst=-1;
        auto stamp=std::mktime(&day);std::tm normalized{};localtime_s(&normalized,&stamp);
        char date[16]{};std::strftime(date,sizeof(date),"%Y-%m-%d",&normalized);dates[i]=date;
    }
    return dates;
}
std::wstring tokenAmount(long double value) {
    const wchar_t* unit=L"";
    if(value>=1e9L){value/=1e9L;unit=L"B";}else if(value>=1e6L){value/=1e6L;unit=L"M";}
    else if(value>=1e3L){value/=1e3L;unit=L"K";}
    std::wostringstream out;out<<std::fixed<<std::setprecision(*unit?1:0)<<value<<unit;return out.str();
}
int usageChartTop(const View& v,int top){return top+cardHeight(v)-USAGE_SPACE-4;}
void usageChart(HDC dc,const View& v,int top,int width) {
    const int left=22,area=width-44,step=area/7,baseline=top+82;
    box(dc,left-4,top,area+8,USAGE_HEIGHT,RGB(237,230,246),14);
    text(dc,L"近七天 Token 用量",left+8,top+5,150,18,INK,11,FW_MEDIUM);
    auto dates=usageDates();long double total=0,peak=1;int known=0;
    for(const auto& date:dates) {auto it=v.usage.days.find(date);if(it!=v.usage.days.end()){
        total+=it->second;peak=std::max(peak,static_cast<long double>(it->second));++known;}}
    const auto summary=known?(known==7?L"合计 ":L"已知合计 ")+tokenAmount(total):L"暂无数据";
    text(dc,summary,left+150,top+5,area-160,18,MUTED,9,FW_NORMAL,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    for(int i=0;i<7;++i) {
        int cx=left+step*i+step/2;auto it=v.usage.days.find(dates[i]);bool has=it!=v.usage.days.end();
        const int height=has&&it->second>0?std::max(2,static_cast<int>(it->second/peak*40)):0;
        box(dc,cx-7,baseline,14,2,RGB(217,206,232),2);
        if(height)box(dc,cx-7,baseline-height,14,height,i==6?WARM:RGB(239,214,134),7);
        text(dc,has?tokenAmount(static_cast<long double>(it->second)):L"—",cx-step/2,baseline-height-16,step,14,has?INK:MUTED,9,FW_NORMAL,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        text(dc,i==6?L"今天":wide(dates[i].substr(5)),cx-step/2,baseline+4,step,16,i==6?INK:MUTED,9,i==6?FW_MEDIUM:FW_NORMAL,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
    std::wstring note=demo?L"演示数据 · 悬停竖条查看明细":!v.usage.message.empty()?wide(v.usage.message):
        v.usageUpdated?L"更新 "+date(v.usageUpdated)+L" · 按服务日期展示":
        !v.usageChecked?L"正在等待每日用量；可按 F5 刷新":L"服务未提供这七天的用量";
    text(dc,note,left+8,top+103,area-16,11,MUTED,9);
}
void detail(HDC dc,const View& v,int y,int w,int index) {
    int height=cardHeight(v);
    box(dc,10,y,w-20,height,CARD,20);
    text(dc,wide(v.profile.name.empty()?"ChatGPT":v.profile.name),22,y+9,w-186,22,INK,13,FW_MEDIUM);
    removeGlyph(dc,w-48,y+10,hotRemove==index);
    text(dc,demo?L"演示数据":(v.busy?L"同步中":v.paused?L"已暂停":v.failed?L"更新失败":stale(v)?L"旧数据":v.synced?L"已同步":L"未连接"),
         w-154,y+11,90,21,v.failed?LOW:MUTED,11,FW_NORMAL,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    std::wstring sub=wide(v.profile.plan);
    if(v.profile.local)sub+=L" · 本机 Codex";
    if(v.synced)sub+=L" · "+date(static_cast<int64_t>(v.synced));
    text(dc,sub,22,y+31,w-44,16,MUTED,10);
    int row=y+54;
    if(v.snapshot.windows.empty()){text(dc,v.synced?L"服务未返回额度窗口":L"额度尚未读取",22,row,w-44,22,MUTED);row+=45;}
    for(const auto& b:v.snapshot.windows) {
        auto label=duration(b);
        if(v.snapshot.windows.size()>2||b.bucketId!="codex")
            label=wide(b.bucketName.empty()?b.bucketId:b.bucketName)+L" · "+label;
        text(dc,label,22,row,w-146,19,MUTED,11);
        text(dc,percent(b.remaining)+L" 剩余",w-120,row,96,19,INK,11,FW_MEDIUM,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        progress(dc,22,row+20,w-44,6,b.remaining,stale(v));
        text(dc,reset(b),22,row+28,w-44,16,MUTED,10);
        row+=45;
    }
    text(dc,L"重置卡",22,row,90,20,INK,11,FW_MEDIUM);
    text(dc,v.snapshot.creditCount?std::to_wstring(*v.snapshot.creditCount)+L" 张可用":L"未提供",
        w-130,row,106,20,MUTED,11,FW_NORMAL,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    row+=24;
    if(v.snapshot.credits.empty()) {
        std::wstring message;
        if(!v.snapshot.creditCount)message=L"服务未返回重置卡信息";
        else if(*v.snapshot.creditCount==0)message=L"没有可用重置卡";
        else message=L"未返回卡片明细";
        text(dc,message,22,row,w-44,18,MUTED,10);row+=20;
    }
    for(size_t i=0;i<v.snapshot.credits.size();++i) {
        const auto& c=v.snapshot.credits[i];
        auto label=L"卡片 "+std::to_wstring(i+1);
        if(!c.status.empty()&&c.status!="available")label+=L" · "+wide(c.status);
        text(dc,label,22,row,w-244,18,MUTED,10);
        text(dc,c.expires?date(c.expires)+L" 到期":L"到期时间未提供",w-218,row,194,18,MUTED,10,FW_NORMAL,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        row+=20;
    }
    if(incompleteCredits(v.snapshot)){text(dc,L"服务仅返回部分卡片明细",22,row,w-44,18,MUTED,10);row+=20;}
    if(v.failed)text(dc,wide(v.status)+(v.synced?L"（保留上次数据）":L""),22,row+2,w-44,34,LOW,10,FW_NORMAL,DT_LEFT|DT_WORDBREAK);
    usageChart(dc,v,usageChartTop(v,y),w);
}
void dockedPaint(HDC dc,int width,int height) {
    const bool horizontal=settings.dockSide==3||settings.dockSide==4;
    const int count=static_cast<int>(views.size());
    if(count==0) {
        ring(dc,width/2,height/2-8,22,{},false);
        text(dc,L"—",width/2-20,height/2+18,40,20,MUTED,11,FW_MEDIUM,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        return;
    }
    for(int i=0;i<count;++i) {
        const auto& v=views[i];auto main=mainWindow(v.snapshot);auto week=weekWindow(v.snapshot);
        int cx=horizontal?42+i*84-scrollY:width/2;
        int cy=horizontal?34:34+i*84-scrollY;
        if((horizontal&&(cx+36<0||cx-36>width))||(!horizontal&&(cy+50<0||cy-30>height)))continue;
        const bool hasMain=main&&!main->weekly();
        if(hasMain&&week) {
            ringColor(dc,cx,cy,27,week->remaining,stale(v),ACCENT);
            ringColor(dc,cx,cy,19,main->remaining,stale(v),WARM);
        } else if(week) {
            ringColor(dc,cx,cy,24,week->remaining,stale(v),ACCENT);
        } else {
            ringColor(dc,cx,cy,24,hasMain?main->remaining:std::optional<double>{},stale(v),WARM);
        }
        avatar(dc,cx,cy,9,avatarText(v.profile));
        text(dc,percent(hasMain?main->remaining:(week?week->remaining:std::optional<double>{})),cx-30,cy+32,60,18,
            v.failed?LOW:INK,11,FW_MEDIUM,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(v.paused||v.failed||(main&&main->remaining&&*main->remaining<20)||(week&&week->remaining&&*week->remaining<20))
            text(dc,v.paused?L"Ⅱ":L"!",cx+24,cy-30,12,16,v.paused?MUTED:LOW,10,FW_BOLD);
    }
    if(totalContent>viewContent) {
        const int length=std::max(16,viewContent*viewContent/totalContent);
        const int offset=(viewContent-length)*scrollY/std::max(1,totalContent-viewContent);
        if(horizontal)box(dc,offset,height-3,length,2,ACCENT,2);
        else box(dc,width-3,offset,2,length,ACCENT,2);
    }
}
void paint(HDC dc) {
    RECT r{};GetClientRect(window,&r);int width=logical(r.right);
    HDC mem=CreateCompatibleDC(dc);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=r.right;
    info.bmiHeader.biHeight=-r.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(!mem||!bitmap){if(mem)DeleteDC(mem);if(bitmap)DeleteObject(bitmap);return;}
    auto ob=SelectObject(mem,bitmap);
    auto present=[&]{
        GdiFlush();
        auto data=static_cast<uint32_t*>(pixels);
        for(int i=0;i<r.right*r.bottom;++i)data[i]=surfacePixel(data[i]);
        BitBlt(dc,0,0,r.right,r.bottom,mem,0,0,SRCCOPY);
        SelectObject(mem,ob);DeleteObject(bitmap);DeleteDC(mem);
    };
    box(mem,0,0,width,logical(r.bottom),BG);
    if(settings.docked) {
        dockedPaint(mem,width,logical(r.bottom));
        present();return;
    }
    text(mem,demo?L"Codex Meter · 演示":L"Codex Meter",12,3,width-88,28,INK,13,FW_MEDIUM);
    int save=SaveDC(mem);IntersectClipRect(mem,0,px(HEADER),r.right,px(HEADER+viewContent));
    if(views.empty()) {
        text(mem,L"尚未添加账号",12,HEADER+12,width-24,22,INK,13,FW_MEDIUM);
        text(mem,findCodex(settings).empty()?L"未找到 Codex，请在“设置”中选择已安装的程序。":L"已找到 Codex。添加账号，或右键关联本机登录。",12,HEADER+42,width-24,44,MUTED,11,FW_NORMAL,DT_WORDBREAK);
    } else {
        int y=HEADER+4-scrollY;
        for(size_t index=0;index<views.size();++index) {
            const auto& v=views[index];
            int h=expanded?cardHeight(v)+DETAIL_GAP:50;
            if(y+h>=HEADER&&y<=HEADER+viewContent) {
                if(expanded)detail(mem,v,y,width-4,static_cast<int>(index));else compact(mem,v,y,width);
            }
            y+=h;
        }
    }
    RestoreDC(mem,save);
    if(totalContent>viewContent){
        int h=std::max(18,viewContent*viewContent/totalContent);
        int pos=HEADER+(viewContent-h)*scrollY/std::max(1,totalContent-viewContent);
        box(mem,width-4,pos,3,h,LINE,3);
    }
    present();
}
HICON appIcon(int width,int height) {
    return reinterpret_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(IDI_CODEX_METER),
        IMAGE_ICON,width,height,LR_SHARED));
}
void trayAdd(){
    tray.cbSize=sizeof(tray);tray.hWnd=window;tray.uID=1;tray.uCallbackMessage=WM_TRAY;
    tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;
    tray.hIcon=appIcon(GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON));
    lstrcpynW(tray.szTip,L"Codex Meter · 单击显示/隐藏，右键菜单",128);Shell_NotifyIconW(NIM_ADD,&tray);
}
void command(int cmd);
struct NameEdit { std::wstring value; };
INT_PTR CALLBACK nameDialog(HWND dialog,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_INITDIALOG) {
        auto data=reinterpret_cast<NameEdit*>(lp);
        SetWindowLongPtrW(dialog,DWLP_USER,lp);
        SetWindowTextW(dialog,L"设置显示名称");
        auto control=[&](const wchar_t* cls,const wchar_t* label,DWORD style,int x,int y,int w,int h,int id) {
            RECT r{x,y,x+w,y+h};MapDialogRect(dialog,&r);
            auto child=CreateWindowExW(cls==std::wstring(L"EDIT")?WS_EX_CLIENTEDGE:0,cls,label,
                WS_CHILD|WS_VISIBLE|style,r.left,r.top,r.right-r.left,r.bottom-r.top,dialog,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
            SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font(12)),TRUE);return child;
        };
        control(L"STATIC",L"仅在本应用使用；留空恢复自动名称。",0,10,8,220,16,0);
        auto edit=control(L"EDIT",data->value.c_str(),WS_TABSTOP|ES_AUTOHSCROLL,10,28,220,20,10);
        SendMessageW(edit,EM_SETLIMITTEXT,32,0);
        control(L"BUTTON",L"保存",WS_TABSTOP|BS_DEFPUSHBUTTON,114,58,54,22,IDOK);
        control(L"BUTTON",L"取消",WS_TABSTOP,176,58,54,22,IDCANCEL);
        RECT owner{},rect{};GetWindowRect(window,&owner);GetWindowRect(dialog,&rect);
        MONITORINFO info{sizeof(info)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&info);
        const int w=rect.right-rect.left,h=rect.bottom-rect.top;
        SetWindowPos(dialog,HWND_TOP,std::clamp<LONG>(owner.left,info.rcWork.left,std::max(info.rcWork.left,info.rcWork.right-w)),
            std::clamp<LONG>(owner.top,info.rcWork.top,std::max(info.rcWork.top,info.rcWork.bottom-h)),0,0,SWP_NOSIZE);
        SetFocus(edit);SendMessageW(edit,EM_SETSEL,0,-1);return FALSE;
    }
    if(msg==WM_COMMAND) {
        if(LOWORD(wp)==IDCANCEL){EndDialog(dialog,IDCANCEL);return TRUE;}
        if(LOWORD(wp)==IDOK) {
            wchar_t buffer[33]{};GetDlgItemTextW(dialog,10,buffer,33);
            auto value=utf8(buffer);
            if(value.find('@')!=std::string::npos||value.find("mailto:")!=std::string::npos) {
                MessageBoxW(dialog,L"请输入昵称，不要填写邮箱。",L"设置显示名称",MB_OK);return TRUE;
            }
            auto data=reinterpret_cast<NameEdit*>(GetWindowLongPtrW(dialog,DWLP_USER));
            data->value=wide(cleanDisplayName(value));EndDialog(dialog,IDOK);return TRUE;
        }
    }
    if(msg==WM_CLOSE){EndDialog(dialog,IDCANCEL);return TRUE;}
    return FALSE;
}
void renameProfile(size_t index) {
    const auto id=views[index].profile.id;
    NameEdit data{wide(views[index].profile.alias.empty()?views[index].profile.displayName:views[index].profile.alias)};
    struct Template { DLGTEMPLATE dialog; WORD menu=0,windowClass=0,title=0; } spec{};
    spec.dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME;
    spec.dialog.cx=240;spec.dialog.cy=90;
    if(DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&spec.dialog,window,nameDialog,reinterpret_cast<LPARAM>(&data))==IDOK) {
        auto it=std::find_if(views.begin(),views.end(),[&](const View& v){return v.profile.id==id;});
        if(it!=views.end()){it->profile.alias=utf8(data.value);persist();InvalidateRect(window,nullptr,FALSE);}
    }
}
HWND dialogControl(HWND dialog,const wchar_t* cls,const wchar_t* label,DWORD style,int x,int y,int w,int h,int id) {
    RECT r{x,y,x+w,y+h};MapDialogRect(dialog,&r);
    auto child=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|style,r.left,r.top,r.right-r.left,r.bottom-r.top,
        dialog,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font(12)),TRUE);return child;
}
INT_PTR CALLBACK preferencesDialog(HWND dialog,UINT msg,WPARAM wp,LPARAM) {
    if(msg==WM_INITDIALOG) {
        SetWindowTextW(dialog,L"Codex Meter 设置");
        auto checkbox=[&](int id,const wchar_t* label,int y,bool checked){
            auto child=dialogControl(dialog,L"BUTTON",label,WS_TABSTOP|BS_AUTOCHECKBOX,12,y,264,20,id);
            SendMessageW(child,BM_SETCHECK,checked?BST_CHECKED:BST_UNCHECKED,0);
        };
        checkbox(20,L"始终置顶",10,settings.topmost);
        checkbox(21,L"磨砂背景（不支持时使用实色）",34,settings.glass);
        checkbox(22,L"在任务栏显示",58,settings.taskbar);
        checkbox(23,L"红色关闭按钮隐藏到托盘",82,settings.closeToTray);
        checkbox(24,L"启动时展开详情",106,settings.startExpanded);
        dialogControl(dialog,L"STATIC",L"自动刷新间隔",0,12,136,100,20,0);
        auto combo=dialogControl(dialog,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,120,134,156,90,25);
        for(auto value:{L"30 秒",L"60 秒",L"5 分钟"})SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(value));
        SendMessageW(combo,CB_SETCURSEL,settings.interval==30?0:settings.interval==300?2:1,0);
        dialogControl(dialog,L"STATIC",findCodex(settings).empty()?L"Codex：未找到。请先安装并选择程序。":L"Codex：已找到，连接时检查接口兼容性。",0,12,164,264,26,26);
        dialogControl(dialog,L"BUTTON",L"选择 Codex 程序…",WS_TABSTOP,12,196,130,24,ChooseExe);
        dialogControl(dialog,L"BUTTON",L"保存",WS_TABSTOP|BS_DEFPUSHBUTTON,154,196,58,24,IDOK);
        dialogControl(dialog,L"BUTTON",L"取消",WS_TABSTOP,218,196,58,24,IDCANCEL);
        RECT owner{},r{};GetWindowRect(window,&owner);GetWindowRect(dialog,&r);
        MONITORINFO info{sizeof(info)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&info);
        SetWindowPos(dialog,HWND_TOP,std::clamp<LONG>(owner.left,info.rcWork.left,std::max(info.rcWork.left,info.rcWork.right-(r.right-r.left))),
            std::clamp<LONG>(owner.top,info.rcWork.top,std::max(info.rcWork.top,info.rcWork.bottom-(r.bottom-r.top))),0,0,SWP_NOSIZE);
        return TRUE;
    }
    if(msg==WM_COMMAND) {
        if(LOWORD(wp)==ChooseExe){chooseExe();SetDlgItemTextW(dialog,26,findCodex(settings).empty()?L"Codex：未找到":L"Codex：已找到，连接时检查接口兼容性。");return TRUE;}
        if(LOWORD(wp)==IDCANCEL){EndDialog(dialog,IDCANCEL);return TRUE;}
        if(LOWORD(wp)==IDOK) {
            auto checked=[&](int id){return SendDlgItemMessageW(dialog,id,BM_GETCHECK,0,0)==BST_CHECKED;};
            settings.topmost=checked(20);settings.glass=checked(21);settings.taskbar=checked(22);
            settings.closeToTray=checked(23);settings.startExpanded=checked(24);
            auto choice=SendDlgItemMessageW(dialog,25,CB_GETCURSEL,0,0);settings.interval=choice==0?30:choice==2?300:60;
            for(auto& v:views)if(!v.paused)v.next=Clock::now()+std::chrono::seconds(settings.interval);
            persist();applyStyle();EndDialog(dialog,IDOK);return TRUE;
        }
    }
    if(msg==WM_CLOSE){EndDialog(dialog,IDCANCEL);return TRUE;}
    return FALSE;
}
void preferences(){
    struct Template { DLGTEMPLATE dialog; WORD menu=0,windowClass=0,title=0; } spec{};
    spec.dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME;spec.dialog.cx=288;spec.dialog.cy=232;
    DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&spec.dialog,window,preferencesDialog,0);
}
int accountAt(int x,int y){
    if(settings.docked) {
        const int n=((settings.dockSide>=3?x:y)+scrollY)/84;
        return n>=0&&n<static_cast<int>(views.size())?n:-1;
    }
    if(y<HEADER||y>=HEADER+viewContent)return -1;
    int top=HEADER+4-scrollY;
    for(size_t i=0;i<views.size();++i){int h=expanded?cardHeight(views[i]):46;if(y>=top&&y<top+h)return static_cast<int>(i);top+=expanded?h+DETAIL_GAP:50;}
    return -1;
}
int removeAt(int x,int y){
    if(!expanded||settings.docked)return -1;
    RECT client{};GetClientRect(window,&client);int top=HEADER+4-scrollY;
    for(size_t i=0;i<views.size();++i){if(removeHit(x,y,logical(client.right),top,HEADER,HEADER+viewContent))return static_cast<int>(i);top+=cardHeight(views[i])+DETAIL_GAP;}
    return -1;
}
void menu(POINT screen,int selected=-1) {
    HMENU m=CreatePopupMenu();
    auto item=[&](UINT id,const wchar_t* label,bool checked=false){AppendMenuW(m,MF_STRING|(checked?MF_CHECKED:0),id,label);};
    item(Expand,expanded?L"收起详情":L"展开详情");item(Refresh,L"刷新全部  F5");
    item(Add,L"添加账号…");item(LinkLocal,L"关联本机 Codex 登录");
    if(selected>=0&&selected<static_cast<int>(views.size())){
        AppendMenuW(m,MF_SEPARATOR,0,nullptr);
        std::wstring label=wide(views[selected].profile.name);if(label.size()>80)label.resize(80);
        AppendMenuW(m,MF_STRING|MF_DISABLED,0,label.c_str());
        item(ProfileRefresh+selected,L"刷新此账号");
        item(ProfileRename+selected,L"设置显示名称…");
        if(!views[selected].profile.local)item(ProfileLogin+selected,L"登录 / 重新登录此账号");
        item(ProfileRemove+selected,views[selected].profile.local?L"解除此账号监测":L"退出登录并移除此账号");
    }
    AppendMenuW(m,MF_SEPARATOR,0,nullptr);
    const int active=worker?worker->activeKind():0;
    AppendMenuW(m,MF_STRING|(active?0:MF_GRAYED),Cancel,active==2?L"停止当前登录":L"停止当前同步");
    item(Preferences,L"设置…");
    item(DockToggle,settings.docked?L"退出边缘收纳":L"边缘收纳模式");
    AppendMenuW(m,MF_SEPARATOR,0,nullptr);item(About,L"关于与帮助");item(Hide,L"隐藏到托盘");item(Quit,L"退出");
    SetForegroundWindow(window);
    int result=TrackPopupMenu(m,TPM_RETURNCMD|TPM_RIGHTBUTTON,screen.x,screen.y,0,window,nullptr);
    DestroyMenu(m);PostMessageW(window,WM_NULL,0,0);if(result)command(result);
}
void command(int cmd){
    if(cmd>=ProfileRename&&cmd<ProfileRename+static_cast<int>(views.size())){renameProfile(cmd-ProfileRename);return;}
    if(cmd>=ProfileRemove&&cmd<ProfileRemove+static_cast<int>(views.size())){
        int i=cmd-ProfileRemove;if(demo||views[i].busy)return;
        if(MessageBoxW(window,L"移除此监测账号？独立账号将退出其 Codex 登录；本机 Codex 登录不受影响。",
             L"移除账号",MB_OKCANCEL|MB_ICONQUESTION)!=IDOK)return;
        if(views[i].profile.local){removeUsageCache(dataDir,views[i].profile.id);views.erase(views.begin()+i);persist();layout();}
        else queue(views[i],Action::Remove);
        return;
    }
    if(cmd>=ProfileLogin&&cmd<ProfileLogin+static_cast<int>(views.size())){queue(views[cmd-ProfileLogin],Action::Login);return;}
    if(cmd>=ProfileRefresh&&cmd<ProfileRefresh+static_cast<int>(views.size())){auto& v=views[cmd-ProfileRefresh];v.usageChecked=0;queue(v);return;}
    if(cmd==DockToggle) {
        if(settings.docked) {
            undock();
        } else {
            RECT r{};GetWindowRect(window,&r);HMONITOR monitor=MonitorFromRect(&r,MONITOR_DEFAULTTOPRIMARY);
            MONITORINFO info{sizeof(info)};GetMonitorInfoW(monitor,&info);
            dock(dockSideFor(r,info.rcWork,0,false),r);
        }
        return;
    }
    switch(cmd){
    case Expand:toggle();break;
    case WindowClose:if(settings.closeToTray)ShowWindow(window,SW_HIDE);else SendMessageW(window,WM_CLOSE,0,0);break;
    case WindowCollapse:if(expanded){expanded=false;scrollY=0;layout();}break;
    case WindowExpand:if(!expanded){expanded=true;scrollY=0;layout();if(!demo)refreshAll();}break;
    case Refresh:refreshAll();break;
    case Add:addAccount();break;
    case LinkLocal:linkLocal();break;
    case Cancel:if(worker)worker->cancelCurrent();break;
    case ChooseExe:chooseExe();break;
    case Preferences:preferences();break;
    case Hide:ShowWindow(window,SW_HIDE);break;
    case Quit:SendMessageW(window,WM_CLOSE,0,0);break;
    case Topmost:settings.topmost=!settings.topmost;applyStyle();persist();break;
    case Interval30:case Interval60:case Interval300:
        settings.interval=cmd==Interval30?30:cmd==Interval60?60:300;
        for(auto& v:views)v.next=Clock::now()+std::chrono::seconds(settings.interval);
        persist();break;
    case Opaque:case Translucent:settings.glass=cmd!=Opaque;applyStyle();persist();break;
    case About:
        MessageBoxW(window,L"Codex Meter " METER_VERSION_W L" · Windows 原生版\n\n"
          L"拖动顶栏移动；右上角展开/收起；滚轮浏览账号。\n"
          L"右键账号可刷新、设置昵称、重新登录和移除。F5 刷新，Esc 收起。\n"
          L"右键 → 设置：磨砂背景、任务栏、关闭行为及刷新间隔。\n\n"
          L"将窗口拖到屏幕边缘可进入圆环收纳；点击圆环或右键菜单可恢复。\n\n"
          L"独立账号通过官方 Codex 浏览器登录；凭据由 Codex 保存到 Windows 凭据库。\n"
          L"需要已安装的 codex.exe。默认不会上传日志或发送模型请求。\n"
          L"登录、真实额度和重置卡可用性取决于官方接口与账号。\n\n"
          L"本应用为社区工具，与 OpenAI 无隶属关系。",L"Codex Meter",MB_OK);break;
    }
}
void results(){
    for(auto& e:worker->take()) {
        auto it=std::find_if(views.begin(),views.end(),[&](const View& v){return v.profile.id==e.id;});
        if(it==views.end())continue;
        if(e.removed){removeUsageCache(dataDir,it->profile.id);views.erase(it);persist();continue;}
        it->status=e.status;
        if(e.data){
            if(it->profile.name!=e.data->name){it->usage={};it->usageChecked=0;it->usageUpdated=0;removeUsageCache(dataDir,it->profile.id);}
            const bool metadataChanged=it->profile.name!=e.data->name||it->profile.plan!=e.data->plan||it->profile.displayName!=e.data->displayName;
            it->profile.name=e.data->name;it->profile.plan=e.data->plan;it->profile.avatar=e.data->avatar;
            it->profile.displayName=e.data->displayName;
            it->snapshot=std::move(e.data->snapshot);
            if(e.data->usage){
                it->usageChecked=std::time(nullptr);
                if(e.data->usage->available){
                    it->usage=std::move(*e.data->usage);it->usageUpdated=it->usageChecked;
                    try{saveUsageCache(dataDir,it->profile,it->usage,it->usageUpdated);}
                    catch(...){it->usage.message=u8"用量已更新；本地缓存未能保存";}
                }
                else it->usage.message=it->usage.available?u8"用量更新失败，保留上次数据":e.data->usage->message;
            }
            it->synced=std::time(nullptr);it->failed=false;it->failures=0;if(metadataChanged)persist();
        }
        if(e.finished){
            it->busy=false;
            it->paused=e.cancelled;
            if(e.cancelled){it->failed=false;it->failures=0;it->next=Clock::time_point::max();continue;}
            if(!e.data){it->failed=true;++it->failures;}
            auto seconds=std::min(900,settings.interval*(1<<std::min(it->failures,3)));
            it->next=Clock::now()+std::chrono::seconds(seconds);
        }
    }
    if(!dragging)layout();else InvalidateRect(window,nullptr,FALSE);
}
// GDI child controls need explicit alpha on the extended DWM frame too.
class ButtonSurface {
    HDC target;HBITMAP bitmap;HGDIOBJ old;void* pixels=nullptr;int width,height;
public:
    HDC dc;
    explicit ButtonSurface(const DRAWITEMSTRUCT& item):target(item.hDC),width(item.rcItem.right),height(item.rcItem.bottom) {
        dc=CreateCompatibleDC(target);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
        bitmap=CreateDIBSection(target,&info,DIB_RGB_COLORS,&pixels,nullptr,0);old=SelectObject(dc,bitmap);
        box(dc,0,0,logical(width),logical(height),BG);
    }
    ~ButtonSurface(){
        GdiFlush();if(pixels)for(int i=0;i<width*height;++i){auto& pixel=static_cast<uint32_t*>(pixels)[i];pixel=surfacePixel(pixel);}
        BitBlt(target,0,0,width,height,dc,0,0,SRCCOPY);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
    }
};
LRESULT CALLBACK buttonProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
    const int id=GetDlgCtrlID(hwnd);
    if(msg==WM_MOUSEMOVE) {
        if(hotButton!=id){hotButton=id;InvalidateRect(hwnd,nullptr,FALSE);}
        TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,hwnd,0};TrackMouseEvent(&tracking);
    } else if(msg==WM_MOUSELEAVE) {
        if(hotButton==id)hotButton=0;InvalidateRect(hwnd,nullptr,FALSE);
    } else if(msg==WM_ERASEBKGND)return 1;
    return DefSubclassProc(hwnd,msg,wp,lp);
}
LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==taskbarCreated&&window){trayAdd();return 0;}
    switch(msg){
    case WM_GETMINMAXINFO:{
        auto limits=reinterpret_cast<MINMAXINFO*>(lp);limits->ptMinTrackSize={px(40),px(40)};return 0;
    }
    case WM_NCCALCSIZE:if(wp)return 0;break;
    case WM_NCHITTEST:return HTCLIENT;
    case WM_CREATE:{
        window=hwnd;scale=GetDpiForWindow(hwnd)/96.0;
        auto button=[&](int id,const wchar_t* label){auto child=CreateWindowExW(0,L"BUTTON",label,
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,10,10,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),nullptr,nullptr);
            SetWindowSubclass(child,buttonProc,1,0);return child;};
        closeButton=button(WindowClose,L"关闭");
        collapseButton=button(WindowCollapse,L"收起");
        expandButton=button(WindowExpand,L"展开");
        addButton=button(Add,L"添加账号");refreshButton=button(Refresh,L"刷新");
        tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,
            CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,hwnd,nullptr,GetModuleHandleW(nullptr),nullptr);
        SendMessageW(tooltip,TTM_SETMAXTIPWIDTH,0,px(330));
        TOOLINFOW tip{sizeof(tip)};tip.uFlags=TTF_SUBCLASS;tip.hwnd=hwnd;tip.uId=1;tip.lpszText=LPSTR_TEXTCALLBACKW;
        tip.rect={0,0,32767,32767};SendMessageW(tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tip));
        for(HWND child:{closeButton,collapseButton,expandButton,addButton,refreshButton}) {
            tip.uFlags=TTF_SUBCLASS|TTF_IDISHWND;tip.uId=reinterpret_cast<UINT_PTR>(child);
            SendMessageW(tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tip));
        }
        SetTimer(hwnd,1,1000,nullptr);trayAdd();return 0;
    }
    case WM_COMMAND:command(LOWORD(wp));return 0;
    case WM_DRAWITEM:{
        auto original=reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(original->CtlType!=ODT_BUTTON)break;
        ButtonSurface surface(*original);auto drawing=*original;drawing.hDC=surface.dc;auto d=&drawing;
        int w=logical(d->rcItem.right-d->rcItem.left),h=logical(d->rcItem.bottom-d->rcItem.top);
        if(d->CtlID==WindowClose||d->CtlID==WindowCollapse||d->CtlID==WindowExpand) {
            COLORREF color=d->CtlID==WindowClose?TRAFFIC_RED:d->CtlID==WindowCollapse?TRAFFIC_YELLOW:TRAFFIC_GREEN;
            paintSurfaceShape(d->hDC,px(1),px(1),px(16),px(16),0,color,CLR_INVALID,BG,glassActive,true);
            if(d->itemState&ODS_SELECTED) {
                auto shade=CreatePen(PS_SOLID,1,INK);
                auto old=SelectObject(d->hDC,shade);SetBkMode(d->hDC,TRANSPARENT);
                if(d->CtlID==WindowClose) {
                    MoveToEx(d->hDC,px(6),px(6),nullptr);LineTo(d->hDC,px(12),px(12));
                    MoveToEx(d->hDC,px(12),px(6),nullptr);LineTo(d->hDC,px(6),px(12));
                } else if(d->CtlID==WindowCollapse) {
                    MoveToEx(d->hDC,px(5),px(9),nullptr);LineTo(d->hDC,px(13),px(9));
                } else {
                    MoveToEx(d->hDC,px(5),px(9),nullptr);LineTo(d->hDC,px(13),px(9));
                    MoveToEx(d->hDC,px(9),px(5),nullptr);LineTo(d->hDC,px(9),px(13));
                }
                SelectObject(d->hDC,old);DeleteObject(shade);
            }
            return TRUE;
        }
        const auto fill=(d->itemState&ODS_SELECTED)?RGB(201,185,222):hotButton==static_cast<int>(d->CtlID)?RGB(212,197,230):BUTTON;
        box(d->hDC,0,0,w,h,fill,12);
        if((d->itemState&ODS_FOCUS)&&!(d->itemState&ODS_NOFOCUSRECT))
            box(d->hDC,2,2,w-4,h-4,fill,8,ACCENT);
        wchar_t title[64]{};GetWindowTextW(d->hwndItem,title,64);
        text(d->hDC,title,0,0,w,h,INK,12,FW_NORMAL,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        return TRUE;
    }
    case WM_RESULTS:if(worker)results();return 0;
    case WM_NOTIFY:{
        auto header=reinterpret_cast<NMHDR*>(lp);
        if(header->hwndFrom==tooltip&&header->code==TTN_GETDISPINFOW){
            auto tip=reinterpret_cast<NMTTDISPINFOW*>(lp);tooltipText.clear();
            if(header->idFrom!=1) {
                wchar_t title[64]{};GetWindowTextW(reinterpret_cast<HWND>(header->idFrom),title,64);tooltipText=title;
            } else {
                POINT p{};GetCursorPos(&p);ScreenToClient(hwnd,&p);int index=accountAt(logical(p.x),logical(p.y));
                if(index>=0) {
                    const auto& v=views[index];tooltipText=compactName(v.profile)+L"\n默认文字头像";
                    for(const auto& q:v.snapshot.windows)tooltipText+=L"\n"+duration(q)+L"："+percent(q.remaining)+L" · "+reset(q);
                    if(!v.status.empty())tooltipText+=L"\n"+wide(v.status);
                    if(settings.docked)tooltipText+=L"\n内圈米黄：短周期；外圈淡紫：周额度\n点击展开，滚轮切换账号";
                    if(expanded&&!settings.docked) {
                        int cardTop=HEADER+4-scrollY;for(int i=0;i<index;++i)cardTop+=cardHeight(views[i])+DETAIL_GAP;
                        int chart=usageChartTop(v,cardTop),x=logical(p.x),y=logical(p.y);
                        RECT client{};GetClientRect(hwnd,&client);int step=(logical(client.right)-48)/7;
                        if(y>=chart+24&&y<chart+102&&x>=22&&x<22+step*7) {
                            int day=(x-22)/step;auto dates=usageDates();auto found=v.usage.days.find(dates[day]);
                            tooltipText=compactName(v.profile)+L"\n"+wide(dates[day])+L"（服务日期）\n"+
                                (found==v.usage.days.end()?L"无数据":std::to_wstring(found->second)+L" Tokens");
                            if(demo)tooltipText+=L"\n演示数据";
                        }
                    }
                } else tooltipText=L"拖动顶栏移动；右键打开设置";
            }
            tip->lpszText=tooltipText.data();return 0;
        }
        break;
    }
    case WM_TIMER:{
        auto now=Clock::now();
        static std::string lastUsageDay;auto today=usageDates().back();
        if(today!=lastUsageDay){lastUsageDay=today;for(auto& v:views)v.usageChecked=0;}
        if(!demo)for(auto& v:views)if(!v.paused&&!v.busy&&now>=v.next)queue(v);
        static time_t minute=0;auto m=std::time(nullptr)/60;
        if(m!=minute){minute=m;if(IsWindowVisible(hwnd))InvalidateRect(hwnd,nullptr,FALSE);}
        return 0;
    }
    case WM_POWERBROADCAST:
        if(wp==PBT_APMRESUMEAUTOMATIC)for(auto& v:views)if(!v.paused)queue(v);
        return TRUE;
    case WM_PAINT:{PAINTSTRUCT p{};auto dc=BeginPaint(hwnd,&p);paint(dc);EndPaint(hwnd,&p);return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_LBUTTONDOWN:{
        if(settings.docked){dockPressed=true;dockDragging=false;GetCursorPos(&dockPointerStart);GetWindowRect(hwnd,&dockWindowStart);SetCapture(hwnd);return 0;}
        int y=logical(GET_Y_LPARAM(lp)),x=logical(GET_X_LPARAM(lp));
        int hit=removeAt(x,y);
        if(hit>=0){pressedRemove=views[hit].profile.id;SetCapture(hwnd);return 0;}
        if(y<HEADER){ReleaseCapture();SendMessageW(hwnd,WM_NCLBUTTONDOWN,HTCAPTION,0);}return 0;
    }
    case WM_LBUTTONUP:{
        if(settings.docked){
            const bool clicked=dockPressed&&!dockDragging,moved=dockDragging;
            dockPressed=false;dockDragging=false;dragging=false;
            if(GetCapture()==hwnd)ReleaseCapture();
            if(clicked)undock();
            else if(moved){RECT r{};GetWindowRect(hwnd,&r);settings.x=r.left;settings.y=r.top;layout();persist();}
            return 0;
        }
        int x=logical(GET_X_LPARAM(lp)),y=logical(GET_Y_LPARAM(lp));
        const auto pressed=pressedRemove;pressedRemove.clear();if(GetCapture()==hwnd)ReleaseCapture();
        int hit=removeAt(x,y);
        if(hit>=0&&views[hit].profile.id==pressed)command(ProfileRemove+hit);
        return 0;
    }
    case WM_CAPTURECHANGED:
        pressedRemove.clear();dockPressed=false;
        if(dockDragging){dockDragging=false;dragging=false;RECT r{};GetWindowRect(hwnd,&r);settings.x=r.left;settings.y=r.top;layout();persist();}
        return 0;
    case WM_MOUSEMOVE:{
        if(settings.docked&&dockPressed&&GetCapture()==hwnd){
            POINT pointer{};GetCursorPos(&pointer);
            const int dx=pointer.x-dockPointerStart.x,dy=pointer.y-dockPointerStart.y;
            if(!dockDragging&&(std::abs(dx)>=GetSystemMetrics(SM_CXDRAG)||std::abs(dy)>=GetSystemMetrics(SM_CYDRAG))){
                dockDragging=true;dragging=true;SendMessageW(tooltip,TTM_POP,0,0);
            }
            if(dockDragging){
                MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromPoint(pointer,MONITOR_DEFAULTTONEAREST),&monitor);
                auto r=pinDock({dockWindowStart.left+dx,dockWindowStart.top+dy,dockWindowStart.right+dx,dockWindowStart.bottom+dy},
                    {monitor.rcWork.left,monitor.rcWork.top,monitor.rcWork.right,monitor.rcWork.bottom},settings.dockSide);
                SetWindowPos(hwnd,nullptr,r.left,r.top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
            }
            return 0;
        }
        int found=removeAt(logical(GET_X_LPARAM(lp)),logical(GET_Y_LPARAM(lp)));
        if(found!=hotRemove){hotRemove=found;InvalidateRect(hwnd,nullptr,FALSE);}
        TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,hwnd,0};TrackMouseEvent(&track);
        return 0;
    }
    case WM_MOUSELEAVE:
        if(hotRemove!=-1){hotRemove=-1;InvalidateRect(hwnd,nullptr,FALSE);}return 0;
    case WM_ENTERSIZEMOVE:dragging=true;return 0;
    case WM_MOVING:{
        if(!settings.docked&&!pendingDock) {
            auto r=reinterpret_cast<RECT*>(lp);MONITORINFO info{sizeof(info)};
            GetMonitorInfoW(MonitorFromRect(r,MONITOR_DEFAULTTONEAREST),&info);
            int side=dockSideFor(*r,info.rcWork,0,true);
            if(side){pendingDock=side;pendingDockRect=*r;PostMessageW(hwnd,WM_DOCK,0,0);}
        }
        return TRUE;
    }
    case WM_DOCK:{
        if(pendingDock&&!settings.docked){
            const int side=pendingDock;const auto rect=pendingDockRect;dragging=false;
            SendMessageW(hwnd,WM_CANCELMODE,0,0);ReleaseCapture();pendingDock=0;dock(side,rect);
        }
        pendingDock=0;return 0;
    }
    case WM_EXITSIZEMOVE:{
        dragging=false;if(pendingDock)return 0;
        RECT r{};GetWindowRect(hwnd,&r);
        if(!settings.docked) {
            HMONITOR monitor=MonitorFromRect(&r,MONITOR_DEFAULTTOPRIMARY);MONITORINFO info{sizeof(info)};GetMonitorInfoW(monitor,&info);
            const int side=dockSideFor(r,info.rcWork,0,true);
            if(side) {
                dock(side,r);return 0;
            }
        }
        settings.x=r.left;settings.y=r.top;persist();layout();return 0;
    }
    case WM_CONTEXTMENU:{
        POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        if(p.x==-1){GetCursorPos(&p);menu(p);return 0;}
        POINT client=p;ScreenToClient(hwnd,&client);menu(p,accountAt(logical(client.x),logical(client.y)));return 0;
    }
    case WM_MOUSEWHEEL:
        scrollY=std::clamp(scrollY-GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA*(settings.docked?84:50),0,std::max(0,totalContent-viewContent));
        InvalidateRect(hwnd,nullptr,FALSE);return 0;
    case WM_KEYDOWN:
        if(wp==VK_ESCAPE&&expanded)toggle();
        if(wp==VK_F5)refreshAll();
        if(wp=='N'&&(GetKeyState(VK_CONTROL)&0x8000))addAccount();
        if(wp==VK_NEXT||wp==VK_PRIOR){scrollY=std::clamp(scrollY+(wp==VK_NEXT?viewContent:-viewContent),0,std::max(0,totalContent-viewContent));InvalidateRect(hwnd,nullptr,FALSE);}
        return 0;
    case WM_DPICHANGED:{
        scale=HIWORD(wp)/96.0;clearFonts();
        auto r=reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOACTIVATE|SWP_NOZORDER);
        layout();return 0;
    }
    case WM_DISPLAYCHANGE:layout();return 0;
    case WM_DWMCOMPOSITIONCHANGED:case WM_THEMECHANGED:case WM_SETTINGCHANGE:applyStyle();return 0;
    case WM_TRAY:
        if(lp==WM_LBUTTONUP){if(IsWindowVisible(hwnd))ShowWindow(hwnd,SW_HIDE);else{layout();ShowWindow(hwnd,SW_SHOWNOACTIVATE);}}
        if(lp==WM_RBUTTONUP){POINT p{};GetCursorPos(&p);menu(p);}return 0;
    case WM_CLOSE:
        if(closing)return 0;closing=true;
        {RECT r{};GetWindowRect(hwnd,&r);settings.x=r.left;settings.y=r.top;persist();}
        KillTimer(hwnd,1);if(worker)worker->shutdown();DestroyWindow(hwnd);return 0;
    case WM_DESTROY:Shell_NotifyIconW(NIM_DELETE,&tray);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
void demoData(){
    for(int i=0;i<2;++i){
        View v;v.profile={"demo"+std::to_string(i),i==0?u8"示例账户 A":u8"示例账户 B","Plus",false};
        v.profile.displayName=v.profile.name;
        Json r={{"rateLimits",{{"limitId","codex"},{"primary",{{"usedPercent",i?62:28},{"windowDurationMins",300},{"resetsAt",std::time(nullptr)+7200}}},
            {"secondary",{{"usedPercent",i?39:16},{"windowDurationMins",10080},{"resetsAt",std::time(nullptr)+259200}}}}},
            {"rateLimitResetCredits",{{"availableCount",i?0:2},{"credits",Json::array()}}}};
        if(i==0)r["rateLimitResetCredits"]["credits"]={
            {{"id","demo-1"},{"status","available"},{"expiresAt",std::time(nullptr)+86400*4}},
            {{"id","demo-2"},{"status","available"},{"expiresAt",std::time(nullptr)+86400*10}}};
        v.snapshot=parseLimits(r);v.synced=std::time(nullptr);views.push_back(std::move(v));
        auto& sample=views.back();sample.usage.available=true;sample.usageChecked=std::time(nullptr);
        const int64_t tokens[2][7]={{142000,386000,214000,628000,492000,310000,458000},{58000,174000,92000,265000,0,138000,186000}};
        auto dates=usageDates();for(int day=0;day<7;++day)sample.usage.days[dates[day]]=tokens[i][day];
    }
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int) {
    SetCurrentProcessExplicitAppUserModelID(L"CodexMeter.Desktop");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    Gdiplus::GdiplusStartupInput graphicsInput;
    if(Gdiplus::GdiplusStartup(&graphicsToken,&graphicsInput,nullptr)!=Gdiplus::Ok)return 1;
    try{
        dataDir=defaultDataDir();bool local=false,initialExpanded=false,windowed=false;
        int argc=0;LPWSTR* args=CommandLineToArgvW(GetCommandLineW(),&argc);
        for(int i=1;i<argc;++i){
            std::wstring a=args[i];
            if(a==L"--demo")demo=true;
            else if(a==L"--live")local=true;
            else if(a==L"--expanded")initialExpanded=true;
            else if(a==L"--windowed")windowed=true;
            else if(a==L"--data-dir"&&i+1<argc)dataDir=fs::absolute(args[++i]);
        }
        LocalFree(args);
        fs::create_directories(dataDir);
        auto identity=fs::weakly_canonical(dataDir).wstring();
        std::transform(identity.begin(),identity.end(),identity.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});
        std::wstring mutexName=L"Local\\CodexMeter-"+std::to_wstring(std::hash<std::wstring>{}(identity))+(demo?L"-demo":L"");
        Handle singleton(CreateMutexW(nullptr,FALSE,mutexName.c_str()));
        if(!singleton)throw std::runtime_error("Cannot create instance lock");
        DWORD lockResult=WaitForSingleObject(singleton.get(),0);
        if(lockResult==WAIT_FAILED)throw std::runtime_error("Cannot acquire instance lock");
        if(lockResult==WAIT_TIMEOUT){
            HWND existing=nullptr;
            while((existing=FindWindowExW(nullptr,existing,L"CodexMeterV2",nullptr)))
                if(GetPropW(existing,mutexName.c_str())){ShowWindow(existing,SW_SHOWNORMAL);SetForegroundWindow(existing);break;}
            return 0;
        }
        if(!demo){
            try{settings=loadSettings(dataDir);}catch(...){
                MessageBoxW(nullptr,L"配置文件无法读取。为避免覆盖原配置，应用未启动。\n请备份并检查数据目录中的 settings.json。",L"Codex Meter",MB_ICONERROR);return 1;
            }
            for(auto& p:settings.profiles){View v;v.profile=p;
                if(auto cached=loadUsageCache(dataDir,p)){v.usage=std::move(cached->usage);v.usageUpdated=cached->updated;}
                views.push_back(std::move(v));}
        }else demoData();
        expanded=initialExpanded||settings.startExpanded;
        if(windowed)settings.taskbar=true;
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
        taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
        WNDCLASSEXW wc{sizeof(wc)};wc.hInstance=instance;wc.lpfnWndProc=proc;wc.lpszClassName=L"CodexMeterV2";
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        wc.hIcon=appIcon(GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON));
        wc.hIconSm=appIcon(GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON));
        if(!RegisterClassExW(&wc))throw std::runtime_error("Register window failed");
        window=CreateWindowExW(settings.taskbar?WS_EX_APPWINDOW:WS_EX_TOOLWINDOW,L"CodexMeterV2",demo?L"Codex Meter · 演示":L"Codex Meter",
            WS_POPUP|WS_THICKFRAME|WS_CAPTION,0,0,320,140,nullptr,nullptr,instance,nullptr);
        if(!window)throw std::runtime_error("Create window failed");
        SetPropW(window,mutexName.c_str(),reinterpret_cast<HANDLE>(1));
        worker=std::make_unique<Worker>(window,WM_RESULTS);
        layout(true);applyStyle();ShowWindow(window,SW_SHOWNOACTIVATE);UpdateWindow(window);
        if(local&&!demo)linkLocal();else if(!demo)refreshAll();
        MSG m{};int status;
        while((status=GetMessageW(&m,nullptr,0,0))>0){
            if(m.message==WM_KEYDOWN&&(m.wParam==VK_F5||m.wParam==VK_ESCAPE||m.wParam==VK_NEXT||m.wParam==VK_PRIOR||
                (m.wParam=='N'&&(GetKeyState(VK_CONTROL)&0x8000)))){
                SendMessageW(window,WM_KEYDOWN,m.wParam,m.lParam);continue;
            }
            if(!IsDialogMessageW(window,&m)){TranslateMessage(&m);DispatchMessageW(&m);}
        }
        worker.reset();clearFonts();Gdiplus::GdiplusShutdown(graphicsToken);CoUninitialize();return status<0?1:0;
    }catch(...){
        if(worker)worker->shutdown();
        MessageBoxW(nullptr,L"无法启动 Codex Meter。请检查数据目录权限和可用磁盘空间。",L"Codex Meter",MB_ICONERROR);return 1;
    }
}

