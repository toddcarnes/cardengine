# Changelog

All notable changes to CardEngine, newest first. Versions are pre-1.0:
minor bumps add features, patches fix bugs.

## 0.2.0

- Engine: Omaha showdown construction, limit and pot-limit betting,
  parameterized antes/hole/board counts, single-pass showdown evaluation.
- Bots play all variants (showdown-aware strength, Omaha draw rules).
- Bots: heuristic v2 (draws, position), adaptive (opponent profiling),
  balanced GTO-lite, `tag`/`lag`/`nit`/`gto`/`adaptive` presets.
- Tournaments: levels, eliminations with places, prize pool, rebuys.
- Protocol: per-seat `state`, append-only `log`, bot seating, `showdown` line.
- Hardening: mid-hand setup guards, idempotent tournament booking,
  strict path/seed validation, quoted file paths.
- Out-of-process `cardengine_bot` runner plus `match.py` host.

## 0.1.0

- Initial release: cards, seeded deck, hand evaluator, no-limit Hold'em
  table engine (blinds, side pots, showdown), key=value game files, stdio
  text protocol with reference client, file-driven random/heuristic bots,
  hand-history event log. Dependency-free CTest suite.
