#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
export MOSS_CI_NATIVE_ROOT="$PWD/.local/ci/native"
export MOSS_FFMPEG_ROOT="$MOSS_CI_NATIVE_ROOT/ffmpeg"
export MOSS_BUILD_DIR="${MOSS_BUILD_DIR:-build-ci}"
export MOSS_SFX_PATH="$PWD/.local/ci/build/sa3/bin/sa3-generate"
export PKG_CONFIG_PATH="$MOSS_FFMPEG_ROOT/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
export BUILD_JOBS="${BUILD_JOBS:-3}"
export MOSS_OUTPUT_ROOT="$PWD/.local/ci/test-data/works"
export MOSS_SETTINGS_ROOT="$PWD/.local/ci/test-data/settings"
python3 scripts/prepare_ci_macos.py
./scripts/build.sh \
    -DOpenCV_DIR="$MOSS_CI_NATIVE_ROOT/opencv/lib/cmake/opencv4" \
    -DMOSS_AUDIO_DEPS="$MOSS_CI_NATIVE_ROOT/audio-deps" \
    -DONNXRUNTIME_ROOT="$MOSS_CI_NATIVE_ROOT/audio-deps/onnxruntime" \
    -DMOSS_MEDIAPIPE_ROOT="$MOSS_CI_NATIVE_ROOT/mediapipe" \
    -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)" \
    -DJSON_INCLUDE="$(brew --prefix nlohmann-json)/include" \
    -DVOICE_JSON_INCLUDE="$(brew --prefix nlohmann-json)/include"
./scripts/package.sh
python3 scripts/audit_bundle.py "dist/Moss Studio.app" dist/bundle-audit.json
python3 scripts/macos_distribution.py package "dist/Moss Studio.app" --output dist/artifacts
