#!/bin/sh
set -eu

app_source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir="$app_source_dir/build-hardware-test"

if [ "$(id -u)" -ne 0 ]; then
    echo "hardware_test.sh must run as root to access /dev/mem" >&2
    exit 2
fi

if command -v fuser >/dev/null 2>&1 && fuser /dev/mem >/dev/null 2>&1; then
    echo "another process is using /dev/mem; stop the resonance_tracker web app before testing" >&2
    exit 3
fi

cmake -S "$app_source_dir" -B "$build_dir" \
    -DINSTALL_DIR=/opt/redpitaya \
    -DCMAKE_BUILD_TYPE=Release \
    -DRESONANCE_TRACKER_BUILD_TESTS=OFF \
    -DRESONANCE_TRACKER_BUILD_HARDWARE_TESTS=ON

CMAKE_BUILD_PARALLEL_LEVEL=1 cmake --build "$build_dir" --target raw_iq_hardware_test
exec "$build_dir/resonance_tracker/raw_iq_hardware_test" "$@"
