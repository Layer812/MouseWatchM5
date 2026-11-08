// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Layer812

#include "face_storage.hpp"
#include "image_cache_format.hpp"
#include <esp_vfs_fat.h>
#include <diskio_impl.h>
#include <ff.h>
#include <esp_partition.h>
#include <esp_heap_caps.h>
#include <dirent.h>
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <sys/stat.h>
#include <algorithm>
#include <M5GFX.h>
namespace mousewatch {
namespace {
constexpr const char* kMount = "/faces";
const esp_partition_t* storage_part() {
  // Match LIVE partition table, not an assumed offset or size.
  return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "storage");
}
// Writable 512-byte FatFs diskio, using 4096-byte flash RMW with a single
// write-back cache. This is used only in normal mouse mode for MWCACHE.BIN.
// USB MSC never runs while this VFS is mounted (FaceStorage::end unmounts it).
// No external partition changes or raw writes outside the existing FAT volume.
static const esp_partition_t* g_raw_part=nullptr;
static BYTE g_raw_drive=FF_DRV_NOT_USED;
static char g_raw_drive_name[4]="0:";
static uint32_t g_sector=UINT32_MAX;
static bool g_dirty=false;
static uint8_t g_page[4096];
bool flush_raw() {
  if (!g_dirty) return true;
  if (!g_raw_part||g_sector==UINT32_MAX) return false;
  if (esp_partition_erase_range(g_raw_part,g_sector,4096)!=ESP_OK ||
      esp_partition_write(g_raw_part,g_sector,g_page,4096)!=ESP_OK)return false;
  g_dirty=false;return true;
}
bool load_page(uint32_t address) {
  const uint32_t base=address&~4095u;
  if(g_sector==base)return true;
  if(!flush_raw() || !g_raw_part || base+4096>g_raw_part->size)return false;
  if(esp_partition_read(g_raw_part,base,g_page,4096)!=ESP_OK)return false;
  g_sector=base;return true;
}
DSTATUS rw_init(BYTE) {return 0;}
DSTATUS rw_status(BYTE) {return 0;}
DRESULT rw_read(BYTE, BYTE* buf, DWORD lba, UINT count) {
  if(!g_raw_part||!buf||!count||
     (uint64_t(lba)+count)*512>g_raw_part->size)return RES_ERROR;
  uint32_t address=uint32_t(lba)*512;
  uint32_t bytes=uint32_t(count)*512;
  while(bytes) {
    const uint32_t base=address&~4095u, offset=address-base;
    const uint32_t n=std::min(bytes,4096u-offset);
    if(g_sector==base)memcpy(buf,g_page+offset,n);
    else if(esp_partition_read(g_raw_part,address,buf,n)!=ESP_OK)return RES_ERROR;
    address+=n;buf+=n;bytes-=n;
  }
  return RES_OK;
}
DRESULT rw_write(BYTE,const BYTE* buf,DWORD lba,UINT count) {
  if(!g_raw_part||!buf||!count||
     (uint64_t(lba)+count)*512>g_raw_part->size)return RES_ERROR;
  uint32_t address=uint32_t(lba)*512;
  uint32_t bytes=uint32_t(count)*512;
  while(bytes) {
    const uint32_t offset=address&4095u;
    const uint32_t n=std::min(bytes,4096u-offset);
    if(!load_page(address))return RES_ERROR;
    if(memcmp(g_page+offset,buf,n)) {memcpy(g_page+offset,buf,n);g_dirty=true;}
    address+=n;buf+=n;bytes-=n;
  }
  return RES_OK;
}
DRESULT rw_ioctl(BYTE, BYTE cmd, void* out) {
  if(!g_raw_part)return RES_ERROR;
  switch(cmd) {
    case CTRL_SYNC: return flush_raw()?RES_OK:RES_ERROR;
    case GET_SECTOR_SIZE: if(!out)return RES_PARERR; *(WORD*)out=512;return RES_OK;
    case GET_SECTOR_COUNT: if(!out)return RES_PARERR; *(DWORD*)out=g_raw_part->size/512;return RES_OK;
    case GET_BLOCK_SIZE: if(!out)return RES_PARERR; *(DWORD*)out=8;return RES_OK;
    default:return RES_PARERR;
  }
}
static const ff_diskio_impl_t kRawDiskIO={rw_init,rw_status,rw_read,rw_write,rw_ioctl};
bool mount_raw512() {
  if(g_raw_drive!=FF_DRV_NOT_USED) return true;
  BYTE d=FF_DRV_NOT_USED;
  if(ff_diskio_get_drive(&d)!=ESP_OK) return false;
  g_raw_part=storage_part();
  if(!g_raw_part) return false;
  g_sector=UINT32_MAX;g_dirty=false;
  ff_diskio_register(d,&kRawDiskIO);
  snprintf(g_raw_drive_name,sizeof(g_raw_drive_name),"%u:",unsigned(d));
  FATFS* fsp=nullptr;
  const esp_err_t e=esp_vfs_fat_register(kMount,g_raw_drive_name,3,&fsp);
  if(e!=ESP_OK){ff_diskio_unregister(d);g_raw_part=nullptr;return false;}
  if(f_mount(fsp,g_raw_drive_name,1)!=FR_OK) {
    f_mount(nullptr,g_raw_drive_name,0);
    esp_vfs_fat_unregister_path(kMount);
    ff_diskio_unregister(d);
    g_raw_part=nullptr;
    return false;
  }
  g_raw_drive=d;
  return true;
}
void unmount_raw512() {
  if(g_raw_drive==FF_DRV_NOT_USED) return;
  flush_raw();
  f_mount(nullptr,g_raw_drive_name,0);
  flush_raw(); // Also flush anything written during FatFs unmount.
  esp_vfs_fat_unregister_path(kMount);
  ff_diskio_unregister(g_raw_drive);
  g_raw_drive=FF_DRV_NOT_USED;
  g_raw_part=nullptr;g_sector=UINT32_MAX;g_dirty=false;
}

// VinylM5's proven raw FAT16 layout, with a 512-byte BPB and MSC block.
// A raw layout is only created after verifying every storage byte is 0xff
// and the user has explicitly selected USB DRIVE. Existing volumes are kept.
uint16_t le16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1])<<8); }
uint32_t le32(const uint8_t* p) { return uint32_t(le16(p)) | (uint32_t(le16(p+2))<<16); }
void put16(uint8_t* p,uint16_t v) {p[0]=v&255;p[1]=(v>>8)&255;}
void put32(uint8_t* p,uint32_t v) {put16(p,v&65535);put16(p+2,v>>16);}
bool is_raw_fat16() {
  const auto* p=storage_part();
  if (!p || p->size < (4U<<20)) return false;
  uint8_t b[512]{};
  if (esp_partition_read(p,0,b,sizeof(b))!=ESP_OK) return false;
  if (b[510]!=0x55 || b[511]!=0xaa || le16(b+11)!=512) return false;
  if (b[13]==0 || (b[13] & (b[13]-1)) || b[16]==0 || memcmp(b+54,"FAT16",5)) return false;
  const uint32_t fat_sectors=le16(b+22), reserved=le16(b+14);
  const uint32_t root_sectors=(uint32_t(le16(b+17))*32+511)/512;
  const uint32_t first_data=reserved+uint32_t(b[16])*fat_sectors+root_sectors;
  const uint32_t total=le16(b+19) ? le16(b+19) : le32(b+32);
  if (!fat_sectors || !reserved || !root_sectors || total>p->size/512 || first_data>=total) return false;
  const uint32_t clusters=(total-first_data)/b[13];
  return clusters>=4085 && clusters<65525;
}
bool format_raw_fat16() {
  const auto* p=storage_part();
  if (!p || p->size % 4096) return false;
  constexpr uint16_t reserved=1, roots=512, root_sectors=32;
  constexpr uint8_t spc=4, fat_copies=2;
  const uint32_t sectors=p->size/512;
  if (sectors>65535 || sectors<10000) return false;
  uint16_t fat_sectors=1;
  for (int i=0;i<16;i++) {
    const uint32_t clusters=(sectors-reserved-root_sectors-fat_copies*fat_sectors)/spc;
    const uint16_t needed=uint16_t((uint64_t(clusters+2)*2+511)/512);
    if (needed==fat_sectors) break;
    fat_sectors=needed;
  }
  const uint32_t data_start=reserved+fat_copies*fat_sectors+root_sectors;
  const uint32_t clusters=(sectors-data_start)/spc;
  if (clusters<4085 || clusters>=65525) return false;
  const uint32_t erase_bytes=((data_start*512U+4095U)&~4095U);
  // This is only called for a completely erased volume; never overwrite old data.
  if (esp_partition_erase_range(p,0,erase_bytes)!=ESP_OK) return false;
  uint8_t boot[512]{};
  boot[0]=0xeb;boot[1]=0x3c;boot[2]=0x90;
  memcpy(boot+3,"MSDOS5.0",8);put16(boot+11,512);boot[13]=spc;
  put16(boot+14,reserved);boot[16]=fat_copies;put16(boot+17,roots);
  put16(boot+19,uint16_t(sectors));boot[21]=0xf8;put16(boot+22,fat_sectors);
  put16(boot+24,32);put16(boot+26,64);boot[36]=0x80;boot[38]=0x29;
  put32(boot+39,0x20261108);memcpy(boot+43,"MOUSEWATCH ",11);
  memcpy(boot+54,"FAT16   ",8);boot[510]=0x55;boot[511]=0xaa;
  if (esp_partition_write(p,0,boot,sizeof(boot))!=ESP_OK) return false;
  // Metadata must contain 0x00 bytes (not erased 0xff) for Windows FAT.
  uint8_t zero[4096]{};
  const uint32_t meta_end=data_start*512U;
  const uint32_t fat0=reserved*512U, fat1=(reserved+fat_sectors)*512U;
  const uint32_t volume_label=(reserved+fat_copies*fat_sectors)*512U;
  for(uint32_t addr=512;addr<meta_end;) {
    const uint32_t n=std::min(uint32_t(sizeof(zero)),meta_end-addr);
    memset(zero,0,n);
    auto reserved_fat=[&](uint32_t off) { if (off>=addr && off+4<=addr+n) {
      const uint32_t x=off-addr;zero[x]=0xf8;zero[x+1]=0xff;zero[x+2]=0xff;zero[x+3]=0xff;
    }};
    reserved_fat(fat0);reserved_fat(fat1);
    if (volume_label>=addr && volume_label+32<=addr+n) {
      const uint32_t x=volume_label-addr;memcpy(zero+x,"MOUSEWATCH ",11);zero[x+11]=0x08;
    }
    if (esp_partition_write(p,addr,zero,n)!=ESP_OK) return false;
    addr+=n;
  }
  return is_raw_fat16();
}
bool is_image(const char* n) {
  if (!n || !*n) return false;
  const char* ext = strrchr(n, '.');
  return ext && (!strcasecmp(ext,".jpg") || !strcasecmp(ext,".jpeg") || !strcasecmp(ext,".png"));
}
} // namespace
bool FaceStorage::begin() {
  if (mounted_) return true;
  if (!storage_part()) return false;
  esp_vfs_fat_mount_config_t cfg={};
  cfg.format_if_mount_failed=false;
  cfg.max_files=3;
  cfg.allocation_unit_size=4096;
  // USB copies images to raw FAT16. Read directly using the same 512-byte
  // logical sectors as VinylM5/Windows; do not depend on a VFS remount.
  if (is_raw_fat16()) {
    const auto* part=storage_part();
    if (!raw_reader_.begin((void*)part, raw_read, part->size)) return false;
    layout_=StorageLayout::RawFat16;
    mounted_=true;
    // Optional cache write access; direct raw image scanning still works if
    // the local FatFs driver cannot mount this Windows-readable volume.
    mount_raw512();
    scan_images();
    return true;
  }
  if (is_blank_storage()) return false;
  // Preserve previously created wear-levelled FAT volumes. No conversion,
  // erase, or reformating is done for nonblank storage.
  wl_handle_t handle=WL_INVALID_HANDLE;
  const esp_err_t e=esp_vfs_fat_spiflash_mount(kMount,partition_label(),&cfg,&handle);
  if(e!=ESP_OK) return false;
  wl_=handle;
  layout_=StorageLayout::WearLevelFat;
  mounted_=true;
  scan_images();
  return true;
}
void FaceStorage::end() {
  if (!mounted_) return;
  if (layout_==StorageLayout::RawFat16) unmount_raw512();
  if (layout_==StorageLayout::WearLevelFat)
    esp_vfs_fat_spiflash_unmount(kMount,wl_);
  mounted_=false;
  layout_=StorageLayout::None;
  wl_=WL_INVALID_HANDLE;
}
bool FaceStorage::is_blank_storage() const {
  const esp_partition_t* p=storage_part();
  if (!p || mounted_) return false;
  uint8_t b[4096];
  // Full check protects any existing content, including legacy WL metadata.
  for (size_t pos=0;pos<p->size;pos+=sizeof(b)) {
    const size_t n=std::min(size_t(sizeof(b)),size_t(p->size-pos));
    if(esp_partition_read(p,pos,b,n)!=ESP_OK) return false;
    for(size_t i=0;i<n;i++) if(b[i]!=0xff) return false;
  }
  return true;
}
bool FaceStorage::initialize_blank_volume() {
  if (mounted_ || !is_blank_storage()) return false;
  if (!format_raw_fat16()) return false;
  // FAT volume may be valid for Windows even if local read-only VFS mount fails.
  // Do not trap the user or re-format a valid volume to fix a local VFS issue.
  begin();
  return is_raw_fat16();
}
StorageLayout FaceStorage::usb_layout() const {
  if (mounted_) return layout_;
  return is_raw_fat16() ? StorageLayout::RawFat16 : StorageLayout::None;
}
bool FaceStorage::raw_read(void* context,uint32_t byte,void* dest,uint32_t size) {
  const auto* part=(const esp_partition_t*)context;
  return part && uint64_t(byte)+size<=part->size && esp_partition_read(part,byte,dest,size)==ESP_OK;
}
bool FaceStorage::scan_images() {
  image_count_=0;cat_frames_=false;
  if (layout_==StorageLayout::RawFat16) {
    image_count_=raw_reader_.scan(raw_files_,48);
    for (unsigned i=0;i<image_count_;i++)
      snprintf(files_[i],sizeof(files_[i]),"%s",raw_files_[i].name);
    // Original Clock Cat assets are NOT bundled: never invent them.
    return true;
  }
  DIR* dir=opendir(kMount);
  if (dir) {
    for (dirent* ent=readdir(dir);ent;ent=readdir(dir)) {
      if (image_count_>=48) break;
      if (!is_image(ent->d_name)) continue;
      if (strchr(ent->d_name,'/') || strchr(ent->d_name,'\\')) continue;
      snprintf(files_[image_count_],sizeof(files_[0]),"%s/%s",kMount,ent->d_name);
      ++image_count_;
    }
    closedir(dir);
  }
  for (int i=0;i<37;i++) {
    char n[96];snprintf(n,sizeof(n),"%s/CLOCKCAT/%02d.JPG",kMount,i);
    struct stat st{};
    if (stat(n,&st)!=0||st.st_size<=0) {cat_frames_=false;break;}
    cat_frames_=true;
  }
  return true;
}
const char* FaceStorage::filename(uint8_t index) const {
  return index < image_count_ ? files_[index] : "";
}
bool FaceStorage::show_bytes(M5GFX& screen,const uint8_t* data,uint32_t sz,const char* name) {
  if(!data||sz<32||!name)return false;
  const char* ext=strrchr(name,'.');
  bool png=ext && !strcasecmp(ext,".png");
  uint32_t w=0,h=0;
  if(png) {
    // PNG IHDR stores width/height as big-endian in bytes 16..23.
    if(sz>=24 && !memcmp(data,"\x89PNG\r\n\x1a\n",8)) {
      w=(uint32_t(data[16])<<24)|(uint32_t(data[17])<<16)|(uint32_t(data[18])<<8)|data[19];
      h=(uint32_t(data[20])<<24)|(uint32_t(data[21])<<16)|(uint32_t(data[22])<<8)|data[23];
    }
  } else {
    // JPEG SOF markers carry the actual image dimensions.
    if(data[0]==0xff && data[1]==0xd8) {
      for(uint32_t i=2;i+9<sz;) {
        if(data[i]!=0xff){++i;continue;}
        uint8_t tag=data[i+1];i+=2;
        if(tag==0xd8||tag==0x01||(tag>=0xd0&&tag<=0xd9))continue;
        if(i+2>sz)break;
        const uint16_t block=(uint16_t(data[i])<<8)|data[i+1];
        if(block<2||uint64_t(i)+block>sz)break;
        if((tag>=0xc0&&tag<=0xc3)||(tag>=0xc5&&tag<=0xc7)||(tag>=0xc9&&tag<=0xcb)||(tag>=0xcd&&tag<=0xcf)) {
          if(block>=7){h=(uint16_t(data[i+3])<<8)|data[i+4];w=(uint16_t(data[i+5])<<8)|data[i+6];}
          break;
        }
        i+=block;
      }
    }
  }
  // COVER, not FIT: preserve aspect ratio and fill the whole square display.
  // Datum middle_center clips the source overhang, while destination (0,0)
  // fills the entire screen. A 468x468 panel is used at actual resolution;
  // this is the requested 480x480-equivalent crop without creating a new file.
  float scale=1.f;
  if(w&&h){
    const float sw=float(screen.width())/float(w);
    const float sh=float(screen.height())/float(h);
    scale=sw>sh?sw:sh;
  }
  screen.fillScreen(0);
  bool decoded=png
    ?screen.drawPng(data,sz,0,0,screen.width(),screen.height(),0,0,scale,scale,middle_center)
    :screen.drawJpg(data,sz,0,0,screen.width(),screen.height(),0,0,scale,scale,middle_center);
  return decoded;
}
bool FaceStorage::show_file(M5GFX& screen,const char* path) {
  if (!mounted_||!path)return false;
  FILE* f=fopen(path,"rb");if(!f)return false;
  if(fseek(f,0,SEEK_END)!=0){fclose(f);return false;}
  const long sz=ftell(f);rewind(f);
  if(sz<=0||sz>6*1024*1024){fclose(f);return false;}
  uint8_t* data=(uint8_t*)heap_caps_malloc((size_t)sz,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!data){fclose(f);return false;}
  const bool ok=fread(data,1,(size_t)sz,f)==(size_t)sz;fclose(f);
  const bool shown=ok&&show_bytes(screen,data,(uint32_t)sz,path);
  heap_caps_free(data);
  return shown;
}
bool FaceStorage::show(M5GFX& screen,uint8_t ix) {
  if(ix>=image_count_)return false;
  if(show_cached(screen,ix))return true;
  if(prepare_image(screen,ix) && show_cached(screen,ix))return true;
  if(layout_!=StorageLayout::RawFat16)return show_file(screen,files_[ix]);
  const RawImageEntry& file=raw_files_[ix];
  uint8_t* data=(uint8_t*)heap_caps_malloc(file.size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!data)return false;
  const bool ok=raw_reader_.load(file,data,file.size);
  const bool shown=ok && show_bytes(screen,data,file.size,file.name);
  heap_caps_free(data);return shown;
}

namespace {
constexpr const char* kCachePath="/faces/MWCACHE.BIN";
constexpr const char* kCacheTemp="/faces/MWTEMP.BIN";

bool image_dimensions(const uint8_t* data,uint32_t sz,const char* name,
                      uint32_t& w,uint32_t& h) {
  w=h=0;
  if(!data || sz<32||!name)return false;
  const char* ext=strrchr(name,'.');
  const bool png=ext && !strcasecmp(ext,".png");
  if(png) {
    if(sz>=24 && !memcmp(data,"\x89PNG\r\n\x1a\n",8)) {
      w=(uint32_t(data[16])<<24)|(uint32_t(data[17])<<16)|(uint32_t(data[18])<<8)|data[19];
      h=(uint32_t(data[20])<<24)|(uint32_t(data[21])<<16)|(uint32_t(data[22])<<8)|data[23];
    }
  } else if(data[0]==0xff && data[1]==0xd8) {
    for(uint32_t i=2;i+9<sz;) {
      if(data[i]!=0xff){++i;continue;}
      const uint8_t tag=data[i+1];i+=2;
      if(tag==0xd8||tag==0x01||(tag>=0xd0&&tag<=0xd9))continue;
      if(i+2>sz)break;
      const uint16_t block=(uint16_t(data[i])<<8)|data[i+1];
      if(block<2||uint64_t(i)+block>sz)break;
      if((tag>=0xc0&&tag<=0xc3)||(tag>=0xc5&&tag<=0xc7)||
         (tag>=0xc9&&tag<=0xcb)||(tag>=0xcd&&tag<=0xcf)) {
        if(block>=7){h=(uint16_t(data[i+3])<<8)|data[i+4];w=(uint16_t(data[i+5])<<8)|data[i+6];}
        break;
      }
      i+=block;
    }
  }
  return w>0 && h>0 && w<=16384 && h<=16384;
}
}

bool FaceStorage::can_cache() const {
  return mounted_ && (layout_==StorageLayout::WearLevelFat ||
     (layout_==StorageLayout::RawFat16 && g_raw_drive!=FF_DRV_NOT_USED));
}

bool FaceStorage::cache_matches(uint8_t ix,int width,int height) const {
  if(!can_cache() || ix>=image_count_)return false;
  FILE* f=fopen(kCachePath,"rb");
  if(!f)return false;
  FaceCacheHeader h{};
  const bool read=fread(&h,1,sizeof(h),f)==sizeof(h);
  bool whole=false;
  if(read && fseek(f,0,SEEK_END)==0) {
    const long bytes=ftell(f);
    whole=bytes>=0 && uint64_t(bytes)==sizeof(h)+h.payload_bytes;
  }
  fclose(f);
  uint32_t src_size=0,stamp=0;uint16_t cluster=0;
  if(layout_==StorageLayout::RawFat16) {
    src_size=raw_files_[ix].size;
    stamp=raw_files_[ix].modified;
    cluster=raw_files_[ix].first_cluster;
  } else {
    struct stat st{};if(stat(files_[ix],&st)!=0)return false;
    src_size=uint32_t(st.st_size);
    stamp=uint32_t(st.st_mtime);
  }
  return whole && cache_header_valid(h,files_[ix],src_size,stamp,cluster,
                                     uint16_t(width),uint16_t(height));
}

bool FaceStorage::show_cached(M5GFX& screen,uint8_t ix) {
  const int w=screen.width(),h=screen.height();
  if(!cache_matches(ix,w,h))return false;
  FILE* f=fopen(kCachePath,"rb");if(!f)return false;
  FaceCacheHeader header{};
  if(fread(&header,1,sizeof(header),f)!=sizeof(header)){fclose(f);return false;}
  // Verify before displaying; interrupted USB writes and bad FAT cannot
  // produce a 'valid-looking' half-frame on screen.
  const uint32_t bytes=header.payload_bytes;
  auto* pixels=(uint8_t*)heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!pixels){fclose(f);return false;}
  const bool ok=fread(pixels,1,bytes,f)==bytes;
  fclose(f);
  const bool valid=ok && cache_hash(2166136261u,pixels,bytes)==header.payload_hash;
  if(valid) {
    screen.pushImage(0,0,w,h,reinterpret_cast<const uint16_t*>(pixels));
  }
  heap_caps_free(pixels);
  // A stale/corrupt cache must not block regeneration on the next attempt.
  if(!valid)remove(kCachePath);
  return valid;
}

bool FaceStorage::prepare_image(M5GFX& screen,uint8_t ix) {
  if(ix>=image_count_ || !can_cache())return false;
  const uint16_t w=uint16_t(screen.width()),h=uint16_t(screen.height());
  if(w<400 || w>480 || h<400 || h>480)return false;
  if(cache_matches(ix,w,h))return true;

  uint32_t source_size=0,stamp=0;uint16_t cluster=0;
  if(layout_==StorageLayout::RawFat16) {
    source_size=raw_files_[ix].size;
    stamp=raw_files_[ix].modified;
    cluster=raw_files_[ix].first_cluster;
  } else {
    struct stat st{};if(stat(files_[ix],&st)!=0)return false;
    source_size=uint32_t(st.st_size);stamp=uint32_t(st.st_mtime);
  }
  if(!source_size||source_size>6*1024*1024)return false;
  auto* source=(uint8_t*)heap_caps_malloc(source_size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!source)return false;
  bool loaded=false;
  if(layout_==StorageLayout::RawFat16) loaded=raw_reader_.load(raw_files_[ix],source,source_size);
  else {
    FILE* f=fopen(files_[ix],"rb");
    if(f){loaded=fread(source,1,source_size,f)==source_size;fclose(f);}
  }
  uint32_t sw=0,sh=0;
  if(!loaded || !image_dimensions(source,source_size,files_[ix],sw,sh)) {
    heap_caps_free(source);return false;
  }

  M5Canvas canvas(&screen);
  canvas.setPsram(true);
  canvas.setColorDepth(16);
  if(!canvas.createSprite(w,h)){heap_caps_free(source);return false;}
  canvas.fillScreen(0);
  const float zoom=cover_zoom(sw,sh,w,h);
  // Important: LGFX image-datum positioning expects the DESTINATION TOP-LEFT
  // (0,0) here, not w/2,h/2. Datum middle_center computes a source offset
  // for the overhang; a w/2 starting point shifted output into half the screen.
  // The whole pixel canvas is physically clipped at its 468x468 edges.
  // The on-disk cache therefore always contains exactly w*h pixels.
  const bool is_png=strrchr(files_[ix],'.') && !strcasecmp(strrchr(files_[ix],'.'),".png");
  const bool decoded=is_png
    ? canvas.drawPng(source,source_size,0,0,w,h,0,0,zoom,zoom,middle_center)
    : canvas.drawJpg(source,source_size,0,0,w,h,0,0,zoom,zoom,middle_center);
  heap_caps_free(source);
  if(!decoded){canvas.deleteSprite();return false;}

  const uint32_t block_bytes=uint32_t(w)*16u; // eight RGB565 rows
  auto* rows=(uint8_t*)heap_caps_malloc(block_bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!rows){canvas.deleteSprite();return false;}
  FaceCacheHeader head{};
  head.magic=kFaceCacheMagic;head.version=kFaceCacheVersion;
  head.width=w;head.height=h;
  head.source_bytes=source_size;head.source_stamp=stamp;
  head.source_cluster=cluster;head.payload_bytes=uint32_t(w)*h*2u;
  snprintf(head.source_name,sizeof(head.source_name),"%s",files_[ix]);

  FILE* f=fopen(kCacheTemp,"wb");
  bool good=(f!=nullptr);
  if(good)good=fwrite(&head,1,sizeof(head),f)==sizeof(head);
  uint32_t checksum=2166136261u;
  for(uint16_t y=0;good && y<h;y+=8) {
    const int n=std::min(8,int(h-y));
    canvas.readRect(0,y,w,n,reinterpret_cast<uint16_t*>(rows));
    const uint32_t len=uint32_t(n)*w*2u;
    checksum=cache_hash(checksum,rows,len);
    good=fwrite(rows,1,len,f)==len;
  }
  if(f) {
    if(good) {
      head.payload_hash=checksum;
      good=fseek(f,0,SEEK_SET)==0 && fwrite(&head,1,sizeof(head),f)==sizeof(head);
    }
    if(good)good=fflush(f)==0;
    if(fclose(f)!=0)good=false;
  }
  heap_caps_free(rows);
  canvas.deleteSprite();
  if(!good){remove(kCacheTemp);return false;}
  // Only now replace the old cache. The source JPEG/PNG is never modified.
  if(remove(kCachePath)!=0) { // ENOENT is fine; rename would work directly.
    FILE* old=fopen(kCachePath,"rb");
    if(old){fclose(old);remove(kCacheTemp);return false;}
  }
  if(rename(kCacheTemp,kCachePath)!=0)return false;
  return cache_matches(ix,w,h);
}

bool FaceStorage::show_cat_frame(M5GFX& screen,uint8_t frame) {
  if(!cat_frames_)return false;
  char path[96];snprintf(path,sizeof(path),"%s/CLOCKCAT/%02u.JPG",kMount,unsigned(frame%37));
  return show_file(screen,path);
}
} // namespace mousewatch