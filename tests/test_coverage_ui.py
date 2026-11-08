# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Layer812
from pathlib import Path
s=(Path(__file__).parents[1]/'src/main.cpp').read_text()
f=(Path(__file__).parents[1]/'src/face_storage.cpp').read_text()
assert 'usb_row_armed = screen == Screen::WatchFaces' in s
assert 'mousewatch::WatchFaceAction::Back' in s
assert 'screen=Screen::Settings;draw_settings();' in s
assert s.count('enter_usb_mode();')==2
assert 'if(pref.getBool("usbreq",false)) pref.putBool("usbreq", false);' in s
assert 'canvas.drawJpg(source,source_size,0,0,w,h,0,0,zoom,zoom,middle_center)' in f
assert 'canvas.drawPng(source,source_size,0,0,w,h,0,0,zoom,zoom,middle_center)' in f
assert 'screen.drawJpg(data,sz,0,0,screen.width(),screen.height(),0,0,scale,scale,middle_center)' in f
assert 'screen.drawPng(data,sz,0,0,screen.width(),screen.height(),0,0,scale,scale,middle_center)' in f
assert 'kFaceCacheVersion = 2' in (Path(__file__).parents[1]/'src/image_cache_format.hpp').read_text()
assert 'USBDrive::start(FaceStorage& faces)' in (Path(__file__).parents[1]/'src/usb_drive.cpp').read_text()
print('PASS: guarded USB route; full-screen cover; v2 flash cache; USB code retained')
