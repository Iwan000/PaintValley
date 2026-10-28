"""Project Rain - Paint Valley performance data and figure for the report, from Scripts/paint_sweep.sh's lines.
    python3 Scripts/paint_perf.py   -> Docs/Perf/paint_sweep.csv, Docs/Perf/paint_fig_scaling.png
Reads Saved/Sweeps/PaintMeasure.txt after its last "# ==== full sweep" marker; a later line for the same system and
count replaces an earlier one (a rerun).
Left: frame time against particle count (System 1, System 2 on the CPU and its GPU port), with the 16.6 ms budget.
Right: the played system's Niagara CPU time (game + render thread, the engine's per-system particle perf stats), with
the 4 ms budget.
"""
import csv
import os
import re

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PERF = os.path.join(ROOT, "Docs", "Perf")
SOURCE = os.path.join(ROOT, "Saved", "Sweeps", "PaintMeasure.txt")

# Categorical slots 1-3 of the reference palette, as in Scripts/plot_perf.py.
SERIES = [
    ("rain", "System 1 rain (GPU sprites)", "#2a78d6", "o"),
    ("balls_cpu", "System 2 paint balls (CPU meshes)", "#eb6834", "s"),
    ("balls_gpu", "System 2 paint balls, GPU port", "#1baf7a", "^"),
]
INK = "#2b2b2a"
MUTED = "#6b6b67"
GRID = "#e6e5e0"
SURFACE = "#fcfcfb"
FIELDS = ["system", "count", "live", "frames", "frame_ms", "game_ms", "render_ms", "gpu_ms", "niagara_gt_ms", "niagara_rt_ms", "coverage", "pause_idle"]


def read_rows():
    text = open(SOURCE).read()
    text = text[text.rfind("# ==== full sweep"):]
    rows = {}
    mode = ""
    for line in text.splitlines():
        if line.startswith("# "):
            mode = line.split()[1]
            continue
        if not line.startswith("PAINTMEASURE") or "niagara_gt_ms" not in line or mode in ("capture", "check"):
            continue
        v = dict(re.findall(r"(\w+)=([\w.]+)", line))
        system = "rain" if v["system"] == "1" else "balls_" + v["system"][1:]
        count = int(v["drops"] if system == "rain" else v["balls"])
        pause = "-"
        if mode == "pause":
            system, pause = "pause", line_pause(text, line)
        row = {
            "system": system, "count": count, "live": int(v.get("live", -1)), "frames": int(v["frames"]), "frame_ms": float(v["frame_ms"]),
            "game_ms": float(v["game_ms"]), "render_ms": float(v["render_ms"]), "gpu_ms": float(v["gpu_ms"]),
            "niagara_gt_ms": float(v["niagara_gt_ms"]), "niagara_rt_ms": float(v["niagara_rt_ms"]),
            "coverage": float(v["coverage"]), "pause_idle": pause,
        }
        rows[(system, count, pause)] = row
    return sorted(rows.values(), key=lambda r: (r["system"], r["count"], r["pause_idle"]))


def line_pause(text, line):
    # The marker just above the line: "# pause pause=1 hh:mm:ss".
    head = text[:text.index(line)].rstrip().splitlines()[-1]
    return head.split("pause=")[1].split()[0] if "pause=" in head else "-"


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
    rows = read_rows()
    os.makedirs(PERF, exist_ok=True)
    with open(os.path.join(PERF, "paint_sweep.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS)
        w.writeheader()
        w.writerows(rows)

    fig, (left, right) = plt.subplots(1, 2, figsize=(7.4, 2.7), dpi=220)
    fig.patch.set_facecolor(SURFACE)
    for key, label, color, marker in SERIES:
        series = [r for r in rows if r["system"] == key and r["count"] > 0]
        n = [r["count"] for r in series]
        for ax, y in ((left, [r["frame_ms"] for r in series]), (right, [r["niagara_gt_ms"] + r["niagara_rt_ms"] for r in series])):
            ax.plot(n, y, color=color, linewidth=2, marker=marker, markersize=4.5,
                    markeredgecolor=SURFACE, markeredgewidth=1.2, label=label, zorder=3)

    style(left, "frame time (ms), 1920x1080")
    top = max(r["frame_ms"] for r in rows)
    left.set_ylim(0, max(30.0, 1.1 * top))
    budget(left, 16.6, "60 fps budget 16.6 ms")
    left.axhline(8.33, color=MUTED, linewidth=0.6)
    left.text(0.01, 8.33 - 0.01 * left.get_ylim()[1], "display cap 8.33 ms (120 Hz)", transform=left.get_yaxis_transform(), ha="left", va="top", fontsize=7, color=MUTED)
    left.set_title("Frame time", fontsize=8.5, color=INK, loc="left")

    style(right, "Niagara GT + RT (ms)")
    right.set_ylim(0, max(10.0, 1.1 * max(r["niagara_gt_ms"] + r["niagara_rt_ms"] for r in rows)))
    budget(right, 4.0, "Niagara budget 4 ms")
    right.set_title("Niagara CPU time", fontsize=8.5, color=INK, loc="left")

    handles, labels = left.get_legend_handles_labels()
    fig.legend(handles, labels, loc="lower center", ncol=3, frameon=False, fontsize=7.5, labelcolor=INK)
    fig.tight_layout(rect=(0, 0.08, 1, 1))
    out = os.path.join(PERF, "paint_fig_scaling.png")
    fig.savefig(out, facecolor=SURFACE)
    for r in rows:
        print("%-9s %8d live %8d  frame %6.2f  game %5.2f  gpu %6.2f  niagara gt %6.3f rt %5.3f  cov %.3f  pause %s" % (
            r["system"], r["count"], r["live"], r["frame_ms"], r["game_ms"], r["gpu_ms"], r["niagara_gt_ms"], r["niagara_rt_ms"], r["coverage"], r["pause_idle"]))
    print(out)


if __name__ == "__main__":
    main()
