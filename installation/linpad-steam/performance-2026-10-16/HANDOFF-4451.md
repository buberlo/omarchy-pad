# Saved handoff — 16 October 2026

Work stopped at the user's request to save and push everything before further token use.

## Current device

LinPad1.4.0 build4451 is installed and running with512MiB JIT and both approved memory entitlements. No game is currently running. Last successful baseline displayed Hollow Knight's German main menu at659×540, with212 compositor frame deliveries in30seconds (7.0667/s); this is not gameplay/display FPS. User reported audible but severely stuttering music on4450. Normal gameplay and smooth audio remain unqualified. Linux uses the available iPad filesystem dynamically; no fixed VM disk needs expansion.

## Latest diagnosis

All1223 retained startup opcode samples are LRCPC2 LDAPUR/STLUR. LinPad's fast translator lacks this family and repeatedly falls back to gadget execution. The baseline was manually terminated at13:56:50–53UTC. A new process with only FEX_HOSTFEATURES=disablelrcpc2 immediately exited139 before Unity at13:57:22UTC. Do not make that flag a production default.

Local reproduction: a dynamic x86 probe passes default JIT, fails with the feature override under JIT, and passes with the same override under the gadget engine. The exact host faulting opcode/address is not yet proven. The probable ordered-access alignment/fault-delivery issue remains a hypothesis.

## Resume here

1. Read build4451/VERIFICATION.md, build4451-lrcpc2-control/README.md and rcpc2-config-repro/README.md.
2. Implement and review native LRCPC2 support following build4451-ss-analysis/LRCPC2-DESIGN.md. No production translator patch was applied. Preserve memory ordering, address translation, fault metadata and cross-page semantics.
3. Run meaningful actual-translator/guest probes, build/sign4452 locally, then install only after verifying current device ownership. GitHub Actions remain disabled.
4. Retest the unchanged default FEX route, verify fallback samples and matched foreground menu cadence, then actual gameplay/audio. Do not infer playability from menus or intros.
5. Only after the working route is qualified, integrate the diagnostic Wayland/EGL launch into the production Steam compatibility tool and qualify Steam-to-game launch. The separate x86 Steam SDK repair is prepared, not installed.

The FPCR candidate is saved but unapplied and deprioritized: actual samples contained zero FPCR writes. Build4450 intro used empty save slot2; existing slot1 was left untouched. No proprietary game binaries or account secrets are part of this archive. Existing device/Linux data must be preserved when reinstalling the same app bundle.

## Local working context

Repository: work/LinPad. Device bundle: com.konradkern.linpad. Device host PID21692 was last observed on4451; verify before using. Guest348 was manually stopped; guest1703 failed. DeviceHub keyboard capture must stay off because it previously captured the Mac keyboard. The local8766 diagnostic server and device-console session may remain attached; verify live state before reuse. Current test launchers and helper copies are under /tmp on the device; original sources are preserved in the build/control archives. Root filesystem, FEX, Mesa, EGL, Steam, and game data are distinct from the IPA. No automatic continuation is scheduled.
