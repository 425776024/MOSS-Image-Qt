"""Build immutable, independently reusable Windows CUDA component archives."""
import hashlib
import json
from pathlib import Path
import re
import zipfile

REPOSITORY = '425776024/MOSS-Image-Qt'
RUNTIME_TAG = 'gpu-runtime-components-v1'
DOWNLOAD_BASE = f'https://github.com/{REPOSITORY}/releases/download/{RUNTIME_TAG}/'


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def safe_name(name):
    return (bool(re.fullmatch(r'[A-Za-z0-9_./-]+', name))
            and all(p not in ('', '.', '..') and not p.endswith('.') for p in name.split('/')))


def build_components(native, output, required):
    """The manifest contains exact archive and unpacked-file hashes, no app version."""
    output.mkdir(parents=True, exist_ok=True)
    receipt = json.loads((native / 'cuda/runtime-files.json').read_text())
    groups = receipt['components']
    components, provided = [], set()
    for name, dlls in sorted(groups.items()):
        sources = {filename: native / 'cuda' / filename for filename in dlls}
        notices = native / 'licenses/cuda' / name
        if not notices.is_dir() or not any(notices.iterdir()):
            raise ValueError(f'Missing redistributable notices for {name}')
        sources.update({f'licenses/{name}/{p.name}': p for p in notices.iterdir() if p.is_file()})
        rows = []
        for path, source in sorted(sources.items()):
            if not safe_name(path) or source.is_symlink() or not source.is_file():
                raise ValueError(f'Unsafe runtime file: {path}')
            sha = digest(source)
            if path in dlls and sha != receipt['files'][path]:
                raise ValueError(f'Runtime changed after verification: {path}')
            if path in provided:
                raise ValueError(f'Runtime component collision: {path}')
            provided.add(path)
            rows.append(dict(path=path, bytes=source.stat().st_size, sha256=sha))
        archive = output / f'{name}.zip.tmp'
        # Fixed timestamps and permissions keep the same asset hash across app builds.
        with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as z:
            for row in rows:
                entry = zipfile.ZipInfo(row['path'], date_time=(1980, 1, 1, 0, 0, 0))
                entry.compress_type = zipfile.ZIP_DEFLATED
                entry.create_system = 3
                entry.external_attr = 0o100644 << 16
                with sources[row['path']].open('rb') as source, z.open(entry, 'w', force_zip64=True) as target:
                    import shutil
                    shutil.copyfileobj(source, target, 1024 * 1024)
        sha = digest(archive)
        filename = f'{name}-{sha}.zip'
        archive.replace(output / filename)
        size = (output / filename).stat().st_size
        if size >= 2 * 1024**3:
            raise ValueError('Runtime component exceeds the release asset limit')
        components.append(dict(name=name, id=sha, url=DOWNLOAD_BASE + filename,
                               archive=filename, bytes=size, sha256=sha, files=rows))
    if not set(required) <= provided:
        raise ValueError('CUDA component set is incomplete')
    identity = hashlib.sha256(json.dumps([c['id'] for c in components], separators=(',', ':')).encode()).hexdigest()
    manifest = dict(schema=1, id=identity, platform='windows-x64', components=components)
    (output / 'cuda-runtime.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    return manifest


def verify_components(folder):
    manifest = json.loads((folder / 'cuda-runtime.json').read_text())
    if manifest.get('schema') != 1 or manifest.get('platform') != 'windows-x64' or not manifest.get('components'):
        raise ValueError('Invalid runtime manifest')
    expected, paths = {'cuda-runtime.json'}, set()
    for component in manifest['components']:
        name = component['archive']
        if not re.fullmatch(r'[a-z0-9-]+-[0-9a-f]{64}\.zip', name) or component['url'] != DOWNLOAD_BASE + name:
            raise ValueError('Invalid runtime download location')
        if component['id'] != component['sha256'] or component['sha256'] not in name:
            raise ValueError('Runtime identity mismatch')
        archive = folder / name
        if archive.is_symlink() or archive.stat().st_size != component['bytes'] or digest(archive) != component['sha256']:
            raise ValueError('Runtime archive checksum mismatch')
        expected.add(name)
        rows = {row['path']: row for row in component['files']}
        if len(rows) != len(component['files']) or not rows:
            raise ValueError('Duplicate or missing component files')
        with zipfile.ZipFile(archive) as z:
            if sorted(z.namelist()) != sorted(rows):
                raise ValueError('Runtime archive members do not match the manifest')
            for member in z.infolist():
                path = member.filename
                if not safe_name(path) or path in paths or (member.external_attr >> 16) & 0o170000 not in (0, 0o100000):
                    raise ValueError('Unsafe runtime archive member')
                paths.add(path)
                row = rows[path]
                with z.open(member) as stream:
                    actual = hashlib.file_digest(stream, 'sha256').hexdigest()
                if member.file_size != row['bytes'] or actual != row['sha256']:
                    raise ValueError('Runtime file checksum mismatch')
    identity = hashlib.sha256(json.dumps([c['id'] for c in manifest['components']], separators=(',', ':')).encode()).hexdigest()
    if manifest['id'] != identity or {p.name for p in folder.iterdir()} != expected:
        raise ValueError('Runtime artifact set mismatch')
    return manifest
