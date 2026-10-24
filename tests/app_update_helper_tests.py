"""Exercise the actual macOS replacement/rollback helper on disposable bundles."""
import json
import os
from pathlib import Path
import shlex
import signal
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/update_macos.sh'


@unittest.skipUnless(sys.platform == 'darwin', 'macOS ditto required')
class UpdateHelperTests(unittest.TestCase):
    def fixture(self, root, *, failing=False):
        token = 'a' * 32
        target = root / "安装目录 ' $/XAvatar.app"
        work = root / 'cache/app-updates' / token
        stage = work / 'staged/Moss Studio.app'
        old = target / 'Contents/MacOS/MossStudio'
        new = stage / 'Contents/MacOS/MossStudio'
        old.parent.mkdir(parents=True)
        new.parent.mkdir(parents=True)
        old.write_text('#!/bin/sh\ntouch ' + shlex.quote(str(root / 'old-restarted')) + '\n')
        old.chmod(0o755)
        if failing:
            new.write_text('#!/bin/sh\nexit 9\n')
        else:
            # The real app writes this acknowledgement after its QML root opens.
            new.write_text('#!/bin/sh\nprintf \'{"pid":%s,"token":"%s"}\' "$$" "$2" > '
                           + shlex.quote(str(work / 'started.json')) + '\nexec /bin/sleep 60\n')
        new.chmod(0o755)
        (target / 'old-only.txt').write_text('old distribution file')
        (stage / 'new-only.txt').write_text('new distribution file')
        (root / 'projects').mkdir()
        (root / 'projects/user-work.txt').write_text('keep my work')
        (work / 'package.zip').write_bytes(b'download')
        (work / 'pending.json').write_text(json.dumps({'token': token, 'target': str(target)}))
        return target, work, stage, token

    def run_helper(self, target, work, stage, token):
        parent = subprocess.Popen(['/bin/sleep', '0.2'])
        try:
            # Reap the mock old app; kill -0 must observe its actual exit.
            parent.wait(timeout=2)
            return subprocess.run(['/bin/sh', str(SCRIPT), str(target), str(stage), str(work),
                                   str(parent.pid), token], capture_output=True, text=True, timeout=20)
        finally:
            if parent.poll() is None:
                parent.terminate()
                parent.wait()

    def test_success_restarts_and_removes_old_app_and_download(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            target, work, stage, token = self.fixture(root)
            # Save the launched PID outside the cleaned transaction for teardown.
            new = stage / 'Contents/MacOS/MossStudio'
            new.write_text(new.read_text().replace('exec /bin/sleep',
                'echo "$$" > ' + shlex.quote(str(root / 'new-pid')) + '\nexec /bin/sleep'))
            result = self.run_helper(target, work, stage, token)
            try:
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertTrue((target / 'new-only.txt').exists())
                self.assertFalse((target / 'old-only.txt').exists())
                self.assertFalse(work.exists())
                self.assertFalse(Path(str(target) + '.previous-' + token).exists())
                self.assertFalse(Path(str(target) + '.incoming-' + token).exists())
                self.assertEqual((root / 'projects/user-work.txt').read_text(), 'keep my work')
            finally:
                if (root / 'new-pid').exists():
                    os.kill(int((root / 'new-pid').read_text()), signal.SIGTERM)

    def test_failed_launch_restores_previous_app(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            target, work, stage, token = self.fixture(root, failing=True)
            result = self.run_helper(target, work, stage, token)
            self.assertEqual(result.returncode, 7, result.stderr)
            self.assertTrue((target / 'old-only.txt').exists())
            self.assertFalse((target / 'new-only.txt').exists())
            self.assertFalse(work.exists())
            self.assertEqual((root / 'projects/user-work.txt').read_text(), 'keep my work')

    def test_mismatched_target_never_changes_installation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            target, work, stage, token = self.fixture(root)
            (work / 'pending.json').write_text(json.dumps({'token': token, 'target': '/other.app'}))
            result = self.run_helper(target, work, stage, token)
            self.assertEqual(result.returncode, 2)
            self.assertTrue((target / 'old-only.txt').exists())


if __name__ == '__main__':
    unittest.main()
