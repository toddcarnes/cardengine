#!/usr/bin/env python3
"""Deep-pool ablation: full 12-bot field plus slider variants, no Randys.

Each campaign races the full rated field with 2-3 seats replaced by slider
variants, 3 reps of full Mega brackets with different seeds. Noise is read
from seed-varied copies of the BASE file (same settings, different seed):
pure RNG noise without the lottery-ticket pathology. Field bots dilute any
single heat 11 ways instead of 6.

Usage:
    python examples/ablate_deep.py --slider survival --values 0.0,1.0
        [--base bots/vanguard.txt] [--seed N] [--workdir DIR]

Stdlib only.
"""
import argparse
import csv
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cli import _find_exe  # noqa: E402
from rate import fit_ratings, ordered_names, pair_key  # noqa: E402
from tune import run_bracket  # noqa: E402
from ablate_board import load_base, write_candidate  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parent.parent
MEGA = REPO_ROOT / ".scratch" / "mega-bash.txt"
FIELD = ["bots/tight.txt", "bots/loose.txt", "bots/random.txt", "bots/tag.txt",
         "bots/lag.txt", "bots/nit.txt", "bots/gto.txt", "bots/adaptive.txt",
         "bots/station.txt", "bots/fitfold.txt", "bots/survivor.txt"]
BASE_SEEDS = [61, 62, 63]


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
    parser = argparse.ArgumentParser(description="Deep-pool ablation")
    parser.add_argument("--slider", required=True)
    parser.add_argument("--values", required=True)
    parser.add_argument("--base", default="bots/vanguard.txt")
    parser.add_argument("--seed", type=int, default=240000000)
    parser.add_argument("--engine", default=None)
    parser.add_argument("--workdir", default=None)
    args = parser.parse_args()

    engine = Path(args.engine) if args.engine else _find_exe("cardengine_stress")
    if engine is None or not Path(engine).is_file():
        print("error: engine executable not found (pass --engine)", file=sys.stderr)
        return 1
    base_settings = load_base(REPO_ROOT / args.base)
    workdir = Path(args.workdir or (REPO_ROOT / ".scratch" / f"deep-{args.slider}"))
    workdir.mkdir(parents=True, exist_ok=True)

    values = args.values.split(",")
    # Seed-varied base copies: same settings, different seeds (noise probe).
    base_names = []
    for k, seed in enumerate(BASE_SEEDS):
        name = "Base" if k == 0 else f"BaseS{k}"
        path = workdir / f"{name.lower()}.txt"
        write_candidate(path, name, base_settings, args.slider,
                        base_settings.get(args.slider, "0.0"), seed=seed)
        base_names.append(name)
    variants = []
    for k, value in enumerate(values):
        # Skip the base value itself (already covered by Base copies).
        if value == base_settings.get(args.slider, "0.0"):
            continue
        name = f"v{k}"
        path = workdir / f"{name}.txt"
        write_candidate(path, name, base_settings, args.slider, value,
                        seed=4000 + k)
        variants.append((name, value))

    # Pool: field + base copies + variants (no Randys).
    bot_files = [REPO_ROOT / f for f in FIELD]
    bot_files += [workdir / f"{n.lower()}.txt" for n in base_names]
    bot_files += [workdir / f"{n}.txt" for n, _ in variants]

    csvs = []
    for rep in range(3):
        out = workdir / f"deep-r{rep}.csv"
        run_bracket(engine, MEGA, bot_files, args.seed + rep * 100000, out,
                    max_hands=5000000)
        csvs.append(out)
    ratings, tables = fit_csvs(csvs)
    base = ratings.get("Base", 1200.0)
    print(f"=== {args.slider} deep-pool ({tables} tables) ===")
    for name, value in variants:
        print(f"  {args.slider}={value:<5} {ratings.get(name, base) - base:+7.1f} vs Base")
    spreads = [ratings.get(n, base) for n in base_names]
    noise = max(spreads) - min(spreads)
    print(f"  Base {base:.1f}; base-seed spread (noise): {noise:.1f}")
    # Field context: where does Base sit overall?
    ranked = sorted(ratings, key=ratings.get, reverse=True)
    print(f"  Base rank: {ranked.index('Base') + 1}/{len(ranked)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
