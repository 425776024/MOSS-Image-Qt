#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
# Build in an owned checkout. The reference project's libraries are never changed.
source_root="${MOSS_FFMPEG_SOURCE:?Set MOSS_FFMPEG_SOURCE to an FFmpeg source checkout}"
destination="$PWD/.local/media-deps"
source_copy="$PWD/.local/media-source/ffmpeg"
if [ ! -f "$source_root/configure" ]; then
    echo "Set MOSS_FFMPEG_SOURCE to an FFmpeg source checkout." >&2
    exit 1
fi
mkdir -p "$source_copy" "$destination"
if [ ! -f "$source_copy/configure" ]; then
    rsync -a --exclude='.git' --exclude='*.o' --exclude='*.a' --exclude='*.d' \
        --exclude='*.dylib' --exclude='*.dylib.*' "$source_root/" "$source_copy/"
    (cd "$source_copy"; make distclean >/dev/null 2>&1 || true)
fi
cd "$source_copy"
./configure --prefix="$destination" --arch=arm64 --target-os=darwin \
    --cc=/usr/bin/clang --cxx=/usr/bin/clang++ \
    --enable-shared --disable-static --disable-gpl --disable-nonfree \
    --disable-version3 --disable-autodetect --enable-zlib \
    --enable-videotoolbox --enable-audiotoolbox --enable-pthreads --enable-neon \
    --enable-pic --disable-debug --disable-doc --disable-ffplay \
    --install-name-dir='@rpath' \
    --extra-cflags='-mmacosx-version-min=15.0' \
    --extra-ldflags='-mmacosx-version-min=15.0'
make -j "${BUILD_JOBS:-6}"
make install
