#!/usr/bin/env python3
"""Board-scale ablation: full Mega brackets per slider value, shared anchors.

Each campaign races a slider's values plus the base plus seed-varied Randy
controls in full depth-4 Mega brackets (1555 tables each), N reps per value
set. BT fit over the pooled tables; deltas vs base are board-scale marginal
effects with the Randy spread as the noise gate.

Usage:
    python examples/ablate_board.py --slider survival --values 0.0,0.5,1.0
        [--base bots/vanguard.txt] [--reps 2] [--seed N] [--workdir DIR]

Base file supplies every slider; only the swept slider is overridden.
Randy controls (3 seeds) ride every bracket. Stdlib only.
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
MEGA = REPO_ROOT / ".scratch" / "mega-bash.txt"
RANDY_SEEDS = [1, 7, 13]


def load_base(path):
    settings = {}
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, _, value = line.partition("=")
            settings[key.strip()] = value.strip().strip('"')
    return settings


def write_candidate(path, name, base_settings, slider, value, seed):
    settings = dict(base_settings)
    settings[slider] = value
    order = ["style", "mistake_rate", "aggression", "looseness", "bluff_rate",
             "defense", "position_weight", "survival", "planning", "barrels"]
    lines = ["# CardEngine bot file (format 1) -- ablate_board.py candidate",
             "format_version = 1",
             f'name = "{name}"',
             f'description = "board ablation: {slider}={value}"']
    for key in order:
        if key in settings:
            lines.append(f"{key} = {settings[key]}")
    lines.append(f"seed = {seed}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_randy(path, name, seed):
    path.write_text(
        "# CardEngine bot file (format 1) -- ablate_board.py noise control\n"
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
    parser = argparse.ArgumentParser(description="Board-scale ablation")
    parser.add_argument("--slider", required=True)
    parser.add_argument("--values", required=True,
                        help="comma-separated slider values")
    parser.add_argument("--base", default="bots/vanguard.txt")
    parser.add_argument("--reps", type=int, default=2)
    parser.add_argument("--seed", type=int, default=230000000)
    parser.add_argument("--engine", default=None)
    parser.add_argument("--workdir", default=None)
    args = parser.parse_args()

    engine = Path(args.engine) if args.engine else _find_exe("cardengine_stress")
    if engine is None or not Path(engine).is_file():
        print("error: engine executable not found (pass --engine)", file=sys.stderr)
        return 1
    base_settings = load_base(REPO_ROOT / args.base)
    if args.slider not in base_settings and args.slider != "barrels":
        print(f"error: slider {args.slider} not in base file", file=sys.stderr)
        return 1
    workdir = Path(args.workdir or (REPO_ROOT / ".scratch" / f"abl-{args.slider}"))
    workdir.mkdir(parents=True, exist_ok=True)

    values = args.values.split(",")
    names = []
    bot_files = []
    base_path = workdir / "base.txt"
    write_candidate(base_path, "Base", base_settings, args.slider,
                    base_settings.get(args.slider, "0.0"), seed=61)
    bot_files.append(base_path)
    for k, value in enumerate(values):
        name = f"v{k}"
        path = workdir / f"{name}.txt"
        write_candidate(path, name, base_settings, args.slider, value,
                        seed=3000 + k)
        names.append((name, value))
        bot_files.append(path)
    randys = []
    for k, seed in enumerate(RANDY_SEEDS):
        name = f"randyN{k}"
        path = workdir / f"{name}.txt"
        write_randy(path, name, seed)
        randys.append(name)
        bot_files.append(path)

    csvs = []
    for rep in range(args.reps):
        out = workdir / f"board-r{rep}.csv"
        run_bracket(engine, MEGA, bot_files, args.seed + rep * 100000, out,
                    max_hands=5000000)
        csvs.append(out)
    ratings, tables = fit_csvs(csvs)
    base = ratings.get("Base", 1200.0)
    print(f"=== {args.slider} ({tables} tables) ===")
    for name, value in names:
        print(f"  {args.slider}={value:<5} {ratings.get(name, base) - base:+7.1f} vs base")
    spread = max(ratings.get(n, base) for n in randys) - min(
        ratings.get(n, base) for n in randys)
    print(f"  Base {base:.1f}; Randy spread (noise): {spread:.1f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
