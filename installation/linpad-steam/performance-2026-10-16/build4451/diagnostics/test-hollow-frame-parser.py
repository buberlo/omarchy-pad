#!/usr/bin/env python3
"""Replay synthetic parser checks; this is not device performance evidence."""
import importlib.util
import json
from pathlib import Path

parser_path = Path(__file__).with_name("parse-hollow-frame-window.py")
spec = importlib.util.spec_from_file_location("frames", parser_path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
meta = "start_second=100 end_second=130 start_line=10 end_line=15"
checks = []

result = module.analyze("1000 7 1 640 360\n1016 7 2 640 360\n1020 9 20 50 50\n", meta, 7)
assert result["frames_observed"] == 2 and result["other_view_rows_excluded"] == 1
assert result["full_window_rate_approx_fps"] < 0.1 and result["active_span_rate_fps"] == 62.5
checks.append("two early frames then 30-second-window stall cannot become 62.5 full-window FPS")

result = module.analyze("4294967290 7 4294967295 640 360\n10 7 0 640 360\n", meta, 7)
assert result["interval_ms"]["p50"] == 16 and not result["sequence_discontinuities"]
checks.append("timestamp and sequence 32-bit wrap preserve a 16ms interval")

result = module.analyze("1000 7 1 640 360\n4000 7 4 640 360\n", meta, 7)
assert len(result["sequence_discontinuities"]) == 1 and len(result["warnings"]) == 2
checks.append("sequence loss and long stall explicitly flagged")

result = module.analyze("", meta, 7)
assert result["full_window_rate_approx_fps"] == 0 and result["active_span_rate_fps"] is None
checks.append("no frames is zero full-window rate, not missing success")

try:
    module.analyze("bad row", meta, 7)
except ValueError:
    checks.append("malformed row rejected")
else:
    raise AssertionError("bad row accepted")

print(json.dumps({"scope": "synthetic parser verification, no device performance claim",
                  "checks": checks, "passed": len(checks)}, indent=2))
