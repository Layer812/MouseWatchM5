// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Layer812

// R9 HTML companion sprite assets: 32 regular + six corrected swat poses.
// Bit 7 = first pixel. Transparent background, black silhouettes, 160x160.
#pragma once
#include <stdint.h>
namespace mousewatch {
static constexpr int kClockCatArtWidth=160;
static constexpr int kClockCatArtHeight=160;
static constexpr int kClockCatArtStride=3200;
static constexpr int kClockCatRegularFrames=32;
static constexpr int kClockCatSwatFrames=6;
extern const uint8_t kClockCatArt[38][kClockCatArtStride];
}