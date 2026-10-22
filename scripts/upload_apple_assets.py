#!/usr/bin/env python3
"""Upload verified .aar receipts with altool; resume only confirmed successful uploads."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import subprocess
from apple_model_assets import digest


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--directory',required=True,type=Path)
    p.add_argument('--username',required=True)
    p.add_argument('--app-id',default='6825373745')
    p.add_argument('--keychain-item',default='Xinshen-AppStore-Upload')
    args=p.parse_args()
    receipts=[json.loads(f.read_text()) for f in args.directory.glob('*.receipt.json')]
    def upload(row):
        archive=Path(row['path']); pack=row['assetPackID']
        done=args.directory/(pack+'.uploaded.json')
        if done.exists():
            saved=json.loads(done.read_text())
            if saved.get('appId')==args.app_id and saved.get('archiveSha256')==row['sha256']:
                return True
        if digest(archive)!=row['sha256']:raise ValueError('Archive changed: '+str(archive))
        log=args.directory/(pack+'.upload.log')
        if log.exists() and not done.exists():
            raise ValueError('Previous upload outcome must be reconciled before retry: '+str(log))
        print('Uploading '+row['modelPath'],flush=True)
        with log.open('w') as stream:
            run=subprocess.run(['xcrun','altool','--upload-asset-pack',str(archive),'--apple-id',args.app_id,
                '--username',args.username,'--password','@keychain:'+args.keychain_item,'--wait'],stdout=stream,stderr=subprocess.STDOUT)
        text=log.read_text()
        ok=run.returncode==0 and 'Status: READY_FOR_TESTING' in text
        if ok:
            done.write_text(json.dumps({'appId':args.app_id,'assetPackID':pack,'archiveSha256':row['sha256'],
                'status':'READY_FOR_TESTING','modelPath':row['modelPath']},indent=2)+'\n')
        print(('Ready for testing: ' if ok else 'FAILED; inspect log: ')+row['modelPath'],flush=True)
        return ok
    with ThreadPoolExecutor(max_workers=2) as pool:
        results=list(pool.map(upload,sorted(receipts,key=lambda r:r['bytes'])))
    raise SystemExit(0 if all(results) else 1)

if __name__=='__main__':main()
