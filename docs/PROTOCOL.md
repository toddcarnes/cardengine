# CardEngine Text Protocol (v0)

UCI-style: the engine is a child process, the UI talks lines on stdin/stdout.
Any language can drive it — see `examples/cli.py` (Python, ~150 lines, no
bindings) for the reference client.

## Framing rules (clients must follow these)

- One command per line, one reply per command.
- Replies are a single line (`ok ...` / `error ...` / `bye`), except:
  - `state` replies with a block terminated by an `end` line.
  - `settle` replies with `showdown` + `payout` lines, then a final `ok`.
- The engine flushes after every reply, so piped clients never deadlock.
  The first line on startup is a banner (`cardengine 0.1.0`), not a reply.
- An `error` reply never changes session state — retry or send `quit`.

## Commands

| Command | Reply | Notes |
|---|---|---|
| `help` | `ok commands: ...` | |
| `load <game-file>` | `ok` | Re-tables with fresh stacks. Path may contain spaces. |
| `start <seed>` | `ok` | Starts a hand; stacks and button carry over between hands. |
| `state` | block, then `end` | Full table dump (below). |
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
board -|<cards...>
seat <i> stack <s> bet <b> committed <c> in|out live|folded hole <cards...|-->
... (one line per seat)
end
```

`bet` is committed this round, `committed` this hand. Folded or out-of-hand
seats show `hole --`.

> **Trust note (v0):** `state` shows every live seat's hole cards. That is
> correct for local play (hotseat UI, bots on the same machine) and wrong
> for networked play. Per-seat filtered views arrive with remote opponents;
> until then, never expose this stream to an untrusted client.

## Game files

Shareable variants, `key = value` lines (`#` comments allowed). `games/`
holds examples. Required key: `format_version = 1`. Optional keys fall back
to standard Hold'em defaults: `name`, `description`, `num_players`,
`starting_stack`, `small_blind`, `big_blind`, `ante`, `hole_cards`,
`board_cards`, `betting = nolimit|limit|potlimit`. Unknown keys, bad values,
and rule combinations the engine can't run are rejected with a line number —
a friend's hand-edited file can error, never corrupt a game.

## Bot files

Tunable personalities, same conventions as game files (`bots/` holds
examples). Parameters, not code: `style = heuristic|random`, plus 0..1
sliders `mistake_rate` (decisions replaced by a random legal action),
`aggression` (sizing and thin value), `looseness` (how weak a hand
continues), and integer `seed` for determinism. New *strategies* still need
C++ behind the `Bot` interface; new *personalities* are just files.

Bots live in the session for now (same machine, local trust — they see only
their own hole cards by construction via `SeatView`). Out-of-process bots
over the protocol wait on per-seat state views (see trust note above).

## Event log

`log` dumps the append-only history of every hand this session, one event
per line, terminated by `end`. GUI replay, analysis tools, and bot training
read this instead of scraping state:

```
begin_hand button 0 seed 7 stacks 10000,10000 hole As,Ad|7c,2d
action 0 raise 200 pot 300
street flop Ks Qh Jh
settle showdown yes payouts 0:300
```

- `begin_hand`: pre-hand stacks, the seed (`-` for from-deck testing deals),
  and dealt hole cards per seat (`|`-separated, positionally).
- `action`: seat, action, and pot after the action.
- `street`: only the newly dealt cards.
- `settle`: payouts as `seat:amount` pairs. Never contains hole cards —
  folders' cards appear in `begin_hand` and nowhere else.

Same trust model as `state`: omniscient, for local eyes only.

## Example session (real transcript)
```
> help
ok commands: help load start state options act deal settle quit
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
> quit
bye
```
