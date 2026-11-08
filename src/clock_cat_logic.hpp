// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Layer812

#pragma once
#include <stdint.h>
#include <math.h>
namespace mousewatch {
struct CatClockTime { uint16_t year; uint8_t month,day,hour,minute,second; };
enum class CatMode : uint8_t { Sleep,Sit,Walk,Run,Wake,Approach,Depart,Caught,Swat,Special };
inline bool time_plausible(const CatClockTime& x) {return x.year>=2024&&x.year<=2099&&x.month>=1&&x.month<=12&&x.day>=1&&x.day<=31&&x.hour<24&&x.minute<60&&x.second<60;}
inline int cat_frame_index(CatMode m, uint32_t elapsed, float p) {
  switch(m) {
    case CatMode::Walk:return (elapsed/245)%4;
    case CatMode::Run:return 4+(elapsed/100)%4;
    case CatMode::Sit:return 8+(elapsed/750)%4;
    case CatMode::Sleep:case CatMode::Caught:return 12+(elapsed/1600)%4;
    case CatMode::Wake:return 16+(p>=.75f?3:int(p*4));
    case CatMode::Approach:return 20+(p>=.667f?2:int(p*3));
    case CatMode::Depart:return 23;
    case CatMode::Swat:{static constexpr float t[]={0.f,.22f,.43f,.62f,.76f,.89f};int i=0;for(int j=1;j<6;j++)if(p>=t[j])i=j;return 32+i;}
    case CatMode::Special:return 28+(p>=.75f?3:int(p*4));
  }
  return 12;
}
inline float cat_minute_angle(const CatClockTime& t){return 6.283185307179586f*(t.minute+(t.second/60.f))/60.f;}
inline float cat_hour_angle(const CatClockTime& t){return 6.283185307179586f*((t.hour%12)+(t.minute/60.f)+(t.second/3600.f))/12.f;}
} // namespace mousewatch