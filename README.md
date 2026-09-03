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

The signal chain inside the loop is fixed: `grain sum → HP → LP → Shimmer → Diffuse →
Saturation → Limiter → buffer`. The limiter is what makes Feedback above 100 % safe, so
no colour stage may be placed after it.

Build phases 1 and 2 of the handoff's build order are in. Still to come: Freeze, Chaos,
Randomize and Panic; the transient system; Age and Degrade; Reverse/Rewind; Wake modes;
mod slots; and the Custom user scale for Quantize.

## Development

- Branches: `main` only for now; feature work lands on short-lived `jp/`-prefixed
  branches merged into `main` by pull request. A `develop` integration branch gets
  added once the first release is out.
- Work tracking: Notion database **"Sillage — Dev Tracker"** (Backlog → To do →
  In progress → Fixed in develop → Shipped in release). Dev builds are referenced
  as `dev-<shortsha>`.
- Tests: `ctest --test-dir build` runs a headless render harness
  (`tests/SillageTests.cpp`) that pushes audio through the real processor.
