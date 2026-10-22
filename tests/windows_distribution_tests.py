"""Exercise release boundaries without requiring a Windows build host."""
import importlib.util
import os
from pathlib import Path
import subprocess
import shutil
import tempfile
import unittest
from unittest.mock import patch
import zipfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


build = module('windows_build', 'scripts/build_ci_windows.py')
release = module('windows_release', 'scripts/public_release.py')
package = module('windows_package', 'scripts/package_windows.py')


class WindowsDistributionTest(unittest.TestCase):
    def test_worker_usage_exit_is_accepted_but_loader_failures_are_not(self):
        for code, output, valid in ((1, 'usage: sa3-generate --model ...', True),
                                     (1, 'missing runtime DLL', False),
                                     (3221225781, '', False)):
            with self.subTest(code=code, output=output):
                result = subprocess.CompletedProcess([], code, stdout='', stderr=output)
                with patch.object(package.subprocess, 'run', return_value=result):
                    if valid:
                        package.check_worker_startup(Path('sa3-generate.exe'), {})
                    else:
                        with self.assertRaises(ValueError):
                            package.check_worker_startup(Path('sa3-generate.exe'), {})

    def test_portable_runtime_cannot_be_satisfied_by_ci_system_dlls(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            destination, system = root / 'app', root / 'system'
            destination.mkdir()
            system.mkdir()
            for name in ('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'vcomp140.dll', 'kernel32.dll'):
                (system / name).write_bytes(b'installed on CI only')
            self.assertTrue(package.dependency_available('kernel32.dll', destination, system))
            self.assertFalse(package.dependency_available('msvcp140.dll', destination, system))
            self.assertFalse(package.dependency_available('vcomp140.dll', destination, system))
            redist = root / 'redist'
            crt = redist / 'x64/Microsoft.VC143.CRT'
            crt.mkdir(parents=True)
            for name in ('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll'):
                (crt / name).write_bytes(b'redistributable')
            openmp = redist / 'x64/Microsoft.VC143.OpenMP'
            openmp.mkdir()
            (openmp / 'vcomp140.dll').write_bytes(b'redistributable')
            package.deploy_msvc_runtime(destination, redist)
            for name in ('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll', 'vcomp140.dll'):
                self.assertEqual((destination / name).read_bytes(), b'redistributable')
                self.assertTrue(package.dependency_available(name, destination, system))

    @unittest.skipUnless(shutil.which('cmake') and shutil.which('ninja'), 'CMake and Ninja are required')
    def test_compiler_probe_and_nested_executable_manifests(self):
        # On Windows this uses the exact production configure helper, so a
        # global linker override that breaks try_compile fails before the
        # expensive dependency build. Other hosts exercise target discovery.
        with tempfile.TemporaryDirectory(prefix='moss manifest ') as tmp:
            root = Path(tmp)
            source, output = root / 'source', root / 'build'
            (source / 'nested').mkdir(parents=True)
            overlay = (ROOT / 'config/windows-manifest.cmake').as_posix()
            (source / 'main.cpp').write_text('''#ifdef _WIN32
#ifdef WIN32_LEAN_AND_MEAN
#error The pinned image CLI requires full Windows Shell API declarations
#endif
#ifndef _CPPUNWIND
#error CMake MSVC exception handling defaults must be preserved
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
struct UnwindProbe {
    bool &destroyed;
    ~UnwindProbe() { destroyed = true; }
};
int main() {
    bool destroyed = false;
    try {
        UnwindProbe probe{destroyed};
        throw 42;
    } catch (int value) {
        if (value != 42 || !destroyed) return 2;
    }
#ifdef _WIN32
    int count = 0;
    auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return 3;
    LocalFree(arguments);
    if (count < 1) return 4;
#endif
    return 0;
}
''')
            (source / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.24)
project(ManifestProbe LANGUAGES C CXX)
if(NOT MSVC)
    set(MSVC TRUE)
    include("''' + overlay + '''")
endif()
add_executable(manifest_probe main.cpp)
add_library(probe_library STATIC main.cpp)
add_subdirectory(nested)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/main-sources.txt" CONTENT "$<TARGET_PROPERTY:manifest_probe,SOURCES>")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/nested-sources.txt" CONTENT "$<TARGET_PROPERTY:nested_probe,SOURCES>")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/library-sources.txt" CONTENT "$<TARGET_PROPERTY:probe_library,SOURCES>")
''')
            (source / 'nested/CMakeLists.txt').write_text('''project(NestedProbe LANGUAGES CXX)
add_executable(nested_probe ../main.cpp)
''')
            if os.name == 'nt':
                build.cmake(source, output)
            else:
                subprocess.run(['cmake', '-S', str(source), '-B', str(output), '-G', 'Ninja',
                    '-DCMAKE_PROJECT_INCLUDE=' + overlay], check=True, capture_output=True, text=True)
            for name in ('main', 'nested'):
                files = (output / (name + '-sources.txt')).read_text().split(';')
                self.assertEqual(sum(Path(p).name == 'windows.manifest' for p in files), 1)
            self.assertNotIn('windows.manifest', (output / 'library-sources.txt').read_text())
            subprocess.run(['cmake', '--build', str(output)], check=True, capture_output=True, text=True)
            executable = output / ('manifest_probe.exe' if os.name == 'nt' else 'manifest_probe')
            subprocess.run([str(executable)], check=True, capture_output=True, text=True)
            if os.name == 'nt':
                manifest = root / 'embedded.manifest'
                subprocess.run(['mt.exe', '-nologo',
                    f'-inputresource:{output / "manifest_probe.exe"};#1', f'-out:{manifest}'],
                    check=True, capture_output=True, text=True)
                xml = ET.parse(manifest)
                self.assertEqual(xml.findtext('.//{http://schemas.microsoft.com/SMI/2019/WindowsSettings}activeCodePage'), 'UTF-8')
                self.assertEqual(xml.findtext('.//{http://schemas.microsoft.com/SMI/2016/WindowsSettings}longPathAware'), 'true')

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
