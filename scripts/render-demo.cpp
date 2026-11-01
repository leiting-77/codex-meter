// Documentation export: reuse the application renderer at 200% scale.
// No account storage, RPC calls, visible windows or desktop capture.
#define wWinMain MeterUnusedEntry
#include "../src/main.cpp"
#undef wWinMain
#include <iostream>

void exportDemo(const fs::path& path,bool detailMode,int side) {
    expanded=detailMode;settings.docked=side!=0;settings.dockSide=side;
    totalContent=settings.docked?static_cast<int>(views.size())*84:contentHeight();
    viewContent=totalContent;scrollY=0;
    const int width=settings.docked?(side>=3?totalContent:84):(expanded?440:320);
    const int height=settings.docked?(side>=3?90:totalContent):HEADER+totalContent+(expanded?FOOTER:0);
    SetWindowPos(window,nullptr,0,0,px(width),px(height),SWP_NOZORDER|SWP_NOACTIVATE);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=px(width);info.bmiHeader.biHeight=-px(height);
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* pixels=nullptr;HDC dc=CreateCompatibleDC(nullptr);
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(!dc||!bitmap)throw std::runtime_error("Cannot create export surface");
    auto old=SelectObject(dc,bitmap);
    paint(dc);
    auto control=[&](HWND child,int id,int x,int y,int w,int h) {
        int saved=SaveDC(dc);SetViewportOrgEx(dc,px(x),px(y),nullptr);
        DRAWITEMSTRUCT item{};item.CtlType=ODT_BUTTON;item.CtlID=id;item.hwndItem=child;
        item.hDC=dc;item.rcItem={0,0,px(w),px(h)};
        proc(window,WM_DRAWITEM,0,reinterpret_cast<LPARAM>(&item));RestoreDC(dc,saved);
    };
    if(!settings.docked) {
        control(closeButton,WindowClose,width-75,9,18,18);
        control(collapseButton,WindowCollapse,width-51,9,18,18);
        control(expandButton,WindowExpand,width-27,9,18,18);
        if(expanded) {
            control(refreshButton,Refresh,10,height-29,60,24);
            control(addButton,Add,width-102,height-29,92,24);
        }
    }
    GdiFlush();
    CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
    Gdiplus::Bitmap output(px(width),px(height),px(width)*4,PixelFormat32bppARGB,static_cast<BYTE*>(pixels));
    if(output.Save(path.c_str(),&png,nullptr)!=Gdiplus::Ok)throw std::runtime_error("PNG export failed");
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
    std::wcout<<path.filename().wstring()<<L" "<<px(width)<<L"x"<<px(height)<<L"\n";
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc!=2)throw std::runtime_error("Usage: render-demo.exe output-directory");
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        Gdiplus::GdiplusStartupInput input;
        if(Gdiplus::GdiplusStartup(&graphicsToken,&input,nullptr)!=Gdiplus::Ok)throw std::runtime_error("GDI+ startup failed");
        scale=2;demo=true;glassActive=false;demoData();
        window=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!window)throw std::runtime_error("Cannot create render target");
        auto child=[&](const wchar_t* label){return CreateWindowExW(0,L"BUTTON",label,WS_CHILD,0,0,1,1,window,nullptr,nullptr,nullptr);};
        closeButton=child(L"关闭");collapseButton=child(L"收起");expandButton=child(L"展开");
        refreshButton=child(L"刷新");addButton=child(L"添加账号");
        fs::path output=fs::absolute(argv[1]);fs::create_directories(output);
        exportDemo(output/L"compact.png",false,0);
        exportDemo(output/L"details.png",true,0);
        exportDemo(output/L"edge-dock.png",false,3);
        exportDemo(output/L"edge-dock-vertical.png",false,2);
        DestroyWindow(window);clearFonts();Gdiplus::GdiplusShutdown(graphicsToken);CoUninitialize();
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
