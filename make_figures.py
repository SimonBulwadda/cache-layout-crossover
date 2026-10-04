import csv
import math
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker as mticker

# ---- palette (validated: dataviz skill, categorical slots 1-3) ----
BLUE = "#2a78d6"     # Machine 1 (Lenovo)
ORANGE = "#eb6834"   # Machine 2 (GitHub Actions)
AQUA = "#1baf7a"     # indirected-scrambled, when used as a third series
INK = "#0b0b0b"
INK_SECONDARY = "#52514e"
INK_MUTED = "#898781"
GRID = "#e1e0d9"
SURFACE = "#fcfcfb"

plt.rcParams.update({
    "font.family": "sans-serif",
    "font.sans-serif": ["DejaVu Sans", "Arial", "Helvetica"],
    "figure.facecolor": SURFACE,
    "axes.facecolor": SURFACE,
    "axes.edgecolor": GRID,
    "axes.labelcolor": INK,
    "text.color": INK,
    "xtick.color": INK_MUTED,
    "ytick.color": INK_MUTED,
    "axes.grid": True,
    "grid.color": GRID,
    "grid.linewidth": 0.7,
    "axes.axisbelow": True,
})

SIZES = [32, 64, 128, 256, 512, 1024, 2048, 4096]
CACHE_LINE = 64

# ---- load derived metrics ----
with open("derived_metrics.csv") as f:
    dm = list(csv.DictReader(f))

# ---- load prefetch contribution ----
with open("prefetch_contribution.csv") as f:
    pf = list(csv.DictReader(f))

def get_dm(machine, regime, size, pattern):
    for r in dm:
        if (r["machine"] == machine and r["regime"] == regime and
                int(r["object_size_bytes"]) == size and r["access_pattern"] == pattern):
            return r
    return None

MACHINES = [("Machine1_Lenovo", "Machine 1 (Lenovo)", BLUE),
            ("Machine2_GHActions", "Machine 2 (GitHub Actions)", ORANGE)]

# ============================================================
# Figure 1: headline crossover chart -- gap ratio vs object size
# ============================================================
fig, axes = plt.subplots(1, 2, figsize=(11, 4.6), sharey=False)
regimes = [("small_pile", "Cache-resident regime (~128 KB working set)"),
           ("huge_pile", "Memory-resident regime (~128 MB working set)")]

for ax, (regime_key, regime_title) in zip(axes, regimes):
    for machine_key, machine_label, color in MACHINES:
        scrambled_ratios, scrambled_err = [], []
        tidy_ratios = []
        for size in SIZES:
            row = get_dm(machine_key, regime_key, size, "Sequential")
            if row is None:
                scrambled_ratios.append(None); scrambled_err.append(0); tidy_ratios.append(None)
                continue
            ratio = float(row["gap_ratio_scrambled"])
            cv_num = float(row["indirected_scrambled_cv"])
            cv_den = float(row["packed_cv"])
            rel_err = math.sqrt(cv_num**2 + cv_den**2)
            scrambled_ratios.append(ratio)
            scrambled_err.append(ratio * rel_err)
            tidy_ratios.append(float(row["gap_ratio_tidy"]))

        xs = SIZES
        ys = scrambled_ratios
        errs = scrambled_err
        ax.plot(xs, ys, color=color, linewidth=1.8, marker="o", markersize=4,
                label=f"{machine_label} -- scrambled (realistic)")
        lo = [max(y - e, 0.5) for y, e in zip(ys, errs)]
        hi = [y + e for y, e in zip(ys, errs)]
        ax.fill_between(xs, lo, hi, color=color, alpha=0.15, linewidth=0)
        ax.plot(xs, tidy_ratios, color=color, linewidth=1.3, linestyle="--",
                alpha=0.75, label=f"{machine_label} -- tidy (allocator-lucky)")

    ax.axvline(CACHE_LINE, color=INK_MUTED, linewidth=1, linestyle=":")
    ax.text(CACHE_LINE * 1.08, ax.get_ylim()[1] if ax.get_yscale() != "log" else None,
            "", fontsize=8)
    ax.set_xscale("log", base=2)
    ax.set_yscale("log")
    ax.set_xticks(SIZES)
    ax.get_xaxis().set_major_formatter(mticker.ScalarFormatter())
    ax.set_xticklabels([str(s) for s in SIZES], fontsize=8.5)
    ax.set_xlabel("Object size (bytes)", fontsize=9.5, color=INK_SECONDARY)
    ax.set_ylabel("Gap ratio (indirected / packed, sequential access)", fontsize=9, color=INK_SECONDARY)
    ax.set_title(regime_title, fontsize=10.5, color=INK, pad=10)
    ax.grid(True, which="major", axis="both")
    ax.grid(False, which="minor")

    # secondary top axis in cache-line units
    ax2 = ax.secondary_xaxis("top", functions=(lambda x: x / CACHE_LINE, lambda x: x * CACHE_LINE))
    ax2.set_xlabel("Object size (cache lines, 64B each)", fontsize=8, color=INK_MUTED)
    ax2.tick_params(labelsize=7.5, colors=INK_MUTED)

axes[0].legend(fontsize=7.3, frameon=False, loc="upper right")
fig.suptitle("The packed-vs-pointer advantage shrinks with object size, on two different machines",
             fontsize=12, color=INK, y=1.04)
fig.tight_layout()
fig.savefig("fig1_crossover_gap_ratio.png", dpi=180, bbox_inches="tight", facecolor=SURFACE)
plt.close(fig)
print("Wrote fig1_crossover_gap_ratio.png")

# ============================================================
# Figure 2: absolute time-per-touch, Machine 1, huge_pile -- shows the "floor"
# ============================================================
fig, ax = plt.subplots(figsize=(7.2, 5))
series = [
    ("packed_ns_per_touch", "packed_cv", "Packed (contiguous)", BLUE),
    ("indirected_tidy_ns_per_touch", "indirected_tidy_cv", "Indirected -- tidy allocation", ORANGE),
    ("indirected_scrambled_ns_per_touch", "indirected_scrambled_cv", "Indirected -- scrambled (realistic)", AQUA),
]
label_ys = {}
for val_col, cv_col, label, color in series:
    ys, errs = [], []
    for size in SIZES:
        row = get_dm("Machine1_Lenovo", "huge_pile", size, "Sequential")
        v = float(row[val_col])
        cv = float(row[cv_col])
        ys.append(v)
        errs.append(v * cv)
    ax.plot(SIZES, ys, color=color, linewidth=1.8, marker="o", markersize=4)
    lo = [max(y - e, 1) for y, e in zip(ys, errs)]
    hi = [y + e for y, e in zip(ys, errs)]
    ax.fill_between(SIZES, lo, hi, color=color, alpha=0.15, linewidth=0)
    label_ys[label] = (ys[-1], color)

# direct end-of-line labels instead of a legend box (avoids overlap, keeps identity off color-alone)
ax.set_xscale("log", base=2)
ax.set_yscale("log")
ax.set_xlim(SIZES[0] * 0.85, SIZES[-1] * 2.6)
for label, (y, color) in label_ys.items():
    ax.annotate(label, xy=(SIZES[-1], y), xytext=(8, 0), textcoords="offset points",
                fontsize=8.3, color=color, va="center", fontweight="medium")

ax.set_xticks(SIZES)
ax.get_xaxis().set_major_formatter(mticker.ScalarFormatter())
ax.set_xticklabels([str(s) for s in SIZES], fontsize=8.5)
ax.set_xlabel("Object size (bytes)", fontsize=9.5, color=INK_SECONDARY)
ax.set_ylabel("Time per touch (ns, log scale)", fontsize=9.5, color=INK_SECONDARY)
ax.set_title("Machine 1, memory-resident regime: packed keeps a floor, not zero",
             fontsize=11.5, color=INK, pad=12)
fig.tight_layout()
fig.savefig("fig2_absolute_time_floor.png", dpi=180, bbox_inches="tight", facecolor=SURFACE)
plt.close(fig)
print("Wrote fig2_absolute_time_floor.png")

# ============================================================
# Figure 3: prefetch contribution vs size, huge_pile, both machines
# ============================================================
fig, ax = plt.subplots(figsize=(7.2, 4.6))
for machine_key, machine_label, color in MACHINES:
    xs, ys = [], []
    for r in pf:
        if r["machine"] == machine_key and r["regime"] == "huge_pile":
            xs.append(int(r["object_size_bytes"]))
            ys.append(float(r["prefetch_contribution"]))
    pairs = sorted(zip(xs, ys))
    xs = [p[0] for p in pairs]; ys = [p[1] for p in pairs]
    ax.plot(xs, ys, color=color, linewidth=1.8, marker="o", markersize=4, label=machine_label)

ax.axhline(0, color=INK_MUTED, linewidth=1)
ax.set_xscale("log", base=2)
ax.set_xticks(SIZES)
ax.get_xaxis().set_major_formatter(mticker.ScalarFormatter())
ax.set_xticklabels([str(s) for s in SIZES], fontsize=8.5)
ax.set_xlabel("Object size (bytes)", fontsize=9.5, color=INK_SECONDARY)
ax.set_ylabel("prefetch_contribution\n(sequential ratio - random ratio)", fontsize=9, color=INK_SECONDARY)
ax.set_title("How much of the packed advantage is the prefetcher's doing?\n(memory-resident regime)",
             fontsize=11, color=INK, pad=12)
ax.legend(fontsize=8.5, frameon=False, loc="upper right")
fig.tight_layout()
fig.savefig("fig3_prefetch_contribution.png", dpi=180, bbox_inches="tight", facecolor=SURFACE)
plt.close(fig)
print("Wrote fig3_prefetch_contribution.png")