#!/usr/bin/env python3
"""Publish verified default-branch CI artifacts as immutable development Releases."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

REPO = '425776024/MOSS-Image-Qt'
BRANCH = 'public-release'
PLATFORMS = {
    '.github/workflows/macos.yml': ('macos', 'macOS', 'macOS-arm64-unsigned', 'SHA256SUMS.txt'),
    '.github/workflows/windows.yml': ('windows', 'Windows', 'Windows-x64-preview', 'SHA256SUMS-windows-x64.txt'),
}


def gh(*args):
    return subprocess.check_output(['gh', *map(str, args)], text=True)


def api(path):
    return json.loads(gh('api', f'repos/{REPO}/{path}'))


def validate_run(run):
    if (run.get('event') != 'push' or run.get('head_branch') != BRANCH
            or run.get('status') != 'completed' or run.get('conclusion') != 'success'
            or run.get('head_repository', {}).get('full_name') != REPO
            or run.get('path') not in PLATFORMS
            or not re.fullmatch(r'[0-9a-f]{40}', run.get('head_sha', ''))):
        raise ValueError('Only successful macOS/Windows push builds from public-release may be published')
    return PLATFORMS[run['path']]


def verified_assets(folder, platform, checksums, sha):
    entries = {}
    checksum_file = folder / checksums
    if checksum_file.is_symlink():
        raise ValueError('Checksum file must not be a symlink')
    for line in checksum_file.read_text().splitlines():
        match = re.fullmatch(r'([0-9a-f]{64})  ([A-Za-z0-9_.-]+)', line)
        if not match or match[2] in entries:
            raise ValueError('Invalid or duplicate checksum entry')
        digest, name = match.groups()
        path = folder / name
        if path.is_symlink() or not path.is_file():
            raise ValueError('Missing package or unsafe path')
        with path.open('rb') as stream:
            actual = hashlib.sha256()
            for block in iter(lambda: stream.read(8 * 1024 * 1024), b''):
                actual.update(block)
            if actual.hexdigest() != digest:
                raise ValueError(f'Checksum mismatch: {name}')
        entries[name] = digest
    suffixes = (('-macOS-arm64-unsigned.app.zip', '-macOS-arm64-unsigned.dmg')
                if platform == 'macos' else ('-Windows-x64-preview.zip',))
    if len(entries) != len(suffixes) or any(
            sum(name.startswith('Moss-Studio-') and name.endswith(suffix) for name in entries) != 1
            for suffix in suffixes):
        raise ValueError('Unexpected development package set')
    names = set(entries) | {checksums}
    if platform == 'macos':
        manifest_path = folder / 'release-manifest.json'
        if manifest_path.is_symlink():
            raise ValueError('Manifest must not be a symlink')
        manifest = json.loads(manifest_path.read_text())
        if (manifest.get('commit') != sha or manifest.get('notarized') is not False
                or manifest.get('modelWeightsIncluded') is not False or manifest.get('sha256') != entries):
            raise ValueError('Manifest does not match this unsigned, model-free build')
        names.add(manifest_path.name)
    if {p.name for p in folder.iterdir()} != names:
        raise ValueError('Unexpected files in build artifact')
    return [folder / name for name in sorted(names)]


def latest_success(run):
    workflow = Path(run['path']).name
    runs = api(f'actions/workflows/{workflow}/runs?branch={BRANCH}&event=push&status=success&per_page=1')
    return bool(runs['workflow_runs']) and runs['workflow_runs'][0]['id'] == run['id']


def publish(run_id):
    if os.environ.get('GH_REPO', REPO) != REPO:
        raise ValueError('Unexpected repository')
    run = api(f'actions/runs/{run_id}')
    platform, label, artifact, checksums = validate_run(run)
    sha = run['head_sha']
    if api(f'compare/{sha}...{BRANCH}')['status'] not in ('ahead', 'identical'):
        raise ValueError('Build commit is no longer on the release branch')
    if not latest_success(run):
        print('A newer successful build exists; skipping this older run.')
        return
    tag = f'{platform}-build-{run_id}'
    existing = subprocess.run(['gh', 'release', 'view', tag, '--repo', REPO,
                               '--json', 'isDraft'], capture_output=True, text=True)
    if existing.returncode == 0 and not json.loads(existing.stdout)['isDraft']:
        print(f'Already published: https://github.com/{REPO}/releases/tag/{tag}')
        return
    with tempfile.TemporaryDirectory(prefix='moss-preview-') as tmp:
        root = Path(tmp)
        folder = root / 'artifacts'
        gh('run', 'download', run_id, '--repo', REPO, '--name', artifact, '--dir', folder)
        assets = verified_assets(folder, platform, checksums, sha)
        provenance = root / 'build-source.json'
        provenance.write_text(json.dumps({'commit': sha, 'runId': run_id,
            'workflow': run['path'], 'buildUrl': run['html_url'], 'builtAt': run['updated_at'],
            'developmentPreview': True, 'notarized': False}, indent=2) + '\n')
        assets.append(provenance)
        qualification = ('**macOS 开发测试包，未经 Apple 公证，仅有 ad-hoc 签名。** '
                         '需要已公证安装包时，请选择下载目录中的已公证发布版。\n\n'
                         '**macOS development build: ad-hoc signed, not Apple notarized.** '
                         'Choose a notarized release for a notarized installer.' if platform == 'macos' else
                         '**Windows 开发测试包。** 完整解压 ZIP 后运行 MossStudio.exe；'
                         'GPU 推理与真实桌面验收仍需实机测试。\n\n'
                         '**Windows development build.** Extract the complete ZIP before running MossStudio.exe. '
                         'GPU inference and desktop validation still require a real PC.')
        notes = root / 'notes.md'
        notes.write_text(f'{qualification}\n\n'
                         f'源码 / Source: [{sha[:7]}](https://github.com/{REPO}/commit/{sha})\n\n'
                         f'构建 / Build: [{run_id}]({run["html_url"]})\n\n'
                         f'构建完成 / Built: {run["updated_at"]}\n\n'
                         f'校验 / Checksums: `{checksums}`. 模型按需下载；无需 Python。\n\n'
                         '后续主线构建成功后会自动发布新的开发包。'
                         'Successful release-branch builds automatically publish new development packages.\n')
        title = f'{label} 开发版 · {sha[:7]}' + ('（未公证）' if platform == 'macos' else '')
        if not latest_success(run):
            print('A newer successful build completed while downloading; skipping this run.')
            return
        if existing.returncode:
            gh('release', 'create', tag, '--repo', REPO, '--target', sha, '--draft',
               '--prerelease', '--latest=false', '--title', title, '--notes-file', notes)
        gh('release', 'upload', tag, *assets, '--repo', REPO, '--clobber')
        # The public listing changes only after all verified assets are uploaded.
        gh('release', 'edit', tag, '--repo', REPO, '--draft=false', '--prerelease',
           '--latest=false', '--title', title, '--notes-file', notes)
        print(f'Published: https://github.com/{REPO}/releases/tag/{tag}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run_id', type=int)
    publish(parser.parse_args().run_id)
