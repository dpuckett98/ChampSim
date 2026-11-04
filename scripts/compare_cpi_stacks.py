#!/usr/bin/env python3
"""
Check how well the baseline (gc) CPI stacks predict the speedup of the idealized configs.

For each (CPI-stack category, ideal config) pair - by default mis-speculating vs. gc_bp and load in flight vs.
gc_l1d - and each trace:

    predicted = 100 * category CPI / total CPI        in the gc run's CPI stack (% of gc's cycles)
    actual    = 100 * (gc cycles - config cycles) / gc cycles
    diff      = predicted - actual                     (percentage points of gc's cycles)

A positive difference means the stack overstates what removing that bottleneck buys; negative means it
understates it. One panel per (category, stack, config), bars sorted from least to greatest.

Usage:
    python3 compare_cpi_stacks.py RESULTS_DIR [-o fig/cpi_stack_vs_ideal.png] [--csv diffs.csv]
                                  [--pair mis_speculating=gc_bp --pair load_in_flight=gc_l1d] [--baseline gc]
"""

import argparse
import math
import sys
from pathlib import Path

import pandas as pd

from parse_champsim_outputs import (CPI_STAGES, GRID_COLOR, TEXT_PRIMARY, TEXT_SECONDARY, cycles_removed,
                                    parse_dir, short_trace_name)

DEFAULT_NAME_REGEX = r"^(?P<trace>.+)\.cst_bin:(?P<config>\w+)$"
DEFAULT_PAIRS = (("mis_speculating", "gc_bp"), ("load_in_flight", "gc_l1d"))

OVER_COLOR = "#e34948"   # stack predicts more than the ideal config removes
UNDER_COLOR = "#2a78d6"  # stack predicts less


def find_category(df, stage, category, cpu=0):
    """The CPI-stack column name for category in stage ('load_in_flight' also matches 'stalled_load_in_flight')."""
    prefix = f"cpu{cpu}.cpi_stack.{stage}."
    for cat in (category, f"stalled_{category}"):
        col = f"{prefix}{cat}.cpi"
        if col in df.columns:
            return cat, col
    return None, None


def compare(df, pairs=DEFAULT_PAIRS, baseline="gc", cpu=0, trace_col="trace", config_col="config"):
    """
    Long-format table: one row per (category, stage, config, trace) with predicted, actual and diff columns.
    Stages that don't have the category are skipped.
    """
    removed = cycles_removed(df, baseline=baseline, trace_col=trace_col, config_col=config_col,
                             cycles_col=f"cpu{cpu}.cycles")
    base = df[df["completed"].astype(bool) & (df[config_col] == baseline)].set_index(trace_col)

    rows = []
    for category, config in pairs:
        if config not in removed.columns:
            print(f"warning: no runs for config {config!r}, skipping {category}", file=sys.stderr)
            continue
        found = False
        for stage in CPI_STAGES:
            cat, col = find_category(df, stage, category, cpu)
            total_col = f"cpu{cpu}.cpi_stack.{stage}.cpi"
            if col is None or total_col not in base.columns:
                continue
            found = True
            predicted = 100 * base[col] / base[total_col]
            for trace in removed.index:
                p = predicted.get(trace, math.nan)
                a = removed.at[trace, config]
                rows.append({"category": cat, "stage": stage, "config": config, "trace": trace,
                             "predicted": p, "actual": a, "diff": p - a})
        if not found:
            print(f"warning: no CPI-stack category {category!r} in the {baseline} runs", file=sys.stderr)
    return pd.DataFrame(rows)


def plot(table, output=None, ncols=None, sharey=True):
    """One panel per (category, stage, config); bars are the per-trace differences, sorted least to greatest."""
    import matplotlib

    if output:
        matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.patches import Patch
    from matplotlib.ticker import FuncFormatter

    groups = list(table.groupby(["category", "stage", "config"], sort=False))
    ncols = ncols or min(len(groups), 4)
    nrows = math.ceil(len(groups) / ncols)
    fig, axes = plt.subplots(nrows, ncols, figsize=(4.6 * ncols, 4.2 * nrows), sharey=sharey,
                             layout="constrained", squeeze=False)

    for ax, ((category, stage, config), g) in zip(axes.flat, groups):
        g = g.assign(trace=g["trace"].map(short_trace_name)).sort_values("diff", na_position="last", kind="stable")
        x = range(len(g))
        vals = g["diff"]
        ax.bar(x, vals.fillna(0), width=0.6, zorder=2,
               color=[OVER_COLOR if v > 0 else UNDER_COLOR for v in vals.fillna(0)])
        for i, v in enumerate(vals):
            if pd.isna(v):
                ax.text(i, 0, "n/a", ha="center", va="bottom", fontsize=7, color=TEXT_SECONDARY, rotation=90)
        ax.axhline(0, color=TEXT_SECONDARY, linewidth=0.8, zorder=3)
        ax.set_xticks(list(x))
        ax.set_xticklabels(g["trace"], rotation=60, ha="right", fontsize=7, color=TEXT_SECONDARY)
        ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v:+g}" if v else "0"))
        ax.tick_params(axis="y", labelsize=8, colors=TEXT_SECONDARY, length=0)
        ax.tick_params(axis="x", length=0)
        ax.grid(axis="y", color=GRID_COLOR, linewidth=0.8, zorder=0)
        for side in ("top", "right", "left", "bottom"):
            ax.spines[side].set_visible(False)
        if sharey:
            ax.yaxis.set_tick_params(labelleft=True)

        n = vals.notna().sum()
        name = category.replace("stalled_", "").replace("_", " ").replace("mis speculating", "mis-speculating")
        summary = (f"mean {vals.mean() + 0.0:+.1f} pp, mean |diff| {vals.abs().mean():.1f} pp  ({n}/{len(vals)} traces)"
                   if n else "no finished runs")
        ax.set_title(f"{stage} {name} vs. {config}\n{summary}", fontsize=9.5, color=TEXT_PRIMARY, loc="left",
                     linespacing=1.5)

    for ax in axes.flat[len(groups):]:
        ax.set_visible(False)
    for row in axes:
        row[0].set_ylabel("CPI-stack share − cycles removed\n(percentage points of gc cycles)",
                          fontsize=9, color=TEXT_SECONDARY)

    fig.legend(handles=[Patch(color=OVER_COLOR, label="stack overstates the ideal config's gain"),
                        Patch(color=UNDER_COLOR, label="stack understates it")],
               loc="outside upper right", ncols=2, frameon=False, fontsize=8)

    if output:
        Path(output).parent.mkdir(parents=True, exist_ok=True)
        fig.savefig(output, dpi=150)
        plt.close(fig)
    return fig


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("directory", help="directory containing ChampSim stdout captures")
    ap.add_argument("-o", "--output", default="fig/cpi_stack_vs_ideal.png", help="figure to write")
    ap.add_argument("--csv", help="also write the per-trace predicted/actual/diff table here")
    ap.add_argument("--pair", action="append", metavar="CATEGORY=CONFIG",
                    help="CPI-stack category and the ideal config that removes it (repeatable; "
                         "default: mis_speculating=gc_bp load_in_flight=gc_l1d)")
    ap.add_argument("--baseline", default="gc", help="config whose CPI stacks are the prediction (default: gc)")
    ap.add_argument("--name-regex", default=DEFAULT_NAME_REGEX,
                    help="regex with 'trace' and 'config' groups for the file names (default: %(default)s)")
    ap.add_argument("--glob", default="*", help="filename pattern to parse (default: all files)")
    ap.add_argument("--independent-y", action="store_true", help="give each panel its own y scale")
    args = ap.parse_args()

    pairs = DEFAULT_PAIRS
    if args.pair:
        try:
            pairs = [tuple(p.split("=", 1)) for p in args.pair]
            assert all(len(p) == 2 for p in pairs)
        except AssertionError:
            sys.exit("--pair takes CATEGORY=CONFIG, e.g. --pair load_in_flight=gc_l1d")

    df = parse_dir(args.directory, glob=args.glob, name_regex=args.name_regex)
    if df.empty or not {"trace", "config"} <= set(df.columns):
        sys.exit("no files with 'trace' and 'config' in their names; check --name-regex / --glob")

    table = compare(df, pairs=pairs, baseline=args.baseline)
    if table.empty:
        sys.exit("nothing to compare")
    if args.csv:
        table.to_csv(args.csv, index=False)
        print(f"wrote {args.csv}", file=sys.stderr)
    plot(table, output=args.output, sharey=not args.independent_y)
    print(f"wrote {args.output}", file=sys.stderr)

    summary = table.groupby(["category", "stage", "config"], sort=False)[["predicted", "actual", "diff"]].mean()
    print(summary.round(1).to_string())


if __name__ == "__main__":
    main()
