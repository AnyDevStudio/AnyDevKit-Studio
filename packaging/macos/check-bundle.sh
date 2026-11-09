#!/bin/bash
# Proves a macOS app bundle runs on its own. Every library it links must be inside the bundle or
# part of the system, and it must start with Homebrew unreadable.
#
# Usage: check-bundle.sh path/to/AnyDevKitStudio.app screenshot.png
set -euo pipefail

app=$1
screenshot=$2
program="$app/Contents/MacOS/AnyDevKitStudio"
failures=0

while IFS= read -r -d '' file; do
    file --brief "$file" | grep -q 'Mach-O' || continue

    while read -r dependency; do
        case "$dependency" in
            @executable_path/* | @loader_path/* | @rpath/* | /usr/lib/* | /System/Library/*) ;;
            *)
                echo "outside the bundle: ${file#"$app"/} links $dependency"
                failures=$((failures + 1))
                ;;
        esac
    done < <(otool -L "$file" | tail -n +2 | awk '{print $1}')

    while read -r rpath; do
        case "$rpath" in
            @executable_path* | @loader_path*) ;;
            *)
                echo "outside the bundle: ${file#"$app"/} searches $rpath"
                failures=$((failures + 1))
                ;;
        esac
    done < <(otool -l "$file" | awk '$1 == "cmd" && $2 == "LC_RPATH" { getline; getline; print $2 }')
done < <(find "$app" -type f -print0)

if [ "$failures" -ne 0 ]; then
    echo "$failures link(s) leave the bundle"
    exit 1
fi

codesign --verify --deep --strict "$app"

QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
    sandbox-exec -p '(version 1)(allow default)(deny file-read* (subpath "/opt/homebrew") (subpath "/usr/local/opt"))' \
    "$program" --screenshot "$screenshot"
test -s "$screenshot"
echo "bundle is self-contained: $screenshot"
