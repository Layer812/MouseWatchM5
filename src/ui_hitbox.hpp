// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Layer812

#pragma once
namespace mousewatch {
enum class WatchFaceAction { None, SingleImage, CatClock, USBDrive, Back };
inline WatchFaceAction watch_face_hit(int x,int y,int center_x) {
  // Strict, NONOVERLAPPING hit rectangles. USB is intentionally separated
  // from BACK by more than 20px and requires the touch inside the text region.
  if (x < 0 || y < 0) return WatchFaceAction::None;
  if (y>=391 && y<=451 && x>=center_x-135 && x<=center_x+135)
    return WatchFaceAction::Back;
  if (y>=332 && y<=370 && x>=center_x-128 && x<=center_x+128)
    return WatchFaceAction::USBDrive;
  if (y>=252 && y<=311 && x>=center_x-155 && x<=center_x+155)
    return WatchFaceAction::CatClock;
  if (y>=124 && y<=240 && x>=center_x-168 && x<=center_x+168)
    return WatchFaceAction::SingleImage;
  return WatchFaceAction::None;
}
} // namespace mousewatch