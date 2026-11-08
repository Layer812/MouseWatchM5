// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Layer812

// Raw FAT16 512-byte sector reader. Never writes or formats flash.
// The USB Drive (MSC) and local image reader use the same disk layout.
#pragma once
#include <stdint.h>
#include <stddef.h>
namespace mousewatch {
struct RawImageEntry {
  char name[96]{}; // UTF-8 display name (Windows long filename when present)
  uint16_t first_cluster=0;
  uint32_t size=0;
  uint32_t modified=0; // FAT date/time: changes on PC-side replacement
};
using RawReadFn=bool(*)(void*,uint32_t,void*,uint32_t);
class RawFat16 {
 public:
  bool begin(void* context, RawReadFn reader, uint32_t size_bytes);
  uint8_t scan(RawImageEntry* files,uint8_t cap) const;
  bool load(const RawImageEntry& file, uint8_t* target, uint32_t capacity) const;
  bool valid() const {return ok_;}
 private:
  bool rd(uint64_t byte,void* dst,uint32_t len) const;
  bool next(uint16_t from,uint16_t& to) const;
  void* context_=nullptr;
  RawReadFn read_=nullptr;
  uint32_t capacity_=0, first_fat_=0, root_start_=0, data_start_=0, total_sectors_=0;
  uint16_t root_entries_=0, cluster_limit_=0;
  uint8_t sectors_per_cluster_=0;
  bool ok_=false;
};
} // namespace mousewatch