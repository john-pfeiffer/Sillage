# Sillage — repo conventions

- **Spec:** the product spec is the "Sillage — Handoff Document". Build order: grain
  engine → feedback path → freeze/chaos/randomize → transients → age → reverse/rewind →
  wake → mod slots → UI polish. Ship incrementally on `develop`; each phase must be
  testable in a DAW before the next starts.
- **Branches:** `main` (releases) + `develop` (integration). Never commit directly to
  `main`.
- **Work queue:** Notion database "Sillage — Dev Tracker" (via Notion MCP). Status flow:
  Backlog → To do → In progress → Fixed in develop → Shipped in release. Reference
  builds as `dev-<shortsha>` in the Build field.
- **Build:** `scripts/dev-build.sh [--debug] [--test] [--install]`. JUCE is pinned via
  FetchContent in `CMakeLists.txt`.
- **Tests:** `tests/SillageTests.cpp` is a headless render harness run by CTest. Every
  DSP change needs a rendered-audio assertion, and the suite must pass in Release
  before pushing.
- **EVS standards:** every user-facing parameter host-automatable; resizable UI;
  pure-function UI (knobs look like knobs); Randomize never touches Mix, Output,
  Freeze, or Wake mode; report latency honestly via `setLatencySamples` (0 unless a
  stage really adds lookahead).
- **DSP invariants:** the feedback-loop limiter is what makes Feedback > 100 % and
  Chaos safe — never remove it or place color stages after it; the grain cap
  (`kMaxGrains = 64`) is compile-time; per-grain work must stay cheap (8+ instances
  per session is the CPU budget).
- **Parameter IDs** live only in `src/Parameters.h` (`params::id::…`); the editor
  builds its sections from ID tables in `src/PluginEditor.cpp`; keep the README
  parameter table current when the set changes.
