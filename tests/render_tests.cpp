#include "../src/platform.hpp"
#include "../src/ring_renderer.hpp"
#include "../src/surface_renderer.hpp"
#include "../src/text_renderer.hpp"
#include <iostream>
#define CHECK(c) do { ++checks; if (!(c)) throw std::runtime_error("Failed: " #c); } while(0)
int main() {
    int checks=0;
    ULONG_PTR graphicsToken=0;Gdiplus::GdiplusStartupInput graphicsInput;
    if(Gdiplus::GdiplusStartup(&graphicsToken,&graphicsInput,nullptr)!=Gdiplus::Ok)return 1;
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
        const COLORREF tint=RGB(243,248,248),red=RGB(238,99,88);
        const uint32_t tintRgb=0x00F3F8F8u,redRgb=0x00EE6358u;
        for(double scale:{1.0,1.25,1.5,2.0}) {
            std::fill_n(static_cast<uint32_t*>(bits),128*128,tintRgb);
            int size=static_cast<int>(16*scale);
            CHECK(meter::paintSurfaceShape(dc,8,8,size,size,0,red,CLR_INVALID,tint,true,true));
            auto pixels=static_cast<uint32_t*>(bits);
            CHECK(meter::resolveSurfacePixel(pixels[0],tint,true)>>24==110);
            CHECK(pixels[(8+size/2)*128+8+size/2]==(0xFF000000u|redRgb));
            bool partial=false,valid=true,noWhiteFringe=true;
            for(int i=0;i<128*128;++i) {
                auto pixel=meter::resolveSurfacePixel(pixels[i],tint,true);unsigned a=pixel>>24;
                partial|=a>110&&a<255;
                valid&=(pixel&255)<=a&&((pixel>>8)&255)<=a&&((pixel>>16)&255)<=a;
                if(a==255)noWhiteFringe&=(pixel&0xFFFFFFu)==redRgb;
            }
            CHECK(partial);CHECK(valid);CHECK(noWhiteFringe);
            CHECK(meter::resolveSurfacePixel(tintRgb,tint,false)==(0xFF000000u|tintRgb));
        }
        // Respect the parent's content clip; shapes must not leak into header/footer.
        std::fill_n(static_cast<uint32_t*>(bits),128*128,tintRgb);
        int saved=SaveDC(dc);IntersectClipRect(dc,20,20,40,40);
        CHECK(meter::paintSurfaceShape(dc,8,8,60,28,12,red,CLR_INVALID,tint,true));RestoreDC(dc,saved);
        CHECK(static_cast<uint32_t*>(bits)[25*128+19]==tintRgb);
        CHECK((static_cast<uint32_t*>(bits)[25*128+25]>>24)==255);
        for(double scale:{1.0,1.25,1.5,2.0}) {
            std::fill_n(static_cast<uint32_t*>(bits),128*128,tintRgb);
            HFONT label=CreateFontW(-static_cast<int>(12*scale),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
            CHECK(meter::paintSurfaceText(dc,L"Codex 字",4,4,120,40,label,RGB(24,23,68),DT_SINGLELINE,tint,true));
            DeleteObject(label);
            bool inkFound=false,partialFound=false,valid=true;
            for(int i=0;i<128*128;++i) {
                auto pixel=meter::resolveSurfacePixel(static_cast<uint32_t*>(bits)[i],tint,true);unsigned a=pixel>>24;
                inkFound|=a==255;partialFound|=a>110&&a<255;
                valid&=(pixel&255)<=a&&((pixel>>8)&255)<=a&&((pixel>>16)&255)<=a;
            }
            CHECK(inkFound);CHECK(partialFound);CHECK(valid);
            for(auto kind:{meter::TrafficGlyph::Close,meter::TrafficGlyph::Minimize,meter::TrafficGlyph::Expand}) {
                std::fill_n(static_cast<uint32_t*>(bits),128*128,0xFFEE6358u);
                const int size=static_cast<int>(18*scale);
                CHECK(meter::paintTrafficGlyph(dc,size,size,kind,scale,RGB(24,23,68),tint,true));
                long double weightedX=0,weightedY=0,total=0;bool opaque=true;
                for(int y=0;y<size;++y)for(int x=0;x<size;++x) {
                    auto pixel=static_cast<uint32_t*>(bits)[y*128+x];opaque&=(pixel>>24)==255;
                    const int darkness=238-static_cast<int>((pixel>>16)&255);
                    if(darkness>0){total+=darkness;weightedX+=(x+.5L)*darkness;weightedY+=(y+.5L)*darkness;}
                }
                CHECK(total>0&&opaque);
                CHECK(std::abs(weightedX/total-size*.5L)<.75L&&std::abs(weightedY/total-size*.5L)<.75L);
            }
        }
        std::cout<<checks<<" GDI and glass surface rendering checks passed\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<"\n";SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);Gdiplus::GdiplusShutdown(graphicsToken);return 1;}
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);Gdiplus::GdiplusShutdown(graphicsToken);return 0;
}
