// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Layer812

#pragma once
#include <USB.h>
#include <USBMSC.h>
#include <wear_levelling.h>
#include <cstdint>
#include <atomic>
namespace mousewatch {
class FaceStorage;
enum class USBError : uint8_t {
  None, NotMounted, Partition, WearLevel, Sector, Capacity, Msc, Usb, UnknownFormat
};
class USBDrive {
 public:
  bool start(FaceStorage& faces); // exclusive FAT ownership; raw FAT16 or legacy WL, never format
  bool ready() const { return ready_.load(); }
  bool host_seen() const { return host_seen_.load(); }
  bool ejected() const { return ejected_.load(); }
  bool failed() const { return failed_.load(); }
  bool host_enumerated() const { return static_cast<bool>(USB); }
  const char* error_text() const;
 private:
  static int32_t read_cb(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize);
  static int32_t write_cb(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize);
  static bool start_stop_cb(uint8_t power_condition, bool start, bool load_eject);
  static USBDrive* current_;
  USBMSC msc_;
  USBError error_ = USBError::None;
  wl_handle_t wl_ = WL_INVALID_HANDLE;
  uint32_t count_ = 0;
  uint32_t sector_ = 512;
  uint32_t block_bytes_ = 512;
  bool raw_ = false;
  const esp_partition_t* part_ = nullptr;
  std::atomic<bool> ready_{false};
  std::atomic<bool> host_seen_{false};
  std::atomic<bool> failed_{false};
  std::atomic<bool> ejected_{false};
};
} // namespace mousewatch