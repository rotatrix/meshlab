#!/bin/bash
# The same app-only payload is used for previews and central release signing.
set -euo pipefail
APP=$(realpath "${1:?Usage: 3_pack_app.sh app build-dir output-dir variant}")
BUILD=$(realpath "${2:?Missing build directory}")
mkdir -p "${3:?Missing output directory}"
OUTPUT=$(realpath "$3")
VARIANT=${4:?Missing architecture-precision-SHA variant}
[[ "$VARIANT" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]] || exit 1
[[ "$(basename "$APP")" == meshlab.app ]] || exit 1
SOURCE=$(cd "$(dirname "$0")/../.." && pwd)
QT_ROOT=${QT_ROOT:-$(qmake -query QT_INSTALL_PREFIX)}
export APP BUILD SOURCE QT_ROOT
python3 "$SOURCE/scripts/macOS/internal/complete_dylibs.py" "$APP"

# Keep notices in Resources, including transitive bundled-source notices.
python3 - <<'PY'
import os, plistlib, shutil, subprocess, sys
from pathlib import Path
app, source, build, qt = (Path(os.environ[k]) for k in ('APP', 'SOURCE', 'BUILD', 'QT_ROOT'))
with (app / 'Contents/Info.plist').open('rb') as stream:
    info = plistlib.load(stream)
if info.get('CFBundleIdentifier') != 'com.vcg.meshlab' or info.get('CFBundleExecutable') != 'meshlab':
    raise SystemExit('Expected upstream MeshLab bundle identity')
dest = app / 'Contents/Resources/licenses'
roots = [('MeshLab', source), ('dependencies', build / '_deps'), ('Qt', qt)]
# Homebrew libraries may be selected by upstream CMake instead of downloaded.
brew = Path(subprocess.check_output(['brew', '--prefix'], text=True).strip()) / 'opt'
formulae = ('qt@5', 'openssl@3', 'libomp', 'glew', 'gmp', 'mpfr', 'boost', 'eigen',
            'cgal', 'tbb', 'embree', 'xerces-c')
installed = [name for name in formulae if (brew / name).exists()]
deps = subprocess.check_output(['brew', 'deps', '--installed', '--union', *installed], text=True).split()
for name in sorted(set(installed + deps)):
    if (brew / name).exists():
        roots.append((name, (brew / name).resolve()))
sys.path.insert(0, str(source / 'scripts/rotatrix'))
from license_notices import collect
# This generated notice directory belongs to this packaging stage only.
if dest.exists():
    shutil.rmtree(dest)
collect(dest, roots)
for name in ('OpenAxis.md', 'OpenAxis.rotatrix.yaml'):
    shutil.copyfile(source / 'docs' / name, app / 'Contents/Resources' / name)
(app / 'Contents/Resources/BUILD_COMMIT.txt').write_text(
    subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True))
PY

bash "$SOURCE/scripts/macOS/internal/2b_adhoc_test_bundle.sh" "$APP"
ARCHIVE="$OUTPUT/macos-app-$VARIANT.tar.gz"
COPYFILE_DISABLE=1 tar -czf "$ARCHIVE" -C "$(dirname "$APP")" meshlab.app
SMOKE=$(mktemp -d "${TMPDIR:-/tmp}/meshlab-smoke.XXXXXX")
trap 'rm -rf "$SMOKE"' EXIT
tar -xzf "$ARCHIVE" -C "$SMOKE"
codesign --verify --deep --strict --verbose=2 "$SMOKE/meshlab.app"
"$SMOKE/meshlab.app/Contents/MacOS/meshlab" --version
printf 'Validated app archive: %s\n' "$ARCHIVE"
