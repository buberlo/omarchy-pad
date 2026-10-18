#!/usr/bin/env python3
"""Gate 0.4 evidence: decide whether a captured frame is actually non-black.

    python3 png-stats.py shot.png [frame.raw]

The PNG comes from grim (zwlr-screencopy copyShm -> glReadPixels into client shm) and
the raw file is Hyprland's own dump of the headless output buffer (HYPR_PAD_SHM_DUMP),
so the two are independent views of the same frame.

Exits 0 only if the PNG is a readable image with real content.
"""
import sys
import zlib
import struct
import collections


def read_png(path):
    """Minimal PNG reader (no PIL in this environment): returns (w, h, rgb_rows)."""
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"{path}: not a PNG")

    pos, idat, meta = 8, b"", None
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos : pos + 4])
        ctype = data[pos + 4 : pos + 8]
        chunk = data[pos + 8 : pos + 8 + length]
        if ctype == b"IHDR":
            w, h, depth, color, comp, filt, inter = struct.unpack(">IIBBBBB", chunk)
            meta = (w, h, depth, color, inter)
        elif ctype == b"IDAT":
            idat += chunk
        elif ctype == b"IEND":
            break
        pos += 12 + length

    w, h, depth, color, inter = meta
    if depth != 8 or inter != 0 or color not in (2, 6):
        raise SystemExit(f"{path}: unsupported PNG format depth={depth} color={color} interlace={inter}")

    channels = 3 if color == 2 else 4
    raw = zlib.decompress(idat)
    stride = w * channels
    rows, prev, off = [], bytearray(stride), 0
    for _ in range(h):
        ftype = raw[off]
        line = bytearray(raw[off + 1 : off + 1 + stride])
        off += 1 + stride
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = prev[i]
            c = prev[i - channels] if i >= channels else 0
            if ftype == 1:
                line[i] = (line[i] + a) & 0xFF
            elif ftype == 2:
                line[i] = (line[i] + b) & 0xFF
            elif ftype == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 0xFF
            elif ftype == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        rows.append(bytes(line))
        prev = line
    return w, h, channels, rows


def describe(name, pixels, w, h, channels):
    """pixels: list of (r,g,b) tuples."""
    total = len(pixels)
    nonblack = sum(1 for r, g, b in pixels if r + g + b > 12)
    colors = collections.Counter(pixels)
    luma = 0.2126 * sum(p[0] for p in pixels) / total + 0.7152 * sum(p[1] for p in pixels) / total + 0.0722 * sum(p[2] for p in pixels) / total
    mx = max(max(p) for p in pixels)
    print(f"{name}: {w}x{h} channels={channels}")
    print(f"  distinct colours      : {len(colors)}")
    print(f"  non-black pixels      : {nonblack}/{total} ({100.0 * nonblack / total:.2f}%)")
    print(f"  mean luma             : {luma:.2f} / 255")
    print(f"  max channel value     : {mx}")
    print(f"  top 5 colours         : {[c for c, _ in colors.most_common(5)]}")
    print(f"  top 5 counts          : {[n for _, n in colors.most_common(5)]}")
    return nonblack, luma, len(colors)


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)

    w, h, channels, rows = read_png(sys.argv[1])
    pixels = []
    for y in range(0, h, max(1, h // 200)):          # sample at most ~200 rows
        row = rows[y]
        for x in range(0, w, max(1, w // 200)):
            i = x * channels
            pixels.append((row[i], row[i + 1], row[i + 2]))

    nonblack, luma, distinct = describe("PNG " + sys.argv[1], pixels, w, h, channels)
    ok = nonblack > len(pixels) * 0.5 and luma > 1.0

    if len(sys.argv) > 2:
        try:
            raw = open(sys.argv[2], "rb").read()
            stride = w * 4
            if len(raw) >= stride * h:
                rawpx = []
                for y in range(0, h, max(1, h // 200)):
                    for x in range(0, w, max(1, w // 200)):
                        i = y * stride + x * 4
                        rawpx.append((raw[i + 2], raw[i + 1], raw[i]))   # XRGB8888
                n2, l2, d2 = describe("RAW " + sys.argv[2], rawpx, w, h, 4)
                ok = ok and n2 > len(rawpx) * 0.5 and l2 > 1.0
            else:
                print(f"RAW {sys.argv[2]}: too small ({len(raw)} < {stride * h}), buffer not written")
                ok = False
        except FileNotFoundError:
            print(f"RAW {sys.argv[2]}: missing")
            ok = False

    print("VERDICT:", "NON-BLACK FRAME" if ok else "BLANK/BLACK FRAME")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
