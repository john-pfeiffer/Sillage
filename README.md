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

## Using it

The plugin opens on the **Main** page: eight knobs that read like a reverb or delay.
**Time** (with Sync and a division under it), **Spread** (the delay-to-reverb slider: 0
reads exactly at Time, 100 scatters grains across the whole buffer), **Feedback**, **Size**,
**Damping** (the loop low-pass), **Shimmer**, **Width** and **Mix**. The top bar has the
presets, a **Type** selector, **Freeze**, the **Wake** mode and the Randomize / Panic /
Rewind buttons.

**Type** is a quick start, like a reverb's algorithm selector: Delay, Reverb, Shimmer, Wash
or Granular sets every parameter to a tuned starting point for that character (leaving Mix,
Output, Freeze and Wake mode alone) and names the preset after itself. The main knobs are
then yours to move.

Everything else lives on the tabs — **Grain**, **Feedback**, **Transients**, **Age**,
**Rewind & Chaos**, **Mod**, **Output** — and none of it needs touching to use the plugin.
Every parameter on every tab is still host-automatable.

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
| Freeze & Chaos | Randomize Amt | 0–100 % | 100 = full reroll, 20 = a nudge. Randomize never touches Mix, Output, Freeze, Wake mode or Panic |
| Transients | Sensitivity | 0–100 % | Spectral-flux onset detector on the input; the Hit indicator shows detections |
| Transients | Retrigger, Burst Count, Burst Rate, Burst Div, Burst Amt, Burst Offset | 1–16, 10–1000 ms / division, 0–100 %, 0–100 ms | A hit fires a burst of grains that all read the hit itself — a stutter, not a smear. Rate follows Sync |
| Transients | Duck, Duck Depth, Duck Attack, Duck Release | 0–100 %, 1–50 ms, 20–2000 ms | A hit pushes the wet signal down and lets it bloom back |
| Transients | Choke, Choke Amt, Choke Fade | 0–100 %, 1–200 ms | A hit kills the existing tail, both the grains in flight and the buffer they read from |
| Transients | Env > Density, Env > Spread | ±100 % | The input envelope (relative to recent loudness) drives Density / Spread. Positive: hits dense, sustains sparse |
| Sync | Sync | on / off | Grain spacing and burst rate follow the host tempo |
| Sync | Grain Div | 1/64T – 1 bar | Grain spacing when Sync is on (takes over from Density) |
| Sync | Swing | 0–100 % | Delays every other grain slot; 100 = a 2:1 triplet feel |
| Output | Panic | momentary | Clears every buffer and grain instantly (automatable) |
| Age | Lifetime | 100 ms – 30 s | Age is normalised against this for the Lifetime Curves |
| Age | Bits/pass, SR/pass, Noise/pass, LP tilt/pass | 0–2 bit, 0–10 %, 0–100 %, 0–500 Hz | Per-pass Degrade: driven by the audio's age, so it compounds every time round (floors: 4-bit, 4 kHz, 200 Hz) |
| Age | Drift/pass, Drift Dir | 0–50 ct, Up / Down / Random | Pitch drift in the loop; Random re-draws its direction every pass |
| Age | Curve: LP cutoff … Level | on / off ×9 | Enables a Lifetime Curve per destination (LP, HP, Bit depth, Sample rate, Grain size, Pitch offset, Reverse, Pan spread, Level). Curve shapes are drawn in the editor and saved with the session |
| Rewind | Rewind | on / off | Continuously captures the wet tail |
| Rewind | Rewind Length | 100 ms – 8 s | How much of the captured tail a trigger plays back |
| Rewind | Rewind Trig | Timer / Transient / Threshold / Manual | What fires a rewind. Timer uses Rewind Every, or Rewind Div under Sync; Threshold fires when the tail falls below Rewind Thresh |
| Rewind | Rewind Every, Rewind Div, Rewind Thresh | 0.1–30 s, 1/4 – 8 bars, −60–0 dB | |
| Rewind | Rewind Level, Rewind Pitch | 0–100 %, ±12 st | The captured tail plays back *reversed* into the feedback path, so it swells up through the colour stages |
| Rewind | Rewind Now | momentary | Manual trigger (automatable); works in every trigger mode |
| Wake | Wake | Shared / Isolated | Shared: new input writes into the same buffer the tail lives in. Isolated: every onset spawns its own tail instance (own buffer, scheduler, feedback path and Age clock), up to 8, oldest stolen with a fade; input keeps feeding the newest one between onsets. Switching crossfades the input routing, so the ringing tail is never dropped. Randomize leaves this alone |
| Wake | Displace | 0–100 % | How much a new hit pushes the existing tail down: Choke made continuous in Shared, and a duck of the older instances in Isolated. Uses Choke Fade |
| Modulation | Mod 1–6 Src / Dest / Amt / Curve | see below | Six slots: Source → Destination, bipolar Amount (±100 % = the whole knob, in the destination's own range), Curve (Linear / Exp / Log) |
| Modulation | Transient Decay | 10–2000 ms | Decay of the Transient source's one-shot |
| Modulation | LFO 1–2 Shape / Rate / Sync / Div / Phase | Sine / Tri / Saw / Square / S&H, 0.01–20 Hz, division, 0–360° | Two LFOs; synced ones lock to the host transport |
| Output | Width | 0–200 % | Mid/side width of the wet signal, after the wet filters |
| Output | Wet HP / Wet LP | 20–4000 Hz / 200–20000 Hz | Post-loop filters: clean the output without changing the feedback behaviour |

The signal chain inside the loop is fixed: `grain sum → HP → LP → Shimmer → Diffuse →
Saturation → Limiter → buffer`. The limiter is what makes Feedback above 100 % safe, so
no colour stage may be placed after it.

Transient detection reacts a few milliseconds after a hit (about half an FFT window
plus a hop). That is the responses' reaction time, not plugin latency: the dry path is
never delayed, and reported latency stays 0.

Every buffer sample carries its **Age** in seconds in a parallel buffer, so a grain knows
how old the audio it plays is, per-pass Degrade compounds with that age, and the Lifetime
Curves shape each grain from its own age (global destinations use the average age of the
live grains). Loudness compensation blends from coherent at Spread 0 to incoherent at
Spread 100, so Feedback means the same thing across the whole delay-to-reverb range.

**Modulation sources:** Age (grain) — a grain's own age over Lifetime, resolved per grain
for the per-grain destinations (Size, Pitch, Pitch Spread, Pan Spread, Reverse) and as the
average of the live grains everywhere else; Age (avg) — the average age of live grains over
Lifetime; Envelope — the input envelope relative to recent loudness; Transient — a one-shot
on a hit with Transient Decay; Chaos — a smoothed-random source of its own; LFO 1 / LFO 2.

**Modulation destinations:** Time, Density, Spread, Size, Feedback, Pitch, Pitch Spread,
Pan Spread, Reverse, Loop HP, Loop LP, Shimmer Amt, Shimmer Fine, Diffuse, Drive, the five
per-pass Degrade amounts, Rewind Level, Displace, Mix.

The wet stage after the loop is `Duck → Wet HP/LP → Width → Mix`.

**Presets:** the plugin state saves with the host session as usual; the Save / Load buttons
write and read `.sillage` files (the same XML), defaulting to
`Documents/Elan Vital Studios/Sillage/Presets`. Init returns every parameter and curve to
its default.

Memory per instance at 48 kHz is about 44 MB, all allocated in `prepareToPlay`: 13 MB for
the shared tail (10 s audio ring, its age ring, a 12 s Rewind capture and its age ring),
9 × 3.5 MB for the isolated Wake instances (6 s ring plus age ring each, one spare slot for
a stolen instance's fade), and a second Rewind capture that covers them together.

All nine phases of the handoff's build order are in. Still to come: the Custom user scale
for Quantize, and the deferred items in the handoff (sidechain detection, MIDI-triggered
Freeze/Rewind, a preset browser, oversampled saturation).

## Installers

`installers/macos/build-pkg.sh <version> <artefacts-dir>` builds a universal `.pkg`
(VST3, AU, optional Standalone) with `pkgbuild`/`productbuild`; set
`SILLAGE_CODESIGN_ID` and `SILLAGE_INSTALLER_ID` to sign. `installers/windows/build-installer.ps1`
compiles `installers/windows/Sillage.iss` with Inno Setup 6 into a `.exe` that installs the
VST3 to the shared 64-bit VST3 folder and (optionally) the Standalone app. The dev-build
workflow attaches both installers to its artifacts; pushing a `vX.Y.Z` tag runs
`.github/workflows/release.yml`, which builds them again and attaches them to a GitHub
Release.

## Development

- Branches: `main` only for now; feature work lands on short-lived `jp/`-prefixed
  branches merged into `main` by pull request. A `develop` integration branch gets
  added once the first release is out.
- Work tracking: Notion database **"Sillage — Dev Tracker"** (Backlog → To do →
  In progress → Fixed in develop → Shipped in release). Dev builds are referenced
  as `dev-<shortsha>`.
- Tests: `ctest --test-dir build` runs a headless render harness
  (`tests/SillageTests.cpp`) that pushes audio through the real processor.
- UI check without a DAW: configure with `-DSILLAGE_BUILD_SNAPSHOT_TOOL=ON` and run
  `SillageSnapshot <dir>` (under `xvfb-run` on a headless machine) to render every tab of
  the editor to PNG.
