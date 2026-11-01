#pragma once
#include <algorithm>
#include <cmath>
#include <climits>
namespace meter {
struct UiRect { int left,top,right,bottom; };
inline double quotaSweep(double percent) {
    return std::isfinite(percent)?std::clamp(percent,0.0,100.0)*3.6:0.0;
}
inline bool removeHit(int x,int y,int width,int cardTop,int viewportTop,int viewportBottom) {
    if(y<viewportTop||y>=viewportBottom)return false;
    const int dx=x-(width-40),dy=y-(cardTop+22);
    return dx*dx+dy*dy<=12*12;
}
inline int edgeSide(UiRect r,UiRect work,int threshold,bool requireTouch=true) {
    const bool vertical=r.bottom>work.top&&r.top<work.bottom;
    const bool horizontal=r.right>work.left&&r.left<work.right;
    int distances[]={vertical?std::max(0,r.left-work.left):INT_MAX,
        vertical?std::max(0,work.right-r.right):INT_MAX,
        horizontal?std::max(0,r.top-work.top):INT_MAX,
        horizontal?std::max(0,work.bottom-r.bottom):INT_MAX};
    int side=0,best=INT_MAX;
    for(int i=0;i<4;++i)if(distances[i]<best&&(!requireTouch||distances[i]<=threshold)){best=distances[i];side=i+1;}
    return side;
}
struct DockExtent { int total,visible,maxScroll; };
inline UiRect pinDock(UiRect r,UiRect work,int side) {
    const int width=r.right-r.left,height=r.bottom-r.top;
    int x=std::clamp(r.left,work.left,std::max(work.left,work.right-width));
    int y=std::clamp(r.top,work.top,std::max(work.top,work.bottom-height));
    if(side==1)x=work.left;
    if(side==2)x=std::max(work.left,work.right-width);
    if(side==3)y=work.top;
    if(side==4)y=std::max(work.top,work.bottom-height);
    return {x,y,x+width,y+height};
}
inline DockExtent dockExtent(int accounts,int available) {
    const int total=std::max(84,accounts*84+8);
    const int visible=std::min(total,std::max(1,available));
    return {total,visible,std::max(0,total-visible)};
}
}
