// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Layer812

#include "clock_cat.hpp"
#include <math.h>
#include <esp_system.h>
namespace mousewatch {
static constexpr float PI_F=3.14159265358979323846f;
static float clamp01(float v){return v<0?0:v>1?1:v;}
static float ease(float v){v=clamp01(v);return v*v*(3.f-2.f*v);}
static uint32_t state_ms(CatMode s){switch(s){
 case CatMode::Walk:case CatMode::Run:return 5000;
 case CatMode::Wake:return 2500;case CatMode::Approach:return 3000;
 case CatMode::Depart:case CatMode::Swat:return 2000;
 case CatMode::Special:return 8000;case CatMode::Caught:return 300000;
 default:return 30000;}}
void CatClock::enter(CatMode state,uint32_t now){state_=state;stateStarted_=now;duration_=state_ms(state);}
uint32_t CatClock::choose()const{
  // Same 9/5/6/4/1 weighted cycle as the Clock Cat R9 web counterpart.
  const uint32_t n=esp_random()%25;
  if(n<9)return uint32_t(CatMode::Sleep);
  if(n<14)return uint32_t(CatMode::Sit);
  if(n<20)return uint32_t(CatMode::Walk);
  if(n<24)return uint32_t(CatMode::Run);
  return uint32_t(CatMode::Approach);
}
bool CatClock::begin(int16_t w,int16_t h){
 if(initialized_)return true;
 if(w<400||h<400||w!=h)return false;
 width_=w;height_=h;
 frameMutex_=xSemaphoreCreateMutex();if(!frameMutex_)return false;
 canvas_.setPsram(true);canvas_.setColorDepth(16);
 if(!canvas_.createSprite(width_,height_))return false;
 canvas_.fillSprite(0);
 const BaseType_t ok=xTaskCreatePinnedToCore(taskEntry,"CatClockRender",8192,this,1,nullptr,0);
 if(ok!=pdPASS){canvas_.deleteSprite();return false;}
 initialized_=true;
 return true;
}
void CatClock::enable(bool en){enabled_=en;if(en)presentedSerial_=0;}
void CatClock::updateRtc(CatClockTime value){
 if(!time_plausible(value))return;
 portENTER_CRITICAL(&timeLock_);current_=value;portEXIT_CRITICAL(&timeLock_);
}
void CatClock::taskEntry(void* arg){static_cast<CatClock*>(arg)->worker();vTaskDelete(nullptr);}
void CatClock::worker(){
 for(;;){
  if(!enabled_){vTaskDelay(pdMS_TO_TICKS(90));continue;}
  CatClockTime t;
  portENTER_CRITICAL(&timeLock_);t=current_;portEXIT_CRITICAL(&timeLock_);
  if(xSemaphoreTake(frameMutex_,pdMS_TO_TICKS(12))==pdTRUE){
    render(millis(),t);++frameSerial_;
    xSemaphoreGive(frameMutex_);
  }
  vTaskDelay(pdMS_TO_TICKS(140));  // ~6 FPS, low-priority CPU0; HID remains on CPU1.
 }
}
bool CatClock::present(){
 if(!enabled_||!initialized_||frameSerial_==presentedSerial_)return false;
 if(xSemaphoreTake(frameMutex_,0)!=pdTRUE)return false;
 if(!enabled_){xSemaphoreGive(frameMutex_);return false;}
 canvas_.pushSprite(0,0);
 presentedSerial_=frameSerial_;
 xSemaphoreGive(frameMutex_);
 return true;
}
static void wideHand(M5Canvas& c,int x,int y,float a,int len,int thickness){
 const int tx=x+int(sinf(a)*len),ty=y-int(cosf(a)*len);
 const int px=int(cosf(a)*thickness*.5f),py=int(sinf(a)*thickness*.5f);
 c.fillTriangle(x-px,y-py, x+px,y+py,tx+px,ty+py,0x0000);
 c.fillTriangle(x-px,y-py, tx-px,ty-py,tx+px,ty+py,0x0000);
}
static void drawCat(M5Canvas& c,int frame,int x,int y){
 // drawBitmap is a 1bpp transparent foreground operation; no alpha decode in the loop.
 if(frame<0||frame>=38)return;
 c.drawBitmap(x,y,kClockCatArt[frame],kClockCatArtWidth,kClockCatArtHeight,(uint16_t)0x0000);
}
void CatClock::render(uint32_t now,CatClockTime t){
 if(!time_plausible(t)) {t.year=2026;t.hour=12;t.minute=0;t.second=0;}
 if(!stateStarted_)enter(CatMode::Sleep,now);
 // Deliberate 29->34 cat-punch behavior in the R9 reference.
 const uint32_t minKey=(uint32_t(t.year)*372u+uint32_t(t.month)*31u+t.day)*24u*60u+t.hour*60u+t.minute;
 if(minKey!=lastMinuteKey_){
   lastMinuteKey_=minKey;
   if(t.minute==29 && state_==CatMode::Sleep && !held_){
     heldMinute_=6.2831853f*29.f/60.f;
     CatClockTime anchor=t;anchor.minute=29;anchor.second=0;
     heldHour_=cat_hour_angle(anchor);held_=true;enter(CatMode::Caught,now);
   }
 }
 if(held_&&state_==CatMode::Caught&&t.minute>=34){enter(CatMode::Swat,now);}
 uint32_t elapsed=now-stateStarted_;
 if(elapsed>=duration_&&state_!=CatMode::Caught){
   if(state_==CatMode::Swat){held_=false;enter(CatMode::Sit,now);}
   else if(state_==CatMode::Approach)enter(CatMode::Depart,now);
   else if(state_==CatMode::Wake||state_==CatMode::Depart||state_==CatMode::Special)enter(CatMode::Sit,now);
   else if(state_==CatMode::Sleep&&choose()!=uint32_t(CatMode::Sleep))enter(CatMode::Wake,now);
   else enter(CatMode(choose()),now);
   elapsed=0;
 }
 const float p=clamp01(float(elapsed)/(duration_?duration_:1));
 const int frame=cat_frame_index(state_,elapsed,p);
 const int cx=width_/2, cy=height_/2;
 const int radius=int(width_*.403f); // 188 for 468 AMOLED; inside both status bars.
 canvas_.fillSprite(0x0000);
 canvas_.fillCircle(cx,cy,radius,0xffff);
 canvas_.drawCircle(cx,cy,radius,0xbdf7);
 canvas_.drawCircle(cx,cy,radius-2,0x0000);
 float minAngle=cat_minute_angle(t),hourAngle=cat_hour_angle(t),wheelSpin=0;
 if(state_==CatMode::Caught){minAngle=heldMinute_;hourAngle=heldHour_;}
 if(state_==CatMode::Swat && held_){
   const float u=ease((p-.43f)/.075f);
   minAngle=heldMinute_+(minAngle-heldMinute_)*u;
   hourAngle=heldHour_+(hourAngle-heldHour_)*u;
 }
 if(state_==CatMode::Walk||state_==CatMode::Run){
    const float q=clamp01((p-.20f)/.71f);
    wheelSpin=(state_==CatMode::Run?2.f:1.f)*6.2831853f*(.9f*q+.1f*ease(q));
 }
 wideHand(canvas_,cx,cy,hourAngle+wheelSpin,radius*52/100,10);
 wideHand(canvas_,cx,cy,minAngle+wheelSpin,radius*77/100,5);
 canvas_.fillCircle(cx,cy,9,0x0000);
 // Cat is always drawn last, so paw can strike hand without disappearing behind it.
 int x=cx-80,y=cy+29;
 if(state_==CatMode::Swat){x=cx-52;y=cy+29;}
 if(state_==CatMode::Walk||state_==CatMode::Run){
   float angle=.52f*ease(p/.20f),theta=PI_F*.5f-angle;
   const float rad=radius*.81f;
   x=int(cx+rad*cosf(theta))-80;
   y=int(cy+rad*sinf(theta))-118;
   if(p>.91f){float u=ease((p-.91f)/.09f);x=int(x*(1-u)+(cx-80)*u);y=int(y*(1-u)+(cy+29)*u-12.f*sinf(PI_F*u));}
 }
 if(state_==CatMode::Approach){
   x=cx-80;y=cy+20;
 }
 drawCat(canvas_,frame,x,y);
 // Paint the status banners inside the SAME backbuffer: no black-bar flicker
 // and no second LCD transaction after pushSprite() on the mouse core.
 canvas_.fillRect(0,0,width_,44,(uint16_t)0x0000);
 canvas_.fillRect(0,height_-44,width_,44,(uint16_t)0x0000);
 canvas_.setFont(&fonts::Font4);
 canvas_.setTextDatum(middle_center);
 canvas_.setTextSize(1.f);
 canvas_.setTextColor((uint16_t)0xffff,(uint16_t)0x0000);
 canvas_.drawString("MouseWatchM5",cx,22);
 canvas_.setTextColor((uint16_t)(connected_?0x4fa5:0x4e9f),(uint16_t)0x0000);
 canvas_.drawString(connected_?"BLE Connected":"BLE Waiting",cx,height_-23);
}
} // namespace mousewatch