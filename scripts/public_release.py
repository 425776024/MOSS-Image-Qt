#!/usr/bin/env python3
"""Export public source or a hash-verified model allowlist; never copy local trees."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
MODEL_SUFFIXES = {'.npz', '.engine', '.onnx', '.pth', '.pt', '.ckpt', '.gguf', '.safetensors', '.mlmodel', '.task', '.fst', '.bin', '.dylib', '.a', '.so', '.mp4', '.mov'}
MODEL_SUFFIXES |= {'.p12', '.p8', '.pem', '.key', '.keychain', '.keychain-db', '.mobileprovision', '.provisionprofile'}
SOURCE_DIRS = ('src', 'qml', 'config', 'scripts', 'tests', 'vendor', 'assets', '.github')
SOURCE_DOCS = ('LICENSE', 'README.md', 'README-en.md', 'THIRD_PARTY.md', 'CMakeLists.txt', '.gitignore', '.gitattributes',
               'AGENTS.md', 'DESIGN.md', '.cursor/rules/macos-design.mdc', 'docs/macos-design-skills.md',
               'docs/model-management.md', 'docs/ditto-default-model.md', 'docs/build.md',
               'docs/macos-release.md', 'docs/apple-model-assets.md', 'docs/privacy.md', 'docs/model-terms.md')

# Exact, hash-pinned showcase exceptions. Local exports remain forbidden.
PUBLIC_DEMO_MEDIA = {
    'assets/showcase/avatar-demo-01.mp4': '19f00fc12efed60f4869010394d7da2c39fe536214a45ceb681575d1ff2c5137',
    'assets/showcase/avatar-demo-02.mp4': 'a5d3649fe6b6cf4e6f1e8750bb27fc826d6e66340a51dcecbbf112cdbc02f705',
}

PUBLIC_SCRIPTS = {
    "audit_bundle.py", "build.sh", "build_media_dependencies.sh", "bundle_native.py",
    "package.sh", "prepare_audio.sh", "public_release.py", "run-local.sh", "run_background_test.sh",
    "prepare_ming.py", "prepare_ci_macos.py", "build_ci_macos.sh", "macos_distribution.py", "import_apple_secrets.py",
    "apple_model_assets.py", "upload_apple_assets.py", "package_appstore.py",
    "configure_appstore_upload.py", "appstore_keychain_service.swift",
}

PRIVATE_SERVER_FILES = {
    'config/gpu-server.json', 'config/gpu-known-hosts',
    'scripts/gpu_test.py', 'scripts/gpu_test_remote.py',
    'scripts/remote_image_worker.py', 'scripts/serve_gpu_test_models.py',
    'scripts/compare_gguf_1024.py', 'scripts/build_gguf_comparison_gallery.py',
    'scripts/ocr_comparison.swift', 'tests/remote_image_worker_tests.py',
}

def audit_app(app):
    for path in app.rglob('*'):
        if path.is_file() and path.name in {Path(p).name for p in PRIVATE_SERVER_FILES}:
            raise ValueError('Server payload in App: ' + str(path))
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software')
    result = subprocess.run([str(app.absolute() / 'Contents/MacOS/MossStudio'),
                             '--headless', '--public-release-check'],
                            capture_output=True, text=True, timeout=10, env=env)
    if result.returncode or result.stdout.strip() != 'MOSS_PUBLIC_LOCAL_ONLY_V1':
        raise ValueError('App is not a verified local-only public build')
    return 1

def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(8 * 1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()

def catalog():
    return json.loads((ROOT / 'config/models.json').read_text())['files']

def safe(root, relative):
    rel = Path(relative)
    if rel.is_absolute() or '..' in rel.parts:
        raise ValueError('Unsafe catalog path: ' + relative)
    path = root / rel
    for candidate in (path, *path.parents):
        if candidate.is_symlink():
            raise ValueError('Symlink is not permitted: ' + str(candidate))
        if candidate == root:
            break
    return path

def copy_models(source, destination, quant):
    groups = {'image-q4' if quant == 'Q4_K' else 'image-q8', 'image-shared', 'ditto', 'voice', 'sfx'}
    files = [f for f in catalog() if f['group'] in groups]
    # Validate the whole set BEFORE writing anything. Private denoisers cannot
    # pass this allowlist even if renamed to the public filename.
    plan = []
    for f in files:
        src = safe(source, f['path'])
        if not src.is_file() or src.stat().st_size != f['bytes'] or digest(src) != f['sha256']:
            raise ValueError('Public model missing or hash mismatch: ' + f['path'])
        plan.append((src, safe(destination, f['path'])))
    if destination.exists():
        raise ValueError('Destination must not already exist: ' + str(destination))
    for src, dst in plan:
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
    return len(plan)

def source_files():
    paths = [ROOT / name for name in SOURCE_DOCS]
    for name in SOURCE_DIRS:
        paths += [p for p in (ROOT / name).rglob('*') if p.is_file() or p.is_symlink()]
    allowed = []
    for path in sorted(set(paths)):
        relative = path.relative_to(ROOT)
        if any(part in {'__pycache__', '.DS_Store', '.git'} for part in relative.parts) or path.suffix in {'.pyc', '.pyo'}:
            continue
        if relative.as_posix() in PRIVATE_SERVER_FILES:
            raise ValueError("Private server file in public tree: " + str(relative))
        if relative.parts[0] == "scripts" and relative.as_posix() not in {"scripts/" + name for name in PUBLIC_SCRIPTS}:
            raise ValueError("Unapproved release script: " + str(relative))
        safe(ROOT, str(relative))
        if path.suffix.lower() in MODEL_SUFFIXES or '.mlpackage' in str(relative):
            expected = PUBLIC_DEMO_MEDIA.get(relative.as_posix())
            if not expected or digest(path) != expected:
                raise ValueError('Payload in public source tree: ' + str(relative))
        if not path.is_file():
            raise ValueError('Missing source: ' + str(relative))
        allowed.append(relative)
    return allowed

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    sub.add_parser('audit')
    app = sub.add_parser('app');app.add_argument('path', type=Path)
    export = sub.add_parser('source');export.add_argument('destination', type=Path)
    models = sub.add_parser('models');models.add_argument('source', type=Path);models.add_argument('destination', type=Path);models.add_argument('--quant', choices=['Q4_K', 'Q8_0'], default='Q8_0')
    args = parser.parse_args()
    if args.command == 'app':
        count = audit_app(args.path)
    elif args.command == 'models':
        count = copy_models(args.source.absolute(), args.destination.absolute(), args.quant)
    else:
        files = source_files();count = len(files)
        if args.command == 'source':
            destination = args.destination.absolute()
            if destination.exists():
                raise ValueError('Destination must not exist')
            for relative in files:
                dst = safe(destination, str(relative));dst.parent.mkdir(parents=True, exist_ok=True);shutil.copy2(ROOT / relative, dst)
        # Git add is intentionally never performed by this script.
        tracked = subprocess.check_output(['git','ls-files','-z'], cwd=ROOT).decode().split('\0')
        forbidden = [p for p in tracked if p and (p in PRIVATE_SERVER_FILES or p.startswith(('.local/', 'runs/', 'dist/', 'build')) or (Path(p).suffix.lower() in MODEL_SUFFIXES and p not in PUBLIC_DEMO_MEDIA))]
        if forbidden:
            raise ValueError('Private/generated payload already tracked: ' + ', '.join(forbidden))
    print(json.dumps({'action': args.command, 'files': count, 'privateModelsIncluded': False}))

if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr);sys.exit(1)
