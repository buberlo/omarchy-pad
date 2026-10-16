# Local reproduction of the rejected LRCPC2 feature control

Completed 2026-10-16 on the existing macOS-hosted LinPad CLI and patched FEX binary. These are local diagnostic results, separate from the physical build-4451 game failure. No production source, iPad settings, game files, or installed app was changed by this audit.

| Guest probe | FEX setting | LinPad JIT | Result |
| --- | --- | --- | --- |
| Static x86 hello | Default | Enabled | Exit 0 |
| Static x86 hello | `disablelrcpc2` | Enabled | Exit 0 |
| Dynamic x86 glibc stack-limits probe | Default | Enabled | Exit 0, stack checks pass |
| Same dynamic probe | `disablelrcpc2` | Enabled | Exit 139 before probe output |
| Same dynamic probe, verbose diagnostic control | `disablelrcpc2` | Enabled | Exit 139, repeated fault at same guest PC |
| Same dynamic probe | `disablelrcpc2` | Disabled, gadget engine | Exit 0, stack checks pass |

The failed local dynamic run reports signal 11, code 0, with address equal to PC `0x800135084098`. The verbose control reports 257 repeated guest page faults at that PC before the existing stuck-PC guard forces exit 139. The gadget-only control completes in 0.514 seconds and confirms a 128 MiB inferred main stack. Timings here establish a bounded quick reproduction, not a speed comparison.

The result isolates a failure associated with the native LinPad JIT path when FEX selects the non-LRCPC2 instruction sequence. It does not establish the exact faulting opcode or prove an alignment cause. Existing LDAPR/STLR host-alignment behavior remains a source-based hypothesis. A read-only local LLDB attempt failed to launch the debuggee (`no such process`), so no debugger register evidence was obtained. No retry or debugger escalation was made.

`launch.c` explicitly sets or clears only the feature override, clears LD_PRELOAD/FEX_MULTIBLOCK, and selects the existing FEX binary and runtime. `run.py` records the first four bounded controls; `diagnostic-results.json` records the two additional exact commands and host environment values. Full raw logs remain in the scratch parent directory; curated excerpts and their original SHA-256 values are provided for the repository. The dynamic probe source and binary are nonproprietary. No Unity, Mono, mscorlib, Steam-account or preference files are included.

Investigation stopped on the user's request to save and push the current work. The feature-only control is rejected for deployment. Native LRCPC2 support was being prepared separately by the FEX agent and is outside this reproduction's ownership and acceptance claim.
