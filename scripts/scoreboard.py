#!/usr/bin/env python3
"""100K+ scoreboard campaign for CardEngine.

Runs every game variant in scripts/variants.txt with every bot in
scripts/bots.txt through cardengine_stress, then rates each variant board
plus an equal-weight overall board into ratings/.

Adding a variant or bot is one new line in the matching list file --
nothing here is hardcoded. Budgets are computed, not configured: each
variant plays enough tables that every bot averages APPEARANCE_TARGET
(100k) seat appearances, i.e. tables = 100k * bots / seats. That is why
heads-up needs ~3x the tables of 6-max and stud-8max needs fewer.

Usage:
    python scripts/scoreboard.py [--tables-cap N] [--depth-cap D]
                                 [--rerate] [--resume] [--workers W]
                                 [variant ...]

Outputs: ratings/<variant>.json + ratings/overall.json (the tree).
Everything regenerable (championships, CSVs, logs) goes to .scratch/.
Positional args filter to named variants. --rerate skips the stress runs
and re-fits from the CSVs already in .scratch/. --resume reuses finished
CSVs and plays only missing seeds (default wipes the variant's CSVs and
starts fresh). Stdlib only.
"""
import argparse
import csv
import glob
import json
import math
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
from pathlib import Path

# Windows: each cardengine_stress worker is a console exe, which pops a
# visible terminal per run unless suppressed. 20 workers x 140 runs =
# a desktop full of flashing windows. Never remove this flag.
NO_WINDOW = 0x08000000 if os.name == "nt" else 0

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "examples"))
from cli import _find_exe  # noqa: E402
from rate import BASE_RATING, fit_ratings, ordered_names, pair_key  # noqa: E402

REPO = Path(__file__).resolve().parent.parent
SCRATCH = REPO / ".scratch"
RATINGS = REPO / "ratings"
APPEARANCE_TARGET = 100000
BUY_IN = 10000


def read_list(path):
    """Non-blank, non-# lines split on whitespace."""
    rows = []
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            rows.append(line.split())
    return rows


def game_value(game_rel, key):
    for line in (REPO / game_rel).read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if line.startswith(key + "=") or line.startswith(key + " "):
            _, _, value = line.partition("=")
            return value.strip()
    raise KeyError(f"{game_rel}: no {key}")


def tables_per_run(seats, depth):
    return (seats ** (depth + 1) - 1) // (seats - 1)


def write_championship(path, name, game_rel, depth, seats):
    prizes = "70, 30" if seats == 2 else "50, 30, 20"
    path.write_text(
        "# scoreboard.py generated -- do not edit, do not commit\n"
        "format_version = 1\n"
        f'name = "{name} scoreboard"\n'
        f"game = ../{game_rel}\n"
        f"depth = {depth}\n"
        f"buy_in = {BUY_IN}\n"
        f"prizes = {prizes}\n"
        "champion_prize = 100000\n",
        encoding="utf-8",
    )


def count_rows(csv_path):
    with open(csv_path, newline="", encoding="utf-8") as handle:
        return sum(1 for _ in csv.DictReader(handle))


def run_stress(stress, champ, bots, seed, out_csv):
    # Generous --max-hands: the default 100k cap truncates exactly the big
    # brackets this script runs (a 1555-table 6-max bracket plays ~150k
    # hands; limit plays millions). Brackets must complete -- partial CSVs
    # bias toward early-stage tables. 1B is effectively uncapped.
    cmd = [str(stress), "--championship", str(champ),
           "--bots", ",".join(bots), "--seed", str(seed),
           "--yes", "--max-hands", "1000000000", "--out", str(out_csv)]
    proc = subprocess.run(cmd, capture_output=True, text=True,
                            creationflags=NO_WINDOW)
    if proc.returncode != 0:
        tail = (proc.stdout + proc.stderr)[-2000:]
        print(f"  seed {seed} FAILED:\n{tail}", file=sys.stderr)
        return 0
    return count_rows(out_csv)


def play_variant(stress, name, game, depth, target, bots, workers,
                 seed_base, rerate, resume=False):
    prefix = f"{name}-"
    champ = SCRATCH / f"{name}-champ.txt"
    seats = int(game_value(game, "num_players"))
    per_run = tables_per_run(seats, depth)
    write_championship(champ, name, game, depth, seats)

    done = 0
    seeds_done = set()
    if not resume and not rerate:
        # Fresh campaign: wipe this variant's CSVs so truncated reruns
        # never mix with clean data.
        for stale in glob.glob(str(SCRATCH / f"{prefix}*.csv")):
            Path(stale).unlink()
    elif resume:
        for path in glob.glob(str(SCRATCH / f"{prefix}*.csv")):
            stem = Path(path).stem.rsplit("-", 1)[1]
            if not stem.isdigit():
                continue
            with open(path, newline="", encoding="utf-8") as handle:
                if any(True for _ in csv.DictReader(handle)):
                    seeds_done.add(int(stem))
    nxt = seed_base
    if not rerate:
        round_no = 0
        expected_per_run = per_run
        while done < target and round_no < 8:
            runs = math.ceil((target - done) / expected_per_run)
            round_no += 1
            print(f"[{name}] round {round_no}: {runs} runs "
                  f"({done}/{target} tables)", flush=True)
            args = []
            while len(args) < runs:
                if nxt not in seeds_done:
                    args.append((nxt, SCRATCH / f"{prefix}{nxt}.csv"))
                nxt += 1
            with ThreadPoolExecutor(max_workers=workers) as pool:
                counts = list(pool.map(
                    lambda a: run_stress(stress, champ, bots, a[0], a[1]),
                    args))
            fresh = sum(counts)
            expected_per_run = max(1, round(fresh / max(1, runs)))
            good = 0
            for (seed, path), made in zip(args, counts):
                if made == 0:
                    if path.exists():
                        path.unlink()
                    continue
                seeds_done.add(seed)
                with open(path, newline="", encoding="utf-8") as handle:
                    for row in csv.DictReader(handle):
                        if len(safe_order(row)) >= 2:
                            good += 1
            done += good
            print(f"[{name}] round {round_no}: {fresh} rows, {good} valid")
        if done < target:
            print(f"[{name}] WARNING: short of budget "
                  f"({done}/{target})", file=sys.stderr)
    else:
        print(f"[{name}] rerate mode")
        for path in glob.glob(str(SCRATCH / f"{prefix}*.csv")):
            stem = Path(path).stem.rsplit("-", 1)[1]
            if stem.isdigit():
                seeds_done.add(int(stem))
    paths = sorted(glob.glob(str(SCRATCH / f"{prefix}*.csv")))
    return {"name": name, "paths": paths, "tables": done, "seats": seats}


def safe_order(row, names=None):
    try:
        order = ordered_names(row)
    except (AttributeError, ValueError, TypeError):
        return []
    # Engine bug guard: interleave-corrupted CSV lineups (truncated bot
    # names, wrong seat counts) must never enter the fit.
    if len(order) != len(set(order)):
        return []
    if names is not None and any(bot not in names for bot in order):
        return []
    return order


def audit(paths):
    """(table, seat) de-dupe + placements-conservation check."""
    seen, dupes, tables, skipped = set(), 0, 0, 0
    for path in paths:
        with open(path, newline="", encoding="utf-8") as handle:
            for row in csv.DictReader(handle):
                key = (row.get("seed"), row.get("stage"), row.get("table"))
                if key in seen:
                    dupes += 1
                seen.add(key)
                if len(safe_order(row)) < 2:
                    skipped += 1
                    continue
                tables += 1
    return tables, skipped, dupes


def fit_rows(rows):
    """BT pair matrix over already-read CSV rows. Returns (names, matrix)."""
    names = set()
    wins = {}
    for row in rows:
        order = safe_order(row)
        if len(order) < 2:
            continue
        names.update(order)
        for i, higher in enumerate(order):
            for lower in order[i + 1:]:
                slot = wins.setdefault(pair_key(higher, lower), [0, 0])
                if higher < lower:
                    slot[0] += 1
                else:
                    slot[1] += 1
    matrix = {}
    for key, (won_first, won_second) in wins.items():
        first, second = key.split("")
        matrix[(first, second)] = (won_first, won_second)
    return names, matrix


def rate_variant(name, paths, bots):
    rows = []
    for path in paths:
        with open(path, newline="", encoding="utf-8") as handle:
            rows.extend(csv.DictReader(handle))
    # One vote per table: re-seeded CSVs replay the same (seed, stage,
    # table) rows, so de-dupe before fitting.
    valid = set()
    bot_names = set()
    for path in bots:
        text = Path(REPO / path).read_text(encoding="utf-8")
        for line in text.splitlines():
            line = line.strip()
            if line.startswith("name"):
                _, _, value = line.partition("=")
                bot_names.add(value.strip().strip('"'))
                break
    unique = []
    seen = set()
    rejected = 0
    for row in rows:
        key = (row.get("seed"), row.get("stage"), row.get("table"))
        if key in seen:
            continue
        if len(safe_order(row, bot_names)) < 2:
            seen.add(key)
            rejected += 1
            continue
        seen.add(key)
        unique.append(row)
    names, matrix = fit_rows(unique)
    fitted = fit_ratings(names, matrix)
    counts = {}
    for row in unique:
        for bot in safe_order(row, bot_names):
            counts[bot] = counts.get(bot, 0) + 1
    players = {bot: {"rating": round(fitted[bot], 2),
                     "tables": counts.get(bot, 0)} for bot in fitted}
    pairs = {}
    for (first, second), (won_first, won_second) in matrix.items():
        pairs[pair_key(first, second)] = [won_first, won_second]
    events = [{"csv": os.path.abspath(p), "rows": count_rows(p)}
              for p in paths]
    board = {"players": players, "pairs": pairs, "events": events,
             "rejected_rows": rejected,
             "updated": datetime.now(timezone.utc).isoformat(timespec="seconds")}
    out = RATINGS / f"{name}.json"
    out.write_text(json.dumps(board, indent=2, sort_keys=True) + "\n",
                   encoding="utf-8")
    return board, matrix, names


def print_board(title, players):
    print(f"--- {title} ---")
    board = sorted(players.items(), key=lambda kv: kv[1]["rating"],
                   reverse=True)
    print(f"{'rank':>4}  {'name':<14} {'rating':>7}  {'tables':>8}")
    for rank, (bot, info) in enumerate(board, 1):
        print(f"{rank:>4}  {bot:<14} {info['rating']:>7.2f}  "
              f"{info['tables']:>8}")


def write_overall(boards, bots):
    """Equal-weight pool: each variant's pair matrix scaled to unit duels."""
    valid = set()
    for path in bots:
        text = Path(REPO / path).read_text(encoding="utf-8")
        for line in text.splitlines():
            line = line.strip()
            if line.startswith("name"):
                _, _, value = line.partition("=")
                valid.add(value.strip().strip('"'))
                break
    pooled, names, info = {}, set(), []
    seen_overall = set()
    for name, matrix, variants_names, tables in boards:
        duels = sum(a + b for (a, b) in matrix.values())
        scale = 1.0 / duels if duels > 0 else 0.0
        info.append({"name": name, "tables": tables, "duels": duels,
                     "weight": scale})
        for pair, (a, b) in matrix.items():
            slot = pooled.setdefault(pair, [0.0, 0.0])
            slot[0] += a * scale
            slot[1] += b * scale
        names.update(variants_names)
    fitted = fit_ratings(names, {p: (a, b) for p, (a, b) in pooled.items()})
    players = {bot: {"rating": round(r, 2), "tables": 0}
               for bot, r in fitted.items()}
    for name, _, _, _ in boards:
        for path in glob.glob(str(SCRATCH / f"{name}-*.csv")):
            with open(path, newline="", encoding="utf-8") as handle:
                for row in csv.DictReader(handle):
                    key = (row.get("seed"), row.get("stage"),
                           row.get("table"))
                    if key in seen_overall:
                        continue
                    seen_overall.add(key)
                    for bot in safe_order(row, valid):
                        if bot in players:
                            players[bot]["tables"] += 1
    out = {"players": players, "variants": info,
           "updated": datetime.now(timezone.utc).isoformat(timespec="seconds")}
    (RATINGS / "overall.json").write_text(
        json.dumps(out, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return out


def main():
    parser = argparse.ArgumentParser(description="100K+ scoreboard campaign")
    parser.add_argument("only", nargs="*", help="variant names to run")
    parser.add_argument("--variants", default=str(REPO / "scripts" / "variants.txt"))
    parser.add_argument("--bots", default=str(REPO / "scripts" / "bots.txt"))
    parser.add_argument("--tables-cap", type=int, default=0,
                        help="cap tables per variant (smoke test)")
    parser.add_argument("--depth-cap", type=int, default=0,
                        help="cap bracket depth (smoke test)")
    parser.add_argument("--rerate", action="store_true",
                        help="skip stress runs, fit from existing CSVs")
    parser.add_argument("--workers", type=int, default=os.cpu_count() or 8)
    parser.add_argument("--resume", action="store_true",
                        help="reuse completed-bracket CSVs already in .scratch/"
                             " (default: wipe variant CSVs and start fresh)")
    args = parser.parse_args()

    bot_rows = read_list(args.bots)
    bots = [row[0] for row in bot_rows]
    variants = read_list(args.variants)
    if args.only:
        variants = [v for v in variants if v[0] in args.only]
        if not variants:
            print("error: no matching variants", file=sys.stderr)
            return 1
    stress = Path(_find_exe("cardengine_stress"))
    print(f"stress: {stress} | bots: {len(bots)} | workers: {args.workers}")

    SCRATCH.mkdir(exist_ok=True)
    fitted_boards = []
    for index, row in enumerate(variants):
        name, game = row[0], row[1]
        depth = int(row[2]) if len(row) > 2 else 4
        override = int(row[3]) if len(row) > 3 else 0
        if args.depth_cap:
            depth = min(depth, args.depth_cap)
        seats = int(game_value(game, "num_players"))
        target = override or math.ceil(APPEARANCE_TARGET * len(bots) / seats)
        if args.tables_cap:
            target = min(target, args.tables_cap)
        print(f"[{name}] {game} seats={seats} depth={depth} "
              f"target={target} tables", flush=True)
        played = play_variant(stress, name, game, depth, target, bots,
                              args.workers, 700000 + index * 100000,
                              args.rerate, args.resume)
        tables, skipped, dupes = audit(played["paths"])
        print(f"[{name}] audit: {tables} tables, {skipped} bad rows, "
              f"{dupes} dupe seeds")
        board, matrix, names = rate_variant(name, played["paths"], bots)
        print_board(name, board["players"])
        print(f"saved ratings/{name}.json")
        fitted_boards.append((name, matrix, names, tables))

    if len(fitted_boards) > 1:
        overall = write_overall(fitted_boards, bots)
        print_board("overall (equal weight per variant)",
                    overall["players"])
        print("saved ratings/overall.json")
    return 0


if __name__ == "__main__":
    sys.exit(main())
