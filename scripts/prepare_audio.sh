#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
qt_source="${VIDEOCUT_QT_SOURCE:?Set VIDEOCUT_QT_SOURCE for this optional local dependency importer}"
video_source="${VIDEOCUT_SOURCE:?Set VIDEOCUT_SOURCE for this optional local dependency importer}"
runtime="$qt_source/.local/release-closeout-r21/native-deps/ai"
sa3="$video_source/.local/ai-models/runtime/src/sa3.cpp"
for input in "$runtime/onnxruntime" "$runtime/sherpa-onnx" "$sa3"; do
    if [ ! -d "$input" ]; then echo "Missing audio source: $input" >&2; exit 1; fi
done
mkdir -p .local/audio-deps
copy_once() {
    if [ ! -e "$2" ]; then cp -cR "$1" "$2"; fi
}
copy_once "$runtime/onnxruntime" .local/audio-deps/onnxruntime
copy_once "$runtime/sherpa-onnx" .local/audio-deps/sherpa
if [ ! -f .local/audio-deps/sa3.cpp/CMakeLists.txt ]; then
    mkdir -p .local/audio-deps/sa3.cpp
    rsync -a --exclude='.git' --exclude='build*' --exclude='__pycache__' \
        "$sa3/" .local/audio-deps/sa3.cpp/
fi
cmake -S .local/audio-deps/sa3.cpp -B .local/build-audio -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DSA3_METAL=ON -DGGML_METAL_EMBED_LIBRARY=ON \
    -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF -DGGML_CCACHE=OFF \
    -DCMAKE_C_COMPILER_LAUNCHER= -DCMAKE_CXX_COMPILER_LAUNCHER= -DCMAKE_ASM_COMPILER_LAUNCHER=
cmake --build .local/build-audio --target sa3-generate -j "${BUILD_JOBS:-6}"
