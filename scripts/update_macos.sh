#!/bin/sh
# Runs outside the app bundle after a verified download. No Python dependency.
set -eu
target=$1
staged=$2
work=$3
parent_pid=$4
token=$5
case "$token" in ''|*[!0-9a-f]*) exit 2;; esac
[ "${#token}" = 32 ] || exit 2
case "$parent_pid" in ''|*[!0-9]*) exit 2;; esac
[ "$parent_pid" -gt 1 ] || exit 2
case "$target" in /*.app) ;; *) exit 2;; esac
case "$work" in /*/app-updates/"$token") ;; *) exit 2;; esac
case "$staged" in "$work"/staged/*.app) ;; *) exit 2;; esac
[ -d "$target" ] && [ ! -L "$target" ] && [ -d "$staged" ] && [ ! -L "$staged" ] || exit 2
[ -f "$work/pending.json" ] || exit 2
[ "$(/usr/bin/plutil -extract target raw -o - "$work/pending.json")" = "$target" ] || exit 2
[ "$(/usr/bin/plutil -extract token raw -o - "$work/pending.json")" = "$token" ] || exit 2
[ ! -e "$work/started.json" ] || exit 2
backup="$target.previous-$token"
incoming="$target.incoming-$token"
[ ! -e "$backup" ] && [ ! -e "$incoming" ] || exit 2
cleanup() { rm -rf -- "$work"; }
# Copy to the installation volume before touching the running app. Renames below
# then remain atomic even if the user's cache lives on another volume.
if ! /usr/bin/ditto "$staged" "$incoming"; then rm -rf -- "$incoming"; cleanup; exit 3; fi
touch "$work/helper-ready"
count=0
while kill -0 "$parent_pid" 2>/dev/null; do
    if [ -f "$work/cancel.json" ]; then rm -rf -- "$incoming"; cleanup; exit 4; fi
    count=$((count+1))
    if [ "$count" -ge 150 ]; then rm -rf -- "$incoming"; cleanup; exit 4; fi
    sleep 2
done
if [ -f "$work/cancel.json" ]; then rm -rf -- "$incoming"; cleanup; exit 4; fi
if ! mv -- "$target" "$backup"; then
    rm -rf -- "$incoming"; cleanup
    "$target/Contents/MacOS/MossStudio" > /dev/null 2>&1 &
    exit 5
fi
if ! mv -- "$incoming" "$target"; then
    mv -- "$backup" "$target"; cleanup
    "$target/Contents/MacOS/MossStudio" > /dev/null 2>&1 &
    exit 6
fi
"$target/Contents/MacOS/MossStudio" --complete-update "$token" > /dev/null 2>&1 &
new_pid=$!
count=0
while [ "$count" -lt 45 ]; do
    if [ -f "$work/started.json" ] && kill -0 "$new_pid" 2>/dev/null; then
        # A new QML root window acknowledged this exact transaction.
        sleep 2
        if kill -0 "$new_pid" 2>/dev/null; then
            rm -rf -- "$backup"
            cleanup
            echo 'Update installed; previous app and downloaded package removed.'
            exit 0
        fi
    fi
    kill -0 "$new_pid" 2>/dev/null || break
    count=$((count+1)); sleep 1
done
kill "$new_pid" 2>/dev/null || true
wait "$new_pid" 2>/dev/null || true
# Preserve all app-external user data. The failed replacement is the only
# directory removed here; the original bundle is restored at its original path.
rm -rf -- "$target"
mv -- "$backup" "$target"
"$target/Contents/MacOS/MossStudio" > /dev/null 2>&1 &
cleanup
echo 'Update failed to start; previous application restored.'
exit 7
