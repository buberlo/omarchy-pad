# FEX metadata fault caused by incorrect main-stack bounds

The local Mono observer identifies the request that makes live FEX block-link metadata inaccessible: Mono's stack-overflow guard initialization calls `mprotect(..., 32768, PROT_NONE)` using a wrong lower stack boundary. The selected address lies inside the FEX JIT's `BlockLinks` monotonic allocator. This is not a Mono GC scan.

1. LinPad's recorded last maps entry is `808007800000-1000000000000 rw-p`; the real stack allocation ends at `0x808008000000`.
2. glibc 2.41 `pthread_getattr_np` finds the maps entry containing its initial stack end. It subtracts the distance from stack end to mapping end from `RLIMIT_STACK`. The incorrect enormous mapping end causes unsigned underflow. It then clamps the size using the preceding mapping's end, which is live FEX metadata. This explains why the returned stack base changes as FEX allocates buffers even though the real stack does not move.
3. Exact Mono disassembly has stack-bound helper call at RVA `0xbd8b8`, adds one page to the returned lower bound, rounds 32768 bytes to a page, then calls `mono_mprotect` at `0xbd91a` (return `0xbd91f`). The embedded assertion explicitly names `tls->stack_ovf_guard_base` and `tls->stack_ovf_guard_size` from `mini-exceptions.c`.
4. Local observer captures that request at `0x800157d5f000` for `0x8000` bytes. The subsequent FEX fault address `0x800157d640b0` is inside this exact interval.
5. Physical fault PC `0xef94a278` minus the actual ELF load base `0xef782000` is VMA `0x1c8278`: `std::_Rb_tree_insert_and_rebalance+0x38`, storing a parent right-child link. The caller return is VMA `0x3aa100`. Its node layout (56-byte node, key of two 64-bit fields, 8-byte value) and callsites match FEX's `GuestToHostMap::BlockLinks` insertion in `Arm64JITCore::ExitFunctionLink`. Local failures at rebalance offsets `+0x64/+0x70` access the same protected node.

`ExitFunctionLink` holds a code-invalidation lock and the lookup-cache write lock while inserting. Its signal-deferring guard is consistent with the observed lack of guest sampling callbacks after the synchronous host fault; this audit has not established every signal-handler lock transition.

The narrow semantic host correction is in `fs/proc/pid.c`, `proc_maps_dump_mem`: its inner contiguous-region loop must advance by one page, rather than use the sparse `mem_next_page` iterator. That iterator skips absent page-table subtrees, so it can wrongly bridge unmapped holes or extend the final mapping to `MEM_PAGES`. The outer search through holes should retain sparse advancement. No FEX `/proc/stat` shim is indicated: FEX's emulated-file registry does not replace `/proc/self/maps` in this build.

`glibc-stack-model.py` is a mathematical reproduction using captured bounds, not a regression of the host implementation. A production fix needs an actual sparse-page-table maps regression plus native/FEX `pthread_getattr_np` and exact Mono initialization validation. Changing `FEX_MULTIBLOCK` is not a semantic fix: the no-SDL local control escaped the overlap, while the full-SDL control still reproduced it.

Disassembly labels nearest stripped symbols are not reliable names for internal functions. The Mono routine is identified from its own instructions, call targets, and embedded assertions. The runtime PC mapping uses the ELF load base rather than LinPad's misleading displayed `FEX+offset`.

The provenance file hashes the exact local FEX, libc, Mono, logs, and source references. Proprietary Mono and Unity binaries are not included.

Reference: [glibc 2.41 pthread_getattr_np](https://raw.githubusercontent.com/bminor/glibc/glibc-2.41/nptl/pthread_getattr_np.c), especially lines 94-135.

Archive update: the host fix and actual local before/after comparisons are now complete; see `../host-fix` and `../mono-probe/after-maps-mono-results.json`. The unchanged default-FEX controls initialize Mono successfully with no SDL and full dummy-backend SDL. Physical build4447 qualification remains separate and pending.
