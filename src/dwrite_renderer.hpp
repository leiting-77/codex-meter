#pragma once
#include "surface_renderer.hpp"
#include <dwrite_1.h>
#include <string>
namespace meter {
template<class T> class WriteRef {
    T* value=nullptr;
public:
    ~WriteRef(){if(value)value->Release();}
    WriteRef()=default;WriteRef(const WriteRef&)=delete;WriteRef& operator=(const WriteRef&)=delete;
    T* get() const{return value;}T* operator->() const{return value;}
    T** put(){if(value)value->Release();value=nullptr;return &value;}
};
class WriteGlyphRenderer final:public IDWriteTextRenderer {
    IDWriteBitmapRenderTarget* target;IDWriteRenderingParams* params;
public:
    WriteGlyphRenderer(IDWriteBitmapRenderTarget* t,IDWriteRenderingParams* p):target(t),params(p){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** value) override {
        if(!value)return E_POINTER;*value=nullptr;
        if(id==__uuidof(IUnknown)||id==__uuidof(IDWritePixelSnapping)||id==__uuidof(IDWriteTextRenderer)){
            *value=static_cast<IDWriteTextRenderer*>(this);AddRef();return S_OK;}
        return E_NOINTERFACE;
    }
    // Non-owning synchronous callback; it is never retained outside layout->Draw.
    ULONG STDMETHODCALLTYPE AddRef() override{return 1;}
    ULONG STDMETHODCALLTYPE Release() override{return 1;}
    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*,BOOL* disabled) override{*disabled=FALSE;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*,DWRITE_MATRIX* matrix) override{*matrix={1,0,0,1,0,0};return S_OK;}
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*,FLOAT* pixels) override{*pixels=1;return S_OK;}
    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*,FLOAT x,FLOAT y,DWRITE_MEASURING_MODE mode,
        const DWRITE_GLYPH_RUN* run,const DWRITE_GLYPH_RUN_DESCRIPTION*,IUnknown*) override {
        return target->DrawGlyphRun(x,y,mode,run,params,RGB(255,255,255));
    }
    HRESULT STDMETHODCALLTYPE DrawUnderline(void*,FLOAT,FLOAT,const DWRITE_UNDERLINE*,IUnknown*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*,FLOAT,FLOAT,const DWRITE_STRIKETHROUGH*,IUnknown*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void* context,FLOAT x,FLOAT y,IDWriteInlineObject* object,
        BOOL sideways,BOOL rtl,IUnknown* effect) override{return object->Draw(context,this,x,y,sideways,rtl,effect);}
};
class WriteTextEngine {
    WriteRef<IDWriteFactory> factory;
    WriteRef<IDWriteGdiInterop> interop;
    WriteRef<IDWriteRenderingParams> params;
    WriteRef<IDWriteBitmapRenderTarget> target;
    int capacityWidth=0,capacityHeight=0;
    bool initialized=false;
public:
    WriteTextEngine() {
        if(FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(factory.put()))))return;
        if(FAILED(factory->GetGdiInterop(interop.put())))return;
        WriteRef<IDWriteRenderingParams> defaults;
        if(FAILED(factory->CreateRenderingParams(defaults.put())))return;
        initialized=SUCCEEDED(factory->CreateCustomRenderingParams(defaults->GetGamma(),defaults->GetEnhancedContrast(),0,
            DWRITE_PIXEL_GEOMETRY_FLAT,DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,params.put()));
    }
    bool ready() const{return initialized;}
    bool paint(HDC dc,const std::wstring& text,int x,int y,int width,int height,HFONT font,COLORREF ink,
               UINT flags,COLORREF background,bool glass) {
        if(!initialized)return false;
        if(width<=0||height<=0||text.empty())return true;
        if(!target.get()||width>capacityWidth||height>capacityHeight) {
            capacityWidth=std::max(width,capacityWidth);capacityHeight=std::max(height,capacityHeight);
            if(FAILED(interop->CreateBitmapRenderTarget(dc,capacityWidth,capacityHeight,target.put())))return false;
            WriteRef<IDWriteBitmapRenderTarget1> grayscale;
            if(FAILED(target->QueryInterface(__uuidof(IDWriteBitmapRenderTarget1),reinterpret_cast<void**>(grayscale.put())))||
               FAILED(grayscale->SetTextAntialiasMode(DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE)))return false;
            target->SetPixelsPerDip(1);
        }
        LOGFONTW description{};if(!GetObjectW(font,sizeof(description),&description))return false;
        WriteRef<IDWriteTextFormat> format;
        if(FAILED(factory->CreateTextFormat(description.lfFaceName,nullptr,static_cast<DWRITE_FONT_WEIGHT>(description.lfWeight),
            description.lfItalic?DWRITE_FONT_STYLE_ITALIC:DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,
            static_cast<float>(std::abs(description.lfHeight)),L"zh-CN",format.put())))return false;
        format->SetWordWrapping((flags&DT_WORDBREAK)?DWRITE_WORD_WRAPPING_WRAP:DWRITE_WORD_WRAPPING_NO_WRAP);
        format->SetTextAlignment((flags&DT_CENTER)?DWRITE_TEXT_ALIGNMENT_CENTER:(flags&DT_RIGHT)?DWRITE_TEXT_ALIGNMENT_TRAILING:DWRITE_TEXT_ALIGNMENT_LEADING);
        format->SetParagraphAlignment((flags&DT_VCENTER)?DWRITE_PARAGRAPH_ALIGNMENT_CENTER:DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        WriteRef<IDWriteTextLayout> layout;
        if(FAILED(factory->CreateTextLayout(text.c_str(),static_cast<UINT32>(text.size()),format.get(),static_cast<float>(width),static_cast<float>(height),layout.put())))return false;
        WriteRef<IDWriteInlineObject> ellipsis;
        if((flags&DT_END_ELLIPSIS)&&SUCCEEDED(factory->CreateEllipsisTrimmingSign(format.get(),ellipsis.put()))) {
            DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER,0,0};layout->SetTrimming(&trimming,ellipsis.get());
        }
        HDC source=target->GetMemoryDC();RECT rect{0,0,width,height};FillRect(source,&rect,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        WriteGlyphRenderer renderer(target.get(),params.get());
        if(FAILED(layout->Draw(nullptr,&renderer,0,0)))return false;
        // Explicit top-down copy: don't assume the orientation of DirectWrite's DIB.
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;
        info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
        HDC mask=CreateCompatibleDC(dc);if(!mask)return false;
        void* bits=nullptr;HBITMAP bitmap=CreateDIBSection(mask,&info,DIB_RGB_COLORS,&bits,nullptr,0);
        if(!bitmap){DeleteDC(mask);return false;}
        auto old=SelectObject(mask,bitmap);BitBlt(mask,0,0,width,height,source,0,0,SRCCOPY);GdiFlush();
        bool result=compositeSurfaceMask(dc,x,y,width,height,static_cast<const BYTE*>(bits),width*4,background,glass,true,ink);
        SelectObject(mask,old);DeleteObject(bitmap);DeleteDC(mask);return result;
    }
};
inline WriteTextEngine& writeTextEngine(){static WriteTextEngine engine;return engine;}
}
