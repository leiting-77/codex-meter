#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <cstdint>
#include <algorithm>
namespace meter {
inline uint32_t resolveSurfacePixel(uint32_t pixel,COLORREF background,bool glass) {
    // Shaped pixels already contain premultiplied alpha. GDI writes RGB with alpha=0.
    if(pixel>>24)return pixel;
    const uint32_t rgb=pixel&0xFFFFFFu;
    const uint32_t bg=((background&255u)<<16)|(background&0xFF00u)|((background>>16)&255u);
    const unsigned alpha=glass&&rgb==bg?110:255;
    return (alpha<<24)|((((rgb>>16)&255)*alpha/255)<<16)|((((rgb>>8)&255)*alpha/255)<<8)|((rgb&255)*alpha/255);
}
inline bool compositeSurfaceMask(HDC dc,int x,int y,int width,int height,const BYTE* sourceBits,int stride,
                                 COLORREF background,bool glass,bool grayscale=false,COLORREF ink=0) {
    if(width<=0||height<=0)return true;
    DIBSECTION target{};
    if(GetObjectW(GetCurrentObject(dc,OBJ_BITMAP),sizeof(target),&target)!=sizeof(target)||
       !target.dsBm.bmBits||target.dsBm.bmBitsPixel!=32)return false;
    // All application buffers are created top-down. GetObject normalizes biHeight
    // to a positive value, so its sign cannot be used to recover that orientation.
    GdiFlush();
    POINT origin{};GetViewportOrgEx(dc,&origin);RECT clip{};GetClipBox(dc,&clip);
    const int left=std::max<int>({0,clip.left+origin.x,x+origin.x}),top=std::max<int>({0,clip.top+origin.y,y+origin.y});
    const int right=std::min<int>({target.dsBm.bmWidth,clip.right+origin.x,x+origin.x+width});
    const int bottom=std::min<int>({target.dsBm.bmHeight,clip.bottom+origin.y,y+origin.y+height});
    for(int row=top;row<bottom;++row) {
        auto dest=reinterpret_cast<uint32_t*>(static_cast<BYTE*>(target.dsBm.bmBits)+row*target.dsBm.bmWidthBytes);
        auto src=reinterpret_cast<const uint32_t*>(sourceBits+(row-y-origin.y)*stride);
        for(int col=left;col<right;++col) {
            uint32_t source=src[col-x-origin.x];
            if(grayscale) {
                const unsigned coverage=source&255u;
                source=(coverage<<24)|((GetRValue(ink)*coverage/255)<<16)|((GetGValue(ink)*coverage/255)<<8)|(GetBValue(ink)*coverage/255);
            }
            const unsigned alpha=source>>24;if(!alpha)continue;
            if(alpha==255){dest[col]=source;continue;}
            const uint32_t prior=resolveSurfacePixel(dest[col],background,glass);const unsigned inverse=255-alpha;
            uint32_t out=0;for(unsigned shift:{0u,8u,16u,24u})
                out|=(((source>>shift)&255u)+(((prior>>shift)&255u)*inverse+127)/255)<<shift;
            dest[col]=out;
        }
    }
    return true;
}
inline bool paintSurfaceShape(HDC dc,int x,int y,int width,int height,float diameter,COLORREF fill,
                              COLORREF border,COLORREF background,bool glass,bool ellipse=false) {
    if(width<=0||height<=0)return true;
    Gdiplus::Bitmap mask(width,height,PixelFormat32bppPARGB);
    {
        Gdiplus::Graphics graphics(&mask);graphics.Clear(Gdiplus::Color(0,0,0,0));
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        auto color=[](COLORREF c){return Gdiplus::Color(255,GetRValue(c),GetGValue(c),GetBValue(c));};
        Gdiplus::GraphicsPath path;
        float w=static_cast<float>(width)-1,h=static_cast<float>(height)-1;
        if(ellipse)path.AddEllipse(.5f,.5f,w,h);
        else {
            float d=std::min({diameter,w,h});if(d<=0)return true;
            path.AddArc(.5f,.5f,d,d,180,90);path.AddArc(.5f+w-d,.5f,d,d,270,90);
            path.AddArc(.5f+w-d,.5f+h-d,d,d,0,90);path.AddArc(.5f,.5f+h-d,d,d,90,90);path.CloseFigure();
        }
        Gdiplus::SolidBrush brush(color(fill));graphics.FillPath(&brush,&path);
        if(border!=CLR_INVALID&&border!=fill){Gdiplus::Pen pen(color(border),1);graphics.DrawPath(&pen,&path);}
    }
    Gdiplus::BitmapData data{};Gdiplus::Rect rect(0,0,width,height);
    if(mask.LockBits(&rect,Gdiplus::ImageLockModeRead,PixelFormat32bppPARGB,&data)!=Gdiplus::Ok)return false;
    bool result=compositeSurfaceMask(dc,x,y,width,height,static_cast<const BYTE*>(data.Scan0),data.Stride,background,glass);
    mask.UnlockBits(&data);return result;
}
}
