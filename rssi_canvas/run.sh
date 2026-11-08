#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir="$script_dir/build"

cmake -S "$script_dir" -B "$build_dir"
cmake --build "$build_dir" --parallel
exec "$build_dir/rssi_canvas"
