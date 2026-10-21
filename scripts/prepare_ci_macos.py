#!/usr/bin/env python3
"""Prepare arm64 dependencies from public, pinned sources; never import local SDKs."""
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / '.local/ci'
NATIVE = BASE / 'native'
LOCK = json.loads((ROOT / 'config/macos-dependencies.json').read_text())
JOBS = os.environ.get('BUILD_JOBS', '3')


def run(*args, **kwargs):
    subprocess.run([str(a) for a in args], check=True, **kwargs)


def download(name):
    entry = LOCK[name]
    path = BASE / 'downloads' / name
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        partial = path.with_suffix('.part')
        run('curl', '--fail', '--location', '--silent', '--show-error', '--retry', '3',
            '--proto', '=https', '--proto-redir', '=https', entry['url'], '-o', partial)
        partial.replace(path)
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != entry['sha256']:
        raise ValueError(f'Checksum mismatch: {path}; remove it and retry')
    return path


def unpack(name):
    archive = download(name)
    destination = BASE / 'sources' / name
    if not destination.exists():
        destination.mkdir(parents=True)
        with tarfile.open(archive) as tar:
            # Python 3.12's data filter rejects traversal and escaping links.
            tar.extractall(destination, filter='data')
    roots = list(destination.iterdir())
    if len(roots) != 1 or not roots[0].is_dir():
        raise ValueError(f'Expected one archive root: {name}')
    return roots[0]


def cmake(source, name, *options):
    build = BASE / 'build' / name
    run('cmake', '-S', source, '-B', build, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_OSX_ARCHITECTURES=arm64',
        '-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0', *options)
    return build


def main():
    if platform.system() != 'Darwin' or platform.machine() != 'arm64':
        raise SystemExit('This release targets Apple Silicon macOS only.')
    NATIVE.mkdir(parents=True, exist_ok=True)
    notices = NATIVE / 'licenses'
    notices.mkdir(exist_ok=True)
    for name in ('onnxruntime', 'sherpa'):
        source = unpack(name)
        target = NATIVE / 'audio-deps' / name
        # The voice backend only needs these three FST archives and headers.
        # Do not accidentally substitute sherpa's bundled ONNX runtime.
        shutil.copytree(source / 'include', target / 'include', dirs_exist_ok=True)
        (target / 'lib').mkdir(parents=True, exist_ok=True)
        libraries = list((source / 'lib').glob('*.dylib')) if name == 'onnxruntime' else [
            source / 'lib' / f'libsherpa-onnx-{part}.a' for part in ('kaldifst-core', 'fstfar', 'fst')]
        for library in libraries:
            shutil.copy2(library, target / 'lib' / library.name)
        for pattern in ('LICENSE*', '*Notices*', 'Copyright*'):
            for file in source.glob(pattern):
                if file.is_file():
                    shutil.copy2(file, notices / f'{name}-{file.name}')

    # Sherpa's release archive omits these internal headers. Use the exact
    # versions pinned by sherpa 1.13.2 / kaldi-decoder 0.3.0 for ABI compatibility.
    kaldifst, openfst = unpack('kaldifst'), unpack('openfst')
    include = NATIVE / 'audio-deps/sherpa/include'
    shutil.copytree(kaldifst / 'kaldifst', include / 'kaldifst', dirs_exist_ok=True)
    shutil.copytree(openfst / 'src/include/fst', include / 'fst', dirs_exist_ok=True)
    shutil.copy2(kaldifst / 'LICENSE', notices / 'kaldifst-LICENSE')
    shutil.copy2(openfst / 'COPYING', notices / 'OpenFST-COPYING')

    mediapipe = NATIVE / 'mediapipe'
    mediapipe.mkdir(exist_ok=True)
    with zipfile.ZipFile(download('mediapipe-wheel')) as archive:
        (mediapipe / 'libmediapipe.dylib').write_bytes(archive.read('mediapipe/tasks/c/libmediapipe.dylib'))
        (notices / 'MediaPipe-LICENSE').write_bytes(archive.read('mediapipe-0.10.35.dist-info/licenses/LICENSE'))
    source = unpack('mediapipe-source')
    shutil.copytree(source / 'mediapipe/tasks/c', mediapipe / 'include/mediapipe/tasks/c',
                    ignore=shutil.ignore_patterns('*.cc', 'BUILD'), dirs_exist_ok=True)
    run('install_name_tool', '-id', '@rpath/libmediapipe.dylib', mediapipe / 'libmediapipe.dylib')
    run('codesign', '--force', '--sign', '-', mediapipe / 'libmediapipe.dylib')

    ffmpeg = unpack('ffmpeg')
    ffbuild = BASE / 'build/ffmpeg'
    ffbuild.mkdir(parents=True, exist_ok=True)
    run(ffmpeg / 'configure', f'--prefix={NATIVE / "ffmpeg"}', '--arch=arm64', '--target-os=darwin',
        '--cc=/usr/bin/clang', '--cxx=/usr/bin/clang++', '--enable-shared', '--disable-static',
        '--disable-gpl', '--disable-nonfree', '--disable-version3', '--disable-autodetect',
        '--enable-zlib', '--enable-videotoolbox', '--enable-audiotoolbox', '--enable-pthreads',
        '--enable-neon', '--enable-pic', '--disable-debug', '--disable-doc', '--disable-programs',
        '--install-name-dir=@rpath', '--extra-cflags=-mmacosx-version-min=15.0',
        '--extra-ldflags=-mmacosx-version-min=15.0', cwd=ffbuild)
    run('make', '-j', JOBS, cwd=ffbuild)
    run('make', 'install', cwd=ffbuild)
    shutil.copy2(ffmpeg / 'COPYING.LGPLv2.1', notices / 'FFmpeg-LGPL-2.1.txt')

    opencv = unpack('opencv')
    build = cmake(opencv, 'opencv', f'-DCMAKE_INSTALL_PREFIX={NATIVE / "opencv"}',
                  '-DBUILD_LIST=core,imgproc,dnn', '-DBUILD_SHARED_LIBS=ON',
                  '-DBUILD_TESTS=OFF', '-DBUILD_PERF_TESTS=OFF', '-DBUILD_EXAMPLES=OFF',
                  '-DBUILD_opencv_apps=OFF', '-DBUILD_JAVA=OFF', '-DBUILD_opencv_python3=OFF',
                  '-DWITH_FFMPEG=OFF', '-DWITH_GSTREAMER=OFF', '-DWITH_OPENCL=OFF',
                  '-DWITH_IPP=OFF', '-DWITH_ITT=OFF', '-DWITH_PROTOBUF=OFF',
                  '-DOPENCV_DNN_OPENCL=OFF', '-DOPENCV_DNN_ONNX=OFF')
    # DNN is used for blobFromImage only; inference uses ONNX Runtime.
    run('cmake', '--build', build, '--parallel', JOBS)
    run('cmake', '--install', build)
    shutil.copy2(opencv / 'LICENSE', notices / 'OpenCV-LICENSE')

    sa3 = BASE / 'sources/sa3'
    entry = LOCK['sa3']
    if not sa3.exists():
        run('git', 'clone', '--no-checkout', entry['repository'], sa3)
        run('git', '-C', sa3, 'checkout', '--detach', entry['revision'])
    head = subprocess.check_output(['git', '-C', str(sa3), 'rev-parse', 'HEAD'], text=True).strip()
    if head != entry['revision']:
        raise ValueError('sa3 source does not match the pinned revision')
    run('git', '-C', sa3, 'submodule', 'update', '--init', '--recursive')
    build = cmake(sa3, 'sa3', '-DSA3_METAL=ON', '-DGGML_METAL_EMBED_LIBRARY=ON',
                  '-DBUILD_SHARED_LIBS=OFF', '-DBUILD_TESTING=OFF', '-DGGML_CCACHE=OFF', '-DGGML_NATIVE=OFF')
    run('cmake', '--build', build, '--target', 'sa3-generate', '--parallel', JOBS)
    shutil.copy2(sa3 / 'LICENSE', notices / 'sa3-LICENSE')
    print('Public native dependencies ready:', NATIVE)


if __name__ == '__main__':
    main()
