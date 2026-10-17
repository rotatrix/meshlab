"""Build-time package and plugin inventory for diagnostics."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def git(*args):
    return subprocess.check_output(['git', *args], text=True).strip()


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
    parser.add_argument('command', choices=['record'])
    parser.add_argument('--platform')
    args = parser.parse_args()
    record(args.platform)
