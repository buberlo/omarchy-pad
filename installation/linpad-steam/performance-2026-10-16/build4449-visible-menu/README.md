# Build4449 visible menus, input and qualified cadence observations

The unchanged screenshot establishes a visible Hollow Knight language list
and confirmation dialog on the mirrored physical iPad. Parent observed it at
approximately 12:54 UTC after the 12:47:41 launch. This is the first captured
observation, not a measured first-frame latency. The game was not restarted
or instrumented with the unfinished Vulkan proxy.

Parent accepted Deutsch in the game. Unity logs restoration of DE at
12:56:44.040 and again 13:01:35.577; this confirms language-selection input
response. It does not establish movement/combat or normal gameplay. Subsequent
observation was a black loading transition. The log's automatic-level-start
message is retained without claiming a visible playable level.

The passive capture waited five seconds for foregrounding, then observed
13:04:02–13:04:32 UTC. Exact titled view 1 retained the same v1-2.buf identity.
It delivered 48 compositor frame rows over the full 30 seconds: **1.6 composited
frames/second**. All 48 sequences are contiguous, 4436–4483, with no other view
rows. The 47 inter-frame intervals span 15.663 seconds (active-only 3.00/s); that
shorter-span rate must not replace the full-window result. Median interval is
297 ms, 95th percentile 389 ms, maximum 1766 ms. The parser flags the long interval;
the captured compositor-event slice contains no acknowledgement warning.

The run initially requested 960×540. Unity later logged a swapchain reset to
659×540 at 12:51:16.853; the observed capture damage is also 659×540 and the
unchanged backing file is 1,423,440 bytes (659×540×4). Damage dimensions alone
would not prove swapchain size; here the separate Unity message provides that
evidence. This resized black transition is not comparable to a 960×540 stable
gameplay scene.

The measured boundary is the compositor's game-view output, not GPU render
duration or independently measured display refresh. The snapshot confirms
visible menu content and a working language input path, while the transition
remains very slow. Normal gameplay, audio playback and steady-state playable
performance remain unqualified here.

A second unchanged screenshot, observed around 13:05 UTC, shows the German
main title menu with Spiel starten, Optionen, Erfolge, Extras and Spiel verlassen.
The second 30-second capture runs from 13:06:43 to 13:07:13 UTC and contains
97 rows for the same view: 3.23 compositor frames/second over the full window.
Median interval is 304 ms, 95th percentile 327 ms and maximum 436 ms; sequences
4908–5004 are contiguous. The active-only rate is 3.25/s over 29.517 seconds.
**Foreground state was uncertain/partial:** parent saw the terminal foreground
initially and confirmed the game foreground around 13:07. This is a title-menu
context observation, not a controlled foreground baseline, improvement over
the loading transition or gameplay FPS. The different scenes and foreground
states prevent a matched performance comparison between these captures.

The report, exact meta/rows/status and parser are included. The wrapper makes
no game/configuration/input changes and reads only regular files. It uses a
unique capture directory and one bounded POST to the parent's existing local
collector. Five wrapper tests use synthetic files and stub waits/network;
five parser tests exercise arithmetic. They are separate from the physical
capture. Reproduce analysis with:

```
python3 diagnostics/parse-hollow-frame-window.py loading-window.txt loading-window.meta --view 1
```

Raw process inventories, full game logs and proprietary game binaries remain
outside Git. The original screenshot bytes are preserved, including the
mirroring application's surrounding UI, in this private evidence archive.
Frozen build4449 signing/probe proof and the earlier pre-frame snapshot remain
unchanged. Subsequent source f93f1449 belongs to a future build and is not
attributed to this physical result.
