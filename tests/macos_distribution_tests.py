"""Security boundaries for the credential-bearing distribution step."""
import base64
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('distribution', Path(__file__).resolve().parents[1] / 'scripts/macos_distribution.py')
distribution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(distribution)


class DistributionSecurityTest(unittest.TestCase):
    def test_missing_credentials_fail_before_any_command(self):
        with patch.dict(os.environ, {}, clear=True), patch.object(distribution, 'run') as run:
            with self.assertRaisesRegex(ValueError, 'Missing'):
                distribution.sign_notarize(Path('/app'), Path('/out'))
            run.assert_not_called()

    def test_child_tools_do_not_inherit_secrets(self):
        with patch.dict(os.environ, {name: 'secret' for name in distribution.SECRET_NAMES}), patch.object(distribution.subprocess, 'run') as run:
            distribution.run('/usr/bin/true')
            self.assertTrue(set(distribution.SECRET_NAMES).isdisjoint(run.call_args.kwargs['env']))

    def test_failed_import_deletes_keychain_and_private_files(self):
        env = {name: 'test-only' for name in distribution.SECRET_NAMES}
        env['APPLE_CERTIFICATE_P12_BASE64'] = base64.b64encode(b'fake-p12').decode()
        calls = []

        def command(*args, **kwargs):
            calls.append(args)
            if args[1] == 'create-keychain':
                Path(args[-1]).touch()
            elif args[0] == '/usr/bin/curl':
                Path(args[-1]).write_bytes(b'test-certificate')
            elif args[1] == 'import' and str(args[2]).endswith('.p12'):
                raise subprocess.CalledProcessError(1, args)
            return subprocess.CompletedProcess(args, 0)

        old_umask = os.umask(0o022)
        try:
            with patch.dict(os.environ, env), patch.object(distribution, 'run', side_effect=command), patch.object(distribution, 'sha256', side_effect=lambda p: __import__('json').loads((distribution.ROOT / 'config/macos-dependencies.json').read_text())[p.stem]['sha256']):
                with self.assertRaises(subprocess.CalledProcessError):
                    distribution.sign_notarize(Path('/app'), Path('/out'))
            self.assertEqual(calls[-1][1], 'delete-keychain')
            self.assertFalse(Path(calls[-1][-1]).parent.exists())
        finally:
            os.umask(old_umask)

    def test_signing_order_handles_nested_bundles_and_symlinks(self):
        with tempfile.TemporaryDirectory() as tmp:
            app = Path(tmp) / 'Moss Studio.app'
            framework = app / 'Contents/Frameworks/Example.framework'
            binary = framework / 'Versions/A/Example'
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b'\xcf\xfa\xed\xfe' + b'test')
            (framework / 'Example').symlink_to('Versions/A/Example')
            items = distribution.code_items(app)
            self.assertLess(items.index(binary), items.index(framework))
            self.assertEqual(items[-1], app)
            self.assertNotIn(framework / 'Example', items)


if __name__ == '__main__':
    unittest.main()
