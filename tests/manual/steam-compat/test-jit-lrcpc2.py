#!/usr/bin/env python3
"""Inspect the actual private translator; execution lives in jit-lrcpc2-probe.c.

This harness includes production translate.c with allocation-only stubs. No
replacement decoder/emitter is used. Linker dead stripping removes installation
and dispatcher functions that are outside the decoder test's scope.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include "jit/translate.c"
bool mem_host_alloc_fails(void) { return false; }
int jit_tier1_compact;
static unsigned checks, cases;
#define CHECK(v) do { if(!(v)) {fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#v);abort();}checks++; } while(0)
static void test(uint32_t word,int tier,int compact) {
 struct tr t={.pc=0x12345000,.start=0x12345000,.gi=7,.tier=tier};
 jit_tier1_compact=compact;
 int size=word>>30,opc=(word>>22)&3;
 bool valid=!((size==3&&opc>=2)||(size==2&&opc==3));
 int r=tr_ldst(&t,word);CHECK(r==(valid?1:-1));
 if(valid) {
  CHECK(!t.oom && t.nmap>0 && t.nfx>0);
  unsigned barriers=0,stores=0,loads=0,commits=0,stubs=0;
  for(unsigned b=0;b<2;b++) for(unsigned i=0;i<t.b[b].n;i++) {
   uint32_t w=t.b[b].w[i];barriers+=w==0xd5033bbfu;
   if((w&0x3f200c00)==0x38000000 && ((w>>5)&31)==HR_T1) {
    CHECK((int)(w>>30)==size);CHECK((int)((w>>22)&3)==opc);
    CHECK(((w>>12)&511)==0);
    if(opc)loads++;else stores++;
   }
  }
  for(unsigned i=0;i<t.nfx;i++) {
   struct fixup *f=&t.fx[i];
   if(f->tgt>=TGT_STUB)stubs++;
   if(f->tgt==TGT_HELPER) {
    uint32_t lit=t.b[f->buf].w[f->idx+1];
    unsigned kind=lit&3,desc=(lit>>2)&0x3fffff;
    if(kind==JH_COMMIT)commits++;
    if(kind==JH_MEM) {CHECK((desc&255)==(1u<<size));CHECK(!(desc&(1<<9)));}
   }
  }
  CHECK(barriers==1);
  for(unsigned i=0;i<t.nmap;i++)CHECK(t.map[i].gi==7);
  if(!opc) {CHECK(t.b[HOT].w[0]==0xd5033bbfu);CHECK(stores==2);CHECK(commits==1);CHECK(stubs==0);}
  else {CHECK(t.b[HOT].w[t.b[HOT].n-1]==0xd5033bbfu);CHECK(loads==1);CHECK(commits==0);}
 }
 free(t.b[0].w);free(t.b[1].w);free(t.fx);free(t.map);cases++;
}
int main(void) {
 int offsets[]={-256,-96,0,16,112,255};
 for(int tier=1;tier<=2;tier++)for(int compact=0;compact<2;compact++)
 for(int size=0;size<4;size++)for(int opc=0;opc<4;opc++)
 for(int base=0;base<32;base++)for(int data=0;data<32;data++)
 for(unsigned o=0;o<sizeof(offsets)/sizeof(*offsets);o++)
  test(((uint32_t)size<<30)|0x19000000u|(opc<<22)|((offsets[o]&511)<<12)|(base<<5)|data,tier,compact);
 uint32_t sampled[]={0x99400174,0x994703b4,0xd9400294,0x991c0134,0x99000087,0xd95a0125,0xd9410124,0x19400294};
 for(unsigned i=0;i<sizeof(sampled)/sizeof(*sampled);i++)test(sampled[i],1,0);
 printf("LRCPC2_TRANSLATOR PASS cases=%u checks=%u\n",cases,checks);return 0;
}
'''

def main():
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--sanitize',default='address,undefined');a=p.parse_args()
    repo=Path(__file__).resolve().parents[3];a.out.mkdir(parents=True,exist_ok=True)
    c=a.out/'translator-harness.c';c.write_text(HARNESS);binary=a.out/'translator-harness'
    cmd=['xcrun','clang','-std=gnu11','-DGUEST_ARM64=1','-DISH_JIT=1','-O1','-g','-I'+str(repo),'-ffunction-sections','-fdata-sections','-Wl,-dead_strip']
    if a.sanitize:cmd+=['-fsanitize='+a.sanitize]
    cmd += [str(c),'-o',str(binary)]
    subprocess.run(cmd,check=True)
    r=subprocess.run([str(binary)],capture_output=True,text=True,check=True)
    (a.out/'translator-results.json').write_text(json.dumps({'compile_command':cmd,'stdout':r.stdout,'stderr':r.stderr,'exit':r.returncode,'source_sha256':hashlib.sha256((repo/'jit/translate.c').read_bytes()).hexdigest()},indent=2)+'\n');print(r.stdout,end='')
if __name__=='__main__':main()
