# CPU accounting verification

Read-only source audit plus a local native ARM64 guest calibration on the macOS-hosted LinPad CLI. No physical-device action or production edit.

`fs/proc/pid.c:178` calls `rusage_get_group`; lines208–210 convert the returned user/system time to100Hz fields14/15. `kernel/resource.c:201–225` sums accumulated exited-thread usage and live host-thread usage. On Apple platforms each live task is queried through `pthread_mach_thread_np` and `thread_info(THREAD_BASIC_INFO)`; lines185–196 read its `user_time` and `system_time`. The local iPhoneOS SDK `mach/thread_info.h:92–93` defines these as user/system run time. `sys_getrusage(RUSAGE_SELF)` uses the same group path. No wall/monotonic time enters those CPU fields; wall time elsewhere in proc stat is used for starttime.

Task host threads are independently created with `pthread_create` in `kernel/task.c:208–210`. Proc statR and statusS are separately synthesized for normal tasks, not measured scheduling states. They cannot establish whether a thread is actually blocked. Per-thread stat files currently repeat the group total, so do not sum them.

The unchanged guest-facing APIs were calibrated with a2-second nanosleep followed by a1-second busy loop. Sleep:2.004199 wall seconds,0.001133 getrusage CPU seconds,0.00 proc-stat CPU seconds. Busy:1.000017 wall seconds,0.877391 getrusage CPU seconds,0.88 proc-stat CPU seconds. Probe exits0/PASS. Raw tracing interleaves the diagnostic value print, so the excerpt retains complete timing fields only; the raw log is retained and hashed.

Therefore the approximately one-core growth in the physical game samples is real host CPU consumption, not sleeping wall time. CPU does not by itself prove useful progress: an executing spin loop also consumes CPU. Pair it with changes in Unity phase logs, loaded components, and visible/presented frames. This calibration is not a physical-device performance benchmark.
