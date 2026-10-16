# Mono startup stall: protected FEX allocation audit

This audit began as read-only investigation and subsequently identified a concrete procfs boundary defect. The final section records the confirmed protection requester, narrow production fix and matched native regression. No device interaction was performed by this agent. The live build 4446 game stall and local standalone Mono initialization failure reach the same FEX tree operation; physical game recovery still requires a new build and device test.

## Corrected fault locations

`../build4446/device-console.log:102,596` records physical PIDs 295 and 1729 with SIGSEGV code 2, PC `0xef94a278`, LR `0xefb2c100`, and protected addresses `0x8003292d6ac0` / `0x8003292d71c0`.

The console's `FEX+0x1bb278` / `+0x39d100` labels are relative to the executable mapping, not the ELF load bias. The load bias is `0xef782000`; the corresponding ELF virtual offsets are **`0x1c8278`** and **`0x3aa100`**. Independent disassembly by the FEX agent identifies PC as `std::_Rb_tree_insert_and_rebalance+0x38`. Caller classification is being checked against FEX block-link insertion.

`mono-0-fault-report.txt:20325,20356` records local PCs `0xef94a2a4` and `0xef94a2b0`, therefore ELF offsets **`0x1c82a4` / `0x1c82b0`**, with the same LR `0xefb2c100`. The instructions read and then write the same node address `0x800157d640b0` in the same tree operation. No x86 fault-reporter callback was emitted before the subsequent futex wait.

## The local fault is inside the requested protection interval

The local log records:

```text
16722 mmap64(0x800157d5e000, 0x12000, RW, MAP_FIXED|PRIVATE|ANONYMOUS) = 0x800157d5e000
20303 mprotect(0x800157d5f000, 0x8000, PROT_NONE) = 0
20325 read fault at 0x800157d640b0
20356 write fault at 0x800157d640b0
```

The requested interval is `[0x800157d5f000, 0x800157d67000)`. The fault address is 0x50b0 bytes inside it. `/proc/maps` in the same log, lines 20505–20507, reports exactly that interval as `---p` with writable regions on each side. This observation does not support a fault caused by lowering an adjacent 16 KiB host page.

LinPad `kernel/mmap.c:448–455` requires a 4 KiB-aligned start, rounds the length to guest pages and calls `pt_set_flags`. `kernel/memory.c:805–825` changes each requested guest entry's flags. Its host `mprotect` branch runs **only when read/write rights increase**, and first requests host RW so neighboring 4 KiB guest pages sharing an Apple 16 KiB page remain accessible. A transition to PROT_NONE does not lower host permissions in this path. Guest page permissions still reject the access, including when host backing is writable (`memory.c:928–941`).

Existing `tests/arm64-insn/hostprot.c` specifically tests NONE→READ next to a writable page, including atomics; `tests/manual/steam-compat/prot-none-read-probe.c` tests guarded neighbors and rights transitions. This audit did not rerun those tests or claim an additional result.

## Allocation sequence matches FEX's block-link buffer resource

FEX source root: `work/performance/fex-build/FEX` relative to the workspace.

`FEXCore/Source/Interface/Core/LookupCache.h:74–83,142–144` stores the block-link tree in `named_monotonic_page_buffer_resource` and inserts nodes in `AddBlockLink`. `LookupCache.cpp:17–21` names it `FEXMem_BlockLinks`.

`FEXCore/include/FEXCore/fextl/memory_resource.h:80–98,112` starts allocations at 4 KiB and increases the next buffer size by 1.5, rounded to 4 KiB. The resulting size sequence is 0x1000, 0x2000, 0x3000, 0x5000, 0x8000, 0xc000, 0x12000, 0x1b000. The local log contains that sequence:

| Log line | Base | Bytes |
|---|---|---|
| 242 | 0x800136045000 | 0x1000 |
| 2960 | 0x800137d40000 | 0x2000 |
| 4554 | 0x800137d42000 | 0x3000 |
| 7461 | 0x800137d45000 | 0x5000 |
| 8951 | 0x800137d4a000 | 0x8000 |
| 12220 | 0x800157d52000 | 0xc000 |
| 16722 | 0x800157d5e000 | 0x12000 |
| 19554 | 0x800157d70000 | 0x1b000 |

The faulting tree node lies within the 0x12000 allocation. Together with the tree-operation disassembly, this is strong evidence of an affected FEX block-link buffer. The allocation-size match alone is not a proof of which caller changed its permissions.

## Smallest useful next observation

The last high-address PROT_NONE call has no preceding matching high-address `madvise`. FEX's rpmalloc decommit (`FEXCore/Source/Utils/AllocatorHooks.cpp:164–175`) normally performs MADV_DONTNEED immediately before PROT_NONE. Guest `GuestMprotect` (`Source/Tools/LinuxEmulation/LinuxSyscalls/SyscallsSMCTracking.cpp:471–484`) forwards a requested range unchanged before updating tracking.

Capture a diagnostic x86 `mprotect` call with its return address/library offset and unchanged forwarding, paired with stack bounds. A match would establish a guest-origin request; no match would require checking direct syscalls or FEX's own native protection callers. The memory agent owns that observer; no duplicate implementation is needed. Mono opens `/proc/self/maps` before the failure (local log 10564 and 12338), so map-derived stack bounds are worth checking, but this is a hypothesis rather than an established cause.

Do not weaken PROT_NONE enforcement or alter neighboring-page semantics to bypass this fault. A production fix requires identifying the requester and why it selected a live FEX allocation. If an independent page-granularity regression is needed, the smallest extension is a high-address three-page mapping: protect only the middle page, verify left/right accesses remain valid, the middle returns EFAULT for a kernel copy, then restore it and verify all three. That would test the proposed kernel mechanism independently of Mono.

While this audit was being recorded, the memory agent reported that the same local no-SDL Mono probe with only `FEX_MULTIBLOCK=0` changed completed initialization with a nonnull domain and exit 0 in 12.528 seconds. That is a promising local control, not a device/game result or proof of the protection requester. The x86 observer remains necessary to distinguish a corrected cause from avoidance of the affected allocation/layout.

## Confirmed caller and procfs fix

`mono-0-mprotect-excerpt.txt` confirms the x86 caller is the game's `libmonobdwgc-2.0.so+0xbd91f`. It requests `mprotect(0x800157d5f000, 0x8000, PROT_NONE)`. Static analysis by the memory/FEX agents identifies Mono's stack-overflow guard: take the inferred stack base, add one page and guard 32 KiB. The observer forwards the call unchanged. Its initial `pthread_getattr_np` reports base `0x800137d45000`, size `0x7ed02bb000`; the next query changes the inferred base to `0x800157d8b000`. Actual stack locals remain near `0x808007fff000`.

The terminal map was falsely printed as `808007800000-1000000000000`, rather than `808007800000-808008000000`. Independent analysis of glibc 2.41's main-thread stack inference shows that this huge terminal endpoint underflows its stack-limit subtraction and makes a prior FEX mapping determine the lower bound. That explains why the computed Mono guard intersects live FEX metadata. The maps defect, caller and invalid stack bounds are independently observed; application recovery remains a separate validation gate.

The narrow fix in LinPad `fs/proc/pid.c` changes only the **inner contiguous-region step** from `mem_next_page(mem, &page)` to `page++`, with a comment. The sparse iterator remains in the outer search for the next mapped region. Sparse skipping is valid for finding a mapping, but cannot establish that two mappings are contiguous or determine the last mapped page's endpoint.

The new native AArch64 `tests/manual/steam-compat/proc-maps-boundary-probe.c` places anonymous islands at the end of separate 2 MiB page-table subtrees and adds an 8 MiB stack-like mapping at the exact high-address shape. It checks exact boundaries, gaps, adjacent coalescing, protection splits and the new last region after unmap. Saved pre-fix CLI: **6 failures in 10 checks, exit 1**. Rebuilt fixed CLI: **all 10 pass, exit 0**. See `../proc-maps-regression/{before.txt,after.txt,provenance.json}` for exact commands and hashes. This is host-local native guest validation; exact default-FEX Mono tests are owned by the memory agent and physical validation by the parent task.

The memory agent completed the unchanged **default-FEX** local controls after the fix: no SDL → Mono initialization passes with a nonnull domain and exit 0 in 13.794 s; full embedded SDL `0x3221` using the dummy display backend → Mono initialization passes with a nonnull domain and exit 0 in 27.971 s. Both corresponding pre-fix controls timed out after 45 s. `stack-limits-after.txt` now reports stable base `0x808000000000`, size `0x8000000` (128 MiB), matching the directly read RLIMIT_STACK. Results are recorded in `after-maps-mono-results.json`. No FEX_MULTIBLOCK workaround is required for these local passes. Device game startup, rendering and performance remain separate gates.
