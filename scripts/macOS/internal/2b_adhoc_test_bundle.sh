#!/bin/bash
# Test bundles have no Developer ID or notarization. Give their final contents
# valid ad-hoc signatures after macdeployqt has rewritten library paths.
set -euo pipefail
APP=${1:?Usage: 2b_adhoc_test_bundle.sh path/to/meshlab.app}
test -d "$APP/Contents/MacOS"

# U3D installs its build-only static archive alongside runtime libraries.
# Archives cannot be loaded at runtime and must not occupy a nested-code slot.
find "$APP/Contents/Frameworks" -type f -name '*.a' -print -delete

echo 'Checking signatures before ad-hoc signing:'
if ! codesign --verify --deep --strict --verbose=2 "$APP"; then
    echo 'Bundle is unsigned or has stale signatures; signing final test contents.'
fi

# Sign actual Mach-O files first, then enclosing bundles inside out. Do not use
# --deep for signing: Qt plugins and libraries must each be handled explicitly.
while IFS= read -r -d '' binary; do
    # Signing the main executable also seals the enclosing app. Defer it until
    # every plugin and framework is signed, via the bundle pass below.
    if [[ "$binary" == "$APP/Contents/MacOS/meshlab" ]]; then
        continue
    fi
    if /usr/bin/file -b "$binary" | grep -q 'Mach-O'; then
        codesign --force --sign - --timestamp=none "$binary"
    fi
done < <(find "$APP" -type f -print0)

while IFS= read -r -d '' bundle; do
    codesign --force --sign - --timestamp=none "$bundle"
done < <(find "$APP" -depth -type d \( -name '*.framework' -o -name '*.app' -o -name '*.xpc' -o -name '*.bundle' \) -print0)

codesign --verify --deep --strict --verbose=2 "$APP"
codesign --display --verbose=2 "$APP"
"$APP/Contents/MacOS/meshlab" --version
