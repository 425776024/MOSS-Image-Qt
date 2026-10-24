"""Exercise Windows checkout and the always-on embedded-resource build gate."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ModelResourceIntegrityTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.resources = []
        for name in ('models.json', 'models-store.json'):
            catalog = ROOT / 'config' / name
            destination = self.root / 'config' / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(catalog, destination)
            for entry in json.loads(catalog.read_text(encoding='utf-8'))['files']:
                if 'resource' not in entry:
                    continue
                relative = entry['resource'].removeprefix(':/models/')
                path = self.root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT / relative, path)
                self.resources.append((relative, entry))
        self.driver = self.root / 'validate.cmake'
        self.driver.write_text(
            f'set(CMAKE_CURRENT_SOURCE_DIR "{self.root.as_posix()}")\n'
            'set(MOSS_APP_STORE ON)\n'
            f'include("{(ROOT / "config/validate-model-resources.cmake").as_posix()}")\n', encoding='utf-8')

    def validate(self):
        return subprocess.run(['cmake', '-P', str(self.driver)],
                              capture_output=True, text=True)

    def test_catalogued_resources_pass_build_gate(self):
        result = self.validate()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_crlf_embedded_config_stops_packaging(self):
        path = self.root / 'config/ditto-original/config.json'
        path.write_bytes(path.read_bytes().replace(b'\n', b'\r\n'))
        result = self.validate()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('config/ditto-original/config.json', result.stderr)
        self.assertIn('LF line endings', ' '.join(result.stderr.split()))

    def test_same_size_corruption_stops_packaging(self):
        path = self.root / 'config/ditto-store/manifest.json'
        body = path.read_bytes()
        path.write_bytes(b'!' + body[1:])
        result = self.validate()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('config/ditto-store/manifest.json', result.stderr)

    def test_windows_checkout_preserves_pinned_resource_identity(self):
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        shutil.copyfile(ROOT / '.gitattributes', self.root / '.gitattributes')
        subprocess.run(['git', '-C', str(self.root), '-c', 'core.autocrlf=false',
                        'add', '.gitattributes', 'config'], check=True)
        for relative, entry in self.resources:
            body = subprocess.check_output([
                'git', '-C', str(self.root), '-c', 'core.autocrlf=true',
                'cat-file', '--filters', ':' + relative])
            self.assertEqual(len(body), entry['bytes'], relative)
            self.assertEqual(hashlib.sha256(body).hexdigest(), entry['sha256'], relative)


if __name__ == '__main__':
    unittest.main()
