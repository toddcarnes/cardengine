#!/usr/bin/env python3
"""Network host reference for CardEngine (Wave 3, item 8).

Same shape as match.py, with TCP sockets in place of local pipes: one
engine process plus one seat per connection. Each player — human client
or cardengine_bot behind a TCP adapter — gets only their own filtered
`state <seat>` view; the engine filters before the words leave, so even a
tampered client cannot see more than its seat allows.

What this host owns (the engine never will):
  - seats by number, names by roster (`--roster NAME,NAME,...` or
    `seat:botfile` specs like match.py; unlisted seats are humans);
  - the action clock (`--action-seconds`: folds the holder with `timeout`
    when the wait runs out) and timed blind levels (the engine advances
    them; the host just reports `tstatus` countdowns);
  - disconnects (`sitout` parks the seat, `resume` reseats it; a seated
    bot keeps playing a parked seat only if --bots-stay-seated);
  - crash recovery (`--save FILE`: `save` after every settled hand, so a
    rebooted host + `restore` resumes the tournament).

Wire protocol (line-based, like the engine's own):
  - server -> client: `welcome <seat> <name>` then, per decision, a
    `state <seat>` block (terminated by `end`) plus one `options` line;
  - client -> server: one `act ...` line per betting decision (or one
    `discard ...` line per draw exchange, or `quit` to leave).
  Out-of-process `cardengine_bot` programs speak the same two halves
  already (state block + options in, act line out), so a tiny adapter —
  `examples/bot_proxy.py --port ... --seat N --bot FILE` — bridges them.

Usage:
    python examples/host.py --port 18447 --roster Alice,Bob
                            [--game FILE | --tournament FILE]
                            [--hands N] [--seed N] [--auto]
                            [--bots 1:bots/tight.txt] [--bot-exe PATH]
                            [--action-seconds 30] [--save FILE]
                            [--bots-stay-seated]

Stdlib only (socket, subprocess, argparse). Single-threaded: select()
multiplexes engine + clients; bots behind proxies are processes.
"""

import argparse
import secrets
import select
import socket
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cli import DEFAULT_BOT, DEFAULT_ENGINE, Engine, hand_seed  # noqa: E402
from cli import parse_options, parse_state  # noqa: E402


def now_seconds():
    return int(time.monotonic())


class Client:
    """One TCP player: buffered lines in, flushed lines out."""

    def __init__(self, conn, seat, name):
        self.conn = conn
        self.seat = seat
        self.name = name
        self.inbuf = b""
        self.outbuf = b""
        self.live = True

    def send(self, text):
        self.outbuf += (text + "\n").encode()

    def lines(self):
        while b"\n" in self.inbuf:
            raw, self.inbuf = self.inbuf.split(b"\n", 1)
            yield raw.decode().strip()


def roster_names(specs, count):
    """Seat -> display name. `seat:botfile` specs name bots by file stem;
    --roster names humans in seat order; unknowns are 'seat N'."""
    names = {}
    for spec in specs:
        seat_text, _, path = spec.partition(":")
        try:
            seat = int(seat_text)
        except ValueError:
            continue
        names[seat] = Path(path).stem
    return names


class Host:
    def __init__(self, args):
        self.args = args
        self._hello_buf = {}
        self.engine = Engine(args.engine)
        if args.tournament:
            self._must_ok(f"tload {args.tournament}")
            self.tournament = True
        elif args.game:
            self._must_ok(f"load {args.game}")
            self.tournament = False
        else:
            self.tournament = False
        seats = self._seat_count()
        humans = (args.roster.split(",") if args.roster else [])
        self.names = {}
        for seat in range(seats):
            self.names[seat] = (
                humans[seat] if seat < len(humans) and humans[seat] else f"seat {seat}"
            )
        self.runners = {}  # seat -> Popen(cardengine_bot), proxied seats
        self.proxies = {}  # seat -> Popen(bot_proxy.py), TCP bridges
        for spec in args.bots:
            seat_text, _, path = spec.partition(":")
            seat = int(seat_text)
            self.names[seat] = Path(path).stem
            self.runners[seat] = subprocess.Popen(
                [str(args.bot_exe), "--seat", str(seat), "--bot", str(path)],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                text=True,
                bufsize=1,
            )
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("127.0.0.1", args.port))
        self.sock.listen(seats)
        self.sock.setblocking(False)
        self.clients = {}  # seat -> Client (humans over TCP)
        self.pending = []  # unseated connections awaiting hello
        self.start_refusal = []  # engine reply when the last start failed
        print(f"host: listening on 127.0.0.1:{args.port} "
              f"({seats} seats, action clock {args.action_seconds}s)")

    def _must_ok(self, command):
        reply = self.engine.send(command)
        if reply != ["ok"]:
            raise RuntimeError(f"{command}: {reply}")
        return reply

    def _seat_count(self):
        state = parse_state(self.engine.send("state"))
        return len(state["seats"])

    def close(self):
        for proc in list(self.runners.values()) + list(self.proxies.values()):
            try:
                proc.stdin.close()
            except Exception:
                pass
            proc.wait(timeout=10)
        try:
            self.engine.close()
        finally:
            self.sock.close()

    def _seat_of(self, conn):
        for client in self.clients.values():
            if client.conn is conn:
                return client.seat
        return None

    def _broadcast_state(self):
        full = self.engine.send("state")
        acting = parse_state(full)["acting"]
        return full, acting

    def _bot_move(self, seat, options_line):
        proc = self.runners[seat]
        for line in self.engine.send(f"state {seat}"):
            proc.stdin.write(line + "\n")
        proc.stdin.write("end\n" + options_line + "\n")
        proc.stdin.flush()
        return proc.stdout.readline().strip()

    def _bot_discard(self, seat, draws_line):
        proc = self.runners[seat]
        for line in self.engine.send(f"state {seat}"):
            proc.stdin.write(line + "\n")
        proc.stdin.write("end\n" + draws_line + "\n")
        proc.stdin.flush()
        return proc.stdout.readline().strip()

    def _await_human(self, seat, decision_line, deadline):
        """Pump sockets until this seat answers or the clock runs out."""
        client = self.clients.get(seat)
        if client is None:
            return None  # No connection: caller parks or auto-plays.
        # Skip seats whose connection already died (pump reaps them).
        self._pump(client.conn)
        client = self.clients.get(seat)
        if client is None:
            return None
        for line in self.engine.send(f"state {seat}"):
            client.send(line)
        client.send("end")
        client.send(decision_line)
        self._flush()
        # Betting answers `act ...`; the draw exchange answers `discard ...`.
        want = "discard" if decision_line.startswith("draws") else "act"
        while True:
            remaining = deadline - now_seconds()
            if remaining <= 0:
                return None
            readable, _, _ = select.select(
                [self.sock] + [c.conn for c in self.clients.values()],
                [], [], remaining)
            for conn in readable:
                if conn is self.sock:
                    self._accept()
                    continue
                self._pump(conn)
            self._flush()
            client = self.clients.get(seat)
            if client is None:
                return None  # Left mid-decision: caller times out.
            # Drain this seat's answer lines (`act ...` / `discard ...`).
            answer = None
            leftovers = []
            for line in client.lines():
                if line == "quit":
                    self._drop(seat, "quit")
                    return None
                if line.startswith(want) and answer is None:
                    answer = line
                else:
                    leftovers.append(line)
            for line in leftovers:
                client.send(f"error expected '{want} ...', got {line!r}")
            self._flush()
            if answer is not None:
                return answer

    def _accept(self):
        conn, _ = self.sock.accept()
        conn.setblocking(False)
        # Seat by hello: `hello <seat>`; default: first free human seat.
        self.pending.append(conn)

    def _pump(self, conn):
        try:
            chunk = conn.recv(4096)
        except BlockingIOError:
            return
        if not chunk:
            seat = self._seat_of(conn)
            if seat is not None:
                self._drop(seat, "closed")
            elif conn in self.pending:
                self.pending.remove(conn)
                conn.close()
                self._hello_buf.pop(conn, None)
            return
        seat = self._seat_of(conn)
        if seat is not None:
            self.clients[seat].inbuf += chunk
            return
        # Unseated: buffer until `hello <seat>` claims the connection.
        self._hello_buf.setdefault(conn, b"")
        self._hello_buf[conn] += chunk
        while self._hello_buf.get(conn, b"").find(b"\n") >= 0:
            raw, self._hello_buf[conn] = self._hello_buf[conn].split(b"\n", 1)
            line = raw.decode().strip()
            if not line.startswith("hello"):
                continue
            parts = line.split()
            seat = int(parts[1]) if len(parts) > 1 else self._free_seat()
            name = self.names.get(seat, f"seat {seat}")
            self.clients[seat] = Client(conn, seat, name)
            if conn in self.pending:
                self.pending.remove(conn)
            self._hello_buf.pop(conn, None)
            self.clients[seat].send(f"welcome {seat} {name}")
            print(f"host: {name} seated at {seat}")

    def _free_seat(self):
        taken = set(self.clients) | set(self.runners)
        seats = self._seat_count()
        for seat in range(seats):
            if seat not in taken:
                return seat
        return seats - 1

    def _drop(self, seat, why):
        client = self.clients.pop(seat, None)
        if client is None:
            return
        print(f"host: {client.name} (seat {seat}) left ({why}); parking")
        try:
            client.conn.close()
        except Exception:
            pass
        if self.engine.send(f"sitout {seat}") != ["ok"]:
            print(f"host: WARNING: sitout {seat} refused")
        if seat in self.runners and not self.args.bots_stay_seated:
            print(f"host: WARNING: botted seat {seat} parked; bot idles")

    def _flush(self, timeout=0.0):
        conns = [c.conn for c in self.clients.values()]
        if not conns:
            return
        _, writable, _ = select.select([], conns, [], timeout)
        for conn in writable:
            seat = self._seat_of(conn)
            client = self.clients.get(seat)
            if client and client.outbuf:
                try:
                    sent = conn.send(client.outbuf)
                    client.outbuf = client.outbuf[sent:]
                except BlockingIOError:
                    pass

    def play_hand(self, seed):
        # Drain the listen queue before dealing: players connect any time,
        # and the first hand should seat whoever is already waiting.
        self._drain_listen(timeout=2.0)
        reply = self.engine.send(f"start {seed}")
        if reply != ["ok"]:
            self.start_refusal = reply
            status = self.engine.send("tstatus") if self.tournament else reply
            print(f"host: start refused: {status}")
            return False
        self.start_refusal = []
        while True:
            # Seat newcomers every decision (humans connect any time).
            self._drain_listen(timeout=0.0)
            full, acting = self._broadcast_state()
            self._flush()
            if acting == -1:
                if self.engine.send("deal") == ["ok"]:
                    continue
                pending = [line for line in full if line.startswith("draws")]
                if pending and pending[0] != "draws -":
                    drawer = int(pending[0].split()[1])
                    move = None
                    if drawer in self.runners and drawer not in self.clients:
                        move = self._bot_discard(drawer, pending[0])
                        print(f"  bot seat {drawer}: {move}")
                    elif drawer in self.clients:
                        cap = next(
                            (line.split()[1] for line in full
                             if line.startswith("max_draw")),
                            None,
                        )
                        cap_note = f" (max {cap})" if cap is not None else ""
                        print(f"  seat {drawer} must discard{cap_note} "
                              f"(e.g. `discard As Td`, bare `discard` stands pat)")
                        deadline = now_seconds() + self.args.action_seconds
                        move = self._await_human(drawer, pending[0], deadline)
                        if move is None or not move.startswith("discard"):
                            print(f"  seat {drawer} timed out; standing pat")
                            self.engine.send(f"discard")
                            continue
                        print(f"  {self.names[drawer]}: {move}")
                    elif self.args.auto:
                        move = "discard"
                    else:
                        print(f"  seat {drawer} ({self.names[drawer]}) has no "
                              f"connection; standing pat")
                        self.engine.send("discard")
                        continue
                    reply = self.engine.send(move)
                    if reply != ["ok"]:
                        print(f"  engine refused: {reply}")
                        target = self.clients.get(drawer)
                        if target is not None:
                            target.send(f"error {reply[0]}")
                        return False
                    continue
                settle = self.engine.send("settle")
                for line in settle:
                    print(f"  {line}")
                if self.args.save:
                    saved = self.engine.send(f"save {self.args.save}")
                    if saved != ["ok"]:
                        print(f"host: WARNING: save failed: {saved}")
                # Parked seats with live connections resume next hand.
                for seat in list(self.clients):
                    if self.engine.send(f"resume {seat}") != ["ok"]:
                        pass
                return settle[-1] == "ok"
            options_line = self.engine.send("options")[0]
            opts = parse_options(options_line)
            move = None
            if acting in self.runners and acting not in self.clients:
                move = self._bot_move(acting, options_line)
                print(f"  bot seat {acting}: {move}")
            elif acting in self.clients:
                deadline = now_seconds() + self.args.action_seconds
                move = self._await_human(acting, options_line, deadline)
                if move is None:
                    print(f"  seat {acting} timed out; folding by the clock")
                    self.engine.send("timeout")
                    continue
                print(f"  {self.names[acting]}: {move}")
            elif self.args.auto:
                move = f"act {'check' if opts['check'] else 'call'}"
            else:
                print(f"  seat {acting} ({self.names[acting]}) has no connection; parking")
                self.engine.send(f"sitout {acting}")
                # Re-deal around them next hand; this hand plays on without
                # them only if they were never dealt — a mid-hand sitout
                # waits, so fold by the clock instead.
                self.engine.send("timeout")
                continue
            reply = self.engine.send(move)
            if reply != ["ok"]:
                print(f"  engine refused: {reply}")
                target = self.clients.get(acting)
                if target is not None:
                    target.send(f"error {reply[0]}")
                return False

    def _drain_listen(self, timeout=0.0):
        """Accept waiting connections and pump hello bytes (nonblocking)."""
        readable, _, _ = select.select([self.sock], [], [], timeout)
        if readable:
            self._accept()
        for conn in list(self.pending):
            self._pump(conn)

    def run(self):
        try:
            for hand in range(self.args.hands):
                # Seat newcomers before each hand (nonblocking).
                self._drain_listen()
                self._flush()
                seed = hand_seed(self.args.seed, hand)
                print(f"--- hand {hand + 1} (seed {seed}) ---")
                if self.tournament:
                    for line in self.engine.send("tstatus"):
                        if line.startswith("tournament"):
                            print(f"  {line}")
                if not self.play_hand(seed):
                    if self.tournament:
                        alive = sum(
                            1 for line in self.engine.send("tstatus")
                            if " alive " in line)
                        if alive <= 1:
                            print("host: champion crowned")
                            break
                    elif any("need at least 2 players" in line
                             for line in self.start_refusal):
                        print("host: game over, one player holds all the chips")
                        if self.args.save:
                            self.engine.send(f"save {self.args.save}")
                        break
                    return 1
        finally:
            pass
        return 0


def main():
    sys.stdout.reconfigure(line_buffering=True)
    parser = argparse.ArgumentParser(description="CardEngine network host")
    parser.add_argument("--engine", default=str(DEFAULT_ENGINE))
    parser.add_argument("--bot-exe", default=str(DEFAULT_BOT))
    parser.add_argument("--port", type=int, default=18447)
    parser.add_argument("--game", default=None)
    parser.add_argument("--tournament", default=None)
    parser.add_argument("--roster", default="",
                        help="comma-separated human names in seat order")
    parser.add_argument("--seed", type=int, default=None,
                        help="first hand's seed (default: OS entropy)")
    parser.add_argument("--hands", type=int, default=1)
    parser.add_argument("--auto", action="store_true",
                        help="check-or-call policy for connectionless seats")
    parser.add_argument("--bots", action="append", default=[],
                        help="seat:botfile, repeatable (engine-side bots)")
    parser.add_argument("--action-seconds", type=int, default=30)
    parser.add_argument("--save", default=None,
                        help="session file: `save` after every hand")
    parser.add_argument("--bots-stay-seated", action="store_true",
                        help="parked botted seats keep their bot (default: bot idles)")
    args = parser.parse_args()

    host = Host(args)
    try:
        code = host.run()
    finally:
        host.close()
    return code


if __name__ == "__main__":
    sys.exit(main())
