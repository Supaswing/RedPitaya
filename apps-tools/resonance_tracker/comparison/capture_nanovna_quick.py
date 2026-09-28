"""One baseline and short, timestamped 5/3-point NanoVNA captures.

Run from the RedPitaya repository root, for example:
  py -3 apps-tools/resonance_tracker/comparison/capture_nanovna_quick.py --port COM11
Requires pyserial, as in the existing sdsi_reader measurement scripts.
"""

import argparse
import csv
import json
import time
from datetime import datetime, timezone
from pathlib import Path

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="NanoVNA serial port, e.g. COM11")
    parser.add_argument("--seconds", type=float, default=15.0, help="minimum seconds per tracking mode")
    parser.add_argument("--bandwidth", type=int, default=100, help="tracking bandwidth in Hz")
    args = parser.parse_args()
    if args.seconds <= 0 or args.bandwidth <= 0:
        parser.error("seconds and bandwidth must be positive")

    root = Path(__file__).parent / "output"
    output = root / ("setupA_nanovna_" + datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S"))
    output.mkdir(parents=True, exist_ok=False)
    metadata = {
        "port": args.port,
        "baud": 38400,
        "requested_tracking_bandwidth_hz": args.bandwidth,
        "baseline_start_hz": 20_000_000,
        "baseline_stop_hz": 26_000_000,
        "sensor_count": 2,
        "sensor_1_distance_cm": 6,
        "sensor_2_distance_cm": 7,
        "minimum_seconds_per_mode": args.seconds,
        "minimum_rtd_frames_per_mode": 20,
        "physical_state": "unchanged",
        "display_state": "record separately",
        "calibration_state": "record separately",
    }
    (output / "metadata.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    print(f"Saving raw data in {output}", flush=True)

    with serial.Serial(args.port, 38400, timeout=0.10) as device, (output / "raw.csv").open(
        "w", newline="", encoding="utf-8"
    ) as raw_file:
        writer = csv.writer(raw_file)
        writer.writerow(["elapsed_s", "utc", "phase", "record"])
        start = time.perf_counter()
        buffer = bytearray()

        def send(command):
            device.write((command + "\r\n").encode("ascii"))
            device.flush()
            writer.writerow([f"{time.perf_counter() - start:.6f}", datetime.now(timezone.utc).isoformat(),
                             "command", command])
            raw_file.flush()

        def receive(phase, duration, until=None, minimum_rtd=0, maximum=None):
            deadline = time.perf_counter() + duration
            hard_deadline = time.perf_counter() + (maximum if maximum is not None else duration)
            records = []
            rtd = 0
            while time.perf_counter() < hard_deadline:
                chunk = device.read(max(1, device.in_waiting))
                if chunk:
                    buffer.extend(chunk)
                while b"\n" in buffer:
                    line, _, remainder = buffer.partition(b"\n")
                    buffer[:] = remainder
                    record = line.decode("utf-8", errors="replace").strip()
                    if not record:
                        continue
                    writer.writerow([f"{time.perf_counter() - start:.6f}",
                                     datetime.now(timezone.utc).isoformat(), phase, record])
                    raw_file.flush()
                    records.append(record)
                    if record.startswith("RTD,"):
                        rtd += 1
                    if until and record.startswith(until):
                        return records, rtd
                if not until and time.perf_counter() >= deadline and rtd >= minimum_rtd:
                    return records, rtd
            if until:
                raise RuntimeError(f"Missing {until}; inspect {output / 'raw.csv'}")
            if rtd < minimum_rtd:
                raise RuntimeError(f"Only {rtd} RTD frames in {phase}; inspect {output / 'raw.csv'}")
            return records, rtd

        try:
            send("rtrack stop")
            send("rtrack debug 0")
            send("rtrack sensors 2")
            send(f"bandwidth hz {args.bandwidth}")
            send("rtrack status")
            send("bandwidth")
            send("version")
            receive("initial", 2)

            send("rtrack points 5")
            send("rtrack baseline 20000000 26000000")
            baseline, _ = receive("baseline", 120, until="RTB_DONE,")
            done = [line for line in baseline if line.startswith("RTB_DONE,")][-1]
            print("Baseline:", done, flush=True)
            if input("Are these the two intended sensor resonances? Type y to continue: ").strip().lower() != "y":
                print("Stopped before tracking. Raw baseline was saved.", flush=True)
                return

            for points in (5, 3):
                send(f"rtrack points {points}")
                send("rtrack start")
                _, count = receive(f"tracking_{points}pt", args.seconds, minimum_rtd=20,
                                   maximum=max(60.0, args.seconds))
                send("rtrack stop")
                receive(f"stop_{points}pt", 0.5)
                print(f"{points}-point: {count} timestamped RTD frames", flush=True)

            # One separate debug frame preserves the complex RTM points without
            # slowing either timed frame-rate measurement at 38,400 baud.
            send("rtrack points 5")
            send("rtrack debug 1")
            send("rtrack once")
            receive("complex_debug_frame", 10, until="RTD,")
            send("rtrack debug 0")

            send("rtrack status")
            send("bandwidth")
            receive("final", 1)
            print(f"Done. Share {output / 'raw.csv'} and {output / 'metadata.json'}", flush=True)
        finally:
            send("rtrack stop")
            send("rtrack debug 0")


if __name__ == "__main__":
    main()
