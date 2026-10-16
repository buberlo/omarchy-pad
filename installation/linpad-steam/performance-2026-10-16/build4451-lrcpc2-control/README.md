# Failed build 4451 FEX feature-mask control

At 13:57:22 UTC on 2026-10-16, the same signed app was used to launch Hollow Knight with `FEX_HOSTFEATURES=disablelrcpc2`. The guest immediately exited 139, before the candidate Unity log existed. The host console identifies guest PID 1703, signal 11, code 0, anonymous generated-code PC/address `0x80013505aa34` and LR `0x800124041214`. These lines do not establish the underlying defect. This is a failed diagnostic configuration, not a recommended setting.

The candidate launcher is byte-for-byte identical to the preserved baseline after removing its one added feature-mask line and restoring its unique output/log prefix. Exact hashes and the diff are included. No native translator or app source change was made for this experiment.

The [baseline build 4451 checkpoint](../build4451/VERIFICATION.md) independently reached the foreground German title menu at actual 659×540. Its 13:55:24–54 capture recorded 212 rows over 30 seconds (7.0667 composited deliveries/s), with no sequence gaps or selected-view remap. The baseline was then manually stopped with TERM/KILL at 13:56:50–53 for this test. Screenshots, frame rows, parser and original menu report live in that baseline archive rather than being duplicated here.

There is no candidate cadence measurement, so no before/after performance ratio is valid. The feature mask was intended to avoid the measured LRCPC2 fallback family while preserving ordered memory operations; this failed execution does not prove that the intended alternate instruction path is correct. Later local/native diagnosis must be recorded separately.

This folder is frozen historical evidence. Its checksum list covers only the lightweight files here. Raw device console/process/maps data and proprietary game binaries remain outside Git. The proc counter observations and original signing/SMC/source qualification are in the baseline checkpoint. No private-release upload or normal gameplay acceptance is claimed by this folder.
