// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Layer812

#pragma once
#include <M5Unified.h>
#include <wear_levelling.h>
#include <cstdint>
#include <cstddef>
#include "raw_fat16.hpp"
namespace mousewatch {
enum class StorageLayout : uint8_t { None, RawFat16, WearLevelFat };
class FaceStorage {
 public:
  bool begin();
  void end();
  bool initialize_blank_volume(); // explicit consent, only after complete blank check
  bool is_blank_storage() const;
  StorageLayout layout() const { return layout_; }
  StorageLayout usb_layout() const; // allow validated raw FAT16 without local VFS mount
  uint8_t count() const { return image_count_; }
  const char* filename(uint8_t index) const;
  bool show(M5GFX& screen, uint8_t index);
  // Make one display-sized RGB565 image in the existing FAT16 storage.
  // Original JPEG/PNG files remain unchanged, and USB owns the volume exclusively.
  bool prepare_image(M5GFX& screen, uint8_t index);
  bool show_cat_frame(M5GFX& screen, uint8_t frame);
  bool has_cat_frames() const { return cat_frames_; }
  bool mounted() const { return mounted_; }
  // The FAT volume and USB block device must never be active simultaneously.
  const char* partition_label() const { return "storage"; }
 private:
  bool scan_images();
  static bool raw_read(void*,uint32_t,void*,uint32_t);
  bool show_bytes(M5GFX& screen, const uint8_t* data, uint32_t size, const char* name);
  bool show_file(M5GFX& screen, const char* filename);
  bool cache_matches(uint8_t index, int width, int height) const;
  bool show_cached(M5GFX& screen, uint8_t index);
  bool can_cache() const;
  bool mounted_ = false;
  bool cat_frames_ = false;
  uint8_t image_count_ = 0;
  char files_[48][96]{};
  RawImageEntry raw_files_[48]{};
  RawFat16 raw_reader_;
  wl_handle_t wl_ = WL_INVALID_HANDLE;
  StorageLayout layout_ = StorageLayout::None;
};
} // namespace mousewatch