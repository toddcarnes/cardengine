# CardEngine Config Files

Game variants, bot personalities, and tournaments are all plain-text
`key = value` files. This document lists every key: which are mandatory,
which are optional, their defaults, and how each one changes play.
`games/`, `bots/`, and `tournaments/` hold working examples.

## Shared conventions

- One `key = value` per line. Surrounding whitespace is ignored.
- `#` starts a comment; blank lines are ignored. CRLF files work.
- Keys are case-insensitive (`Num_Players` works). Enum values
  (`nolimit`, `omaha`, `gto`) are case-insensitive too.
- `name`/`description`/`game` accept `"double quotes"` (needed when the
  value contains `#` or `=`); quotes are optional otherwise.
- Every failure names its line: `games/broken.txt: line 4: bad integer 'six'`.
  Unknown keys are rejected — a typo can error, never silently misconfigure.
- After parsing, the config is validated (`invalid config: ...`). An invalid
  file never produces a half-configured game.

## Game files (`games/`)

One file is one playable variant. Only `format_version` is mandatory;
everything else falls back to standard no-limit Hold'em.

| Key | Default | Rules |
|---|---|---|
| `format_version` | **required** | Must be `1`. Future formats bump this instead of misreading. |
| `name` | `""` | Display name for UI game lists. |
| `description` | `""` | One-line summary for UI game lists. |
| `num_players` | `6` | Seats, 2–10. With 2 seats the button posts the small blind and acts first preflop (heads-up rules). |
| `starting_stack` | `10000` | Chips per seat. Must be positive and cover the big blind. |
| `small_blind` | `50` | Must be positive and not exceed the big blind. |
| `big_blind` | `100` | Sets the minimum open and the preflop/early-street raise unit. |
| `ante` | `0` | Dead money every participant pays each hand (short stacks go all-in on it). Raises the cost of folding to showdown. |
| `hole_cards` | `2` | Cards dealt to each seat, 1–7. |
| `board_cards` | `5` | Community cards, 0–5. Dealt 3 (flop), 1 (turn), 1 (river), scaled down when fewer are configured (a 4-board deals 3+1+0). Zero boards still run all four betting rounds. |
| `betting` | `nolimit` | `nolimit`: any raise from the minimum to all-in. `limit`: fixed bets (big blind preflop/flop, double after) with a per-round cap. `potlimit`: raises capped at the pot-sized raise (`bet + pot + 2×call`). Minimums work the same in all three. |
| `showdown` | `holdem` | `holdem`: best any-five. `omaha`: exactly 2 from hand + 3 from board (requires 4 hole + 5 board). |
| `max_raises` | `4` | Limit betting only: aggressive actions allowed per round, opening bet included (`4` = open + 3 re-raises, then capped). Short all-ins below the fixed size are always legal and consume no cap. |

Cross-key rules (rejected with an explanation, never silently adjusted):

- `hole_cards + board_cards` must be 5–7 in `holdem` mode (best-five needs it).
- `omaha` mode requires exactly 4 hole + 5 board.
- `seats × hole + board` must fit one 52-card deck (10-max Hold'em uses 25).

Example — turn the stock game into a tight limit game by changing three lines:

```
format_version = 1
name = "Kitchen Limit"
num_players = 6
starting_stack = 1000
small_blind = 10
big_blind = 20
betting = limit
max_raises = 3
```

## Bot files (`bots/`)

One file is one playable personality: parameters, not code. Only
`format_version` is mandatory. New *strategies* still need C++ behind the
`Bot` interface; new *personalities* are files like these.

| Key | Default | Effect |
|---|---|---|
| `format_version` | **required** | Must be `1`. |
| `name` | `""` | Shown in logs and UIs. |
| `description` | `""` | One-line summary. |
| `style` | `heuristic` | `random`: uniform legal action (the baseline every real bot must beat). `heuristic`: Chen-style preflop points, category postflop values, counted outs for draws, tighter early / looser late. `adaptive`: the heuristic, plus it profiles each opponent's looseness across hands and continues wider against maniacs, tighter against rocks. `gto`: fixed-fraction value bets with same-size bluffs, calls at minimum-defense frequency. |
| `mistake_rate` | `0.0` | 0–1. Fraction of decisions replaced by a random legal action. Low values humanize strong bots; `1.0` is a slower random bot. |
| `aggression` | `0.5` | 0–1. Scales bet sizing (fraction of pot on top of the current bet) and how thin the value bets go. |
| `looseness` | `0.3` | 0–1. How weak a hand still continues against a bet. The adaptive style retunes this mid-session; the starting value is its prior. |
| `seed` | `0` | Mistake/random-number seed. Same file + same game = same decisions, every platform. |

Notes:

- `looseness = 0` + low aggression plays like a rock; high `looseness` +
  high `aggression` plays like a maniac. The `tag`/`lag`/`nit` presets are
  exactly this trick, no code involved.
- The adaptive style needs several observed hands before its image of each
  opponent means anything (prior-weighted: 3 imaginary neutral hands). In a
  single hand it plays its starting `looseness`.
- Bots only ever see their own hole cards plus public state. Files cannot
  grant extra information — strength comes from parameters, not peeking.

Example — a splashy maniac for home games:

```
format_version = 1
name = "Birthday Maniac"
style = heuristic
mistake_rate = 0.1
aggression = 0.9
looseness = 0.7
seed = 42
```

## Tournament files (`tournaments/`)

A tournament composes a game with a schedule and a prize pool.

| Key | Default | Effect |
|---|---|---|
| `format_version` | **required** | Must be `1`. |
| `name` / `description` | `""` | For UI lists. |
| `game` | none | Path to a game file, resolved relative to the tournament file. Its rules become the base; any game keys in the tournament file override them. Omit it to build on Hold'em defaults. |
| Any game-file key | (game/defaults) | Overrides: `num_players`, `starting_stack`, blinds, `ante`, `betting`, `showdown`, etc. |
| `buy_in` | `0` | Per-player prize pool contribution. Pool = `buy_in × seats`. `0` means play money (chips only, no prizes). |
| `prizes` | (empty) | Comma list of place percentages, e.g. `50, 30, 20`. Must sum to ≤ 100. Empty means no payouts. Leftover dust goes to the champion. |
| `level` | one `50, 100, 0, 10` | Repeatable: `small, big, ante, hands`. Levels advance automatically after their hands complete; the last level repeats. At least the default always exists. |

Tournament rules the file implies:

- Eliminations follow bust order (simultaneous busts in seat order); a
  busted player finishing where N players remained takes Nth place, and a
  matching `prizes` entry pays immediately.
- Rebuys and manual level advances (for GUI clocks) are library calls
  (`Tournament::rebuy`, `advance_level`); the protocol gains matching
  commands when a GUI needs them.
