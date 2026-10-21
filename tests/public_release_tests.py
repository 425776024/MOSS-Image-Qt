"""Release boundary tests: a private checkpoint must never enter an offline pack."""
import importlib.util
import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('release', Path(__file__).resolve().parents[1] / 'scripts/public_release.py')
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)

class ReleaseTest(unittest.TestCase):
    def test_signing_secrets_cannot_enter_source_export(self):
        for suffix in ('.p12', '.p8', '.key', '.pem', '.keychain-db'):
            with self.subTest(suffix=suffix), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp); (root / 'config').mkdir()
                (root / 'config' / ('signing' + suffix)).write_bytes(b'private-key')
                with patch.object(release, 'ROOT', root), patch.object(release, 'SOURCE_DOCS', ()), patch.object(release, 'SOURCE_DIRS', ('config',)):
                    with self.assertRaises(ValueError): release.source_files()

    def test_server_source_is_rejected_even_when_added_back(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'scripts').mkdir()
            (root/'scripts/remote_image_worker.py').write_text('# private worker')
            with patch.object(release,'ROOT',root), patch.object(release,'SOURCE_DOCS',()), patch.object(release,'SOURCE_DIRS',('scripts',)):
                with self.assertRaises(ValueError):release.source_files()

    def test_app_with_server_payload_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);(root/'remote_image_worker.py').write_text('# private worker')
            with self.assertRaises(ValueError):release.audit_app(root)

    def test_old_or_private_binary_is_rejected(self):
        from subprocess import CompletedProcess
        with patch.object(release.subprocess,'run',return_value=CompletedProcess([],0,stdout='',stderr='')):
            with self.assertRaises(ValueError):release.audit_app(Path('/nonexistent/app'))

    def test_private_weight_with_public_filename_is_rejected_before_copy(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp);src = root / 'source';(src / 'ditto-original').mkdir(parents=True)
            (src / 'ditto-original/denoise.onnx').write_bytes(b'private')
            entry = dict(group='ditto',path='ditto-original/denoise.onnx',bytes=7,sha256=hashlib.sha256(b'public!').hexdigest())
            with patch.object(release,'catalog',return_value=[entry]):
                with self.assertRaises(ValueError):release.copy_models(src,root/'package','Q4_K')
            self.assertFalse((root/'package').exists())

    def test_only_allowlisted_files_are_copied(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp);src=root/'source';src.mkdir();(src/'public.bin').write_bytes(b'public')
            (src/'step-05000.pth').write_bytes(b'private');(src/'result.mp4').write_bytes(b'local data')
            entry=dict(group='ditto',path='public.bin',bytes=6,sha256=hashlib.sha256(b'public').hexdigest())
            with patch.object(release,'catalog',return_value=[entry]):release.copy_models(src,root/'package','Q4_K')
            self.assertEqual([p.name for p in (root/'package').iterdir()],['public.bin'])
            self.assertTrue((src/'step-05000.pth').exists())

    def test_symlink_in_catalog_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);src=root/'source';src.mkdir();(root/'private').write_bytes(b'secret');(src/'public.bin').symlink_to(root/'private')
            entry=dict(group='ditto',path='public.bin',bytes=6,sha256=hashlib.sha256(b'secret').hexdigest())
            with patch.object(release,'catalog',return_value=[entry]):
                with self.assertRaises(ValueError):release.copy_models(src,root/'package','Q4_K')

if __name__ == '__main__':unittest.main()
