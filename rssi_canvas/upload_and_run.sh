#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(dirname "$script_dir")
platformio="${HOME}/.platformio/penv/bin/platformio"

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 hub|beacon" >&2
    exit 2
fi

case "$1" in
    hub)
        cd "$project_root/hub"
        "$platformio" run -e esp32-s3-devkitc-1 -t upload
        exec "$script_dir/run.sh"
        ;;
    beacon)
        cd "$project_root/beacon"
        exec "$platformio" run -e esp32doit-devkit-v1 -t upload
        ;;
    *)
        echo "Unknown firmware target: $1" >&2
        exit 2
        ;;
esac
