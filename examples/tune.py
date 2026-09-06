#!/usr/bin/env python3
"""Parameter search for stronger heuristic bots (TAG-beater program).

Grid-searches the heuristic sliders (looseness/aggression/mistake_rate plus
the Phase-1 trio bluff_rate/defense/position_weight), screens candidates in
small grouped brackets anchored by TAG, then races the finalists plus TAG
and Nit in full brackets. Scoring reuses examples/rate.py's order-independent
Bradley-Terry fit; each candidate's screen score is its rating minus TAG's
rating in the same pool, so groups are comparable.

Usage:
    python examples/tune.py [--lo L...] [--ag A...] [--mi M...]
                            [--bl B...] [--de D...] [--pw W...]
                            [--finalists N] [--seed N] [--hands ...]
                            [--style heuristic|gto] [--variant FILE]

Typical run takes a few minutes (screening) plus the final brackets. The
winner is reported, not promoted: copying a finalist into bots/ under a real
name stays a human decision. Stdlib only.
"""
import argparse
import csv
import glob
import os
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cli import _find_exe  # noqa: E402
from rate import fit_ratings, ordered_names, pair_key  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parent.parent
SCREEN_GAME = REPO_ROOT / "championships" / "big-bash.txt"
FINAL_GAME = REPO_ROOT / ".scratch" / "mega-bash.txt"
ANCHORS = ["bots/tag.txt", "bots/nit.txt"]


def write_candidate(path, name, style, looseness, aggression, mistake,
                    bluff, defense, position_weight, barrels, seed):
    lines = [
        "# CardEngine bot file (format 1) -- tune.py candidate",
        "format_version = 1",
        f'name = "{name}"',
        f'description = "tune: style={style} looseness={looseness} '
        f'aggression={aggression} mistake_rate={mistake} bluff_rate={bluff} '
        f'defense={defense} position_weight={position_weight} '
        f'barrels={barrels}"',
        f"style = {style}",
        f"mistake_rate = {mistake}",
        f"aggression = {aggression}",
        f"looseness = {looseness}",
        f"bluff_rate = {bluff}",
        f"defense = {defense}",
        f"position_weight = {position_weight}",
    ]
    if barrels is not None:
        lines.append(f"barrels = {barrels}")
    lines.append(f"seed = {seed}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def run_bracket(engine, championship, bot_files, seed, out_csv):
    cmd = [str(engine), "--championship", str(championship),
           "--bots", ",".join(str(f) for f in bot_files),
           "--seed", str(seed), "--yes", "--max-hands", "500000",
           "--out", str(out_csv)]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        print(f"stress failed (seed {seed}):\n{proc.stdout}\n{proc.stderr}",
              file=sys.stderr)
        raise SystemExit(1)


def fit_csvs(csvs):
    """Bradley-Terry ratings over CSVs. Returns ({name: rating}, tables)."""
    names = set()
    wins = {}
    tables = 0
    for path in csvs:
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
    return fit_ratings(names, matrix), tables


def main():
    parser = argparse.ArgumentParser(description="Heuristic bot tuner")
    parser.add_argument("--lo", default="0.1,0.2,0.3,0.4,0.55",
                        help="looseness grid (comma-separated)")
    parser.add_argument("--ag", default="0.4,0.55,0.7,0.85",
                        help="aggression grid (comma-separated)")
    parser.add_argument("--mi", default="0.0,0.02",
                        help="mistake_rate grid (comma-separated)")
    parser.add_argument("--bl", default="0.35",
                        help="bluff_rate grid (comma-separated)")
    parser.add_argument("--de", default="1.0",
                        help="defense grid (comma-separated)")
    parser.add_argument("--pw", default="1.0",
                        help="position_weight grid (comma-separated)")
    parser.add_argument("--ba", default=None,
                        help="barrels grid (comma-separated; default: file "
                             "default 0.0)")
    parser.add_argument("--style", default="heuristic",
                        choices=["heuristic", "gto"],
                        help="candidate bot style")
    parser.add_argument("--variant", default=None,
                        help="screen/final championship file "
                             "(default: big-bash screen, mega final)")
    parser.add_argument("--group-size", type=int, default=5)
    parser.add_argument("--screen-reps", type=int, default=2)
    parser.add_argument("--finalists", type=int, default=6)
    parser.add_argument("--final-reps", type=int, default=2)
    parser.add_argument("--seed", type=int, default=20000000)
    parser.add_argument("--engine", default=None)
    parser.add_argument("--workdir", default=str(REPO_ROOT / ".scratch" / "tune"))
    args = parser.parse_args()

    engine = Path(args.engine) if args.engine else _find_exe("cardengine_stress")
    if engine is None or not Path(engine).is_file():
        print("error: engine executable not found (pass --engine)", file=sys.stderr)
        return 1
    workdir = Path(args.workdir)
    workdir.mkdir(parents=True, exist_ok=True)
    grid_lo = [float(v) for v in args.lo.split(",")]
    grid_ag = [float(v) for v in args.ag.split(",")]
    grid_mi = [float(v) for v in args.mi.split(",")]
    grid_bl = [float(v) for v in args.bl.split(",")]
    grid_de = [float(v) for v in args.de.split(",")]
    grid_pw = [float(v) for v in args.pw.split(",")]
    grid_ba = [None] if args.ba is None else [float(v) for v in args.ba.split(",")]
    combos = [(lo, ag, mi, bl, de, pw, ba)
              for lo in grid_lo for ag in grid_ag for mi in grid_mi
              for bl in grid_bl for de in grid_de for pw in grid_pw
              for ba in grid_ba]
    print(f"{len(combos)} candidates "
          f"({len(grid_lo)} lo x {len(grid_ag)} ag x {len(grid_mi)} mi x "
          f"{len(grid_bl)} bl x {len(grid_de)} de x {len(grid_pw)} pw x "
          f"{len(grid_ba)} ba)")

    candidates = {}
    for num, (lo, ag, mi, bl, de, pw, ba) in enumerate(combos):
        name = f"tune{num:02d}"
        path = workdir / f"{name}.txt"
        write_candidate(path, name, args.style, lo, ag, mi, bl, de, pw, ba,
                        seed=1000 + num)
        candidates[name] = path

    # Screen: groups of candidates + TAG anchor, small brackets.
    names = sorted(candidates)
    groups = [names[i:i + args.group_size]
              for i in range(0, len(names), args.group_size)]
    screen_game = Path(args.variant) if args.variant else SCREEN_GAME
    final_game = Path(args.variant) if args.variant else FINAL_GAME
    scores = {}
    for gi, group in enumerate(groups):
        csvs = []
        bot_files = [REPO_ROOT / ANCHORS[0]] + [candidates[n] for n in group]
        for rep in range(args.screen_reps):
            out = workdir / f"screen-g{gi}-r{rep}.csv"
            run_bracket(engine, screen_game, bot_files,
                        args.seed + gi * 1000 + rep, out)
            csvs.append(out)
        ratings, _ = fit_csvs(csvs)
        anchor = ratings.get("TAG", 1200.0)
        for name in group:
            scores[name] = ratings.get(name, anchor) - anchor
        best = max(group, key=lambda n: scores[n])
        print(f"group {gi}: best {best} ({scores[best]:+.1f} vs TAG)")

    ranked = sorted(scores, key=scores.get, reverse=True)
    finalists = ranked[:args.finalists]
    print(f"finalists: {', '.join(f'{n} ({scores[n]:+.1f})' for n in finalists)}")

    # Final: finalists + TAG + Nit, full brackets.
    bot_files = [REPO_ROOT / a for a in ANCHORS] + [candidates[n]
                                                    for n in finalists]
    csvs = []
    for rep in range(args.final_reps):
        out = workdir / f"final-r{rep}.csv"
        run_bracket(engine, final_game, bot_files,
                    args.seed + 9000000 + rep * 100000, out)
        csvs.append(out)
    ratings, tables = fit_csvs(csvs)
    print(f"final ({tables} tables):")
    for name in sorted(ratings, key=ratings.get, reverse=True):
        mark = " <-- candidate" if name in candidates else ""
        print(f"  {name:<12} {ratings[name]:>8.2f}{mark}")
    print(f"candidate files in {workdir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
