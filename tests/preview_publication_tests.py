"""Release provenance and payload checks do not execute downloaded binaries."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('preview', Path(__file__).resolve().parents[1] / 'scripts/publish_preview.py')
preview = importlib.util.module_from_spec(spec)
spec.loader.exec_module(preview)
SHA = 'a' * 40


class PreviewPublicationTests(unittest.TestCase):
    def run_info(self):
        return dict(event='push', head_branch='master', status='completed',
                    conclusion='success', head_repository={'full_name': preview.REPO},
                    path='.github/workflows/macos.yml', head_sha=SHA)

    def mac_artifact(self, root, notarized=False):
        entries = {}
        for suffix in ('app.zip', 'dmg'):
            kind = 'notarized' if notarized else 'unsigned'
            name = f'Moss-Studio-0.1.0-macOS-arm64-{kind}.{suffix}'
            (root / name).write_bytes(b'test artifact')
            entries[name] = hashlib.sha256(b'test artifact').hexdigest()
        (root / 'SHA256SUMS.txt').write_text(''.join(f'{digest}  {name}\n' for name, digest in entries.items()))
        (root / 'release-manifest.json').write_text(json.dumps(dict(
            commit=SHA, notarized=notarized, modelWeightsIncluded=False, sha256=entries,
            notarySubmissionIds=['app-receipt', 'dmg-receipt'] if notarized else [])))
        return entries

    def test_mac_ci_publication_stops_until_local_notarization(self):
        with patch.object(preview, 'api', return_value=self.run_info()), patch.object(preview, 'gh') as gh:
            preview.publish(123)
            gh.assert_not_called()

    def test_signed_assets_require_receipts_for_both_app_and_dmg(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.mac_artifact(root, notarized=True)
            self.assertEqual(len(preview.verified_assets(root, 'macos', 'SHA256SUMS.txt', SHA, notarized=True)), 4)
            path = root / 'release-manifest.json'
            manifest = json.loads(path.read_text())
            manifest['notarySubmissionIds'] = ['app-only']
            path.write_text(json.dumps(manifest))
            with self.assertRaises(ValueError):
                preview.verified_assets(root, 'macos', 'SHA256SUMS.txt', SHA, notarized=True)

    def test_only_trusted_successful_push_builds_are_publishable(self):
        self.assertEqual(preview.validate_run(self.run_info())[0], 'macos')
        for key, value in (('event', 'pull_request'), ('head_branch', 'windows-x64'),
                           ('conclusion', 'failure'), ('status', 'in_progress'),
                           ('head_repository', {'full_name': 'someone/fork'}),
                           ('path', '.github/workflows/untrusted.yml'), ('head_sha', 'invalid')):
            with self.subTest(key=key):
                run = self.run_info()
                run[key] = value
                with self.assertRaises(ValueError):
                    preview.validate_run(run)

    def test_mac_packages_and_manifest_match_exact_build(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.mac_artifact(root)
            self.assertEqual(len(preview.verified_assets(root, 'macos', 'SHA256SUMS.txt', SHA)), 4)
            with self.assertRaises(ValueError):
                preview.verified_assets(root, 'macos', 'SHA256SUMS.txt', 'b' * 40)

    def test_corrupt_package_or_extra_file_prevents_publication(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            entries = self.mac_artifact(root)
            (root / 'extra.txt').write_text('unexpected')
            with self.assertRaises(ValueError):
                preview.verified_assets(root, 'macos', 'SHA256SUMS.txt', SHA)
            (root / 'extra.txt').unlink()
            (root / next(iter(entries))).write_bytes(b'corrupt')
            with self.assertRaises(ValueError):
                preview.verified_assets(root, 'macos', 'SHA256SUMS.txt', SHA)

    def test_unsafe_checksum_paths_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.mac_artifact(root)
            for name in ('../escaped.zip', '/absolute.zip', 'nested/file.zip'):
                (root / 'SHA256SUMS.txt').write_text('0' * 64 + '  ' + name + '\n')
                with self.assertRaises(ValueError):
                    preview.verified_assets(root, 'macos', 'SHA256SUMS.txt', SHA)

    def test_mac_artifact_cannot_claim_notarization(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.mac_artifact(root)
            path = root / 'release-manifest.json'
            manifest = json.loads(path.read_text())
            manifest['notarized'] = True
            path.write_text(json.dumps(manifest))
            with self.assertRaises(ValueError):
                preview.verified_assets(root, 'macos', 'SHA256SUMS.txt', SHA)

    def test_windows_installer_is_verified(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            name = 'XAvatar-0.1.0-Windows-x64-Setup.exe'
            (root / name).write_bytes(b'windows archive')
            (root / 'SHA256SUMS-windows-x64.txt').write_text(
                hashlib.sha256(b'windows archive').hexdigest() + '  ' + name + '\n')
            self.assertEqual(len(preview.verified_assets(root, 'windows', 'SHA256SUMS-windows-x64.txt', SHA)), 2)


if __name__ == '__main__':
    unittest.main()
