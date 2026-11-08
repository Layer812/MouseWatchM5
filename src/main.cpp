// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Layer812

// MouseWatchM5 R2 FIX13: BLE status overlays + Clock Cat on CPU0.
// Normal mouse boots retain the stock USB Serial/JTAG programming port.
// No back/forward double tap; ring scroll only. Clock Cat R9 companion art embedded.
// Hardware power, display, buttons and touch are managed only via M5Unified 0.3.0.
#include <Arduino.h>
#include <M5Unified.h>
#include <BleMouse.h>
#include <Preferences.h>
#include "cursor_mapping.hpp"
#include "ring_scroll.hpp"
#include "face_storage.hpp"
#include "usb_drive.hpp"
#include "ui_hitbox.hpp"
#include "clock_cat.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace {
constexpr uint8_t kUsbRequestVersion=7;
constexpr uint16_t kFaceBlack = 0x0000;
constexpr uint16_t kFaceWhite = 0xFFFF;
constexpr uint16_t kFaceCyan = 0x4e9f;
constexpr uint16_t kFaceGrey = 0x6b4d;
constexpr uint16_t kFaceGreen = 0x4fa5;
constexpr uint16_t kFaceOrange = 0xfd20;
constexpr int TAP_MAX_DIST = 15;
constexpr uint32_t TAP_MAX_MS = 260;

BleMouse mouse("MouseWatchM5", "M5Stack", 100);
Preferences pref;
enum class Screen : uint8_t { Boot, Settings, WatchFaces, MouseSettings, ImagePicker, Active, USBMode, USBError };
enum class Face : uint8_t { ClockCat=0, Image=2 };
Screen screen = Screen::Boot;

bool right_handed = true;
uint8_t pointer_speed = 5; // 1..10
bool reverse_scroll = false;
bool ring_enabled = true;
Face selected_face = Face::Image;
uint8_t selected_image = 0;
uint32_t face_last_draw = 0;
mousewatch::CatClock cat_clock;
uint32_t rtc_last_read_ms = 0;
bool prefs_available = false;
char selected_image_name[96]{}; // stable across USB file additions and sort order
bool ble_started = false;
bool connected_prev = false;
int usb_status_prev = -1;
bool fallback_face_visible = false; // Active mouse is independent of image storage.
bool buttons_armed = false;
bool left_sent = false;
bool right_sent = false;
bool touch_prev = false;
bool touch_active = false;
bool touch_is_ring = false;
bool ring_gesture_moved = false;
int start_x = 0, start_y = 0, last_x = 0, last_y = 0;
uint32_t touch_start_ms = 0;
int ui_press_x = 0, ui_press_y = 0;
bool usb_row_armed = false;
int ui_last_x = 0, ui_last_y = 0;
uint32_t ui_press_ms = 0;
uint32_t vibration_end_ms = 0;
float x_remainder = 0.f;
float y_remainder = 0.f;
mousewatch::RingScroll ring;
mousewatch::FaceStorage faces;
mousewatch::USBDrive usb_disk;

bool display_ready() {
  const int w = M5.Display.width(), h = M5.Display.height();
  return w == h && (w == 466 || w == 468);
}
int cx() { return M5.Display.width() / 2; }
int cy() { return M5.Display.height() / 2; }
int clamp_report(int x) { return x < -90 ? -90 : (x > 90 ? 90 : x); }

void vibrate(uint8_t strength = 110, uint16_t duration_ms = 22) {
  M5.Power.setVibration(strength);
  vibration_end_ms = millis() + duration_ms;
}
void font(uint16_t color = kFaceWhite, uint16_t bg = kFaceBlack) {
  M5.Display.setFont(&fonts::Font4);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(1.f);
  M5.Display.setTextColor(color, bg);
}
void label(const char* s, int y, uint16_t color = kFaceWhite) {
  font(color);
  M5.Display.drawString(s, cx(), y);
}
const char* face_name() {
  switch (selected_face) {
    case Face::ClockCat: return "CAT CLOCK";
    case Face::Image: return "SINGLE IMAGE";
  }
  return "SINGLE IMAGE";
}
void label_at(const char* s, int x, int y, uint16_t color = kFaceWhite) {
  font(color);
  M5.Display.drawString(s, x, y);
}
void menu_base(const char* title) {
  M5.Display.fillScreen(kFaceBlack);
  M5.Display.drawCircle(cx(), cy(), cx()-5, kFaceGrey);
  label(title, 76, kFaceCyan);
}
void draw_boot() {
  menu_base("MouseWatchM5");
  label(face_name(), 167);
  label(right_handed ? "RIGHT HAND" : "LEFT HAND", 218, kFaceGrey);
  label("BUTTON: START", 294, kFaceWhite);
  label("CENTER: SETTINGS", 349, kFaceCyan);
}
void draw_settings() {
  menu_base("SETTINGS");
  label("WATCH FACE >", 163, kFaceWhite);
  label("MOUSE SETTING >", 243, kFaceWhite);
  label("EXIT", 325, kFaceGreen);
}
void visible_filename(char* out,size_t cap,const char* name) {
  // Show the actual selected filename, including its extension. No path.
  const char* base=strrchr(name,'/');base=base?base+1:name;
  if(!base||!*base){snprintf(out,cap,"NO IMAGE SELECTED");return;}
  // Reserve enough space for the circular bezel: about 30 ASCII chars.
  snprintf(out,cap,"%.30s",base);
}
void draw_watch_faces() {
  menu_base("WATCH FACE");
  label_at("SINGLE IMAGE", cx()-43, 159,
           selected_face==Face::Image ? kFaceGreen : kFaceWhite);
  label_at("[SET]", cx()+123, 159, kFaceCyan);
  char filename[64];
  visible_filename(filename,sizeof(filename),faces.count()?faces.filename(selected_image%faces.count()):"");
  label(filename,213,faces.count()?kFaceGrey:kFaceOrange);
  label("CAT CLOCK",284,
        selected_face==Face::ClockCat?kFaceGreen:kFaceWhite);
  label("USB DRIVE",349,kFaceWhite);
  label("BACK",416,kFaceCyan);
}
void draw_mouse_settings() {
  menu_base("MOUSE SETTING");
  label(right_handed ? "HAND: RIGHT" : "HAND: LEFT", 140);
  char line[32];
  snprintf(line, sizeof(line), "SPEED: %u", unsigned(pointer_speed));
  label(line, 208);
  label(ring_enabled ? "RING: ON" : "RING: OFF", 278, kFaceGreen);
  label(reverse_scroll ? "WHEEL: REVERSE" : "WHEEL: NORMAL", 344);
  label("BACK", 410, kFaceCyan);
}
void draw_image_picker() {
  menu_base("SINGLE IMAGE [SET]");
  const uint8_t count=faces.count();
  if (!count) {
    label("NO IMAGES",205,kFaceOrange);
    label("USB DRIVE TO COPY",278,kFaceGrey);
  } else {
    char line[40];
    snprintf(line,sizeof(line),"%u / %u",unsigned(selected_image%count+1),unsigned(count));
    label(line,142,kFaceGrey);
    char name[64];visible_filename(name,sizeof(name),faces.filename(selected_image%count));
    label(name,211,kFaceWhite);
    label_at("< PREV",cx()-92,280,kFaceCyan);
    label_at("NEXT >",cx()+92,280,kFaceCyan);
    label("USE THIS IMAGE",347,kFaceGreen);
  }
  label("BACK",410,kFaceCyan);
}
// The normal mouse surface shows two subtle concentric guide circles:
// inner center = pointer area reference, outer = ring-scroll boundary.
// No "TOUCH TO MOVE", no F-version text, no runtime menus.
void draw_pointer_guides() {
  const int r=cx();
  M5.Display.drawCircle(cx(),cy(),55,kFaceGrey);
  M5.Display.drawCircle(cx(),cy(),174,kFaceGrey);
  M5.Display.drawCircle(cx(),cy(),r-8,kFaceGrey);
}
void draw_empty_mouse_face() {
  M5.Display.fillScreen(kFaceBlack);
  draw_pointer_guides();
}
// The label is an overlay AFTER the face, never part of the resized image cache.
// Paint only on face refresh / BLE transition. Text is large enough for the AMOLED.
void draw_ble_overlay() {
  const bool connected=ble_started && mouse.isConnected();
  const int w=M5.Display.width(),h=M5.Display.height();
  M5.Display.fillRect(0,0,w,44,kFaceBlack);
  M5.Display.fillRect(0,h-44,w,44,kFaceBlack);
  font(kFaceWhite,kFaceBlack);
  M5.Display.drawString("MouseWatchM5",cx(),22);
  font(connected?kFaceGreen:kFaceCyan,kFaceBlack);
  M5.Display.drawString(connected?"BLE Connected":"BLE Waiting",cx(),h-23);
}
void read_clock_rtc() {
  const uint32_t now=millis();
  if(now-rtc_last_read_ms<400)return;
  rtc_last_read_ms=now;
  if(!M5.Rtc.isEnabled())return;
  const auto dt=M5.Rtc.getDateTime();
  mousewatch::CatClockTime time{};
  time.year=dt.date.year;time.month=dt.date.month;time.day=dt.date.date;
  time.hour=dt.time.hours;time.minute=dt.time.minutes;time.second=dt.time.seconds;
  cat_clock.updateRtc(time);
}
void draw_face() {
  // Mouse HID never depends on images or clock rendering.
  if(selected_face==Face::ClockCat) {
    if(cat_clock.begin(M5.Display.width(),M5.Display.height())) {
      read_clock_rtc();
      cat_clock.setConnected(ble_started && mouse.isConnected());
      cat_clock.enable(true);
      // Worker renders a new offscreen frame on core0; core1 pushes it later.
      M5.Display.fillScreen(kFaceBlack);
      draw_ble_overlay();
    } else {
      draw_empty_mouse_face();
      draw_ble_overlay();
    }
    face_last_draw=millis();return;
  }
  cat_clock.enable(false);
  bool shown=false;
  if(faces.count()) {
    selected_image%=faces.count();
    shown=faces.show(M5.Display,selected_image);
  }
  fallback_face_visible=!shown;
  if (!shown)draw_empty_mouse_face();
  else draw_pointer_guides();
  draw_ble_overlay();
  face_last_draw=millis();
}
void save_settings() {
  if (!prefs_available) return;
  pref.putBool("right", right_handed);
  pref.putUChar("speed", pointer_speed);
  pref.putBool("reverse", reverse_scroll);
  pref.putBool("ring", ring_enabled);
  pref.putUChar("face", static_cast<uint8_t>(selected_face));
  pref.putUChar("image", selected_image);
  pref.putString("imgName",selected_image_name);
}
void enter_active() {
  // Boot button is never forwarded as a click on the iPad.
  buttons_armed = false;
  touch_active = false;

  touch_prev = M5.Touch.isEnabled() && M5.Touch.getDetail().isPressed();
  screen = Screen::Active;
  // Start BLE HID BEFORE any optional image loading or decoding.
  // A zero-image drive must not delay the beginning of mouse advertising.
  if (!ble_started) {
    ble_started = true;
    mouse.begin();
  }
  draw_face();
  vibrate(90, 20);
}
void release_sent_buttons() {
  if (ble_started && mouse.isConnected()) {
    if (left_sent) mouse.release(MOUSE_LEFT);
    if (right_sent) mouse.release(MOUSE_RIGHT);
  }
  left_sent = right_sent = false;
}
void sync_buttons(bool connected) {
  if (!connected) { left_sent = right_sent = false; return; }
  const bool a = M5.BtnA.isPressed(), b = M5.BtnB.isPressed();
  // Require releasing the boot-menu selection key before any HID click.
  if (!buttons_armed) { if (!a && !b) buttons_armed = true; return; }
  const bool left = right_handed ? a : b;
  const bool right = right_handed ? b : a;
  if (left != left_sent) {
    if (left) mouse.press(MOUSE_LEFT); else mouse.release(MOUSE_LEFT);
    left_sent = left;
    if (left) vibrate();
  }
  if (right != right_sent) {
    if (right) mouse.press(MOUSE_RIGHT); else mouse.release(MOUSE_RIGHT);
    right_sent = right;
    if (right) vibrate();
  }
}
void click_primary() {
  if (!ble_started || !mouse.isConnected() || !buttons_armed) return;
  mouse.click(right_handed ? MOUSE_LEFT : MOUSE_RIGHT);
  vibrate(100, 20);
}
void handle_touch_active(bool connected, bool down, int x, int y) {
  const uint32_t now = millis();
  if (down && !touch_prev) {
    touch_active = mousewatch::is_inside_circle(x,y,cx(),cy());
    touch_is_ring = ring_enabled && touch_active && mousewatch::is_ring_start(x,y,cx(),cy());
    if (touch_is_ring) ring.begin(x,y,cx(),cy());
    touch_start_ms = now;
    start_x = last_x = x;
    start_y = last_y = y;
    x_remainder = y_remainder = 0.f;
  } else if (down && touch_prev && touch_active) {
    if (connected) {
      if (touch_is_ring) {
        const int steps = ring.update(x, y, cx(), cy());
        if (steps) mouse.move(0, 0, static_cast<int8_t>(clamp_report(reverse_scroll ? steps : -steps)));
      } else {
        // Direct touch X/Y mapping. NEVER rotate display or cursor by 90 degrees.
        const float gain = 0.9f + 0.15f * pointer_speed;
        const auto delta = mousewatch::touch_to_hid((x-last_x)*gain, (y-last_y)*gain);
        x_remainder += delta.x;
        y_remainder += delta.y;
        const int dx = clamp_report(static_cast<int>(x_remainder));
        const int dy = clamp_report(static_cast<int>(y_remainder));
        x_remainder -= dx;
        y_remainder -= dy;
        if (dx || dy) mouse.move(static_cast<int8_t>(dx), static_cast<int8_t>(dy));
      }
    }
    last_x = x;
    last_y = y;
  } else if (!down && touch_prev) {
    if (touch_active && !touch_is_ring) {
      const int dx = last_x-start_x, dy = last_y-start_y;
      if (dx*dx + dy*dy <= TAP_MAX_DIST*TAP_MAX_DIST &&
          now-touch_start_ms <= TAP_MAX_MS) click_primary();
    }
    touch_active = touch_is_ring = false;
    x_remainder = y_remainder = 0.f;
  }
  touch_prev = down;
}
// Only entered after a one-shot NVS USB request, consumed on the next boot.
// The explicit USB DRIVE selection authorizes first-use FAT ONLY if the entire
// storage partition is blank (0xFF), never if any previous data exists.
void draw_usb_mode() {
  menu_base("USB DRIVE");
  if (usb_disk.ejected()) {
    label("SAFE TO REBOOT", 190, kFaceGreen);
    label("BUTTON: REBOOT", 270, kFaceWhite);
  } else if (usb_disk.failed()) {
    label("USB TRANSFER ERROR", 190, kFaceOrange);
    label("EJECT ON PC", 250, kFaceWhite);
  } else if (usb_disk.host_seen()) {
    label("USB DATA ACTIVE", 173, kFaceGreen);
    label("COPY JPG / PNG", 246, kFaceWhite);
    label("EJECT ON PC", 317, kFaceCyan);
  } else if (usb_disk.host_enumerated()) {
    label("USB LINK READY", 173, kFaceGreen);
    label("OPEN THIS PC", 246, kFaceWhite);
    label("COPY JPG / PNG", 317, kFaceCyan);
  } else {
    label("WAITING FOR PC", 173, kFaceCyan);
    label("USB DATA CABLE", 246, kFaceWhite);
    label("CONNECT TO PC", 317, kFaceGrey);
  }
  usb_status_prev = usb_disk.ejected()?4:usb_disk.failed()?3:usb_disk.host_seen()?2:usb_disk.host_enumerated()?1:0;
}
// Called during setup after a user-requested warm reboot: native TinyUSB
// enumerates on a fresh boot, not by replacing the active Serial/JTAG link.
void start_usb_at_boot() {
  menu_base("USB DRIVE");
  label("PREPARING STORAGE", 218, kFaceCyan);
  // Only a totally erased storage partition may be formatted automatically
  // on the user's explicit USB DRIVE selection. Never overwrite unknown data.
  if (!faces.mounted() && faces.is_blank_storage()) {
    label("FORMATTING NEW FAT16", 280, kFaceWhite);
    if (!faces.initialize_blank_volume()) {
      screen=Screen::USBError;
      menu_base("USB DRIVE");
      label("FAT16 FORMAT FAILED", 190, kFaceOrange);
      label("BUTTON: BACK", 300, kFaceWhite);
      return;
    }
  }
  if (usb_disk.start(faces)) {
    screen=Screen::USBMode;
    draw_usb_mode();
  } else {
    screen=Screen::USBError;
    menu_base("USB DRIVE");
    label("USB NOT AVAILABLE", 180, kFaceOrange);
    label(usb_disk.error_text(), 249, kFaceWhite);
    label("BUTTON: BACK", 323, kFaceCyan);
  }
}
// User taps USB DRIVE, firmware reboots ONCE into its dedicated USB mass
// storage personality. Ordinary boot/COM16 and mouse runtime stay unchanged.
void enter_usb_mode() {
  // VinylM5's proven one-shot NVS request: survives ESP.restart(), unlike
  // undocumented retention of temporary .noinit RAM across USB reset modes.
  save_settings();
  if (!prefs_available || pref.putUChar("usbver", kUsbRequestVersion) != 1 ||
      pref.putBool("usbreq", true) != 1) {
    menu_base("USB DRIVE");
    label("USB REQUEST FAILED", 211, kFaceOrange);
    label("BUTTON: MOUSE", 295, kFaceWhite);
    screen=Screen::USBError;
    return;
  }
  menu_base("USB DRIVE");
  label("SWITCHING TO USB", 210, kFaceCyan);
  label("PLEASE WAIT", 280, kFaceWhite);
  delay(180);
  ESP.restart();
}
void handle_boot_or_settings(bool down, int x, int y) {
  if (down && !touch_prev) {
    ui_press_x = x;
    ui_press_y = y;
    ui_last_x = x; ui_last_y = y;
    ui_press_ms = millis();
    // Only a NEW touch beginning inside the narrow USB row may arm USB.
    // The BACK row and held touches inherited from another menu never arm it.
    usb_row_armed = screen == Screen::WatchFaces &&
      mousewatch::watch_face_hit(x,y,cx()) == mousewatch::WatchFaceAction::USBDrive;
  } else if (down && touch_prev && x >= 0 && y >= 0) {
    ui_last_x = x; ui_last_y = y;
  }
  if (screen == Screen::USBMode) {
    // Exit only AFTER the PC ejects the drive, to avoid corrupting its FAT.
    if (usb_disk.ejected() && (M5.BtnA.wasPressed() || M5.BtnB.wasPressed())) {
      ESP.restart();  // New boot: mouse/face starts normally, FAT is remounted.
    }
    const int status = usb_disk.ejected()?4:usb_disk.failed()?3:usb_disk.host_seen()?2:usb_disk.host_enumerated()?1:0;
    if (status != usb_status_prev) draw_usb_mode();
    touch_prev = down;
    return;
  }
  if (screen == Screen::USBError && (M5.BtnA.wasPressed() || M5.BtnB.wasPressed())) {
    screen = Screen::WatchFaces;
    draw_watch_faces();
    touch_prev = down;
    return;
  }
  // Physical buttons start immediately on boot, without entering any menus.
  if (screen == Screen::Boot && (M5.BtnA.wasPressed() || M5.BtnB.wasPressed())) {
    enter_active();
    return;
  }
  // Physical buttons also provide a shortcut to start from any settings page.
  if (screen != Screen::Boot && (M5.BtnA.wasPressed() || M5.BtnB.wasPressed())) {
    save_settings(); enter_active(); return;
  }
  // Some StopWatch touch-driver revisions can omit the release pulse.
  // Keep hold-to-select ONLY when the new gesture started on the actual USB
  // text, remains inside that same strict hitbox, and is stable for 250ms.
  // BACK cannot trigger this path, even if a release point is stale.
  if (screen == Screen::WatchFaces && usb_row_armed && down && touch_prev &&
      millis()-ui_press_ms >= 250 && millis()-ui_press_ms < 850 &&
      mousewatch::watch_face_hit(ui_last_x,ui_last_y,cx()) == mousewatch::WatchFaceAction::USBDrive) {
    const int dx=ui_last_x-ui_press_x, dy=ui_last_y-ui_press_y;
    if (dx*dx+dy*dy <= TAP_MAX_DIST*TAP_MAX_DIST) {
      usb_row_armed = false;
      enter_usb_mode();
      touch_prev=down;
      return;
    }
  }
  // Ignore swipes in the menus. Short taps are handled on release.
  if (!down && touch_prev && millis()-ui_press_ms <= TAP_MAX_MS) {
    // The touch driver may reset the point on release; use the last held point.
    const int dx=ui_last_x-ui_press_x, dy=ui_last_y-ui_press_y;
    const bool stable = (dx*dx+dy*dy <= TAP_MAX_DIST*TAP_MAX_DIST);
    if (stable) {
      if (screen == Screen::Boot) {
        const int ox=ui_press_x-cx(), oy=ui_press_y-cy();
        if (ox*ox + oy*oy <= 90*90) {
          screen = Screen::Settings;
          draw_settings();
        }
      } else if (screen == Screen::Settings) {
        if (ui_press_y >= 130 && ui_press_y < 208) {
          screen=Screen::WatchFaces; draw_watch_faces();
        } else if (ui_press_y >= 208 && ui_press_y < 287) {
          screen=Screen::MouseSettings; draw_mouse_settings();
        } else if (ui_press_y >= 287 && ui_press_y < 370) {
          save_settings(); enter_active();
        }
      } else if (screen == Screen::WatchFaces) {
        const auto action=mousewatch::watch_face_hit(ui_press_x,ui_press_y,cx());
        if (action==mousewatch::WatchFaceAction::SingleImage) {
          screen=Screen::ImagePicker;
          draw_image_picker();
        } else if (action==mousewatch::WatchFaceAction::CatClock) {
          selected_face=Face::ClockCat;save_settings();
          draw_watch_faces();
        } else if (action==mousewatch::WatchFaceAction::USBDrive && usb_row_armed &&
                   mousewatch::watch_face_hit(ui_last_x,ui_last_y,cx())==mousewatch::WatchFaceAction::USBDrive) {
          usb_row_armed = false;
          enter_usb_mode();
        } else if (action==mousewatch::WatchFaceAction::Back) {
          // BACK ONLY navigates. It never touches NVS USB keys or restarts.
          usb_row_armed = false;
          screen=Screen::Settings;draw_settings();
        }
      } else if (screen == Screen::MouseSettings) {
        if (ui_press_y >= 112 && ui_press_y < 175) right_handed = !right_handed;
        else if (ui_press_y >= 175 && ui_press_y < 242) pointer_speed = pointer_speed == 10 ? 1 : pointer_speed + 1;
        else if (ui_press_y >= 242 && ui_press_y < 312) ring_enabled = !ring_enabled;
        else if (ui_press_y >= 312 && ui_press_y < 376) reverse_scroll = !reverse_scroll;
        else if (ui_press_y >= 376 && ui_press_y < 439) { screen=Screen::Settings; draw_settings(); }
        save_settings();
        if (screen == Screen::MouseSettings) draw_mouse_settings();
      } else if (screen == Screen::ImagePicker) {
        if (ui_press_y >= 245 && ui_press_y < 315 && faces.count()) {
          if (ui_press_x < cx()) selected_image=(selected_image+faces.count()-1)%faces.count();
          else selected_image=(selected_image+1)%faces.count();
          draw_image_picker();
        } else if (ui_press_y >= 315 && ui_press_y < 379 && faces.count()) {
          selected_face=Face::Image;
          snprintf(selected_image_name,sizeof(selected_image_name),"%s",faces.filename(selected_image%faces.count()));
          save_settings();
          menu_base("SINGLE IMAGE");
          label("PREPARING IMAGE",218,kFaceCyan);
          label("SAVING TO FLASH",278,kFaceWhite);
          const bool cache_ready=faces.prepare_image(M5.Display,selected_image);
          if(!cache_ready) M5.Log.printf("MouseWatchM5: image cache unavailable; original decode fallback.\n");
          screen=Screen::WatchFaces; draw_watch_faces();
        } else if (ui_press_y >= 379 && ui_press_y < 440) {
          screen=Screen::WatchFaces; draw_watch_faces();
        }
      }
    }
  }
  if(!down) usb_row_armed=false;
  touch_prev = down;
}
} // namespace

static uint8_t compiled_month(const char* m){
  static const char* const names[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
  for(int i=0;i<12;i++)if(!strncmp(m,names[i],3))return uint8_t(i+1);
  return 1;
}
static void ensure_rtc_initialized() {
  if(!M5.Rtc.isEnabled())return;
  const auto d=M5.Rtc.getDateTime();
  // Preserve any plausible clock: never set RTC merely because firmware updated.
  if(d.date.year>=2024&&d.date.year<=2099&&d.time.hours<24&&d.time.minutes<60) return;
  const char* date=__DATE__;const char* stamp=__TIME__;
  const int yr=atoi(date+7),mon=compiled_month(date),day=atoi(date+4);
  const int hh=atoi(stamp),mm=atoi(stamp+3),ss=atoi(stamp+6);
  M5.Rtc.setDateTime({{yr,mon,day},{hh,mm,ss}});
}
void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.clear_display = true;
  cfg.internal_imu = true;
  cfg.internal_rtc = true;
  cfg.internal_spk = true;
  cfg.internal_mic = true;
  M5.begin(cfg); // ONE initialization. No GPIO46/panel reset/PMIC direct manipulation.
  M5.Display.setRotation(0); // Screen NEVER rotates to compensate cursor.
  M5.Display.setBrightness(180);
  ensure_rtc_initialized();
  if (!display_ready()) {
    M5.Display.fillScreen(kFaceBlack);
    label("DISPLAY NOT READY", cy(), kFaceOrange);
    M5.Log.printf("MouseWatchM5: display geometry invalid: %dx%d\n",
                  M5.Display.width(),M5.Display.height());
    return;
  }
  prefs_available = pref.begin("mousewatch", false);
  bool requested_usb = false;
  if (prefs_available) {
    // Use exactly VinylM5's reboot-handoff method. Consume the request before
    // starting USB, so unplugging or powering off cannot trap the user in USB.
    requested_usb = pref.getUChar("usbver", 0) == kUsbRequestVersion &&
                    pref.getBool("usbreq", false);
    // Consume on ANY boot: a stale request can never be re-played by BACK.
    if(pref.getBool("usbreq",false)) pref.putBool("usbreq", false);
    right_handed = pref.getBool("right", true);
    pointer_speed = pref.getUChar("speed", 5);
    reverse_scroll = pref.getBool("reverse", false);
    ring_enabled = pref.getBool("ring", true);
    const uint8_t old_face = pref.getUChar("face", 2);
    // FIX10 migration: old Slide Show (1) always becomes Single Image (2).
    // Old slide_on / slideshow-second keys are ignored, never run.
    selected_face = old_face==0 ? Face::ClockCat : Face::Image;
    selected_image = pref.getUChar("image", 0);
    String chosen=pref.getString("imgName","");
    snprintf(selected_image_name,sizeof(selected_image_name),"%s",chosen.c_str());
    if (pointer_speed < 1 || pointer_speed > 10) pointer_speed = 5;
  }
  // USB mode does not start BLE HID and only runs after a user menu request.
  faces.begin(); // Existing FAT only; explicit new-blank initialization in USB mode.
  // USB copying may reorder FAT directory entries; restore by filename, not index.
  if(faces.count()) {
    if(selected_image_name[0]) {
      for(uint8_t i=0;i<faces.count();i++)
        if(!strcmp(faces.filename(i),selected_image_name)){selected_image=i;break;}
    }
    if(selected_image>=faces.count())selected_image=0;
  }
  if (requested_usb) start_usb_at_boot();
  else draw_boot();
  M5.Log.printf("MouseWatchM5 R2 FIX13: USB request=%d, 1-shot NVS, rotation=0.\n", requested_usb);
}

void loop() {
  M5.update();
  if (vibration_end_ms && (int32_t)(millis() - vibration_end_ms) >= 0) {
    M5.Power.setVibration(0);
    vibration_end_ms = 0;
  }
  if (!display_ready()) { delay(100); return; }
  const bool down = M5.Touch.isEnabled() && M5.Touch.getDetail().isPressed();
  const int x = M5.Touch.isEnabled() ? M5.Touch.getDetail().x : -1;
  const int y = M5.Touch.isEnabled() ? M5.Touch.getDetail().y : -1;
  if (screen != Screen::Active) {
    handle_boot_or_settings(down, x, y);
  } else {
    const bool connected = mouse.isConnected();
    if (connected != connected_prev) {
      release_sent_buttons();
      connected_prev = connected;
      buttons_armed = false;
      M5.Log.printf("MouseWatchM5 BLE: %s\n", connected ? "CONNECTED" : "DISCONNECTED");
      // Restore a readable connection banner over both image and clock faces.
      if(selected_face==Face::ClockCat)cat_clock.setConnected(connected);
      else draw_ble_overlay();
    }
    sync_buttons(connected);
    handle_touch_active(connected, down, x, y);
    // No slideshow and no image re-decoding. Core0 prepares only Clock Cat
    // pixels; core1 commits one completed PSRAM sprite to the panel.
    if(selected_face==Face::ClockCat && cat_clock.enabled()) {
      read_clock_rtc();
      cat_clock.present(); // already contains both status banners in the same PSRAM frame
    }
  }
  delay(12);
}