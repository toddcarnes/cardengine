# CardEngine Text Protocol

How outside programs talk to the engine. The engine runs as its own program
in the background; a user interface (or a bot, or a script) starts it, sends
it one text line per command, and reads back replies. This works the same
from any programming language — see `examples/cli.py` (about 340 lines of
Python, using nothing but typed and printed lines) for the reference example.
(The design follows the same idea as chess engines, which is part of why
this project is called CardEngine.)

## Framing rules (clients must follow these)

- One command per line, one reply per command.
- Most replies are a single line (`ok ...` / `error ...` / `bye`). Four
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
| `tlevel` | `ok` | Tournament only: manual clock advance (sticks at final level). |
| `trebuy <seat>` | `ok` | Tournament only, between hands: top up a live seat or bring back a busted one (adds a buy-in to the pool). Not for sitting-out seats. |
| `tchop <seat:amount> ...` | `ok` | Tournament only, between hands: final-table deal. One pair per surviving seat, amounts summing to the remaining pool exactly; places go by stack (leader first), tournament ends. |
| `sitout <seat>` / `resume <seat>` | `ok` | Flag a seat out (disconnect/away) or bring it back. Applies from the next hand; the running hand is unaffected. |
| `save <file>` | `ok` | Between hands: writes the whole session (stacks, button, sit-outs, tournament books, log) to a session file. |
| `restore <file>` | `ok` | Between hands: reboots this session from a session file (clears bots — reseat them). |
| `start <seed>` | `ok` | Starts a hand; stacks and button carry over between hands. |
| `state` | block, then `end` | Full table dump — local trust only (below). |
| `state <seat>` | block, then `end` | That seat's view: own hole cards shown, all other seats `--`. |
| `options` | `options seat S check yes\|no call N raise yes\|no [min M max X]` | For the acting seat; `error no action pending` otherwise. |
| `act fold\|check\|call` | `ok` | Acts for the current seat. |
| `act raise <amount>` | `ok` | Amount is the target *total* bet for the round. |
| `discard [cards...]` | `ok` | Draw games only: exchanges the named hole cards for replacements (bare `discard` stands pat). Only the pending drawer may move; `draws` names them in turn order. |
| `draws` | `draws -` or `draws <seat...>` | Draw games only: seats still owed the exchange, button-out. Empty outside the draw street. |
| `timeout` | `ok` | Folds the acting seat by the clock (disconnect/stalled client). Logged as `timeout <seat> pot <pot>` — bots read it as a fold. Betting only; the draw exchange has no clock (a stuck drawer blocks `deal`). |
| `deal` | `ok` | Next street; only when the round is complete. |
| `settle` | `showdown yes\|no`, `payout <seat> <amount>` × n, `ok` | Hand must be complete. |
| `log` | event lines, then `end` | Append-only hand history (below). |
| `addbot <seat> <bot-file>` | `ok` | Seats an engine-side bot (see below). |
| `bots` | `bots -` or `bots <seat...>` | Lists automated seats. |
| `step` | `ok <seat> <fold\|check\|call\|raise> [amount]` | The botted acting seat acts. Manual `act` on a botted seat errors. On the draw street steps the pending drawer instead (`ok <seat> discard ...`, `-` for pat). |
| `quit` | `bye` | |

## State block

```
street preflop|draw|flop|turn|river|third|fourth|fifth|sixth|seventh|none|complete
showdown holdem|omaha|omaha_hilo|stud|draw|deuce
button <seat>
acting <seat|-1>
acting_since <seconds|-1>
pot <chips>
current <highest total bet this round>
board -|<cards...>
max_draw <n> (draw games only: most cards a seat may exchange)
draws -|<seats...> (draw street turn order, button-out)
seat <i> stack <s> bet <b> committed <c> in|out live|folded hole <cards...|--> [up <cards...|-->] (stud only)
... (one line per seat)
community -|<card> (stud only: the shared river card when 8-handed play overflows the shoe)
end
```

Draw games run preflop betting, then a `draw` street where each live seat
exchanges in turn (`discard As Td`, bare `discard` stands pat, at most
`max_draw` cards), then betting resumes on the flop slot through the river
(no board is ever dealt). `deal` advances into and out of the draw street;
during it `acting` is `-1`, `options`/`act`/`timeout` are refused, and
`deal` itself waits until `draws` is empty.

Stud games run five streets (`third` through `seventh`): third deals 2
down + 1 up with the low door card's bring-in opening left of it, fourth
through sixth add one up card each with the best visible hand opening,
seventh adds one down card (or a single shared `community` up card when
the shoe runs dry 8-handed). `hole` carries down cards (owner-only), `up`
carries face-up cards (public — every live seat's shown, like the board).
`deal` advances street by street; the log records each round as
`stud <street> <seat:up ...>` — third street's door cards first
(e.g. `stud third 1:2c 0:Ah`), then e.g. `stud fourth 1:Kd 0:Qs` —
per-seat tags, since only live seats are dealt;
`stud seventh community Qh` for the shared river, bare `stud seventh`
for down cards). Older bare-cards lines (`stud fourth Kd Qs`) still
parse.

`acting_since` is the action-clock start (seconds on the engine's monotonic
clock, `-1` when nobody holds the action). A host enforces its own limit —
`expired(now, acting_since, limit)` in `clock.h` — and folds the holder with
`timeout` when the wait runs out. The engine never folds for you.

`bet` is committed this round, `committed` this hand. Folded or out-of-hand
seats show `hole --`. A sitting-out seat carries an `out` marker after
`live`/`folded` (`seat 0 ... live out hole --`); it is dealt nothing and
posts nothing until resumed.

A host parks a disconnected player with `sitout`, keeps dealing to everyone
else, and seats them back with `resume` — no rebuy, no lost stack, no
special-casing in the deal loop. Bots on a sitting-out seat simply never
become `acting`; `step` on them reports no action pending.

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
program per seat.

## Network hosts

`examples/host.py` is the same shape with TCP sockets instead of local
pipes: one engine process plus one seat per connection. The wire protocol is
line-based, like the engine's own:

- Client connects and sends `hello <seat>` (or bare `hello` for the first
  free seat); the host replies `welcome <seat> <name>`.
- Per decision the host sends that seat's `state <seat>` block (`end`
  terminated) plus one `options` line; the client replies one `act ...`
  line. Clients send nothing else except `quit` to leave.
- `examples/bot_proxy.py` bridges a `cardengine_bot` program onto a seat,
  so bots and humans share one host without special cases.

The host owns everything the engine never will: the seat roster (names live
host-side; the engine only numbers seats), the action clock (`--action-seconds`,
enforced with `timeout` when the wait runs out — the engine stamps, the host
folds), disconnects (`sitout` parks a dead connection, `resume` reseats a
live one — see below for the mid-hand rule), and crash recovery (`--save`
writes a session file after every hand; a rebooted host `restore`s it).
Filtered views are the security boundary: the host forwards `state <seat>`,
never bare `state`, so a tampered client sees exactly its own hole cards.

A dead connection mid-decision folds by the clock (`timeout`); a dead
connection between decisions parks with `sitout` and the next hand deals
around it. A seat that reconnects (`hello <seat>`) resumes with `resume` —
stack intact, no rebuy, no lost chips. `sitout` mid-hand is legal but waits:
the running hand is unaffected, and the flag bites from the next deal.

## Game files

Shareable variants, `key = value` lines (`#` comments allowed). `games/`
holds examples. Required key: `format_version = 1`. Optional keys fall back
to standard Hold'em defaults: `name`, `description`, `num_players`,
`starting_stack`, `small_blind`, `big_blind`, `ante`, `hole_cards`,
`board_cards`, `betting = nolimit|limit|potlimit`,
`showdown = holdem|omaha` (exactly 2+3)`|omaha_hilo` (high/low split, 8-or-better)`|draw` (5 cards + one exchange, best five wins)`|deuce` (same deal,
2-7 lowball: worst hand wins, straights and flushes count against)`|stud` (7 cards street-by-street, 4 up, best five of 7 wins), `max_draw` (draw cap 1–5), `max_raises` (limit cap). Unknown keys, bad values,
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
On the draw street the runner takes a `draws` line instead and answers
`discard ...` (bare `discard` stands pat).

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
- `street`: only the newly dealt cards (draw games log an empty `street draw` line for the exchange, then betting resumes on `street flop` with no board cards; stud games log each round as `stud <street> <seat:up ...>` — third street's door cards included — with `stud seventh community Qh` for the shared river and bare `stud seventh` for down cards; pre-tag bare-cards lines still parse).
- `draw`: one seat's exchange as counts, never cards (`draw 1 drew 3`, `draw 0 drew 0` for pat) — discards stay private like folded hands.
- `settle`: payouts as `seat:amount` pairs plus per-seat `committed` totals
  (pot accounting for analysis and learning bots). Never contains hole cards —
  folders' cards appear in `begin_hand` and nowhere else. Run-it-twice hands
  append `boards N` (1 is classic and omitted); the spare boards appear as
  `runout <board> <cards...>` lines (`runout 2 Kc Qd Jc 8s 3c`), board 1
  being the felt.
- `timeout`: a clock fold of a seat, with the pot after (`timeout 3 pot 420`).
  Bots read it as a fold by that seat.

Same caution as `state`: this log records everybody's private cards, so keep
it on this computer.

## Saving and restoring

A crashed host loses the tournament — unless it saved. `save <file>`
(between hands only) writes the whole session: game rules, stacks, button,
sit-out flags, tournament books (levels, pool, busts, places), and the raw
log lines. `restore <file>` reboots this session from that file: same stacks,
same button, same books, same log, ready to `start` the next hand. Seated
bots are deliberately not saved — `addbot` them again after a restore
(their adaptive reads rebuild from the restored log as new hands are
observed). Session files are plain `key = value` text like game files
(full key reference in `docs/CONFIG_FILES.md`); a hand-edited file that
fails validation errors with a line number and changes nothing.
A pending full kill rides along: `save` writes it as `kill_pending`
(format version 2), and the restored session deals double blinds next hand.

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
level = 100, 200, 25, 4, 20 # ...or add minutes: 4 hands or 20 minutes,
                            # whichever hits first (0 = hands only)
```

Timed levels count wall time per completed hand and fire at the next deal
(never mid-hand) — a slow final table still blinds up on schedule.
`tstatus` reports the clock on timed levels (`elapsed 61/1200 left 1139`);
`save` banks it as `level_elapsed` so a reboot keeps the schedule.

Places follow bust order (simultaneous busts in seat order); unwon prize
remainder goes to the champion. Manual clock advances go over the wire
(`tlevel`), as do rebuys (`trebuy <seat>`, between hands only — the library
calls `Tournament::advance_level` / `Tournament::rebuy` underneath). A
final table may end by agreement instead of cards: `tchop` splits the
remaining pool exactly as listed (one `seat:amount` pair per survivor),
awards places by stack with the leader first (earlier busts slide below in
bust order), and closes the tournament — `tstatus` shows the deal shares,
further `start` hands are refused.

## Example session (real transcript)
```
> help
ok commands: help load tload tstatus tlevel trebuy tchop sitout resume save restore start state options act discard draws timeout deal settle log addbot bots step quit
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
