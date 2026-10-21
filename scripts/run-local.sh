#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
studio_build_dir="${MOSS_BUILD_DIR:-build-delivery}"
if [ ! -f "$studio_build_dir/CMakeCache.txt" ]; then
    cmake -S . -B "$studio_build_dir" -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_PREFIX_PATH="${QT_ROOT:-$HOME/Qt/6.10.2/macos}" \
        -DMOSS_FFMPEG_ROOT="$PWD/.local/media-deps" \
        -DXINSHEN_SOURCE="${XINSHEN_SOURCE:-}"
fi
cmake --build "$studio_build_dir" --target MossStudio moss-ditto moss-voice -j "${BUILD_JOBS:-6}"
# Use the checkout's workers and models; no deployment, resource copy or signing.
exec "$studio_build_dir/MossStudio.app/Contents/MacOS/MossStudio" "$@"
