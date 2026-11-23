# Debug HUD (F3) — Design

## Why

To check performance while playing: the frame rate, the moments it drops and what caused them,
and which GPU the game actually runs on (the laptop has a Radeon 610M and an RTX 5070).

## What

**F3** toggles a small panel in a corner, everywhere (menus, gameplay, cutscenes, over the
overlay). It takes no input and never pauses the game. Its on/off state is saved as
`debug_hud = true|false` in `config.toml`.

```
┌ 238 fps  4.2 ms  (1% low 171) ──────────────┐
│ ▁▁▁▂▁▁▁▁▁█▁▁▁▁▁▁▂▁▁   frame times, 4 s       │
│ game 1.9 ms  present 1.6 ms  pacing 0.7 ms   │
│ CPU 31% (game thread 24%)                    │
│ GPU NVIDIA RTX 5070 · Vulkan · busy 18%      │
│ scene GameCoreSection · retrace 51234        │
│ last drop: 38 ms at retrace 51180 — DVD read │
│   dataDir/stages/... (2.1 MB)                │
└──────────────────────────────────────────────┘
```

- **Frame rate:** fps and frame time averaged over 0.5 s; 1% low (average of the slowest 1% of
  frames over the last 4 s).
- **Frame-time graph:** the last 4 s, frames over 2× the target highlighted.
- **Breakdown per frame:** game (guest code), present (aurora), pacing (waiting for the next frame).
- **CPU:** process and game-thread usage from `/proc/self/stat` and `/proc/self/task/<tid>/stat`.
- **GPU:** Dawn's adapter (device name, backend, vendor), and busy % from NVML (`libnvidia-ml.so.1`,
  loaded with `dlopen`, matched by name) for NVIDIA or `/sys/class/drm/card*/device/gpu_busy_percent`
  (matched by PCI vendor/device ID) for AMD; "n/a" otherwise.
- **Drops:** a frame longer than 2× the target is a drop. Its cause is the most notable event in
  the recent-events ring since the previous frame (DVD read with path and size > scene change >
  thread switch > none). The last drop is shown; every drop is also logged
  (`[perf] drop 38.1 ms at retrace 51180: dvd read ...`) so runs can be checked afterwards.

## Constraints

- Measuring must not change game behaviour: no clock reads that advance `--fast` virtual time
  (host `steady_clock` only), nothing in `--headless`.
- Cost when hidden: frame records and drop detection only (a few hundred ns per frame).
- Depends on the overlay from the exit/settings plan (ImGui frame, input layer, config writer).

## Approaches considered

- **A. ImGui corner panel (chosen).**
- **B. Window title text:** no graph or causes, invisible in fullscreen.

## Left to the builder

- Panel corner and style (top-left, translucent, small font; respects HUD scale? no — fixed).
- Exact smoothing windows.
