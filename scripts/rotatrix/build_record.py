"""Build-time records and source preservation; never used to rebuild at promotion."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tarfile


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def git(*args):
    return subprocess.check_output(['git', *args], text=True).strip()


def sources():
    sha = git('rev-parse', 'HEAD')
    output = Path('portable') / f'MeshLab-sources-{sha[:12]}.tar.gz'
    output.parent.mkdir(exist_ok=True)
    def keep(member):
        parts = Path(member.name).parts
        return None if '.git' in parts or 'CMakeFiles' in parts else member
    with tarfile.open(output, 'w:gz') as archive:
        # git ls-files includes the submodule directory. tar preserves its checked
        # out sources, while keep() excludes its .git pointer and credentials.
        for name in git('ls-files', '-z').split('\0'):
            path = Path(name)
            if name and path.exists():
                archive.add(path, arcname='meshlab/' + path.as_posix(), filter=keep)
        downloads = Path('src/external/downloads')
        if downloads.exists():
            archive.add(downloads, arcname='meshlab/src/external/downloads', filter=keep)
        for path in sorted(Path('build/_deps').glob('*-src')):
            archive.add(path, arcname='meshlab/dependency-sources/' + path.name, filter=keep)
    # Includes fetched sources; system-library corresponding-source obligations
    # still require review before distribution. Do not claim this is exhaustive.


def record(platform):
    sha = git('rev-parse', 'HEAD')
    plugin_roots = [Path('install/plugins'), Path('install/usr/lib/meshlab/plugins'),
                    Path('install/meshlab.app/Contents/PlugIns')]
    plugins = sorted(p.name for root in plugin_roots if root.exists()
                     for p in root.iterdir() if p.suffix in ('.dll', '.so'))
    if len(plugins) != 75:
        raise ValueError(f'Expected all 75 MeshLab plugins, found {len(plugins)}')
    packages = {p.name: digest(p) for p in sorted(Path('portable').iterdir()) if p.is_file()}
    result = dict(schema=1, repository=os.environ['GITHUB_REPOSITORY'], commit=sha,
                  ref=os.environ['GITHUB_REF'], event=os.environ['GITHUB_EVENT_NAME'],
                  run_id=int(os.environ['GITHUB_RUN_ID']),
                  run_attempt=int(os.environ['GITHUB_RUN_ATTEMPT']), platform=platform,
                  precision='single', version=Path('ML_VERSION').read_text().strip(),
                  windows_signed=os.environ.get('WINDOWS_SIGNED') == 'true',
                  plugins=plugins, packages=packages,
                  submodules=git('submodule', 'status', '--recursive').splitlines(),
                  source_review_required=True)
    Path('provenance').mkdir(exist_ok=True)
    Path(f'provenance/build-{platform}-{sha[:12]}.json').write_text(
        json.dumps(result, indent=2, sort_keys=True) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('command', choices=['sources', 'record'])
    parser.add_argument('--platform')
    args = parser.parse_args()
    if args.command == 'sources':
        sources()
    else:
        record(args.platform)
