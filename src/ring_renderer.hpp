#pragma once
#include "ui_geometry.hpp"
#include "surface_renderer.hpp"
#include <windows.h>
#include <gdiplus.h>
#include <optional>
#include <vector>
namespace meter {
inline void drawQuotaRing(HDC dc,int cx,int cy,int radius,int thickness,
                          std::optional<double> remaining,COLORREF track,COLORREF color,
                          std::optional<COLORREF> gradientEnd={},COLORREF background=0,bool glass=false) {
    const int size=radius*2+4;
    constexpr int samples=3;
    const int sampleSize=size*samples;
    Gdiplus::Bitmap mask(sampleSize,sampleSize,PixelFormat32bppPARGB);
    {
        Gdiplus::Graphics graphics(&mask);
        graphics.Clear(Gdiplus::Color(0,0,0,0));
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.ScaleTransform(static_cast<float>(samples),static_cast<float>(samples));
        auto colorOf=[](COLORREF value){return Gdiplus::Color(255,GetRValue(value),GetGValue(value),GetBValue(value));};
        const float diameter=static_cast<float>(radius*2-thickness);
        const float inset=2.0f+thickness*.5f;
        const Gdiplus::RectF bounds(inset,inset,diameter,diameter);
        Gdiplus::Pen trackPen(colorOf(track),static_cast<float>(thickness));
        graphics.DrawEllipse(&trackPen,bounds);
        const double sweep=remaining?quotaSweep(*remaining):0;
        if(sweep>0) {
            Gdiplus::LinearGradientBrush gradient(
                Gdiplus::PointF(0,0),
                Gdiplus::PointF(static_cast<float>(size),static_cast<float>(size)),
                colorOf(color),colorOf(gradientEnd.value_or(color)));
            Gdiplus::Pen activePen(&gradient,static_cast<float>(thickness));
            activePen.SetStartCap(Gdiplus::LineCapRound);activePen.SetEndCap(Gdiplus::LineCapRound);
            activePen.SetLineJoin(Gdiplus::LineJoinRound);
            if(sweep>=360)graphics.DrawEllipse(&activePen,bounds);
            else graphics.DrawArc(&activePen,bounds,-90.0f,static_cast<float>(sweep));
        }
    }
    Gdiplus::Rect area(0,0,sampleSize,sampleSize);
    Gdiplus::BitmapData data{};
    if(mask.LockBits(&area,Gdiplus::ImageLockModeRead,PixelFormat32bppPARGB,&data)==Gdiplus::Ok) {
        std::vector<uint32_t> resolved(static_cast<size_t>(size)*size);
        const auto* pixels=static_cast<const BYTE*>(data.Scan0);
        // Resolve coverage in premultiplied space so transparent samples cannot add a fringe.
        for(int y=0;y<size;++y)for(int x=0;x<size;++x) {
            unsigned channels[4]{};
            for(int sy=0;sy<samples;++sy) {
                const auto* row=pixels+(y*samples+sy)*data.Stride+x*samples*4;
                for(int sx=0;sx<samples;++sx)for(int c=0;c<4;++c)channels[c]+=row[sx*4+c];
            }
            uint32_t pixel=0;
            for(int c=0;c<4;++c)pixel|=((channels[c]+4)/(samples*samples))<<(c*8);
            resolved[static_cast<size_t>(y)*size+x]=pixel;
        }
        mask.UnlockBits(&data);
        compositeSurfaceMask(dc,cx-radius-2,cy-radius-2,size,size,
            reinterpret_cast<const BYTE*>(resolved.data()),size*4,background,glass);
    }
}
}
