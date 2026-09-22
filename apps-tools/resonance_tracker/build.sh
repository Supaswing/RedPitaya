#!/bin/sh
set -eu

app_source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

rw
cmake -S "$app_source_dir" -B "$app_source_dir/build" \
    -DINSTALL_DIR=/opt/redpitaya \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_FLAGS_DEBUG="-O0 -g0" \
    -DCMAKE_CXX_FLAGS_DEBUG="-O0 -g0" \
    -DRESONANCE_TRACKER_BUILD_TESTS=OFF

# Native compilation runs on memory-constrained target hardware. Keeping this
# serial also prevents a cached test build from competing with controllerhf.so.
CMAKE_BUILD_PARALLEL_LEVEL=1 cmake --build "$app_source_dir/build" --target install
