#pragma once
#include "surface_renderer.hpp"
#include <string>
namespace meter {
inline bool paintSurfaceText(HDC dc,const std::wstring& text,int x,int y,int width,int height,
                             HFONT font,COLORREF ink,UINT flags,COLORREF background,bool glass) {
    if(width<=0||height<=0||text.empty())return true;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    HDC mask=CreateCompatibleDC(dc);if(!mask)return false;
    void* bits=nullptr;HBITMAP bitmap=CreateDIBSection(mask,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!bitmap){DeleteDC(mask);return false;}
    auto previous=SelectObject(mask,bitmap);auto previousFont=SelectObject(mask,font);
    std::fill_n(static_cast<uint32_t*>(bits),static_cast<size_t>(width)*height,0u);
    SetBkMode(mask,TRANSPARENT);SetTextColor(mask,RGB(255,255,255));
    RECT rect{0,0,width,height};DrawTextW(mask,text.c_str(),static_cast<int>(text.size()),&rect,flags|DT_NOPREFIX);
    GdiFlush();
    const bool result=compositeSurfaceMask(dc,x,y,width,height,static_cast<const BYTE*>(bits),width*4,background,glass,true,ink);
    SelectObject(mask,previousFont);SelectObject(mask,previous);DeleteObject(bitmap);DeleteDC(mask);
    return result;
}
enum class TrafficGlyph { Close,Minimize,Expand };
inline bool paintTrafficGlyph(HDC dc,int width,int height,TrafficGlyph kind,double scale,COLORREF ink,COLORREF background,bool glass) {
    if(width<=0||height<=0)return true;
    Gdiplus::Bitmap mask(width,height,PixelFormat32bppPARGB);
    {
        Gdiplus::Graphics graphics(&mask);graphics.Clear(Gdiplus::Color(0,0,0,0));
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const float cx=width*.5f,cy=height*.5f,s=static_cast<float>(scale);
        Gdiplus::Pen pen(Gdiplus::Color(230,GetRValue(ink),GetGValue(ink),GetBValue(ink)),1.25f*s);
        pen.SetStartCap(Gdiplus::LineCapRound);pen.SetEndCap(Gdiplus::LineCapRound);pen.SetLineJoin(Gdiplus::LineJoinRound);
        Gdiplus::GraphicsPath glyph;
        auto line=[&](float x1,float y1,float x2,float y2){glyph.StartFigure();glyph.AddLine(cx+x1*s,cy+y1*s,cx+x2*s,cy+y2*s);};
        if(kind==TrafficGlyph::Close){line(-2.7f,-2.7f,2.7f,2.7f);line(2.7f,-2.7f,-2.7f,2.7f);}
        else if(kind==TrafficGlyph::Minimize)line(-3.2f,0,3.2f,0);
        else {
            line(-3,3,3,-3);
            Gdiplus::PointF upper[]={{cx+.5f*s,cy-3*s},{cx+3*s,cy-3*s},{cx+3*s,cy-.5f*s}};
            Gdiplus::PointF lower[]={{cx-.5f*s,cy+3*s},{cx-3*s,cy+3*s},{cx-3*s,cy+.5f*s}};
            glyph.StartFigure();glyph.AddLines(upper,3);glyph.StartFigure();glyph.AddLines(lower,3);
        }
        graphics.DrawPath(&pen,&glyph);
    }
    Gdiplus::BitmapData data{};Gdiplus::Rect rect(0,0,width,height);
    if(mask.LockBits(&rect,Gdiplus::ImageLockModeRead,PixelFormat32bppPARGB,&data)!=Gdiplus::Ok)return false;
    const bool result=compositeSurfaceMask(dc,0,0,width,height,static_cast<const BYTE*>(data.Scan0),data.Stride,background,glass);
    mask.UnlockBits(&data);return result;
}
}
