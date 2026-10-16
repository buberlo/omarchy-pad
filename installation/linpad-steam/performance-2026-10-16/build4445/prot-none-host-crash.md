# Build4443 host crash: kernel read of guest PROT_NONE

Physical report: `crashes4443/iSH ARM64-2026-10-16-173752.ips`.
App/dSYM UUID: `C8B4FFA5-ADC8-3C76-9FAF-1986637FC46F`.
Fault thread: `FEX-apple--4320`.

The report identifies SIGBUS/KERN_PROTECTION_FAILURE while host memcpy reads
one byte from `0x727ae54000`. The host mapping is a 16 KiB PROT_NONE region.
Registers retain guest address `0xc000000000` and a one-byte copy length.

Matching dSYM symbolication:
- `0x1028f4750`: `user_read_task`, user.c:38 (inlined copy at user.c:14).
- Return address `0x1028f012c`, call instruction `0x1028f0128`:
  `handle_interrupt`, calls.c:511.
- App load address `0x1021a8000`.

That call is the pre-existing V8 workaround's frame-chain scan. FEX may use
x29 as its own state, not a native frame pointer. The scan called user_get on
a no-access mapping. `mem_ptr_nofault(MEM_READ)` previously returned any mapped
page regardless of its guest protection; the kernel then dereferenced the
host-inaccessible backing. The same issue affects ordinary syscall copies.
This report does not show a concurrent unmap race.

Minimal fix:
- Reject MEM_READ if the mapped page has no P_READ/P_WRITE/P_EXEC bits.
- Reject the same access to lazy no-access reservations before materializing
  a host page.
- Preserve the existing treatment of readable, writable and executable pages.
- Keep the fix in shared address translation, so syscall copies, diagnostic
  walks and other callers receive NULL/EFAULT instead of an unsafe host pointer.

## Local before/after evidence

`tests/manual/steam-compat/prot-none-read-probe.c` is native AArch64.
- Default: pipe write reading a PROT_NONE buffer. Old CLI host-crashes in
  user_read_task+128; fixed CLI returns EFAULT. Adjacent 4 KiB guest guards remain
  inaccessible when their shared 16 KiB host page becomes readable. Protection
  changes and valid data accesses continue to work. Includes a lazy 128 MiB
  reservation, promotion of one page, and inaccessible neighbors.
- `fault`: set x29 to a mapped PROT_NONE page and execute a deliberate null
  load. Old CLI host-crashes during the diagnostic frame walk. Fixed CLI
  delivers the null fault to the guest handler.
- `fault-lazy`: same fault with a lazy 128 MiB guard reservation; fixed CLI passes.

Normal JIT and gadget paths pass syscall and frame-pointer cases. Forced tier 2
and compact tier 1 pass the frame-pointer case. The earlier 24-case exact-address
null-fault regression still passes. Logs are `prot-none-*.txt`; compilation
log `prot-none-build.txt`. These are local Mac CLI results, not device claims.

Staged device binary: `serve/prot-none-read-probe`.
SHA256 `f0f3086f9c477aa8c10b4e32c597534d13b4cfefabc8224fb86309d6b8eb4671`.
Run directly, without FEX, with no argument, `fault`, and `fault-lazy`.

Source files ready for build 4444:
`kernel/memory.c`, `kernel/memory.h`,
`tests/manual/steam-compat/README.md`,
`tests/manual/steam-compat/prot-none-read-probe.c`.
