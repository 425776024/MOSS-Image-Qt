#!/usr/bin/env python3
"""Apply the small FlowEdit adapter to the pinned native engine, idempotently."""
from pathlib import Path
import argparse
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def prepare(engine):
    adapter = ROOT / 'vendor/moss-flowedit'
    patch = adapter / 'engine.patch'
    check = lambda reverse=False: subprocess.run(
        ['git', 'apply', '--check', *(['--reverse'] if reverse else []), str(patch)],
        cwd=engine, capture_output=True, text=True)
    if check().returncode == 0:
        subprocess.run(['git', 'apply', str(patch)], cwd=engine, check=True)
    elif check(True).returncode != 0:
        raise SystemExit('FlowEdit patch does not match this engine. Keep local changes and use the pinned revision.')
    shutil.copyfile(adapter / 'moss_flowedit.hpp', engine / 'src/pipeline/moss_flowedit.hpp')
    print('FlowEdit native adapter ready')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine', type=Path)
    prepare(parser.parse_args().engine.resolve())
