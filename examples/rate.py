#!/usr/bin/env python3
"""Skill ratings for CardEngine bots (and, later, humans).

Reads cardengine_stress CSVs and maintains a persistent ratings file. Every
table contributes each-higher-beats-each-lower pair from the finishing order
(`placements` column; legacy CSVs without it fall back to winner-beats-field)
into a win matrix, which is fitted with the Bradley-Terry model (Hunter's MM
algorithm) and reported on the Elo scale:

    P(A beats B) = 1 / (1 + 10^((Rb - Ra) / 400))

The fit is order-independent: re-rating the same events in any order gives
bit-identical ratings, and a bot that beats everyone head-to-head always
comes out on top. (Per-game sequential Elo was tried first and failed that
bar: over 100k tables its feedback dynamics buried the Condorcet winner
last. The matrix fit cannot do that.)

Usage:
    python examples/rate.py [--out ratings/bots.json] [--reset] [--matrix]
                            results1.csv [pattern ...]
    python examples/rate.py --overall OUT --reset --variant NAME=PATTERN ...

Shell-style globs are expanded internally. Applied events are recorded, so
re-rating the same CSV is a no-op (use --reset to start over). Any name in a
lineup is a rated entity: bots today, human players once rated games exist.

--overall pools several variants into one board with equal weight per
variant: a 6-max table yields 15 duels and a heads-up table 1, so raw
pooling would let big tables shout down small ones. Each variant's pair
matrix is scaled to equal total weight before the shared fit. Stdlib only.
"""
import argparse
import csv
import glob
import json
import math
import os
import sys
from datetime import datetime, timezone

SCALE = 400.0
BASE_RATING = 1200.0
PRIOR = 0.5  # Virtual win each way per met pair: keeps the fit finite when
# a bot has never lost (or never won) against someone.


def ordered_names(row):
    """Finishing order, champion first, as bot/player names."""
    lineup = [name.strip() for name in row.get("lineup", "").split(";")]
    lineup = [name for name in lineup if name]
    if not lineup:
        return []
    raw_places = (row.get("placements") or "").strip()
    if raw_places:
        try:
            seats = [int(s) for s in raw_places.split(";")]
        except ValueError:
            return []
        if sorted(seats) != list(range(len(lineup))):
            return []
        return [lineup[s] for s in seats]
    # Legacy CSVs: winner first, everyone else in seat order.
    try:
        winner = int(row.get("winner_seat", -1))
    except ValueError:
        return []
    if winner < 0 or winner >= len(lineup):
        return []
    rest = [name for i, name in enumerate(lineup) if i != winner]
    return [lineup[winner]] + rest


def pair_key(first, second):
    return "\u0001".join(sorted((first, second)))


def fit_ratings(names, wins):
    """Bradley-Terry MLE via Hunter's MM algorithm. Returns {name: Elo}."""
    order = sorted(names)
    index = {name: i for i, name in enumerate(order)}
    count = len(order)
    beaten = [0.0] * count  # Total pair-wins per bot (plus prior).
    versus = [[0.0] * count for _ in range(count)]  # Shared tables per pair.
    for (first, second), (won_first, won_second) in wins.items():
        i, j = index[first], index[second]
        beaten[i] += won_first + PRIOR
        beaten[j] += won_second + PRIOR
        versus[i][j] += won_first + won_second + 2 * PRIOR
        versus[j][i] += won_first + won_second + 2 * PRIOR
    strength = [1.0] * count
    for _ in range(10000):
        biggest = 0.0
        nxt = [0.0] * count
        for i in range(count):
            denom = 0.0
            for j in range(count):
                if i != j and versus[i][j] > 0:
                    denom += versus[i][j] / (strength[i] + strength[j])
            nxt[i] = beaten[i] / denom if denom > 0 else strength[i]
            biggest = max(biggest, abs(nxt[i] - strength[i]))
        # Geometric-mean normalization keeps the scale anchored.
        log_mean = sum(math.log(s) for s in nxt) / count
        strength = [s / math.exp(log_mean) for s in nxt]
        if biggest < 1e-12:
            break
    anchor = BASE_RATING - sum(SCALE * math.log10(s)
                               for s in strength) / count
    return {name: SCALE * math.log10(strength[index[name]]) + anchor
            for name in order}


def read_matrix(paths):
    """Accumulate a BT pair matrix over CSVs. Returns (names, wins, tables)
    where wins maps (low, high) name pairs to (low-won, high-won) duels."""
    names = set()
    wins = {}
    tables = 0
    for path in paths:
        with open(path, newline="", encoding="utf-8") as handle:
            for row in csv.DictReader(handle):
                order = ordered_names(row)
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
                tables += 1
    matrix = {}
    for key, (won_first, won_second) in wins.items():
        first, second = key.split("\u0001")
        matrix[(first, second)] = (won_first, won_second)
    return names, matrix, tables


def count_tables(paths):
    counts = {}
    for path in paths:
        with open(path, newline="", encoding="utf-8") as handle:
            for row in csv.DictReader(handle):
                order = ordered_names(row)
                if len(order) < 2:
                    continue
                for name in order:
                    counts[name] = counts.get(name, 0) + 1
    return counts


def main():
    parser = argparse.ArgumentParser(description="BT ratings from stress CSVs")
    parser.add_argument("csvs", nargs="*", help="result files or glob patterns")
    parser.add_argument("--out", default=os.path.join("ratings", "bots.json"))
    parser.add_argument("--reset", action="store_true",
                        help="ignore existing ratings and start over")
    parser.add_argument("--matrix", action="store_true",
                        help="also print the head-to-head matrix")
    parser.add_argument("--overall", default=None, metavar="OUT",
                        help="pool variants into one board (needs --variant)")
    parser.add_argument("--variant", action="append", default=[],
                        metavar="NAME=PATTERN",
                        help="one variant board for --overall; repeatable")
    args = parser.parse_args()

    if args.overall is not None:
        return run_overall(args)

    if not args.csvs:
        print("error: need CSV files or patterns", file=sys.stderr)
        return 1

    ratings = {"players": {}, "pairs": {}, "events": []}
    if not args.reset and os.path.exists(args.out):
        with open(args.out, encoding="utf-8") as handle:
            ratings = json.load(handle)
    ratings.setdefault("players", {})
    ratings.setdefault("pairs", {})
    ratings.setdefault("events", [])
    applied = {(e.get("csv"), e.get("rows")) for e in ratings["events"]}
    players = ratings["players"]

    expanded = []
    for pattern in args.csvs:
        hits = sorted(glob.glob(pattern))
        if not hits:
            print(f"error: no files match '{pattern}'", file=sys.stderr)
            return 1
        expanded.extend(hits)

    total_tables = 0
    skipped_rows = 0
    for path in expanded:
        key = os.path.abspath(path)
        with open(path, newline="", encoding="utf-8") as handle:
            rows = list(csv.DictReader(handle))
        if (key, len(rows)) in applied:
            print(f"skip {path}: already applied ({len(rows)} tables)")
            continue
        for row in rows:
            order = ordered_names(row)
            if len(order) < 2:
                skipped_rows += 1
                continue
            for name in order:
                players.setdefault(name, {"rating": BASE_RATING,
                                          "tables": 0})["tables"] += 1
            for i, higher in enumerate(order):
                for lower in order[i + 1:]:
                    slot = ratings["pairs"].setdefault(pair_key(higher, lower),
                                                       [0, 0])
                    if higher < lower:
                        slot[0] += 1
                    else:
                        slot[1] += 1
            total_tables += 1
        ratings["events"].append({"csv": key, "rows": len(rows)})

    wins = {}
    for key, (won_first, won_second) in ratings["pairs"].items():
        first, second = key.split("\u0001")
        wins[(first, second)] = (won_first, won_second)
    fitted = fit_ratings(set(players), wins)
    for name, rating in fitted.items():
        players[name]["rating"] = round(rating, 2)
    ratings["updated"] = datetime.now(timezone.utc).isoformat(timespec="seconds")
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as handle:
        json.dump(ratings, handle, indent=2, sort_keys=True)
        handle.write("\n")

    board = sorted(players.items(), key=lambda kv: kv[1]["rating"], reverse=True)
    print(f"tables rated: {total_tables} (skipped rows: {skipped_rows})")
    print(f"{'rank':>4}  {'name':<12} {'rating':>7}  {'tables':>6}")
    for rank, (name, info) in enumerate(board, 1):
        print(f"{rank:>4}  {name:<12} {info['rating']:>7.2f}  {info['tables']:>6}")
    if args.matrix:
        print_matrix(players, wins)
    print(f"saved {args.out}")
    return 0


def print_matrix(players, wins):
    names = sorted(players)
    print("head-to-head row-beats-col %:")
    print("           " + "".join(f"{n[:6]:>7}" for n in names))
    for first in names:
        cells = []
        for second in names:
            if first == second:
                cells.append("    ---")
                continue
            low, high = sorted((first, second))
            won_low, won_high = wins.get((low, high), (0, 0))
            game_count = won_low + won_high
            won = won_low if first == low else won_high
            if game_count == 0:
                cells.append("    ---")
            else:
                cells.append(f"{won / game_count:>6.0%}")
        print(f"{first[:10]:<10}" + "".join(cells))


def expand_patterns(patterns):
    expanded = []
    for pattern in patterns:
        hits = sorted(glob.glob(pattern))
        if not hits:
            print(f"error: no files match '{pattern}'", file=sys.stderr)
            return None
        expanded.extend(hits)
    return expanded


def run_overall(args):
    """Pool --variant NAME=PATTERN boards into one equal-weight board."""
    if not args.variant:
        print("error: --overall needs at least one --variant NAME=PATTERN",
              file=sys.stderr)
        return 1
    variants = []
    for spec in args.variant:
        name, _, pattern = spec.partition("=")
        if not name or not pattern:
            print(f"error: bad --variant '{spec}' (want NAME=PATTERN)",
                  file=sys.stderr)
            return 1
        paths = expand_patterns([pattern])
        if paths is None:
            return 1
        names, matrix, tables, = read_matrix(paths)[:3]
        variants.append({"name": name, "names": names, "matrix": matrix,
                         "tables": tables})
        print(f"variant {name}: {tables} tables")
    pooled = {}
    for variant in variants:
        duels = sum(a + b for (a, b) in variant["matrix"].values())
        scale = 1.0 / duels if duels > 0 else 0.0
        variant["weight"] = scale
        variant["duels"] = duels
        for pair, (won_low, won_high) in variant["matrix"].items():
            slot = pooled.setdefault(pair, [0.0, 0.0])
            slot[0] += won_low * scale
            slot[1] += won_high * scale
    wins = {pair: (a, b) for pair, (a, b) in pooled.items()}
    names = set()
    for variant in variants:
        names.update(variant["names"])
    fitted = fit_ratings(names, wins)
    players = {name: {"rating": round(rating, 2), "tables": 0}
               for name, rating in fitted.items()}
    for variant, spec in zip(variants, args.variant):
        _, _, pattern = spec.partition("=")
        for name, count in count_tables(expand_patterns([pattern])).items():
            players[name]["tables"] += count
    board = sorted(players.items(), key=lambda kv: kv[1]["rating"], reverse=True)
    print(f"{'variant':<12} {'tables':>7} {'duels':>9} {'weight':>10}")
    for variant in variants:
        print(f"{variant['name']:<12} {variant['tables']:>7} "
              f"{variant['duels']:>9.0f} {variant['weight']:>10.3e}")
    print(f"{'rank':>4}  {'name':<12} {'rating':>7}  {'tables':>6}")
    for rank, (name, info) in enumerate(board, 1):
        print(f"{rank:>4}  {name:<12} {info['rating']:>7.2f}  {info['tables']:>6}")
    ratings = {"players": players, "variants": [
        {"name": v["name"], "tables": v["tables"], "duels": v["duels"],
         "weight": v["weight"]} for v in variants],
        "updated": datetime.now(timezone.utc).isoformat(timespec="seconds")}
    os.makedirs(os.path.dirname(os.path.abspath(args.overall)), exist_ok=True)
    with open(args.overall, "w", encoding="utf-8") as handle:
        json.dump(ratings, handle, indent=2, sort_keys=True)
        handle.write("\n")
    print(f"saved {args.overall}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
