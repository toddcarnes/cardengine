# CardEngine

Open source card game engine in portable ISO C++23. Copyright (c) 2026 Todd Carnes, MIT licensed (see `LICENSE`).
Texas Hold'em is the first game, built as the test case for the engine's game-configuration design.

## Goals

- Correct, portable card game engine (Windows, macOS, Linux); Texas Hold'em first.
- Standard C++23 only: `CMAKE_CXX_EXTENSIONS OFF`, no compiler extensions, no POSIX/Win32-only code in core.
- Minimal dependencies: standard library only by default. Any added library must be MIT-compatible.

## Build

Requires CMake 3.28+ and a C++23 compiler (MSVC 2022+, GCC 13+, Clang 17+).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

Run one test:

```sh
ctest --test-dir build -R test_version --output-on-failure
```

Out-of-source builds only; `build/` is git-ignored.

## Play

The engine runs as its own program in the background, and any user
interface talks to it by exchanging simple text lines (full spec in
`docs/PROTOCOL.md`; every setting is explained in `docs/CONFIG_FILES.md`).
Fastest ways to try it:

```sh
# Shared-screen client (Python 3, nothing to install): every seat is played
# by a human taking turns at this computer.
python examples/cli.py --game games/holdem-headsup.txt
# Automatic demo: simple built-in players check and call, 3 hands.
python examples/cli.py --auto --hands 3 --seed 42
# Seat built-in bots (seat 0 stays human, or --auto plays it too).
python examples/cli.py --auto --bots bots/tight.txt,bots/random.txt --hands 2
# Separate bot programs, one per seat, each seeing only its own cards.
python examples/match.py --auto --bots 1:bots/tight.txt --bots 2:bots/random.txt --hands 2
# Tournament (players bust out, places and prizes awarded, stops at the champion).
python examples/match.py --auto --bots 1:bots/tight.txt --tournament tournaments/freezeout-6max.txt --hands 30
```

## Layout

- `include/cardengine/` — public engine headers (`card.h`, `deck.h`, `hand.h`, `config.h`, `types.h`, `table.h`, `event.h`, `game_file.h`, `protocol.h`, `bot.h`, `bot_runner.h`; `cardengine.h` umbrella)
- `src/` — `cardengine` static library + `cardengine` entrypoint (target `cardengine_app`, `OUTPUT_NAME cardengine`; speaks `docs/PROTOCOL.md` on stdin/stdout) + `cardengine_bot` seat runner
- `games/` — shareable game-variant files (`key = value`, see `docs/PROTOCOL.md`)
- `tournaments/` — shareable tournament files (levels, prizes, buy-ins; compose game files)
- `bots/` — shareable bot-personality files (same format family)
- `examples/cli.py` — shared-screen reference client; proves any programming language can talk to the engine using only typed and printed lines
- `examples/match.py` — match host: one engine plus one bot program per seat (the same shape a future network version will reuse)
- `docs/` — protocol spec (`PROTOCOL.md`) and config-file reference (`CONFIG_FILES.md`)
- `tests/` — dependency-free CTest executables (no external test framework)
- `.github/workflows/ci.yml` — builds + tests on Windows, macOS, Linux

## Contributing

- Keep core portable: no `#ifdef _WIN32` / `#ifdef __unix__` in engine code without prior discussion.
- Every behavior change needs a deterministic test under `tests/` (seed RNG, no network).
- New third-party code must be MIT-compatible; document the license in this README and prefer header-only or vendored copies with their notices intact.
