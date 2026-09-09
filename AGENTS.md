# AGENTS.md — CardEngine (C++ / CMake)

Open source card game engine (Texas Hold'em first). MIT licensed, copyright Todd Carnes (`LICENSE`).
Verified 2026-09-05: configure + build + `ctest` pass with CMake 4.3.4 / VS 2026 on Windows.

## Structure (real)

- Root `CMakeLists.txt` — `project(cardengine VERSION 0.5.0 LANGUAGES CXX)`; C++23 required, `CMAKE_CXX_EXTENSIONS OFF`.
- `include/cardengine/cardengine.h` + `src/cardengine.cpp` — `cardengine` static lib (engine lives here).
  Umbrella header; engine modules: `card.h` (`Rank` 2–14 / `Suit` / `Card`, `"Qh"` notation + `parse_card`),
  `deck.h` (`Deck`, deterministic `shuffle(seed)`), `hand.h` (`evaluate_five` / `evaluate_best`, `HandValue` with
  category + significance-ordered tiebreak). `evaluate_best` takes 5–7 cards; throws otherwise.
  Table engine: `config.h` (`GameConfig`: seats/stacks/blinds, `ante` (+`ante_from` seats/button) / `straddle` (live UTG 2×BB) / `kill` (double blinds after a 10×BB pot), `hole_cards`/`board_cards` with deck-math
  validation, `BettingStructure` NoLimit/Limit/PotLimit + `validate`), `table.h` (`Table`: blinds incl.
  heads-up button-as-SB, fixed/pot-limit rules in `options`/`act` with min-raise + closed action on short all-ins, side pots, showdown with
  odd chips clockwise from button, `settle()` advances button). `start_hand_from_deck` is the deterministic testing seam.
- `types.h` (shared `Street`/`Action`/`Payout`, split out to avoid an include cycle — explicit values, never reorder: saved logs persist them) + `event.h` (append-only `Event` history: begin/action/street/stud/draw/settle; `Table::events()`, `clear_events()`; `log` command; folders' cards never reappear after the opening event).
- `src/main.cpp` — `cardengine` entrypoint (target `cardengine_app`, `OUTPUT_NAME cardengine`); speaks `docs/PROTOCOL.md` on stdin/stdout, flushes every reply.
- `game_file.h` (key=value `GameFile` parse/save, line-numbered errors; `games/*.txt` examples; key reference in `docs/CONFIG_FILES.md`) + `protocol.h` (`Session::execute`, never throws; `run_protocol` stream loop).
- `bot.h` (`BotFile` personalities + `Bot` interface; `RandomBot` baseline, `HeuristicBot` with mistake/aggression/looseness sliders; `SeatView` so bots can't peek; `bots/*.txt` examples). Session seats bots in-process via `addbot`/`step`/`bots`; out-of-process runners use filtered views.
- `bot_runner.h` + `cardengine_bot` exe (out-of-process seat runner: `state <seat>` + `options` in, one `act` line out; parses only its own hole). `examples/match.py` hosts engine + N runners. Deal loop counts participants, not seats (busted-out seats sit out without duplicating cards).
- M8 variants: `HandConstruction` (`holdem` best-5 / `omaha` exact 2+3 via `evaluate_omaha` / `omaha_hilo` high/low split via `evaluate_omaha_hilo` with 8-or-better `LowValue`, high scoops unqualified, odd chip to high first) selected in `GameConfig`/`showdown` file key; `Limit` (fixed BB / 2×BB sizes, `max_raises` cap, short all-ins consume nothing) and `PotLimit` (`bet + pot + 2×call` max) in `Table::options`/`act`; `games/omaha-plo-6max.txt`, `games/omaha-hilo-6max.txt`, `games/holdem-limit-6max.txt`.
- M12 draw: `HandConstruction` (`draw` best-5 / `deuce` 2-7 lowball via `evaluate_deuce`, pat or one exchange) with `max_draw` cap; `Street::Draw` exchange street, `DrawEvent`, `Table::discard`/`draws_pending`, `discard`/`draws` protocol, `Bot::choose_discards`; `games/draw-6max.txt`, `games/deuce-6max.txt`.
- M13 stud: `HandConstruction::StudSeven` with real rules (2 down + 1 up on `Street::Third` with `bring_in_seat` bring-in, up cards on `Fourth`–`Sixth` opening on the best visible hand, down or community river on `Seventh` via `community_`, limit small-big split with complete-not-a-raise); `StudDealtEvent`, `up`/`community` state lines, `SeatView::up/rival_up/community`; `games/stud-8max.txt` (2–8 seats, no straddle/kill/runouts).
- M9 bots: heuristic v2 (out-counted draws, button-distance `position` in `SeatView`); `Adaptive` (prior-weighted opponent looseness retunes its range; `Bot::observe`, `summarize_hand` over the log); `Gto` (MDF calls, fixed-fraction value+bluff bets); `tag`/`lag`/`nit`/`gto`/`adaptive`/`station`/`fitfold`/`survivor`/`vanguard` preset files. `HandSettledEvent.committed` feeds summaries.
- M10 tournaments: `tournament.h` (`Tournament` above `Table`: `begin_hand` applies level blinds, `finish_hand` books busts/places/prizes + level clock, `rebuy`, `advance_level` for GUI clocks); `TournamentFile` composes a `game` file (relative path) with `level`/`prizes`/`buy_in` overrides; `tload`/`tstatus` + auto-finish at `settle`. `Table::set_blinds`/`set_ante` refuse mid-hand changes.
- M11 championships: `championship.h` (fixed bracket of `Tournament`s; winners feed seats in order, stacks reset; `source()` slot math; gating touches to unplayed stages); `generate_championship` depth shortcut (`P^depth` openers), `estimate_championship` counts + hand bound; `championships/*.txt` (`stage` lines or `game`+`depth`); `cardengine_stress` exe (per-table shuffled seating, CSV results with lineups + `placements` finishing order, `--yes` gate, `--max-hands` cap).
- `tests/` — dependency-free CTest executables via `cardengine_add_test(name source)` (stdlib only, no gtest/Catch2); shared `check`/`expect_throws`/`cards` helpers in `tests/helpers.h`; `test_fuzz` invariant fuzz across variant shapes. Quarantine (MSBuild links stale objects under ctest): one live `Table` per block in test files — never two `Table`s in one scope.
- `.github/workflows/ci.yml` — Windows + macOS + Linux build/test, plus the Python examples smoke step.
- `examples/cli.py` — hotseat reference client (stdlib-only Python; framing: `state`/`log` are `end`-terminated blocks, `settle`/`tstatus` end with `ok`). `examples/match.py` — engine + N bot processes. Neither is in CTest; CI runs both as a smoke step.
- `scripts/` — tracked reusable tooling (scoreboard generators, campaign configs). Scripts read committed configs, dump regenerable outputs (CSVs, logs) into `.scratch/`, and write only final results (e.g. `ratings/*.json`) to the tree. Reusable means committed — anything that must survive lives here, never in `.scratch/`.

## Commands

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build --output-on-failure -C Release
```

- Single test: `ctest --test-dir build -R <TestNameRegex> --output-on-failure -C Release`
- After a fix: rebuild + `ctest -R` above, then full suite before finishing.
- Clean build before every build: delete `build/` completely and reconfigure + rebuild (owner-set 2026-09-08 — MSBuild silently links stale objects/libs, incremental builds are unreliable).
- Out-of-source builds only; `build/` is git-ignored. `CMAKE_BUILD_TYPE` is unused with the VS generator (multi-config) — harmless warning.
- Throwaway scripts, probes, and captured logs go in `.scratch/` (git-ignored). Never commit them, and never leave temp files elsewhere in the tree.
- DANGER (2026-09-08): bulk-deleted all of `.scratch/` with `Remove-Item` — `-LiteralPath` silently ignores `-Include`, so the intended `*.csv,*.log` filter matched everything and wiped the scoreboard scripts too. When deleting by pattern: never combine `-LiteralPath` with `-Include`/`-Exclude` (use `-Path`), list first with `Get-ChildItem` alone, then pipe to `Remove-Item`. `.scratch/` is throwaway *content*, but reusable scripts must live in `scripts/` — if it only exists in `.scratch/`, move it before any cleanup.
- Commit policy (owner-set 2026-09-06): one commit per finished item — green only (rebuild + full `ctest` clean + docs + CHANGELOG entry). Never batch a whole wave into one diff, never commit red or intermediate edits. `git log` style is `<Area>: <description>` (e.g. `Protocol: tlevel and trebuy tournament commands`).
- Versioning: accumulate features under CHANGELOG `Unreleased`; bump `project(... VERSION ...)` + the `test_version` tripwire literals at wave boundaries only (e.g. 0.3.0 when Wave 1 lands), never per item. Tag every bump as annotated `vX.Y.Z` on the bump commit (`git tag -a vX.Y.Z <sha> -m "..."`); tags are part of the release, not optional.

## Hard constraints (owner-set)

- Portable ISO C++23 only: no compiler extensions, no POSIX/Win32-only code in core. No `#ifdef _WIN32` / `#ifdef __unix__` in engine code without discussion.
- Strict build: `CMAKE_COMPILE_WARNING_AS_ERROR ON` (`/W4` MSVC, `-Wall -Wextra -Wpedantic` elsewhere). Never silence a warning without discussion.
- Minimal dependencies, stdlib-only default. Any new third-party lib must be MIT-compatible; record its license in `README.md`.
- Single-threaded engine: no `std::thread`/mutexes/atomics in core (determinism is the feature). Concurrency is processes — bots, gateway, GUI — never engine threads.
- Tests deterministic: seed RNG, no network. New behavior needs a test under `tests/`.
- Persistence isn't done until the protocol `save`/`restore` path carries the new state end-to-end, with a Session-level round-trip test.
- Keep header/`.cpp` separation so the engine stays testable without UI.
