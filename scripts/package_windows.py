#!/usr/bin/env python3
"""Deploy XAvatar and build and exercise a model-free Windows installer."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def run(*args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, **kwargs)


def deploy_msvc_runtime(destination, redist):
    libraries = {
        'CRT': ('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll'),
        'OpenMP': ('vcomp140.dll',),
    }
    for library, required in libraries.items():
        folders = sorted((redist / 'x64').glob(f'Microsoft.VC*.{library}'))
        if len(folders) != 1:
            raise ValueError(f'Expected one x64 MSVC {library} directory in {redist}')
        runtime = folders[0]
        for name in required:
            if not (runtime / name).is_file():
                raise ValueError(f'Missing redistributable runtime: {runtime / name}')
        for dll in runtime.glob('*.dll'):
            shutil.copy2(dll, destination / dll.name)


def dependency_available(name, destination, system):
    if (destination / name).is_file():
        return True
    # System32 on the CI image has Visual Studio runtimes installed already.
    # Requiring app-local copies prevents that image from hiding missing DLLs.
    if re.match(r'(?:msvcp|vcruntime|concrt|vcomp)\d', name, re.I):
        return False
    return (system / name).is_file()


def check_worker_startup(binary, env):
    result = subprocess.run([str(binary)], env=env, timeout=30,
        capture_output=True, text=True, encoding='utf-8', errors='replace')
    if result.returncode != 1 or 'usage:' not in (result.stdout + result.stderr):
        detail = (result.stdout + result.stderr)[-2000:]
        raise ValueError(f'Native worker failed to load: {binary.name}: {result.returncode}: {detail}')


def build_installer(destination, artifacts):
    compiler = os.environ.get('INNO_SETUP_COMPILER') or shutil.which('ISCC.exe')
    if not compiler:
        compiler = str(Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Inno Setup 6/ISCC.exe')
    if not Path(compiler).is_file():
        raise ValueError('Install Inno Setup 6 or set INNO_SETUP_COMPILER to ISCC.exe')
    run(compiler, f'/DPayloadDir={destination.resolve()}', f'/DArtifactsDir={artifacts.resolve()}',
        ROOT / 'config/windows-installer.iss')
    installer = artifacts / 'XAvatar-0.1.0-Windows-x64-Setup.exe'
    if not installer.is_file():
        raise ValueError('The XAvatar installer was not generated')
    return installer


def check_installer(installer, destination, test_root, env):
    installed = test_root / '安装验证-中文路径/XAvatar'
    # Keep the installed files on failure for diagnosis. Never reuse stale files.
    if installed.exists():
        raise ValueError(f'Preserve the previous installation check before rebuilding: {installed}')
    run(installer, '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', '/LANG=english',
        '/TASKS=desktopicon', f'/DIR={installed.resolve()}',
        f'/LOG={test_root / "installer.log"}', timeout=300)
    for source in destination.rglob('*'):
        if source.is_file():
            target = installed / source.relative_to(destination)
            if not target.is_file():
                raise ValueError(f'Installed file missing or damaged: {source.relative_to(destination)}')
            with target.open('rb') as actual, source.open('rb') as expected:
                if hashlib.file_digest(actual, 'sha256').digest() != hashlib.file_digest(expected, 'sha256').digest():
                    raise ValueError(f'Installed file damaged: {source.relative_to(destination)}')
    # Run from a different working directory, with build paths excluded.
    installed_env = dict(env, PATH=os.pathsep.join([str(installed),
        str(Path(os.environ['SystemRoot']) / 'System32'), os.environ['SystemRoot']]))
    public = run(installed / 'XAvatar.exe', '--headless', '--public-release-check',
        cwd=test_root, env=installed_env, timeout=30, capture_output=True, text=True)
    if public.stdout.strip() != 'MOSS_PUBLIC_LOCAL_ONLY_V1':
        raise ValueError('Installed XAvatar did not pass the public binary check')
    shortcuts = run('powershell.exe', '-NoProfile', '-Command', '''
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
$exe = Join-Path $env:XAVATAR_INSTALL_TEST_ROOT 'XAvatar.exe'
$metadata = [Diagnostics.FileVersionInfo]::GetVersionInfo($exe)
if ($metadata.OriginalFilename -ne 'XAvatar.exe' -or $metadata.ProductName -ne 'XAvatar') { throw 'Incorrect application branding' }
$shell = New-Object -ComObject Shell.Application
$links = @((Join-Path ([Environment]::GetFolderPath('Desktop')) 'XAvatar.lnk'),
    (Join-Path ([Environment]::GetFolderPath('Programs')) 'XAvatar/XAvatar.lnk'))
$results = foreach ($link in $links) {
    if (!(Test-Path -LiteralPath $link)) { throw "Missing shortcut: $link" }
    $folder = $shell.NameSpace([IO.Path]::GetDirectoryName($link))
    $item = $folder.ParseName([IO.Path]::GetFileName($link))
    [PSCustomObject]@{link = $link; target = $item.ExtendedProperty('System.Link.TargetParsingPath')}
}
ConvertTo-Json -InputObject @($results) -Compress
''', env=dict(installed_env, XAVATAR_INSTALL_TEST_ROOT=str(installed.resolve())),
        timeout=30, capture_output=True, text=True, encoding='utf-8')
    # Shell targets may use a short path. Compare the actual file identity and
    # read the Unicode shell property rather than WScript's text conversion.
    import json
    links = json.loads(shortcuts.stdout)
    (test_root / 'shortcut-check.json').write_text(json.dumps(links, ensure_ascii=False, indent=2), encoding='utf-8')
    for index, link in enumerate(links):
        shutil.copy2(link['link'], test_root / f'shortcut-{index}.lnk')
        target = link.get('target')
        if not target or not Path(target).is_file() or not os.path.samefile(target, installed / 'XAvatar.exe'):
            raise ValueError(f'Incorrect shortcut target: {link!r}; expected {installed / "XAvatar.exe"}')
    run(installed / 'XAvatar.exe', '--home-smoke', '--minimum-window', '--check-initial-setup',
        '--ui-language', 'zh', cwd=test_root, env=installed_env, timeout=90)
    # Test uninstall without deleting files owned by the user.
    sentinel = test_root / 'works/uninstall-preserves-project.txt'
    sentinel.parent.mkdir(parents=True, exist_ok=True)
    sentinel.write_text('user project', encoding='utf-8')
    uninstaller = installed / 'unins000.exe'
    run(uninstaller, '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART',
        f'/LOG={test_root / "uninstaller.log"}', cwd=test_root, timeout=120)
    if (installed / 'XAvatar.exe').exists() or sentinel.read_text(encoding='utf-8') != 'user project':
        raise ValueError('Uninstall did not remove the application cleanly while preserving user data')


def deploy_cuda_runtime(worker, native, destination):
    required = json.loads((ROOT / 'config/windows-dependencies.json').read_text())['cuda-runtime']['requiredDlls']
    source = native / 'cuda'
    missing = [name for name in required if not (source / name).is_file()]
    if missing:
        raise ValueError(f'Missing packaged CUDA runtime: {sorted(missing)}')
    target = destination / 'cuda'
    target.mkdir()
    # Only the small, app-versioned worker belongs in the installer. Its pinned
    # dependencies are independently published and cached by RuntimeManager.
    shutil.copy2(worker, target / 'moss-ditto.exe')


def package(qt, prefix, native, image_build, sfx_build, build):
    destination = ROOT / 'dist/windows/XAvatar'
    if destination.exists():
        raise ValueError(f'Preserve the previous package before rebuilding: {destination}')
    destination.mkdir(parents=True)
    for source in (build / 'XAvatar.exe', build / 'workers/moss-ditto.exe',
                   build / 'workers/moss-voice.exe', image_build / 'bin/sd-cli.exe',
                   sfx_build / 'bin/sa3-generate.exe'):
        shutil.copy2(source, destination)
    run(qt / 'bin/windeployqt.exe', '--release', '--qmldir', ROOT / 'qml',
        '--no-compiler-runtime', '--include-plugins', 'qoffscreen',
        '--dir', destination, destination / 'XAvatar.exe')
    # Bundle app-local VC DLLs so the per-user installer needs no separate
    # runtime installation or administrator privileges.
    deploy_msvc_runtime(destination, Path(os.environ['VCToolsRedistDir']))
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
    deploy_cuda_runtime(build / 'workers/moss-ditto.exe', native, destination)
    from windows_runtime import build_components, verify_components
    runtime_artifacts = ROOT / 'dist/windows-runtime-artifacts'
    required = json.loads((ROOT / 'config/windows-dependencies.json').read_text())['cuda-runtime']['requiredDlls']
    build_components(native, runtime_artifacts, required)
    verify_components(runtime_artifacts)
    shutil.copy2(runtime_artifacts / 'cuda-runtime.json', destination / 'cuda-runtime.json')
    shutil.copytree(ROOT / 'assets/voices', destination / 'assets/voices')
    shutil.copytree(ROOT / 'assets/avatars', destination / 'assets/avatars')
    licenses = destination / 'licenses'
    shutil.copytree(native / 'licenses', licenses, ignore=shutil.ignore_patterns('cuda'))
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
    for source in ('test_windows.ps1', 'test_windows.cmd', 'test_ditto_gpu.ps1'):
        shutil.copy2(ROOT / 'scripts' / source, destination)
    shutil.copy2(ROOT / 'docs/windows-release.md', destination / 'README-Windows.md')
    # The updater removes only files owned by the previous distribution. User
    # files inside the installation folder survive the directory replacement.
    (destination / 'update-files.json').write_text(json.dumps({
        'product': 'ai.ffclip.studio',
        'files': sorted([p.relative_to(destination).as_posix()
                         for p in destination.rglob('*') if p.is_file()] + ['update-files.json']),
    }, indent=2) + '\n', encoding='utf-8')
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
        MOSS_SETTINGS_ROOT=str(test_root / 'settings'), MOSS_MODEL_ROOT=str(test_root / 'models'),
        PATH=os.pathsep.join([str(destination), str(Path(os.environ['SystemRoot']) / 'System32'),
                              os.environ['SystemRoot']]))
    # Also check all imported DLLs, including delay imports, in the package.
    dlls = []
    system = Path(os.environ['SystemRoot']) / 'System32'
    for binary in [*destination.rglob('*'), *(native / 'cuda').glob('*.dll')]:
        if binary.suffix.lower() not in ('.exe', '.dll'):
            continue
        label = ('runtime-components/' + binary.name if binary.parent == native / 'cuda'
                 else binary.relative_to(destination).as_posix())
        output = subprocess.check_output(['dumpbin', '/nologo', '/dependents', str(binary)], text=True)
        for dependency in re.findall(r'^\s+([\w.+-]+\.dll)\s*$', output, re.M | re.I):
            if dependency.lower().startswith(('api-ms-', 'ext-ms-')):
                continue
            if binary.parent == native / 'cuda' and dependency.lower() in {'nvcuda.dll', 'nvml.dll'}:
                dlls.append({'file': label, 'dependency': dependency, 'providedBy': 'NVIDIA-driver'})
                continue
            # Windows DLL lookup is case-insensitive; Path.exists follows it.
            if not ((binary.parent / dependency).is_file() or dependency_available(dependency, destination, system)):
                raise ValueError(f'Unresolved import: {binary.name}: {dependency}')
            dlls.append({'file': label, 'dependency': dependency})
    # Audit without inheriting Qt/plugin/search paths from the build shell.
    saved = os.environ.copy()
    try:
        os.environ.clear()
        os.environ.update(env)
        audit_app(destination)
        flowedit = run(destination / 'sd-cli.exe', '--moss-flowedit-version',
                       env=env, timeout=30, capture_output=True, text=True)
        if flowedit.stdout.strip() != 'MOSS_FLOWEDIT_V1':
            raise ValueError('The packaged image engine is missing the FlowEdit adapter')
        for language in ('zh', 'en'):
            for theme in ('light', 'dark'):
                run(destination / 'XAvatar.exe', '--home-smoke', '--minimum-window',
                    '--home-page', 'avatars', '--check-builtin-library', '--check-library-defaults',
                    '--ui-language', language, '--ui-theme', theme, env=env, timeout=90)
        run(destination / 'sd-cli.exe', '--help', env=env, timeout=30, stdout=subprocess.DEVNULL)
        # The pinned sa3 CLI has no --help branch: no model arguments print
        # usage and return 1, just like our voice and Ditto workers.
        for worker in ('sa3-generate.exe', 'moss-voice.exe', 'moss-ditto.exe'):
            check_worker_startup(destination / worker, env)
        runtime = json.loads(subprocess.check_output(
            [str(destination / 'moss-ditto.exe'), 'runtime-check'], env=env, timeout=30, text=True))
        if not runtime.get('tensorCheck') or 'DmlExecutionProvider' not in runtime.get('providers', []):
            raise ValueError(f'Ditto runtime or DirectML provider missing: {runtime}')
        cuda_env = dict(env, XINSHEN_CUDA_RUNTIME_DIR=str(native / 'cuda'),
                        PATH=os.pathsep.join([str(native / 'cuda'), str(destination), env['PATH']]))
        cuda_runtime = json.loads(subprocess.check_output(
            [str(destination / 'cuda/moss-ditto.exe'), 'runtime-check'], env=cuda_env, timeout=30, text=True))
        if not cuda_runtime.get('tensorCheck') or 'CUDAExecutionProvider' not in cuda_runtime.get('providers', []):
            raise ValueError(f'Ditto CUDA runtime missing: {cuda_runtime}')
    finally:
        os.environ.clear()
        os.environ.update(saved)
    artifacts = ROOT / 'dist/windows-artifacts'
    artifacts.mkdir(parents=True, exist_ok=True)
    installer = build_installer(destination, artifacts)
    check_installer(installer, destination, test_root, env)
    evidence = ROOT / 'dist/windows-bundle-audit.json'
    evidence.write_text(json.dumps({'platform': 'Windows x64', 'modelFree': True,
        'pythonFree': True, 'deployedSmoke': True, 'installerValidated': True,
        'installedStartupValidated': True, 'uninstallValidated': True, 'gpuInferenceValidated': False,
        'cudaBundledInInstaller': False, 'cudaComponentsVerified': True,
        'imports': dlls}, indent=2) + '\n')
    with installer.open('rb') as f:
        checksum = hashlib.file_digest(f, 'sha256').hexdigest()
    (artifacts / 'SHA256SUMS-windows-x64.txt').write_text(f'{checksum}  {installer.name}\n')
    print('Windows installer:', installer)
