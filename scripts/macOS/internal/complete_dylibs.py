#!/usr/bin/env python3
"""Complete macdeployqt's transitive @rpath dylibs before signing the app."""
import os
from pathlib import Path
import shutil
import subprocess
import sys

app = Path(sys.argv[1]).resolve()
frameworks = app / 'Contents/Frameworks'
executable_dir = app / 'Contents/MacOS'
brew = Path(subprocess.check_output(['brew', '--prefix'], text=True).strip())
source_roots = [brew / 'lib', Path(os.environ['QT_ROOT']) / 'lib']


def macho(path):
    if not path.is_file() or path.is_symlink():
        return False
    with path.open('rb') as stream:
        return stream.read(4) in (
            b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf',
            b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca',
            b'\xca\xfe\xba\xbf', b'\xbf\xba\xfe\xca')


pending = [path for path in app.rglob('*') if macho(path)]
for binary in pending:
    identity = subprocess.check_output(['otool', '-D', str(binary)], text=True).splitlines()[1:]
    lines = subprocess.check_output(['otool', '-L', str(binary)], text=True).splitlines()[1:]
    for line in lines:
        dependency = line.strip().split(' (', 1)[0]
        if dependency in identity or dependency.startswith(('/usr/lib/', '/System/Library/')):
            continue
        if dependency.startswith('@rpath/'):
            relative = dependency[len('@rpath/'):]
            target = frameworks / relative
            if not target.exists():
                # Qt deploys frameworks itself. Only complete missing flat dylibs.
                if Path(relative).name != relative or not relative.endswith('.dylib'):
                    raise SystemExit(f'Unresolved framework dependency: {binary}: {dependency}')
                candidates = {p.resolve() for root in source_roots if (p := root / relative).is_file()}
                if len(candidates) != 1:
                    raise SystemExit(f'Cannot uniquely resolve {binary}: {dependency}: {candidates}')
                source = candidates.pop()
                if not macho(source):
                    raise SystemExit(f'Not a Mach-O dependency: {source}')
                shutil.copy2(source, target)
                pending.append(target)
                print(f'Bundled transitive dependency: {source} -> {target}', flush=True)
            replacement = '@loader_path/' + os.path.relpath(target, binary.parent)
            subprocess.run(['install_name_tool', '-change', dependency, replacement, str(binary)], check=True)
        elif dependency.startswith('@loader_path/'):
            target = binary.parent / dependency[len('@loader_path/'):]
        elif dependency.startswith('@executable_path/'):
            target = executable_dir / dependency[len('@executable_path/'):]
        else:
            raise SystemExit(f'Unbundled dependency: {binary}: {dependency}')
        if not target.is_file() or not target.resolve().is_relative_to(app):
            raise SystemExit(f'Dependency missing or outside app: {binary}: {dependency}')
print(f'Validated runtime dependencies of {len(pending)} Mach-O files')
