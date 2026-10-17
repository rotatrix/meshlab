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
        self.assertEqual(len(names), 5)
        self.assertFalse(any(n.startswith('build-') for n in names))
        self.assertFalse(any('installer' in n or 'double' in n for n in names))
        self.assertFalse(any('linux-arm64' in n or 'sources-' in n for n in names))
        self.assertTrue(all(len(n) < 65 and '/' not in n for n in names))

    def test_prepare_downloads_exact_packages_and_rejects_tampering(self):
        payloads = {name: name.encode() for name in promote.expected(SHA)}
        artifacts = [dict(id=i, name=name, expired=False,
                          workflow_run=dict(id=42, head_sha=SHA),
                          digest='sha256:' + hashlib.sha256(data).hexdigest())
                     for i, (name, data) in enumerate(payloads.items(), 1)]
        jobs = [dict(name=f'MeshLab {p} single_precision', conclusion='success') for p in promote.PLATFORMS]
        for tamper in (False, True):
            def download(command, stdout, **kwargs):
                artifact_id = int(command[-1].split('/')[-2])
                stdout.write(payloads[artifacts[artifact_id - 1]['name']] + (b'changed' if tamper else b''))
            with tempfile.TemporaryDirectory() as root, \
                    patch('promote.require_new_release'), patch('promote.tag_commit', return_value=SHA), \
                    patch('promote.api', side_effect=[{'status': 'identical'}, run()]), \
                    patch('promote.pages', side_effect=[jobs, artifacts]), \
                    patch('promote.subprocess.run', side_effect=download):
                if tamper:
                    with self.assertRaisesRegex(ValueError, 'digest mismatch'):
                        promote.prepare('MeshLab-2025.07-rotatrix.1', 42, Path(root))
                else:
                    result = promote.prepare('MeshLab-2025.07-rotatrix.1', 42, Path(root))
                    self.assertEqual(result['run_id'], 42)
                    self.assertEqual(len(result['artifacts']), 5)

    def test_existing_draft_or_public_release_is_rejected(self):
        for draft in (True, False):
            with patch('promote.subprocess.check_output', return_value=json.dumps(
                    [[dict(tag_name='tag', draft=draft)]])), self.assertRaisesRegex(ValueError, 'exists'):
                promote.require_new_release('tag')

    def test_draft_contains_only_five_downloads_and_checksums(self):
        with tempfile.TemporaryDirectory() as root:
            directory = Path(root)
            records = []
            for name in promote.expected(SHA):
                path = directory / name
                path.write_bytes(b'app')
                records.append(dict(name=name, sha256=promote.digest(path)))
            (directory / 'signed').mkdir()
            for arch in ('arm64', 'x64'):
                (directory / 'signed' / f'signed-{arch}.tar.gz').write_bytes(b'signed')
            plan = dict(repository=promote.REPO, tag='MeshLab-2025.07-rotatrix.1-beta.1',
                        commit=SHA, run_id=42, artifacts=records, prerelease=True)
            (directory / 'promotion.json').write_text(json.dumps(plan))
            with patch('promote.tag_commit', return_value=SHA), patch('promote.require_new_release'), \
                    patch('promote.subprocess.run') as command:
                promote.draft(directory)
                args = command.call_args.args[0]
                self.assertIn('--draft', args)
                self.assertIn('--verify-tag', args)
                self.assertIn('--prerelease', args)
                self.assertEqual(len((directory / 'SHA256SUMS').read_text().splitlines()), 5)
                self.assertFalse(any('macos-app-' in arg or arg.endswith('.json') for arg in args))
            with patch('promote.tag_commit', return_value='b' * 40), self.assertRaisesRegex(ValueError, 'Tag moved'):
                promote.draft(directory)

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


if __name__ == '__main__':
    unittest.main()
