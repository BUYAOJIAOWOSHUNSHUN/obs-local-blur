/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <algorithm>
#include <cmath>

namespace crop {
struct Point { double x, y; };
struct Rect { double left, top, right, bottom; };
enum Handle { None=0, Left=1, Right=2, Top=4, Bottom=8, Move=16 };
inline double clamp(double v, double lo, double hi) { return std::max(lo, std::min(hi,v)); }
inline Rect fromMargins(double left, double top, double right, double bottom)
{ return {left/100.0,top/100.0,1.0-right/100.0,1.0-bottom/100.0}; }
inline bool valid(Rect r)
{ return std::isfinite(r.left)&&std::isfinite(r.top)&&std::isfinite(r.right)&&std::isfinite(r.bottom)&&r.left>=0&&r.top>=0&&r.right<=1&&r.bottom<=1&&r.right>r.left&&r.bottom>r.top; }
inline Rect fit(double sw,double sh,double vw,double vh)
{
    if(sw<=0||sh<=0||vw<=0||vh<=0) return {0,0,0,0};
    const double s=std::min(vw/sw,vh/sh),w=sw*s,h=sh*s;
    return {(vw-w)/2,(vh-h)/2,(vw+w)/2,(vh+h)/2};
}
inline Point normalized(Point p,Rect viewport)
{ return {(p.x-viewport.left)/(viewport.right-viewport.left),(p.y-viewport.top)/(viewport.bottom-viewport.top)}; }
inline int hit(Rect r,Point p,double tx,double ty)
{
    if(!valid(r)) return None;
    const bool inX=p.x>=r.left-tx&&p.x<=r.right+tx;
    const bool inY=p.y>=r.top-ty&&p.y<=r.bottom+ty;
    if(!inX||!inY) return None;
    int h=None;
    if(std::abs(p.x-r.left)<=tx) h|=Left;
    else if(std::abs(p.x-r.right)<=tx) h|=Right;
    if(std::abs(p.y-r.top)<=ty) h|=Top;
    else if(std::abs(p.y-r.bottom)<=ty) h|=Bottom;
    return h?h:Move;
}
inline Rect drag(Rect r,int handle,Point delta,double minWidth,double minHeight)
{
    if(!valid(r)) return r;
    if(handle==Move) {
        const double dx=clamp(delta.x,-r.left,1-r.right),dy=clamp(delta.y,-r.top,1-r.bottom);
        return {r.left+dx,r.top+dy,r.right+dx,r.bottom+dy};
    }
    minWidth=std::min(minWidth,r.right-r.left);minHeight=std::min(minHeight,r.bottom-r.top);
    if(handle&Left) r.left=clamp(r.left+delta.x,0,r.right-minWidth);
    if(handle&Right) r.right=clamp(r.right+delta.x,r.left+minWidth,1);
    if(handle&Top) r.top=clamp(r.top+delta.y,0,r.bottom-minHeight);
    if(handle&Bottom) r.bottom=clamp(r.bottom+delta.y,r.top+minHeight,1);
    return r;
}
inline Rect dragAspect(Rect r,int handle,Point delta,double minWidth,double minHeight)
{
    if(handle==Move||!valid(r))return drag(r,handle,delta,minWidth,minHeight);
    const double w=r.right-r.left,h=r.bottom-r.top;
    const bool horizontal=handle&(Left|Right),vertical=handle&(Top|Bottom);
    const double sx=horizontal?((handle&Left)?-delta.x:delta.x)/w:0;
    const double sy=vertical?((handle&Top)?-delta.y:delta.y)/h:0;
    double scale=1+(std::abs(sx)>std::abs(sy)?sx:sy);
    const double ax=(handle&Left)?r.right:(handle&Right)?r.left:(r.left+r.right)/2;
    const double ay=(handle&Top)?r.bottom:(handle&Bottom)?r.top:(r.top+r.bottom)/2;
    const double maxX=(handle&Left)?ax/w:(handle&Right)?(1-ax)/w:2*std::min(ax,1-ax)/w;
    const double maxY=(handle&Top)?ay/h:(handle&Bottom)?(1-ay)/h:2*std::min(ay,1-ay)/h;
    const double maximum=std::min(maxX,maxY),minimum=std::min(maximum,std::max(minWidth/w,minHeight/h));
    scale=std::clamp(scale,minimum,maximum);
    const double nw=w*scale,nh=h*scale;
    const double left=(handle&Left)?ax-nw:(handle&Right)?ax:ax-nw/2;
    const double top=(handle&Top)?ay-nh:(handle&Bottom)?ay:ay-nh/2;
    return {left,top,left+nw,top+nh};
}
}
