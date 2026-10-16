# Build 4444: physical boot rejected

Build 4444 was signed and installed successfully, but both physical launches failed during Alpine init. Each enabled the persisted 512 MiB JIT arena and then terminated with guest SIGSEGV, exit 139, at musl offset `0x6bc0c`. Neither reached the Linux desktop or a game test. **Do not use build 4444 as the recommended installation.** Build 4443 remains preserved as the preceding qualified build.

The first failure occurred after boot at 17:49:29 +08:00; the ordinary retry failed at 17:51:41 +08:00 on 16 October 2026. Both record the same four preceding syscalls: set_tid_address, brk, brk and mmap. The curated excerpts retain the JIT setting, boot timing and exact fault/exit lines. Raw console files remain in the local workspace; hashes and source line numbers in PROVENANCE.json identify them without copying unrelated diagnostics.

The protected-memory guard in source commit `29c99bc46075a048f422fe4b714e7d531be726ee` correctly rejected PROT_NONE host reads in focused local probes, but exposed legacy zero-permission mappings for the kernel-provided vDSO and vvar. Exact bundled Alpine musl/BusyBox reproduced the boot failure locally. The follow-up gives vDSO explicit read/execute permissions and vvar read permission while retaining the protected-memory guard. That correction is in build 4445; its separate archive records the successful desktop boot and protected-memory tests. This folder records the rejected build only.

A read-only audit of every production pt_map/pt_map_nothing/pt_map_lazy/pt_set_flags caller and direct page-flag assignment found no other implicit-readable zero-rights constructors. ELF loads start with P_READ; stack and brk have P_WRITE and still satisfy the guard's P_RWX test; fork, COW, remap and discard preserve original rights; file, video, GPU and lazy mappings propagate requested protections.

Signed app, IPA, symbols and complete logs remain preserved locally for diagnosis. This narrow evidence folder does not contain release binaries, full preferences or authentication material. It makes no desktop, GPU, game-start or performance qualification for build 4444.
