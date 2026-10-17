"""Verify and stage exact CI outputs. No compilation or package rewriting."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
from urllib.parse import quote

REPO = 'rotatrix/meshlab'
WORKFLOW = '.github/workflows/BuildMeshLab.yml'
PLATFORMS = ('ubuntu-22.04', 'windows-2022', 'macos-15', 'macos-15-intel')
TAG = re.compile(r'(MeshLab-[0-9]+\.[0-9]+)-rotatrix\.[1-9][0-9]*(?:-beta\.[1-9][0-9]*)?\Z')


def require(value, message):
    if not value:
        raise ValueError(message)


def api(path):
    return json.loads(subprocess.check_output(['gh', 'api', f'repos/{REPO}/{path}'], text=True))


def pages(path, key):
    result = []
    for page in range(1, 101):
        rows = api(f'{path}{"&" if "?" in path else "?"}per_page=100&page={page}')[key]
        result.extend(rows)
        if len(rows) < 100:
            return result
    raise ValueError('Unexpected pagination limit')


def tag_commit(tag):
    obj = api('git/ref/tags/' + quote(tag, safe=''))['object']
    for _ in range(8):
        if obj['type'] == 'commit':
            return obj['sha']
        require(obj['type'] == 'tag', 'Tag does not identify a commit')
        obj = api('git/tags/' + obj['sha'])['object']
    raise ValueError('Excessively nested tag')


def validate_run(run, sha):
    require(run['repository']['full_name'] == REPO and run['head_repository']['full_name'] == REPO,
            'Build must originate in rotatrix/meshlab')
    require(run['path'] == WORKFLOW, 'Unapproved build workflow')
    require(run['event'] in ('push', 'workflow_dispatch') and
            re.fullmatch(r'rotatrix/[A-Za-z0-9._/-]+', run.get('head_branch') or ''),
            'Only trusted Rotatrix branch builds can be promoted; PRs are excluded')
    require(run['head_sha'] == sha, 'Build SHA differs from immutable tag')
    require(run['status'] == 'completed' and run['conclusion'] == 'success', 'Build has not succeeded')


def expected(sha):
    short = sha[:12]
    return {f'MeshLab-windows-x64-{short}.zip',
            *(f'MeshLab-linux-{arch}-{short}.{ext}' for arch in ('x64',)
              for ext in ('tar.gz', 'AppImage')),
            *(f'macos-app-{arch}-{short}.tar.gz' for arch in ('x64', 'arm64')),
            *(f'build-{platform}-{short}.json' for platform in PLATFORMS)}


def platform_packages(platform, sha):
    short = sha[:12]
    if platform == 'windows-2022':
        return {f'MeshLab-windows-x64-{short}.zip'}
    if platform.startswith('macos-'):
        arch = 'x64' if platform.endswith('-intel') else 'arm64'
        return {f'macos-app-{arch}-{short}.tar.gz'}
    arch = 'arm64' if platform.endswith('-arm') else 'x64'
    names = {f'MeshLab-linux-{arch}-{short}.{ext}' for ext in ('tar.gz', 'AppImage')}
    return names


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def verify_attestation(path, run, output):
    evidence = subprocess.check_output([
        'gh', 'attestation', 'verify', str(path), '--repo', REPO,
        '--signer-workflow', REPO + '/' + WORKFLOW,
        '--source-ref', 'refs/heads/' + run['head_branch'], '--source-digest', run['head_sha'],
        '--deny-self-hosted-runners', '--format', 'json'], text=True)
    invocation = f'https://github.com/{REPO}/actions/runs/{run["id"]}/attempts/'
    matched = False
    for item in json.loads(evidence):
        cert = item.get('verificationResult', {}).get('signature', {}).get('certificate', {})
        uri = cert.get('runInvocationURI', '')
        attempt = uri.removeprefix(invocation)
        if uri.startswith(invocation) and attempt.isdecimal():
            matched |= (1 <= int(attempt) <= run['run_attempt'] and cert.get('buildTrigger') == run['event'])
    require(matched, 'Attestation does not identify selected build run/event')
    (output / (path.name + '.attestation.json')).write_text(evidence)
    # Retain the signed bundle as well as the verification report.
    subprocess.run(['gh', 'attestation', 'download', str(path.resolve()), '--repo', REPO],
                   cwd=output, check=True)
    # gh uses a colon on Unix; keep release evidence downloadable on Windows.
    bundle = output / ('sha256:' + digest(path) + '.jsonl')
    if bundle.exists():
        bundle.rename(output / ('sha256-' + digest(path) + '.jsonl'))


def prepare(tag, run_id, output):
    require(not output.exists() or not any(output.iterdir()), 'Use an empty promotion directory')
    match = TAG.fullmatch(tag)
    require(match, 'Invalid MeshLab release tag')
    sha = tag_commit(tag)
    base = tag_commit(match[1])
    require(api(f'compare/{base}...{sha}')['status'] in ('ahead', 'identical'),
            'Release does not descend from the upstream tag')
    if not run_id:
        candidates = pages(f'actions/workflows/BuildMeshLab.yml/runs?head_sha={sha}&status=success', 'workflow_runs')
        eligible = []
        for run in candidates:
            try:
                validate_run(run, sha)
                eligible.append(run['id'])
            except ValueError:
                pass
        require(len(eligible) == 1, 'Select an explicit successful build run ID; no unique eligible run for this SHA')
        run_id = eligible[0]
    run = api(f'actions/runs/{run_id}')
    validate_run(run, sha)
    jobs = pages(f'actions/runs/{run_id}/jobs?filter=latest', 'jobs')
    required = {f'MeshLab {p} single_precision' for p in PLATFORMS}
    require(required <= {j['name'] for j in jobs if j['conclusion'] == 'success'},
            'Missing required platform checks; focused-only runs cannot be promoted alone')
    artifacts = pages(f'actions/runs/{run_id}/artifacts', 'artifacts')
    selected = [a for a in artifacts if a['name'] in expected(sha)]
    require(len(selected) == len(expected(sha)) and {a['name'] for a in selected} == expected(sha),
            'Missing or duplicate packages or build records; rebuild explicitly')
    output.mkdir(parents=True, exist_ok=True)
    records = []
    for artifact in selected:
        require(not artifact['expired'] and artifact['workflow_run']['id'] == run_id and
                artifact['workflow_run']['head_sha'] == sha, 'Expired or mismatched artifact')
        require(re.fullmatch(r'sha256:[0-9a-f]{64}', artifact.get('digest') or ''), 'Artifact has no digest')
        path = output / artifact['name']
        with path.open('wb') as stream:
            subprocess.run(['gh', 'api', f'repos/{REPO}/actions/artifacts/{artifact["id"]}/zip'],
                           stdout=stream, check=True)
        require('sha256:' + digest(path) == artifact['digest'], 'Downloaded artifact digest mismatch')
        verify_attestation(path, run, output)
        records.append(dict(id=artifact['id'], name=path.name, sha256=digest(path)))
    by_name = {r['name']: r['sha256'] for r in records}
    for platform in PLATFORMS:
        metadata = json.loads((output / f'build-{platform}-{sha[:12]}.json').read_text())
        require(metadata['commit'] == sha and metadata['run_id'] == run_id and metadata['repository'] == REPO
                and metadata['platform'] == platform and metadata['precision'] == 'single'
                and len(set(metadata['plugins'])) == 75, 'Build record does not match required configuration')
        require(metadata['ref'] == 'refs/heads/' + run['head_branch'] and metadata['event'] == run['event'],
                'Build record ref/event mismatch')
        required_packages = platform_packages(platform, sha)
        # Older trusted builds also recorded a source snapshot; do not promote it.
        legacy_sources = {f'MeshLab-sources-{sha[:12]}.tar.gz'} if platform == 'ubuntu-22.04' else set()
        require(metadata['version'] == match[1].removeprefix('MeshLab-') and
                required_packages <= set(metadata['packages']) and
                set(metadata['packages']) <= required_packages | legacy_sources,
                'Build version or platform package inventory mismatch')
        for name in required_packages:
            require(by_name.get(name) == metadata['packages'][name], 'Build record package hash mismatch')
    prerelease = '-beta.' in tag
    require(tag_commit(tag) == sha, 'Tag changed during verification')
    plan = dict(repository=REPO, tag=tag, commit=sha, run_id=run_id, run_attempt=run['run_attempt'],
                source_ref='refs/heads/' + run['head_branch'], prerelease=prerelease,
                artifacts=records, source_review_required=True)
    (output / 'promotion.json').write_text(json.dumps(plan, indent=2) + '\n')
    (output / 'SHA256SUMS').write_text(''.join(f'{r["sha256"]}  {r["name"]}\n' for r in sorted(records, key=lambda r: r['name'])))
    return plan


def publish(directory):
    require(os.environ.get('ROTATRIX_PROMOTION_ENABLED') == 'true', 'Central promotion transition is not enabled')
    require(os.environ.get('SOURCE_ACCEPTANCE') == 'true', 'Source/license completeness and package acceptance must be recorded')
    require(os.environ.get('SIGNER_DISPATCH_TOKEN'), 'Signing dispatch credential is missing')
    plan = json.loads((directory / 'promotion.json').read_text())
    require(plan['repository'] == REPO and TAG.fullmatch(plan['tag']), 'Invalid promotion plan')
    require(tag_commit(plan['tag']) == plan['commit'], 'Tag moved before publication')
    for record in plan['artifacts']:
        require(record['name'] in expected(plan['commit']) and
                digest(directory / record['name']) == record['sha256'], 'Staged artifact changed')
    # A retry must fail rather than replace or silently add to a published release.
    existing = subprocess.run(['gh', 'api', f'repos/{REPO}/releases/tags/{quote(plan["tag"], safe="")}'],
                              capture_output=True, text=True)
    require(existing.returncode != 0 and 'HTTP 404' in existing.stderr,
            'Release exists or cannot be inspected; verify existing assets before retrying')
    files = [str(p) for p in directory.iterdir() if p.is_file() and not p.name.startswith('macos-app-')]
    command = ['gh', 'release', 'create', plan['tag'], *files, '--repo', REPO, '--verify-tag', '--draft',
               '--title', 'MeshLab ' + plan['tag'], '--notes',
               f'Exact source: {plan["commit"]}\nBuild: https://github.com/{REPO}/actions/runs/{plan["run_id"]}\n'
               'Draft pending signed macOS packages and final release acceptance.']
    if plan['prerelease']:
        command.append('--prerelease')
    subprocess.run(command, check=True)
    # Signed inputs are retained with distinct names, never advertised as signed downloads.
    for record in plan['artifacts']:
        if not record['name'].startswith('macos-app-'):
            continue
        path = directory / record['name']
        subprocess.run(['gh', 'release', 'upload', plan['tag'],
                        str(path) + '#Signing input (ad-hoc)', '--repo', REPO], check=True)
        payload = dict(ref='main', inputs=dict(source_repository=REPO, source_run_id=str(plan['run_id']),
                       artifact_id=str(record['id']), release_tag=plan['tag'], publish=True))
        require(tag_commit(plan['tag']) == plan['commit'], 'Tag moved before signing dispatch')
        subprocess.run(['gh', 'api', '--method', 'POST',
                        'repos/rotatrix/release-signing/actions/workflows/sign-macos.yml/dispatches', '--input', '-'],
                       input=json.dumps(payload), text=True, check=True,
                       env={**os.environ, 'GH_TOKEN': os.environ['SIGNER_DISPATCH_TOKEN']})


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('command', choices=['prepare', 'publish'])
    parser.add_argument('--tag')
    parser.add_argument('--run-id', type=int)
    parser.add_argument('--directory', type=Path, default=Path('promotion'))
    args = parser.parse_args()
    if args.command == 'prepare':
        print(json.dumps(prepare(args.tag, args.run_id, args.directory), indent=2))
    else:
        publish(args.directory)
