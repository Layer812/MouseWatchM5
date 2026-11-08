# MouseWatchM5 — source code

Copyright (c) 2026 Layer812. Licensed under **GNU GPL-3.0-or-later**; see [LICENSE](LICENSE).

M5Stack StopWatch Bluetooth LE **touchpad mouse** with user-selectable single-image Mouse Face, Cat Clock animation, outer-ring scrolling, and USB MSC for importing JPEG/PNG. This is **not** a gyroscope-controlled air mouse.

This folder is the **R2 FIX13 development source**. It restores the `MouseWatchM5` / `BLE Connected` overlay and adds a Cat Clock implementation using user's Clock Cat R9 stand-alone art, with rendering on CPU0 and BLE/touch/display on CPU1. The firmware has **not been independently built or tested on hardware** in this environment. The StopWatch R3F8/R3F9 original Cat Clock assets are not included.

## Build

- PlatformIO with `espressif32` platform `6.12.0`, Arduino framework.
- M5Unified `0.3.0` + M5GFX `0.3.0`, M5PM1 `1.0.7`, M5IOE1 `1.0.9`, ESP32 BLE Mouse `0.3.1`.
- ESP32-S3R8 StopWatch with 16 MB flash / 8 MB OPI PSRAM.

Run `BUILD_ONLY.cmd` first. For a physical device, `RUN_THIS_MOUSEWATCHM5_APP_ONLY.cmd COM16` uses the live partition table and OTA selection to update **only the active APP slot**. Do not format or erase other partitions to install this source.

## Mouse controls

On the boot screen, press either hardware button to start. Tap the middle of the boot screen for settings. Connect the device named `MouseWatchM5` in Bluetooth settings. Move the pointer with touch gestures, scroll around the circular edge, and click with the side buttons. To import pictures, use Watch Face > USB Drive, safely eject in the OS, then select one JPEG/PNG in Single Image.

## Licensing

This application's authored source is distributed under **GPL-3.0-or-later**, including the Cat Clock R9-derived sprite data here. External dependencies are **not included** and remain under their own terms. See [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
