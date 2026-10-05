"""Summarize one fixed-frequency reference-gamma capture block."""

import argparse
import json
from pathlib import Path

from analyze_nanovna_matrix import complex_stats, read_rows, write_rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("block", type=Path)
    args = parser.parse_args()
    manifest = json.loads((args.block / "manifest.json").read_text(encoding="utf-8"))
    rows = []
    for entry in manifest["conditions"]:
        if entry["status"] != "complete":
            continue
        source = read_rows(Path(entry["raw_csv"]))
        for sensor in (1, 2):
            records = [row["record"].split(",") for row in source
                       if row["phase"] == f"gamma_sensor_{sensor}" and
                       row["record"].startswith("G,")]
            frequencies = {int(record[2]) for record in records}
            expected = int(manifest["reference_frequencies_hz"][str(sensor)])
            if frequencies != {expected} or len(records) != manifest["samples_per_sensor_condition"]:
                raise ValueError(f"Incomplete or mismatched sensor {sensor} capture")
            stats = complex_stats([complex(float(record[3]), float(record[4]))
                                   for record in records])
            rows.append({"sample_rate_khz": manifest["sample_rate_khz_claimed"],
                         "if_hz_requested": entry["if_hz_requested"],
                         "tracking_bandwidth_hz": entry["bandwidth_hz_requested"],
                         "sensor_id": sensor, "frequency_hz": expected,
                         "sample_count": len(records), **stats})
    output = args.block / "reference_gamma_summary.csv"
    write_rows(output, rows)
    print(f"Wrote {len(rows)} rows: {output}")


if __name__ == "__main__":
    main()
