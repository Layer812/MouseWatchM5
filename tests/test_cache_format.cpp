// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Layer812
#include "../src/image_cache_format.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
int main() {
 using namespace mousewatch;
 assert(cover_zoom(1920,1080,468,468) > .4332f);
 assert(cover_zoom(1920,1080,468,468) < .4334f);
 assert(cover_zoom(1080,1920,468,468) > .4332f);
 assert(cover_zoom(600,600,468,468) == .78f);
 assert(cover_zoom(0,100,468,468)==0);
 FaceCacheHeader h{};
 h.magic=kFaceCacheMagic;h.version=kFaceCacheVersion;h.width=468;h.height=468;
 h.source_bytes=501234;h.source_stamp=0x5aa00123;h.source_cluster=21;
 h.payload_bytes=468*468*2;
 std::strcpy(h.source_name,"My Photo.JPG");
 assert(cache_header_valid(h,"My Photo.JPG",501234,0x5aa00123,21,468,468));
 assert(!cache_header_valid(h,"Other.JPG",501234,0x5aa00123,21,468,468));
 assert(!cache_header_valid(h,"My Photo.JPG",501234,0x5aa00124,21,468,468));
 assert(!cache_header_valid(h,"My Photo.JPG",501234,0x5aa00123,21,466,466));
 h.payload_bytes--;
 assert(!cache_header_valid(h,"My Photo.JPG",501234,0x5aa00123,21,468,468));
 const uint8_t b[]={1,2,3,4};
 assert(cache_hash(2166136261u,b,4)==1463068797u);
 puts("PASS: cover geometry, cache header invalidation, integrity hash");
}
