import copy
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import tarfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/rotatrix'))
import promote
import license_notices
import build_record

SHA = 'a' * 40


def run():
    return dict(id=42, repository=dict(full_name=promote.REPO),
                head_repository=dict(full_name=promote.REPO), path=promote.WORKFLOW,
                event='push', head_branch='rotatrix/work/MeshLab-2025.07', head_sha=SHA,
                status='completed', conclusion='success', run_attempt=2)


class PromotionChecks(unittest.TestCase):
    def test_trusted_branch_and_manual_build(self):
        for event in ('push', 'workflow_dispatch'):
            candidate = run()
            candidate['event'] = event
            promote.validate_run(candidate, SHA)

    def test_reject_wrong_origin_workflow_event_ref_sha_or_status(self):
        for key, value in (
            ('repository', dict(full_name='someone/meshlab')),
            ('head_repository', dict(full_name='someone/meshlab')),
            ('path', '.github/workflows/untrusted.yml'), ('event', 'pull_request'),
            ('head_branch', 'main'), ('head_sha', 'b' * 40),
            ('status', 'in_progress'), ('conclusion', 'failure')):
            candidate = run()
            candidate[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                promote.validate_run(candidate, SHA)

    def test_all_required_platform_records_and_packages(self):
        names = promote.expected(SHA)
        self.assertEqual(len(names), 13)
        self.assertEqual(len([n for n in names if n.startswith('build-')]), 5)
        self.assertFalse(any('installer' in n or 'double' in n for n in names))
        self.assertTrue(all(len(n) < 65 and '/' not in n for n in names))

    def test_original_ref_and_run_attestation(self):
        candidate = run()
        certificate = dict(runInvocationURI=f'https://github.com/{promote.REPO}/actions/runs/42/attempts/1',
                           buildTrigger='push')
        evidence = [dict(verificationResult=dict(signature=dict(certificate=certificate)))]
        with tempfile.TemporaryDirectory() as root:
            output = Path(root)
            package = output / 'package.zip'
            package.write_bytes(b'package')
            with patch('promote.subprocess.check_output', return_value=json.dumps(evidence)) as command, \
                    patch('promote.subprocess.run'):
                promote.verify_attestation(package, candidate, output)
                args = command.call_args.args[0]
                self.assertIn('refs/heads/rotatrix/work/MeshLab-2025.07', args)
                self.assertIn('--deny-self-hosted-runners', args)
            for uri, event in ((certificate['runInvocationURI'].replace('/42/', '/43/'), 'push'),
                               (certificate['runInvocationURI'], 'pull_request'),
                               (certificate['runInvocationURI'].replace('/1', '/3'), 'push')):
                bad = copy.deepcopy(evidence)
                bad[0]['verificationResult']['signature']['certificate'].update(
                    runInvocationURI=uri, buildTrigger=event)
                with patch('promote.subprocess.check_output', return_value=json.dumps(bad)), \
                        self.assertRaises(ValueError):
                    promote.verify_attestation(package, candidate, output)

    def test_missing_platform_checks_stop_before_download(self):
        with tempfile.TemporaryDirectory() as root, \
                patch('promote.tag_commit', return_value=SHA), \
                patch('promote.api', side_effect=[{'status': 'identical'}, run()]), \
                patch('promote.pages', return_value=[]), self.assertRaisesRegex(ValueError, 'platform'):
            promote.prepare('MeshLab-2025.07-rotatrix.1-beta.1', 42, Path(root))

    def test_missing_provenance_stops_before_download(self):
        jobs = [dict(name=f'MeshLab {p} single_precision', conclusion='success') for p in promote.PLATFORMS]
        with tempfile.TemporaryDirectory() as root, \
                patch('promote.tag_commit', return_value=SHA), \
                patch('promote.api', side_effect=[{'status': 'identical'}, run()]), \
                patch('promote.pages', side_effect=[jobs, []]), self.assertRaisesRegex(ValueError, 'Missing'):
            promote.prepare('MeshLab-2025.07-rotatrix.1-beta.1', 42, Path(root))

    def test_publish_disabled_before_any_external_action(self):
        with patch.dict('os.environ', {}, clear=True), patch('promote.api') as api, \
                self.assertRaisesRegex(ValueError, 'transition'):
            promote.publish(Path('not-used'))
        api.assert_not_called()

    def test_complete_prepare_preserves_bytes_and_rejects_tampering(self):
        payloads = {name: name.encode() for name in promote.expected(SHA) if not name.startswith('build-')}
        for platform in promote.PLATFORMS:
            metadata = dict(commit=SHA, run_id=42, repository=promote.REPO, platform=platform,
                            precision='single', plugins=[str(i) for i in range(75)],
                            ref='refs/heads/' + run()['head_branch'], event='push',
                            version='2025.07', windows_signed=True,
                            packages={name: hashlib.sha256(payloads[name]).hexdigest()
                                      for name in promote.platform_packages(platform, SHA)})
            payloads[f'build-{platform}-{SHA[:12]}.json'] = json.dumps(metadata).encode()
        artifacts = [dict(id=i, name=name, expired=False,
                          workflow_run=dict(id=42, head_sha=SHA),
                          digest='sha256:' + hashlib.sha256(data).hexdigest())
                     for i, (name, data) in enumerate(payloads.items(), 1)]
        jobs = [dict(name=f'MeshLab {p} single_precision', conclusion='success') for p in promote.PLATFORMS]
        for tamper in (False, True):
            def download(command, stdout, **kwargs):
                artifact_id = int(command[-1].split('/')[-2])
                data = payloads[artifacts[artifact_id - 1]['name']]
                stdout.write(data + (b'changed' if tamper else b''))
            with tempfile.TemporaryDirectory() as root, \
                    patch('promote.tag_commit', return_value=SHA), \
                    patch('promote.api', side_effect=[{'status': 'identical'}, run()]), \
                    patch('promote.pages', side_effect=[jobs, artifacts]), \
                    patch('promote.verify_attestation'), patch('promote.subprocess.run', side_effect=download):
                if tamper:
                    with self.assertRaisesRegex(ValueError, 'digest mismatch'):
                        promote.prepare('MeshLab-2025.07-rotatrix.1-beta.1', 42, Path(root))
                else:
                    result = promote.prepare('MeshLab-2025.07-rotatrix.1-beta.1', 42, Path(root))
                    self.assertEqual(result['run_id'], 42)
                    for name, data in payloads.items():
                        self.assertEqual((Path(root) / name).read_bytes(), data)

    def test_source_acceptance_required(self):
        with patch.dict('os.environ', {'ROTATRIX_PROMOTION_ENABLED': 'true'}, clear=True), \
                self.assertRaisesRegex(ValueError, 'completeness'):
            promote.publish(Path('not-used'))

    def test_notices_keep_mapping_without_deep_output_paths(self):
        with tempfile.TemporaryDirectory() as root:
            source, output = Path(root) / 'source', Path(root) / 'notices'
            deep = source / 'dependency' / ('long-name-' * 10)
            deep.mkdir(parents=True)
            (deep / 'LICENSE').write_text('license text')
            (source / 'COPYING').write_text('license text')
            license_notices.collect(output, [('dependency', source)])
            records = json.loads((output / 'sources.json').read_text())
            self.assertEqual(len(records), 2)
            self.assertEqual(records[0]['file'], records[1]['file'])
            for record in records:
                payload = output / record['file']
                self.assertEqual(hashlib.sha256(payload.read_bytes()).hexdigest(), record['sha256'])
                self.assertLess(len(payload.relative_to(output).as_posix()), 70)
            self.assertTrue(any('long-name' in r['source'] for r in records))

    def test_build_record_and_source_snapshot(self):
        previous = Path.cwd()
        with tempfile.TemporaryDirectory() as root:
            try:
                os.chdir(root)
                Path('ML_VERSION').write_text('2025.07\n')
                plugins = Path('install/plugins')
                plugins.mkdir(parents=True)
                for i in range(75):
                    (plugins / f'plugin{i}.dll').write_bytes(b'plugin')
                submodule = Path('src/vcglib')
                submodule.mkdir(parents=True)
                (submodule / 'source.h').write_text('source')
                (submodule / '.git').write_text('private git pointer')
                fetched = Path('build/_deps/openaxis-src')
                fetched.mkdir(parents=True)
                (fetched / 'LICENSE').write_text('license')
                with patch('build_record.git', side_effect=[SHA, 'ML_VERSION\0src/vcglib\0']):
                    build_record.sources()
                archive = Path(f'portable/MeshLab-sources-{SHA[:12]}.tar.gz')
                with tarfile.open(archive) as tar:
                    names = tar.getnames()
                    self.assertIn('meshlab/src/vcglib/source.h', names)
                    self.assertIn('meshlab/dependency-sources/openaxis-src/LICENSE', names)
                    self.assertFalse(any(n.endswith('/.git') for n in names))
                env = dict(GITHUB_REPOSITORY=promote.REPO, GITHUB_REF='refs/heads/' + run()['head_branch'],
                           GITHUB_EVENT_NAME='push', GITHUB_RUN_ID='42', GITHUB_RUN_ATTEMPT='1')
                with patch.dict('os.environ', env), patch('build_record.git', side_effect=[SHA, 'submodule revision']):
                    build_record.record('windows-2022')
                record = json.loads(Path(f'provenance/build-windows-2022-{SHA[:12]}.json').read_text())
                self.assertEqual(len(record['plugins']), 75)
                self.assertEqual(record['packages'][archive.name], build_record.digest(archive))
                self.assertTrue(record['source_review_required'])
            finally:
                os.chdir(previous)


if __name__ == '__main__':
    unittest.main()
