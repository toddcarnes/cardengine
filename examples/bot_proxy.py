#!/usr/bin/env python3
"""TCP adapter: bridges one cardengine_bot process to a host.py seat.

Speaks host.py's client side (hello, filtered state blocks, act lines)
on the socket and the runner's strict alternation (state block + options
in, one act line out) on the pipe. Stdlib only.

Usage:
    python examples/bot_proxy.py --port 18447 --seat 1 --bot bots/tight.txt
                                 [--bot-exe PATH]
"""

import argparse
import socket
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cli import DEFAULT_BOT  # noqa: E402


def read_block(sock, buf=b""):
    """Read lines until `end`; return (lines-without-end, leftover bytes)."""
    lines = []
    while True:
        while b"\n" not in buf:
            chunk = sock.recv(4096)
            if not chunk:
                raise RuntimeError("host closed the connection")
            buf += chunk
        raw, buf = buf.split(b"\n", 1)
        line = raw.decode().strip()
        if line == "end":
            return lines, buf
        lines.append(line)


def main():
    parser = argparse.ArgumentParser(description="CardEngine bot TCP proxy")
    parser.add_argument("--port", type=int, default=18447)
    parser.add_argument("--seat", type=int, required=True)
    parser.add_argument("--bot", required=True)
    parser.add_argument("--bot-exe", default=str(DEFAULT_BOT))
    args = parser.parse_args()

    bot = subprocess.Popen(
        [str(args.bot_exe), "--seat", str(args.seat), "--bot", args.bot],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        text=True,
        bufsize=1,
    )
    sock = socket.create_connection(("127.0.0.1", args.port))
    sock.sendall(f"hello {args.seat}\n".encode())
    try:
        buf = b""
        # The welcome line arrives before the first state block.
        while b"\n" not in buf:
            chunk = sock.recv(4096)
            if not chunk:
                raise RuntimeError("host closed the connection")
            buf += chunk
        while True:
            state, buf = read_block(sock, buf)
            # The options line is the last line before the next block starts.
            while not state or not state[-1].startswith("options"):
                more, buf = read_block(sock, buf)
                state += more
            options = state.pop()
            bot.stdin.write("\n".join(state) + "\nend\n")
            bot.stdin.write(options + "\n")
            bot.stdin.flush()
            reply = bot.stdout.readline()
            if not reply:
                raise RuntimeError("bot closed the pipe")
            sock.sendall((reply.strip() + "\n").encode())
    finally:
        try:
            bot.stdin.close()
        except Exception:
            pass
        bot.wait(timeout=10)
        sock.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
