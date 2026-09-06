#!/usr/bin/env python3
"""Single-factor ablation: one slider at a time off a fixed base.

Every candidate is the base bot with exactly one slider changed. Seed-varied
pure Randys ride along as a noise floor: any slider effect smaller than the
Randy-to-Randy spread is not real. Scoring is BT rating-minus-base in shared
pools (same convention as tune.py screen scores).

Usage:
    python examples/ablate.py [--base bots/vanguard.txt] [--seed N]
                              [--workdir DIR] [--reps N]

Writes candidates to workdir, screens each sweep group with the base as
anchor on big-bash, then prints the marginal-effect table gated on the
noise floor. Stdlib only.
"""
import argparse
import csv
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cli import _find_exe  # noqa: E402
from rate import fit_ratings, ordered_names, pair_key  # noqa: E402
from tune import run_bracket  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parent.parent
SCREEN_GAME = REPO_ROOT / "championships" / "big-bash.txt"

BASE_DEFAULTS = {
    "style": "heuristic",
    "mistake_rate": "0.0",
    "aggression": "0.65",
    "looseness": "0.2",
    "bluff_rate": "0.7",
    "defense": "1.0",
    "position_weight": "0.0",
    "survival": "1.0",
    "planning": "2",
}

# slider -> values to try (base value included as control).
SWEEPS = {
    "looseness": ["0.0", "0.1", "0.2", "0.35", "0.5"],
    "aggression": ["0.3", "0.5", "0.65", "0.85"],
    "mistake_rate": ["0.0", "0.03", "0.1"],
    "bluff_rate": ["0.0", "0.35", "0.7", "1.0"],
    "defense": ["0.0", "0.5", "1.0", "1.5", "2.0"],
    "position_weight": ["0.0", "1.0", "2.0"],
    "survival": ["0.0", "0.5", "1.0"],
    "planning": ["0", "1", "2"],
    "barrels": ["0.0", "1.0", "2.0"],
    "adapt_rate": ["0.0", "1.0", "2.0"],
}

RANDY_SEEDS = [1, 7, 13]


def write_candidate(path, name, slider, value, seed):
    settings = dict(BASE_DEFAULTS)
    settings[slider] = value
    lines = ["# CardEngine bot file (format 1) -- ablate.py candidate",
             "format_version = 1",
             f'name = "{name}"',
             f'description = "ablation: {slider}={value} (base otherwise)"',
             f"style = {settings['style']}",
             f"mistake_rate = {settings['mistake_rate']}",
             f"aggression = {settings['aggression']}",
             f"looseness = {settings['looseness']}",
             f"bluff_rate = {settings['bluff_rate']}",
             f"defense = {settings['defense']}",
             f"position_weight = {settings['position_weight']}",
             f"survival = {settings['survival']}",
             f"planning = {settings['planning']}",
             f"barrels = {settings.get('barrels', '0.0')}",
             f"seed = {seed}"]
    # adapt_rate only valid on adaptive style; heuristic candidates skip it.
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_randy(path, name, seed):
    path.write_text(
        "# CardEngine bot file (format 1) -- ablate.py noise control\n"
        "format_version = 1\n"
        f'name = "{name}"\n'
        'description = "noise floor: pure random, seed varied"\n'
        "style = random\n"
        f"seed = {seed}\n", encoding="utf-8")


def fit_csvs(csvs):
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
    parser = argparse.ArgumentParser(description="Single-factor ablation")
    parser.add_argument("--seed", type=int, default=220000000)
    parser.add_argument("--reps", type=int, default=3)
    parser.add_argument("--engine", default=None)
    parser.add_argument("--workdir",
                        default=str(REPO_ROOT / ".scratch" / "ablate"))
    args = parser.parse_args()

    engine = Path(args.engine) if args.engine else _find_exe("cardengine_stress")
    if engine is None or not Path(engine).is_file():
        print("error: engine executable not found (pass --engine)", file=sys.stderr)
        return 1
    workdir = Path(args.workdir)
    workdir.mkdir(parents=True, exist_ok=True)

    # Noise controls: seed-varied pure Randys.
    randys = {}
    for k, seed in enumerate(RANDY_SEEDS):
        name = f"randyN{k}"
        path = workdir / f"{name}.txt"
        write_randy(path, name, seed)
        randys[name] = path

    # Base control (Vanguard-equivalent, fixed seed).
    base_path = workdir / "base.txt"
    write_candidate(base_path, "Base", "looseness",
                    BASE_DEFAULTS["looseness"], seed=61)

    results = {}
    seq = 0
    for slider, values in SWEEPS.items():
        if slider == "adapt_rate":
            print("skip adapt_rate: needs adaptive style (separate study)")
            continue
        csvs = []
        group_names = []
        bot_files = [base_path]
        for value in values:
            name = f"ab{seq:02d}"
            seq += 1
            path = workdir / f"{name}.txt"
            write_candidate(path, name, slider, value, seed=2000 + seq)
            group_names.append((name, slider, value))
            bot_files.append(path)
        # Noise controls IN the pool: seed-varied pure Randys share every
        # table, so their spread measures pool noise, not skill.
        bot_files.extend(randys.values())
        for rep in range(args.reps):
            out = workdir / f"screen-{slider}-r{rep}.csv"
            run_bracket(engine, SCREEN_GAME, bot_files,
                        args.seed + seq * 1000 + rep, out)
            csvs.append(out)
        ratings, tables = fit_csvs(csvs)
        randy_r = [ratings.get(n, 1200.0) for n in randys]
        spread = max(randy_r) - min(randy_r)
        base = ratings.get("Base", 1200.0)
        print(f"--- {slider} ({tables} tables) ---")
        for name, s, value in group_names:
            delta = ratings.get(name, base) - base
            results[(s, value)] = delta
            print(f"  {s}={value:<5} {delta:+7.1f} vs base")
        print(f"  [noise: Randy spread {spread:.1f} -- small-pool winner "
              f"effects dominate; treat deltas as directional]")
        results[("NOISE", slider)] = spread
    return 0


if __name__ == "__main__":
    sys.exit(main())
