"""Runtime packages remain independent of app releases and reject tampering."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import windows_runtime as runtime
import publish_preview as publisher


def fixture(root):
    native = root / 'native'
    (native / 'cuda').mkdir(parents=True)
    files = {'onnxruntime.dll': b'ort-v1', 'cudnn64_9.dll': b'cudnn-v1'}
    for name, content in files.items():
        (native / 'cuda' / name).write_bytes(content)
    for name in ('onnxruntime-cuda', 'cudnn'):
        path = native / 'licenses/cuda' / name
        path.mkdir(parents=True)
        (path / 'LICENSE').write_text('runtime redistribution notice')
    receipt = dict(files={name: hashlib.sha256(data).hexdigest() for name, data in files.items()},
                   components={'onnxruntime-cuda': ['onnxruntime.dll'], 'cudnn': ['cudnn64_9.dll']})
    (native / 'cuda/runtime-files.json').write_text(json.dumps(receipt))
    return native, receipt


class RuntimePackageTests(unittest.TestCase):
    def test_rebuild_reuses_every_component_and_engine_change_preserves_cudnn(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); native, receipt = fixture(root)
            first = runtime.build_components(native, root / 'one', receipt['files'])
            second = runtime.build_components(native, root / 'two', receipt['files'])
            self.assertEqual(first, second)
            self.assertEqual(runtime.verify_components(root / 'one'), first)
            content = b'ort-v2'
            (native / 'cuda/onnxruntime.dll').write_bytes(content)
            receipt['files']['onnxruntime.dll'] = hashlib.sha256(content).hexdigest()
            (native / 'cuda/runtime-files.json').write_text(json.dumps(receipt))
            third = runtime.build_components(native, root / 'three', receipt['files'])
            old = {c['name']: c for c in first['components']}
            new = {c['name']: c for c in third['components']}
            self.assertEqual(old['cudnn'], new['cudnn'])
            self.assertNotEqual(old['onnxruntime-cuda']['id'], new['onnxruntime-cuda']['id'])
            self.assertNotEqual(first['id'], third['id'])

    def test_tampering_and_unlisted_assets_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); native, receipt = fixture(root)
            folder = root / 'artifacts'
            manifest = runtime.build_components(native, folder, receipt['files'])
            extra = folder / 'unexpected.dll'; extra.write_bytes(b'unknown')
            with self.assertRaises(ValueError): runtime.verify_components(folder)
            extra.unlink()
            (folder / manifest['components'][0]['archive']).write_bytes(b'damaged')
            with self.assertRaises(ValueError): runtime.verify_components(folder)

    def test_publisher_reuses_existing_assets_and_never_overwrites(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); native, receipt = fixture(root)
            folder = root / 'artifacts'; manifest = runtime.build_components(native, folder, receipt['files'])
            assets = [dict(name=c['archive'], size=c['bytes'], digest='sha256:' + c['sha256']) for c in manifest['components']]
            with patch.object(publisher.subprocess, 'run') as run, patch.object(publisher, 'runtime_release_snapshot', return_value=dict(assets=assets, draft=False)), patch.object(publisher, 'gh') as gh:
                run.return_value.returncode = 0
                self.assertEqual(publisher.publish_runtime_components(folder, 'a' * 40), manifest)
                gh.assert_not_called()
                assets[0]['digest'] = 'sha256:' + '0' * 64
                with self.assertRaises(ValueError): publisher.publish_runtime_components(folder, 'a' * 40)
                gh.assert_not_called()

    def test_new_draft_is_resolved_by_numeric_id_before_its_tag_exists(self):
        location = f'https://api.github.com/repos/{publisher.REPO}/releases/12345'
        with patch.object(publisher, 'gh', return_value=json.dumps(dict(apiUrl=location))), patch.object(publisher, 'api', return_value=dict(draft=True)) as api:
            self.assertEqual(publisher.runtime_release_snapshot(), dict(draft=True))
            api.assert_called_once_with('releases/12345')
        with patch.object(publisher, 'gh', return_value=json.dumps(dict(apiUrl='https://example.com/releases/12345'))), patch.object(publisher, 'api') as api:
            with self.assertRaises(ValueError): publisher.runtime_release_snapshot()
            api.assert_not_called()

    def test_runtime_tag_does_not_trigger_windows_build_tags(self):
        self.assertFalse(runtime.RUNTIME_TAG.startswith(('v', 'windows-')))


if __name__ == '__main__':
    unittest.main()
