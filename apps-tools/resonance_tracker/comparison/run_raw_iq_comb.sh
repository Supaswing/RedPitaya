#!/bin/sh
set -eu
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ "$(id -u)" -ne 0 ]; then
    echo 'Run as root for /dev/mem access.' >&2
    exit 2
fi
if ! command -v fuser >/dev/null 2>&1; then
    echo 'fuser is required to verify exclusive acquisition access.' >&2
    exit 2
fi
if fuser /dev/mem >/dev/null 2>&1; then
    echo 'Stop the web app first: another process owns /dev/mem.' >&2
    exit 3
fi
mkdir -p "$source_dir/output"
output_dir="$source_dir/output/raw_iq_comb_$(date -u +%Y%m%d_%H%M%S)"
mkdir "$output_dir"
g++ -std=c++20 -O2 -Wall -Wextra -I"$source_dir/../src" \
    "$source_dir/raw_iq_comb_capture.cpp" "$source_dir/../src/raw_iq_acquisition.cpp" \
    -o "$output_dir/raw_iq_comb_capture"
printf '%s\n' 'clock_hz=125000000' 'start_requested_hz=30000000' \
    'step_phase_increment=1' 'frequencies=128' 'readings_per_frequency=16' \
    'window_shifts=18,19,20' 'amplitude_register=0x0800' \
    'settling_us=100' 'timeout_us=10000' 'order=alternating_forward_reverse_passes' \
    'physical_setup=unrecorded' > "$output_dir/settings.txt"
if "$output_dir/raw_iq_comb_capture" "$output_dir/raw.csv" 2> "$output_dir/capture.log"; then
    cat "$output_dir/capture.log"
    printf 'Capture saved to %s\n' "$output_dir"
else
    cat "$output_dir/capture.log" >&2
    exit 1
fi
