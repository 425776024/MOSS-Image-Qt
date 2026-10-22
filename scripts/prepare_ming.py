#!/usr/bin/env python3
"""Build the pinned, Ming-only native C ABI for both macOS editions (no weights)."""
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def prepare(base=ROOT / '.local', destination=ROOT / '.local/audio-deps/ming'):
    lock = json.loads((ROOT / 'config/macos-dependencies.json').read_text())['ming-audio-cpp']
    source = base / 'sources/ming-audio-cpp'
    build = base / 'build/ming-audio-cpp'
    source.parent.mkdir(parents=True, exist_ok=True)
    build.mkdir(parents=True, exist_ok=True)
    def run(*args):
        subprocess.run([str(a) for a in args], check=True)
    if not source.exists():
        run('git', 'clone', '--no-checkout', '--filter=blob:none', lock['repository'], source)
        run('git', '-C', source, 'checkout', '--detach', lock['revision'])
    head = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    dirty = subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain'], text=True).strip()
    if head != lock['revision'] or dirty:
        raise ValueError('Preserve the existing audio.cpp checkout; expected clean pinned revision')
    run('git', '-C', source, 'submodule', 'update', '--init', '--recursive')
    # Replace ggml's debugger-spawning backtrace with native stack symbols.
    # Keep upstream source clean and carry this deployment change as an overlay.
    ggml = (source / 'external/ggml/src/ggml.c').read_text()
    start = ggml.index('void ggml_print_backtrace(void) {')
    end = ggml.index('\n#else\nvoid ggml_print_backtrace(void)', start)
    patched = build / 'ggml-native-backtrace.c'
    patched.write_text(ggml[:start] + '''void ggml_print_backtrace(void) {
    if (!getenv("GGML_NO_BACKTRACE")) ggml_print_backtrace_symbols();
}''' + ggml[end:])
    overlay = build / 'native-backtrace.cmake'
    overlay.write_text(f'''if(PROJECT_NAME STREQUAL "AudioCpp")
function(moss_ming_backtrace)
  get_target_property(sources ggml-base SOURCES)
  list(FILTER sources EXCLUDE REGEX "(^|/)ggml[.]c$")
  set_property(TARGET ggml-base PROPERTY SOURCES "${{sources}}")
  target_sources(ggml-base PRIVATE "{patched.resolve()}")
  target_include_directories(ggml-base PRIVATE "{source.resolve() / 'external/ggml/src'}")
endfunction()
cmake_language(DEFER CALL moss_ming_backtrace)
endif()
''')
    run('cmake', '-S', source, '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
        '-DCMAKE_OSX_ARCHITECTURES=arm64', '-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0',
        '-DAUDIOCPP_MODEL_SET=custom', '-DAUDIOCPP_MODELS=ming3_tts', '-DAUDIOCPP_BUILD_C_API=ON',
        '-DAUDIOCPP_DEPLOYMENT_BUILD=ON', '-DENGINE_ENABLE_OPENMP=OFF', '-DGGML_OPENMP=OFF',
        '-DENGINE_ENABLE_NATIVE_CPU=OFF', '-DENGINE_ENABLE_METAL=OFF', '-DENGINE_BUILD_TESTS=OFF',
        '-DAUDIOCPP_STATIC_ESPEAK=OFF', '-DAUDIOCPP_BUILD_NATIVE_MODEL_MANAGER=OFF',
        '-DCMAKE_CXX_VISIBILITY_PRESET=hidden', '-DCMAKE_C_VISIBILITY_PRESET=hidden',
        '-DCMAKE_VISIBILITY_INLINES_HIDDEN=ON', f'-DCMAKE_PROJECT_INCLUDE={overlay.resolve()}')
    run('cmake', '--build', build, '--target', 'audiocpp', '--parallel', os.environ.get('BUILD_JOBS', '3'))
    (destination / 'lib').mkdir(parents=True, exist_ok=True)
    (destination / 'include').mkdir(exist_ok=True)
    library = destination / 'lib/libaudiocpp.dylib'
    shutil.copy2(build / 'bin/libaudiocpp.0.1.0.dylib', library)
    run('install_name_tool', '-id', '@rpath/libaudiocpp.dylib', library)
    symbols = subprocess.check_output(['nm', '-u', str(library)], text=True)
    prohibited = {'_fork', '_vfork', '_execl', '_execlp', '_execv', '_execve', '_execvp',
                  '_posix_spawn', '_posix_spawnp', '_popen', '_pclose', '_system'}
    if any(line.split()[-1] in prohibited for line in symbols.splitlines() if line.split()):
        raise ValueError('Ming runtime imports a process-launch API')
    run('codesign', '--force', '--sign', '-', library)
    shutil.copy2(source / 'include/audiocpp.h', destination / 'include')
    notices = destination / 'licenses'
    notices.mkdir(exist_ok=True)
    shutil.copy2(source / 'LICENSE', notices / 'audio-cpp-Apache-2.0.txt')
    for package in ('ggml', 'sentencepiece', 'utf8proc', 'cJSON', 'libyaml', 'llama_tokenizer'):
        for file in (source / 'external' / package).rglob('*'):
            if file.is_file() and file.name.startswith(('LICENSE', 'COPYING', 'NOTICE')):
                shutil.copy2(file, notices / '-'.join(file.relative_to(source / 'external').parts))
    (notices / 'sources.json').write_text(json.dumps(lock, indent=2) + '\n')
    print('Ming native runtime ready:', destination)


if __name__ == '__main__':
    prepare()
