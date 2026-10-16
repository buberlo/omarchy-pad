#!/usr/bin/env python3
"""Local-only candidate emitter/ARM64 tests; never applies the patch or builds LinPad.

The small generator uses the real read_greg/mov_imm64, emit.h, register map,
and context layout. Its emitted words are interpreted, then assembled unchanged
inside an isolated native wrapper. This is not a full JIT/trampoline test.
"""
import hashlib
import json
from pathlib import Path
import platform
import random
import re
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
REPO = ROOT / 'work/LinPad'
OUT = HERE / 'local-test'
MASK = 0x07c80000
U64 = (1 << 64) - 1

def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

def function(source, name):
    match = re.search(r'^static[^\n]*\b' + re.escape(name) + r'\([^\n]*\) \{', source, re.M)
    assert match, name
    pos, depth = match.end(), 1
    while depth:
        if source[pos] == '{': depth += 1
        elif source[pos] == '}': depth -= 1
        pos += 1
    return source[match.start():pos]

def run(args):
    result = subprocess.run(args, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise RuntimeError(f'{args[0]} failed {result.returncode}\n{result.stdout}\n{result.stderr}')
    return result.stdout

def prepare_generator():
    provenance = json.loads((HERE / 'source-provenance.json').read_text())
    for path, expected in provenance['inputs'].items():
        assert sha(REPO / path) == expected, f'source changed after provenance: {path}'
    assert sha(HERE / 'msr-fpcr-handler.inc') == provenance['handler_sha256']
    source = (REPO / 'jit/translate.c').read_text()
    jit = (REPO / 'jit/jit.c').read_text()
    gadgets = (REPO / 'asbestos/guest-arm64/gadgets-aarch64/gadgets.h').read_text()
    assert '#define FPCR_GUEST_MASK 0x07c80000u' in jit
    assert '#define FPCR_GUEST_MASK_HI 0x07c8' in gadgets
    # The candidate relies on these existing cache and host restore operations.
    for text in [
        'ctx->guest_fpcr_masked = ctx->cpu.fpcr & FPCR_GUEST_MASK;',
        'ctx->guest_fpcr_masked = cpu->fpcr & FPCR_GUEST_MASK;',
        'e_str_x(9, HR_CTX, CTX_OFF(host_fpcr))',
        'e_ldr_x(9, HR_CTX, CTX_OFF(host_fpcr))',
        'e_ldr_w(9, HR_CTX, CTX_OFF(guest_fpcr_masked))',
    ]: assert text in jit, text
    mapping = re.search(r'const int8_t jit_g2h\[32\] = \{.*?\n\};', source, re.S).group()
    snippet = (HERE / 'msr-fpcr-handler.inc').read_text()
    code = '''#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "jit/jit_internal.h"
#include "jit/emit.h"
enum { HOT = 0 };
struct tr { uint32_t w[32]; unsigned n; };
static void put(struct tr *t, int buf, uint32_t word) {
    (void)buf; if (t->n == 32) abort(); t->w[t->n++] = word;
}
'''
    code += mapping + '\n' + function(source, 'mov_imm64') + '\n' + function(source, 'read_greg')
    code += '\nstatic int candidate(struct tr *t, uint32_t insn) {\nuint32_t sys=insn & 0xffffffe0; int rt=insn & 31;\n'
    code += snippet + '\nreturn -1;\n}\n'
    fields = ['cpu.fpcr', 'cpu.fpsr', 'cpu.nzcv', 'cpu.sp', 'cpu.fp', 'guest_fpcr_masked',
              'host_fpcr', 'helper_result', 'helper_arg']
    code += 'int main(void) {\nprintf("{\\"offsets\\":{");\n'
    for i, field in enumerate(fields):
        code += f'printf("{"," if i else ""}\\"{field}\\":%zu", CTX_OFF({field}));\n'
    code += 'printf(",\\"regs\\":%zu},\\"g2h\\":[", CTX_REG(0));\n'
    code += 'for(int g=0;g<32;g++) printf("%s%d",g?",":"",jit_g2h[g]);\n'
    code += 'printf("],\\"cases\\":[");\n'
    code += '''for(int rt=0; rt<32; rt++) { struct tr t={0};
if(candidate(&t,0xd51b4400u|rt)!=1) abort();
printf("%s[",rt?",":"");
for(unsigned i=0;i<t.n;i++) printf("%s%u",i?",":"",t.w[i]); printf("]"); }
printf("]}\\n"); return 0; }
'''
    path = OUT / 'emitter.c'
    path.write_text(code)
    flags = ['xcrun', 'clang', '-std=gnu11', '-DGUEST_ARM64=1', '-DISH_JIT=1', '-O2',
             '-I' + str(REPO), '-I' + str(ROOT / 'work/linpad-cli')]
    run(flags + [str(path), '-o', str(OUT / 'emitter')])
    data = json.loads(run([str(OUT / 'emitter')]))
    (OUT / 'emitted.json').write_text(json.dumps(data, indent=2) + '\n')
    return data, flags

def interpret(words, h, mem, fpcr):
    h = h[:]
    mem = dict(mem)
    def reg(n): return 0 if n == 31 else h[n]
    for word in words:
        rd, rn, rm = word & 31, (word >> 5) & 31, (word >> 16) & 31
        if word & 0xffc00000 == 0xf9400000:  # LDR X unsigned offset
            h[rd] = mem[reg(rn) + ((word >> 10) & 4095) * 8]
        elif word & 0xffc00000 == 0xb9000000:  # STR W
            mem[reg(rn) + ((word >> 10) & 4095) * 4] = reg(rd) & 0xffffffff
        elif word & 0xff800000 == 0xd2800000:  # MOVZ X
            h[rd] = ((word >> 5) & 65535) << (((word >> 21) & 3) * 16)
        elif word & 0xffe0fc00 == 0x8a000000:  # AND X unshifted, no flags
            h[rd] = reg(rn) & reg(rm)
        elif word & 0xffe0ffe0 == 0xaa0003e0:  # MOV X (negative control)
            h[rd] = reg(rm)
        elif word & 0xffffffe0 == 0xd51b4400:  # MSR FPCR
            fpcr = reg(rd)
        else:
            raise AssertionError(f'unexpected candidate instruction {word:08x}')
    return h, mem, fpcr

def model_tests(data):
    rng = random.Random(0x46504352)
    vals = [0, U64, MASK, ~MASK & U64, 1 << 63]
    vals += [1 << b for b in range(64)]
    vals += [rng.getrandbits(64) for _ in range(128)]
    off, mapping = data['offsets'], data['g2h']
    n = 0
    for rt, words in enumerate(data['cases']):
        for value in vals:
            h = [rng.getrandbits(64) for _ in range(32)]
            mem = {off['regs'] + g*8: rng.getrandbits(64) for g in range(31)}
            h[11] = 0  # context-relative addresses
            mem[off['cpu.fpcr']] = 0x800000
            mem[off['guest_fpcr_masked']] = 0x800000
            if rt != 31:
                mem[off['regs'] + rt*8] = value
                if mapping[rt] >= 0: h[mapping[rt]] = value
            before_h, before_mem = h[:], dict(mem)
            h, mem, hardware = interpret(words, h, mem, 0x800000)
            expected = value if rt != 31 else 0
            assert mem[off['cpu.fpcr']] == expected & 0xffffffff
            assert mem[off['guest_fpcr_masked']] == expected & MASK
            assert hardware == expected & MASK
            # An ordinary exit/reentry must reload the same applied guest value.
            assert mem[off['guest_fpcr_masked']] == hardware
            for g in range(31):
                assert mem[off['regs'] + g*8] == before_mem[off['regs'] + g*8]
                if mapping[g] >= 0: assert h[mapping[g]] == before_h[mapping[g]]
            assert h[15] == before_h[15]  # guest SP in host x15
            assert h[11] == 0  # context pointer
            n += 1
    # These mutation controls demonstrate that stale reentry state / missing
    # masking would fail the state checks, rather than only mirroring syntax.
    words = data['cases'][0]
    h = [0] * 32
    h[0] = U64
    mem = {off['cpu.fpcr']: 0, off['guest_fpcr_masked']: 0}
    removed_cache_store = [w for w in words if not (w & 0xffc00000 == 0xb9000000 and
                          ((w >> 10) & 4095) * 4 == off['guest_fpcr_masked'])]
    _, bad_mem, applied = interpret(removed_cache_store, h, mem, 0)
    assert bad_mem[off['guest_fpcr_masked']] != applied
    removed_mask = [0xaa0003ec if w & 0xffe0fc00 == 0x8a000000 else w for w in words]
    _, bad_mem, applied = interpret(removed_mask, h, mem, 0)
    assert bad_mem[off['guest_fpcr_masked']] == U64 & 0xffffffff
    assert applied & ~MASK != 0
    return {'register_value_cases': n, 'stale_reentry_negative_control_detected': True,
            'missing_mask_negative_control_detected': True}

def assembly(data):
    off, mapping = data['offsets'], data['g2h']
    s = '.text\n'
    for rt, words in enumerate(data['cases']):
        s += f'.p2align 2\n.globl _case_{rt}\n_case_{rt}:\nsub sp,sp,#128\n'
        for g in range(19,31,2): s += f'stp x{g},x{g+1},[sp,#{(g-19)*8}]\n'
        s += 'mrs x12,fpsr\nstr x12,[sp,#96]\nmrs x12,nzcv\nstr x12,[sp,#104]\nmov x11,x0\n'
        s += f'mrs x12,fpcr\nstr x12,[x11,#{off["host_fpcr"]}]\n'
        s += f'ldr w12,[x11,#{off["guest_fpcr_masked"]}]\nmsr fpcr,x12\n'
        s += f'ldr w12,[x11,#{off["cpu.fpsr"]}]\nmsr fpsr,x12\n'
        s += f'ldr w12,[x11,#{off["cpu.nzcv"]}]\nmsr nzcv,x12\n'
        for g in range(31):
            if mapping[g] >= 0: s += f'ldr x{mapping[g]},[x11,#{off["regs"]+8*g}]\n'
        s += f'ldr x15,[x11,#{off["cpu.sp"]}]\n'
        s += '// Exact words emitted by the isolated candidate handler.\n'
        s += ''.join(f'.long 0x{word:08x}\n' for word in words)
        for g in range(31):
            if mapping[g] >= 0: s += f'str x{mapping[g]},[x11,#{off["regs"]+8*g}]\n'
        s += f'str x15,[x11,#{off["cpu.sp"]}]\n'
        s += f'mrs x14,fpcr\nstr x14,[x11,#{off["helper_result"]}]\n'
        s += f'mrs x14,fpsr\nstr w14,[x11,#{off["cpu.fpsr"]}]\n'
        s += f'mrs x14,nzcv\nstr w14,[x11,#{off["cpu.nzcv"]}]\n'
        s += f'ldr d0,[x11,#{off["cpu.fp"]}]\nfrinti d0,d0\nfmov x14,d0\nstr x14,[x11,#{off["helper_arg"]}]\n'
        s += f'ldr x14,[x11,#{off["host_fpcr"]}]\nmsr fpcr,x14\n'
        s += 'ldr x14,[sp,#96]\nmsr fpsr,x14\nldr x14,[sp,#104]\nmsr nzcv,x14\n'
        for g in range(19,31,2): s += f'ldp x{g},x{g+1},[sp,#{(g-19)*8}]\n'
        s += 'add sp,sp,#128\nret\n'
    return s

def hardware_tests(data, flags):
    (OUT / 'candidate-native.S').write_text(assembly(data))
    code = '''#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jit/jit_internal.h"
'''
    code += ''.join(f'extern void case_{rt}(struct jit_ctx *);\n' for rt in range(32))
    code += 'static void (*cases[32])(struct jit_ctx *)={' + ','.join(f'case_{rt}' for rt in range(32)) + '};\n'
    code += r'''
static uint64_t get_fpcr(void) {uint64_t x; __asm__ volatile("mrs %0,fpcr":"=r"(x));return x;}
static void set_fpcr(uint64_t x) {__asm__ volatile("msr fpcr,%0"::"r"(x):"memory");}
static uint64_t control(uint64_t x) {
 uint64_t before=get_fpcr();set_fpcr(x & 0x07c80000u);uint64_t actual=get_fpcr();set_fpcr(before);return actual;
}
int main(void) {
 struct jit_ctx *ctx=calloc(1,sizeof(*ctx)); if(!ctx)return 2;
 uint64_t values[80]={0,~0ull,0x07c80000,~0x07c80000ull,0xabcdef0101800000ull};
 for(int i=0;i<64;i++)values[5+i]=1ull<<i;
 for(int i=0;i<4;i++)values[69+i]=(uint64_t)i<<22;
 unsigned checked=0,failures=0;
 uint64_t original=get_fpcr();set_fpcr(2ull<<22);uint64_t outer=get_fpcr();
 for(int rt=0;rt<32;rt++)for(int v=0;v<73;v++)for(int sign=0;sign<2;sign++) {
  uint64_t before[31];
  for(int r=0;r<31;r++)before[r]=ctx->cpu.regs[r]=0x1234000000000000ull+r;
  if(rt!=31)before[rt]=ctx->cpu.regs[rt]=values[v];
  uint64_t value=rt==31?0:values[v],masked=value & 0x07c80000u;
  ctx->cpu.fpcr=0;ctx->guest_fpcr_masked=0;ctx->cpu.fpsr=0x0800001f;ctx->cpu.nzcv=0xa0000000;
  ctx->cpu.sp=0xabcdeffedcba9876ull;
  ctx->cpu.fp[0].d[0]=sign?0xc00a000000000000ull:0x400a000000000000ull; // +/-3.25
  uint64_t expected=control(masked);
  cases[rt](ctx);
  int mode=(expected>>22)&3;
  uint64_t rounded=sign?0xc008000000000000ull:0x4008000000000000ull; // +/-3
  if((!sign&&mode==1)||(sign&&mode==2))rounded=sign?0xc010000000000000ull:0x4010000000000000ull;
  int bad=ctx->cpu.fpcr!=(uint32_t)value || ctx->guest_fpcr_masked!=masked ||
   ctx->helper_result!=expected || ctx->helper_arg!=rounded ||
   ctx->cpu.fpsr!=0x0800001f || ctx->cpu.nzcv!=0xa0000000 ||
   ctx->cpu.sp!=0xabcdeffedcba9876ull || get_fpcr()!=outer;
  for(int r=0;r<31;r++)bad|=ctx->cpu.regs[r]!=before[r];
  if(bad&&failures++<4)fprintf(stderr,"FAIL rt=%d value=%llx sign=%d fpcr=%x cache=%x hw=%llx flags=%x/%x round=%llx\n",
   rt,values[v],sign,ctx->cpu.fpcr,ctx->guest_fpcr_masked,ctx->helper_result,ctx->cpu.fpsr,ctx->cpu.nzcv,ctx->helper_arg);
  checked++;
 }
 set_fpcr(original);free(ctx);
 printf("{\"cases\":%u,\"failures\":%u,\"host_fpcr_restored\":%s}\n",checked,failures,get_fpcr()==original?"true":"false");
 return failures?1:0;
}
'''
    (OUT / 'native.c').write_text(code)
    run(flags + [str(OUT / 'native.c'), str(OUT / 'candidate-native.S'), '-o', str(OUT / 'candidate-native')])
    output = run([str(OUT / 'candidate-native')])
    (OUT / 'native-result.json').write_text(output)
    # Keep a human-inspectable disassembly from the actual local native test.
    disasm = run(['xcrun','llvm-objdump','--disassemble',str(OUT/'candidate-native')])
    (OUT/'candidate-native-disassembly.txt').write_text(disasm)
    return json.loads(output)

def main():
    assert platform.system() == 'Darwin' and platform.machine() == 'arm64', 'native test requires Mac ARM64'
    OUT.mkdir(exist_ok=True)
    data, flags = prepare_generator()
    model = model_tests(data)
    hardware = hardware_tests(data, flags)
    result = {
      'scope': 'isolated candidate emitter + native ARM64 emitted words, not integrated LinPad',
      'model': model, 'native': hardware,
      'compiler': run(['xcrun','clang','--version']).splitlines()[0],
      'candidate_sha256': sha(HERE/'msr-fpcr-candidate.patch'),
      'test_source_sha256': sha(Path(__file__)),
      'emitted_sha256': sha(OUT/'emitted.json'),
      'limitations': ['No production patch applied', 'No complete JIT/trampoline execution',
                     'No device test', 'No measured opcode frequency or game speedup'],
    }
    (HERE/'test-results.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))

if __name__=='__main__':
    main()
