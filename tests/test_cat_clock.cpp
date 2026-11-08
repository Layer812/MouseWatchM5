// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Layer812
#include "../src/clock_cat_logic.hpp"
#include "../src/clock_cat_art.hpp"
#include <cassert>
#include <cmath>
using namespace mousewatch;
int main(){
 CatClockTime t{2026,11,8,21,30,0};assert(time_plausible(t));
 assert(cat_frame_index(CatMode::Sleep,0,0)==12);
 assert(cat_frame_index(CatMode::Walk,490,0)==2);
 assert(cat_frame_index(CatMode::Run,200,0)==6);
 assert(cat_frame_index(CatMode::Swat,0,0)==32);
 assert(cat_frame_index(CatMode::Swat,1000,.92f)==37);
 assert(cat_minute_angle(t)>3.1f && cat_minute_angle(t)<3.2f);
 assert(kClockCatRegularFrames==32 && kClockCatSwatFrames==6);
 for(int i=0;i<38;i++){int n=0;for(int j=0;j<kClockCatArtStride;j++)n+=kClockCatArt[i][j]!=0;assert(n>0);}
}
