"""Exercise signing identity, archive boundaries and promotion ordering offline."""
import importlib.util
import json
from pathlib import Path
import stat
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

SCRIPTS = Path(__file__).resolve().parents[1] / 'scripts'
sys.path.insert(0, str(SCRIPTS))
spec = importlib.util.spec_from_file_location('local_release', SCRIPTS / 'release_macos.py')
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class LocalMacOSReleaseTests(unittest.TestCase):
    def test_latest_running_build_is_waited_for_and_not_replaced_by_old_success(self):
        run = dict(id=123, head_sha='a' * 40, status='in_progress', event='push',
                   head_branch='public-release', path='.github/workflows/macos.yml',
                   head_repository={'full_name': release.preview.REPO}, html_url='https://example.com/build')
        finished = dict(run, status='completed', conclusion='success')
        responses = [{'commit': {'sha': run['head_sha']}}, {'workflow_runs': [run]}, finished, {'status': 'identical'}]
        with patch.object(release.preview, 'api', side_effect=responses), patch.object(release.preview, 'gh') as gh:
            self.assertEqual(release.select_run()['id'], 123)
            gh.assert_called_once_with('run', 'watch', 123, '--repo', release.preview.REPO,
                                       '--interval', '30', '--exit-status')

    def test_failed_build_never_silently_falls_back_to_older_release(self):
        run = dict(id=123, status='completed', conclusion='failure')
        with patch.object(release.preview, 'api', return_value=run), patch.object(release.preview, 'gh') as gh:
            with self.assertRaises(ValueError):
                release.select_run(123)
            gh.assert_not_called()

    def test_auto_identity_excludes_self_signed_certificates(self):
        text = ('A' * 40 + ' "Developer ID Application: Fake (Self-Signed)"\n'
                + 'B' * 40 + ' "Developer ID Application: Real (TEAM)"')
        self.assertEqual(release.signing_identity(text), 'B' * 40)
        with self.assertRaises(ValueError):
            release.signing_identity(text, 'A' * 40)
        with self.assertRaises(ValueError):
            release.signing_identity(text + '\n' + 'C' * 40 + ' "Developer ID Application: Other (TEAM2)"')

    def test_signing_and_smoke_processes_do_not_inherit_upload_credentials(self):
        with patch.dict(release.os.environ, {'GH_TOKEN': 'private', 'GITHUB_TOKEN': 'private',
                                            'APPLE_NOTARY_KEY_P8': 'private'}):
            env = release.local_env('a' * 40, Path('/tmp/work'))
        self.assertNotIn('GH_TOKEN', env)
        self.assertNotIn('GITHUB_TOKEN', env)
        self.assertNotIn('APPLE_NOTARY_KEY_P8', env)
        self.assertEqual(env['GITHUB_SHA'], 'a' * 40)
        self.assertEqual(env['MOSS_DISABLE_DEV_PATHS'], '1')

    def test_framework_links_are_allowed_but_external_links_are_not(self):
        with tempfile.TemporaryDirectory() as tmp:
            archive = Path(tmp) / 'app.zip'
            for target, allowed in (('Versions/Current/QtCore', True), ('/tmp/escape', False),
                                    ('../../../..', False), ('../../../../../../escape', False)):
                with zipfile.ZipFile(archive, 'w') as z:
                    link = zipfile.ZipInfo('Moss Studio.app/Contents/Frameworks/QtCore.framework/QtCore')
                    link.create_system = 3
                    link.external_attr = (stat.S_IFLNK | 0o777) << 16
                    z.writestr(link, target)
                if allowed:
                    release.validate_archive(archive)
                else:
                    with self.assertRaises(ValueError):
                        release.validate_archive(archive)

    def test_zip_traversal_rejected_before_ditto(self):
        with tempfile.TemporaryDirectory() as tmp:
            archive = Path(tmp) / 'app.zip'
            with zipfile.ZipFile(archive, 'w') as z:
                z.writestr('Moss Studio.app/../../escaped', 'bad')
            with self.assertRaises(ValueError):
                release.validate_archive(archive)

    def test_upload_checksum_failure_does_not_promote_or_delete_old_package(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            asset = root / 'notarized.dmg'
            asset.write_bytes(b'notarized bytes')
            old = dict(name='macOS (未公证)', draft=False, assets=[dict(
                name='Moss-Studio-0.1.0-macOS-arm64-unsigned.dmg', size=1, digest='sha256:bad')])
            run = dict(head_sha='a' * 40, id=123, html_url='https://example.com/build', updated_at='2026-10-23')
            with patch.object(release, 'release_snapshot', return_value=old), patch.object(
                    release.preview, 'api', return_value={'status': 'identical'}), patch.object(release.preview, 'gh') as gh:
                with self.assertRaises(ValueError):
                    release.publish_signed(run, [asset], root)
                actions = [call.args[:2] for call in gh.call_args_list]
                self.assertIn(('release', 'upload'), actions)
                self.assertNotIn(('release', 'edit'), actions)
                self.assertNotIn(('release', 'delete-asset'), actions)

    def test_verified_upload_is_promoted_before_old_unsigned_asset_is_removed(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            asset = root / 'notarized.dmg'
            asset.write_bytes(b'notarized bytes')
            old_name = 'Moss-Studio-0.1.0-macOS-arm64-unsigned.dmg'
            state = dict(name='macOS (未公证)', draft=False, html_url='https://example.com/release',
                         assets=[dict(name=old_name, size=1, digest='sha256:bad')])
            calls = []
            def gh(*args):
                calls.append(args[:2])
                if args[:2] == ('release', 'upload'):
                    path = Path(args[3])
                    state['assets'].append(dict(name=path.name, size=path.stat().st_size,
                                               digest='sha256:' + release.distribution.sha256(path)))
                elif args[:2] == ('release', 'edit'):
                    state['name'] = args[args.index('--title') + 1]
                elif args[:2] == ('release', 'delete-asset'):
                    state['assets'] = [a for a in state['assets'] if a['name'] != args[3]]
            run = dict(head_sha='a' * 40, id=123, html_url='https://example.com/build', updated_at='2026-10-23')
            with patch.object(release, 'release_snapshot', side_effect=lambda tag: state), patch.object(
                    release.preview, 'api', return_value={'status': 'identical'}), patch.object(release.preview, 'gh', side_effect=gh):
                self.assertEqual(release.publish_signed(run, [asset], root), state['html_url'])
                self.assertLess(calls.index(('release', 'edit')), calls.index(('release', 'delete-asset')))
                calls.clear()
                release.publish_signed(run, [asset], root)
                self.assertFalse(calls, 'Repeated successful release must not upload or edit again')


if __name__ == '__main__':
    unittest.main()
