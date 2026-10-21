#!/usr/bin/env python3
"""Create App ZIP/DMG artifacts, or sign and notarize them in an ephemeral keychain."""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import secrets
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SECRET_NAMES = ('APPLE_CERTIFICATE_P12_BASE64', 'APPLE_CERTIFICATE_PASSWORD',
                'APPLE_NOTARY_KEY_P8', 'APPLE_NOTARY_KEY_ID', 'APPLE_NOTARY_ISSUER_ID')
MAGIC = {b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf', b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca',
         b'\xca\xfe\xba\xbf', b'\xbf\xba\xfe\xca'}


def run(*args, **kwargs):
    # Never echo commands: security/notarytool arguments can contain credentials.
    env = {k: v for k, v in os.environ.items() if k not in SECRET_NAMES}
    kwargs.setdefault('check', True)
    return subprocess.run([str(a) for a in args], env=env, **kwargs)


def sha256(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def code_items(app):
    """Libraries/helpers first, then inner bundles, finally the outer app."""
    files, bundles = [], []
    for path in app.rglob('*'):
        if path.is_symlink():
            continue
        if path.is_file():
            with path.open('rb') as stream:
                if stream.read(4) in MAGIC:
                    files.append(path)
        elif path.is_dir() and path.suffix in ('.app', '.framework', '.xpc', '.bundle'):
            bundles.append(path)
    return sorted(files, key=lambda p: len(p.parts), reverse=True) + sorted(
        bundles, key=lambda p: len(p.parts), reverse=True) + [app]


def zip_app(app, output):
    run('/usr/bin/ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', app, output)


def create_dmg(app, output):
    if output.exists():
        raise ValueError(f'Refusing to overwrite {output}')
    with tempfile.TemporaryDirectory(prefix='moss-dmg-') as tmp:
        stage = Path(tmp) / 'volume'
        stage.mkdir()
        run('/usr/bin/ditto', app, stage / app.name)
        (stage / 'Applications').symlink_to('/Applications')
        run('/usr/bin/hdiutil', 'create', '-volname', 'Moss Studio', '-srcfolder', stage,
            '-format', 'UDZO', '-fs', 'HFS+', output)
    run('/usr/bin/hdiutil', 'verify', output)


def notarize(path, credentials, work):
    result = run('/usr/bin/xcrun', 'notarytool', 'submit', path, *credentials,
                 '--wait', '--timeout', '30m', '--output-format', 'json',
                 stdout=subprocess.PIPE, text=True, check=False)
    try:
        submission = json.loads(result.stdout)
    except json.JSONDecodeError:
        raise ValueError(f'Notary service returned no JSON result (exit {result.returncode})') from None
    if result.returncode or submission.get('status') != 'Accepted':
        print(f'Notary submission: {submission.get("id", "unavailable")}; status: {submission.get("status", "unknown")}')
        if submission.get('id') and submission.get('status') in ('Invalid', 'Rejected'):
            log = work / 'notary-log.json'
            run('/usr/bin/xcrun', 'notarytool', 'log', submission['id'], *credentials, log)
            print(log.read_text())
        raise ValueError(f'Notarization did not succeed: {submission.get("status")}')
    print(f'Notarization accepted: {path.name}; submission {submission["id"]}')
    return submission['id']


def artifacts(app, output, signed=False, identity=None, keychain=None, credentials=None, work=None):
    with (app / 'Contents/Info.plist').open('rb') as stream:
        info = plistlib.load(stream)
    version = info['CFBundleShortVersionString']
    if not re.fullmatch(r'[0-9]+(?:\.[0-9]+){1,3}', version):
        raise ValueError('Invalid bundle version')
    suffix = 'notarized' if signed else 'unsigned'
    stem = f'Moss-Studio-{version}-macOS-arm64-{suffix}'
    output.mkdir(parents=True, exist_ok=True)
    archive, dmg = output / f'{stem}.app.zip', output / f'{stem}.dmg'
    if archive.exists() or dmg.exists():
        raise ValueError('Output already exists; use a fresh directory')
    submissions = []
    if signed:
        submission_zip = work / 'app-for-notarization.zip'
        zip_app(app, submission_zip)
        submissions.append(notarize(submission_zip, credentials, work))
        run('/usr/bin/xcrun', 'stapler', 'staple', app)
        run('/usr/bin/xcrun', 'stapler', 'validate', app)
        run('/usr/sbin/spctl', '--assess', '--type', 'execute', '--verbose=2', app)
    zip_app(app, archive)
    create_dmg(app, dmg)
    if signed:
        run('/usr/bin/codesign', '--force', '--sign', identity, '--keychain', keychain,
            '--timestamp', dmg)
        submissions.append(notarize(dmg, credentials, work))
        run('/usr/bin/xcrun', 'stapler', 'staple', dmg)
        run('/usr/bin/xcrun', 'stapler', 'validate', dmg)
        run('/usr/sbin/spctl', '--assess', '--type', 'open', '--context', 'context:primary-signature', dmg)
    files = {p.name: sha256(p) for p in (archive, dmg)}
    (output / 'SHA256SUMS.txt').write_text(''.join(f'{h}  {name}\n' for name, h in files.items()))
    (output / 'release-manifest.json').write_text(json.dumps({
        'version': version, 'architecture': 'arm64', 'minimumMacOS': '15.0',
        'commit': os.environ.get('GITHUB_SHA', ''), 'notarized': signed,
        'modelWeightsIncluded': False, 'notarySubmissionIds': submissions, 'sha256': files,
    }, indent=2) + '\n')
    print(f'Created {suffix} App ZIP and DMG in {output}')


def sign_notarize(app, output):
    missing = [k for k in SECRET_NAMES if not os.environ.get(k)]
    if missing:
        raise ValueError('Missing macos-release environment secrets: ' + ', '.join(missing))
    os.umask(0o077)
    with tempfile.TemporaryDirectory(prefix='moss-signing-') as tmp:
        work = Path(tmp)
        keychain = work / 'signing.keychain-db'
        password = secrets.token_urlsafe(32)
        p12, p8 = work / 'identity.p12', work / 'notary.p8'
        p12.write_bytes(base64.b64decode(os.environ['APPLE_CERTIFICATE_P12_BASE64'], validate=True))
        p8.write_text(os.environ['APPLE_NOTARY_KEY_P8'])
        credentials = ['--key', p8, '--key-id', os.environ['APPLE_NOTARY_KEY_ID'],
                       '--issuer', os.environ['APPLE_NOTARY_ISSUER_ID']]
        try:
            run('/usr/bin/security', 'create-keychain', '-p', password, keychain)
            run('/usr/bin/security', 'set-keychain-settings', '-lut', '7200', keychain)
            run('/usr/bin/security', 'unlock-keychain', '-p', password, keychain)
            # A fresh runner may lack the intermediate for the exported identity.
            # Import public Apple intermediates only; never alter trust settings.
            lock = json.loads((ROOT / 'config/macos-dependencies.json').read_text())
            for name in ('developer-id-ca', 'developer-id-g2-ca'):
                certificate = work / f'{name}.cer'
                run('/usr/bin/curl', '--fail', '--silent', '--show-error', '--location', '--retry', '3',
                    '--proto', '=https', '--proto-redir', '=https', lock[name]['url'], '-o', certificate)
                if sha256(certificate) != lock[name]['sha256']:
                    raise ValueError('Apple intermediate certificate checksum mismatch')
                run('/usr/bin/security', 'import', certificate, '-k', keychain, stdout=subprocess.DEVNULL)
            run('/usr/bin/security', 'import', p12, '-k', keychain,
                '-P', os.environ['APPLE_CERTIFICATE_PASSWORD'], '-T', '/usr/bin/codesign',
                stdout=subprocess.DEVNULL)
            run('/usr/bin/security', 'set-key-partition-list', '-S', 'apple-tool:,apple:,codesign:',
                '-s', '-k', password, keychain, stdout=subprocess.DEVNULL)
            identities = run('/usr/bin/security', 'find-identity', '-v', '-p', 'codesigning', keychain,
                             stdout=subprocess.PIPE, text=True).stdout
            matches = re.findall(r'\b([A-Fa-f0-9]{40})\s+"Developer ID Application:[^"\n]+"', identities)
            if len(matches) != 1:
                raise ValueError('P12 must contain exactly one valid Developer ID Application identity')
            identity = matches[0]
            for item in code_items(app):
                args = ['/usr/bin/codesign', '--force', '--sign', identity, '--keychain', keychain,
                        '--timestamp', '--options', 'runtime']
                if item == app:
                    args += ['--entitlements', ROOT / 'config/macos-app.entitlements']
                run(*args, item)
            run('/usr/bin/codesign', '--verify', '--deep', '--strict', app)
            artifacts(app, output, True, identity, keychain, credentials, work)
        finally:
            if keychain.exists():
                run('/usr/bin/security', 'delete-keychain', keychain)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('package', 'sign-notarize'))
    parser.add_argument('app', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    app = args.app.resolve()
    if not (app / 'Contents/MacOS/MossStudio').is_file():
        raise ValueError('Expected a deployed Moss Studio.app')
    if args.command == 'sign-notarize':
        sign_notarize(app, args.output.resolve())
    else:
        run('/usr/bin/codesign', '--verify', '--deep', '--strict', app)
        artifacts(app, args.output.resolve())


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        # CalledProcessError.__str__ includes the command, which may contain a password.
        raise SystemExit(f'Distribution command failed (exit {error.returncode}). See preceding tool diagnostics.') from None
    except (ValueError, OSError) as error:
        raise SystemExit(str(error)) from None
