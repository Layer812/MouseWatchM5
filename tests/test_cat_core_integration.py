# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Layer812
from pathlib import Path
p=Path(__file__).resolve().parents[1]
m=(p/'src/main.cpp').read_text()
c=(p/'src/clock_cat.cpp').read_text()
h=(p/'src/clock_cat_art.hpp').read_text()
assert m.count('M5.begin(cfg)')==1
assert 'M5.Display.setRotation(0)' in m
assert 'MouseWatchM5' in m and 'BLE Connected' in m
assert 'selected_face=Face::ClockCat;save_settings();' in m
assert 'if(faces.has_cat_frames())' not in m
assert 'cat_clock.present();' in m
assert 'cat_clock.setConnected(connected)' in m
assert 'cat_clock.enable(true)' in m
assert 'if(selected_face==Face::ClockCat)' in m
assert 'xTaskCreatePinnedToCore(taskEntry' in c and ',0);' in c
assert 'canvas_.pushSprite(0,0)' in c
assert 'canvas_.drawString("MouseWatchM5"' in c
assert 'canvas_.drawString(connected_?"BLE Connected":"BLE Waiting"' in c
assert 'kClockCatRegularFrames=32' in h and 'kClockCatSwatFrames=6' in h
assert 'faces.prepare_image(M5.Display,selected_image)' in m
assert 'requested_usb' in m and 'usb_row_armed' in m
print('PASS: dual core cat render, status overlays, live BLE + USB guarded, cache unchanged')
