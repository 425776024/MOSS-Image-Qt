#!/usr/bin/env python3
"""Deploy a model-free Windows ZIP and validate it with development paths hidden."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def run(*args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, **kwargs)


def package(qt, prefix, native, image_build, sfx_build, build):
    destination = ROOT / 'dist/windows/MossStudio'
    if destination.exists():
        raise ValueError(f'Preserve the previous package before rebuilding: {destination}')
    destination.mkdir(parents=True)
    for source in (build / 'MossStudio.exe', build / 'workers/moss-ditto.exe',
                   build / 'workers/moss-voice.exe', image_build / 'bin/sd-cli.exe',
                   sfx_build / 'bin/sa3-generate.exe'):
        shutil.copy2(source, destination)
    run(qt / 'bin/windeployqt.exe', '--release', '--qmldir', ROOT / 'qml',
        '--compiler-runtime', '--dir', destination, destination / 'MossStudio.exe')
    # Dependencies loaded at runtime (ORT providers and Vulkan) are not all
    # visible to windeployqt. Ship the pinned release DLLs and their notices.
    for folder in (prefix / 'bin', native / 'onnxruntime/lib', native / 'bin',
                   native / 'ming/bin', native / 'mediapipe'):
        for dll in folder.glob('*.dll'):
            if 'debug' in dll.name.lower():
                continue
            target = destination / dll.name
            if target.exists() and target.read_bytes() != dll.read_bytes():
                raise ValueError(f'DLL collision: {dll.name}')
            shutil.copy2(dll, target)
    shutil.copytree(ROOT / 'assets/voices', destination / 'assets/voices')
    licenses = destination / 'licenses'
    shutil.copytree(native / 'licenses', licenses)
    for notice in (prefix / 'share').rglob('*'):
        if notice.is_file() and notice.name.lower().startswith(('copyright', 'license', 'copying', 'notice')):
            target = licenses / 'vcpkg' / notice.relative_to(prefix / 'share')
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(notice, target)
    for filename in ('windows-dependencies.json', 'macos-dependencies.json'):
        shutil.copy2(ROOT / 'config' / filename, licenses)
    shutil.copy2(ROOT / 'config/windows-vcpkg/vcpkg.json', licenses / 'vcpkg-manifest.json')
    shutil.copytree(ROOT / 'vendor/videocut-voice/licenses', licenses / 'voice')
    shutil.copy2(ROOT / 'vendor/videocut-voice/PROVENANCE.md', licenses / 'voice')
    for source in ('LICENSE', 'THIRD_PARTY.md', 'docs/model-terms.md'):
        shutil.copy2(ROOT / source, licenses)
    for source in ('test_windows.ps1', 'test_windows.cmd'):
        shutil.copy2(ROOT / 'scripts' / source, destination)
    shutil.copy2(ROOT / 'docs/windows-release.md', destination / 'README-Windows.md')
    # A portable package must never import a developer's model tree or Python.
    forbidden = {'.py', '.pyc', '.pyd', '.whl', '.gguf', '.onnx', '.safetensors', '.pth', '.pt', '.nupkg'}
    for path in destination.rglob('*'):
        if path.is_file() and (path.suffix.lower() in forbidden or 'python' in path.name.lower()):
            raise ValueError(f'Forbidden payload: {path}')
    from public_release import audit_app
    test_root = ROOT / '.local/ci-windows/deployed-smoke'
    test_root.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, MOSS_DISABLE_DEV_PATHS='1',
        MOSS_EVIDENCE_ROOT=str(test_root), MOSS_OUTPUT_ROOT=str(test_root / 'works'),
        MOSS_SETTINGS_ROOT=str(test_root / 'settings'),
        PATH=os.pathsep.join([str(destination), str(Path(os.environ['SystemRoot']) / 'System32'),
                              os.environ['SystemRoot']]))
    # Also check all imported DLLs, including delay imports, in the package.
    dlls = []
    system = Path(os.environ['SystemRoot']) / 'System32'
    for binary in destination.rglob('*'):
        if binary.suffix.lower() not in ('.exe', '.dll'):
            continue
        output = subprocess.check_output(['dumpbin', '/nologo', '/dependents', str(binary)], text=True)
        for dependency in re.findall(r'^\s+([\w.+-]+\.dll)\s*$', output, re.M | re.I):
            if dependency.lower().startswith(('api-ms-', 'ext-ms-')):
                continue
            # Windows DLL lookup is case-insensitive; Path.exists follows it.
            if not (destination / dependency).exists() and not (system / dependency).exists():
                raise ValueError(f'Unresolved import: {binary.name}: {dependency}')
            dlls.append({'file': binary.relative_to(destination).as_posix(), 'dependency': dependency})
    # Audit without inheriting Qt/plugin/search paths from the build shell.
    saved = os.environ.copy()
    try:
        os.environ.clear()
        os.environ.update(env)
        audit_app(destination)
        for language in ('zh', 'en'):
            for theme in ('light', 'dark'):
                run(destination / 'MossStudio.exe', '--home-smoke', '--minimum-window',
                    '--ui-language', language, '--ui-theme', theme, env=env, timeout=90)
        run(destination / 'sd-cli.exe', '--help', env=env, timeout=30, stdout=subprocess.DEVNULL)
        run(destination / 'sa3-generate.exe', '--help', env=env, timeout=30, stdout=subprocess.DEVNULL)
        for worker in ('moss-voice.exe', 'moss-ditto.exe'):
            result = subprocess.run([str(destination / worker)], env=env, timeout=30,
                capture_output=True, text=True, encoding='utf-8', errors='replace')
            if result.returncode != 1 or 'usage:' not in (result.stdout + result.stderr):
                raise ValueError(f'Native worker failed to load: {worker}: {result.returncode}')
    finally:
        os.environ.clear()
        os.environ.update(saved)
    evidence = ROOT / 'dist/windows-bundle-audit.json'
    evidence.write_text(json.dumps({'platform': 'Windows x64', 'modelFree': True,
        'pythonFree': True, 'deployedSmoke': True, 'gpuInferenceValidated': False,
        'imports': dlls}, indent=2) + '\n')
    artifacts = ROOT / 'dist/windows-artifacts'
    artifacts.mkdir(parents=True, exist_ok=True)
    archive = artifacts / 'Moss-Studio-0.1.0-Windows-x64-preview.zip'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
        for path in sorted(destination.rglob('*')):
            if path.is_file():
                z.write(path, path.relative_to(destination.parent))
    with archive.open('rb') as f:
        checksum = hashlib.file_digest(f, 'sha256').hexdigest()
    (artifacts / 'SHA256SUMS-windows-x64.txt').write_text(f'{checksum}  {archive.name}\n')
    print('Windows preview package:', archive)
