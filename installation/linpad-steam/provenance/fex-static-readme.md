# FEX-static

A faster drop-in replacement for `qemu-user-static` on ARM64 Linux.

FEX-static is a single static binary of [FEX](https://github.com/FEX-Emu/FEX), the x86/x86-64 JIT emulator. Register it with binfmt_misc and x86-64 binaries run transparently — typically 3–10x faster than QEMU's TCG interpreter. No FEXServer, no RootFS image, no shared libraries: one file works on any arm64 system.

## Quick start

Pick the variant for your CPU: `FEX-cortex-a72` (baseline, runs on any ARMv8.0 device incl. Raspberry Pi 4), `FEX-cortex-a76` (ARMv8.2, Raspberry Pi 5 and most modern boards), or `FEX-apple-m1` (Apple Silicon VMs).

```sh
curl -fsSL -o FEX https://github.com/PeronGH/FEX-static/releases/latest/download/FEX-cortex-a72
chmod +x FEX
printf '%s\n' ":FEX-x86_64:M:0:\x7fELF\x02\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x3e\x00:\xff\xff\xff\xff\xff\xfe\xfe\x00\x00\x00\x00\xff\xff\xff\xff\xff\xfe\xff\xff\xff:$PWD/FEX:POCF" | sudo tee /proc/sys/fs/binfmt_misc/register

# then you can just:
./some-x86_64-binary
docker run --platform linux/amd64 ...
```

Configuration is hermetic: only `FEX_*` environment variables (e.g. `FEX_ROOTFS`) are honored — config files under `~/.fex-emu/` and `/usr/share/fex-emu/` are ignored.

## Repo layout

Upstream FEX is tracked as a pristine submodule; this repo only carries the patches and build glue.

- `FEX/` — shallow submodule pointing at upstream `FEX-Emu/FEX`
- `patches/` — plain unified diffs applied on top of the pinned commit
- `scripts/apply-patches.sh` — reset the submodule to the pin and apply all patches (idempotent)
- `scripts/build.sh` — apply patches, then configure and build `build/Bin/FEX`

## Building

Requires clang, lld, cmake, ninja, nasm, and static libc/libstdc++ archives.

```sh
git clone --recurse-submodules https://github.com/PeronGH/FEX-static.git
cd FEX-static
./scripts/build.sh
```

CI builds, smoke-tests, and publishes the binary as a GitHub release tagged with the upstream commit hash. A daily workflow bumps the submodule to upstream main when the patches still apply, and dispatches a fresh build.

## Used by

- [ArcBox](https://github.com/arcboxlabs/arcbox) — uses FEX-static to run amd64 binaries in its arm64 VMs on Apple's Hypervisor framework.
