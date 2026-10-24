"""Synthetic DLL archives for download/cache tests; never used for inference."""
import hashlib
import json
from pathlib import Path
import random
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from windows_runtime import build_components

root = Path(sys.argv[1])
native = root / 'native'
(native / 'cuda').mkdir(parents=True)
groups = {
    'onnxruntime-cuda': ['onnxruntime.dll', 'onnxruntime_providers_shared.dll', 'onnxruntime_providers_cuda.dll'],
    'cuda-libraries': ['cudart64_12.dll', 'cublas64_12.dll', 'cublasLt64_12.dll', 'cudnn64_9.dll', 'cufft64_11.dll', 'curand64_10.dll'],
}
files = {}
rng = random.Random(1024)
for group, names in groups.items():
    notice = native / 'licenses/cuda' / group
    notice.mkdir(parents=True)
    (notice / 'LICENSE').write_text('Synthetic fixture. Not a runtime.')
    for name in names:
        content = rng.randbytes(96 * 1024)
        (native / 'cuda' / name).write_bytes(content)
        files[name] = hashlib.sha256(content).hexdigest()
receipt = native / 'cuda/runtime-files.json'
receipt.write_text(json.dumps(dict(files=files, components=groups)))
build_components(native, root / 'v1', files)
content = b'new engine' + rng.randbytes(96 * 1024)
(native / 'cuda/onnxruntime.dll').write_bytes(content)
files['onnxruntime.dll'] = hashlib.sha256(content).hexdigest()
receipt.write_text(json.dumps(dict(files=files, components=groups)))
build_components(native, root / 'v2', files)
