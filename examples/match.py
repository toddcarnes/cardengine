#!/usr/bin/env python3
"""Match driver: one engine, one cardengine_bot process per botted seat.

This is the listen-server shape in miniature. The driver (standing in for
the future host GUI) relays filtered `state <seat>` views to bot processes
and their `act ...` lines back to the engine. Bots never see another seat's
cards — the engine filters before the bytes leave. Replace these pipes with
sockets and this file becomes the network gateway.

Usage:
    python examples/match.py --bots 1:bots/tight.txt --bots 2:bots/random.txt
                             [--game FILE] [--hands N] [--seed N] [--auto]
"""

import argparse
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cli import Engine, parse_options, parse_state  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_ENGINE = REPO_ROOT / "build" / "Release" / "cardengine.exe"
DEFAULT_BOT = REPO_ROOT / "build" / "Release" / "cardengine_bot.exe"


class Runner:
    """One persistent cardengine_bot process. Strict alternation: we write
    a state block + options line, it prints one act line."""

    def __init__(self, bot_exe, seat, bot_file):
        self.seat = seat
        self.proc = subprocess.Popen(
            [str(bot_exe), "--seat", str(seat), "--bot", str(bot_file)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
            bufsize=1,
        )

    def decide(self, state_lines, options_line):
        self.proc.stdin.write("\n".join(state_lines) + "\nend\n")
        self.proc.stdin.write(options_line + "\n")
        self.proc.stdin.flush()
        reply = self.proc.stdout.readline()
        if not reply:
            raise RuntimeError(f"bot seat {self.seat} closed the pipe")
        return reply.strip()

    def close(self):
        self.proc.stdin.close()
        self.proc.wait(timeout=10)


def play_hand(engine, runners, seed, auto):
    if engine.send(f"start {seed}") != ["ok"]:
        return False
    while True:
        full = engine.send("state")
        acting = parse_state(full)["acting"]
        if acting == -1:
            if engine.send("deal") == ["ok"]:
                continue
            settle = engine.send("settle")
            for line in settle:
                print(f"  {line}")
            if settle[-1] != "ok":
                return False
            print(f"  log events: {len(engine.send('log')) - 1}")
            return True
        options = engine.send("options")[0]
        if acting in runners:
            action = runners[acting].decide(
                engine.send(f"state {acting}"), options)
            print(f"  bot seat {acting}: {action}")
            reply = engine.send(action)
        elif auto:
            opts = parse_options(options)
            reply = engine.send("act check" if opts["check"] else "act call")
        else:
            print(f"  manual seat {acting} needs --auto in match.py")
            return False
        if reply != ["ok"]:
            print(f"  engine refused: {reply}")
            return False


def main():
    sys.stdout.reconfigure(line_buffering=True)
    parser = argparse.ArgumentParser(description="CardEngine match driver")
    parser.add_argument("--engine", default=str(DEFAULT_ENGINE))
    parser.add_argument("--bot-exe", default=str(DEFAULT_BOT))
    parser.add_argument("--game", default=None)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--hands", type=int, default=1)
    parser.add_argument("--auto", action="store_true",
                        help="check-or-call policy for non-botted seats")
    parser.add_argument("--bots", action="append", default=[],
                        help="seat:botfile, repeatable")
    args = parser.parse_args()

    runners = {}
    for spec in args.bots:
        seat_text, _, path = spec.partition(":")
        runners[int(seat_text)] = (args.bot_exe, Path(path))

    engine = Engine(args.engine)
    try:
        if args.game:
            if engine.send(f"load {args.game}") != ["ok"]:
                return 1
        procs = {s: Runner(exe, s, f) for s, (exe, f) in runners.items()}
        try:
            ok = True
            for hand in range(args.hands):
                print(f"--- hand {hand + 1} (seed {args.seed + hand}) ---")
                ok = play_hand(engine, procs, args.seed + hand,
                               args.auto) and ok
        finally:
            for runner in procs.values():
                runner.close()
    finally:
        engine.close()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
