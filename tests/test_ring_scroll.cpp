// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Layer812
// Host-only regression tests; g++ -std=c++17 -Isrc tests/test_ring_scroll.cpp -o test_ring
#include "ring_scroll.hpp"
#include "cursor_mapping.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
int main() {
  using namespace mousewatch;
  assert(is_ring_start(233, 6, 233, 233));
  assert(!is_ring_start(233, 233, 233, 233));
  assert(!is_inside_circle(0, 0, 233, 233));
  // Clockwise in screen coordinates: right -> bottom => positive angle => negative HID wheel.
  RingScroll cw;
  cw.begin(333,233,233,233);
  int ticks = cw.update(304,304,233,233);
  ticks += cw.update(233,333,233,233);
  assert(ticks > 0);
  int hid_wheel = -ticks;
  assert(hid_wheel < 0);
  // Anticlockwise: right -> top => scroll up.
  RingScroll ccw;
  ccw.begin(333,233,233,233);
  const int up_ticks = ccw.update(304,162,233,233) + ccw.update(233,133,233,233);
  assert(up_ticks < 0);
  // Correctly wraps -pi -> +pi without a giant scroll jump.
  RingScroll wrap;
  wrap.begin(133,232,233,233);
  assert(std::abs(wrap.update(133,234,233,233)) <= 1);
  // Central touch keeps identity XY mapping after FIX5.
  auto r = touch_to_hid(20.f,0.f);
  assert(r.x == 20.f && r.y == 0.f);
  std::puts("PASS: circular ring CW/down CCW/up, seam wrapping, no XY rotation");
}
