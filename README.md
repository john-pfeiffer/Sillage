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
dev artifacts named `sillage-dev-{macos,windows}` for every push to `develop`.

Note: distribution builds assume a JUCE license appropriate for closed-source release
(`JUCE_DISPLAY_SPLASH_SCREEN=0`).

## Parameters

| Section | Parameter | Range | Notes |
|---|---|---|---|
| Output | Mix | 0–100 % | Dry/wet, equal-power |
| Output | Output | ±24 dB | Output trim |
| Output | Fallback BPM | 20–300 | Used for synced parameters when the host provides no tempo |

(The parameter set grows with each build phase; this table tracks the current state.)

## Development

- Branches: `main` (releases) + `develop` (integration); feature work lands on
  short-lived branches merged into `develop`.
- Work tracking: Notion database **"Sillage — Dev Tracker"** (Backlog → To do →
  In progress → Fixed in develop → Shipped in release). Dev builds are referenced
  as `dev-<shortsha>`.
- Tests: `ctest --test-dir build` runs a headless render harness
  (`tests/SillageTests.cpp`) that pushes audio through the real processor.
