#!/usr/bin/env python3
"""Model glibc 2.41 pthread_getattr_np's initial-thread stack calculation.

This diagnoses recorded maps data; it is not a test of the Linux emulator.
Source: nptl/pthread_getattr_np.c lines 94-135, glibc-2.41 tag.
"""
import json
MASK = (1 << 64) - 1
PAGE = 4096
SOFT_LIMIT = 0x8000000
STACK_END = 0x808008000000
MAP_BEGIN = 0x808007800000


def infer(map_end, previous_end):
    unaligned = (SOFT_LIMIT - (map_end - STACK_END)) & MASK
    aligned = unaligned & -PAGE
    size = min(aligned, STACK_END - previous_end)
    base = STACK_END - size
    return {"base": hex(base), "size": hex(size),
            "unclamped_size": hex(aligned),
            "guard_base": hex(base + PAGE), "guard_end": hex(base + PAGE + 32768)}


bad = infer(1 << 48, 0x800157d5e000)
good = infer(STACK_END, 0x800157d5e000)
assert int(bad["base"], 16) == 0x800157d5e000
assert int(bad["guard_base"], 16) <= 0x800157d640b0 < int(bad["guard_end"], 16)
assert int(good["base"], 16) == STACK_END - SOFT_LIMIT
assert int(good["size"], 16) == SOFT_LIMIT
assert not (int(good["guard_base"], 16) <= 0x800157d640b0 < int(good["guard_end"], 16))
for previous in [0x800137d45000, 0x800157d8b000]:
    assert int(infer(1 << 48, previous)["base"], 16) == previous
    assert infer(STACK_END, previous) == good
print(json.dumps({"recorded_bad_map_end": hex(1 << 48),
                  "actual_stack_map_end": hex(STACK_END),
                  "soft_stack_limit": hex(SOFT_LIMIT),
                  "bad": bad, "correct": good,
                  "result": "PASS: bad maps reproduce the protected FEX-node range; corrected end yields stable 128MiB stack extent"}, indent=2))
