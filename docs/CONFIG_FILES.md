# CardEngine Config Files

Game variants, bot personalities, and tournaments are all plain-text
`key = value` files. This document lists every key: which are mandatory,
which are optional, their defaults, and how each one changes play.
`games/`, `bots/`, and `tournaments/` hold working examples.

## Words used in this guide

No poker or computer background assumed.

**Card-game words.** *Blinds* are forced bets two players must post each
hand so there is always something to play for. An *ante* is a smaller forced
bet everybody pays. The *pot* is the pile of chips bet so far; the winner of
the hand takes it. Each betting round you may *fold* (give up your cards),
*check* (pass when nothing is owed), *call* (match what is owed), or
*raise* (increase what is owed). Going *all-in* means betting all your
remaining chips. In Texas Hold'em each player gets 2 private (*hole*) cards
and shares 5 community cards (*the board*: 3 on the *flop*, 1 on the
*turn*, 1 on the *river*); the best five-card poker hand at the *showdown*
wins. *Omaha* is a close cousin: 4 hole cards, and you must use exactly 2
of them with exactly 3 board cards. *Omaha Hi-Lo* (or *Omaha 8-or-better*)
splits the pot: the best high hand takes half and the best low hand — five
unpaired cards 8 or lower, with the ace playing as 1 — takes the other half.
A low with 7 as its highest card beats a low topped by 8; straights and
flushes don't count against the low, so A-2-3-4-5 is the best possible low
(*the wheel*, which is also a straight for high). With no qualifying low the
high hand scoops the whole pot. A *kicker* breaks ties between equal
hands (pair of kings with an ace kicker beats pair of kings with a queen
kicker). An *out* is an unseen card that would improve your hand — holding
four hearts leaves nine hearts unseen, so nine outs. *Pot odds* compare what
a call costs to what is in the pot: calling 100 to win 500 only needs to
work 1 time in 5. *Position* is where you sit relative to the *dealer
button* (a marker that rotates each hand): players who act later know more,
so the button is the best seat. A *rock* (or *nit*) folds almost everything
and only plays excellent cards. A *maniac* bets and raises constantly with
almost anything. A *freezeout* tournament ends your day when your chips are
gone; a *rebuy* lets you pay to get back in. A *cash game* plays for chips
directly (unlike a tournament, where chips only decide finishing places).
*Heads-up* means a two-player game.

**Computer words.** The *engine* is the program that runs the card game
(deals, enforces rules, awards pots). The *UI* is what a player looks at
and clicks. They talk to each other by exchanging simple text lines. A
*config file* is a text file of settings the engine reads at startup, so
you can change the game without changing any code. A *seed* is a starting
number for the shuffle: the same seed always deals the same cards, which
makes testing and demonstrations repeatable.

## Shared conventions

- One `key = value` per line. Surrounding whitespace is ignored.
- `#` starts a comment; blank lines are ignored. Files saved on Windows
  (with its different line endings) work the same as files saved elsewhere.
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
| `format_version` | **required** | Must be `1`. If the format ever changes, this number changes too, so old files are rejected clearly instead of misread. |
| `name` | `""` | Display name shown in game lists. Optional to the engine, but write one anyway: a client listing your file shows this name, and falls back to the filename when it is empty. |
| `description` | `""` | One-line summary shown in game lists. |
| `num_players` | `6` | Seats at the table, 2–10. With 2 seats the button posts the small blind and bets first before the flop (the standard two-player rules). |
| `starting_stack` | `10000` | Chips each seat starts with. Must be positive and at least cover the big blind. |
| `small_blind` | `50` | The smaller forced bet. Must be positive and no bigger than the big blind. |
| `big_blind` | `100` | The larger forced bet. It also sets the smallest allowed opening bet and the basic raise size. |
| `ante` | `0` | Extra forced bet everybody pays each hand, on top of the blinds. Antes make each hand more expensive to sit out, so players run out of chips faster and the action loosens up. A player who cannot cover it is all-in for what they have. |
| `ante_from` | `seats` | Who pays the ante: `seats` (every participant posts `ante`) or `button` (the button posts `ante × players` at once — the modern tournament shortcut; same dead money, one payer). |
| `straddle` | `0` | Optional blind double the big blind, posted by the player after the big blind. `0` means no straddle; any other value must be exactly twice the big blind. The straddle is *live*: it sets the preflop bet, action starts after the straddler, and they may raise their own blind when the action returns (like the big blind's option). Needs 3+ players. |
| `kill` | `off` | Full kill: `on` doubles the blinds for the hand after any pot over 10× the big blind (consumed by that hand, then off until the next big pot). Classic live-game throttle for runaway action. |
| `runouts` | `1` | Boards run at showdown, 1–3. `1` is classic poker; `2` runs it twice (each side pot splits across two boards dealt from the remaining shoe — all-in cash-game practice that cuts variance without changing anyone's equity). Spare boards are logged as `runout` lines. |
| `hole_cards` | `2` | Private cards dealt to each seat, 1–7. |
| `board_cards` | `5` | Shared community cards, 0–5. Normally dealt 3 on the flop, 1 on the turn, 1 on the river; if you configure fewer, the early streets simply deal fewer (a 4-board deals 3, then 1, then nothing). With zero board cards the game still runs all four betting rounds on private cards alone. |
| `betting` | `nolimit` | `nolimit`: raise any amount from the minimum up to all your chips. `limit`: bets come in fixed sizes only — one big blind before and on the flop, twice the big blind on the turn and river — with a cap per round (see `max_raises`). `potlimit`: raises are capped at what is already in the pot plus the call you are matching (a middle ground between the other two). The minimum raise works the same in all three. |
| `showdown` | `holdem` | `holdem`: the winner is whoever makes the best five cards out of everything. `omaha`: you must use exactly 2 of your hole cards plus exactly 3 board cards (requires 4 hole + 5 board cards). This one rule is what makes Omaha play so differently: hands run much closer together, because everybody is choosing from more combinations. `omaha_hilo` (or `omaha-hilo`): same 4-hole/2+3 deal, but every pot splits — the best high hand takes half and the best 8-or-better low (five unpaired ranks 8 or lower, ace plays low, straights and flushes ignored) takes half. No qualifying low means the high scoops. The odd chip on a split goes to high first, then clockwise from the button. `stud` (7 cards, 4 up) is config-only for now — the table still deals it board-style, so play stays on holdem rules until the stud deal lands. `draw` (5 cards, no board, `games/draw-6max.txt`): one exchange after the preflop betting (each live seat discards up to `max_draw` and redraws off the shoe, button-out), then betting resumes — best five wins. `deuce` (same deal, `games/deuce-6max.txt`) is 2-7 lowball: the worst poker hand wins, so 7-5-4-3-2 (*the nuts*, aces always high) beats an 8-low, and straights and flushes count against (a made straight loses to any broken hand). |
| `upcards` | `0` | Stud only: face-up cards per seat (`4` for seven-card stud). Anything else with `showdown = stud` is rejected. |
| `bring_in` | `0` | Stud only: the forced bet posted by the lowest upcard on third street. `0` means no bring-in (the high hand opens instead). When set it must sit below the small blind. |
| `max_draw` | `5` | Draw only: most cards a seat may exchange in the one discard round (`3` is the classic casino cap, `5` means any number). The full worst case (every seat drawing the max) must fit the deck with the deal — 6 seats at the 3-cap use 48 cards (`games/draw-6max.txt`, `games/deuce-6max.txt`). |
| `max_raises` | `4` | Limit betting only: aggressive bets allowed per round, opening bet included (`4` = the opening bet plus up to 3 re-raises, then everyone may only call or fold). A player whose remaining chips cannot reach the fixed size may still go all-in for what they have; that short all-in does not use up the cap. |

Cross-key rules (rejected with an explanation, never silently adjusted).
For example, a file asking for 4 hole cards and 5 board cards under Hold'em
rules is refused, because no five-card hand can be judged from nine cards:

- `hole_cards + board_cards` must be 5–7 in `holdem` mode.
- `omaha` and `omaha_hilo` modes require exactly 4 hole + 5 board.
- `stud` mode requires exactly 7 hole + 0 board with `upcards = 4` (the deal itself is still board-style until the stud streets land).
- `draw` and `deuce` modes require exactly 5 hole + 0 board with `max_draw` 1–5, and `seats × (5 + max_draw)` must fit the deck (replacements come off the same shoe); boardless games cannot set `runouts` above 1.
- `seats × hole + board` must fit one 52-card deck (10-player Hold'em uses 25 cards, so there is plenty of room).

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

One file is one playable personality. These files do not contain any
programming — they are just settings, and only `format_version` is
mandatory. (A genuinely new way of playing still requires programming; new
*personalities* are files like these.)

| Key | Default | Effect |
|---|---|---|
| `format_version` | **required** | Must be `1`. |
| `name` | `""` | Shown in logs and on screen — this is the "Fred" in "Fred folds". Optional to the engine, but write one anyway: clients fall back to the filename when it is empty. |
| `description` | `""` | One-line summary. |
| `style` | `heuristic` | How the bot thinks. `random`: picks any legal move at random — the punching bag every real bot must beat. `heuristic`: plays from experience-style rules — it values starting hands (pairs and high cards score best), values made hands after the flop, counts its outs when drawing, and plays a little tighter in early seats and looser on the button. `adaptive`: the same rules, plus it watches every opponent across hands — against players who play lots of hands it calls lighter, against players who fold everything it waits for big hands. `gto` (short for *game theory optimal*): tries to play balanced, hard-to-exploit poker — strong hands and occasional bluffs are bet exactly the same way so opponents cannot tell them apart, and it calls just often enough that bluffing against it does not pay. |
| `mistake_rate` | `0.0` | 0–1. How often a decision is thrown away and replaced by a random legal move. Small values (like 0.05) make strong bots feel human and beatable; `1.0` plays randomly. |
| `aggression` | `0.5` | 0–1. How big it bets (higher means a bigger fraction of the pot on top of the current bet) and how good a hand needs to be before it bets for value. Deep-pool ablation (12-bot field, 4,665 tables, ±17 noise): 0.3→0.85 spans +1→+7 — real but small on an already-aggressive base; the big aggression gains were captured when the base was tight. |
| `looseness` | `0.3` | 0–1. How bad a hand it will still continue with when facing a bet. The adaptive style adjusts this during play; the starting value is its first guess. Board-scale: 0.0→0.5 spans −12→+13, monotonic but small — a tuning knob, not a weapon. |
| `survival` | `0.0` | 0–1. Tournament risk premium: when short-stacked, marginal calls that risk busting become folds. `0` plays cash-game pot odds at any stack; `1` (see the `survivor` preset) prices its tournament life. Deep stacks are unaffected either way. Deep-pool ablation (12-bot field, 4,665 tables, ±5 noise): 0→1 spans −79→−5 — removing survival from Vanguard *loses* 79 points. The earlier inverted reading (+70 for removal) was small-pool pathology; at depth, survival is confirmed as the largest structural edge. |
| `bluff_rate` | `0.35` | 0–1. How often a weak hand bets anyway when nobody has bet yet, so value bets get paid off. `0` never bluffs; `1` always fires. Board-scale: 0→1 spans −42→+8, monotonic — the field does not adjust to aggression, but the base already bluffs at 0.7, so the headroom is small. |
| `defense` | `1.0` | 0–2. Scales how often the `gto` style calls to keep bluffs unprofitable (`1` = textbook minimum defense, `0` = fold everything but the nuts, `2` = never fold). Board-scale on a heuristic base: all values within ±10 of base — defense only matters for the GTO style's MDF path, which heuristics don't take. (The +65 screen-scale reading was pool noise.) |
| `position_weight` | `1.0` | 0–2. How much acting order matters: `0` plays every seat alike, `1` is the classic nudge (tighter in early seats, looser on the button), `2` doubles it. Board-scale: 0/1/2 within −4→−20 of base — decoration against bots that don't exploit position. |
| `adapt_rate` | `1.0` | 0–2. `adaptive` style only: how hard it leans on its reads. `0` plays its starting `looseness` forever; `1` shifts its calling range with the table average and catches suspected bluffs lighter; `2` doubles both swings. Not covered by the Vanguard-base ablation (needs adaptive style — separate study). |
| `barrels` | `0.0` | 0–2. How often the bot keeps betting after it was the last aggressor — second barrels on the flop, delayed bets on later streets — with hands too weak to bet fresh. `0` (the default) only bets hands that qualify on their own; `2` fires the story almost every time. Needs a real hand behind it: pure air still gives up. Deep-pool ablation (12-bot field, ±13 noise): 0→2 spans −8→0 — no measurable effect at depth. Stories don't move strong fields. |
| `planning` | `0` | 0–2. One-street lookahead: `0` judges the hand as it stands; `1` blends in where the hand is heading (draws play stronger, vulnerable made hands slightly weaker); `2` leans harder into the projection. Cheap arithmetic, not a search tree — costs microseconds. Board-scale: 0/1/2 within ±8 of base — no measurable effect in isolation; contributes ~+15 stacked on survival (interaction, not addition). |
| `seed` | `0` | Starting number for its randomness. Same file plus same game means the same decisions on every computer — useful for testing and demonstrations. Affects `random` style and mistake/bluff draws only; seed-varied controls in ablation pools spread ±75–105 at board scale in small pools (winner-take-all dynamics amplify noise) — the yardstick every slider effect above is read against. |

Notes:

- The preset files are just slider combinations, no programming. `nit`
  (an extreme rock: almost everything folded) is low looseness with low
  aggression. `tag` (*tight-aggressive*: few hands, played hard) is low
  looseness with high aggression. `lag` (*loose-aggressive*: many hands,
  maximum pressure) is high on both. Try copying one and nudging a slider.
- The adaptive style needs several played hands before its opinion of each
  opponent means anything (it starts by assuming everyone is average, and
  real evidence gradually outweighs that guess). In a single hand it simply
  plays its starting `looseness`.
- Bots only ever see their own hole cards plus the public cards and chips.
  No setting in these files can show a bot more than that — a strong bot
  wins from its settings, never from peeking.

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

A tournament file takes a game and adds a schedule and a prize pool: a
series of blind levels that get more expensive, and payouts for the top
finishers. When your chips are gone you are out, and the last player seated
wins.

| Key | Default | Effect |
|---|---|---|
| `format_version` | **required** | Must be `1`. |
| `name` / `description` | `""` | For game lists. |
| `game` | none | Path to a game file, read relative to the tournament file. Its rules become the starting point; any game keys written in the tournament file override them. Leave it out to build on standard Hold'em. |
| Any game-file key | (game/defaults) | Overrides: `num_players`, `starting_stack`, blinds, `ante`, `betting`, `showdown`, etc. |
| `buy_in` | `0` | What each player pays into the prize pool. Pool = `buy_in × seats`. `0` means play money — chips only, no prizes. |
| `prizes` | (empty) | Who gets paid, as comma-separated percentages by finishing place: `50, 30, 20` means the winner gets half the pool, second place 30%, third 20%. Must add up to 100 or less; empty means nobody gets paid. Any leftover pennies go to the winner. |
| `level` | one `50, 100, 0, 10` | Repeatable — write one line per level: `small blind, big blind, ante, hands`. Levels advance automatically once their hands are played; the last level then repeats forever. If you write no levels you get a single `50, 100, 0, 10` level. Append a fifth number for minutes (`50, 100, 0, 10, 15` = 10 hands *or* 15 minutes, whichever hits first); timed expiry fires at the next deal, never mid-hand. |
| `level_elapsed` | `0` | (Session files only.) Seconds already banked in the current level — written by `save`, resumed by `restore`. |

Tournament rules the file implies:

- Players bust in the order their chips run out. If you bust while N
  players (including you) are still standing, you finish Nth, and a matching
  `prizes` entry pays you on the spot.
- If two players bust in the same hand, places go in seat order.
- Buying back in (`trebuy`), moving levels by the clock (`tlevel`), and
  final-table deals (`tchop`) all have matching protocol commands; saving
  and restoring the whole session (`save` / `restore`) is covered under
  "Session files" below.

## Championship files (`championships/`)

A championship is a fixed bracket of ordinary tournaments: each stage's
winners fill the next stage's seats in order, and the winner of the single
final table is the champion. Chips reset every stage, so a championship is
pure bracket math plus scheduling — no new poker rules.

| Key | Default | Effect |
|---|---|---|
| `format_version` | **required** | Must be `1`. |
| `name` / `description` | `""` | For game lists. |
| `champion_prize` | `0` | Flat award recorded for the champion, on top of whatever the stage tournaments pay. |
| `stage` | (none) | Repeatable: `tournament file, tables`, with the file path read relative to the championship file. Stages play in order; every stage must produce exactly as many winners (one per table) as the next stage has seats, and the final stage must be one table. |
| `game` + `depth` | (none) | Shortcut instead of `stage` lines: run a level-`depth` bracket where every table plays the same game file. Depth 0 is a single tournament; depth 2 heads-up opens 4 tables, then 2, then a final. Never combined with `stage` lines. |
| `buy_in`, `prizes`, `level`, any game key | (game/defaults) | Shortcut only: applied to every generated table (`level` follows the game's blinds when omitted). |

Examples: `winter-classic.txt` (two 6-seat semifinal freezeouts into a
heads-up final) and `novice-cup.txt` (the depth-2 shortcut).

## Session files (`save` / `restore`)

A session file is a snapshot of a running game, written by `save` and read
back by `restore`. Same `key = value` conventions as every other file; only
`format_version`, `mode`, `stacks`, `button`, and `events` are mandatory
(the rest default or apply to tournaments only). The `events` count says how
many raw log lines follow the keys — they replay verbatim into the restored
log for audit and bot training.

| Key | Default | Effect |
|---|---|---|
| `format_version` | **required** | Must be `1`. |
| `mode` | **required** | `cash` or `tournament`. Tournament mode additionally requires `eliminated`, `places`, and `prizes_earned`. |
| Any game-file key | (game/defaults) | Rules in force: `num_players`, `starting_stack`, blinds, `ante`, `betting`, `showdown`, etc. `stacks` must carry one entry per seat. |
| `stacks` | **required** | Per-seat chips, in order: `10000,9500,...`. |
| `sitting_out` | all `0` | Per-seat flags (`1` = sitting out): `0,1,0,...`. One entry per seat. |
| `button` | **required** | Seat holding the dealer button. |
| `buy_in` / `prizes` / `level` | (tournament) | Tournament schedule and money, same shapes as tournament files (`level` repeatable, with optional minutes). |
| `level_index` / `hands_into_level` | `0` | Where the blind clock stands (hands count plus banked wall time below). |
| `level_elapsed` | `0` | Seconds already banked in the current level (timed levels) — written by `save`, resumed by `restore`. |
| `prize_pool` / `prize_awarded` | `0` | Chips paid in vs prizes booked so far. |
| `eliminated` | (tournament) | Per-seat bust flags (`1` = out). One entry per seat. |
| `places` | (tournament) | Per-seat finishing places (`0` = still playing). One entry per seat. |
| `prizes_earned` | (tournament) | Per-seat booked prizes. One entry per seat. |
| `events` | **required** | How many raw log lines follow the keys. A mismatch (or a malformed log line) rejects the file with a line number and restores nothing. |

Snapshots are always between hands: `save` and `restore` mid-hand are
refused. `restore` clears seated bots — `addbot` them again (adaptive reads
rebuild from the restored log as new hands are observed).
