#pragma once
#include "ui_geometry.hpp"
#include <windows.h>
#include <optional>
namespace meter {
inline void drawQuotaRing(HDC dc,int cx,int cy,int radius,int thickness,
                          std::optional<double> remaining,COLORREF track,COLORREF color) {
    const int saved=SaveDC(dc);
    SelectObject(dc,GetStockObject(NULL_BRUSH));
    auto trackPen=CreatePen(PS_SOLID,thickness,track);
    auto originalPen=SelectObject(dc,trackPen);
    Ellipse(dc,cx-radius,cy-radius,cx+radius,cy+radius);
    SelectObject(dc,originalPen);DeleteObject(trackPen);
    const double sweep=remaining?quotaSweep(*remaining):0;
    if(sweep>0) {
        auto pen=CreatePen(PS_SOLID,thickness,color);SelectObject(dc,pen);
        if(sweep>=360)Ellipse(dc,cx-radius,cy-radius,cx+radius,cy+radius);
        else {MoveToEx(dc,cx,cy-radius,nullptr);AngleArc(dc,cx,cy,radius,90.0f,-static_cast<float>(sweep));}
        SelectObject(dc,originalPen);DeleteObject(pen);
    }
    RestoreDC(dc,saved);
}
}
