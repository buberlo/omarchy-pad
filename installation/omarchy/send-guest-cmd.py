#!/usr/bin/env python3
"""Send one command to the running Omarchy Pad guest over ishwl's events FIFO.

    python3 send-guest-cmd.py '<shell command>' [--events PATH]

The FIFO carries `spawn <escaped command>` lines: the app escapes every byte <= 0x20 and %,
and ishwl's supervisor runs the result through `sh -c`. Writing to the FIFO from the Mac is
the same path the app uses (the FIFO is a host FIFO; a guest read returns host writes), which
makes this the only way to run a diagnostic inside the app's guest while the canvas owns the
screen -- and a useful check of the launch channel itself.
"""
import argparse
import os
import sys


def escape(text: str) -> str:
    out = []
    for b in text.encode():
        out.append("%%%02X" % b if b <= 0x20 or b == 0x25 else chr(b))
    return "".join(out)


def default_events() -> str:
    import subprocess

    udid = os.environ.get("OMARCHY_SIM_UDID", "FE9E27CC-3505-488F-A17F-89E4BC90E5D6")
    data = subprocess.run(
        ["xcrun", "simctl", "get_app_container", udid, "com.valentinneagu.ish.arm64", "data"],
        capture_output=True, text=True, check=True).stdout.strip()
    return os.path.join(data, "Library/Application Support/roots/omarchy/data/tmp/ishwl/events")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("command")
    ap.add_argument("--events")
    args = ap.parse_args()
    events = args.events or default_events()
    if not os.path.exists(events):
        print(f"no events FIFO at {events} (is the app running?)", file=sys.stderr)
        return 1
    line = "spawn " + escape(args.command) + "\n"
    with open(events, "w") as fifo:
        fifo.write(line)
    print(f"sent {len(line)} bytes to {events}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
