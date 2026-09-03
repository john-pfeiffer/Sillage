# Sillage

Granular reverb / delay hybrid effect — the first EVS (Élan Vital Studios) effect plugin,
following the instruments Keepsake, Folie, and Refonte.

*Sillage* (French): the wake a boat leaves, or the trail a perfume leaves in a room.
The thing that lingers behind.

## Concept

Sillage sits on the continuum between delay and reverb and lets you slide anywhere along
it. The engine is a grain scheduler reading from a feedback buffer: **Density** and
**Spread** move continuously from clean repeats to a dense cloud. On top of that core:
a metallic, pitched, saturated feedback path; transient awareness; **Age** (the tail
processes itself over its lifetime); and **Wake** (shared vs. isolated tails per onset).

## Building

Requires CMake ≥ 3.24 and a C++20 compiler. JUCE is fetched automatically (pinned in
`CMakeLists.txt`).

```sh
scripts/dev-build.sh            # Release build, prints artifact paths
scripts/dev-build.sh --test     # ... and run the test suite
scripts/dev-build.sh --install  # ... and copy the VST3/AU into your user plugin folder
```

Formats: VST3 + AU (AU on macOS only) + Standalone. macOS builds are universal
(Intel + Apple Silicon). CI (`.github/workflows/dev-build.yml`) produces installable
dev artifacts named `sillage-dev-{macos,windows}` for every push to `main` or a
`jp/` branch, and for every pull request into `main`.

Note: distribution builds assume a JUCE license appropriate for closed-source release
(`JUCE_DISPLAY_SPLASH_SCREEN=0`).

## Parameters

Every parameter is exposed to host automation (EVS standard).

| Section | Parameter | Range | Notes |
|---|---|---|---|
| Grain | Time | 1–4000 ms | Base read offset behind the write head |
| Grain | Time Sync | on / off | Take Time from the host tempo instead |
| Grain | Division | 1/64T – 1 bar | 19 divisions incl. dotted and triplet; the bar follows the host time signature |
| Grain | Density | 0.5–500 /s | Low = discrete echoes, high = continuous wash |
| Grain | Spread | 0–100 % | 0 = every grain lands at Time (a delay), 100 = reads scattered across the buffer (a reverb) |
| Grain | Size | 2–2000 ms | Grain length |
| Grain | Window | Hann / Trapezoid / Tukey / Expo-decay | Grain envelope |
| Grain | Feedback | 0–120 % | Above 100 % is intentional; the loop limiter keeps it musical |
| Pitch | Pitch | ±24 st | Per-grain playback rate |
| Pitch | Pitch Fine | ±100 ct | |
| Pitch | Pitch Spread | 0–100 % | Random per-grain pitch deviation |
| Pitch | Quantize | Off / Chromatic / Major / Minor / Pentatonic / Octaves+5ths | Snaps randomised grain pitch, and the shimmer, to a scale |
| Pitch | Root | C – B | Root note for Quantize |
| Pitch | Pan Spread | 0–100 % | Random per-grain stereo placement |
| Pitch | Reverse | 0–100 % | Probability a grain reads backwards |
| Feedback | Loop HP / Loop LP | 20–4000 Hz / 200–20000 Hz | Filters inside the loop, so they compound each pass |
| Feedback | Loop Res | 0–100 % | Shared resonance |
| Feedback | Shimmer | −12 … +24 st | Loop pitch shift; compounds every pass |
| Feedback | Shimmer Amt | 0–100 % | |
| Feedback | Shimmer Fine | ±50 ct | Small values give the drifting metallic chorus |
| Feedback | Diffuse | 0–100 % | Four series allpasses between plain repeats and a smear |
| Feedback | Saturation | Soft / Hard / Fold | Fold is the hyperpop one |
| Feedback | Drive | 0–100 % | |
| Output | Mix | 0–100 % | Dry/wet, equal-power |
| Output | Output | ±24 dB | Output trim |
| Output | Fallback BPM | 20–300 | Used for synced parameters when the host provides no tempo |
| Freeze & Chaos | Freeze | on / off | Stops the write head; the held buffer stays a playable source for Time / Spread / Pitch / Density |
| Freeze & Chaos | Freeze Fade | 0–2000 ms | Crossfades into and out of Freeze so it never clicks |
| Freeze & Chaos | Chaos | 0–100 % | Smoothed-random drift of read position, pitch, feedback (up to +30 %), density (±50 %) and shimmer detune; the loop limiter is what keeps 100 % musical |
| Freeze & Chaos | Randomize Amt | 0–100 % | 100 = full reroll, 20 = a nudge. Randomize never touches Mix, Output, Freeze or Panic |
| Transients | Sensitivity | 0–100 % | Spectral-flux onset detector on the input; the Hit indicator shows detections |
| Transients | Retrigger, Burst Count, Burst Rate, Burst Div, Burst Amt, Burst Offset | 1–16, 10–1000 ms / division, 0–100 %, 0–100 ms | A hit fires a burst of grains that all read the hit itself — a stutter, not a smear. Rate follows Sync |
| Transients | Duck, Duck Depth, Duck Attack, Duck Release | 0–100 %, 1–50 ms, 20–2000 ms | A hit pushes the wet signal down and lets it bloom back |
| Transients | Choke, Choke Amt, Choke Fade | 0–100 %, 1–200 ms | A hit kills the existing tail, both the grains in flight and the buffer they read from |
| Transients | Env > Density, Env > Spread | ±100 % | The input envelope (relative to recent loudness) drives Density / Spread. Positive: hits dense, sustains sparse |
| Sync | Sync | on / off | Grain spacing and burst rate follow the host tempo |
| Sync | Grain Div | 1/64T – 1 bar | Grain spacing when Sync is on (takes over from Density) |
| Sync | Swing | 0–100 % | Delays every other grain slot; 100 = a 2:1 triplet feel |
| Output | Panic | momentary | Clears every buffer and grain instantly (automatable) |

The signal chain inside the loop is fixed: `grain sum → HP → LP → Shimmer → Diffuse →
Saturation → Limiter → buffer`. The limiter is what makes Feedback above 100 % safe, so
no colour stage may be placed after it.

Transient detection reacts a few milliseconds after a hit (about half an FFT window
plus a hop). That is the responses' reaction time, not plugin latency: the dry path is
never delayed, and reported latency stays 0.

Build phases 1–4 of the handoff's build order are in. Still to come: Age and Degrade;
Reverse/Rewind mode; Wake modes and Displace; mod slots and LFOs; Width and the post-loop
Wet HP/LP; presets and installers; and the Custom user scale for Quantize.

## Development

- Branches: `main` only for now; feature work lands on short-lived `jp/`-prefixed
  branches merged into `main` by pull request. A `develop` integration branch gets
  added once the first release is out.
- Work tracking: Notion database **"Sillage — Dev Tracker"** (Backlog → To do →
  In progress → Fixed in develop → Shipped in release). Dev builds are referenced
  as `dev-<shortsha>`.
- Tests: `ctest --test-dir build` runs a headless render harness
  (`tests/SillageTests.cpp`) that pushes audio through the real processor.
