#!/bin/bash
# Finishes what macdeployqt starts on Homebrew's Qt. It repoints what bundled libraries link, but
# leaves Homebrew paths in each library's own name and search paths. Those paths are not used to
# load anything, but they tie the bundle to the machine it was made on.
#
# Usage: fix-install-names.sh path/to/AnyDevKitStudio.app
set -euo pipefail

app=$1
frameworks="$app/Contents/Frameworks"

while IFS= read -r -d '' file; do
    file --brief "$file" | grep -q 'Mach-O' || continue

    name=$(otool -D "$file" | tail -n +2)
    case "$name" in
        @* | "") ;;
        *) install_name_tool -id "@executable_path/../Frameworks/${file#"$frameworks"/}" "$file" 2>/dev/null ;;
    esac

    otool -l "$file" | awk '$1 == "cmd" && $2 == "LC_RPATH" { getline; getline; print $2 }' |
        while read -r rpath; do
            case "$rpath" in
                @*) ;;
                *) install_name_tool -delete_rpath "$rpath" "$file" 2>/dev/null ;;
            esac
        done
done < <(find "$frameworks" -type f -print0)

# Editing a library voids its signature, and Apple Silicon refuses to load unsigned code. An ad-hoc
# signature is enough for that. It does not replace Developer ID signing.
codesign --force --deep --sign - "$app"
