#!/bin/bash
# Double-click in Finder, or run from a terminal. Private keys stay in Keychain.
cd "$(dirname "$0")/.." || exit 1
python3 scripts/release_macos.py "$@"
result=$?
if [[ -t 0 && "$TERM_PROGRAM" == "Apple_Terminal" ]]; then
    read -r -p '按 Return 关闭此窗口 / Press Return to close…' _
fi
exit "$result"
