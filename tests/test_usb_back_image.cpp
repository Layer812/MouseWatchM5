// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Layer812
#include "../src/ui_hitbox.hpp"
#include "../src/image_cache_format.hpp"
#include <cassert>
#include <cstdio>
int main(){
 using namespace mousewatch;
 const int c=234;
 assert(watch_face_hit(c,349,c)==WatchFaceAction::USBDrive);
 assert(watch_face_hit(c,332,c)==WatchFaceAction::USBDrive);
 assert(watch_face_hit(c,371,c)==WatchFaceAction::None);
 assert(watch_face_hit(c,385,c)==WatchFaceAction::None);
 assert(watch_face_hit(c,390,c)==WatchFaceAction::None);
 assert(watch_face_hit(c,410,c)==WatchFaceAction::Back);
 assert(watch_face_hit(c,450,c)==WatchFaceAction::Back);
 assert(watch_face_hit(10,350,c)==WatchFaceAction::None);
 assert(watch_face_hit(c,284,c)==WatchFaceAction::CatClock);
 assert(watch_face_hit(c,213,c)==WatchFaceAction::SingleImage);
 assert(watch_face_hit(-1,-1,c)==WatchFaceAction::None);
 assert(kFaceCacheVersion==2);
 FaceCacheHeader h{};
 h.magic=kFaceCacheMagic;h.version=1;h.width=h.height=468;
 h.payload_bytes=468*468*2;h.source_bytes=100;h.source_stamp=20;h.source_cluster=11;
 std::snprintf(h.source_name,sizeof(h.source_name),"PHOTO.JPG");
 assert(!cache_header_valid(h,"PHOTO.JPG",100,20,11,468,468));
 h.version=2;
 assert(cache_header_valid(h,"PHOTO.JPG",100,20,11,468,468));
 assert(cover_zoom(1920,1080,468,468)>0.4332f);
 assert(cover_zoom(1080,1920,468,468)>0.4332f);
 puts("PASS: USB/BACK separated, v1 cache invalid, cover geometry");
}
