"""Run both Android ABI fixtures in an ephemeral guest with no NIC or data disk."""
import argparse
import gzip
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kernel', type=Path, required=True)
    parser.add_argument('--auth-binary', type=Path, required=True)
    parser.add_argument('--ownership-binary', type=Path, required=True)
    parser.add_argument('--busybox', type=Path, default=Path('/bin/busybox'))
    parser.add_argument('--qemu', default='qemu-system-x86_64')
    parser.add_argument('--firmware-dir', type=Path)
    parser.add_argument('--log', type=Path, default=Path('guest-checks.log'))
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    subprocess.run(['bash', str(repo / 'tests/verify_library_hashes.sh')], check=True)
    with tempfile.TemporaryDirectory(prefix='wrapper-guest-checks-') as directory:
        work = Path(directory)
        stage = work / 'root'
        # Never copy rootfs/data: a developer checkout can contain real accounts.
        shutil.copytree(repo / 'rootfs', stage,
                        ignore=shutil.ignore_patterns('data', 'dev', 'proc', 'sys'))
        (stage / 'bin').mkdir(exist_ok=True)
        shutil.copy2(args.busybox, stage / 'bin/busybox')
        for binary, name in ((args.auth_binary, 'auth-library-abi'),
                             (args.ownership_binary, 'library-abi')):
            target = stage / 'system/bin' / name
            shutil.copy2(binary, target)
            target.chmod(0o755)
        (stage / 'system/bin/linker64').chmod(0o755)
        init = stage / 'init'
        init.write_text('''#!/bin/busybox sh
/bin/busybox mount -t proc proc /proc
/bin/busybox mount -t sysfs sysfs /sys
/bin/busybox mount -t devtmpfs devtmpfs /dev
/bin/busybox mkdir -p /data /dev/pts
/bin/busybox mount -t devpts devpts /dev/pts
/system/bin/library-abi
echo OWNERSHIP_EXIT=$?
/system/bin/auth-library-abi
echo AUTH_EXIT=$?
/bin/busybox poweroff -f
''')
        init.chmod(0o755)
        for name in ('proc', 'sys', 'dev'):
            (stage / name).mkdir(exist_ok=True)
        entries = [b'.'] + [os.fsencode(str(p.relative_to(stage))) for p in stage.rglob('*')]
        archive = subprocess.run(['cpio', '--null', '-o', '--format=newc', '--quiet'],
                                 cwd=stage, input=b'\0'.join(entries) + b'\0',
                                 stdout=subprocess.PIPE, check=True).stdout
        initrd = work / 'checks.cpio.gz'
        initrd.write_bytes(gzip.compress(archive, compresslevel=1, mtime=0))
        command = [args.qemu, '-accel', 'tcg,tb-size=64', '-cpu', 'max', '-m', '512', '-smp', '2',
                   '-kernel', str(args.kernel.resolve()), '-initrd', str(initrd),
                   '-append', 'console=ttyS0 quiet panic=-1', '-display', 'none',
                   '-serial', 'stdio', '-nic', 'none', '-no-reboot']
        if args.firmware_dir:
            command += ['-L', str(args.firmware_dir.resolve())]
        try:
            with args.log.open('wb') as output:
                result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, timeout=120)
        except subprocess.TimeoutExpired:
            raise SystemExit('Guest ABI checks timed out; see the guest log')
        log = args.log.read_text(errors='replace')
        if result.returncode or any(marker not in log for marker in (
                '10000 Android ABI ownership cycles passed', 'AUTH_ABI_PASSED', 'NATIVE_UI_ABI_PASSED',
                'OWNERSHIP_EXIT=0', 'AUTH_EXIT=0')):
            raise SystemExit('Guest ABI checks failed; see the guest log')
        if any(secret in log for secret in ('SECRET', 'fixture@example.invalid', '123456', '987654')):
            raise SystemExit('Guest debug logs exposed a private fixture value')
        print('Android ownership/authentication ABI and debug-log privacy checks passed')


if __name__ == '__main__':
    main()
