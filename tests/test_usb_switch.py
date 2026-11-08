# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Layer812
from pathlib import Path
p=Path(__file__).parents[1]/"src"/"main.cpp"
s=p.read_text()
assert 'mousewatch_usb_boot_magic' not in s
assert 'pref.putBool("usbreq", true)' in s
assert 'pref.getBool("usbreq", false)' in s
assert 'pref.putBool("usbreq", false)' in s
assert 'usb_row_armed' in s and 'watch_face_hit(ui_press_x,ui_press_y,cx())' in s
assert 'if (requested_usb) start_usb_at_boot()' in s
assert 'else draw_boot()' in s
assert 'mouse.begin()' in s
assert 'M5.Display.setRotation(0)' in s
usb=(Path(__file__).parents[1]/"src"/"usb_drive.cpp").read_text()
assert 'faces.usb_layout()' in usb
print('PASS: one-shot USB request, tap, flash protection, no-image mouse')
