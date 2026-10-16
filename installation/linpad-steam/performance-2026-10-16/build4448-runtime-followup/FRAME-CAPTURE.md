# Prepared per-game frame capture

This diagnostic has not been run on the iPad. Build4448's physical SMC report
confirms that `/tmp/hollow4448-frames.txt` was created by the compositor.
Only the parent task operates the device. The script reads ordinary files;
it does not consume `events`/`notify` FIFOs or change/restart the game.

After downloading the prepared `hollow4448-frame-capture` into `/tmp`, inspect:

```
sh /tmp/hollow4448-frame-capture status
```

It lists the exact `map toplevel N 'Hollow Knight'` records and current
`vN-GENERATION.buf` files, plus recent Unity graphics milestones. The compositor
unlinks these buffer files when a view unmaps or resizes. A historical map
without a buffer is not accepted as a live capture candidate. Multiple active
candidates require an explicit observed view ID; the script refuses to guess.

Once a physical screenshot confirms actual game content in a stable scene,
start the fixed thirty-second sample (or supply an observed numeric ID):

```
sh /tmp/hollow4448-frame-capture capture auto 30
```

The script allows 1–60 seconds and refuses an existing output prefix. It saves:

- `/tmp/hollow4448-frame-window.meta`: view ID, wall-clock seconds and line boundaries.
- `/tmp/hollow4448-frame-window.txt`: only frame-log rows appended in that window.
- `/tmp/hollow4448-frame-window.status.txt`: title/buffer identity, milestone context and new acknowledgement warnings.

Transfer those regular files through the existing parent-owned collection
method. On the Mac, read `view_id` from the metadata and run:

```
python3 work/performance/parse-hollow-frame-window.py \
  work/performance/reports/hollow4448-frame-window.txt \
  work/performance/reports/hollow4448-frame-window.meta --view OBSERVED_VIEW_ID
```

For another sample set `LINPAD_FRAME_CAPTURE=/tmp/hollow4448-scene2` before
invoking the script; the script never overwrites an earlier result. Its other
optional path overrides are `LINPAD_FRAME_LOG`, `LINPAD_COMPOSITOR_LOG`,
`LINPAD_VIEW_RUNTIME`, and `LINPAD_UNITY_LOG`. Defaults match build4448.

Report the full-window selected-view row rate, including idle time. The
active-span rate is separate and must not replace it. The parser handles
32-bit timestamp/sequence wrap, excludes other views and flags discontinuities.
`VIEW_BUFFER_CHANGED`, map replacement, acknowledgement warnings or a resize
require qualification. Damage dimensions are not the swapchain extent.
This metric is composited game-view delivery cadence, not GPU render time
or independently measured physical display FPS. Physical scene/start/end
captures and an input-response check are still necessary for gameplay claims.

Seven synthetic fake-file checks cover view identification, bounded sampling,
overwrite prevention, duration limit and rejection of ambiguous/stale IDs.
The separate five parser checks cover idle spans, wrap, gaps and malformed
data. These are local arithmetic/diagnostic checks, not device performance.
