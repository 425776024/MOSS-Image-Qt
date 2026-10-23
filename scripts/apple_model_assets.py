#!/usr/bin/env python3
"""Inventory pinned local models and create on-demand Apple asset packs (no uploads)."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(8 * 1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def pack_id(asset):
    return 'model-' + asset['sha256']


def inventory(catalog, roots):
    rows = []
    for asset in catalog['files']:
        if asset.get('resource'):
            continue
        rel = Path(asset['path'])
        if rel.is_absolute() or '..' in rel.parts:
            raise ValueError('Unsafe catalog path')
        row = dict(asset, assetPackID=pack_id(asset), status='missing')
        for root in roots:
            # Exact catalog relative path first; known flat model directories also work.
            for source in (root / rel, root / rel.name):
                if not source.is_file() or source.stat().st_size != asset['bytes']:
                    continue
                if digest(source) == asset['sha256']:
                    row.update(status='verified', source=str(source.resolve()))
                    break
            if row['status'] == 'verified':
                break
        rows.append(row)
    return rows


def build(row, output, notices):
    source = Path(row['source'])
    if row['status'] != 'verified' or digest(source) != row['sha256']:
        raise ValueError('Model changed since inventory: ' + row['path'])
    if not notices or any(not p.is_file() for p in notices):
        raise ValueError('Provide the model license and attribution files before packaging')
    pack = row['assetPackID']
    archive = output / (pack + '.aar')
    if archive.exists():
        raise ValueError('Refusing to overwrite ' + str(archive))
    # Same-volume hard links avoid duplicating tens of GB during packaging.
    with tempfile.TemporaryDirectory(prefix='stage-', dir=output) as work:
        stage = Path(work)
        dest = stage / row['path']
        dest.parent.mkdir(parents=True, exist_ok=True)
        os.link(source, dest)
        notice_dir = stage / 'licenses' / pack
        notice_dir.mkdir(parents=True)
        (notice_dir / 'NOTICE.txt').write_text('\n\n'.join(
            '=== ' + notice.name + ' ===\n' + notice.read_text() for notice in notices))
        (notice_dir / 'source.json').write_text(json.dumps({k: row[k] for k in
            ('repo', 'revision', 'file', 'sha256', 'bytes')}, indent=2) + '\n')
        manifest = {'assetPackID': pack, 'downloadPolicy': {'onDemand': {}},
                    'fileSelectors': [{'file': row['path']}, {'directory': 'licenses/' + pack}],
                    'platforms': ['macOS']}
        manifest_path = output / (pack + '.json')
        manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
        subprocess.run(['xcrun', 'ba-package', 'package', str(manifest_path),
                        '--output-path', str(archive)], cwd=stage, check=True)
    return {'assetPackID': pack, 'path': str(archive), 'sha256': digest(archive),
            'bytes': archive.stat().st_size, 'modelPath': row['path']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    inv = sub.add_parser('inventory')
    inv.add_argument('--root', action='append', required=True, type=Path)
    inv.add_argument('--output', required=True, type=Path)
    inv.add_argument('--app-store', action='store_true', help='Apply the App Store model overrides')
    pkg = sub.add_parser('build')
    pkg.add_argument('--inventory', required=True, type=Path)
    pkg.add_argument('--model', required=True, help='Exact catalog-relative model path')
    pkg.add_argument('--notice', required=True, action='append', type=Path)
    pkg.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if args.command == 'inventory':
        catalog = json.loads((ROOT / 'config/models.json').read_text())
        if args.app_store:
            files = {a['path']: a for a in catalog['files']}
            files.update({a['path']: a for a in json.loads((ROOT / 'config/models-store.json').read_text())['files']})
            catalog['files'] = list(files.values())
        rows = inventory(catalog, args.root)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(rows, ensure_ascii=False, indent=2) + '\n')
        print(json.dumps({'verified': sum(r['status'] == 'verified' for r in rows),
                          'total': len(rows), 'missing': [r['path'] for r in rows if r['status'] != 'verified']}))
    else:
        rows = json.loads(args.inventory.read_text())
        row = next(r for r in rows if r['path'] == args.model)
        args.output = args.output.resolve()
        args.output.mkdir(parents=True, exist_ok=True)
        receipt = build(row, args.output, [p.resolve() for p in args.notice])
        (args.output / (row['assetPackID'] + '.receipt.json')).write_text(json.dumps(receipt, indent=2) + '\n')
        print(json.dumps(receipt))


if __name__ == '__main__':
    main()
