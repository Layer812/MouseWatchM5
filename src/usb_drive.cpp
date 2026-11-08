// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Layer812

// MouseWatchM5 R2 FIX6 - VinylM5-proven 512-byte raw FAT16 USB storage.
// Legacy WL volumes, if present, are preserved and advertised with their
// actual native logical sector size (never 512 against a 4096-byte FAT BPB).
#include "usb_drive.hpp"
#include "face_storage.hpp"
#include <Arduino.h>
#include <esp_partition.h>
#include <algorithm>
#include <cstring>
namespace mousewatch {
USBDrive* USBDrive::current_=nullptr;
namespace {
constexpr uint32_t kFlashSector=4096;
static uint8_t scratch[kFlashSector];
}
bool USBDrive::start(FaceStorage& faces) {
  if (ready_) return true;
  error_=USBError::None;
  // A validated 512-byte raw FAT16 is usable by Windows even if local FatFS
  // could not mount it. Do not gate USB enumeration on optional image scan.
  const StorageLayout layout=faces.usb_layout();
  if (layout==StorageLayout::None) {error_=USBError::NotMounted;return false;}
  const auto* p=esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT,"storage");
  if (!p || p->size<1024*1024) {error_=USBError::Partition;return false;}
  if (layout!=StorageLayout::RawFat16 && layout!=StorageLayout::WearLevelFat) {
    error_=USBError::UnknownFormat;return false;
  }
  host_seen_=false;ejected_=false;failed_=false;
  faces.end();
  part_=p;
  raw_=(layout==StorageLayout::RawFat16);
  if(raw_) {
    block_bytes_=512;sector_=kFlashSector;
    count_=part_->size/block_bytes_;
  } else {
    // A legacy FAT formatted at 4096-byte WL sector size is not a 512-byte
    // filesystem; report its actual media size to Windows.
    if(wl_mount(p,&wl_)!=ESP_OK){error_=USBError::WearLevel;faces.begin();return false;}
    // MSC logical sector size must match the BPB on the mounted FAT volume.
    // WL's erase granularity can be 4096 while BPB bytes/sector is 512.
    uint8_t boot[512]{};
    if(wl_read(wl_,0,boot,sizeof(boot))!=ESP_OK) {
      error_=USBError::WearLevel;
      wl_unmount(wl_);wl_=WL_INVALID_HANDLE;faces.begin();return false;
    }
    block_bytes_=uint32_t(boot[11])|(uint32_t(boot[12])<<8);
    sector_=wl_sector_size(wl_);
    if ((block_bytes_!=512 && block_bytes_!=4096) ||
        (sector_!=512 && sector_!=4096) || (sector_<block_bytes_)) {
      error_=USBError::Sector;wl_unmount(wl_);wl_=WL_INVALID_HANDLE;faces.begin();return false;
    }
    count_=wl_size(wl_)/block_bytes_;
  }
  if(count_<1024){
    error_=USBError::Capacity;
    if(wl_!=WL_INVALID_HANDLE){wl_unmount(wl_);wl_=WL_INVALID_HANDLE;}
    faces.begin();return false;
  }
  current_=this;
  msc_.vendorID("M5STACK");
  msc_.productID("MOUSEWATCH USB");
  msc_.productRevision("R6");
  msc_.onStartStop(start_stop_cb);
  msc_.onRead(read_cb);
  msc_.onWrite(write_cb);
  msc_.mediaPresent(false);
  if(!msc_.begin(count_,uint16_t(block_bytes_))) {
    error_=USBError::Msc;current_=nullptr;
    if(wl_!=WL_INVALID_HANDLE){wl_unmount(wl_);wl_=WL_INVALID_HANDLE;}
    faces.begin();return false;
  }
  ready_=true;
  if(!USB.begin()) {
    error_=USBError::Usb;ready_=false;msc_.end();current_=nullptr;
    if(wl_!=WL_INVALID_HANDLE){wl_unmount(wl_);wl_=WL_INVALID_HANDLE;}
    faces.begin();return false;
  }
  // As in VinylM5: reveal media only after USB stack is started.
  msc_.mediaPresent(true);
  return true;
}
const char* USBDrive::error_text() const {
  switch(error_) {
    case USBError::None:return "NO ERROR";
    case USBError::NotMounted:return "FAT NOT READY";
    case USBError::Partition:return "STORAGE MISSING";
    case USBError::WearLevel:return "LEGACY FLASH ERROR";
    case USBError::Sector:return "FLASH SECTOR ERROR";
    case USBError::Capacity:return "MEDIA SIZE INVALID";
    case USBError::Msc:return "MSC SETUP FAILED";
    case USBError::Usb:return "USB OTG START ERROR";
    case USBError::UnknownFormat:return "UNKNOWN DISK FORMAT";
  }
  return "USB UNKNOWN ERROR";
}
int32_t USBDrive::read_cb(uint32_t lba,uint32_t offset,void* buffer,uint32_t size) {
  USBDrive* self=current_;
  if (!self || !self->ready_ || self->failed_ || self->ejected_ || !buffer) return -1;
  const uint64_t start=uint64_t(lba)*self->block_bytes_+offset;
  const uint64_t limit=uint64_t(self->count_)*self->block_bytes_;
  if(offset>=self->block_bytes_ || lba>=self->count_ || start+size>limit)return -1;
  esp_err_t err;
  if(self->raw_) err=esp_partition_read(self->part_,size_t(start),buffer,size);
  else err=wl_read(self->wl_,size_t(start),buffer,size);
  if(err!=ESP_OK){self->failed_=true;return -1;}
  self->host_seen_=true;
  return (int32_t)size;
}
int32_t USBDrive::write_cb(uint32_t lba,uint32_t offset,uint8_t* buffer,uint32_t size) {
  USBDrive* self=current_;
  if (!self || !self->ready_ || self->failed_ || self->ejected_ || !buffer) return -1;
  const uint64_t start=uint64_t(lba)*self->block_bytes_+offset;
  const uint64_t limit=uint64_t(self->count_)*self->block_bytes_;
  if(offset>=self->block_bytes_ || lba>=self->count_ || start+size>limit)return -1;
  size_t addr=size_t(start);
  uint32_t done=0;
  while(done<size) {
    const size_t base=addr/self->sector_*self->sector_;
    const uint32_t inside=uint32_t(addr-base);
    const uint32_t n=std::min(size-done,self->sector_-inside);
    const esp_err_t rd=self->raw_
        ?esp_partition_read(self->part_,base,scratch,self->sector_)
        :wl_read(self->wl_,base,scratch,self->sector_);
    if(rd!=ESP_OK){self->failed_=true;return -1;}
    if(memcmp(scratch+inside,buffer+done,n)!=0) {
      memcpy(scratch+inside,buffer+done,n);
      const esp_err_t er=self->raw_
          ?esp_partition_erase_range(self->part_,base,self->sector_)
          :wl_erase_range(self->wl_,base,self->sector_);
      const esp_err_t wr=(er!=ESP_OK) ? er : (self->raw_
          ?esp_partition_write(self->part_,base,scratch,self->sector_)
          :wl_write(self->wl_,base,scratch,self->sector_));
      if(wr!=ESP_OK){self->failed_=true;return -1;}
    }
    addr+=n;done+=n;
  }
  self->host_seen_=true;
  return (int32_t)size;
}
bool USBDrive::start_stop_cb(uint8_t,bool start,bool eject) {
  USBDrive* self=current_;
  if(!self)return false;
  if(!start && eject) {self->ejected_=true;self->msc_.mediaPresent(false);}
  return true;
}
} // namespace mousewatch