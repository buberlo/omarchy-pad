# FEX munmap length diagnostic

The first direct Hollow Knight Linux start aborts in Mono with `mono_pe_file_unmap` reporting `EINVAL`. The bounded Unity excerpt is preserved separately in `../evidence/hollow-unity-first-30-lines.txt`; this diagnostic reproduces a matching syscall conformance problem without the game.

At FEX commit `5285020de0b30dbdae7b44d0d83541af2d13d002`, `MemAllocator32Bit::Munmap` rejects a length that is not a multiple of 4096. `GuestMunmap` routes mappings below 4 GiB through that allocator, including x86-64 applications. The excerpts preserve that source version and line locations.

All three regression runs here used the **local Mac-hosted LinPad CLI**, not the physical iPad. The native ARM64 probe passes all 18 checks. The same FEX-apple-m1 binary used for the device setup fails 15 checks: non-page-multiple lengths, zero-length handling, and a partial-page unmap. A temporary x86 preload that rounds nonzero lengths to 4096 bytes passes all 18 checks while retaining address validation and error reporting.

The selected output preserves original line numbers; raw trace hashes and local paths are in `PROVENANCE.json`. The native trace contains syscall/stdout interleaving, so its unchanged selected lines include that interleaving. The original traces remain locally preserved.

`fex-munmap-round.c` is a per-command diagnostic, not a final FEX source repair or proof of successful game execution. `munmap-length-probe.c` exercises anonymous/file mappings, nonzero file offsets, invalid addresses, zero length, and neighboring mappings after a partial unmap. `munmap-length-launch.c` supplies the isolated test environment. `fex-munmap-length.patch` is the prepared source correction: it keeps address alignment validation, replaces the erroneous length-alignment rejection with zero-length rejection, and uses the allocator's existing rounded page count. The patch is applied to the local FEX checkout but has not yet been rebuilt or runtime-tested. A rebuilt FEX and physical game retest remain separate gates.
