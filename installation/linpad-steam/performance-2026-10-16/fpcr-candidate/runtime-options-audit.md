# Source findings for the next performance decision

This audit uses the currently inspected local Linux FEX, LinPad source and previously fetched Madeira source. It makes no device changes and does not claim a new upstream comparison or benchmark. Physical build 4450 has a visible main menu and user-confirmed music, with the parent reporting about 7.0 game-view compositor deliveries/s in a qualified foreground static-menu interval. That is not a demonstrated playable gameplay rate.

| Candidate or question | Current source evidence | Consequence |
| --- | --- | --- |
| Run Steam as ARM64 rather than x86 through FEX | `work/steam/serve/steam-local --client` already executes `steamrtarm64/steam` using the ARM64 glibc library path. No FEX invocation is in that path. | That architecture switch is already done. Steam still runs through LinPad's ARM64 engine, and the current UI uses software GL, Xvnc and a viewer. |
| Remove the second translation layer from the x86 Linux game | The native Linux game is x86-64. FEX generates ARM64 instructions, which LinPad handles through its translated/fallback engine. | Native ARM64 Steam does not make the game native ARM64. A direct native runtime would require a separate integration/ABI project; a shell setting cannot accomplish it. |
| Timer/TLS system-register fast paths | `jit/translate.c` already translates MRS CNTVCT_EL0/CNTFRQ_EL0 and MRS/MSR TPIDR_EL0. `ALUOps.cpp::CycleCounter` uses CNTVCT unless ECV enables CNTVCTSS; sanitized MMFR0 does not advertise ECV. Linux `HostFeatures.cpp` explicitly clears SupportsCPUIndexInTPIDRRO. | Do not attribute fallback overhead to ordinary counter/TLS reads without sampled opcodes. |
| MSR FPCR | Missing from the fast decoder; implemented by the gadget. FEX emits writes for rounding-mode set/push/pop. | A focused candidate is feasible; frequency and integrated correctness remain gates. See README and patch. |
| Madeira's AFP-off policy | Fetched `madeira-fex-CPUFeatures.cpp` describes avoiding ARM64EC entry/exit FPCR overhead in a Windows runtime. Linux LinPad's guest MMFR1 is zero, so AFP is already unadvertised. | Copying that policy does not remove legitimate Linux FEX rounding writes. Do not transfer Madeira's historical sample percentages to Hollow Knight. |
| Madeira's LRCPC2 support | Madeira gates it on a native host probe. LinPad already exposes LRCPC2 only when its host feature set and engine support it; FEX derives SupportsTSOImm9 from that feature. | Enabling an unsupported feature by override is inappropriate. Check sampled generated instructions before considering more translator coverage. |
| Disable TSO | FEX config warns that doing so can break multithreaded applications. | No proposed change; preserve correctness. |
| Disable multiblock | It can change compile work and layout. Earlier Mono success with MB0 preceded the corrected proc-maps stack inference; full SDL still failed then. | That earlier result was partial layout avoidance, not evidence of a durable performance setting. Current default remains unchanged. |
| Disable AVX | Current FEX can emulate AVX through its ARM64 backend; Madeira has a different title/runtime policy. | A CPUID change can select different game/library paths or break a binary. No candidate prepared without path evidence and a matched control. |
| FEX disk cache / smaller blocks | Available config names alone do not prove supported integration or an improvement in this workload. | Preserve the separate cache audit and current baseline; no blind toggle here. |

The next useful decision is the 4451 opcode/counter readout. It samples cache hits too, so a hot cached fallback should remain visible. Exact masked opcode grouping should be paired with counts of misses, invalidations and fallback origins. A recent ring is bounded evidence, not a complete instruction census.

For ordinary Steam Library integration, the prepared official x86 SDK package and narrow sdk64 link installer address the observed missing `steamclient.so`. The custom compatibility tool selects Linux depot 367523 and launches patched FEX with architecture-clean libraries. Actual Steam IPC/Play integration is still separate from the direct-launch game results; leave the running game unchanged until the parent schedules that qualification. Do not infer that merely installing the SDK proves successful login, IPC, achievements or overlay compatibility.

Source anchors used:

- `work/LinPad/jit/translate.c`: system decoder, `read_greg`, register map.
- `work/LinPad/asbestos/guest-arm64/gen.c`: `guest_id_reg`, remaining MMFR zero reads, guest HWCAP construction.
- `work/steam/FEX-static/FEX/Source/Common/HostFeatures.cpp`: MMFR AFP/ECV reads, LRCPC2, Linux TPIDRRO policy.
- `work/steam/FEX-static/FEX/FEXCore/Source/Interface/Core/JIT/{MiscOps,ALUOps}.cpp`: FPCR and counter emitters.
- `work/steam/FEX-static/FEX/FEXCore/Source/Interface/Config/Config.json.in`: option defaults and correctness caveats.
- `work/steam/madeira-fex-CPUFeatures.cpp`: previously fetched source at the recorded Madeira FEX revision `f59cab95659e8add0c88f0c76ec1cf483c536dbf`.
- `work/steam/serve/steam-local`: existing ARM64 Steam launch route.

No new runtime option, environment override, compiler tuning, live source edit or device operation was applied.
