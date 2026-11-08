// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Layer812

#pragma once
#include <cmath>

namespace mousewatch {
constexpr float kPi = 3.14159265358979323846f;
// Coordinates are M5Unified display-space (setRotation(0)), y grows downwards.
// Positive angular motion means clockwise on the displayed circle.
struct RingScroll {
  float last_angle = 0.f;
  float remainder = 0.f;

  void begin(float x, float y, float cx, float cy) {
    last_angle = std::atan2(y - cy, x - cx);
    remainder = 0.f;
  }
  int update(float x, float y, float cx, float cy, float radians_per_step = 0.20f) {
    const float angle = std::atan2(y - cy, x - cx);
    float delta = angle - last_angle;
    last_angle = angle;
    if (delta > kPi) delta -= 2.f * kPi;
    if (delta < -kPi) delta += 2.f * kPi;
    // Sudden discontinuities are likely to be bad samples, not a real finger movement.
    if (std::fabs(delta) > 1.25f || radians_per_step <= 0.f) return 0;
    remainder += delta;
    int steps = static_cast<int>(remainder / radians_per_step);
    if (steps > 8) steps = 8;
    if (steps < -8) steps = -8;
    remainder -= steps * radians_per_step;
    return steps; // clockwise > 0; pass -steps to HID wheel for scroll-down
  }
};

inline bool is_ring_start(int x, int y, int cx, int cy, int inner_radius = 174, int outer_radius = 234) {
  const int dx = x - cx, dy = y - cy;
  const int r2 = dx * dx + dy * dy;
  return r2 >= inner_radius * inner_radius && r2 <= outer_radius * outer_radius;
}
inline bool is_inside_circle(int x, int y, int cx, int cy, int radius = 234) {
  const int dx = x - cx, dy = y - cy;
  return dx * dx + dy * dy <= radius * radius;
}
} // namespace mousewatch