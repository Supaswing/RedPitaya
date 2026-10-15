#!/bin/sh
set -eu

if [ "${1:-}" = '--help' ]; then
    echo 'Usage: sh run_raw_iq_sweep.sh [start_hz [stop_hz [points [sweeps [window_shift [output_parent]]]]]]'
    echo 'Defaults: 33000000 37000000 401 1 20 (output beside this script)'
    exit 0
fi
if [ "$#" -gt 6 ]; then
    echo 'Too many arguments; use --help for usage.' >&2
    exit 2
fi
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
start_hz=${1:-33000000}
stop_hz=${2:-37000000}
points=${3:-401}
sweeps=${4:-1}
window_shift=${5:-20}
output_parent=${6:-"$source_dir/output"}
if [ "$(id -u)" -ne 0 ]; then
    echo 'Run as root for /dev/mem access.' >&2
    exit 2
fi
for tool in fuser flock g++ sha256sum; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Required tool missing: $tool" >&2
        exit 2
    fi
done
# Serializes sweep scripts; fuser also checks the existing app and other tools.
exec 9>/tmp/resonance_tracker_raw_iq_sweep.lock
if ! flock -n 9; then
    echo 'Another IQ sweep is already running.' >&2
    exit 3
fi
if fuser /dev/mem >/dev/null 2>&1; then
    echo 'Stop the web app in Bazaar first: another process owns /dev/mem.' >&2
    exit 3
fi
mkdir -p "$output_parent"
output_dir=$(mktemp -d "$output_parent/raw_iq_sweep_$(date -u +%Y%m%d_%H%M%S)_XXXXXX")
printf 'Output directory: %s\n' "$output_dir"
printf '%s\n' 'status=incomplete' > "$output_dir/status.txt"
{
    printf 'utc=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    printf 'start_hz=%s\nstop_hz=%s\npoints=%s\nsweeps=%s\nwindow_shift=%s\n' \
        "$start_hz" "$stop_hz" "$points" "$sweeps" "$window_shift"
    printf '%s\n' 'platform=STEMlab 125-14 Gen2' 'clock_hz=125000000' \
        'amplitude_register=0x0800' 'phase_offset=0' 'settling_us=100' 'timeout_us=10000' \
        'averages=1 (individual raw windows retained)' 'order=alternating_forward_reverse_sweeps' \
        'physical_setup=unrecorded' 'bitstream_version=unrecorded' 'overflow_detection=unavailable' \
        'timestamp_s=monotonic seconds since capture start at measurement completion'
    if command -v git >/dev/null 2>&1; then
        git -C "$source_dir" rev-parse HEAD 2>/dev/null || true
    fi
    sha256sum "$source_dir/raw_iq_sweep_capture.cpp" "$source_dir/../src/raw_iq_acquisition.cpp" \
        "$source_dir/../src/raw_iq_acquisition.hpp"
} > "$output_dir/settings.txt"
if ! g++ -std=c++20 -O2 -Wall -Wextra -I"$source_dir/../src" \
    "$source_dir/raw_iq_sweep_capture.cpp" "$source_dir/../src/raw_iq_acquisition.cpp" \
    -o "$output_dir/raw_iq_sweep_capture" > "$output_dir/build.log" 2>&1; then
    cat "$output_dir/build.log" >&2
    exit 1
fi
# Recheck after compiling; keep the application stopped for the entire capture.
if fuser /dev/mem >/dev/null 2>&1; then
    echo 'Another process acquired /dev/mem during compilation.' >&2
    exit 3
fi
if "$output_dir/raw_iq_sweep_capture" "$output_dir/raw.csv" "$start_hz" "$stop_hz" \
    "$points" "$sweeps" "$window_shift" 2> "$output_dir/capture.log"; then
    printf '%s\n' 'status=complete' > "$output_dir/status.txt"
    cat "$output_dir/capture.log"
    printf 'Sweep saved to %s\n' "$output_dir"
else
    result=$?
    printf 'status=failed_or_cancelled\nexit_code=%s\n' "$result" > "$output_dir/status.txt"
    cat "$output_dir/capture.log" >&2
    exit "$result"
fi
