#!/usr/bin/env python3
"""Sign an already deployed Store app and create its upload installer.

Uses existing keychain identities; never exports private keys. This does not
upload or claim App Review acceptance. Input must be a disposable release copy.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile

from macos_distribution import code_items, run, sha256

ROOT = Path(__file__).resolve().parents[1]


def remove_unused_timeline(app):
    """macdeployqt includes optional GPL Timeline plugins unused by this app."""
    relative = ('Contents/Frameworks/QtQuickTimeline.framework',
                'Contents/Frameworks/QtQuickTimelineBlendTrees.framework',
                'Contents/PlugIns/quick/libqtquicktimelineplugin.dylib',
                'Contents/PlugIns/quick/libqtquicktimelineblendtreesplugin.dylib',
                'Contents/Resources/qml/QtQuick/Timeline')
    targets = [app / name for name in relative]
    for source in (ROOT / 'qml').rglob('*.qml'):
        if 'QtQuick.Timeline' in source.read_text():
            raise ValueError(f'App now requires Qt Quick Timeline: {source.name}')
    for item in code_items(app):
        if not item.is_file() or any(item == target or target in item.parents for target in targets):
            continue
        dependencies = run('/usr/bin/otool', '-L', item, stdout=subprocess.PIPE, text=True).stdout
        if 'QtQuickTimeline' in dependencies:
            raise ValueError(f'Qt Timeline dependency remains: {item.relative_to(app)}')
    for target in targets:
        if target.is_dir():
            shutil.rmtree(target)
        else:
            target.unlink(missing_ok=True)


def use_system_crypto(app):
    """Keep macOS TLS; reject workers still depending on bundled OpenSSL."""
    tls = app / 'Contents/PlugIns/tls'
    if not (tls / 'libqsecuretransportbackend.dylib').is_file():
        raise ValueError('Missing macOS SecureTransport TLS backend')
    removable = {'libqopensslbackend.dylib', 'libcrypto.3.dylib', 'libssl.3.dylib'}
    for item in code_items(app):
        if not item.is_file() or item.name in removable:
            continue
        dependencies = run('/usr/bin/otool', '-L', item, stdout=subprocess.PIPE, text=True).stdout
        if any(name in dependencies for name in ('libcrypto.', 'libssl.')):
            raise ValueError(f'OpenSSL dependency remains: {item.relative_to(app)}')
    for item in (tls / 'libqopensslbackend.dylib',
                 app / 'Contents/Frameworks/libcrypto.3.dylib',
                 app / 'Contents/Frameworks/libssl.3.dylib'):
        item.unlink(missing_ok=True)
    inventory = app / 'Contents/Resources/native-dependencies.json'
    if inventory.exists():
        entries = json.loads(inventory.read_text())
        if not isinstance(entries, list):
            raise ValueError('Unexpected native dependency inventory format')
        inventory.write_text(json.dumps([entry for entry in entries
            if Path(entry['bundled']).name not in removable], indent=2) + '\n')


def profile_entitlements(path, bundle_id, team, identity, template):
    raw = run('/usr/bin/security', 'cms', '-D', '-i', path,
              stdout=subprocess.PIPE).stdout
    profile = plistlib.loads(raw)
    if profile['ExpirationDate'].replace(tzinfo=datetime.timezone.utc) <= datetime.datetime.now(datetime.timezone.utc):
        raise ValueError(f'Expired profile: {path.name}')
    permitted = profile['Entitlements']
    app_id = f'{team}.{bundle_id}'
    if permitted.get('com.apple.application-identifier') != app_id:
        raise ValueError(f'Profile does not match {app_id}')
    if permitted.get('com.apple.developer.team-identifier') != team:
        raise ValueError('Profile team mismatch')
    certificates = [hashlib.sha1(c).hexdigest().upper() for c in profile['DeveloperCertificates']]
    if identity.upper() not in certificates:
        raise ValueError('Signing certificate is absent from profile')
    if profile.get('ProvisionedDevices') or profile.get('ProvisionsAllDevices') or permitted.get('get-task-allow'):
        raise ValueError('Expected a Store distribution profile')
    entitlements = plistlib.loads(template.read_bytes())
    for group in entitlements.get('com.apple.security.application-groups', []):
        if group not in permitted.get('com.apple.security.application-groups', []):
            raise ValueError(f'Profile does not grant application group {group}')
    entitlements['com.apple.application-identifier'] = app_id
    entitlements['com.apple.developer.team-identifier'] = team
    return entitlements


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app', type=Path)
    parser.add_argument('--main-profile', required=True, type=Path)
    parser.add_argument('--extension-profile', required=True, type=Path)
    parser.add_argument('--identity', required=True)
    parser.add_argument('--installer-identity', required=True)
    parser.add_argument('--team', required=True)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--check-only', action='store_true')
    args = parser.parse_args()
    app = args.app.resolve()
    info = plistlib.loads((app / 'Contents/Info.plist').read_bytes())
    if info.get('CFBundleIdentifier') != 'ai.ffclip.studio' or not info.get('BAUsesAppleHosting'):
        raise ValueError('Expected the ai.ffclip.studio Apple-hosted Store build')
    extension = app / 'Contents/Extensions/XinshenAssets.appex'
    extension_info = plistlib.loads((extension / 'Contents/Info.plist').read_bytes())
    for key in ('CFBundleShortVersionString', 'CFBundleVersion'):
        if info[key] != extension_info[key]:
            raise ValueError(f'Extension version mismatch: {key}')
    targets = [(app, args.main_profile, ROOT / 'config/macos-store-app.entitlements', info),
               (extension, args.extension_profile, ROOT / 'config/apple-assets/extension.entitlements', extension_info)]
    checked = [(target, profile, profile_entitlements(profile, metadata['CFBundleIdentifier'],
                args.team, args.identity, template)) for target, profile, template, metadata in targets]
    for name in ('sd-cli', 'moss-ditto', 'moss-voice', 'sa3-generate'):
        if not (app / 'Contents/Helpers' / name).is_file():
            raise ValueError(f'Missing runtime helper: {name}')
    if args.output.exists():
        raise ValueError('Output exists; use a fresh installer path')
    qt_notices = app / 'Contents/Resources/licenses/qt'
    for notice in ('source-manifest.json', 'qtbase/LICENSES/LGPL-3.0-only.txt'):
        if not (qt_notices / notice).is_file():
            raise ValueError(f'Missing Qt distribution material: {notice}; set MOSS_QT_NOTICES_DIR when packaging')
    if args.check_only:
        print('Store bundle structure and provisioning profiles verified; no files changed.')
        return
    use_system_crypto(app)
    remove_unused_timeline(app)
    with tempfile.TemporaryDirectory(prefix='xinshen-store-sign-') as temporary:
        entitlements = {}
        for index, (target, profile, values) in enumerate(checked):
            # Downloaded profiles may carry quarantine metadata; embed bytes only.
            shutil.copyfile(profile, target / 'Contents/embedded.provisionprofile')
            path = Path(temporary) / f'{index}.plist'
            path.write_bytes(plistlib.dumps(values))
            entitlements[target] = path
        # Apple rejects quarantine attributes anywhere in the submitted bundle.
        run('/usr/bin/xattr', '-dr', 'com.apple.quarantine', app)
        for item in code_items(app):
            options = ['--force', '--sign', args.identity, '--timestamp']
            if item in entitlements:
                options += ['--entitlements', entitlements[item]]
            elif item.parent == app / 'Contents/Helpers':
                options += ['--entitlements', ROOT / 'config/macos-store-helper.entitlements']
            run('/usr/bin/codesign', *options, item)
        run('/usr/bin/codesign', '--verify', '--deep', '--strict', '--verbose=2', app)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        run('/usr/bin/productbuild', '--component', app, '/Applications',
            '--sign', args.installer_identity, args.output)
        run('/usr/sbin/pkgutil', '--check-signature', args.output)
        args.output.with_suffix('.manifest.json').write_text(json.dumps({
            'bundleId': info['CFBundleIdentifier'], 'version': info['CFBundleShortVersionString'],
            'build': info['CFBundleVersion'], 'sha256': sha256(args.output),
            'uploaded': False,
        }, indent=2) + '\n')
        print(f'Signed installer ready for validation: {args.output}')


if __name__ == '__main__':
    main()
