// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Layer812

#pragma once
// Screen-space touch deltas are already aligned with the visible AMOLED.
// Send their X/Y signs unchanged to the BLE HID relative mouse report.
namespace mousewatch {
struct CursorDelta { float x, y; };
inline constexpr CursorDelta touch_to_hid(float touch_dx, float touch_dy) {
  return {touch_dx, touch_dy};
}
} // namespace mousewatch