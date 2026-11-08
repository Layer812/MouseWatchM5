// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Layer812

#include "raw_fat16.hpp"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
namespace mousewatch {
namespace {
uint16_t u16(const uint8_t*p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}
uint32_t u32(const uint8_t*p){return uint32_t(u16(p))|(uint32_t(u16(p+2))<<16);}
bool image_ext(const char* n){
  const char* e=strrchr(n,'.');if(!e)return false;
  ++e;char suffix[8]{};unsigned i=0;
  while(*e&&i<7)suffix[i++]=char(tolower((unsigned char)*e++));
  return !strcmp(suffix,"jpg")||!strcmp(suffix,"jpeg")||!strcmp(suffix,"png");
}
// VFAT checksum makes sure a long name belongs to its actual 8.3 entry.
uint8_t short_sum(const uint8_t* entry){uint8_t s=0;for(int i=0;i<11;i++)s=uint8_t(((s&1)?0x80:0)+(s>>1)+entry[i]);return s;}
void short_name(const uint8_t* e,char* dst,size_t cap){
  size_t n=0;
  for(int i=0;i<8 && e[i]!=' ';i++)if(n+1<cap)dst[n++]=char(e[i]==0x05?0xe5:e[i]);
  if(e[8]!=' '&&n+1<cap)dst[n++]='.';
  for(int i=8;i<11&&e[i]!=' ';i++)if(n+1<cap)dst[n++]=char(e[i]);
  dst[n]=0;
}
// UTF-16 -> UTF-8 (including surrogate pairs), bounded to 95 bytes.
void to_utf8(const uint16_t* chars,size_t max_chars,char* dst,size_t cap){
  size_t w=0;for(size_t i=0;i<max_chars;i++){
    uint32_t c=chars[i];if(!c||c==0xffff)break;
    if(c>=0xd800&&c<=0xdbff){
      if(i+1>=max_chars)break;
      uint32_t d=chars[++i];
      if(d<0xdc00||d>0xdfff)break;
      c=0x10000+((c-0xd800)<<10)+(d-0xdc00);
    }else if(c>=0xdc00&&c<=0xdfff)continue;
    char out[4];int n=0;
    if(c<0x80)out[n++]=char(c);
    else if(c<0x800){out[n++]=char(0xc0|(c>>6));out[n++]=char(0x80|(c&63));}
    else if(c<0x10000){out[n++]=char(0xe0|(c>>12));out[n++]=char(0x80|((c>>6)&63));out[n++]=char(0x80|(c&63));}
    else {out[n++]=char(0xf0|(c>>18));out[n++]=char(0x80|((c>>12)&63));out[n++]=char(0x80|((c>>6)&63));out[n++]=char(0x80|(c&63));}
    if(w+size_t(n)>=cap)break;
    memcpy(dst+w,out,n);w+=n;
  }dst[w]=0;
}
}
bool RawFat16::rd(uint64_t byte,void* dst,uint32_t len) const {
  return read_&&byte+len<=capacity_&&read_(context_,uint32_t(byte),dst,len);
}
bool RawFat16::begin(void* ctx,RawReadFn reader,uint32_t size){
  ok_=false;context_=ctx;read_=reader;capacity_=size;
  uint8_t b[512]{};
  if(size<4*1024*1024 || !rd(0,b,512))return false;
  if(b[510]!=0x55||b[511]!=0xaa||u16(b+11)!=512||b[13]==0||(b[13]&(b[13]-1)))return false;
  if(!b[16]||u16(b+17)<16||u16(b+22)==0||memcmp(b+54,"FAT16",5))return false;
  const uint32_t total=u16(b+19)?u16(b+19):u32(b+32);
  const uint32_t root_secs=(uint32_t(u16(b+17))*32+511)/512;
  const uint32_t reserved=u16(b+14),fat_sectors=u16(b+22);
  const uint32_t data_lba=reserved+uint32_t(b[16])*fat_sectors+root_secs;
  if(!reserved||!total||total>size/512||data_lba>=total)return false;
  const uint32_t clusters=(total-data_lba)/b[13];
  if(clusters<4085||clusters>=65525||clusters+2>65535)return false;
  first_fat_=reserved*512;root_start_=(reserved+uint32_t(b[16])*fat_sectors)*512;
  data_start_=data_lba*512;root_entries_=u16(b+17);total_sectors_=total;
  cluster_limit_=uint16_t(clusters+2);sectors_per_cluster_=b[13];ok_=true;return true;
}
bool RawFat16::next(uint16_t from,uint16_t& to) const {
  if(!ok_||from<2||from>=cluster_limit_)return false;
  uint8_t e[2]{};
  if(!rd(uint64_t(first_fat_)+uint64_t(from)*2,e,2))return false;
  to=u16(e);return true;
}
uint8_t RawFat16::scan(RawImageEntry* files,uint8_t cap) const {
  if(!ok_||!files||!cap)return 0;
  uint8_t n=0,dir_count=0;
  RawImageEntry dirs[12]{};
  uint16_t chars[260];for(auto& c:chars)c=0xffff;
  uint8_t lfn_next=0,lfn_sum=0;bool lfn_ok=false;
  const char* parent_dir="";
  auto reset_lfn=[&](){lfn_ok=false;lfn_next=0;for(auto& c:chars)c=0xffff;};
  // Directory entries are parsed using the same logic for root and folders.
  auto consume=[&](const uint8_t* e,bool root)->bool {
    if(!e[0])return false; // end-of-directory marker
    if(e[0]==0xe5){reset_lfn();return true;}
    if(e[11]==0x0f){
      const uint8_t order=e[0]&0x1f;
      if((e[0]&0x40)&&order>=1&&order<=20){reset_lfn();lfn_next=order;lfn_sum=e[13];lfn_ok=true;}
      if(!lfn_ok||order!=lfn_next||e[13]!=lfn_sum){reset_lfn();return true;}
      static const uint8_t offs[13]={1,3,5,7,9,14,16,18,20,22,24,28,30};
      for(int j=0;j<13;j++)chars[(order-1)*13+j]=u16(e+offs[j]);
      --lfn_next;return true;
    }
    if(e[11]&0x08){reset_lfn();return true;} // volume label
    RawImageEntry entry{};
    short_name(e,entry.name,sizeof(entry.name));
    const char* short_source=entry.name;
    char short_copy[96]{};
    memcpy(short_copy,short_source,strlen(short_source)+1);
    if(lfn_ok&&lfn_next==0&&lfn_sum==short_sum(e)){
      char long_name[96]{};to_utf8(chars,260,long_name,sizeof(long_name));
      if(long_name[0])memcpy(entry.name,long_name,strlen(long_name)+1);
    }
    reset_lfn();
    entry.first_cluster=u16(e+26);entry.size=u32(e+28);
    entry.modified=(uint32_t(u16(e+24))<<16)|u16(e+22);
    if(e[11]&0x10){
      // Allow images in a single folder (e.g. IMAGES/), without asking users
      // to move them to root. Skip hidden/system and dot directories.
      if(root&&dir_count<12&&!(e[11]&0x06)&&entry.name[0]!='.' &&
         entry.first_cluster>=2&&entry.first_cluster<cluster_limit_)
        dirs[dir_count++]=entry;
      return true;
    }
    if(n<cap && entry.size>0&&entry.size<=6*1024*1024 &&
       entry.first_cluster>=2&&entry.first_cluster<cluster_limit_ &&
       (image_ext(entry.name)||image_ext(short_copy))) {
      if(!image_ext(entry.name))memcpy(entry.name,short_copy,strlen(short_copy)+1);
      if(!root && parent_dir[0]) {
        char whole[96]{};
        snprintf(whole,sizeof(whole),"%s/%s",parent_dir,entry.name);
        memcpy(entry.name,whole,strlen(whole)+1);
      }
      files[n++]=entry;
    }
    return true;
  };
  for(uint32_t ix=0;ix<root_entries_&&n<cap;ix++){
    uint8_t e[32]{};if(!rd(uint64_t(root_start_)+ix*32,e,32))break;
    if(!consume(e,true))break;
  }
  // One level of subdirectories; no full filesystem mount and no writes.
  const uint32_t cluster_bytes=uint32_t(sectors_per_cluster_)*512;
  for(uint8_t d=0;d<dir_count&&n<cap;d++){
    parent_dir=dirs[d].name;
    reset_lfn();
    uint16_t cluster=dirs[d].first_cluster;
    bool end=false;
    for(uint32_t hop=0;hop<cluster_limit_&&!end&&n<cap;hop++){
      if(cluster<2||cluster>=cluster_limit_)break;
      const uint64_t base=uint64_t(data_start_)+uint64_t(cluster-2)*cluster_bytes;
      for(uint32_t ix=0;ix<cluster_bytes/32&&n<cap;ix++){
        uint8_t e[32]{};
        if(!rd(base+ix*32,e,32)||!consume(e,false)){end=true;break;}
      }
      if(!end){uint16_t follow=0;if(!next(cluster,follow)||follow>=0xfff8)break;cluster=follow;}
    }
  }
  // Stable menu/slide-show ordering even when files were copied later.
  for(unsigned i=0;i<n;i++)for(unsigned j=i+1;j<n;j++){
    const char *a=files[i].name,*b=files[j].name;
    int diff=0;for(;*a&&*b;++a,++b){diff=tolower((unsigned char)*a)-tolower((unsigned char)*b);if(diff)break;}
    if(!diff)diff=(unsigned char)*a-(unsigned char)*b;
    if(diff>0){const RawImageEntry t=files[i];files[i]=files[j];files[j]=t;}
  }
  return n;
}
bool RawFat16::load(const RawImageEntry& file,uint8_t* target,uint32_t capacity) const {
  if(!ok_||!target||!file.size||file.size>capacity||file.first_cluster<2)return false;
  uint32_t left=file.size,offset=0;
  uint16_t cluster=file.first_cluster;
  uint32_t hops=0;
  const uint32_t bytes_per_cluster=uint32_t(sectors_per_cluster_)*512;
  while(left){
    if(cluster<2||cluster>=cluster_limit_||++hops>cluster_limit_)return false;
    const uint32_t chunk=left<bytes_per_cluster?left:bytes_per_cluster;
    const uint64_t at=uint64_t(data_start_)+uint64_t(cluster-2)*bytes_per_cluster;
    if(!rd(at,target+offset,chunk))return false;
    left-=chunk;offset+=chunk;
    if(left){uint16_t follow=0;if(!next(cluster,follow)||follow>=0xfff8)return false;cluster=follow;}
  }
  return true;
}
} // namespace mousewatch