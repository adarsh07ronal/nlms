#!/usr/bin/env python3
"""
plot_erle.py — Plot ERLE convergence over time from CSV produced by main.c

Usage:
    python3 results/plot_erle.py

Output:
    results/erle_convergence.png
"""

import csv
import os
import sys

try:
    import matplotlib
    matplotlib.use('Agg')          # headless — works without a display
    import matplotlib.pyplot as plt
    import matplotlib.ticker as ticker
except ImportError:
    print("matplotlib not found. Install with:  pip3 install matplotlib")
    sys.exit(1)

CSV_PATH = os.path.join(os.path.dirname(__file__), "erle_over_time.csv")
OUT_PATH = os.path.join(os.path.dirname(__file__), "erle_convergence.png")


def load_csv(path):
    times, erle_vals = [], []
    with open(path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            times.append(float(row["time_s"]))
            erle_vals.append(float(row["erle_db"]))
    return times, erle_vals


def main():
    if not os.path.exists(CSV_PATH):
        print(f"CSV not found: {CSV_PATH}")
        print("Run 'make' first to generate it.")
        sys.exit(1)

    times, erle = load_csv(CSV_PATH)

    fig, ax = plt.subplots(figsize=(9, 4.5))
    ax.plot(times, erle, color="#2E75B6", linewidth=2, label="ERLE")

    # reference lines
    ax.axhline(y=20, color="#E24B4A", linestyle="--", linewidth=1,
               label="20 dB threshold (acceptable)")
    ax.axhline(y=30, color="#1D9E75", linestyle="--", linewidth=1,
               label="30 dB threshold (good)")

    # shading
    ax.fill_between(times, erle, alpha=0.12, color="#2E75B6")

    ax.set_xlabel("Time (s)", fontsize=11)
    ax.set_ylabel("ERLE (dB)", fontsize=11)
    ax.set_title("AEC-NLMS Convergence — Echo Return Loss Enhancement over Time",
                 fontsize=12, fontweight="medium")

    ax.set_xlim(left=0)
    ax.set_ylim(bottom=0)
    ax.yaxis.set_major_formatter(ticker.FormatStrFormatter('%g dB'))
    ax.grid(True, alpha=0.3, linewidth=0.5)
    ax.legend(fontsize=10)

    plt.tight_layout()
    plt.savefig(OUT_PATH, dpi=150, bbox_inches="tight")
    print(f"Plot saved to {OUT_PATH}")


if __name__ == "__main__":
    main()
