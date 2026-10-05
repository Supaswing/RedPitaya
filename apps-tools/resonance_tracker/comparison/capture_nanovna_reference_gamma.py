"""Capture fixed-frequency gamma noise at the same two reference frequencies."""

import argparse
import csv
import json
from datetime import datetime, timezone
from pathlib import Path

import serial

from capture_nanovna_matrix import Session, query_bandwidth, utc_now


REFERENCE_CENTERS_HZ = {1: 21_125_536, 2: 24_635_208}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM11")
    parser.add_argument("--sample-rate-khz", type=int, choices=(192, 384, 768), required=True)
    parser.add_argument("--samples", type=int, default=250)
    args = parser.parse_args()
    if args.samples < 20:
        parser.error("--samples must be at least 20")
    output = (Path(__file__).parent / "output" /
              f"setupA_nanovna_reference_gamma_{args.sample_rate_khz}k_"
              f"{datetime.now(timezone.utc):%Y%m%d_%H%M%S}")
    output.mkdir(parents=True, exist_ok=False)
    manifest = {"port": args.port, "sample_rate_khz_claimed": args.sample_rate_khz,
                "reference_frequencies_hz": REFERENCE_CENTERS_HZ,
                "samples_per_sensor_condition": args.samples,
                "display_off_verified": False, "started_utc": utc_now(), "conditions": []}
    try:
        with serial.Serial(args.port, 38400, timeout=0.1) as device:
            for if_hz in (16_000, 32_000):
                for bw in (100, 1_000, 4_000):
                    entry = {"if_hz_requested": if_hz, "bandwidth_hz_requested": bw,
                             "status": "started", "runtime_if_verified": False}
                    manifest["conditions"].append(entry)
                    path = output / f"if{if_hz}_bw{bw}.csv"
                    with path.open("w", newline="", encoding="utf-8") as raw_file:
                        writer = csv.writer(raw_file)
                        writer.writerow(["elapsed_s", "utc", "phase", "record"])
                        session = Session(device, writer, raw_file)
                        session.send("rtrack stop")
                        session.send(f"offset {if_hz}")
                        actual = query_bandwidth(session, bw)
                        if actual != bw:
                            raise RuntimeError(f"Expected {bw} Hz bandwidth; got {actual}")
                        for sensor, frequency in REFERENCE_CENTERS_HZ.items():
                            session.send(f"gamma {frequency} {args.samples}")
                            rows = session.receive(f"gamma_sensor_{sensor}", 45,
                                                   until="GAMMA_END,")
                            if sum(row.startswith("G,") for row in rows) != args.samples:
                                raise RuntimeError(f"Incomplete gamma capture for sensor {sensor}")
                        entry["status"] = "complete"
                        entry["raw_csv"] = str(path)
                    (output / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
                    print(f"Completed IF {if_hz} BW {bw}", flush=True)
    finally:
        manifest["ended_utc"] = utc_now()
        (output / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
        print(f"Saved {output}", flush=True)


if __name__ == "__main__":
    main()
