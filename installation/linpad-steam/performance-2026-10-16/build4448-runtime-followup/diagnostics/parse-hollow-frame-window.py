#!/usr/bin/env python3
"""Analyze one bounded ISHWL_FRAMELOG slice; never labels it GPU/display FPS."""
import argparse
import json
import math
from pathlib import Path


def analyze(frame_text: str, meta_text: str, view_id: int) -> dict:
    metadata = dict(item.split("=", 1) for item in meta_text.split())
    elapsed = int(metadata["end_second"]) - int(metadata["start_second"])
    if not 0 < elapsed <= 300:
        raise ValueError("capture must span 1–300 wall-clock seconds")
    rows = []
    other_rows = 0
    for number, line in enumerate(frame_text.splitlines(), 1):
        if not line.strip():
            continue
        fields = line.split()
        if len(fields) != 5:
            raise ValueError(f"frame line {number}: expected five integer fields")
        ms, view, seq, width, height = (int(field) for field in fields)
        if not (0 <= ms <= 0xffffffff and 0 <= seq <= 0xffffffff
                and 0 <= view <= 0xffffffff and width > 0 and height > 0):
            raise ValueError(f"frame line {number}: values outside frame-log contract")
        if view == view_id:
            rows.append((ms, seq, width, height))
        else:
            other_rows += 1

    intervals = []
    sequence_discontinuities = []
    warnings = []
    for previous, current in zip(rows, rows[1:]):
        delta = (current[0] - previous[0]) & 0xffffffff
        if delta > (elapsed + 2) * 1000:
            warnings.append("timestamp order/reset exceeds bounded capture duration")
        intervals.append(delta)
        increment = (current[1] - previous[1]) & 0xffffffff
        if increment != 1:
            sequence_discontinuities.append({"previous": previous[1], "current": current[1], "increment": increment})
    if sequence_discontinuities:
        warnings.append("frame sequence discontinuity: check view reuse, duplicate rows or dropped log data")
    if not rows:
        warnings.append("no frames observed for selected view during the full window")
    elif len(rows) < 2:
        warnings.append("only one frame; active interval cadence unavailable")
    if intervals and max(intervals) >= 1500:
        warnings.append("gap of at least 1.5 seconds: correlate compositor ack-timeout messages")

    ordered = sorted(intervals)
    def percentile(p):
        return ordered[max(0, math.ceil(len(ordered) * p / 100) - 1)] if ordered else None

    span_ms = sum(intervals)
    return {
        "metric": "composited_game_view_delivery_cadence",
        "view_id": view_id,
        "captured_wall_seconds": elapsed,
        "wall_clock_resolution_seconds": 1,
        "frames_observed": len(rows),
        "other_view_rows_excluded": other_rows,
        "full_window_rate_approx_fps": len(rows) / elapsed,
        "active_span_seconds": span_ms / 1000 if intervals else None,
        "active_span_rate_fps": len(intervals) * 1000 / span_ms if span_ms else None,
        "interval_ms": {"p50": percentile(50), "p95": percentile(95), "p99": percentile(99),
                        "max": max(intervals) if intervals else None},
        "first_timestamp_ms": rows[0][0] if rows else None,
        "last_timestamp_ms": rows[-1][0] if rows else None,
        "first_sequence": rows[0][1] if rows else None,
        "last_sequence": rows[-1][1] if rows else None,
        "damage_dimensions_observed": sorted({(row[2], row[3]) for row in rows}),
        "sequence_discontinuities": sequence_discontinuities,
        "warnings": warnings,
        "interpretation": "Full-window rate includes observed idle time; active-span rate must not replace it. Damage dimensions are not swapchain extent. This is not GPU render duration or independently verified physical display FPS.",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("frames", type=Path)
    parser.add_argument("metadata", type=Path)
    parser.add_argument("--view", type=int, required=True)
    args = parser.parse_args()
    if not 0 <= args.view <= 0xffffffff:
        parser.error("--view must be an unsigned 32-bit ID observed in ishwl.log")
    try:
        result = analyze(args.frames.read_text(), args.metadata.read_text(), args.view)
    except (ValueError, KeyError, OSError) as exc:
        parser.error(str(exc))
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
