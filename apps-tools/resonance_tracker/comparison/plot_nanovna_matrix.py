"""Plot three-rate NanoVNA noise and tracking quality from summary CSVs."""

import argparse
import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


def read_csv(path):
    with path.open(newline="", encoding="utf-8") as source:
        return list(csv.DictReader(source))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("matrix", type=Path)
    parser.add_argument("reference_gamma", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    rows = [row for row in read_csv(args.matrix) if row["tracker_points"] == "5"]
    reference = {(int(row["if_hz_requested"]), int(row["tracking_bandwidth_hz"]),
                  int(row["sensor_id"])): row for row in read_csv(args.reference_gamma)}
    fig, axes = plt.subplots(2, 4, figsize=(17, 8), sharex=True)
    colors = {192: "#1976d2", 384: "#e68a00", 768: "#b52525"}
    metrics = [
        ("frequency_noise_rms_population_hz", "Frequency spread (Hz)"),
        ("relative_amplitude_noise_rms", "Relative amplitude RMS"),
        ("phase_noise_rms_population_deg", "Phase RMS (degrees)"),
        ("relock_event_count", "Relock events / 20 s"),
    ]
    for row_index, if_hz in enumerate((16_000, 32_000)):
        for rate in (192, 384, 768):
            for sensor in (1, 2):
                selected = sorted((row for row in rows
                                   if int(row["if_hz"]) == if_hz and
                                   int(row["sample_rate_khz"]) == rate and
                                   int(row["sensor_id"]) == sensor),
                                  key=lambda row: int(row["tracking_bandwidth_hz"]))
                if len(selected) != 3:
                    raise ValueError(f"Expected three bandwidths for {rate}k, IF{if_hz}, S{sensor}")
                x = [int(row["tracking_bandwidth_hz"]) for row in selected]
                for col, (key, title) in enumerate(metrics):
                    values = []
                    for row in selected:
                        value_row = (reference[(if_hz, int(row["tracking_bandwidth_hz"]), sensor)]
                                     if rate == 768 and col in (1, 2) else row)
                        values.append(float(value_row[key]))
                    axes[row_index, col].plot(x, values, marker="o", color=colors[rate],
                                              linestyle="-" if sensor == 1 else "--",
                                              label=f"{rate}k S{sensor}")
        for col, (_, title) in enumerate(metrics):
            ax = axes[row_index, col]
            ax.set_title(f"Requested IF {if_hz // 1000} kHz — {title}")
            ax.set_xscale("log")
            ax.set_xticks([100, 1000, 4000], ["100", "1000", "4000"])
            ax.grid(alpha=0.25)
            ax.set_xlabel("Tracking bandwidth (Hz)")
            if col == 3:
                ax.set_ylim(bottom=0)
            if col == 1:
                ax.ticklabel_format(axis="y", style="sci", scilimits=(0, 0))
    axes[0, 0].legend(loc="upper left", fontsize=8)
    fig.suptitle("NanoVNA matrix: observed frequency spread, fixed-frequency noise, and relocks\n"
                 "Frequency spread includes relock motion; 768k amplitude/phase use common reference frequencies")
    fig.tight_layout(rect=(0, 0, 1, 0.92))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.output, dpi=160)
    print(f"Wrote {args.output}")


if __name__ == "__main__":
    main()
