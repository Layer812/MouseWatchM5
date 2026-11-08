// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Layer812

#pragma once
#include <Arduino.h>
#include <M5Unified.h>
#include "clock_cat_logic.hpp"
#include "clock_cat_art.hpp"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
namespace mousewatch {
// Core 0 renders ONLY an offscreen PSRAM sprite. No M5.update()/USB/BLE/LCD use.
// Core 1 owns M5Unified input, mouse HID and the only LCD pushSprite() calls.
class CatClock {
public:
  bool begin(int16_t w,int16_t h);
  void enable(bool enabled);
  bool enabled()const {return enabled_;}
  void setConnected(bool connected){connected_=connected;}
  bool present(); // call on Arduino loop/core 1; never hold the mutex waiting for render
  void updateRtc(CatClockTime value);
private:
  static void taskEntry(void* arg);
  void worker();
  void render(uint32_t now,CatClockTime t);
  void enter(CatMode state,uint32_t now);
  uint32_t choose()const;
  bool initialized_=false;
  volatile bool enabled_=false;
  volatile bool connected_=false;
  SemaphoreHandle_t frameMutex_=nullptr;
  portMUX_TYPE timeLock_=portMUX_INITIALIZER_UNLOCKED;
  CatClockTime current_{};
  M5Canvas canvas_{&M5.Display};
  uint16_t width_=0,height_=0;
  volatile uint32_t frameSerial_=0;
  uint32_t presentedSerial_=0;
  CatMode state_=CatMode::Sleep;
  uint32_t stateStarted_=0,duration_=30000, lastMinuteKey_=UINT32_MAX;
  float heldMinute_=0,heldHour_=0;
  bool held_=false;
};
} // namespace mousewatch