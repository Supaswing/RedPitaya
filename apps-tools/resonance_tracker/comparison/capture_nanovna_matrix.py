"""Capture one NanoVNA firmware's 2 IF x 3 bandwidth comparison block.

Run once after flashing each 192k/384k/768k firmware image. This script does not
flash firmware or claim that a dark panel proves SPI traffic is absent.
"""

import argparse
import csv
import hashlib
import json
import re
import time
from datetime import datetime, timezone
from pathlib import Path

import serial


ROOT = Path(__file__).parent
IF_VALUES = (16_000, 32_000)
BW_VALUES = (100, 1_000, 4_000)


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def firmware_hash(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


class Session:
    def __init__(self, device, writer, raw_file):
        self.device = device
        self.writer = writer
        self.raw_file = raw_file
        self.start = time.perf_counter()
        self.buffer = bytearray()

    def log(self, phase, record):
        self.writer.writerow([f"{time.perf_counter() - self.start:.6f}", utc_now(), phase, record])
        self.raw_file.flush()

    def send(self, command):
        self.device.write((command + "\r\n").encode("ascii"))
        self.device.flush()
        self.log("command", command)

    def receive(self, phase, timeout, until=None, minimum_rtd=0, duration=0,
                allow_missing_until=False):
        deadline = time.perf_counter() + timeout
        minimum_end = time.perf_counter() + duration
        records = []
        rtd_count = 0
        while time.perf_counter() < deadline:
            chunk = self.device.read(max(1, self.device.in_waiting))
            self.buffer.extend(chunk)
            while b"\n" in self.buffer:
                line, _, remaining = self.buffer.partition(b"\n")
                self.buffer[:] = remaining
                record = line.decode("utf-8", errors="replace").strip()
                if not record:
                    continue
                self.log(phase, record)
                records.append(record)
                if record.startswith("RTD,"):
                    rtd_count += 1
                if until and record.startswith(until):
                    return records
            if not until and time.perf_counter() >= minimum_end and rtd_count >= minimum_rtd:
                return records
        if until and allow_missing_until:
            return records
        if until:
            raise RuntimeError(f"Missing {until} in phase {phase}; raw log was retained")
        raise RuntimeError(f"Only {rtd_count} RTD frames in phase {phase}; raw log was retained")


def query_bandwidth(session, requested):
    session.send(f"bandwidth hz {requested}")
    records = session.receive("bandwidth_check", 3, until="bandwidth ")
    matches = [re.fullmatch(r"bandwidth \d+ \((\d+)Hz\)", r) for r in records]
    valid = [int(m.group(1)) for m in matches if m]
    if not valid:
        raise RuntimeError("No parseable bandwidth response")
    return valid[-1]


def baseline_info(records, requested_bw):
    starts = [r.split(",") for r in records if r.startswith("RTB,")]
    if len(starts) != 1 or len(starts[0]) < 11:
        raise RuntimeError("Expected exactly one complete RTB configuration record")
    start = starts[0]
    if (int(start[2]) != 2 or int(start[3]) != 20_000_000 or int(start[4]) != 26_000_000 or
            int(start[5]) != 101 or int(start[8]) != requested_bw or
            int(start[9]) != 3):
        raise RuntimeError(f"Unexpected RTB configuration: {','.join(start)}")
    centers = {}
    for record in records:
        if record.startswith("RTB_RES,"):
            fields = record.split(",")
            centers[int(fields[1])] = int(round(float(fields[2])))
    if set(centers) != {1, 2}:
        raise RuntimeError(f"Expected two resonance centers; got {centers}")
    # This firmware prints the compile-time FREQUENCY_OFFSET in RTB even after
    # the runtime `offset` command updates IF_OFFSET. It is not an IF readback.
    return {"rtb_compile_time_if_hz": int(start[6]), "runtime_if_verified": False,
            "reported_baseline_bw_hz": int(start[7]),
            "reported_tracking_bw_hz": int(start[8]), "centers_hz": centers}


def verify_display_off(session, off_command, status_command, off_pattern):
    session.send(off_command)
    session.receive("display_off_command", 2, duration=0.5)
    session.send(status_command)
    records = session.receive("display_status", 3, duration=1)
    if not any(re.fullmatch(off_pattern, record) for record in records if not record.startswith("ch>")):
        raise RuntimeError("Display status did not confirm OFF; raw log retained and capture stopped")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM11")
    parser.add_argument("--sample-rate-khz", type=int, choices=(192, 384, 768), required=True)
    parser.add_argument("--if-values", type=int, nargs="+", choices=IF_VALUES,
                        default=IF_VALUES, help="IF blocks to capture; useful for resuming after failure")
    parser.add_argument("--bandwidth-values", type=int, nargs="+", choices=BW_VALUES,
                        default=BW_VALUES, help="bandwidths to capture; useful for resuming after failure")
    parser.add_argument("--firmware", type=Path,
                        help="exact binary flashed before this block; used for provenance")
    parser.add_argument("--installed-firmware-unverified", action="store_true",
                        help="capture with firmware already installed, recording image identity as unverified")
    parser.add_argument("--tracking-seconds", type=float, default=20.0)
    parser.add_argument("--gamma-samples", type=int, default=250,
                        help="fixed-frequency complex samples per sensor and condition")
    parser.add_argument("--output-root", type=Path, default=ROOT / "output")
    parser.add_argument("--display-off-command", help="firmware serial command to disable display")
    parser.add_argument("--display-status-command", help="firmware serial command reporting display state")
    parser.add_argument("--display-off-pattern", help="full-line regex matching the OFF status reply")
    parser.add_argument("--display-state-unverified", action="store_true",
                        help="capture without display OFF verification, explicitly marked in the manifest")
    parser.add_argument("--plan", action="store_true", help="show matrix without opening the port")
    args = parser.parse_args()
    if args.tracking_seconds <= 0 or not 20 <= args.gamma_samples <= 3000:
        parser.error("tracking seconds must be positive; gamma samples must be 20..3000")
    if args.plan:
        print(json.dumps({"sample_rate_khz": args.sample_rate_khz,
                          "conditions": [{"if_hz": if_hz, "bandwidth_hz": bw}
                                         for if_hz in args.if_values for bw in args.bandwidth_values],
                          "tracking_seconds_per_mode": args.tracking_seconds,
                          "gamma_samples_per_sensor": args.gamma_samples}, indent=2))
        return
    if not args.installed_firmware_unverified and (args.firmware is None or not args.firmware.is_file()):
        parser.error("--firmware must name the binary just flashed")
    if args.firmware is not None and not args.firmware.is_file():
        parser.error("--firmware path does not exist")
    if not args.display_state_unverified and not all((args.display_off_command, args.display_status_command, args.display_off_pattern)):
        parser.error("display OFF command, status command, and OFF reply pattern are required")
    if args.display_off_pattern:
        try:
            re.compile(args.display_off_pattern)
        except re.error as exc:
            parser.error(f"invalid --display-off-pattern: {exc}")
    digest = firmware_hash(args.firmware) if args.firmware else None
    if not args.installed_firmware_unverified:
        print(f"Expected firmware: {args.firmware.resolve()} SHA256 {digest}", flush=True)
        if input("Confirm this exact binary is flashed (type FLASHED): ").strip() != "FLASHED":
            raise SystemExit("Firmware identity was not confirmed; no capture started")
    block = args.output_root / (f"setupA_nanovna_{args.sample_rate_khz}k_"
                                + datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S"))
    block.mkdir(parents=True, exist_ok=False)
    manifest = {"sample_rate_khz_claimed": args.sample_rate_khz,
                "firmware_path": str(args.firmware.resolve()) if args.firmware else None,
                "firmware_sha256": digest,
                "flashed_image_identity_verified": not args.installed_firmware_unverified,
                "display_off_visually_confirmed": False, "display_status_verified": False,
                "display_spi_disabled_verified": False,
                "port": args.port, "started_utc": utc_now(), "conditions": []}
    try:
        with serial.Serial(args.port, 38400, timeout=0.1) as device:
            for if_hz in args.if_values:
                for bw in args.bandwidth_values:
                    condition = block / f"if{if_hz}_bw{bw}"
                    condition.mkdir()
                    entry = {"if_hz_requested": if_hz, "bandwidth_hz_requested": bw,
                             "raw_csv": str(condition / "raw.csv"), "status": "started"}
                    manifest["conditions"].append(entry)
                    (block / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
                    with (condition / "raw.csv").open("w", newline="", encoding="utf-8") as raw_file:
                        writer = csv.writer(raw_file)
                        writer.writerow(["elapsed_s", "utc", "phase", "record"])
                        session = Session(device, writer, raw_file)
                        try:
                            session.send("rtrack stop")
                            session.send("rtrack debug 0")
                            if not args.display_state_unverified:
                                verify_display_off(session, args.display_off_command,
                                                   args.display_status_command, args.display_off_pattern)
                                entry["display_status_verified"] = True
                                manifest["display_status_verified"] = True
                                if not manifest["display_off_visually_confirmed"]:
                                    if input("Is the NanoVNA panel visibly dark? Type DISPLAY OFF: ").strip() != "DISPLAY OFF":
                                        raise RuntimeError("Panel OFF was not visually confirmed")
                                    manifest["display_off_visually_confirmed"] = True
                            session.send("rtrack sensors 2")
                            session.send("version")
                            session.receive("initial", 2, duration=1)
                            probe = query_bandwidth(session, 16_000)
                            expected_probe = args.sample_rate_khz * 1_000 // 48
                            if probe != expected_probe:
                                raise RuntimeError(f"Sample-rate probe gave {probe} Hz; expected {expected_probe} Hz")
                            entry["sample_rate_probe_bandwidth_hz"] = probe
                            session.send(f"offset {if_hz}")
                            actual_bw = query_bandwidth(session, bw)
                            if actual_bw != bw:
                                raise RuntimeError(f"Requested {bw} Hz, device reports {actual_bw} Hz")
                            session.send("rtrack points 5")
                            session.send("rtrack baseline 20000000 26000000")
                            baseline = session.receive("baseline", 150, until="RTB_DONE,")
                            entry.update(baseline_info(baseline, bw))
                            print(f"IF {if_hz} BW {bw}: centers {entry['centers_hz']}", flush=True)
                            for points in (5, 3):
                                session.send(f"rtrack points {points}")
                                session.send("rtrack start")
                                frames = session.receive(f"tracking_{points}pt", max(90, args.tracking_seconds + 30),
                                                         minimum_rtd=20, duration=args.tracking_seconds)
                                session.send("rtrack stop")
                                session.receive(f"stop_{points}pt", 2, duration=0.5)
                                entry[f"tracking_{points}pt_rtd_frames"] = sum(r.startswith("RTD,") for r in frames)
                            session.send("rtrack points 5")
                            session.send("rtrack debug 1")
                            session.send("rtrack once")
                            debug = session.receive("complex_debug_frame", 15, until="RTD,",
                                                    allow_missing_until=True)
                            rtm = [r.split(",") for r in debug if r.startswith("RTM,")]
                            entry["debug_complete"] = (
                                len(rtm) >= 10 and len({r[1] for r in rtm[:10]}) == 1 and
                                {(int(r[2]), int(r[3])) for r in rtm[:10]} ==
                                {(sensor, offset) for sensor in (1, 2)
                                 for offset in (-2, -1, 0, 1, 2)})
                            entry["debug_rtm_points"] = len(rtm)
                            entry["debug_rtd_present"] = any(r.startswith("RTD,") for r in debug)
                            session.send("rtrack stop")
                            session.send("rtrack debug 0")
                            for sensor in (1, 2):
                                frequency = entry["centers_hz"][sensor]
                                session.send(f"gamma {frequency} {args.gamma_samples}")
                                fixed = session.receive(f"gamma_sensor_{sensor}",
                                                        max(30, args.gamma_samples * 0.04 + 10),
                                                        until="GAMMA_END,")
                                if sum(r.startswith("G,") for r in fixed) != args.gamma_samples:
                                    raise RuntimeError(f"Incomplete gamma samples for sensor {sensor}")
                            entry["status"] = "complete"
                        finally:
                            session.send("rtrack stop")
                            session.send("rtrack debug 0")
                            (block / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
                    print(f"Completed IF {if_hz} BW {bw}", flush=True)
    finally:
        manifest["ended_utc"] = utc_now()
        (block / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
        print(f"Saved block: {block}", flush=True)


if __name__ == "__main__":
    main()
