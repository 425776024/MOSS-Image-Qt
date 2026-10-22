"""Exercise release boundaries without requiring a Windows build host."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


build = module('windows_build', 'scripts/build_ci_windows.py')
release = module('windows_release', 'scripts/public_release.py')


class WindowsDistributionTest(unittest.TestCase):
    def test_selected_archive_subtree_cannot_escape_destination(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            archive = root / 'dependency.zip'
            with zipfile.ZipFile(archive, 'w') as z:
                z.writestr('bin/../../escaped.dll', b'invalid')
            with self.assertRaises(ValueError):
                build.unzip(archive, root / 'native', 'bin/')
            self.assertFalse((root / 'escaped.dll').exists())

    def test_only_requested_native_subtree_is_extracted(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            archive = root / 'wheel.zip'
            with zipfile.ZipFile(archive, 'w') as z:
                z.writestr('native/runtime.dll', b'native')
                z.writestr('python/runtime.py', b'not for distribution')
            build.unzip(archive, root / 'native', 'native/')
            self.assertEqual([p.name for p in (root / 'native').iterdir()], ['runtime.dll'])

    def test_portable_executable_gets_same_public_binary_check(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'MossStudio.exe').touch()
            completed = subprocess.CompletedProcess([], 0, stdout='MOSS_PUBLIC_LOCAL_ONLY_V1\n', stderr='')
            with patch.object(release.subprocess, 'run', return_value=completed) as run:
                release.audit_app(root)
                self.assertEqual(Path(run.call_args.args[0][0]), root / 'MossStudio.exe')
                self.assertIn('--public-release-check', run.call_args.args[0])

    def test_windows_binaries_cannot_enter_public_source(self):
        for suffix in ('.dll', '.exe', '.lib', '.pyd', '.whl', '.nupkg'):
            with self.subTest(suffix=suffix), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                (root / 'vendor').mkdir()
                (root / 'vendor' / ('payload' + suffix)).write_bytes(b'binary')
                with patch.object(release, 'ROOT', root), patch.object(release, 'SOURCE_DOCS', ()), patch.object(release, 'SOURCE_DIRS', ('vendor',)):
                    with self.assertRaises(ValueError):
                        release.source_files()


if __name__ == '__main__':
    unittest.main()
