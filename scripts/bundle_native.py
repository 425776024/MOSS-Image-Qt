"""Build-time Mach-O dependency bundler. This script is not shipped in the app."""
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import sys

app = pathlib.Path(sys.argv[1]).resolve()
frameworks = app / "Contents/Frameworks"
frameworks.mkdir(exist_ok=True)

def run(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)

def dependencies(path):
    # Universal binaries print an architecture heading for each slice.
    return list(dict.fromkeys(line.strip().split(" (compatibility")[0]
                             for line in run("otool", "-L", str(path)).splitlines()
                             if " (compatibility version" in line))

def macho(path):
    if not path.is_file() or path.is_symlink():
        return False
    with path.open("rb") as stream:
        return stream.read(4) in {b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf',
                                  b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca'}

queue = [p for p in app.rglob("*") if macho(p)]
def rpaths(path):
    return re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset", run("otool", "-l", str(path)))

search_roots = set()
for binary in queue:
    search_roots.update(p for p in rpaths(binary) if p.startswith("/"))

def resolve(dep, binary):
    if dep.startswith("@rpath/"):
        suffix = dep[len("@rpath/"):]
        candidates = [frameworks / suffix]
        # Respect this binary's link-time search order before unrelated binaries.
        # Otherwise a Homebrew FFmpeg can shadow the isolated distribution build.
        for root in rpaths(binary) + sorted(search_roots):
            expanded = root.replace("@loader_path", str(binary.parent)).replace(
                "@executable_path", str(app / "Contents/MacOS"))
            candidates.append(pathlib.Path(expanded) / suffix)
    elif dep.startswith("@loader_path/"):
        candidates = [binary.parent / dep[len("@loader_path/"):]]
    elif dep.startswith("@executable_path/"):
        candidates = [app / "Contents/MacOS" / dep[len("@executable_path/"):]]
    else:
        candidates = [pathlib.Path(dep)]
    for path in candidates:
        if path.is_file():
            return path.resolve()
    raise RuntimeError(f"Unresolved native library: {binary}: {dep}")

seen = set()
receipt_path = app / "Contents/Resources/native-dependencies.json"
receipts = json.loads(receipt_path.read_text()) if receipt_path.exists() else []
while queue:
    binary = queue.pop(0)
    if binary in seen:
        continue
    seen.add(binary)
    for dep in dependencies(binary):
        if dep.startswith(("/System/", "/usr/lib/")):
            continue
        source = resolve(dep, binary)
        if source.is_relative_to(app):
            continue
        # Transitive @rpath dependencies (e.g. OpenBLAS -> gfortran -> libgcc)
        # are often beside their originating library.
        search_roots.add(str(source.parent))
        search_roots.update(p for p in rpaths(source) if p.startswith("/"))
        if ".framework/" in dep:
            raise RuntimeError(f"Undeployed framework: {binary}: {dep}")
        target = frameworks / pathlib.Path(dep).name
        if not target.exists():
            shutil.copy2(source.resolve(), target)
            target.chmod(target.stat().st_mode | 0o200)
            run("install_name_tool", "-id", "@rpath/" + target.name, str(target))
            receipts.append({"source": str(source.resolve()), "bundled": target.name,
                             "sourceSha256": hashlib.sha256(source.read_bytes()).hexdigest()})
            queue.append(target)
        run("install_name_tool", "-change", dep, "@rpath/" + target.name, str(binary))
    # Worker and library dependencies resolve against this bundle.
    rpath = "@executable_path/../Frameworks" if binary.parent.name in ("MacOS", "Helpers") else "@loader_path"
    load = run("otool", "-l", str(binary))
    if f"path {rpath} " not in load:
        run("install_name_tool", "-add_rpath", rpath, str(binary))
    for external in dict.fromkeys(rpaths(binary)):
        if external.startswith("/") and not external.startswith(("/System/", "/usr/lib/")):
            run("install_name_tool", "-delete_rpath", external, str(binary))

for binary in seen:
    for dep in dependencies(binary):
        if dep.startswith("/") and not dep.startswith(("/System/", "/usr/lib/")):
            raise RuntimeError(f"External dependency remains: {binary}: {dep}")
        if dep.startswith("@") and not resolve(dep, binary).is_relative_to(app):
            raise RuntimeError(f"Dependency resolves outside app: {binary}: {dep}")
    run("codesign", "--force", "--sign", "-", str(binary))
receipt_path.write_text(json.dumps(receipts, indent=2))
print(f"Bundled {len(receipts)} native dependencies; inspected {len(seen)} Mach-O files.")
