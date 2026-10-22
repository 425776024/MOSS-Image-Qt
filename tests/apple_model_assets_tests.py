import importlib.util
from pathlib import Path
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('apple_assets',Path(__file__).resolve().parents[1]/'scripts/apple_model_assets.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class AssetsTests(unittest.TestCase):
    def test_same_size_corruption_is_not_packaged(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); f=root/'model.bin';f.write_bytes(b'good');sha=m.digest(f)
            catalog={'files':[{'path':'model.bin','sha256':sha,'bytes':4}]}
            row=m.inventory(catalog,[root])[0];self.assertEqual(row['status'],'verified')
            f.write_bytes(b'evil')
            self.assertEqual(m.inventory(catalog,[root])[0]['status'],'missing')
            with self.assertRaises(ValueError):m.build(row,root,[f])
    def test_traversal_rejected_and_embedded_files_excluded(self):
        with self.assertRaises(ValueError):m.inventory({'files':[{'path':'../private','bytes':1}]},[])
        self.assertEqual(m.inventory({'files':[{'resource':':/embedded'}]},[]),[])
    def test_id_is_content_pinned(self):
        self.assertEqual(m.pack_id({'sha256':'a'*64}),'model-'+'a'*64)
        self.assertNotEqual(m.pack_id({'sha256':'a'*64}),m.pack_id({'sha256':'b'*64}))

if __name__=='__main__':unittest.main()
