"""Summarize NanoVNA matrix raw logs without filtering or pooling conditions."""

import argparse
import csv
import math
import statistics
from collections import defaultdict
from pathlib import Path


def read_rows(path):
    with path.open(newline="", encoding="utf-8") as source:
        return list(csv.DictReader(source))


def write_rows(path, rows):
    with path.open("w", newline="", encoding="utf-8") as target:
        writer = csv.DictWriter(target, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)


def complex_stats(values):
    if len(values) < 2:
        return {"mean_gamma_magnitude": "", "amplitude_noise_rms_population": "",
                "relative_amplitude_noise_rms": "", "mean_gamma_phase_deg": "",
                "phase_noise_rms_population_deg": ""}
    amplitudes = [abs(v) for v in values]
    center = sum(values) / len(values)
    angle = math.atan2(center.imag, center.real)
    residuals = [math.atan2(math.sin(math.atan2(v.imag, v.real) - angle),
                            math.cos(math.atan2(v.imag, v.real) - angle)) for v in values]
    mean_amplitude = statistics.mean(amplitudes)
    amplitude_noise = statistics.pstdev(amplitudes)
    return {"mean_gamma_magnitude": mean_amplitude,
            "amplitude_noise_rms_population": amplitude_noise,
            "relative_amplitude_noise_rms": amplitude_noise / mean_amplitude,
            "mean_gamma_phase_deg": math.degrees(angle),
            "phase_noise_rms_population_deg": math.degrees(statistics.pstdev(residuals))}


def analyze_raw(path, sample_rate_khz, if_hz, bandwidth_hz):
    raw = read_rows(path)
    frames = defaultdict(list)
    gamma = defaultdict(list)
    for row in raw:
        fields = row["record"].split(",")
        phase = row["phase"]
        if phase in ("tracking_5pt", "tracking_3pt") and fields[0] == "RTD" and len(fields) == 9:
            points = int(fields[8])
            if phase != f"tracking_{points}pt":
                raise ValueError(f"Point count mismatch in {path}: {row['record']}")
            frames[points].append((float(row["elapsed_s"]), int(fields[1]),
                                   (float(fields[2]), float(fields[5]))))
        elif phase.startswith("gamma_sensor_") and fields[0] == "G" and len(fields) == 5:
            sensor = int(phase.rsplit("_", 1)[1])
            gamma[sensor].append((int(fields[2]), complex(float(fields[3]), float(fields[4]))))
    result = []
    for points in (5, 3):
        mode = frames[points]
        sequences = [frame[1] for frame in mode]
        if any(b != a + 1 for a, b in zip(sequences, sequences[1:])):
            raise ValueError(f"RTD sequence gap or duplicate in {path}, {points}-point")
        intervals = [b[0] - a[0] for a, b in zip(mode, mode[1:])]
        for sensor in (1, 2):
            frequency = [frame[2][sensor - 1] for frame in mode]
            fixed = gamma[sensor]
            if fixed and len({item[0] for item in fixed}) != 1:
                raise ValueError(f"Gamma frequency changed within sensor {sensor} in {path}")
            c = complex_stats([item[1] for item in fixed])
            result.append({"raw_csv": str(path), "sample_rate_khz": sample_rate_khz,
                           "if_hz": if_hz, "tracking_bandwidth_hz": bandwidth_hz,
                           "tracker_points": points, "sensor_id": sensor,
                           "tracking_frames": len(mode),
                           "mean_frequency_hz": statistics.mean(frequency) if frequency else "",
                           "frequency_noise_rms_population_hz": (statistics.pstdev(frequency)
                                                                  if len(frequency) > 1 else ""),
                           "frequency_sd_sample_hz": (statistics.stdev(frequency)
                                                      if len(frequency) > 1 else ""),
                           "complete_two_sensor_frame_rate_hz": (1 / statistics.mean(intervals)
                                                                 if intervals else ""),
                           "fixed_gamma_frequency_hz": fixed[0][0] if fixed else "",
                           "fixed_gamma_samples": len(fixed), **c})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("block", type=Path, help="output/setupA_nanovna_<rate>k_<timestamp> directory")
    args = parser.parse_args()
    rows = []
    for if_hz in (16_000, 32_000):
        for bw in (100, 1_000, 4_000):
            raw = args.block / f"if{if_hz}_bw{bw}" / "raw.csv"
            if not raw.exists():
                continue
            name = args.block.name
            rate = 192 if "_192k_" in name else 384 if "_384k_" in name else None
            if rate is None:
                parser.error("block directory name must contain _192k_ or _384k_")
            rows.extend(analyze_raw(raw, rate, if_hz, bw))
    if not rows:
        parser.error("No condition raw.csv files were found")
    output = args.block / "comparison_summary.csv"
    write_rows(output, rows)
    print(f"Wrote {len(rows)} rows: {output}")


if __name__ == "__main__":
    main()
