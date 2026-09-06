# CardEngine Text Protocol

How outside programs talk to the engine. The engine runs as its own program
in the background; a user interface (or a bot, or a script) starts it, sends
it one text line per command, and reads back replies. This works the same
from any programming language — see `examples/cli.py` (about 150 lines of
Python, using nothing but typed and printed lines) for the reference example.
(The design follows the same idea as chess engines, which is part of why
this project is called CardEngine.)

## Framing rules (clients must follow these)

- One command per line, one reply per command.
- Most replies are a single line (`ok ...` / `error ...` / `bye`). Three
  commands reply multi-line: `state` and `log` send a block terminated by
  an `end` line; `settle` and `tstatus` send prelude lines and a final `ok`.
- The engine flushes after every reply, so piped clients never deadlock.
  The first line on startup is a banner (`cardengine <version>`), not a reply.
- An `error` reply never changes session state — retry or send `quit`.

## Commands

| Command | Reply | Notes |
|---|---|---|
| `help` | `ok commands: ...` | |
| `load <game-file>` | `ok` | Re-tables with fresh stacks. Path may contain spaces. |
| `tload <tournament-file>` | `ok` | Tournament mode (below); clears bots. |
| `tstatus` | `tournament ...`, `standing ...` × n, `ok` | Levels, pool, places. |
| `start <seed>` | `ok` | Starts a hand; stacks and button carry over between hands. |
| `state` | block, then `end` | Full table dump — local trust only (below). |
| `state <seat>` | block, then `end` | That seat's view: own hole cards shown, all other seats `--`. |
| `options` | `options seat S check yes\|no call N raise yes\|no [min M max X]` | For the acting seat; `error no action pending` otherwise. |
| `act fold\|check\|call` | `ok` | Acts for the current seat. |
| `act raise <amount>` | `ok` | Amount is the target *total* bet for the round. |
| `deal` | `ok` | Next street; only when the round is complete. |
| `settle` | `showdown yes\|no`, `payout <seat> <amount>` × n, `ok` | Hand must be complete. |
| `log` | event lines, then `end` | Append-only hand history (below). |
| `addbot <seat> <bot-file>` | `ok` | Seats an engine-side bot (see below). |
| `bots` | `bots -` or `bots <seat...>` | Lists automated seats. |
| `step` | `ok <seat> <fold\|check\|call\|raise> [amount]` | The botted acting seat acts. Manual `act` on a botted seat errors. |
| `quit` | `bye` | |

## State block

```
street preflop|flop|turn|river|none|complete
button <seat>
acting <seat|-1>
pot <chips>
current <highest total bet this round>
board -|<cards...>
seat <i> stack <s> bet <b> committed <c> in|out live|folded hole <cards...|-->
... (one line per seat)
end
```

`bet` is committed this round, `committed` this hand. Folded or out-of-hand
seats show `hole --`.

> **Trust note (v0):** bare `state` shows every seated player's hole cards. That is
> correct for local play (several humans sharing one screen, or bots running
> on the same computer) and wrong for play over the internet. `state <seat>`
> is the filtered form remote players get; until then, never expose the bare
> stream to anyone you would not show your cards to.
>
> **Trust note (seeds):** whoever supplies `start <seed>` chooses the deck —
> the shuffle is public and deterministic, so seed knowledge is card
> knowledge. Only the trusted host may pick seeds, from OS entropy, never
> from a player, and no player may see a hand's seed (including the `log`
> line that records it) before that hand is over. The logged seed is the
> audit trail: after the hand anyone can replay the shuffle and verify it.

## Separate bot programs

`cardengine_bot --seat N --bot <file>` is a small program that plays a single
seat. It holds its own typed-in/printed-out conversation with whichever
program started it (the *host*): for every decision, the host forwards that
seat's cards-and-table view plus the legal moves, and the bot prints back one
move. The two strictly take turns; when the input ends the bot exits, and if
anything goes wrong it prints an error and stops with a failure signal
instead of guessing a move. The bot program reads only its own seat's private
cards, so even a tampered-with bot cannot see more than its filtered view
allows. `examples/match.py` is the reference host: one engine plus one bot
program per seat. A future network version would reuse exactly this shape,
with internet connections in place of these local message pipes.

## Game files

Shareable variants, `key = value` lines (`#` comments allowed). `games/`
holds examples. Required key: `format_version = 1`. Optional keys fall back
to standard Hold'em defaults: `name`, `description`, `num_players`,
`starting_stack`, `small_blind`, `big_blind`, `ante`, `hole_cards`,
`board_cards`, `betting = nolimit|limit|potlimit`,
`showdown = holdem|omaha` (exactly 2+3), `max_raises` (limit cap). Unknown keys, bad values,
and rule combinations the engine can't run are rejected with a line number —
a friend's hand-edited file can error, never corrupt a game. Full key
reference (mandatory vs optional, defaults, effects): `docs/CONFIG_FILES.md`.

## Bot files

Tunable personalities, same conventions as game files (`bots/` holds
examples). Parameters, not code: `style = heuristic|random|adaptive|gto`, plus 0..1
sliders `mistake_rate` (decisions replaced by a random legal action),
`aggression` (sizing and thin value), `looseness` (how weak a hand
continues), and integer `seed` for determinism. New *strategies* still need
C++ behind the `Bot` interface; new *personalities* are just files
(`tag`/`lag`/`nit`/`gto`/`adaptive` presets included; full key reference in
`docs/CONFIG_FILES.md`).

Bots live in the session for now (same machine, local trust — they see only
their own hole cards by construction via `SeatView`). In-process bots study
each finished hand through `observe` (public action frequencies only).
Out-of-process runners (`cardengine_bot`, `examples/match.py`) decide from
filtered `state <seat>` views plus `options` (see "Separate bot programs").

## Who names whom

The engine only ever identifies seats by number (`seat 1 folds`,
`payout 2 150`). Names are the host program's job: whoever seats a player
knows what to call them. The convention is:

- A human seat shows the screen name typed at the table.
- A botted seat shows the bot file's `name` (`"Fred"` beats `"bot 1"`).
- A game or bot file with an empty `name` is shown under its filename
  without the extension (`tight`, not `tight.txt`).

So "Fred folds, Sally raises 100" is assembled by the client from the seat
roster it already owns — no engine changes needed, and nothing travels over
the wire that was not already there.

## Event log

`log` dumps the append-only history of every hand this session, one event
per line, terminated by `end`. GUI replay, analysis tools, and bot training
read this instead of scraping state:

```
begin_hand button 0 seed 7 stacks 10000,10000 hole As,Ad|7c,2d
action 0 raise 200 pot 300
street flop Ks Qh Jh
settle showdown yes payouts 0:300 committed 200,100
```

- `begin_hand`: pre-hand stacks, the seed (`-` for from-deck testing deals),
  and dealt hole cards per seat (`|`-separated, positionally).
- `action`: seat, action, and pot after the action.
- `street`: only the newly dealt cards.
- `settle`: payouts as `seat:amount` pairs plus per-seat `committed` totals
  (pot accounting for analysis and learning bots). Never contains hole cards —
  folders' cards appear in `begin_hand` and nowhere else.

Same caution as `state`: this log records everybody's private cards, so keep
it on this computer.

## Tournaments

`tload` switches the session to tournament mode: `start` deals at the
current level's blinds, `settle` also books eliminations/places/prizes and
level progress, `tstatus` reports it. `load` switches back to cash.

```
tournament level 0/4 hands 5/10 blinds 50/100 ante 0 pool 60000
standing 0 stack 0 out place 2 prize 0
standing 1 stack 600 alive place 1 prize 600
ok
```

Tournament files (`tournaments/`) share the game-file conventions. A `game`
key composes a game file (path relative to the tournament file); any game
keys alongside override it. The rest is schedule and money (full key
reference in `docs/CONFIG_FILES.md`):

```
format_version = 1
name = "Friday Freezeout"
game = ../games/holdem-6max.txt
buy_in = 10000
prizes = 50, 30, 20
level = 50, 100, 0, 10      # small, big, ante, hands (repeatable)
```

Places follow bust order (simultaneous busts in seat order); unwon prize
remainder goes to the champion. Manual level advances for GUI clocks are a
library call today (`Tournament::advance_level`); the matching protocol
command arrives when a GUI needs it. Rebuys likewise (`Tournament::rebuy`).

## Example session (real transcript)
```
> help
ok commands: help load tload tstatus start state options act deal settle log addbot bots step quit
> start 7
ok
> options
options seat 3 check no call 100 raise yes min 200 max 10000
> act fold        (×5: seats 3, 4, 5, 0, 1 fold)
ok ...
> state
street preflop
button 0
acting -1
pot 150
current 100
board -
seat 0 stack 10000 bet 0 committed 0 in folded hole --
seat 1 stack 9950 bet 50 committed 50 in folded hole --
seat 2 stack 9900 bet 100 committed 100 in live hole 8s Ac
...
end
> settle
showdown no
payout 2 150
ok
> log
begin_hand button 0 seed 7 stacks 10000,10000,10000,10000,10000,10000 hole 9d,4c|8h,Js|8s,Ac|Jc,Tc|Ad,9h|2h,As
action 3 fold pot 150
...
settle showdown no payouts 2:150 committed 0,50,100,0,0,0
end
> quit
bye
```
