#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
qt_root="${QT_ROOT:-$HOME/Qt/6.10.2/macos}"
build_dir="${MOSS_BUILD_DIR:-build}"
build_testing=OFF
if [ "${MOSS_RUN_TESTS:-0}" = 1 ]; then
    build_testing=ON
fi
engine_revision=b13a48219d5cdf408edcb93da6cd69b612ed03d3
mkdir -p .local/deps .local/evidence
if [ ! -d .local/deps/stable-diffusion.cpp/.git ]; then
    git clone https://github.com/leejet/stable-diffusion.cpp.git .local/deps/stable-diffusion.cpp
    git -C .local/deps/stable-diffusion.cpp checkout "$engine_revision"
fi
if [ "$(git -C .local/deps/stable-diffusion.cpp rev-parse HEAD)" != "$engine_revision" ]; then
    echo "Engine revision differs from the validated revision: $engine_revision" >&2
    exit 1
fi
git -C .local/deps/stable-diffusion.cpp submodule update --init --recursive
python3 scripts/prepare_flowedit.py .local/deps/stable-diffusion.cpp
cmake -S .local/deps/stable-diffusion.cpp -B .local/build-engine -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DSD_METAL=ON -DGGML_METAL_EMBED_LIBRARY=ON \
    -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DGGML_NATIVE=OFF \
    -DGGML_CCACHE=OFF -DCMAKE_C_COMPILER_LAUNCHER= -DCMAKE_CXX_COMPILER_LAUNCHER= \
    -DCMAKE_ASM_COMPILER_LAUNCHER= -DSD_WEBP=OFF -DSD_WEBM=OFF
cmake --build .local/build-engine --target sd-cli -j "${BUILD_JOBS:-6}"
cmake -S . -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=/usr/bin/clang++ -DCMAKE_OBJCXX_COMPILER=/usr/bin/clang++ \
    -DCMAKE_PREFIX_PATH="$qt_root" -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
    -DMOSS_FFMPEG_ROOT="${MOSS_FFMPEG_ROOT:-$PWD/.local/media-deps}" \
    -DXINSHEN_SOURCE="${XINSHEN_SOURCE:-}" -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DBUILD_TESTING="$build_testing" "$@"
cmake --build "$build_dir" -j "${BUILD_JOBS:-6}"
if [ "$build_testing" = ON ]; then
    QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir "$build_dir" --output-on-failure
fi
