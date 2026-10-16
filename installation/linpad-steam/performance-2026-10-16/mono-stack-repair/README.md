# Mono main-stack bounds repair and diagnostic evidence

Status: the cause of the build4446 Mono-startup stall is identified and reproduced locally. The host `/proc/maps` correction passes the focused local10-check regression (the unchanged probe fails6 checks on the previous CLI). The unchanged default-FEX Mono controls now both pass; build4447/device results are pending. Hollow Knight gameplay and performance remain unverified.

## Confirmed cause

LinPad's memory-map printer used a sparse iterator inside its contiguous-region loop. That can merge mapped ranges across unmapped page-table subtrees and extend the final stack map to the address-space limit (`0x1000000000000`) rather than its real end (`0x808008000000`). glibc2.41's initial-thread `pthread_getattr_np` calculation subtracts this enormous excess from the correct128MiB stack limit, wraps the unsigned result, and selects a lower bound inside FEX's live metadata. Mono then protects32KiB at that false stack bottom for its stack-overflow guard. A subsequent ARM64 FEX block-link map insertion faults inside the protected range.

The exact local observer, source mapping, bounded disassembly, mathematical reproduction, and input fingerprints are in [fault-diagnosis](fault-diagnosis/README.md). No runtime/game binary was modified to establish this cause. The narrow host correction advances the inner contiguous-map loop one page at a time, while retaining sparse traversal for the outer search through holes.

## Evidence boundaries

| Gate | Observed result |
|---|---|
| Build4446 exact embedded SDL initialization | Previously validated on physical iPad: combined `0x3221` succeeds and returns one display; see adjacent `unity-sdl-repair`. |
| Build4446 game, normal FEX, PID295 | Stops progressing after Wayland selection and Mono paths; nine threads, flat CPU time. ARM64 FEX protected-node fault is recorded. Later deliberately stopped, wrapper exit137. |
| Build4446 game with sampler, PID1729 | Same stage/fault. Nine native `tgkill(SIGURG)` calls succeed, but no guest `X86_SAMPLE` callbacks arrive. Later deliberately stopped, wrapper exit137. This is not a successful physical sampling result. |
| Sampler local control | Three distinct x86 instruction pointers captured for sleeping/running/futex threads; workers resume and join; application handler replacement is preserved; sender refuses disarmed token; exit0. |
| Local GC-only controls | Exact Mono GC initialization/collection passes with no SDL, timer before/after, or full SDL before; dummy display used locally. |
| Local Mono initialization, normal FEX before fix | All four controls exceed their45-second host deadlines; runner termination is recorded, not a natural crash exit. |
| Local `FEX_MULTIBLOCK=0` control | No-SDL initialization succeeds in12.528s; full-SDL control still faults/times out. Allocation-layout-dependent partial avoidance only. |
| Physical `FEX_MULTIBLOCK=0`, PID2331 | Progresses to InputSystem/display initialization, then another protected-memory guest fault at Unity file offset `0x12c033a`. This is not game rendering or a semantic repair. |
| Host mapping correction | Same native boundary probe: before6 failures/exit1, fixedCLI10/10PASS/exit0. See `host-fix`. |
| Local default-FEX Mono after correction | No SDL: nonnull domain, exit0 in13.794s. Full embedded SDL`0x3221` first: nonnull domain, exit0 in27.971s. Same probe/launcher/FEX; no preload or multiblock override. |
| Local glibc stack-bound observer after correction | Stable lower boundary `0x808000000000`, size128MiB, matching direct soft limit. Standalone unchanged x86 probe fails before/exit1, passes after/exit0. |
| Local Mono guard observer after correction | Same caller `Mono+0xbd91f` now protects `0x808000001000` for32KiB within the proper stack region, then initialization returns a domain/exit0. |
| Build4447 physical Mono / actual game | Pending. No frame or gameplay qualification from this repair yet. |

Physical stage excerpts include original source line numbers and fingerprints in `source-manifest.json`; they omit unrelated process/account/console data. `device/scripts` preserves the exact scoped launch and sampling procedures, including the temporary LAN report endpoint. These are historical diagnostic scripts, not a production launcher or a request to run them on another device.

## Contents and reproduction

- `host-fix`: narrow source patch, authored native boundary probe and matched before/after results.
- `fault-diagnosis`: FEX/Mono disassembly, exact ELF mapping, observer evidence, and glibc stack-size model.
- `sampler`: opt-in SIGURG register-sampling source, compiled diagnostics, safety/ownership behavior, and local validation. SIGURG has a default-ignore action; no game-wide signal reservation is claimed.
- `mono-probe`: exact-input hashes, authored probe/launcher/observer source, small built diagnostics, local runner and result summaries. Its `before`/`after` command argument describes SDL-versus-Mono initialization order, **not** host-fix revision; post-fix results will use explicit `after-maps` names.
- `device`: narrow build4446 physical evidence and launch scripts.

The existing FEX binary remains SHA256 `343c8c29bfb933d317ffea15bbb898f58bcdeb82911461a70b5235afb9600e0d`. The probe uses owned local Mono SHA256 `682d74f03efd9cebd05d38e82c4b296a0a1865f070440281aa1ac51022220b94`, matching core assembly SHA256 `19c81960f79980f14cd4dc271ed42c3e7f031f024c17977fc0f4a9f672a23b86`, and Unity SHA256 `12ce7af93c41b13b3e7d9b75b591336cc56c9ce113714143fd897cc1dcf00e33` only as local inputs. Their bytes are intentionally absent from this archive.

Run the mathematical diagnostic with `python3 fault-diagnosis/glibc-stack-model.py`. Actual CLI instructions are in `mono-probe/README.md`; they require the separately prepared runtime and locally owned game inputs. No GitHub Actions are used.

The production source correction is commit `9beb5dea2eeaffd7ebd27952592cba658fe12bb4`. The prepared `mono-probe/mono4447-probe` script is included with hashes for a later physical default-FEX/real-Wayland comparison; its presence is not evidence that it has executed.
