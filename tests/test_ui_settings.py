# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Layer812
from pathlib import Path
root=Path(__file__).resolve().parents[1]
s=(root/'src/main.cpp').read_text(encoding='utf-8')
f=(root/'src/face_storage.cpp').read_text(encoding='utf-8')
assert 'SLIDE SHOW' not in s
assert 'Face::SlideShow' not in s
assert 'slideshow_enabled' not in s
assert 'slideshow_seconds' not in s
assert 'selected_face = old_face==0 ? Face::ClockCat : Face::Image' in s
assert 'SINGLE IMAGE' in s and '[SET]' in s
assert 'screen=Screen::ImagePicker;' in s
assert 'visible_filename(filename,sizeof(filename)' in s
assert 'pref.putString("imgName",selected_image_name)' in s
assert 'draw_pointer_guides();' in s
assert 'label("TOUCH TO MOVE"' not in s
assert 'if (is_raw_fat16()) {' in f and 'raw_reader_.scan(raw_files_,48);' in f
assert 'screen.drawPng(' in f and 'screen.drawJpg(' in f
assert 'MWCACHE.BIN' in f and 'MWTEMP.BIN' in f
assert 'faces.prepare_image(M5.Display,selected_image)' in s
assert 'canvas.drawPng(' in f and 'canvas.drawJpg(' in f
assert 'screen.drawPng(data,sz,0,0,screen.width(),screen.height()' in f
assert 'usb_disk.start(faces)' in s
assert 'if (requested_usb) start_usb_at_boot()' in s
print('PASS: single-image mode; flash RGB565 cache; USB and mouse preserved')
