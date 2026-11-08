// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Layer812

// Host fixture: FAT16 BPB, VFAT LFN, short 8.3, fragmented cluster chain.
#include "../src/raw_fat16.hpp"
#include <vector>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <cstdint>
using namespace mousewatch;
static constexpr uint32_t SZ=12*1024*1024;
static constexpr uint32_t SECTORS=SZ/512;
static constexpr uint32_t FAT_START=512;
static constexpr uint32_t FAT_SECTORS=32;
static constexpr uint32_t ROOT_START=(1+2*FAT_SECTORS)*512;
static constexpr uint32_t DATA_START=(1+2*FAT_SECTORS+32)*512;
void w16(uint8_t*p,unsigned v){p[0]=v&255;p[1]=(v>>8)&255;}
void w32(uint8_t*p,unsigned v){w16(p,v);w16(p+2,v>>16);}
bool rd(void* context,uint32_t at,void* dst,uint32_t n){auto* b=(std::vector<uint8_t>*)context;if(uint64_t(at)+n>b->size())return false;memcpy(dst,b->data()+at,n);return true;}
void fat(std::vector<uint8_t>& b,int cl,int val){for(int i=0;i<2;i++)w16(b.data()+FAT_START+i*FAT_SECTORS*512+cl*2,val);}
void data(std::vector<uint8_t>& b,int cl,const uint8_t* src,int n){memcpy(b.data()+DATA_START+(cl-2)*2048,src,n);}
uint8_t sum(const char* name){uint8_t s=0;for(int i=0;i<11;i++)s=((s&1)?0x80:0)+(s>>1)+uint8_t(name[i]);return s;}
void lfn(std::vector<uint8_t>& b,uint32_t at,int order,int last,const char* word,uint8_t chk){
  uint8_t* e=b.data()+at;memset(e,255,32);e[0]=uint8_t(order|(last?0x40:0));e[11]=15;e[12]=0;e[13]=chk;e[26]=e[27]=0;
  int offs[13]={1,3,5,7,9,14,16,18,20,22,24,28,30};
  size_t total=strlen(word);
  for(int i=0;i<13;i++){size_t ix=(order-1)*13+i;w16(e+offs[i],ix<total?uint8_t(word[ix]):ix==total?0:0xffff);}
}
void ent(std::vector<uint8_t>& b,uint32_t at,const char* short11,uint16_t cl,uint32_t size){uint8_t* e=b.data()+at;memcpy(e,short11,11);e[11]=0x20;w16(e+26,cl);w32(e+28,size);}
int main(){
  std::vector<uint8_t> b(SZ,0);
  uint8_t* boot=b.data();boot[0]=0xeb;boot[1]=0x3c;boot[2]=0x90;
  w16(boot+11,512);boot[13]=4;w16(boot+14,1);boot[16]=2;
  w16(boot+17,512);w16(boot+19,SECTORS);boot[21]=0xf8;
  w16(boot+22,FAT_SECTORS);memcpy(boot+54,"FAT16   ",8);boot[510]=0x55;boot[511]=0xaa;
  fat(b,0,0xfff8);fat(b,1,0xffff);
  // "holiday photo.jpg" is two VFAT LFN entries followed by a short name.
  const char* sh="HOLIDA~1JPG";uint8_t chk=sum(sh);
  const char* longname="holiday photo.jpg";
  lfn(b,ROOT_START,2,1,longname,chk);
  lfn(b,ROOT_START+32,1,0,longname,chk);
  ent(b,ROOT_START+64,sh,2,3400);
  fat(b,2,7);fat(b,7,0xffff); // noncontiguous second cluster
  std::vector<uint8_t> contents(3400);
  for(size_t i=0;i<contents.size();i++)contents[i]=uint8_t((i*7+3)&255);
  data(b,2,contents.data(),2048);data(b,7,contents.data()+2048,contents.size()-2048);
  // One conventional 8.3 PNG root file.
  ent(b,ROOT_START+96,"CAT     PNG",4,1024);fat(b,4,0xffff);
  const uint8_t expected_first=0x89; b[DATA_START+(4-2)*2048]=expected_first;
  // Windows can copy images inside a folder; discover one level deep.
  uint8_t* folder=b.data()+ROOT_START+128;
  memcpy(folder,"IMAGES     ",11);folder[11]=0x10;w16(folder+26,5);fat(b,5,0xffff);
  const uint32_t in_dir=DATA_START+(5-2)*2048;
  ent(b,in_dir,"SCENE   JPG",6,1000);fat(b,6,0xffff);
  for(unsigned i=0;i<1000;i++)b[DATA_START+(6-2)*2048+i]=uint8_t(i&255);
  RawFat16 vol;assert(vol.begin(&b,rd,SZ));
  RawImageEntry files[10];const auto count=vol.scan(files,10);
  if(count!=3){printf("FAIL entries=%u\n",count);return 1;}
  printf("files: %s, %s, %s\n",files[0].name,files[1].name,files[2].name);
  assert(!strcmp(files[0].name,"CAT.PNG"));
  assert(!strcmp(files[1].name,"holiday photo.jpg"));
  assert(!strcmp(files[2].name,"IMAGES/SCENE.JPG"));
  std::vector<uint8_t> got(contents.size(),0);
  assert(vol.load(files[1],got.data(),got.size()));
  assert(got==contents);assert(got[2048]==contents[2048]);
  // Invalid first-cluster chain must not read unrelated flash.
  auto bad=files[1];bad.first_cluster=0xfff0;
  assert(!vol.load(bad,got.data(),got.size()));
  // A fake LFN with invalid checksum cannot replace the true short name.
  lfn(b,ROOT_START+32,1,0,longname,0); // invalid checksum
  assert(vol.scan(files,10)==3);
  assert(!strcmp(files[1].name,"HOLIDA~1.JPG"));
  printf("PASS: VFAT/8.3 scan + subfolder + fragmented file + invalid-LFN guard\n");
}