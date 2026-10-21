#!/usr/bin/env python3
"""Interactively upload one signing identity and notary key; never print secret values."""
import argparse
import base64
import getpass
import json
from pathlib import Path
import subprocess

NAMES = ('APPLE_CERTIFICATE_P12_BASE64', 'APPLE_CERTIFICATE_PASSWORD',
         'APPLE_NOTARY_KEY_P8', 'APPLE_NOTARY_KEY_ID', 'APPLE_NOTARY_ISSUER_ID')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', default='425776024/MOSS-Image-Qt')
    args = parser.parse_args()
    # Require an existing restricted environment; do not silently create an open one.
    result = subprocess.run(['gh', 'api', f'repos/{args.repo}/environments/macos-release'],
                            check=True, capture_output=True, text=True)
    environment = json.loads(result.stdout)
    policy = environment.get('deployment_branch_policy') or {}
    if not (policy.get('custom_branch_policies') or policy.get('protected_branches')):
        raise SystemExit('先在 GitHub macos-release Environment 中限制发布分支/标签，详见 docs/macos-release.md。')
    print(f'将通过 gh 的加密 Secret 接口上传至 {args.repo} / macos-release。')
    print('请勿把证书、私钥或密码粘贴到聊天、源码或命令行参数中。')
    p12 = Path(input('Developer ID Application .p12 文件路径：').strip()).expanduser()
    password = getpass.getpass('.p12 导出密码（隐藏输入）：')
    p8 = Path(input('App Store Connect 团队 API Key .p8 文件路径：').strip()).expanduser()
    key_id = input('API Key ID：').strip()
    issuer = input('Issuer ID：').strip()
    values = [base64.b64encode(p12.read_bytes()).decode(), password, p8.read_text(), key_id, issuer]
    if not all(values) or 'PRIVATE KEY' not in values[2]:
        raise SystemExit('缺少必要信息或 .p8 文件格式不正确；没有上传。')
    for name, value in zip(NAMES, values):
        subprocess.run(['gh', 'secret', 'set', name, '--repo', args.repo, '--env', 'macos-release'],
                       input=value, text=True, check=True, stdout=subprocess.DEVNULL)
        print(f'已保存 {name}（不显示内容）')
    print('全部已保存。请从默认分支手动运行 macOS App and DMG，勾选 notarize。')


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError:
        raise SystemExit('GitHub 操作失败；检查 gh 登录和仓库权限。已上传的 Secret 可在环境设置中管理。') from None
