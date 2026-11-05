"""Host launcher regression tests. Run: python3 -m unittest discover -s tests."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


@unittest.skipIf(os.name == 'nt', 'fake QEMU executable uses a POSIX shebang')
class LauncherTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.work = Path(cls.temp.name)
        cls.binary = cls.work / 'wrapper-lite-qemu'
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++11', '-O2',
                        str(Path(__file__).resolve().parents[1] / 'wrapper-lite-qemu.cpp'),
                        '-o', str(cls.binary)], check=True)
        (cls.work / 'qemu').mkdir()
        cls.fake = cls.work / 'fake-qemu'
        cls.fake.write_text('#!/usr/bin/env python3\nimport json,sys\nprint(json.dumps(sys.argv[1:]))\n')
        cls.fake.chmod(0o755)

    def run_launcher(self, *args, **env):
        child_env = os.environ.copy()
        for key in ('LITE_QEMU_TB_SIZE', 'LITE_QEMU_ACCEL', 'QEMU_BIN'):
            child_env.pop(key, None)
        child_env.update(env)
        return subprocess.run([str(self.binary), '--qemu-bin', str(self.fake), *args],
                              env=child_env, text=True, capture_output=True)

    def qemu_args(self, *args, **env):
        result = self.run_launcher(*args, **env)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def test_default_tcg_cache_is_bounded(self):
        args = self.qemu_args('--accel', 'tcg')
        self.assertEqual(args[args.index('-accel') + 1], 'tcg,tb-size=64')
        self.assertEqual(args[args.index('-m') + 1], os.environ.get('MEMORY', '512'))

    def test_environment_and_cli_precedence(self):
        args = self.qemu_args('--accel', 'tcg', LITE_QEMU_TB_SIZE='128')
        self.assertIn('tcg,tb-size=128', args)
        args = self.qemu_args('--accel', 'tcg', '--tb-size', '32', LITE_QEMU_TB_SIZE='128')
        self.assertIn('tcg,tb-size=32', args)
        guest_args = (self.work / 'qemu' / '.lite-qemu-args')
        self.assertFalse(guest_args.exists())  # removed after QEMU exits

    def test_hardware_accelerators_keep_their_arguments(self):
        for accel, expected in [('kvm', 'kvm'), ('hvf', 'hvf'),
                                ('whpx', 'whpx,kernel-irqchip=off')]:
            with self.subTest(accel=accel):
                args = self.qemu_args('--accel', accel, '--tb-size', '32')
                self.assertEqual(args[args.index('-accel') + 1], expected)
                self.assertNotIn('tcg,tb-size=32', args)

    def test_invalid_sizes_are_rejected_before_starting_qemu(self):
        for size in ('0', '-1', '4097', '1,thread=single', 'abc'):
            with self.subTest(size=size):
                result = self.run_launcher('--tb-size', size)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('1..4096', result.stderr)
                self.assertEqual(result.stdout, '')

    def test_missing_option_value(self):
        result = self.run_launcher('--tb-size')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('requires a value', result.stderr)


if __name__ == '__main__':
    unittest.main()
