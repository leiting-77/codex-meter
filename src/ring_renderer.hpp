#pragma once
#include "ui_geometry.hpp"
#include <windows.h>
#include <gdiplus.h>
#include <optional>
namespace meter {
inline void drawQuotaRing(HDC dc,int cx,int cy,int radius,int thickness,
                          std::optional<double> remaining,COLORREF track,COLORREF color) {
    Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    auto colorOf=[](COLORREF value){return Gdiplus::Color(255,GetRValue(value),GetGValue(value),GetBValue(value));};
    const float diameter=static_cast<float>(radius*2-thickness);
    const Gdiplus::RectF bounds(static_cast<float>(cx-radius)+thickness*.5f,
        static_cast<float>(cy-radius)+thickness*.5f,diameter,diameter);
    Gdiplus::Pen trackPen(colorOf(track),static_cast<float>(thickness));
    graphics.DrawEllipse(&trackPen,bounds);
    const double sweep=remaining?quotaSweep(*remaining):0;
    if(sweep>0) {
        Gdiplus::Pen activePen(colorOf(color),static_cast<float>(thickness));
        activePen.SetStartCap(Gdiplus::LineCapRound);activePen.SetEndCap(Gdiplus::LineCapRound);
        activePen.SetLineJoin(Gdiplus::LineJoinRound);
        if(sweep>=360)graphics.DrawEllipse(&activePen,bounds);
        else graphics.DrawArc(&activePen,bounds,-90.0f,static_cast<float>(sweep));
    }
}
}
