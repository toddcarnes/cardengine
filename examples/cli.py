#!/usr/bin/env python3
"""Shared-screen reference client for CardEngine.

The point of this script is architectural, not luxurious: it proves that ANY
program in ANY language can drive the engine. It starts cardengine, talks
text lines with it (see docs/PROTOCOL.md), and plays poker. No shared
code, no special connections — just typed and printed lines.

Usage:
    python examples/cli.py [--engine PATH] [--game FILE] [--seed N]
                           [--hands N] [--auto] [--bots F1,F2,...]

    --auto plays every human seat with a simple check-or-call stand-in (no
    questions asked), handy for a quick hands-free check that the engine
    works. Without it, every non-botted seat is a human taking turns at
    this screen. --bots seats built-in bots in seats 1..N from the
    given personality files (see bots/).
"""

import argparse
import secrets
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent


def _find_exe(stem):
    """Locate a built executable across generators: Visual Studio puts
    cardengine.exe under build/Release, single-config generators (Make,
    Ninja on macOS/Linux) put cardengine directly under build/."""
    candidates = [
        REPO_ROOT / "build" / "Release" / (stem + ".exe"),
        REPO_ROOT / "build" / (stem + ".exe"),
        REPO_ROOT / "build" / "Release" / stem,
        REPO_ROOT / "build" / stem,
    ]
    for path in candidates:
        if path.is_file():
            return path
    return candidates[0]  # Best guess; Popen reports a clear error.


DEFAULT_ENGINE = _find_exe("cardengine")
DEFAULT_BOT = _find_exe("cardengine_bot")


class Engine:
    def __init__(self, path):
        self.proc = subprocess.Popen(
            [str(path)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
            bufsize=1,  # line-buffered: our commands arrive immediately
        )
        banner = self.proc.stdout.readline().strip()
        if not banner.startswith("cardengine"):
            raise RuntimeError(f"unexpected banner: {banner!r}")
        print(f"engine: {banner}")

    def send(self, command):
        """Send one command, return the reply lines (without `end`).

        Framing follows docs/PROTOCOL.md and routes purely on
        terminators, never on prelude content: `state` and `log`
        reply with blocks terminated by `end`; `settle` and `tstatus`
        reply with prelude lines and a final `ok`; everything else is
        exactly one line. A new prelude line under `settle`/`tstatus`
        (e.g. a future `runout`/`jackpot` line) needs no client
        change — the old code only kept reading while the line was
        literally `showdown`, `payout`, `tournament`, or `standing`,
        so any unlisted prelude was misrouted as a complete reply.
        """
        def read_line():
            line = self.proc.stdout.readline()
            if not line:
                raise RuntimeError("engine closed the pipe")
            return line.rstrip("\n")

        self.proc.stdin.write(command + "\n")
        self.proc.stdin.flush()
        if not command.split():
            # A blank line still gets the engine's single-line reply
            # (`error empty command`); consume it so framing stays aligned.
            return [read_line()]
        first = command.split()[0]

        if first in ("state", "log"):
            lines = []
            while True:
                line = read_line()
                if line == "end":
                    return lines
                # `state <bad-seat>` answers a bare `error` with no
                # `end`; return it instead of waiting forever.
                if not lines and line.startswith("error"):
                    return [line]
                lines.append(line)
        if first in ("settle", "tstatus"):
            lines = []
            while True:
                line = read_line()
                lines.append(line)
                if (line in ("ok", "bye") or line.startswith("ok ")
                        or line.startswith("error")):
                    return lines
        return [read_line()]

    def close(self):
        try:
            self.send("quit")
        finally:
            self.proc.wait(timeout=10)


def parse_state(lines):
    """Fold a `state` block into a dict.

    Keeps `street`, `showdown`, `button`, `acting`, `acting_since`, `pot`,
    `board`, `draws`, `max_draw` (draw games only), `community` (stud
    only), and per-seat `hole` plus `up` cards (stud face-up cards are
    public, like the board) plus the sitting-out `out` marker. Seats
    without an `up` section get `up: []`. (`current` is ignored.)
    """
    state = {"seats": {}, "draws": [], "community": []}
    for line in lines:
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "street":
            state["street"] = parts[1]
        elif parts[0] == "showdown":
            state["showdown"] = parts[1]
        elif parts[0] == "button":
            state["button"] = int(parts[1])
        elif parts[0] == "acting":
            state["acting"] = int(parts[1])
        elif parts[0] == "acting_since":
            state["acting_since"] = int(parts[1])
        elif parts[0] == "pot":
            state["pot"] = int(parts[1])
        elif parts[0] == "board":
            state["board"] = [] if parts[1] == "-" else parts[1:]
        elif parts[0] == "max_draw":
            state["max_draw"] = int(parts[1])
        elif parts[0] == "draws":
            state["draws"] = [] if parts[1] == "-" else [int(s) for s in parts[1:]]
        elif parts[0] == "community":
            state["community"] = [] if parts[1] == "-" else parts[1:]
        elif parts[0] == "seat":
            # seat I stack S bet B committed C in|out live|folded [out] hole ...
            # [up ...] (stud only; `out` marks a sitting-out seat)
            seat = int(parts[1])
            hole_at = parts.index("hole")
            if "up" in parts[hole_at:]:
                up_at = parts.index("up", hole_at)
                hole_toks, up_toks = parts[hole_at + 1:up_at], parts[up_at + 1:]
            else:
                hole_toks, up_toks = parts[hole_at + 1:], []
            state["seats"][seat] = {
                "stack": int(parts[3]),
                "bet": int(parts[5]),
                "committed": int(parts[7]),
                "in": parts[8] == "in",
                "live": parts[9] == "live",
                "sitting_out": "out" in parts[10:hole_at],
                "hole": [] if hole_toks == ["--"] else hole_toks,
                "up": [] if up_toks in ([], ["--"]) else up_toks,
            }
    return state


def parse_options(line):
    """'options seat S check yes|no call N raise yes|no [min M max X]'."""
    parts = line.split()
    get = {parts[i]: parts[i + 1] for i in range(1, len(parts) - 1, 2)}
    opts = {
        "seat": int(get["seat"]),
        "check": get["check"] == "yes",
        "call": int(get["call"]),
        "raise": get["raise"] == "yes",
    }
    if opts["raise"]:
        opts["min"], opts["max"] = int(get["min"]), int(get["max"])
    return opts


def choose_auto(opts):
    if opts["check"]:
        return "check"
    return "call"


def choose_human(seat, hole, opts):
    while True:
        desc = f"seat {seat} {hole} : "
        if opts["check"]:
            desc += "check"
        else:
            desc += f"call {opts['call']}"
        if opts["raise"]:
            desc += f" | raise {opts['min']}-{opts['max']}"
        desc += " | fold > "
        raw = input(desc).strip().split()
        if not raw:
            continue
        if raw[0] in ("check", "call", "fold"):
            return raw[0]
        if raw[0] == "raise" and len(raw) == 2 and opts["raise"]:
            try:
                amount = int(raw[1])
            except ValueError:
                print("amount must be a number")
                continue
            if not opts["min"] <= amount <= opts["max"]:
                print(f"amount must be {opts['min']}..{opts['max']}")
                continue
            return f"raise {amount}"
        print("huh?")


def hand_seed(base, hand):
    """Hand seed: OS entropy by default; reproducible base+hand when the
    operator passes --seed explicitly. The engine's shuffle is public and
    deterministic, so production seeds must never come from a player."""
    if base is None:
        return secrets.randbits(64)
    return base + hand


def play_hand(engine, seed, auto, botted):
    reply = engine.send(f"start {seed}")
    if reply != ["ok"]:
        print(f"start failed: {reply}")
        return False
    while True:
        state = parse_state(engine.send("state"))
        acting = state["acting"]
        if acting == -1:
            if engine.send("deal") == ["ok"]:
                continue
            raw = engine.send("state")
            fresh = parse_state(raw)
            pending = fresh["draws"]
            if pending:
                drawer = pending[0]
                if drawer in botted:
                    print(f"  bot seat {drawer}: {engine.send('step')}")
                    continue
                hole = fresh["seats"][drawer]["hole"]
                cap = fresh.get("max_draw")
                if auto:
                    reply = engine.send("discard")
                else:
                    cap_note = f" (max {cap})" if cap is not None else ""
                    print(f"street=draw pot={state['pot']} "
                          f"seat {drawer} {' '.join(hole)}")
                    raw = input(f"seat {drawer} discards{cap_note} "
                                f"(e.g. `As Td`, empty stands pat) > ").strip()
                    reply = engine.send(f"discard {raw}".strip())
                if reply != ["ok"]:
                    print(f"  engine refused: {reply}")
                    if auto:
                        return False
                continue
            settle = engine.send("settle")
            for line in settle:
                print(f"  {line}")
            return settle[-1] == "ok"
        if acting in botted:
            print(f"  bot seat {acting}: {engine.send('step')}")
            continue
        seat = state["seats"][acting]
        opts = parse_options(engine.send("options")[0])
        if auto:
            action = choose_auto(opts)
        else:
            board = " ".join(state.get("board", [])) or "-"
            prompt = (f"street={state['street']} pot={state['pot']} "
                      f"board={board}")
            if state.get("showdown") == "stud":
                own_up = " ".join(seat["up"]) or "--"
                rivals = " ".join(
                    f"{s}:{' '.join(v['up']) or '--'}"
                    for s, v in sorted(state["seats"].items())
                    if s != acting)
                prompt += f" up={own_up}"
                if rivals:
                    prompt += f" rivals={rivals}"
                if state.get("community"):
                    prompt += f" community={' '.join(state['community'])}"
            print(prompt)
            action = choose_human(acting, seat["hole"], opts)
        reply = engine.send(f"act {action}")
        if reply != ["ok"]:
            print(f"  engine refused: {reply}")
            if auto:
                return False


def main():
    # Line-buffered so piped logs stay live.
    sys.stdout.reconfigure(line_buffering=True)
    parser = argparse.ArgumentParser(description="CardEngine shared-screen client")
    parser.add_argument("--engine", default=str(DEFAULT_ENGINE))
    parser.add_argument("--game", default=None)
    parser.add_argument("--seed", type=int, default=None,
                        help="first hand's seed (reproducible runs only; "
                             "default: OS entropy per hand)")
    parser.add_argument("--hands", type=int, default=1)
    parser.add_argument("--auto", action="store_true")
    parser.add_argument("--bots", default="",
                        help="comma-separated bot files for seats 1..N, "
                             "e.g. --bots bots/tight.txt,bots/random.txt")
    args = parser.parse_args()

    engine = Engine(args.engine)
    try:
        if args.game:
            reply = engine.send(f"load {args.game}")
            print(f"load: {reply}")
            if reply != ["ok"]:
                return 1
        botted = set()
        for i, path in enumerate(p for p in args.bots.split(",") if p):
            seat = i + 1
            reply = engine.send(f"addbot {seat} {path}")
            print(f"addbot {seat}: {reply}")
            if reply != ["ok"]:
                return 1
            botted.add(seat)
        ok = True
        for hand in range(args.hands):
            seed = hand_seed(args.seed, hand)
            print(f"--- hand {hand + 1} (seed {seed}) ---")
            ok = play_hand(engine, seed, args.auto, botted) and ok
    finally:
        engine.close()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
