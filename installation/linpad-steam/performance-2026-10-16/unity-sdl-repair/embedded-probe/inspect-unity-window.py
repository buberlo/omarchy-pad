#!/usr/bin/env python3
"""Read-only ELF section/string/xref inventory for an owned UnityPlayer.so.

RIP-relative LEA matches are candidates, not a substitute for a fault backtrace.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

ap = argparse.ArgumentParser()
ap.add_argument("elf", type=Path)
ap.add_argument("--output", type=Path, required=True)
args = ap.parse_args()
data = args.elf.read_bytes()
assert data[:6] == b"\x7fELF\x02\x01", "Expected little-endian ELF64"
eh = struct.unpack_from("<16sHHIQQQIHHHHHH", data)
assert eh[2] == 62, "Expected x86_64"
shoff, shentsize, shnum, shstrndx = eh[6], eh[11], eh[12], eh[13]
sh = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * shentsize) for i in range(shnum)]
names = data[sh[shstrndx][4]:sh[shstrndx][4] + sh[shstrndx][5]]
sections = []
for header in sh:
    name = names[header[0]:].split(b"\0", 1)[0].decode("ascii", "replace")
    sections.append(dict(name=name, type=header[1], flags=header[2], address=header[3], offset=header[4], size=header[5]))

def addr(offset):
    for s in sections:
        if s["type"] != 8 and s["offset"] <= offset < s["offset"] + s["size"]:
            return s["address"] + offset - s["offset"]
    return None

needles = [b"SDL_VIDEO_X11_FORCE_EGL", b"The selected window backend", b"The video driver did not add any displays", b"Error creating MainPlayerWindow", b"SDL_VIDEODRIVER"]
strings = []
for needle in needles:
    start = 0
    while (pos := data.find(needle, start)) >= 0:
        end = data.find(b"\0", pos, pos + 512)
        if end < 0: end = pos + 512
        strings.append(dict(file_offset=pos, address=addr(pos), text=data[pos:end].decode("utf8", "replace")))
        start = pos + len(needle)
targets = {s["address"] for s in strings if s["address"] is not None}
xrefs = []
for sec in sections:
    if not sec["flags"] & 4: continue
    start, end = sec["offset"], min(len(data), sec["offset"] + sec["size"])
    pos = start
    while (op := data.find(b"\x8d", pos, end - 5)) >= 0:
        pos = op + 1
        modrm = data[op + 1]
        if modrm & 0xc7 != 0x05: continue
        displacement = struct.unpack_from("<i", data, op + 2)[0]
        target = sec["address"] + op + 6 - sec["offset"] + displacement
        if target not in targets: continue
        begin = op - 1 if op > start and 0x40 <= data[op - 1] <= 0x4f else op
        xrefs.append(dict(address=sec["address"] + begin - sec["offset"], file_offset=begin, target=target, bytes=data[begin:op + 6].hex(), section=sec["name"]))
result = dict(file=str(args.elf.resolve()), bytes=len(data), sha256=hashlib.sha256(data).hexdigest(), sections=sections, strings=strings, candidate_rip_lea_xrefs=xrefs)
args.output.mkdir(parents=True, exist_ok=True)
(args.output / "inventory.json").write_text(json.dumps(result, indent=2) + "\n")
objdump = subprocess.check_output(["xcrun", "--find", "llvm-objdump"], text=True).strip()
for i, xref in enumerate(xrefs):
    dis = subprocess.check_output([objdump, "-d", "--x86-asm-syntax=intel", f"--start-address={max(0,xref['address'] - 128)}", f"--stop-address={xref['address'] + 256}", str(args.elf)], text=True)
    (args.output / f"xref-{i:02d}-{xref['address']:x}.txt").write_text(dis)
print(json.dumps({"bytes":len(data), "sha256":result["sha256"], "strings":strings, "xrefs":xrefs}, indent=2))
