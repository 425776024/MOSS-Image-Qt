#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
qt_root="${QT_ROOT:-$HOME/Qt/6.10.2/macos}"
app="${MOSS_PACKAGE_PATH:-$PWD/dist/Moss Studio.app}"
with_models=0
image_quant=Q8_0
model_root=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        --model-root) model_root="$2"; shift 2 ;;
        --with-models) with_models=1; shift ;;
        --image-quant)
            if [ "$#" -lt 2 ]; then echo "--image-quant requires Q4_K or Q8_0" >&2; exit 2; fi
            image_quant="$2"; shift 2 ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
done
case "$image_quant" in Q4_K|Q8_0) ;; *) echo "Unsupported quantization: $image_quant" >&2; exit 2 ;; esac
mkdir -p "$(dirname "$app")"
if [ -e "$app" ]; then
    echo "$app already exists. Move the previous candidate before packaging." >&2
    exit 1
fi
if [ "$with_models" = 1 ] && [ -z "$model_root" ]; then
    echo "--with-models requires an explicit --model-root containing public catalog files; .local/models is never copied implicitly." >&2
    exit 2
fi
python3 scripts/public_release.py audit
build_dir="${MOSS_BUILD_DIR:-build}"
if [ -d "$build_dir/MossStudio.app/Contents/Resources/models" ]; then
    echo "Build bundle already contains models. Use a model-free build before packaging." >&2
    exit 2
fi
python3 scripts/public_release.py app "$build_dir/MossStudio.app"
cp -R "$build_dir/MossStudio.app" "$app"
mkdir -p "$app/Contents/Helpers" "$app/Contents/Resources/licenses"
cp .local/build-engine/bin/sd-cli "$build_dir/workers/moss-ditto" \
    "$build_dir/workers/moss-voice" "${MOSS_SFX_PATH:-.local/build-audio/bin/sa3-generate}" "$app/Contents/Helpers/"
mkdir -p "$app/Contents/Resources/assets"
cp -R assets/voices "$app/Contents/Resources/assets/"
cp -R vendor/videocut-voice/licenses "$app/Contents/Resources/licenses/audio"
cp vendor/videocut-voice/PROVENANCE.md "$app/Contents/Resources/licenses/audio/"
cp .local/deps/stable-diffusion.cpp/LICENSE "$app/Contents/Resources/licenses/stable-diffusion.cpp.txt"
cp vendor/xinshen-ditto/LICENSE.upstream "$app/Contents/Resources/licenses/Ditto.txt"
cp vendor/xinshen-ditto/LICENSE.mediapipe "$app/Contents/Resources/licenses/MediaPipe.txt"
cp THIRD_PARTY.md "$app/Contents/Resources/licenses/"
if [ -n "${MOSS_CI_NATIVE_ROOT:-}" ]; then
    cp -R "$MOSS_CI_NATIVE_ROOT/licenses" "$app/Contents/Resources/licenses/ci-dependencies"
    cp config/macos-dependencies.json "$app/Contents/Resources/licenses/ci-dependencies/"
fi
"$qt_root/bin/macdeployqt" "$app" -qmldir="$PWD/qml" -no-strip
# macdeployqt normally deploys only Cocoa. Release-boundary and CI smoke checks
# deliberately run offscreen, without opening windows or reading user workspaces.
cp "$qt_root/plugins/platforms/libqoffscreen.dylib" "$app/Contents/PlugIns/platforms/"
# The app does not use SQL. Qt's optional SQL plugins can reference databases
# installed on the Qt builder's machine, so exclude that unused plugin group.
if [ -d "$app/Contents/PlugIns/sqldrivers" ]; then
    excluded="$(mktemp -d "$PWD/.local/excluded-qt-plugins.XXXXXX")"
    mv "$app/Contents/PlugIns/sqldrivers" "$excluded/"
fi
# Build-time helper only; the public Mac app runs locally without Python.
python3 scripts/bundle_native.py "$app"
if [ "$with_models" = 1 ]; then
    python3 scripts/public_release.py models "$model_root" "$app/Contents/Resources/models" --quant "$image_quant"
fi
codesign --force --deep --sign - "$app"
codesign --verify --deep --strict "$app"
python3 scripts/public_release.py app "$app"
echo "$app"
