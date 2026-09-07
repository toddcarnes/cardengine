# Changelog

All notable changes to CardEngine, newest first. Versions are pre-1.0:
minor bumps add features, patches fix bugs.

## Unreleased

- Examples: `host.py` network host reference (TCP seats with `hello`/
  `welcome`, filtered `state <seat>` views, action-clock `timeout`s,
  disconnect `sitout`/`resume`, per-hand `save`) plus `bot_proxy.py`
  bridging `cardengine_bot` programs onto seats. Same match.py shape with
  sockets instead of pipes; stdlib only.
- Variants: forced-bet options — live UTG straddle (`straddle = 2×BB`: posts,
  sets the bet, acts last preflop with the raiser's option), button ante
  (`ante_from = button`: button posts the whole table's ante), full kill
  (`kill = on`: blinds double the hand after a 10×BB pot). Config + file
  keys with validation.
- Variants: Omaha Hi-Lo (`showdown = omaha_hilo`, `games/omaha-hilo-6max.txt`)
  — 8-or-better low evaluator under exact 2+3 construction, every pot split
  high/low (high scoops unqualified, odd chip to high first, quartering
  paid clockwise), bots play both ways (wheel-card preflop premium, low
  call discount, postflop live-low strength).

## 0.4.0 — Wave 2: persistence and clocks

- Clocks: timed blind levels (`level = sb, bb, ante, hands, minutes` — wall
  time banked per hand, fires at the next deal, never mid-hand;
  `elapsed/left` in `tstatus`, `level_elapsed` in saves), action clock
  (`acting_since` in `state`, `timeout` folds the holder by the clock and
  logs a `timeout` event bots read as a fold). Hosts enforce, engine stamps.
- Sessions: `save <file>` / `restore <file>` persist the whole game between
  hands (rules, stacks, button, sit-outs, tournament books, raw log) as
  versioned key=value session files; `Table`/`Tournament` snapshot/restore
  plus log-line parsing (`parse_event`) underneath. Bots clear on restore.

## 0.3.0 — Wave 1: tournament operations over the wire

- Tournaments: `tchop <seat:amount> ...` final-table deals — survivors split
  the remaining pool exactly, places go by stack (leader first, earlier
  busts slide below in bust order), and the tournament closes.
- Protocol: `sitout <seat>` / `resume <seat>` park a disconnected seat
  (posts/dealt nothing from the next hand, button skips it) and bring it
  back; sitting-out seats cannot rebuy. `state` marks them with `out`.
- Table: fixed postflop opening action (new betting round now reopens
  action for every live seat instead of stalling on all-checks).
- Protocol: `tlevel` (manual tournament clock advance) and `trebuy <seat>`
  (between-hands top-up / bust-out return) expose the existing
  `Tournament::advance_level` / `rebuy` library calls over the wire.
- Championships: fixed brackets of tournaments with winner advancement,
  `game`+`depth` shortcut generation, game-count estimates, and the
  `cardengine_stress` runner (rotating bots, CSV results with lineups and a
  `placements` finishing-order column, `--yes` gate, `--max-hands` cap) for
  edge-case hunting and bot strength ranging.
- Ratings: `examples/rate.py` maintains Bradley-Terry ratings (Elo scale,
  order-independent fit) in per-variant `ratings/<variant>.json` boards plus
  an equal-weight `ratings/overall.json` (`--overall` prints per-variant
  tables/duels/weight for audit).
- Examples: `cli.py`/`match.py` draw hand seeds from OS entropy (`secrets`)
  unless `--seed` is passed explicitly for reproducible runs.
- Bots: `station` (calling station) and `fitfold` (fit-or-fold) beginner
  presets round out the bottom of the ladder.
- Examples: `tune.py` grid-searches heuristic sliders in screen + final
  brackets with BT scoring; first campaign found no TAG-beater (best: a
  looser variant at −6 over 6220 tables). A GTO equity-floor fix was tried
  and reverted (−50: this field bets value-heavy, MDF already over-defends).
- Ladder: `Fit-or-Fold Fred` joined the rated 10-bot board (~1200s).
- Variants: per-variant BT boards (`ratings/<variant>.json`) plus an
  equal-weight `ratings/overall.json` (`rate.py --overall`); Omaha preflop
  hand reading (pairs/coordination over bare high cards) after PLO data
  showed Hold'em heuristics overplaying trash.
- Hardening: `test_fuzz` invariant fuzz across all five variant shapes
  (chip conservation, raise-range legality, deck integrity, determinism).
- Bots: `survival` slider (ICM-lite risk premium on elimination-risk calls)
  plus a `Survivor` preset; turns marginal bust-out calls into folds short.
- Bots: Phase-1 trio (`bluff_rate`, `defense`, `position_weight`) and
  Phase-2 `adapt_rate` (aggression reads: bluff-catch maniacs, respect
  rocks); `tune.py` searches all six plus style and variant.
- Bots: Phase-3 `barrels` (continued aggression: the prior street's
  aggressor keeps firing medium hands); `tune.py` `--ba` grid.
- Bots: Phase-4 `planning` (one-street lookahead: draws play up, vulnerable
  made hands shade down; zero-cost arithmetic, no search tree).
- Bots: `Vanguard` preset (tune21 bluffing + planning 2 + survival 1);
  beats Survivor head-to-head, leads the next-gen field.
- Tournaments: levels, eliminations with places, prize pool, rebuys.
- Docs: plain-English pass; config-file reference; name conventions.

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
