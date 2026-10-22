#!/usr/bin/env python3
"""Build the complete x64 Windows package from pinned public dependencies.

Run in an x64 MSVC 2022 developer shell with QT_ROOT set. Python is only a
build tool; no Python runtime, wheel or model is included in the distribution.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / '.local/ci-windows'
NATIVE = BASE / 'native'
BUILD = ROOT / 'build-windows'
LOCK = json.loads((ROOT / 'config/windows-dependencies.json').read_text())
SHARED = json.loads((ROOT / 'config/macos-dependencies.json').read_text())
JOBS = os.environ.get('BUILD_JOBS', '3')


def run(*args, **kwargs):
    subprocess.run([str(a) for a in args], check=True, **kwargs)


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def download(name, entry):
    path = BASE / 'downloads' / name
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        part = path.with_suffix('.part')
        run('curl.exe', '--fail', '--location', '--retry', '3', '--proto', '=https',
            '--proto-redir', '=https', entry['url'], '-o', part)
        part.replace(path)
    if digest(path) != entry['sha256']:
        raise ValueError(f'Checksum mismatch: {path}')
    return path


def checkout(name, entry):
    source = BASE / 'sources' / name
    source.parent.mkdir(parents=True, exist_ok=True)
    if not source.exists():
        run('git', 'clone', '--filter=blob:none', '--no-checkout', entry['repository'], source)
        run('git', '-C', source, 'checkout', '--detach', entry['revision'])
    head = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    dirty = subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain'], text=True).strip()
    if head != entry['revision'] or dirty:
        raise ValueError(f'Expected a clean pinned checkout: {source}')
    run('git', '-C', source, 'submodule', 'update', '--init', '--recursive')
    return source


def unzip(archive, destination, prefix):
    destination.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as z:
        for name in z.namelist():
            if not name.startswith(prefix) or name.endswith('/'):
                continue
            relative = Path(name[len(prefix):])
            if relative.is_absolute() or '..' in relative.parts:
                raise ValueError('Unsafe archive member')
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(z.read(name))


def cmake(source, build, *options):
    manifest = (ROOT / 'config/windows.manifest').as_posix()
    run('cmake', '-S', source, '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
        '-DCMAKE_C_FLAGS=/utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX',
        '-DCMAKE_CXX_FLAGS=/utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_USE_MATH_DEFINES',
        f'-DCMAKE_EXE_LINKER_FLAGS=/MANIFEST:EMBED /MANIFESTINPUT:"{manifest}"',
        '-DBUILD_SHARED_LIBS=OFF', '-DGGML_NATIVE=OFF', '-DGGML_AVX=OFF',
        '-DGGML_AVX2=OFF', '-DGGML_FMA=OFF', '-DGGML_F16C=OFF',
        '-DGGML_AVX512=OFF', '-DGGML_CCACHE=OFF', *options)


def main():
    if os.name != 'nt':
        raise SystemExit('Run this script on Windows in an x64 MSVC developer shell.')
    os.chdir(ROOT)
    qt = Path(os.environ['QT_ROOT'])
    NATIVE.mkdir(parents=True, exist_ok=True)
    notices = NATIVE / 'licenses'
    notices.mkdir(exist_ok=True)
    vcpkg = checkout('vcpkg', LOCK['vcpkg'])
    run(vcpkg / 'bootstrap-vcpkg.bat', '-disableMetrics')
    installed = BASE / 'vcpkg_installed'
    run(vcpkg / 'vcpkg.exe', 'install', '--triplet=x64-windows',
        f'--x-manifest-root={ROOT / "config/windows-vcpkg"}', f'--x-install-root={installed}')
    prefix = installed / 'x64-windows'
    # Keep vcpkg DLLs visible to tests, not to the final isolated smoke test.
    os.environ['PATH'] = os.pathsep.join([str(qt / 'bin'), str(prefix / 'bin'),
        str(NATIVE / 'bin'), os.environ['PATH']])
    os.environ['PKG_CONFIG_PATH'] = str(prefix / 'lib/pkgconfig')
    glslc = prefix / 'tools/shaderc/glslc.exe'
    if not glslc.exists():
        raise ValueError('shaderc did not provide glslc.exe')
    gpu = [f'-DCMAKE_TOOLCHAIN_FILE={(vcpkg / "scripts/buildsystems/vcpkg.cmake").as_posix()}',
           f'-DVCPKG_INSTALLED_DIR={installed.as_posix()}', '-DVCPKG_MANIFEST_MODE=OFF',
           '-DVCPKG_TARGET_TRIPLET=x64-windows', f'-DVulkan_GLSLC_EXECUTABLE={glslc.as_posix()}']

    ort = NATIVE / 'onnxruntime'
    archive = download('onnxruntime-directml.nupkg', LOCK['onnxruntime-directml'])
    unzip(archive, ort / 'include', 'build/native/include/')
    unzip(archive, ort / 'lib', 'runtimes/win-x64/native/')
    with zipfile.ZipFile(archive) as z:
        for name in z.namelist():
            if Path(name).name in ('LICENSE', 'ThirdPartyNotices.txt'):
                (notices / ('onnxruntime-' + Path(name).name)).write_bytes(z.read(name))
    # Only notices are retained from the original archives at packaging time.
    dml = download('directml.nupkg', LOCK['directml'])
    unzip(dml, NATIVE / 'bin', 'bin/x64-win/')
    unzip(dml, ort / 'include', 'include/')
    with zipfile.ZipFile(dml) as z:
        for name in z.namelist():
            if 'license' in name.lower() and not name.endswith('/'):
                (notices / ('DirectML-' + Path(name).name)).write_bytes(z.read(name))
    mp = NATIVE / 'mediapipe'
    mp.mkdir(exist_ok=True)
    wheel = download('mediapipe.whl', LOCK['mediapipe-wheel'])
    with zipfile.ZipFile(wheel) as z:
        (mp / 'mediapipe.dll').write_bytes(z.read('mediapipe/tasks/c/libmediapipe.dll'))
        (notices / 'MediaPipe-LICENSE').write_bytes(z.read('mediapipe-0.10.35.dist-info/licenses/LICENSE'))
    archive = download('mediapipe-source.tar.gz', SHARED['mediapipe-source'])
    source = BASE / 'sources/mediapipe'
    source.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive) as tar:
        tar.extractall(source, filter='data')
    mp_source = next(source.iterdir())
    shutil.copytree(mp_source / 'mediapipe/tasks/c', mp / 'include/mediapipe/tasks/c',
                    ignore=shutil.ignore_patterns('*.cc', 'BUILD'), dirs_exist_ok=True)
    exports = subprocess.check_output(['dumpbin', '/nologo', '/exports', str(mp / 'mediapipe.dll')], text=True)
    names = re.findall(r'^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(Mp\w+)\s*$', exports, re.M)
    if not {'MpFaceLandmarkerCreate', 'MpFaceLandmarkerDetectImage'} <= set(names):
        raise ValueError('MediaPipe DLL lacks the required C ABI')
    definition = mp / 'mediapipe.def'
    definition.write_text('LIBRARY mediapipe.dll\nEXPORTS\n' + '\n'.join(names) + '\n')
    run('lib', '/nologo', '/machine:x64', f'/def:{definition}', f'/out:{mp / "mediapipe.lib"}')

    engine = checkout('stable-diffusion', LOCK['stable-diffusion'])
    engine_build = BASE / 'build/image'
    cmake(engine, engine_build, *gpu, '-DSD_VULKAN=ON', '-DSD_WEBP=OFF', '-DSD_WEBM=OFF')
    run('cmake', '--build', engine_build, '--target', 'sd-cli', '--parallel', JOBS)
    shutil.copy2(engine / 'LICENSE', notices / 'stable-diffusion-LICENSE')
    sa3 = checkout('sa3', SHARED['sa3'])
    sfx_build = BASE / 'build/sfx'
    cmake(sa3, sfx_build, *gpu, '-DSA3_VULKAN=ON', '-DBUILD_TESTING=OFF')
    run('cmake', '--build', sfx_build, '--target', 'sa3-generate', '--parallel', JOBS)
    shutil.copy2(sa3 / 'LICENSE', notices / 'sa3-LICENSE')
    ming = checkout('ming', SHARED['ming-audio-cpp'])
    ming_build = BASE / 'build/ming'
    cmake(ming, ming_build, *gpu, '-DAUDIOCPP_MODEL_SET=custom', '-DAUDIOCPP_MODELS=ming3_tts',
          '-DAUDIOCPP_BUILD_C_API=ON', '-DAUDIOCPP_DEPLOYMENT_BUILD=ON',
          '-DENGINE_ENABLE_VULKAN=ON', '-DENGINE_ENABLE_METAL=OFF',
          '-DENGINE_ENABLE_NATIVE_CPU=OFF', '-DENGINE_ENABLE_OPENMP=OFF',
          '-DENGINE_ENABLE_LLAMAFILE=OFF', '-DENGINE_BUILD_TESTS=OFF',
          '-DAUDIOCPP_STATIC_ESPEAK=OFF', '-DAUDIOCPP_BUILD_NATIVE_MODEL_MANAGER=OFF')
    run('cmake', '--build', ming_build, '--target', 'audiocpp', '--parallel', JOBS)
    ming_root = NATIVE / 'ming'
    for folder in ('bin', 'lib', 'include'):
        (ming_root / folder).mkdir(parents=True, exist_ok=True)
    shutil.copy2(ming_build / 'bin/audiocpp.dll', ming_root / 'bin')
    shutil.copy2(ming_build / 'bin/audiocpp.lib', ming_root / 'lib')
    shutil.copy2(ming / 'include/audiocpp.h', ming_root / 'include')
    for name, source in (('ming', ming), ('image', engine), ('sfx', sa3)):
        for path in source.rglob('*'):
            if path.is_file() and path.name.startswith(('LICENSE', 'COPYING', 'NOTICE')):
                target = notices / name / path.relative_to(source)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, target)
    os.environ['PATH'] = os.pathsep.join([str(ort / 'lib'), str(mp), str(ming_root / 'bin'), os.environ['PATH']])
    os.environ['PATH'] = str(prefix / 'tools/ffmpeg') + os.pathsep + os.environ['PATH']
    pkgconfigs = list((vcpkg / 'downloads/tools').rglob('pkgconf.exe'))
    pkgconf = pkgconfigs[0] if pkgconfigs else shutil.which('pkg-config')
    if not pkgconf:
        raise ValueError('vcpkg did not install pkgconf')
    cmake(ROOT, BUILD, *gpu, f'-DCMAKE_PREFIX_PATH={qt.as_posix()};{prefix.as_posix()}',
          f'-DPKG_CONFIG_EXECUTABLE={Path(pkgconf).as_posix()}',
          f'-DONNXRUNTIME_ROOT={ort.as_posix()}', f'-DMOSS_MING_ROOT={ming_root.as_posix()}',
          f'-DMOSS_MEDIAPIPE_ROOT={mp.as_posix()}',
          f'-DJSON_INCLUDE={(prefix / "include").as_posix()}',
          f'-DVOICE_JSON_INCLUDE={(prefix / "include").as_posix()}')
    run('cmake', '--build', BUILD, '--parallel', JOBS)
    # Side-loaded DLLs are needed beside test executables as well as on PATH.
    run('ctest', '--test-dir', BUILD, '--output-on-failure', '--timeout', '180')
    from package_windows import package
    package(qt, prefix, NATIVE, engine_build, sfx_build, BUILD)


if __name__ == '__main__':
    main()
