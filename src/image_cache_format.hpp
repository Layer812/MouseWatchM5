// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Layer812

#pragma once
#include <stdint.h>
#include <string.h>
namespace mousewatch {
static constexpr uint32_t kFaceCacheMagic = 0x3146574dUL; // 'MWF1' LE
// Layout algorithm v2: correct image origin to (0,0) before caching.
// A v1 image (offset half-a-screen) must NEVER be reused.
static constexpr uint32_t kFaceCacheVersion = 2;
struct FaceCacheHeader {
  uint32_t magic;
  uint32_t version;
  uint16_t width;
  uint16_t height;
  uint32_t source_bytes;
  uint32_t source_stamp;
  uint16_t source_cluster;
  uint16_t reserved;
  uint32_t payload_bytes;
  uint32_t payload_hash;
  char source_name[96];
};
static_assert(sizeof(FaceCacheHeader) == 128, "Stable cache-header ABI");
inline uint32_t cache_hash(uint32_t h, const uint8_t* data, uint32_t n) {
  for (uint32_t i=0;i<n;i++) {h ^= data[i];h *= 16777619u;}
  return h;
}
inline float cover_zoom(uint32_t srcw, uint32_t srch, uint32_t dstw, uint32_t dsth) {
  if (!srcw||!srch||!dstw||!dsth)return 0;
  const float x=float(dstw)/float(srcw),y=float(dsth)/float(srch);
  return x>y?x:y;
}
inline bool cache_header_valid(const FaceCacheHeader& h,const char* name,
                               uint32_t source_bytes,uint32_t stamp,
                               uint16_t cluster,uint16_t width,uint16_t height) {
  return h.magic==kFaceCacheMagic && h.version==kFaceCacheVersion &&
    h.width==width && h.height==height &&
    h.width>=400 && h.width<=480 && h.height>=400 && h.height<=480 &&
    h.payload_bytes==uint32_t(width)*height*2u &&
    h.source_bytes==source_bytes && h.source_stamp==stamp &&
    h.source_cluster==cluster && name &&
    memchr(h.source_name,0,sizeof(h.source_name)) && !strcmp(name,h.source_name);
}
} // namespace mousewatch