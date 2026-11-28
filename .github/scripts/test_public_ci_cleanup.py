import os
from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest


class PublicCiCleanupTest(unittest.TestCase):
    def test_transient_delete_error_is_retried_and_persistent_error_fails(self):
        workflow = Path('.github/workflows/public-ci-cleanup.yml')
        if not workflow.exists():
            workflow = Path('.github/public-ci/workflows/public-ci-cleanup.yml')
        script = textwrap.dedent(workflow.read_text().split('        run: |\n')[1].split('\n      - name:', 1)[0])
        for failures, expected in [(1, 0), (3, 1)]:
            with self.subTest(failures=failures), tempfile.TemporaryDirectory() as tmp:
                fake = Path(tmp) / 'gh'
                fake.write_text('#!/bin/bash\n'
                                'if [[ "$*" != *"-X DELETE"* ]]; then\n'
                                '  echo "123 ci/pr-1 old 2000-01-01T00:00:00Z"; exit 0\n'
                                'fi\n'
                                'n=0; [[ -f count ]] && n=$(<count); n=$((n+1)); echo "$n" > count\n'
                                '[[ $n -gt $FAILURES ]]\n')
                fake.chmod(0o755)
                sleep = Path(tmp) / 'sleep'
                sleep.write_text('#!/bin/bash\nexit 0\n')
                sleep.chmod(0o755)
                env = dict(os.environ, PATH=tmp + os.pathsep + os.environ['PATH'],
                           REPO='example/repo', BRANCH='ci/pr-1', CURRENT='current',
                           FAILURES=str(failures))
                result = subprocess.run(['bash', '-c', script], cwd=tmp, env=env,
                                        capture_output=True, text=True)
                self.assertEqual(expected, result.returncode, result.stderr)
                self.assertEqual(min(failures + 1, 3), int((Path(tmp) / 'count').read_text()))

    def test_only_old_deployments_are_made_inactive_then_deleted(self):
        workflow = Path('.github/workflows/public-ci-cleanup.yml')
        if not workflow.exists():
            workflow = Path('.github/public-ci/workflows/public-ci-cleanup.yml')
        script = textwrap.dedent(workflow.read_text().split('        run: |\n')[2])
        with tempfile.TemporaryDirectory() as tmp:
            fake = Path(tmp) / 'gh'
            fake.write_text('#!/bin/bash\n'
                            'echo "$*" >> calls\n'
                            '[[ "$*" == *"-X "* ]] || echo 7\n')
            fake.chmod(0o755)
            env = dict(os.environ, PATH=tmp + os.pathsep + os.environ['PATH'], REPO='example/repo')
            result = subprocess.run(['bash', '-c', script], cwd=tmp, env=env,
                                    capture_output=True, text=True)
            self.assertEqual(0, result.returncode, result.stderr)
            calls = (Path(tmp) / 'calls').read_text().splitlines()
        self.assertIn('environment=garden-tools-ci', calls[0])
        self.assertIn('&page=1 ', calls[1])
        self.assertEqual(['api -X POST repos/example/repo/deployments/7/statuses -f state=inactive',
                          'api -X DELETE repos/example/repo/deployments/7'], calls[2:])


if __name__ == '__main__':
    unittest.main()
