"""Summarize timestamped, two-sensor NanoVNA RTD captures without filtering."""

import argparse
import csv
import json
import statistics
from collections import defaultdict
from pathlib import Path


def summarize(values):
    return {
        "mean": statistics.mean(values),
        "sd_sample": statistics.stdev(values) if len(values) > 1 else None,
        "rms_population": statistics.pstdev(values) if len(values) > 1 else 0.0,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("raw_csv", type=Path)
    args = parser.parse_args()
    rows = list(csv.DictReader(args.raw_csv.open(newline="", encoding="utf-8")))
    frames = defaultdict(list)
    baseline_records = []
    debug_records = []
    for row in rows:
        record = row["record"]
        if row["phase"] == "baseline" and record.startswith("RTB_"):
            baseline_records.append(record)
        if row["phase"] == "complex_debug_frame" and record.startswith(("RTM,", "RTQ,")):
            debug_records.append(record)
        if row["phase"] not in ("tracking_5pt", "tracking_3pt") or not record.startswith("RTD,"):
            continue
        fields = record.split(",")
        if len(fields) != 9:
            raise ValueError(f"Unexpected RTD field count: {record}")
        points = int(fields[8])
        if points != int(row["phase"].split("_")[1].removesuffix("pt")):
            raise ValueError(f"RTD point count disagrees with capture phase: {record}")
        frames[points].append({
            "timestamp_s": float(row["elapsed_s"]),
            "sequence": int(fields[1]),
            "frequency_hz": (float(fields[2]), float(fields[5])),
            "q": (float(fields[3]), float(fields[6])),
            "internal_se_hz": (float(fields[4]), float(fields[7])),
        })

    result = {"source_file": str(args.raw_csv), "baseline_records": baseline_records,
              "debug_records": debug_records, "tracking": []}
    csv_rows = []
    for points in (5, 3):
        mode = frames[points]
        if len(mode) < 20:
            raise ValueError(f"Only {len(mode)} {points}-point frames; require at least 20")
        sequences = [frame["sequence"] for frame in mode]
        if any(b <= a for a, b in zip(sequences, sequences[1:])):
            raise ValueError(f"Nonmonotonic or duplicate {points}-point sequence")
        gaps = sum(b - a - 1 for a, b in zip(sequences, sequences[1:]))
        intervals = [b["timestamp_s"] - a["timestamp_s"] for a, b in zip(mode, mode[1:])]
        if any(interval <= 0 for interval in intervals):
            raise ValueError(f"Nonmonotonic {points}-point capture timestamps")
        interval_stats = summarize(intervals)
        for sensor in (1, 2):
            i = sensor - 1
            frequencies = [frame["frequency_hz"][i] for frame in mode]
            se = [frame["internal_se_hz"][i] for frame in mode]
            q = [frame["q"][i] for frame in mode]
            frequency_stats = summarize(frequencies)
            entry = {
                "platform": "NanoVNA", "setup_id": "A", "tracker_points": points,
                "sensor_id": sensor, "count": len(mode),
                "first_sequence": sequences[0], "last_sequence": sequences[-1],
                "missing_sequences": gaps,
                "mean_frequency_hz": frequency_stats["mean"],
                "frequency_sd_sample_hz": frequency_stats["sd_sample"],
                "df_rms_population_hz": frequency_stats["rms_population"],
                "mean_internal_se_hz": statistics.mean(se),
                "mean_q": statistics.mean(q),
                "mean_frame_interval_s": interval_stats["mean"],
                "median_frame_interval_s": statistics.median(intervals),
                "frame_interval_sd_sample_s": interval_stats["sd_sample"],
                "complete_two_sensor_frame_rate_hz": 1.0 / interval_stats["mean"],
                "source_file": str(args.raw_csv),
                "signal_delta_f_hz": "", "snr_f": "",
            }
            result["tracking"].append(entry)
            csv_rows.append(entry)

    output_dir = args.raw_csv.parent
    (output_dir / "summary.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    with (output_dir / "tracking_summary.csv").open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=csv_rows[0].keys())
        writer.writeheader()
        writer.writerows(csv_rows)
    print(json.dumps(result["tracking"], indent=2))


if __name__ == "__main__":
    main()
