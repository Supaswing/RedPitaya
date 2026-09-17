#!/bin/sh
set -eu

app_source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

rw
cmake -S "$app_source_dir" -B "$app_source_dir/build" \
    -DINSTALL_DIR=/opt/redpitaya \
    -DCMAKE_BUILD_TYPE=Debug
cmake --build "$app_source_dir/build" --target install -- -j2
