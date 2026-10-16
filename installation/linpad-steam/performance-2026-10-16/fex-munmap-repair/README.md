# LinPad FEX allocator repair build

This is an isolated local ARM64 Linux build of FEX commit `5285020de0b30dbdae7b44d0d83541af2d13d002`, using the existing PeronGH/FEX-static release recipe at `da8478086d7fa5067d8522a212f902834f222359`.

The three release patches in `patches/` are applied first. `0005-Linux-munmap-round-nonzero-length.patch` then corrects the low-address allocator: Linux permits any nonzero `munmap` length and rounds it up to pages. Page-aligned addresses are still required, and a zero length still returns EINVAL. The original source checkout is not reset or rebuilt in place.

The build uses Ubuntu 24.04 ARM64, LLVM/Clang 18.1.3, Release, static PIE, LLD, ThinLTO, and `TUNE_CPU=apple-m1`. Other switches match the release recipe: FEXConfig, ccache, offline telemetry, and GDB symbols are disabled. No GitHub Actions or remote runner is used.

`builder-packages.json` records every official Ubuntu package URL, size, version, and SHA256. Packages are downloaded over HTTPS and checked before the offline Docker image is built. `Dockerfile` pins the Ubuntu base manifest digest. The two Python metapackages need a second installation step after their pre-dependencies; `apt-get check` and an empty `dpkg --audit` are required before the image can complete.

Build commands are in `build-inside.sh`. The dedicated local Colima profile is `linpad-fex`; its Docker context is `colima-linpad-fex`. The host directory containing this document is mounted at `/work` in the compiler container. The existing/default Docker context and VM are not changed.

`test-inside.sh` compares the original and rebuilt binaries on the same ARM64 Linux VM. `munmap-length-low-probe.c` is a separate x86-only MAP_32BIT variant of the original regression, ensuring the failing low-address allocator is exercised even when running on a normal Linux host. It covers anonymous and file mappings of seven lengths, an offset file mapping, invalid unaligned address, zero length, and a partial page unmap that preserves adjacent pages. No preload shim is used.

Build and local regression are complete: the original binary fails 15 of 18 checks, the patched binary passes all 18, and the native ARM64 control passes all 18. The rebuilt ELF is AArch64 static PIE with no PT_INTERP or DT_NEEDED entries. No preload shim was used. The physical iPad also passed all 18 checks on LinPad build 4445 at 2026-10-16 10:01:28 UTC. Hollow Knight gameplay remains unverified.

The binary is `FEX-apple-m1-munmap` (SHA256 `343c8c29bfb933d317ffea15bbb898f58bcdeb82911461a70b5235afb9600e0d`). Keep the existing `/opt/fex/FEX-apple-m1` as the rollback copy; the new file can be installed beside it and selected with the compatibility tool's `LINPAD_FEX_BINARY` override. The archived `install-patched-fex-device.sh` records the installation and test already executed on the physical device. Its local server address belongs to that run; it is not automatically executed by unpacking this archive.

The final compiler image was produced in two local stages because the first minimal image omitted `clang-tools-18`. The archived `Dockerfile` and 126-package manifest build an equivalent complete toolchain in one step. The actual two-stage image ID is retained in `provenance.json`; the additional CMake scanner path selects that same LLVM 18 package and does not change code generation.

To recreate the isolated source tree, clone the pinned FEX commit into `FEX/`, initialize the populated submodules listed in `submodules.txt`, and apply `patches/*.patch` in filename order. Uninitialized entries beginning with `-` were unnecessary for the FEX target. Run `fetch-builder-debs.py`, build the Dockerfile locally, mount this directory at `/work`, and execute `build-inside.sh`. A complete rebuild also needs the original FEX baseline binary and the probe rootfs/compiled test fixtures, whose sources and hashes are retained by the surrounding performance archive. The original checkout is never reset.

Test logs, compiler package versions, exact source and patch hashes, and ELF inspection are included. No credentials or Steam account data are included.

The physical-device script verifies the runtime and low-address probe SHA256 values, explicitly unsets `LD_PRELOAD`, and invokes `/opt/fex/FEX-apple-m1-munmap /tmp/munmap-length-x86-low` with the base x86 runtime paths. `patched-fex-device-validation.txt` preserves the captured stdout, including all 18 PASS results and the completion marker. `provenance.json` records the exact environment, script/report hashes, and operator-reported LinPad build. The runtime binary is byte-for-byte identical to the locally validated build.
