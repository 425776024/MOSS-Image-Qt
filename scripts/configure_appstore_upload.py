#!/usr/bin/env python3
"""Run manually in your Terminal. Password input is hidden and never written to files."""
import getpass
import subprocess
from pathlib import Path

if __name__ == '__main__':
    account = input('Apple developer email: ').strip()
    password = getpass.getpass('Apple app-specific password (hidden): ')
    result = subprocess.run(['xcrun', 'altool', '--store-password-in-keychain-item',
                             '--item', 'Xinshen-AppStore-Upload', '--username', account,
                             '--app-password', password], text=True)
    del password
    if result.returncode == 0:
        result = subprocess.run(['swift', str(Path(__file__).with_name('appstore_keychain_service.swift')),
                                 'Xinshen-AppStore-Upload', account])
    if result.returncode == 0:
        result = subprocess.run(['xcrun', 'altool', '--list-asset-packs', '--apple-id', '6825373745',
                                 '--username', account, '--password', '@keychain:Xinshen-AppStore-Upload'])
        if result.returncode == 0:
            print('Upload credential stored and verified successfully.')
    raise SystemExit(result.returncode)
