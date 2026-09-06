# AGENTS.md — CardEngine (C++ / CMake)

Open source card game engine (Texas Hold'em first). MIT licensed, copyright Todd Carnes (`LICENSE`).
Verified 2026-09-05: configure + build + `ctest` pass with CMake 4.3.4 / VS 2026 on Windows.

## Structure (real)

- Root `CMakeLists.txt` — `project(cardengine VERSION 0.2.0 LANGUAGES CXX)`; C++23 required, `CMAKE_CXX_EXTENSIONS OFF`.
- `include/cardengine/cardengine.h` + `src/cardengine.cpp` — `cardengine` static lib (engine lives here).
  Umbrella header; engine modules: `card.h` (`Rank` 2–14 / `Suit` / `Card`, `"Qh"` notation + `parse_card`),
  `deck.h` (`Deck`, deterministic `shuffle(seed)`), `hand.h` (`evaluate_five` / `evaluate_best`, `HandValue` with
  category + significance-ordered tiebreak). `evaluate_best` takes 5–7 cards; throws otherwise.
  Table engine: `config.h` (`GameConfig`: seats/stacks/blinds, `ante`, `hole_cards`/`board_cards` with deck-math
  validation, `BettingStructure` NoLimit/Limit/PotLimit + `validate`), `table.h` (`Table`: blinds incl.
  heads-up button-as-SB, fixed/pot-limit rules in `options`/`act` with min-raise + closed action on short all-ins, side pots, showdown with
  odd chips clockwise from button, `settle()` advances button). `start_hand_from_deck` is the deterministic testing seam.
- `types.h` (shared `Street`/`Action`/`Payout`, split out to avoid an include cycle) + `event.h` (append-only `Event` history: begin/action/street/settle; `Table::events()`, `clear_events()`; `log` command; folders' cards never reappear after the opening event).
- `src/main.cpp` — `cardengine` entrypoint (target `cardengine_app`, `OUTPUT_NAME cardengine`); speaks `docs/PROTOCOL.md` on stdin/stdout, flushes every reply.
- `game_file.h` (key=value `GameFile` parse/save, line-numbered errors; `games/*.txt` examples; key reference in `docs/CONFIG_FILES.md`) + `protocol.h` (`Session::execute`, never throws; `run_protocol` stream loop).
- `bot.h` (`BotFile` personalities + `Bot` interface; `RandomBot` baseline, `HeuristicBot` with mistake/aggression/looseness sliders; `SeatView` so bots can't peek; `bots/*.txt` examples). Session seats bots in-process via `addbot`/`step`/`bots`; out-of-process runners use filtered views.
- `bot_runner.h` + `cardengine_bot` exe (out-of-process seat runner: `state <seat>` + `options` in, one `act` line out; parses only its own hole). `examples/match.py` hosts engine + N runners. Deal loop counts participants, not seats (busted-out seats sit out without duplicating cards).
- M8 variants: `HandConstruction` (`holdem` best-5 / `omaha` exact 2+3 via `evaluate_omaha`) selected in `GameConfig`/`showdown` file key; `Limit` (fixed BB / 2×BB sizes, `max_raises` cap, short all-ins consume nothing) and `PotLimit` (`bet + pot + 2×call` max) in `Table::options`/`act`; `games/omaha-plo-6max.txt`, `games/holdem-limit-6max.txt`.
- M9 bots: heuristic v2 (out-counted draws, button-distance `position` in `SeatView`); `Adaptive` (prior-weighted opponent looseness retunes its range; `Bot::observe`, `summarize_hand` over the log); `Gto` (MDF calls, fixed-fraction value+bluff bets); `tag`/`lag`/`nit`/`gto`/`adaptive` preset files. `HandSettledEvent.committed` feeds summaries.
- M10 tournaments: `tournament.h` (`Tournament` above `Table`: `begin_hand` applies level blinds, `finish_hand` books busts/places/prizes + level clock, `rebuy`, `advance_level` for GUI clocks); `TournamentFile` composes a `game` file (relative path) with `level`/`prizes`/`buy_in` overrides; `tload`/`tstatus` + auto-finish at `settle`. `Table::set_blinds`/`set_ante` refuse mid-hand changes.
- `examples/cli.py` — hotseat reference client (stdlib-only Python; framing: `state`/`log` are `end`-terminated blocks, `settle`/`tstatus` end with `ok`). `examples/match.py` — engine + N bot processes. Neither is in CTest; CI runs both as a smoke step.
- `tests/` — dependency-free CTest executables via `cardengine_add_test(name source)` (stdlib only, no gtest/Catch2); shared `check`/`expect_throws`/`cards` helpers in `tests/helpers.h`.
- `.github/workflows/ci.yml` — Windows + macOS + Linux build/test, plus the Python examples smoke step.

## Commands

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build --output-on-failure -C Release
```

- Single test: `ctest --test-dir build -R <TestNameRegex> --output-on-failure -C Release`
- After a fix: rebuild + `ctest -R` above, then full suite before finishing.
- Incremental builds during work (full rebuilds cost minutes); clean `build/` + rebuild before final verify and commit. If a binary ever behaves behind its sources, delete the stale exe (MSBuild has skipped relinking against a newer static lib).
- Out-of-source builds only; `build/` is git-ignored. `CMAKE_BUILD_TYPE` is unused with the VS generator (multi-config) — harmless warning.
- Throwaway scripts, probes, and captured logs go in `.scratch/` (git-ignored). Never commit them, and never leave temp files elsewhere in the tree.

## Hard constraints (owner-set)

- Portable ISO C++23 only: no compiler extensions, no POSIX/Win32-only code in core. No `#ifdef _WIN32` / `#ifdef __unix__` in engine code without discussion.
- Strict build: `CMAKE_COMPILE_WARNING_AS_ERROR ON` (`/W4` MSVC, `-Wall -Wextra -Wpedantic` elsewhere). Never silence a warning without discussion.
- Minimal dependencies, stdlib-only default. Any new third-party lib must be MIT-compatible; record its license in `README.md`.
- Single-threaded engine: no `std::thread`/mutexes/atomics in core (determinism is the feature). Concurrency is processes — bots, gateway, GUI — never engine threads.
- Tests deterministic: seed RNG, no network. New behavior needs a test under `tests/`.
- Keep header/`.cpp` separation so the engine stays testable without UI.
