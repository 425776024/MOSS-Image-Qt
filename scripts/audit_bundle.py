"""Read-only audit of the final application's runtime library closure."""
import hashlib
import json
import pathlib
import re
import subprocess
import sys

app = pathlib.Path(sys.argv[1]).resolve()
output = pathlib.Path(sys.argv[2])
magic = {b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf', b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca'}
binaries = []
errors = []
python_files = []
for path in app.rglob('*'):
    if not path.is_file() or path.is_symlink():
        continue
    if path.suffix in ('.py', '.pyc') or path.name.lower().startswith('libpython') or path.name.lower() in ('python', 'python3'):
        python_files.append(str(path.relative_to(app)))
    with path.open('rb') as f:
        if f.read(4) not in magic:
            continue
    deps_text = subprocess.check_output(['otool', '-L', str(path)], text=True)
    deps = list(dict.fromkeys(line.strip().split(' (compatibility')[0] for line in deps_text.splitlines() if ' (compatibility version' in line))
    load = subprocess.check_output(['otool', '-l', str(path)], text=True)
    rpaths = list(dict.fromkeys(re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset', load)))
    for dep in deps:
        if dep.startswith(('/System/', '/usr/lib/')):
            continue
        if dep.startswith('/'):
            errors.append(f'{path.name}: external dependency {dep}')
        elif dep.startswith('@rpath/'):
            candidates = [app / 'Contents/Frameworks' / dep[7:]]
            for root in rpaths:
                root = root.replace('@loader_path', str(path.parent)).replace('@executable_path', str(app / 'Contents/MacOS'))
                candidates.append(pathlib.Path(root) / dep[7:])
            if not any(p.is_file() and p.resolve().is_relative_to(app) for p in candidates):
                errors.append(f'{path.name}: unresolved {dep}')
        else:
            expanded = dep.replace('@loader_path', str(path.parent)).replace('@executable_path', str(app / 'Contents/MacOS'))
            if not pathlib.Path(expanded).is_file() or not pathlib.Path(expanded).resolve().is_relative_to(app):
                errors.append(f'{path.name}: unresolved {dep}')
    for root in rpaths:
        if root.startswith('/') and not root.startswith(('/System/', '/usr/lib/')):
            errors.append(f'{path.name}: external rpath {root}')
    binaries.append({'path': str(path.relative_to(app)), 'sha256': hashlib.file_digest(path.open('rb'), 'sha256').hexdigest() if hasattr(hashlib, 'file_digest') else hashlib.sha256(path.read_bytes()).hexdigest(), 'dependencies': deps, 'rpaths': rpaths})
result = {'app': str(app), 'machoCount': len(binaries), 'pythonFiles': python_files, 'errors': errors, 'binaries': binaries}
output.write_text(json.dumps(result, indent=2))
print(json.dumps({k:v for k,v in result.items() if k != 'binaries'}, indent=2))
sys.exit(1 if errors or python_files else 0)
