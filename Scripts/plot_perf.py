"""Project Rain - Rain Shield performance figure for the report, from the sweep CSVs in Docs/Perf.
    python3 Scripts/plot_perf.py        -> Docs/Perf/fig_scaling.png
Left: frame time against particle count (both systems and the hail's GPU port), with the 16.6 ms budget.
Right: Niagara CPU time (game + render thread, per-system particle perf stats) against count, with the 4 ms budget.
"""
import csv
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
PERF = os.path.join(os.path.dirname(HERE), "Docs", "Perf")

# Categorical slots 1-3 of the reference palette (validated: scripts/validate_palette.js, light surface).
SERIES = [
    ("sweep_system1_rain_gpu_sprites.csv", "System 1 rain (GPU sprites)", "#2a78d6", "o"),
    ("sweep_system2_hail_cpu_meshes.csv", "System 2 hail (CPU meshes)", "#eb6834", "s"),
    ("sweep_system2_hail_gpu_meshes_port.csv", "System 2 hail, GPU port", "#1baf7a", "^"),
]
INK = "#2b2b2a"
MUTED = "#6b6b67"
GRID = "#e6e5e0"
SURFACE = "#fcfcfb"


def load(name):
    with open(os.path.join(PERF, name)) as f:
        rows = [r for r in csv.DictReader(f) if int(r["count_requested"]) > 0]
    return rows


def style(ax, ylabel):
    ax.set_facecolor(SURFACE)
    ax.set_xscale("log")
    ax.grid(True, which="major", color=GRID, linewidth=0.6)
    ax.set_axisbelow(True)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(MUTED)
        ax.spines[side].set_linewidth(0.6)
    ax.tick_params(colors=MUTED, labelsize=7, width=0.6)
    ax.set_xlabel("particles (log scale)", color=MUTED, fontsize=7.5)
    ax.set_ylabel(ylabel, color=MUTED, fontsize=7.5)


def budget(ax, y, text, below=False):
    ax.axhline(y, color=INK, linewidth=0.9, linestyle=(0, (4, 3)))
    pad = 0.008 * (ax.get_ylim()[1] - ax.get_ylim()[0])
    ax.text(0.99, y - pad if below else y + pad, text, transform=ax.get_yaxis_transform(), ha="right", va="top" if below else "bottom", fontsize=7, color=INK)


def main():
    fig, (left, right) = plt.subplots(1, 2, figsize=(7.4, 2.7), dpi=220)
    fig.patch.set_facecolor(SURFACE)
    for name, label, color, marker in SERIES:
        rows = load(name)
        n = [int(r["count_requested"]) for r in rows]
        frame = [float(r["frame_ms"]) for r in rows]
        niagara = [float(r["niagara_gt_ms"]) + float(r["niagara_rt_ms"]) for r in rows]
        for ax, y in ((left, frame), (right, niagara)):
            ax.plot(n, y, color=color, linewidth=2, marker=marker, markersize=4.5,
                    markeredgecolor=SURFACE, markeredgewidth=1.2, label=label, zorder=3)

    style(left, "frame time (ms), 1920x1080")
    left.set_ylim(0, 100)
    budget(left, 16.6, "60 fps budget 16.6 ms", below=True)
    base = float(next(csv.DictReader(open(os.path.join(PERF, SERIES[0][0]))))["frame_ms"])
    left.axhline(base, color=MUTED, linewidth=0.6)
    left.text(0.99, base + 0.8, "no particles %.1f ms" % base, transform=left.get_yaxis_transform(), ha="right", va="bottom", fontsize=7, color=MUTED)
    left.set_ylim(0, 100)
    left.set_title("Frame time", fontsize=8.5, color=INK, loc="left")

    style(right, "Niagara GT + RT (ms)")
    right.set_ylim(0, 14)
    budget(right, 4.0, "Niagara budget 4 ms")
    right.set_title("Niagara CPU time", fontsize=8.5, color=INK, loc="left")

    handles, labels = left.get_legend_handles_labels()
    fig.legend(handles, labels, loc="lower center", ncol=3, frameon=False, fontsize=7.5, labelcolor=INK)
    fig.tight_layout(rect=(0, 0.08, 1, 1))
    out = os.path.join(PERF, "fig_scaling.png")
    fig.savefig(out, facecolor=SURFACE)
    print(out)


if __name__ == "__main__":
    main()
