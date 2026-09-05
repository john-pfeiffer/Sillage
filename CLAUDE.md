# Sillage — repo conventions

- **Spec:** the product spec is the "Sillage — Handoff Document". Build order: grain
  engine → feedback path → freeze/chaos/randomize → transients → age → reverse/rewind →
  wake → mod slots → UI polish. Ship incrementally by phase; each phase must be
  testable in a DAW before the next starts.
- **Branches:** `main` only for now. Feature work goes on `jp/`-prefixed branches and
  reaches `main` by pull request — never commit directly to `main`. A `develop`
  integration branch gets added once the first release is out.
- **Work queue:** Notion database "Sillage — Dev Tracker" (via Notion MCP). Status flow:
  Backlog → To do → In progress → Fixed in develop → Shipped in release. Reference
  builds as `dev-<shortsha>` in the Build field.
- **Build:** `scripts/dev-build.sh [--debug] [--test] [--install]`. JUCE is pinned via
  FetchContent in `CMakeLists.txt`.
- **Tests:** `tests/SillageTests.cpp` is a headless render harness run by CTest. Every
  DSP change needs a rendered-audio assertion, and the suite must pass in Release
  before pushing.
- **EVS standards:** every user-facing parameter host-automatable; resizable UI (the
  panel is laid out once at 1280 px and scaled as a whole, fixed aspect); pure-function
  UI (knobs look like knobs); Randomize never touches Mix, Output, Freeze, Wake mode or
  the stage On switches (`randomize::kExcluded`); report latency honestly via `setLatencySamples` (0 unless a
  stage really adds lookahead).
- **DSP invariants:** the feedback-loop limiter is what makes Feedback > 100 % and
  Chaos safe — never remove it or place color stages after it; the grain cap
  (`kMaxGrains = 64`) is compile-time; per-grain work must stay cheap (8+ instances
  per session is the CPU budget). Decay is one feature with two halves — the age gate in
  `resolveShape` and the Spread reach limit in `spawnScheduledGrain` — never ship one
  without the other, and Off (`params::decayIsOff`) must leave the engine exactly as it
  was without Decay.
- **Parameter IDs** live only in `src/Parameters.h` (`params::id::…`); the editor
  builds its tabs from ID tables in `src/PluginEditor.cpp` (`SectionPage`); keep the README
  parameter table current when the set changes.
- **UI is two-tier:** the Main page (`src/MainPage.cpp`, eight knobs) is what a user sees;
  everything else goes on a tab, never on the main page. A parameter is controlled from one
  place only. Every stage has an On switch that leads its tab (`SectionSpec::gate`), and a
  control that only means something while another is on names it as its `master`, so the
  page greys it out. Quick-start Types (`src/Types.cpp`) are tuned full starting points, applied as
  an action — they move everything except the Randomize exclusions.
- **Wake / modulation:** `WakeEngine` (`src/Wake.h`) owns the shared `GrainEngine` and the
  isolated instances; mod destinations resolve through `SillageAudioProcessor::modulated()`
  (normalised offsets, `src/Modulation.h`) — read a modulatable parameter through that, not
  `parameterValue()`, or the slot silently stops working for it.
- **Releases:** push a `vX.Y.Z` tag; `.github/workflows/release.yml` builds the installers
  (`installers/`) and attaches them to a GitHub Release.
