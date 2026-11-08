// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Layer812
// Compile on host: g++ -std=c++17 -Isrc tests/test_touch_cursor.cpp -o test_touch_cursor
#include "cursor_mapping.hpp"
#include <cassert>
#include <cstdio>
int main() {
  const auto right = mousewatch::touch_to_hid(+20.f, 0.f);
  const auto left  = mousewatch::touch_to_hid(-20.f, 0.f);
  const auto up    = mousewatch::touch_to_hid(0.f, -20.f);
  const auto down  = mousewatch::touch_to_hid(0.f, +20.f);
  assert(right.x == 20.f && right.y == 0.f);
  assert(left.x == -20.f && left.y == 0.f);
  assert(up.x == 0.f && up.y == -20.f);
  assert(down.x == 0.f && down.y == 20.f);
  // Regression for FIX4: swipe right MUST NOT produce vertical movement.
  assert(right.y == 0.f && left.y == 0.f);
  std::puts("PASS: touch XY -> HID XY, four directions, no 90-degree rotation");
}
