// SPDX-License-Identifier: GPL-2.0-or-later
// Original Win98 GDI ordinary-DC coordinate state and integer transforms.
// Device data, DC validation/COW and metafile recording are
// explicit provider boundaries; no host pointer occupies an original WORD.
#pragma once
namespace Win98DcMap
{
using U = unsigned;
static_assert(sizeof(U)==4,"Original DWORD arithmetic");
inline U W(U value) { return value&0xffffu; }
inline int S(U value) { return int(value&0x7fffu)-int(value&0x8000u); }
struct Point { int x=0,y=0; };
struct Units { Point window,viewport; };
struct State
{
    Point window_origin{},window_extent{1,1},viewport_effective{},viewport_extent{1,1};
    Point viewport_origin{},device_origin{},translation{},shifts{};
    U mode=1,flags=0,transform_flags=7;
};
inline Point Narrow(Point p) { return {S(U(p.x)),S(U(p.y))}; }
inline int Clamp(long long value) { return value < -32768 ? -32768 : value > 32767 ? 32767 : int(value); }
inline bool Overflow(int value) { return value < -32768 || value > 32767; }

// GDI. Upper flag bits are independent DC state.
inline void Recompute(State &s)
{
    U flags=s.transform_flags&0xf8u;
    if(s.viewport_extent.x==s.window_extent.x && s.viewport_extent.y==s.window_extent.y)
    {
        flags|=1;
        if(!(s.shifts.x|s.shifts.y))
        {
            const U combined=W(U(s.translation.x))|W(U(s.translation.y));
            if(!(combined&0x8000u))
            {
                flags|=4;
                if(!combined)flags|=2;
            }
        }
    }
    s.transform_flags=flags;
}
// GDI.
inline void OriginChanged(State &s)
{
    s.translation={S(U(s.viewport_effective.x-s.window_origin.x)),
                   S(U(s.viewport_effective.y-s.window_origin.y))};
    Recompute(s);
}
// DeviceUnits(mode) returns the original four WORDs from device+28h+(mode-1)*8.
// Mode 1 uses the original fixed unit extents; mode 7 starts from mode 2.
// GDI after ordinary-DC access/recorder resolution.
template<class E> U SetMode(E &e,State &s,U mode)
{
    mode=W(mode);
    if(!mode || mode>8)return 0;
    const U previous=s.mode;
    s.mode=mode;
    if(previous!=mode)s.flags|=0x60;
    if(mode==1)s.window_extent=s.viewport_extent={1,1};
    else if(mode<=7)
    {
        const auto units=e.DeviceUnits(mode==7 ? 2 : mode);
        s.window_extent=Narrow(units.window);
        s.viewport_extent=Narrow(units.viewport);
    }
    Recompute(s);
    return previous;
}
// GDI; Offset entries1:1150..11CD add in WORDs before these stores.
inline Point SetOrigin(State &s,bool viewport,Point value,bool offset=false)
{
    auto &target=viewport ? s.viewport_origin : s.window_origin;
    const Point previous=target;
    if(offset)value={S(U(previous.x+S(U(value.x)))),S(U(previous.y+S(U(value.y))))};
    target=Narrow(value);
    if(viewport)
        s.viewport_effective={S(U(target.x+s.device_origin.x)),S(U(target.y+s.device_origin.y))};
    OriginChanged(s);
    return previous;
}
struct Product { int value; U success; };
// GDI. This is the original 16-bit helper, including SAR rounding
// of the WORD absolute denominator and saturating failure returns in DX:AX.
inline Product MultiplyDivide(U value,U numerator,U denominator)
{
    const U sign=(value^numerator^denominator)&0x8000u;
    U den=W(denominator),a=W(value),b=W(numerator);
    if(den&0x8000)den=W(0u-den);
    if(a&0x8000)a=W(0u-a);
    if(b&0x8000)b=W(0u-b);
    const U product=a*b+W(U(S(den)>>1));
    if(!den || (product>>16)>=den)return {sign ? -32768 : 32767,0};
    const U quotient=product/den;
    if(quotient&0x8000u)return {sign ? -32768 : 32767,0};
    return {sign ? -int(quotient) : int(quotient),1};
}
inline int AbsoluteSaturated(int value) { return value==-32768 ? 32767 : value<0 ? -value : value; }
// GDI. The device aspect is read from device+28h/+2Ah.
inline void Isotropic(State &s,Point aspect)
{
    int y=MultiplyDivide(U(s.viewport_extent.x),U(aspect.x),U(aspect.y)).value;
    y=MultiplyDivide(U(y),U(s.window_extent.y),U(s.window_extent.x)).value;
    y=AbsoluteSaturated(y);
    if(AbsoluteSaturated(s.viewport_extent.y)>=y)
        s.viewport_extent.y=s.viewport_extent.y<0 ? -y : y;
    else
    {
        int x=MultiplyDivide(U(s.viewport_extent.y),U(aspect.y),U(aspect.x)).value;
        x=MultiplyDivide(U(x),U(s.window_extent.x),U(s.window_extent.y)).value;
        x=AbsoluteSaturated(x);
        s.viewport_extent.x=s.viewport_extent.x<0 ? -x : x;
    }
}
// GDI. A zero coordinate returns DX:AX=0 before reading old extents.
// ObjectAffectsMapping is the original selected-object WORD+10h test.
template<class E> Point SetExtent(E &e,State &s,bool viewport,Point value)
{
    value=Narrow(value);
    if(!value.x || !value.y)return {};
    auto &target=viewport ? s.viewport_extent : s.window_extent;
    const Point previous=target;
    if(S(s.mode)>6)
    {
        target=value;
        s.flags|=e.ObjectAffectsMapping() ? 0x60u : 0x40u;
        if(s.mode==7)Isotropic(s,e.DeviceAspect());
        Recompute(s);
    }
    return previous;
}
// GDI. The temporary DC-type substitution prevents duplicate
// metafile recording; that object/recorder operation is outside this state core.
template<class E> Point ScaleExtent(E &e,State &s,bool viewport,Point numerator,Point denominator)
{
    const Point value=viewport ? s.viewport_extent : s.window_extent;
    const Point scaled={MultiplyDivide(U(value.x),U(numerator.x),U(denominator.x)).value,
                        MultiplyDivide(U(value.y),U(numerator.y),U(denominator.y)).value};
    return SetExtent(e,s,viewport,scaled);
}
// GDI. Full-difference fallback uses the original signed comparison
// before the two unsigned DIVs. Absolute denominator8000h therefore saturates.
inline int FullTransform(int value,int source,int numerator,int denominator,int destination)
{
    const long long difference=static_cast<long long>(value)-source;
    const bool negative=(difference<0)^(numerator<0)^(denominator<0);
    const U den=denominator<0 ? U(-denominator) : U(denominator);
    const U number=numerator<0 ? U(-numerator) : U(numerator);
    const unsigned long long magnitude=(difference<0 ? -difference : difference)*number+
                                        W(U(S(den)>>1));
    const U high=U(magnitude>>32)&0xffffu;
    if(S(high)>=S(den))return negative ? -32768 : 32767;
    const unsigned long long quotient=magnitude/den;
    return Clamp((negative ? -static_cast<long long>(quotient) : static_cast<long long>(quotient))+destination);
}
// Original fast path1:AAD9..AB39 /. An IDIV fault goes through
// the original recovery to FullTransform; native arithmetic avoids trapping.
inline int ScaledTransform(int value,int source,int numerator,int denominator,int destination,
                           bool force_fallback=false)
{
    const int difference=value-source;
    if(force_fallback || Overflow(difference) || !denominator)
        return FullTransform(value,source,numerator,denominator,destination);
    const int product=difference*numerator;
    int half=denominator>>1;
    if(((U(half)^U(product>>16))&0x8000u)!=0)half=S(0u-U(half));
    const long long dividend=static_cast<long long>(product)+half;
    const long long quotient=dividend/denominator;
    if(quotient < -32768 || quotient > 32767 || Overflow(int(quotient)+destination))
        return FullTransform(value,source,numerator,denominator,destination);
    return int(quotient)+destination;
}
// GDI. Cached WORD translation can already have overflowed while
// subtracting the device origin. The original chooses JO/JNO loops accordingly.
inline int Translated(int value,int translation,int device,bool inverse,bool api=true)
{
    const int difference=translation-(api ? device : 0);
    const bool prior_overflow=api && Overflow(difference);
    int delta=S(U(difference));
    if(inverse)delta=S(0u-U(delta));
    const int sum=value+delta,result=S(U(sum));
    if(Overflow(sum)!=prior_overflow)return result<0 ? 32767 : -32768;
    return result;
}
inline Point Transform(const State &s,Point value,bool inverse,bool api=true,U remaining_words=2)
{
    value=Narrow(value);
    if(s.transform_flags&1)
        return {Translated(value.x,s.translation.x,s.device_origin.x,inverse,api),
                Translated(value.y,s.translation.y,s.device_origin.y,inverse,api)};
    const Point viewport=api ? s.viewport_origin : s.viewport_effective;
    const Point source=inverse ? viewport : s.window_origin;
    const Point destination=inverse ? s.window_origin : viewport;
    const Point numerator=inverse ? s.window_extent : s.viewport_extent;
    const Point denominator=inverse ? s.viewport_extent : s.window_extent;
    return {ScaledTransform(value.x,source.x,numerator.x,denominator.x,destination.x,(remaining_words&0x8000)!=0),
            ScaledTransform(value.y,source.y,numerator.y,denominator.y,destination.y,((remaining_words-1)&0x8000)!=0)};
}

enum class Change { WindowOrigin, ViewportOrigin, OffsetWindow, OffsetViewport,
                    WindowExtent, ViewportExtent, ScaleWindow, ScaleViewport };
struct ChangeResult { U result; Point previous; bool written; };
// GDI. These Ex wrappers
// deliberately differ on zero extents: only SetViewportExtEx tests DX:AX.
template<class E> ChangeResult ChangeEx(E &e,State &s,Change operation,const U *a)
{
    Point previous;
    if(operation<=Change::OffsetViewport)
        previous=SetOrigin(s,operation==Change::ViewportOrigin || operation==Change::OffsetViewport,
                           {S(a[0]),S(a[1])},operation>=Change::OffsetWindow);
    else if(operation<=Change::ViewportExtent)
        previous=SetExtent(e,s,operation==Change::ViewportExtent,{S(a[0]),S(a[1])});
    else previous=ScaleExtent(e,s,operation==Change::ScaleViewport,
                             {S(a[0]),S(a[2])},{S(a[1]),S(a[3])});
    if(operation==Change::ViewportExtent && !(previous.x|previous.y))return {0,previous,false};
    return {1,previous,true};
}
enum class Field { WindowOrigin, ViewportOrigin, WindowExtent, ViewportExtent, DeviceOrigin };
inline Point Get(const State &s,Field field)
{
    switch(field)
    {
        case Field::WindowOrigin:return s.window_origin;
        case Field::ViewportOrigin:return s.viewport_origin;
        case Field::WindowExtent:return s.window_extent;
        case Field::ViewportExtent:return s.viewport_extent;
        case Field::DeviceOrigin:return s.device_origin;
    }
    return {};
}
// Original loops use a WORD point count and a wrapping WORD byte offset.
// A mapper supplies an independently addressed 64-KiB packed view. This also
// retains the original count-zero LOOP behavior and signed CX fallback.
template<class E> void PackedTransform(E &e,const State &s,U count,bool inverse)
{
    U remaining=W(count);
    if(s.transform_flags&1)
    {
        const U points=remaining ? remaining : 65536u;
        for(U i=0;i<points;++i)
        {
            const U at=W(i*4);
            const auto out=Transform(s,{S(e.ReadPacked(at)),S(e.ReadPacked(W(at+2)))},inverse);
            e.WritePacked(at,W(U(out.x)));e.WritePacked(W(at+2),W(U(out.y)));
        }
        return;
    }
    remaining=W(remaining*2);
    const Point source=inverse ? s.viewport_origin : s.window_origin;
    const Point destination=inverse ? s.window_origin : s.viewport_origin;
    const Point numerator=inverse ? s.window_extent : s.viewport_extent;
    const Point denominator=inverse ? s.viewport_extent : s.window_extent;
    U at=0;bool y=false;
    do
    {
        const int value=S(e.ReadPacked(at)),from=y?source.y:source.x,to=y?destination.y:destination.x;
        const int num=y?numerator.y:numerator.x,den=y?denominator.y:denominator.x;
        const int difference=value-from;
        bool fallback=Overflow(difference);
        int result=0;
        if(!fallback)
        {
            const int product=difference*num;
            int half=den>>1;
            if(((U(half)^U(product>>16))&0x8000u)!=0)half=S(0u-U(half));
            const long long dividend=static_cast<long long>(product)+half;
            fallback=dividend < -2147483648ll || dividend > 2147483647ll;
            if(!fallback)
            {
                long long quotient=den ? dividend/den : 0;
                if(!den || quotient < -32768 || quotient > 32767)
                {
                    // The original #DE handler negates CX before retrying a
                    // repaired division. Its following JL may negate it back.
                    remaining=W(0u-remaining);quotient=0;
                }
                if(S(remaining)<0){remaining=W(0u-remaining);fallback=true;}
                else {result=int(quotient)+to;fallback=Overflow(result);}
            }
        }
        if(fallback)result=FullTransform(value,from,num,den,to);
        e.WritePacked(at,W(U(result)));
        at=W(at+2);y=!y;remaining=W(remaining-1);
    } while(remaining);
}
// GDI32 after probe/stack entry. The full DWORD count is
// used by the in-place pack/expand loops; only the 16-bit callee count narrows.
template<class E> U ConvertPoints(E &e,const State &s,U count,bool inverse)
{
    for(U i=0;i<count;++i)
    {
        const U x=e.Read32(i*8),y=e.Read32(i*8+4);
        e.Write16(i*4,W(x));e.Write16(i*4+2,W(y));
    }
    e.BeginPacked();
    PackedTransform(e,s,count,inverse);
    e.EndPacked();
    if(count && count<=0x1fffffffu)
        for(U i=count;i;--i)
        {
            const U y=U(S(e.Read16(i*4-2))),x=U(S(e.Read16(i*4-4)));
            e.Write32(i*8-4,y);e.Write32(i*8-8,x);
        }
    return 1;
}
} // namespace Win98DcMap
