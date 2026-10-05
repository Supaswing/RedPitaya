"""Read-only NanoVNA serial command probe for COM11."""

import argparse
import time

import serial


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM11")
    parser.add_argument("commands", nargs="*", default=["help", "version", "display"])
    args = parser.parse_args()
    with serial.Serial(args.port, 38400, timeout=0.1) as device:
        for command in args.commands:
            device.reset_input_buffer()
            device.write((command + "\r\n").encode("ascii"))
            device.flush()
            time.sleep(1)
            print(f"## {command}\n{device.read_all().decode('utf-8', errors='replace')}", flush=True)


if __name__ == "__main__":
    main()
