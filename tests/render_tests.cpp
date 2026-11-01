#include "../src/platform.hpp"
#include "../src/ring_renderer.hpp"
#include <iostream>
#define CHECK(c) do { ++checks; if (!(c)) throw std::runtime_error("Failed: " #c); } while(0)
int main() {
    int checks=0;
    HDC dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=128;info.bmiHeader.biHeight=-128;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* bits=nullptr;auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);auto old=SelectObject(dc,bitmap);
    try {
        const COLORREF background=RGB(20,20,20),track=RGB(210,200,235),fill=RGB(240,210,130);
        auto backgroundBrush=CreateSolidBrush(background);
        RECT rect{0,0,128,128};
        for(int percent:{0,25,50,75,100}) {
            FillRect(dc,&rect,backgroundBrush);
            meter::drawQuotaRing(dc,64,64,40,5,percent,track,fill);
            // Quadrant midpoints avoid antialiasing and arc endpoint ambiguity.
            CHECK((GetPixel(dc,92,36)==fill)==(percent>=25));
            CHECK((GetPixel(dc,92,92)==fill)==(percent>=50));
            CHECK((GetPixel(dc,36,92)==fill)==(percent>=75));
            CHECK((GetPixel(dc,36,36)==fill)==(percent>=100));
            CHECK(GetPixel(dc,64,64)==background); // Full rings must never fill their center.
        }
        DeleteObject(backgroundBrush);
        std::cout<<checks<<" GDI ring rendering checks passed\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<"\n";SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return 1;}
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return 0;
}
