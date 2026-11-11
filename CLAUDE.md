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
  before pushing. The grain scheduler is random, so assertions on a cloud average a few
  renders and leave margin.
- **Measuring:** `SillageProbe` (`tools/Probe.cpp`, `-DSILLAGE_BUILD_PROBE=ON`) prints each
  Type's wet level and tail, what each main knob does across its range, tail length over
  Decay × Spread × Feedback and Randomize's yield. Re-run it after any change to the
  Decay model, the loudness compensation, a Type or a Randomize range. `SillageSnapshot`
  (`tools/Snapshot.cpp`) renders the editor to PNG; `--play <Type>` runs audio through it.
- **EVS standards:** every user-facing parameter host-automatable; resizable UI (the
  panel is laid out once at its 960 px design width and scaled as a whole, fixed aspect);
  pure-function UI (knobs look like knobs: an arc and a pointer, a switch is a dot that
  lights, and nothing is drawn that does not show a value); Randomize never touches Mix,
  Output, Freeze, Wake mode or the stage On switches (`randomize::kExcluded`), and never
  aims a mod slot at Mix; report latency honestly via `setLatencySamples` (0 unless a
  stage really adds lookahead).
- **DSP invariants:** the feedback-loop limiter is what makes Feedback > 100 % and
  Chaos safe — never remove it or place color stages after it; the grain cap
  (`kMaxGrains = 64`) is compile-time; per-grain work must stay cheap (8+ instances
  per session is the CPU budget). Decay is one model in three places — `decay::resolve`
  (`src/DecayModel.h`: the one-trip window, the loop gain, the wet gain), the windowed cloud
  in `spawnScheduledGrain`, and the two gains applied in `GrainEngine::process` — never
  change one without the others, and `testDecayHoldsTheTailLength` is what says they still
  agree. The cloud starts one grain length behind Time: a grain that reads closer than it is
  long re-reads itself and the loop runs away. Off (`params::decayIsOff`) must take none of
  those paths.
- **Parameter IDs** live only in `src/Parameters.h` (`params::id::…`); the editor
  builds its tabs from ID tables in `src/PluginEditor.cpp` (`SectionSpec`); keep the README
  parameter table current when the set changes.
- **UI is two-tier:** the Main page (`src/MainPage.cpp`, twelve knobs on four cards) is
  what a user sees; everything else goes on a tab, never on the main page. A parameter is
  controlled from one place only. Tabs are cards built from ID tables in
  `src/PluginEditor.cpp` (`SectionSpec`): a card's `power` is its own switch, drawn as the
  dot at its top right (the stage's On switch on the card that leads a tab), `gate` is the
  stage it answers to, and a control lists in `needs` (`src/Conditions.h`) everything that
  has to hold for it to mean anything, so the page greys out whatever the current state has
  taken over. The look lives in `src/SillageLookAndFeel.*` (near-black, cards, one red) and
  the pictures on the cards in `src/Visuals.*`; each picture shows what the knobs beside it
  do. Quick-start Types (`src/Types.cpp`, twelve) are tuned full starting points, applied
  as an action — they move everything except the Randomize exclusions, and each has a
  level test and, for the six character Types, a test that its signature is heard.
- **Randomize:** not uniform. `randomize::Spec` (`src/Randomize.cpp`) gives each parameter
  the range a roll may land in and how often a switch or an effect is on; `constrain()`
  repairs the combinations no single range rules out. A new parameter that can silence or
  hold the tail needs a Spec, and `testRandomizeStaysAudible` is the check.
- **Wake / modulation:** `WakeEngine` (`src/Wake.h`) owns the shared `GrainEngine` and the
  isolated instances; mod destinations resolve through `SillageAudioProcessor::modulated()`
  (normalised offsets, `src/Modulation.h`) — read a modulatable parameter through that, not
  `parameterValue()`, or the slot silently stops working for it.
- **Releases:** push a `vX.Y.Z` tag; `.github/workflows/release.yml` builds the installers
  (`installers/`) and attaches them to a GitHub Release.
