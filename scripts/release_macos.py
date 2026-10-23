#!/usr/bin/env python3
"""One-click local notarization and publication of the latest macOS CI build."""
import argparse
import json
import os
from pathlib import Path, PurePosixPath
import platform
import posixpath
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import zipfile

import macos_distribution as distribution
import publish_preview as preview

ROOT = Path(__file__).resolve().parents[1]


def say(message):
    print(message, flush=True)


def signing_identity(text, requested=None):
    identities = re.findall(r'\b([A-Fa-f0-9]{40})\s+"(Developer ID Application:[^"\n]+)"', text)
    valid = {fingerprint.upper() for fingerprint, name in identities if 'self-signed' not in name.lower()}
    if requested:
        if requested.upper() not in valid:
            raise ValueError('指定的 Developer ID Application 签名身份不可用。')
        return requested.upper()
    if len(valid) != 1:
        raise ValueError('需要一个正式 Developer ID Application 身份；多个身份时使用 --identity 指定指纹。')
    return next(iter(valid))


def local_env(sha, work):
    env = {k: v for k, v in os.environ.items()
           if k not in (*distribution.SECRET_NAMES, 'GH_TOKEN', 'GITHUB_TOKEN',
                        'QT_PLUGIN_PATH', 'QML2_IMPORT_PATH', 'QML_IMPORT_PATH',
                        'DYLD_LIBRARY_PATH', 'DYLD_FRAMEWORK_PATH')}
    env.update(GITHUB_SHA=sha, MOSS_DISABLE_DEV_PATHS='1', QT_QPA_PLATFORM='offscreen',
               QT_QUICK_BACKEND='software', MOSS_OUTPUT_ROOT=str(work / 'smoke/works'),
               MOSS_SETTINGS_ROOT=str(work / 'smoke/settings'), MOSS_EVIDENCE_ROOT=str(work / 'smoke/evidence'))
    return env


def select_run(run_id=None):
    if run_id:
        run = preview.api(f'actions/runs/{run_id}')
    else:
        sha = preview.api(f'branches/{preview.BRANCH}')['commit']['sha']
        runs = preview.api(f'actions/workflows/macos.yml/runs?branch={preview.BRANCH}&event=push&head_sha={sha}&per_page=1')['workflow_runs']
        if not runs:
            raise ValueError('最新主线提交尚无 macOS 构建，请在 Actions 启动构建或使用 --run-id。')
        run = runs[0]
    if run.get('status') != 'completed':
        say(f'等待 macOS 构建完成：{run["html_url"]}')
        preview.gh('run', 'watch', run['id'], '--repo', preview.REPO, '--interval', '30', '--exit-status')
        run = preview.api(f'actions/runs/{run["id"]}')
    if preview.validate_run(run)[0] != 'macos':
        raise ValueError('所选构建不是 macOS。')
    if preview.api(f'compare/{run["head_sha"]}...{preview.BRANCH}')['status'] not in ('ahead', 'identical'):
        raise ValueError('构建提交不在当前发布分支历史中。')
    return run


def validate_archive(archive):
    # ditto preserves framework links; reject any link or member escaping the app.
    with zipfile.ZipFile(archive) as package:
        for item in package.infolist():
            path = PurePosixPath(item.filename)
            if (path.is_absolute() or '..' in path.parts or not path.parts
                    or path.parts[0] not in ('Moss Studio.app', '__MACOSX')):
                raise ValueError('App ZIP 含不安全或意外的路径。')
            if stat.S_ISLNK(item.external_attr >> 16):
                target = package.read(item).decode('utf-8')
                resolved = PurePosixPath(posixpath.normpath(posixpath.join(str(path.parent), target)))
                if target.startswith('/') or resolved.parts[0] != 'Moss Studio.app':
                    raise ValueError('App ZIP 含指向应用外部的符号链接。')


def release_snapshot(tag):
    result = subprocess.run(['gh', 'api', f'repos/{preview.REPO}/releases/tags/{tag}'],
                            capture_output=True, text=True)
    if result.returncode:
        if 'HTTP 404' in result.stderr:
            return None
        raise ValueError('无法读取 GitHub Release，请检查 gh 登录和网络。')
    return json.loads(result.stdout)


def verify_uploaded(snapshot, assets):
    remote = {item['name']: item for item in snapshot['assets']}
    for path in assets:
        item = remote.get(path.name, {})
        if (item.get('size') != path.stat().st_size
                or item.get('digest') != 'sha256:' + distribution.sha256(path)):
            raise ValueError(f'上传后的校验不一致：{path.name}；保留本地产物，重跑脚本即可重试。')


def publish_signed(run, assets, work):
    sha, run_id = run['head_sha'], run['id']
    tag = f'macos-build-{run_id}'
    old = release_snapshot(tag)
    if old and preview.api(f'compare/{sha}...{tag}')['status'] != 'identical':
        raise ValueError('现有发布标签不对应所选构建，停止更新。')
    source = work / 'build-source.json'
    source.write_text(json.dumps({'commit': sha, 'runId': run_id, 'buildUrl': run['html_url'],
        'builtAt': run['updated_at'], 'notarized': True, 'signingLocation': 'local-keychain'}, indent=2) + '\n')
    assets = [*assets, source]
    if old and not old['draft']:
        try:
            verify_uploaded(old, assets)
            if not any('-unsigned.' in item['name'] for item in old['assets']) and '已公证' in old['name']:
                say(f'已是相同的已公证发布：{old["html_url"]}')
                return old['html_url']
        except ValueError:
            pass
    notes = work / 'release-notes.md'
    notes.write_text('**macOS 安装包已完成 Developer ID 签名和 Apple 公证。**\n\n'
        '支持 Apple Silicon、macOS 15+。下载 DMG 后拖入 Applications，或使用 App ZIP。'
        '无需 Python；模型按需下载。App 与 DMG 均已附公证票据，通过 Gatekeeper 检查。\n\n'
        '**Developer ID signed and Apple notarized.** Both the app and DMG are stapled and '
        'pass Gatekeeper assessment. Apple Silicon, macOS 15+. No Python required.\n\n'
        f'源码 / Source: [{sha[:7]}](https://github.com/{preview.REPO}/commit/{sha})\n\n'
        f'构建 / Build: [{run_id}]({run["html_url"]})\n\n'
        '校验 / Checksums: `SHA256SUMS.txt`；公证记录 / Notarization receipts: `release-manifest.json`。\n\n'
        '此版本仍为预览版；签名和公证不代表所有模型推理、性能与实际安装场景均已验收。\n')
    title = f'macOS 开发版 · {sha[:7]}（已公证）'
    if not old:
        preview.gh('release', 'create', tag, '--repo', preview.REPO, '--target', sha, '--draft',
                   '--prerelease', '--latest=false', '--title', title, '--notes-file', notes)
    say('上传并核对 GitHub 安装包…')
    for path in assets:
        current = release_snapshot(tag)
        item = next((a for a in current['assets'] if a['name'] == path.name), {})
        if item.get('digest') != 'sha256:' + distribution.sha256(path):
            preview.gh('release', 'upload', tag, path, '--repo', preview.REPO, '--clobber')
    verify_uploaded(release_snapshot(tag), assets)
    preview.gh('release', 'edit', tag, '--repo', preview.REPO, '--draft=false', '--prerelease',
               '--latest=false', '--title', title, '--notes-file', notes)
    # Remove only the superseded unsigned installers from this same build.
    for asset in release_snapshot(tag)['assets']:
        if re.fullmatch(r'Moss-Studio-[0-9.]+-macOS-arm64-unsigned\.(?:app\.zip|dmg)', asset['name']):
            preview.gh('release', 'delete-asset', tag, asset['name'], '--repo', preview.REPO, '--yes')
    final = release_snapshot(tag)
    verify_uploaded(final, assets)
    if final['draft'] or '已公证' not in final['name']:
        raise ValueError('发布状态未更新，请重试。')
    return final['html_url']


def release(args):
    if platform.system() != 'Darwin':
        raise ValueError('本地公证发布必须在 macOS 上运行。')
    for tool in ('gh', 'xcrun', 'codesign', 'security', 'ditto'):
        if not shutil.which(tool):
            raise ValueError(f'缺少工具：{tool}')
    identities = subprocess.check_output(['/usr/bin/security', 'find-identity', '-v', '-p', 'codesigning'], text=True)
    identity = signing_identity(identities, args.identity)
    subprocess.run(['/usr/bin/xcrun', 'notarytool', 'history', '--keychain-profile', args.keychain_profile,
                    '--output-format', 'json'], check=True, stdout=subprocess.DEVNULL)
    run = select_run(args.run_id)
    sha = run['head_sha']
    say(f'本次构建：{run["id"]} / {sha[:7]}')
    work = args.work_root.resolve() / str(run['id'])
    work.mkdir(parents=True, exist_ok=True)
    import fcntl
    with (work / 'release.lock').open('w') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise ValueError('同一构建的发布正在运行，请等待该窗口完成。') from None
        signed = work / 'notarized'
        if not signed.exists():
            incoming = work / 'incoming'
            if not incoming.exists():
                say('下载并校验 GitHub 构建…')
                with tempfile.TemporaryDirectory(prefix='download-', dir=work) as tmp:
                    preview.gh('run', 'download', run['id'], '--repo', preview.REPO,
                               '--name', 'macOS-arm64-unsigned', '--dir', tmp)
                    preview.verified_assets(Path(tmp), 'macos', 'SHA256SUMS.txt', sha)
                    Path(tmp).rename(incoming)
            preview.verified_assets(incoming, 'macos', 'SHA256SUMS.txt', sha)
            archive = next(incoming.glob('*.app.zip'))
            validate_archive(archive)
            attempt = Path(tempfile.mkdtemp(prefix='attempt-', dir=work))
            subprocess.run(['/usr/bin/ditto', '-x', '-k', str(archive), str(attempt)], check=True)
            app = attempt / 'Moss Studio.app'
            env = local_env(sha, work)
            log_path = attempt / 'notarization.log'
            say(f'正在签名、公证 App 和 DMG；日志：{log_path}')
            with log_path.open('w') as log:
                def checked(*command):
                    subprocess.run(list(map(str, command)), check=True, env=env,
                                   stdout=log, stderr=subprocess.STDOUT)
                checked(sys.executable, ROOT / 'scripts/public_release.py', 'app', app)
                checked(sys.executable, ROOT / 'scripts/macos_distribution.py', 'sign-notarize-local', app,
                        '--identity', identity, '--keychain-profile', args.keychain_profile,
                        '--output', attempt / 'artifacts')
                subprocess.run([str(app / 'Contents/MacOS/MossStudio'), '--home-smoke'],
                               check=True, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=90)
            preview.verified_assets(attempt / 'artifacts', 'macos', 'SHA256SUMS.txt', sha, notarized=True)
            (attempt / 'artifacts').rename(signed)
        assets = preview.verified_assets(signed, 'macos', 'SHA256SUMS.txt', sha, notarized=True)
        dmg = next(signed.glob('*.dmg'))
        subprocess.run(['/usr/bin/xcrun', 'stapler', 'validate', str(dmg)], check=True)
        subprocess.run(['/usr/sbin/spctl', '--assess', '--type', 'open', '--context',
                        'context:primary-signature', str(dmg)], check=True)
        if not args.run_id and not preview.latest_success(run):
            raise ValueError('公证期间已有更新的成功构建；本次公证结果已保留，请重跑以发布最新版。')
        url = publish_signed(run, assets, work)
        say(f'公证发布完成：{url}\n本地产物：{signed}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-id', type=int, help='Publish this exact successful mainline macOS build')
    parser.add_argument('--identity', default=os.environ.get('MOSS_SIGNING_IDENTITY'))
    parser.add_argument('--keychain-profile', default=os.environ.get('MOSS_NOTARY_PROFILE', 'MossStudio-release'))
    parser.add_argument('--work-root', type=Path, default=ROOT / '.local/macos-release')
    args = parser.parse_args()
    try:
        release(args)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        # Do not print subprocess arguments: authentication stays in Keychain.
        detail = str(error) if isinstance(error, (ValueError, OSError)) else '外部命令失败，请查看本次日志或 Actions 状态。'
        raise SystemExit(f'发布未完成：{detail}\n已下载或已完成公证的产物会保留，可重跑此命令。') from None


if __name__ == '__main__':
    main()
