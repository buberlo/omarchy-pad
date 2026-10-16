# Steam/FEX compatibility probes

These standalone guest programs exercise runtime contracts used during Steam
startup. Compile the native probes for AArch64 Linux. A static musl build is
convenient for running them under both the CLI emulator and iPad app.

- `runtime-probe.c`: MIDR_EL1 and relative faccessat through pipe, file and
  directory descriptors. Requires `/opt/fex/hello-x86_64` to exist.
- `eventfd-probe.c`: semaphore reads, ordinary counter reads, nonblocking
  exhaustion and epoll readiness.
- `pselect-probe.c`: null and packed-null signal masks, empty and ready pipes.
- `epoll-oneshot-probe.c`: registrations persist after delivery, MOD rearms
  an already-ready edge and updates data, DEL accepts disabled registrations,
  and hangup stays disabled until rearmed.
- `epoll-nested-probe.c`: MOD rearming an already-ready one-shot eventfd
  wakes a thread blocked on an outer edge-triggered epoll set.
- `proc-thread-list-probe.c`: lists only process leaders at /proc, while
  preserving direct thread-ID status lookup and /proc/self/task entries.
- `proc-inet-probe.c`: IPv4/IPv6 TCP and UDP loopback endpoints, listening
  and connected TCP state, socket inode matching, and dup lifetime.
- `proc-fd-exit-probe.c`: repeatedly scan a child process file-descriptor
  directory while it exits. The old runtime crashes in `proc_pid_fd_readdir`;
  the repaired CLI completes 300 rounds.
- `pthread-glibc-probe.c`: dynamic glibc thread creation with different stack
  sizes and repeated joins. Link against glibc and run with the isolated
  matching loader/libraries; it does not alter the musl system runtime.
- `x86-runtime-smoke.c`: compile for x86-64 Linux with pthread support and run
  through FEX. Checks threads/mutexes, SSE2 arithmetic, generated executable
  code and signal delivery. This does not qualify a full application or game.

The pselect and epoll probes passed in the Mac CLI and on the physical M2 iPad.
With the approved memory entitlements and `ulimit -s 8192`, all three FEX-static
variants at upstream revision 5285020de passed the x86 runtime smoke test on
the iPad. These results do not establish that Steam's Chromium UI works.
