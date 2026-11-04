#!/usr/bin/env python3
"""
Parse a directory of ChampSim plain-text outputs (stdout captures) into a pandas DataFrame.

One row per output file, one column per statistic. Column names are dot-separated:

    file, path, completed, error, warmup_instructions, simulation_instructions, sim_time_s, trace.cpu0
    cpu0.ipc  cpu0.instructions  cpu0.cycles  cpu0.branch_accuracy  cpu0.mpki ...
    cpu0.mpki.BRANCH_CONDITIONAL         (per-branch-type MPKI)
    cpu0.retired.BRANCH_RETURN / cpu0.taken.BRANCH_RETURN
    cpu0.stage.fetch.correct / .wrong    (pipeline stage counts)
    cpu0.cpi_stack.retire.total_slots / .width / .cpi        (total_slots/width only in slot-based outputs)
    cpu0.cpi_stack.retire.<category>.slots / .pct / .cpi     (.cycles instead of .slots in older outputs)
    cpu0_L1D.LOAD.access / .hit / .miss / .miss_merge
    cpu0_L1D.prefetch_requested, cpu0_L1D.average_miss_latency, cpu0_L1D.average_mshr_occupancy, ...
    Channel 0.rq_row_buffer_hit, Channel 0.avg_read_latency, ...

Histograms (retire/issue/dispatch width, WP instrs. pulled per episode) are stored as dicts in a single
column by default; pass --expand-histograms (or expand_histograms=True) to get one column per bucket.

Only the "Region of Interest" statistics are parsed by default. In multi-core runs ChampSim also prints
"Total Simulation Statistics"; those go into columns prefixed with "total.".

Lines this parser doesn't specifically know about but that look like "CPU <n> <label>: <number> ..." are
still picked up, as cpu<n>.<label> columns, so newly added counters show up without changing this script.

Usage:
    python3 parse_champsim_outputs.py RESULTS_DIR [-o results.csv|.parquet|.pkl] [--glob '*.txt']
                                      [--recursive] [--name-regex '(?P<config>[^.]+)\\.(?P<bench>.+)\\.txt']

As a library:
    from parse_champsim_outputs import parse_dir, parse_file
    df = parse_dir("results/", glob="*.out")
"""

import argparse
import math
import re
import sys
from pathlib import Path

import pandas as pd

NUM = r"(-|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?|nan|inf)"


def num(s):
    """Convert a ChampSim number to int/float; '-' (ChampSim's 'no denominator') becomes NaN."""
    if s is None or s == "-":
        return math.nan
    try:
        return int(s)
    except ValueError:
        return float(s)


def snake(label):
    label = re.sub(r"[^0-9A-Za-z]+", "_", label.strip()).strip("_")
    return label.lower()


# ---------------------------------------------------------------------------------------------------------------
# Line patterns
# ---------------------------------------------------------------------------------------------------------------

RE_HEADER = re.compile(r"^(Warmup Instructions|Simulation Instructions|Number of CPUs|Page size):\s*(\d+)")
RE_PHASE = re.compile(r"^=== (.+) ===$")
RE_RUNS = re.compile(r"^CPU (\d+) runs (.+)$")
RE_COMPLETE = re.compile(
    r"^Simulation complete CPU (\d+) instructions: (\d+) cycles: (\d+) cumulative IPC: " + NUM
    + r"(?: \(Simulation time: (\d+) hr (\d+) min (\d+) sec\))?"
)

# CPI stacks (printed by the listeners before the phase stats)
# Since 79ed8eb1 the stacks count slots ("slots: N (width W) cycles: ..."); older outputs count cycles only.
RE_CPI_TOTAL = re.compile(
    r"^CPU (\d+) (\w+) CPI stack: (?:slots: (\d+) \(width (\d+)\) )?cycles: (\d+) instructions: (\d+) CPI: " + NUM
)
RE_CPI_CAT = re.compile(r"^CPU (\d+) (\w+) (.+?): (slots|cycles): (\d+) \(" + NUM + r"%\) CPI: " + NUM + r"$")

# Core stats
RE_IPC = re.compile(r"^CPU (\d+) cumulative IPC: " + NUM + r" instructions: (\d+) cycles: (\d+)")
RE_BRANCH = re.compile(
    r"^CPU (\d+) Branch Prediction Accuracy: " + NUM + r"% MPKI: " + NUM + r" Average ROB Occupancy at Mispredict: " + NUM
)
RE_WP_HIST = re.compile(r"^CPU (\d+) WP instrs\. pulled per episode \(instrs:not ran out/ran out\):(.*)$")
RE_WIDTH_HIST = re.compile(r"^CPU (\d+) (\w+) width histogram \(instrs:cycles\):(.*)$")
RE_RETIRED_LS = re.compile(
    r"^CPU (\d+) retired loads: (\d+) \(memory operands: (\d+)\) stores: (\d+) \(memory operands: (\d+)\)"
)
RE_FDIP = re.compile(r"^CPU (\d+) FDIP wrong path cycles: (\d+) stalled waiting for wrong path instrs\.: (\d+)")
RE_WP_EPISODES = re.compile(r"^CPU (\d+) WP episodes: (\d+) ran out of WP instrs\.: (\d+) \(" + NUM + r"%\)")
RE_OCCUPANCY = re.compile(
    r"^CPU (\d+) average occupancy ROB: " + NUM + r" LQ: " + NUM + r" SQ: " + NUM
    + r" cycles full ROB: (\d+) LQ: (\d+) SQ: (\d+)"
)
RE_RECOVERY = re.compile(
    r"^CPU (\d+) mispredict recovery cycles: (\d+) fetch resteer cycles: (\d+) "
    r"mispredicts resolved at decode: (\d+) at execute: (\d+)"
)
RE_FORWARD = re.compile(r"^CPU (\d+) loads forwarded from an executed store: (\d+) loads waiting on an older store: (\d+)")
RE_CPU_GENERIC = re.compile(r"^CPU (\d+) (.*)$")

# Lines without a CPU prefix that belong to the most recent "CPU n" block
RE_BRANCH_TYPE_MPKI = re.compile(r"^(BRANCH_\w+): " + NUM + r"$")
RE_BRANCH_TYPE_RETIRED = re.compile(r"^(BRANCH_\w+): retired: (\d+) taken: (\d+)$")
RE_STAGE = re.compile(r"^(\w+): correct path: (\d+) wrong path: (\d+)$")

# Caches: "cpu0->cpu0_L1D LOAD         ACCESS: ..." etc.
RE_CACHE = re.compile(r"^cpu(\d+)->(\S+) (.*)$")
RE_CACHE_HITMISS = re.compile(
    r"^(\w+)\s+ACCESS:\s*(\d+) HIT:\s*(\d+) MISS:\s*(\d+)(?: MISS_MERGE:\s*(\d+))?"
)

# DRAM: "Channel 0 RQ ROW_BUFFER_HIT: ..." followed by indented continuation lines
RE_CHANNEL = re.compile(r"^(Channel \d+) (.*)$")
RE_CONTINUATION = re.compile(r"^\s+(\S.*)$")

# Generic "LABEL: value" pairs, e.g. "AVERAGE MSHR OCCUPANCY: 0.77 MSHR FULL CYCLES: 0"
RE_KV = re.compile(r"([A-Za-z][A-Za-z0-9_ .()/-]*?):\s*" + NUM + r"%?")


def kv_pairs(text):
    """Yield (snake_label, value) for every 'LABEL: number' in text."""
    text = re.sub(r"(\d|-) cycles\b", r"\1", text)  # drop units so they don't leak into the next label
    for m in RE_KV.finditer(text):
        yield snake(m.group(1)), num(m.group(2))


def parse_hist(text):
    """' 0:10 1:20' -> {0: 10, 1: 20}"""
    return {int(k): int(v) for k, v in re.findall(r"(\d+):(\d+)", text)}


def parse_wp_hist(text):
    """' 0:1/2 5:0/3' -> {0: (1, 2), 5: (0, 3)}  as (not ran out, ran out)"""
    return {int(n): (int(a), int(b)) for n, a, b in re.findall(r"(\d+):(\d+)/(\d+)", text)}


# ---------------------------------------------------------------------------------------------------------------
# Parser
# ---------------------------------------------------------------------------------------------------------------


def parse_file(path, expand_histograms=False):
    """Parse one ChampSim output file into a flat dict of column -> value."""
    path = Path(path)
    row = {"file": path.name, "path": str(path), "completed": False, "error": None}

    num_cpus = 1
    in_stats = False  # inside a "Region of Interest" / "Total Simulation" block
    prefix = ""  # "" for ROI, "total." for the multi-core whole-simulation block
    cpu = None  # CPU of the most recent "CPU n ..." stat line, for lines that omit it
    section = None  # last unprefixed heading ("Branch type MPKI", "Retired instruction mix", ...)
    channel = None  # last DRAM channel + queue, for the indented continuation lines

    def put(key, value):
        row[prefix + key] = value

    def put_hist(key, hist):
        if expand_histograms:
            for k, v in hist.items():
                if isinstance(v, tuple):
                    put(f"{key}.{k}.not_ran_out", v[0])
                    put(f"{key}.{k}.ran_out", v[1])
                else:
                    put(f"{key}.{k}", v)
        else:
            put(key, hist)

    def cache_name(cpu_idx, name):
        # Private caches are already named "cpuN_..."; shared ones (LLC) need the requesting CPU in multi-core runs
        if name.startswith(f"cpu{cpu_idx}_") or num_cpus == 1:
            return name
        return f"cpu{cpu_idx}->{name}"

    with open(path, errors="replace") as f:
        for raw in f:
            line = raw.rstrip("\n")
            if not line:
                continue

            # ---- run header / progress lines (outside the stats blocks) ----
            if not in_stats:
                if m := RE_HEADER.match(line):
                    key = snake(m.group(1))
                    row[key] = int(m.group(2))
                    if key == "number_of_cpus":
                        num_cpus = int(m.group(2))
                    continue
                if m := RE_COMPLETE.match(line):
                    c = m.group(1)
                    row[f"cpu{c}.sim_complete_instructions"] = int(m.group(2))
                    row[f"cpu{c}.sim_complete_cycles"] = int(m.group(3))
                    if m.group(5) is not None:
                        row["sim_time_s"] = int(m.group(5)) * 3600 + int(m.group(6)) * 60 + int(m.group(7))
                    continue
                if m := RE_CPI_TOTAL.match(line):
                    c, stage = m.group(1), m.group(2)
                    k = f"cpu{c}.cpi_stack.{stage}"
                    if m.group(3) is not None:
                        row[f"{k}.total_slots"] = int(m.group(3))
                        row[f"{k}.width"] = int(m.group(4))
                    row[f"{k}.cycles"] = int(m.group(5))
                    row[f"{k}.instructions"] = int(m.group(6))
                    row[f"{k}.cpi"] = num(m.group(7))
                    continue
                if m := RE_CPI_CAT.match(line):
                    c, stage, cat = m.group(1), m.group(2), snake(m.group(3))
                    k = f"cpu{c}.cpi_stack.{stage}.{cat}"
                    row[f"{k}.{m.group(4)}"] = int(m.group(5))  # .slots or .cycles, depending on the output's age
                    row[f"{k}.pct"] = num(m.group(6))
                    row[f"{k}.cpi"] = num(m.group(7))
                    continue
                if line.startswith("DEADLOCK!") or line.startswith("terminate called"):
                    row["error"] = row["error"] or line.strip()
                    continue

            if line.startswith("ChampSim completed all phases"):
                row["completed"] = True
                in_stats = False
                continue

            if m := RE_PHASE.match(line):
                row["phase"] = m.group(1)
                in_stats = False
                continue
            if m := RE_RUNS.match(line):
                row[f"trace.cpu{m.group(1)}"] = m.group(2).strip()
                continue
            if line == "Region of Interest Statistics":
                in_stats, prefix, cpu, section = True, "", None, None
                continue
            if line.startswith("Total Simulation Statistics"):
                in_stats, prefix, cpu, section = True, "total.", None, None
                continue

            if not in_stats:
                continue

            # ---- stats block ----
            if line in ("Branch type MPKI", "Pipeline stage counts", "Retired instruction mix", "Core activity"):
                section = line
                continue

            if m := RE_CACHE.match(line):
                c, name, rest = m.group(1), cache_name(m.group(1), m.group(2)), m.group(3)
                if hm := RE_CACHE_HITMISS.match(rest):
                    t = hm.group(1)
                    put(f"{name}.{t}.access", int(hm.group(2)))
                    put(f"{name}.{t}.hit", int(hm.group(3)))
                    put(f"{name}.{t}.miss", int(hm.group(4)))
                    if hm.group(5) is not None:
                        put(f"{name}.{t}.miss_merge", int(hm.group(5)))
                    continue
                if rest.startswith("PREFETCH REQUESTED"):
                    for k, v in kv_pairs(rest):
                        put(f"{name}.{k if k.startswith('prefetch') else 'prefetch_' + k}", v)
                    continue
                if rest.startswith("EVICTIONS"):
                    for k, v in kv_pairs(rest):
                        put(f"{name}.{'evictions_dirty' if k == 'dirty' else k}", v)
                    continue
                for k, v in kv_pairs(rest):
                    put(f"{name}.{k}", v)
                continue

            if m := RE_CHANNEL.match(line):
                ch, rest = m.group(1), m.group(2)
                q = re.match(r"^(RQ|WQ) (.*)$", rest)
                if q:
                    channel = f"{ch}.{q.group(1).lower()}"
                    for k, v in kv_pairs(q.group(2)):
                        put(f"{channel}_{k}", v)
                else:
                    channel = None
                    for k, v in kv_pairs(rest):
                        put(f"{ch}.{k}", v)
                continue
            if channel and (m := RE_CONTINUATION.match(line)):
                ch, q = channel.split(".")
                for k, v in kv_pairs(m.group(1)):
                    put(f"{ch}.{q}_{k}", v)
                continue

            if m := RE_IPC.match(line):
                cpu = m.group(1)
                put(f"cpu{cpu}.ipc", num(m.group(2)))
                put(f"cpu{cpu}.instructions", int(m.group(3)))
                put(f"cpu{cpu}.cycles", int(m.group(4)))
                section = None
                continue
            if m := RE_BRANCH.match(line):
                cpu = m.group(1)
                put(f"cpu{cpu}.branch_accuracy", num(m.group(2)))
                put(f"cpu{cpu}.mpki", num(m.group(3)))
                put(f"cpu{cpu}.avg_rob_occupancy_at_mispredict", num(m.group(4)))
                continue
            if m := RE_WP_HIST.match(line):
                cpu = m.group(1)
                put_hist(f"cpu{cpu}.wp_pulled_per_episode_hist", parse_wp_hist(m.group(2)))
                continue
            if m := RE_WIDTH_HIST.match(line):
                cpu = m.group(1)
                put_hist(f"cpu{cpu}.{m.group(2)}_width_hist", parse_hist(m.group(3)))
                continue
            if m := RE_FDIP.match(line):
                cpu = m.group(1)
                put(f"cpu{cpu}.fdip_wp_cycles", int(m.group(2)))
                put(f"cpu{cpu}.fdip_wp_stalled_cycles", int(m.group(3)))
                continue
            if m := RE_WP_EPISODES.match(line):
                cpu = m.group(1)
                put(f"cpu{cpu}.wp_episodes", int(m.group(2)))
                put(f"cpu{cpu}.wp_episodes_ran_out", int(m.group(3)))
                put(f"cpu{cpu}.wp_episodes_ran_out_pct", num(m.group(4)))
                continue
            if m := RE_OCCUPANCY.match(line):
                cpu = m.group(1)
                for i, q in enumerate(("rob", "lq", "sq")):
                    put(f"cpu{cpu}.avg_occupancy_{q}", num(m.group(2 + i)))
                    put(f"cpu{cpu}.cycles_full_{q}", int(m.group(5 + i)))
                continue
            if m := RE_RECOVERY.match(line):
                cpu = m.group(1)
                put(f"cpu{cpu}.mispredict_recovery_cycles", int(m.group(2)))
                put(f"cpu{cpu}.fetch_resteer_cycles", int(m.group(3)))
                put(f"cpu{cpu}.mispredicts_resolved_at_decode", int(m.group(4)))
                put(f"cpu{cpu}.mispredicts_resolved_at_execute", int(m.group(5)))
                continue
            if m := RE_FORWARD.match(line):
                cpu = m.group(1)
                put(f"cpu{cpu}.loads_forwarded_from_store", int(m.group(2)))
                put(f"cpu{cpu}.loads_waiting_on_older_store", int(m.group(3)))
                continue
            if m := RE_RETIRED_LS.match(line):
                cpu = m.group(1)
                put(f"cpu{cpu}.retired_loads", int(m.group(2)))
                put(f"cpu{cpu}.retired_load_mem_operands", int(m.group(3)))
                put(f"cpu{cpu}.retired_stores", int(m.group(4)))
                put(f"cpu{cpu}.retired_store_mem_operands", int(m.group(5)))
                continue

            # Unprefixed lines in a CPU's block
            if cpu is not None:
                if m := RE_BRANCH_TYPE_RETIRED.match(line):
                    put(f"cpu{cpu}.retired.{m.group(1)}", int(m.group(2)))
                    put(f"cpu{cpu}.taken.{m.group(1)}", int(m.group(3)))
                    continue
                if section == "Branch type MPKI" and (m := RE_BRANCH_TYPE_MPKI.match(line)):
                    put(f"cpu{cpu}.mpki.{m.group(1)}", num(m.group(2)))
                    continue
                if section == "Pipeline stage counts" and (m := RE_STAGE.match(line)):
                    put(f"cpu{cpu}.stage.{m.group(1)}.correct", int(m.group(2)))
                    put(f"cpu{cpu}.stage.{m.group(1)}.wrong", int(m.group(3)))
                    continue

            # Fallback: any other "CPU n label: value ..." line. Labels are made unique by chaining each one onto
            # the first label of the line, e.g. "retired branches: 5 taken: 3" -> retired_branches, retired_branches_taken
            if m := RE_CPU_GENERIC.match(line):
                cpu = m.group(1)
                pairs = list(kv_pairs(m.group(2)))
                if pairs:
                    first = pairs[0][0]
                    for i, (k, v) in enumerate(pairs):
                        put(f"cpu{cpu}.{k if i == 0 else first + '_' + k}", v)
                continue

    return row


def parse_dir(directory, glob="*", recursive=False, expand_histograms=False, name_regex=None):
    """Parse every matching file under directory into a DataFrame (one row per file)."""
    directory = Path(directory)
    files = sorted(directory.rglob(glob) if recursive else directory.glob(glob))
    files = [p for p in files if p.is_file()]
    pattern = re.compile(name_regex) if name_regex else None

    rows = []
    for p in files:
        try:
            row = parse_file(p, expand_histograms=expand_histograms)
        except Exception as e:  # keep going; one bad file shouldn't sink the whole directory
            row = {"file": p.name, "path": str(p), "completed": False, "error": f"parse error: {e}"}
        if pattern and (m := pattern.search(p.name)):
            row.update(m.groupdict())
        rows.append(row)

    df = pd.DataFrame(rows)
    if df.empty:
        return df

    # Put identifying columns first
    front = ["file"] + (list(pattern.groupindex) if pattern else [])
    front += [c for c in df.columns if c.startswith("trace.")]
    front += ["completed", "error", "warmup_instructions", "simulation_instructions", "sim_time_s", "path"]
    front = [c for c in dict.fromkeys(front) if c in df.columns]
    return df[front + [c for c in df.columns if c not in front]]


# ---------------------------------------------------------------------------------------------------------------
# Plotting
# ---------------------------------------------------------------------------------------------------------------

BAR_COLOR = "#2a78d6"
TEXT_PRIMARY = "#0b0b0b"
TEXT_SECONDARY = "#52514e"
GRID_COLOR = "#e4e3df"


def short_trace_name(trace):
    """'028.0.600.perlbench_s-119_9B' -> 'perlbench_s-119_9B'"""
    return re.sub(r"^\d+\.\d+\.\d+\.", "", str(trace))


def cycles_removed(df, baseline="gc", trace_col="trace", config_col="config", cycles_col="cpu0.cycles"):
    """
    Percent of cycles removed relative to the baseline config, as a trace x config table:
        100 * (baseline_cycles - config_cycles) / baseline_cycles
    Positive = fewer cycles than the baseline. Runs that didn't complete are left out (NaN).
    """
    done = df[df["completed"].astype(bool)]
    cycles = done.pivot_table(index=trace_col, columns=config_col, values=cycles_col, aggfunc="first")
    if baseline not in cycles.columns:
        raise ValueError(f"no completed runs for baseline config {baseline!r}")

    # Cycles are only comparable if every run covered the same instructions
    instr_col = cycles_col.replace(".cycles", ".instructions")
    if instr_col in done.columns:
        instrs = done.pivot_table(index=trace_col, columns=config_col, values=instr_col, aggfunc="first")
        drift = (instrs.sub(instrs[baseline], axis=0).abs().div(instrs[baseline], axis=0) > 1e-3).stack()
        if drift.any():
            pairs = ", ".join(f"{t}:{c}" for t, c in drift[drift].index[:5])
            print(f"warning: instruction counts differ from {baseline} by >0.1% for {pairs} ...", file=sys.stderr)

    # Keep configs with no finished runs yet, so they show up as all-NaN rather than vanishing
    all_configs = sorted(c for c in df[config_col].dropna().unique() if c != baseline)
    base = cycles[baseline]
    return cycles.drop(columns=baseline).reindex(columns=all_configs).rsub(base, axis=0).div(base, axis=0) * 100


def plot_cycles_removed(df, baseline="gc", output=None, separate=False, ncols=4, sharey=True, sort=True, **kwargs):
    """
    One bar chart per config: trace on the x axis, % of cycles removed vs. the baseline config on the y axis.

    By default all panels share one y scale so the configs can be compared; sharey=False gives each panel
    its own scale, which makes configs that differ by well under 1% readable. With separate=True each config is
    saved to its own file, named after output with the config name added (results.png -> results_gc_l1d.png).
    With sort=True (the default) each panel orders its bars from least to greatest, with unfinished runs last;
    sort=False keeps the traces in name order.
    Returns (figure or list of figures, the trace x config table that was plotted).
    """
    import matplotlib

    if output:
        matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.ticker import FuncFormatter

    table = cycles_removed(df, baseline=baseline, **kwargs)
    table.index = [short_trace_name(t) for t in table.index]
    table = table.sort_index()
    configs = sorted(table.columns)

    lo, hi = min(0.0, table.min().min()), max(0.0, table.max().max())
    pad = 0.08 * (hi - lo or 1)
    ylim = (lo - pad if lo < 0 else 0, hi + pad)

    def draw(ax, config):
        vals = table[config].sort_values(na_position="last", kind="stable") if sort else table[config]
        x = range(len(vals))
        ax.bar(x, vals.fillna(0), width=0.6, color=BAR_COLOR, zorder=2)
        for i, v in enumerate(vals):
            if pd.isna(v):  # run missing or not finished: say so instead of drawing a zero
                ax.text(i, 0, "n/a", ha="center", va="bottom", fontsize=7, color=TEXT_SECONDARY, rotation=90)
        ax.axhline(0, color=TEXT_SECONDARY, linewidth=0.8, zorder=3)
        if sharey:
            ax.set_ylim(*ylim)
        ax.set_xticks(list(x))
        ax.set_xticklabels(vals.index, rotation=60, ha="right", fontsize=7, color=TEXT_SECONDARY)
        ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v:g}%"))
        ax.tick_params(axis="y", labelsize=8, colors=TEXT_SECONDARY, length=0)
        ax.tick_params(axis="x", length=0)
        ax.grid(axis="y", color=GRID_COLOR, linewidth=0.8, zorder=0)
        for side in ("top", "right", "left"):
            ax.spines[side].set_visible(False)
        ax.spines["bottom"].set_visible(False)
        n = vals.notna().sum()
        mean = f"mean {vals.mean() + 0.0:.1f}%" if n else "no finished runs"
        ax.set_title(f"{config}   {mean}  ({n}/{len(vals)} traces)".replace("-0.0%", "0.0%"), fontsize=10,
                     color=TEXT_PRIMARY, loc="left")

    ylabel = f"% of cycles removed vs. {baseline}"

    if separate:
        figs = []
        for config in configs:
            fig, ax = plt.subplots(figsize=(7, 4), layout="constrained")
            draw(ax, config)
            ax.set_ylabel(ylabel, fontsize=9, color=TEXT_SECONDARY)
            if output:
                p = Path(output)
                p.parent.mkdir(parents=True, exist_ok=True)
                fig.savefig(p.with_name(f"{p.stem}_{config}{p.suffix or '.png'}"), dpi=150)
                plt.close(fig)
            figs.append(fig)
        return figs, table

    nrows = math.ceil(len(configs) / ncols)
    fig, axes = plt.subplots(nrows, ncols, figsize=(4.2 * ncols, 3.6 * nrows), sharey=sharey, layout="constrained",
                             squeeze=False)
    for ax, config in zip(axes.flat, configs):
        draw(ax, config)
    for ax in axes.flat[len(configs):]:
        ax.set_visible(False)
    for row in axes:
        row[0].set_ylabel(ylabel, fontsize=9, color=TEXT_SECONDARY)
    if output:
        Path(output).parent.mkdir(parents=True, exist_ok=True)
        fig.savefig(output, dpi=150)
        plt.close(fig)
    return fig, table


CPI_STAGES = ("dispatch", "issue", "retire")  # pipeline order, left to right
USEFUL_CATEGORIES = {"retiring", "dispatching", "issuing"}
# Segments are coloured by family; individual stall reasons are told apart by their labels, not by colour
FAMILY_COLORS = {
    "useful": "#2a78d6",
    "mis_speculating": "#eb6834",
    "stalled": ("#6250d6", "#9085e9"),  # alternated so neighbouring sub-categories stay distinct
    "drained": ("#a3a29c", "#c8c7c1"),
}
FAMILY_LABELS = {"useful": "useful work", "mis_speculating": "mis-speculating", "stalled": "stalled", "drained": "drained"}


def cpi_stack(row, stage, cpu=0, min_pct=2.0):
    """
    The leaf categories of one CPI stack as [(label, family, cpi), ...] in the order ChampSim prints them.

    A category with sub-categories (e.g. 'stalled' -> 'stalled_waiting_on_cache', ...) is replaced by them, at any depth
    (retire's 'stalled_other' -> 'stalled_other_executing', ...), labelled with their parents' names. Leaves under min_pct
    of the stack's total CPI are summed into one 'rest of <parent>' segment.
    """
    prefix = f"cpu{cpu}.cpi_stack.{stage}."
    cats = [c[len(prefix):-len(".cpi")] for c in row.index
            if c.startswith(prefix) and c.endswith(".cpi") and c.count(".") == 4 and pd.notna(row[c])]
    total = row.get(f"{prefix}cpi", math.nan)

    def family(cat):
        if cat in USEFUL_CATEGORIES:
            return "useful"
        return next((f for f in ("mis_speculating", "stalled", "drained") if cat == f or cat.startswith(f + "_")), "stalled")

    def parent_of(cat):
        # the deepest category cat is nested under (names nest by prefix: stalled > stalled_other > stalled_other_executing)
        return max((p for p in cats if cat.startswith(p + "_")), key=len, default=None)

    def label_of(cat):
        parent = parent_of(cat)
        if parent is None:
            return cat.replace("_", " ")
        if parent_of(parent) is None:
            return cat.replace("_", " ")  # 'stalled waiting on cache', 'drained other', ...
        return f"{label_of(parent)} \u2013 {cat[len(parent) + 1:].replace('_', ' ')}"  # 'stalled other – executing'

    def pretty(label):
        return label.replace("mis speculating", "mis-speculating").replace("rob", "ROB").replace("lq", "LQ").replace("sq", "SQ")

    leaves, folded = [], {}
    for cat in cats:
        if any(c.startswith(cat + "_") for c in cats):
            continue  # drawn as its children
        cpi = float(row[f"{prefix}{cat}.cpi"])
        parent = parent_of(cat)
        if parent and total > 0 and 100 * cpi / total < min_pct:
            folded[parent] = folded.get(parent, 0.0) + cpi
            continue
        leaves.append((cat, pretty(label_of(cat)), family(cat), cpi))
    for parent, cpi in folded.items():
        if cpi <= 0:
            continue
        # put the folded remainder right after its siblings
        idx = max((i for i, l in enumerate(leaves) if l[0].startswith(parent + "_")),
                  default=max((i for i, l in enumerate(leaves) if l[2] == family(parent)), default=len(leaves) - 1)) + 1
        leaves.insert(idx, (parent, pretty(f"rest of {label_of(parent)}"), family(parent), cpi))
    segments = [(label, fam, cpi) for _, label, fam, cpi in leaves]
    return segments, total


def _spread_labels(ys, gap, lo, hi):
    """Move label y positions apart by at least gap while keeping them inside [lo, hi] and in order."""
    ys = list(ys)
    for i in range(1, len(ys)):
        ys[i] = max(ys[i], ys[i - 1] + gap)
    if ys and ys[-1] > hi:
        ys[-1] = hi
        for i in range(len(ys) - 2, -1, -1):
            ys[i] = min(ys[i], ys[i + 1] - gap)
    if ys and ys[0] < lo:
        shift = lo - ys[0]
        ys = [y + shift for y in ys]
    return ys


def _cpi_runs(df, config, trace_col, config_col):
    runs = df[df["completed"].astype(bool)]
    if config_col in runs.columns and config is not None:
        runs = runs[runs[config_col] == config]
    if trace_col not in runs.columns:
        trace_col = "file"
    if runs.empty:
        raise ValueError(f"no completed runs for config {config!r}")
    out = []
    for _, row in runs.sort_values(trace_col).iterrows():
        stacks = {s: cpi_stack(row, s, CPU_FOR_STACKS[0], CPU_FOR_STACKS[1]) for s in CPI_STAGES}
        stacks = {s: v for s, v in stacks.items() if v[0]}
        if not stacks:
            print(f"warning: {row['file']} has no CPI stacks, skipping", file=sys.stderr)
            continue
        out.append((short_trace_name(row[trace_col]), stacks))
    return out


CPU_FOR_STACKS = (0, 2.0)  # (cpu, min_pct), set by the plotting functions before _cpi_runs


def _draw_cpi_stacks(ax, stacks, top, min_pct, fontsize=8, label_gap=0.045, spacing=2.7, label_room=2.4):
    """Draw the stacks of one trace onto ax, with direct labels to the right of each bar."""
    width = 0.55
    gap = label_gap * top  # label spacing, in data units
    for i, (stage, (segs, total)) in enumerate(stacks.items()):
        x, bottom, labels = i * spacing, 0.0, []
        shade = {}
        for label, fam, cpi in segs:
            color = FAMILY_COLORS[fam]
            if isinstance(color, tuple):
                color = color[shade.get(fam, 0) % 2]
                shade[fam] = shade.get(fam, 0) + 1
            ax.bar(x, cpi, width, bottom=bottom, color=color, edgecolor="white", linewidth=1.2, zorder=2)
            if total > 0 and 100 * cpi / total >= min_pct:
                labels.append((bottom + cpi / 2, f"{label}  {cpi:.3f} ({100 * cpi / total:.0f}%)"))
            bottom += cpi

        ys = _spread_labels([y for y, _ in labels], gap, gap / 2, top * 1.02)
        lx = x + width / 2 + 0.12
        for (y0, text), y in zip(labels, ys):
            ax.plot([x + width / 2 + 0.02, lx - 0.03], [y0, y], color=TEXT_SECONDARY, linewidth=0.6, zorder=1)
            ax.text(lx, y, text, va="center", fontsize=fontsize, color=TEXT_PRIMARY)

    ax.set_xticks([i * spacing for i in range(len(stacks))])
    ax.set_xticklabels(list(stacks), fontsize=fontsize + 2, color=TEXT_PRIMARY)
    ax.set_xlim(-width, (len(stacks) - 1) * spacing + label_room)
    ax.set_ylim(0, top * 1.08)
    ax.tick_params(axis="y", labelsize=fontsize, colors=TEXT_SECONDARY, length=0)
    ax.tick_params(axis="x", length=0)
    ax.grid(axis="y", color=GRID_COLOR, linewidth=0.8, zorder=0)
    for side in ("top", "right", "left"):
        ax.spines[side].set_visible(False)
    ax.spines["bottom"].set_color(TEXT_SECONDARY)


def _cpi_legend(fig, all_stacks, fontsize=8):
    from matplotlib.patches import Patch

    families = {f for stacks in all_stacks for segs, _ in stacks.values() for _, f, _ in segs}
    handles = [Patch(color=FAMILY_COLORS[f][0] if isinstance(FAMILY_COLORS[f], tuple) else FAMILY_COLORS[f], label=FAMILY_LABELS[f])
               for f in FAMILY_LABELS if f in families]
    fig.legend(handles=handles, loc="outside upper right", ncols=len(handles), frameon=False, fontsize=fontsize)


def _stack_title(trace, config, stacks):
    cpi_total = next(iter(stacks.values()))[1]
    return f"{trace}" + (f"  ({config})" if config else "") + f"   CPI {cpi_total:.3f}"


def _stack_top(stacks):
    return max(sum(c for *_, c in segs) for segs, _ in stacks.values())


def plot_cpi_stacks(df, config="gc", out_dir=None, cpu=0, min_pct=2.0, trace_col="trace", config_col="config"):
    """
    One figure per trace with the dispatch, issue and retire CPI stacks side by side, in CPI units (so all three
    bars reach the same height: the run's CPI). Uses the given config's runs when the DataFrame has a config
    column. With out_dir, saves out_dir/cpi_stack_<trace>.png for each trace. Returns {trace: figure}.
    """
    import matplotlib

    if out_dir:
        matplotlib.use("Agg")
        Path(out_dir).mkdir(parents=True, exist_ok=True)
    import matplotlib.pyplot as plt

    global CPU_FOR_STACKS
    CPU_FOR_STACKS = (cpu, min_pct)
    figs = {}
    for trace, stacks in _cpi_runs(df, config, trace_col, config_col):
        fig, ax = plt.subplots(figsize=(10, 5.5), layout="constrained")
        _draw_cpi_stacks(ax, stacks, _stack_top(stacks), min_pct)
        ax.set_ylabel("CPI", fontsize=9, color=TEXT_SECONDARY)
        ax.set_title(_stack_title(trace, config, stacks), fontsize=11, color=TEXT_PRIMARY, loc="left")
        _cpi_legend(fig, [stacks])
        if out_dir:
            fig.savefig(Path(out_dir) / f"cpi_stack_{trace}.png", dpi=150)
            plt.close(fig)
        figs[trace] = fig
    return figs


def plot_cpi_stack_summary(df, config="gc", output=None, cpu=0, min_pct=2.0, ncols=4, sharey=True,
                           trace_col="trace", config_col="config"):
    """
    Every trace's CPI stacks (dispatch, issue, retire) in one grid figure, one panel per trace. Panels share one
    CPI scale by default so traces can be compared; sharey=False scales each panel to its own CPI.
    Returns the figure.
    """
    import matplotlib

    if output:
        matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    global CPU_FOR_STACKS
    CPU_FOR_STACKS = (cpu, min_pct)
    runs = _cpi_runs(df, config, trace_col, config_col)
    shared_top = max(_stack_top(stacks) for _, stacks in runs)

    nrows = math.ceil(len(runs) / ncols)
    fig, axes = plt.subplots(nrows, ncols, figsize=(7.6 * ncols, 4.4 * nrows), sharey=sharey,
                             layout="constrained", squeeze=False)
    for ax, (trace, stacks) in zip(axes.flat, runs):
        top = shared_top if sharey else _stack_top(stacks)
        _draw_cpi_stacks(ax, stacks, top, min_pct, fontsize=6.5, label_gap=0.05, spacing=3.5, label_room=3.0)
        ax.set_title(_stack_title(trace, None, stacks), fontsize=10, color=TEXT_PRIMARY, loc="left")
        if sharey:
            ax.yaxis.set_tick_params(labelleft=True)
    for ax in axes.flat[len(runs):]:
        ax.set_visible(False)
    for row in axes:
        row[0].set_ylabel("CPI", fontsize=9, color=TEXT_SECONDARY)
    if config:
        fig.suptitle(f"CPI stacks ({config})", fontsize=13, color=TEXT_PRIMARY, x=0.01, ha="left")
    _cpi_legend(fig, [stacks for _, stacks in runs], fontsize=9)

    if output:
        Path(output).parent.mkdir(parents=True, exist_ok=True)
        fig.savefig(output, dpi=150)
        plt.close(fig)
    return fig


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("directory", help="directory containing ChampSim stdout captures")
    ap.add_argument("-o", "--output", help="write the DataFrame to .csv, .parquet, .pkl or .json")
    ap.add_argument("--glob", default="*", help="filename pattern to parse (default: all files)")
    ap.add_argument("-r", "--recursive", action="store_true", help="search subdirectories too")
    ap.add_argument("--expand-histograms", action="store_true", help="one column per histogram bucket")
    ap.add_argument("--name-regex", help="regex with named groups to pull columns out of each filename")
    ap.add_argument("--plot", metavar="FILE.png",
                    help="plot %% of cycles removed per config vs. --baseline (needs 'config' and 'trace' columns, "
                         "e.g. from --name-regex '^(?P<trace>.+)\\.cst_bin:(?P<config>\\w+)$')")
    ap.add_argument("--baseline", default="gc", help="config the others are normalized to (default: gc)")
    ap.add_argument("--separate", action="store_true", help="with --plot, write one file per config")
    ap.add_argument("--no-sort", action="store_true", help="with --plot, keep traces in name order instead of by value")
    ap.add_argument("--independent-y", action="store_true", help="with --plot / --cpi-summary, give each panel its own y scale")
    ap.add_argument("--cpi-stacks", metavar="DIR", help="write one CPI-stack figure per trace into DIR")
    ap.add_argument("--cpi-summary", metavar="FILE.png", help="write every trace's CPI stacks into one grid figure")
    ap.add_argument("--stack-config", default="gc",
                    help="with --cpi-stacks, which config's runs to plot (default: gc; ignored without a config column)")
    args = ap.parse_args()

    df = parse_dir(args.directory, args.glob, args.recursive, args.expand_histograms, args.name_regex)
    if df.empty:
        sys.exit(f"no files matching {args.glob!r} in {args.directory}")

    print(f"parsed {len(df)} files, {len(df.columns)} columns, {int(df['completed'].sum())} completed", file=sys.stderr)

    if args.output:
        out = Path(args.output)
        if out.suffix == ".csv":
            df.to_csv(out, index=False)
        elif out.suffix == ".parquet":
            df.to_parquet(out, index=False)
        elif out.suffix in (".pkl", ".pickle"):
            df.to_pickle(out)
        elif out.suffix == ".json":
            df.to_json(out, orient="records", indent=1)
        else:
            sys.exit(f"unknown output format {out.suffix!r}")
        print(f"wrote {out}", file=sys.stderr)
    if args.plot:
        missing = {"trace", "config"} - set(df.columns)
        if missing:
            sys.exit(f"--plot needs {sorted(missing)} columns; pass --name-regex with those named groups")
        plot_cycles_removed(df, baseline=args.baseline, output=args.plot, separate=args.separate,
                            sharey=not args.independent_y, sort=not args.no_sort)
        print(f"wrote {args.plot}" + (" (one file per config)" if args.separate else ""), file=sys.stderr)
    if args.cpi_stacks:
        figs = plot_cpi_stacks(df, config=args.stack_config, out_dir=args.cpi_stacks)
        print(f"wrote {len(figs)} CPI-stack figures to {args.cpi_stacks}", file=sys.stderr)
    if args.cpi_summary:
        plot_cpi_stack_summary(df, config=args.stack_config, output=args.cpi_summary, sharey=not args.independent_y)
        print(f"wrote {args.cpi_summary}", file=sys.stderr)
    if not args.output and not args.plot and not args.cpi_stacks and not args.cpi_summary:
        summary = [c for c in df.columns if c in ("file", "completed") or c.endswith(".ipc")]
        print(df[summary].to_string(index=False))


if __name__ == "__main__":
    main()
