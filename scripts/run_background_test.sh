#!/bin/sh
# Also supports older debug binaries which predate the automatic headless mode.
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_binary=${MOSS_TEST_BINARY:-"$repo_root/build-delivery/MossStudio.app/Contents/MacOS/MossStudio"}
if [ "$#" -eq 0 ]; then
    set -- --home-smoke
fi
export QT_QPA_PLATFORM=offscreen
export QT_QUICK_BACKEND=software
export QT_SCALE_FACTOR=${QT_SCALE_FACTOR:-2}
exec "$test_binary" "$@"
