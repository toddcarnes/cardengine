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

        Framing follows docs/PROTOCOL.md: `state` and `log` reply with
        blocks terminated by `end`; `settle` and `tstatus` reply with
        prelude lines and a final `ok`; everything else is one line.
        """
        self.proc.stdin.write(command + "\n")
        self.proc.stdin.flush()
        lines = []
        first = command.split()[0]
        block = first in ("state", "log")
        while True:
            line = self.proc.stdout.readline()
            if not line:
                raise RuntimeError("engine closed the pipe")
            line = line.rstrip("\n")
            if block:
                if line == "end":
                    return lines
                lines.append(line)
                continue
            lines.append(line)
            if line in ("ok", "bye") or line.startswith("error"):
                return lines
            if line.split()[0] in ("showdown", "payout", "tournament",
                                   "standing"):
                continue
            return lines

    def close(self):
        try:
            self.send("quit")
        finally:
            self.proc.wait(timeout=10)


def parse_state(lines):
    """Fold a `state` block into a dict."""
    state = {"seats": {}}
    for line in lines:
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "street":
            state["street"] = parts[1]
        elif parts[0] == "acting":
            state["acting"] = int(parts[1])
        elif parts[0] == "pot":
            state["pot"] = int(parts[1])
        elif parts[0] == "board":
            state["board"] = [] if parts[1] == "-" else parts[1:]
        elif parts[0] == "seat":
            # seat I stack S bet B committed C in|out live|folded hole ...
            seat = int(parts[1])
            state["seats"][seat] = {
                "stack": int(parts[3]),
                "bet": int(parts[5]),
                "committed": int(parts[7]),
                "in": parts[8] == "in",
                "live": parts[9] == "live",
                "hole": [] if parts[11] == "--" else parts[11:],
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
            print(f"street={state['street']} pot={state['pot']} "
                  f"board={' '.join(state['board'])}")
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
    parser.add_argument("--seed", type=int, default=1)
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
            print(f"--- hand {hand + 1} (seed {args.seed + hand}) ---")
            ok = play_hand(engine, args.seed + hand, args.auto, botted) and ok
    finally:
        engine.close()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
