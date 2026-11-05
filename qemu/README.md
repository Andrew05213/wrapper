# wrapper-lite QEMU

Run wrapper-lite inside `qemu-system-x86_64`.

## Layout

```text
wrapper-lite-qemu.cpp       # launcher source (repository root)
wrapper-lite-qemu           # launcher binary
qemu/
├── vmlinuz-lite-qemu
├── lite-initramfs.cpio.gz
├── data.img
├── build.sh
├── mkdata.sh
├── init
└── README.md
```

## Build QEMU assets

```bash
apt-get download busybox-static
./qemu/build.sh
```

## Build launcher

```bash
c++ -std=c++11 -O2 -o wrapper-lite-qemu wrapper-lite-qemu.cpp
```

## Run

```bash
./wrapper-lite-qemu --help
./wrapper-lite-qemu --login user:pass --base-dir /data
./wrapper-lite-qemu
```

Launcher options such as `--memory`, `--smp`, `--accel` and `--tb-size` are
consumed by the launcher. Other arguments are forwarded line-by-line to
wrapper-lite inside the guest. See the main README for the full option list.

TCG's translation cache is limited to 64 MiB by default, independently of the
512 MB guest RAM setting. This prevents QEMU's automatic code cache (which can
reach 1 GiB) from growing as more tracks are processed. Use `--tb-size 128`
if a larger cache improves throughput on your host. This option has no effect
on KVM, HVF or WHPX.

## Acceleration

- Linux: auto-detect KVM.
- macOS: try HVF, fall back to TCG.
- Windows: try WHPX, fall back to TCG.

Force acceleration:

```bash
./wrapper-lite-qemu --accel kvm ...
./wrapper-lite-qemu --accel hvf ...
./wrapper-lite-qemu --accel whpx ...
./wrapper-lite-qemu --accel tcg ...
```

Windows WHPX uses:

```text
-accel whpx,kernel-irqchip=off
-cpu qemu64-v1
```

## Environment variables

| Variable | Default | Description |
|----------|---------|-------------|
| `HOST_PORT` | `12340` | host port |
| `GUEST_PORT` | `12340` | guest port |
| `MEMORY` | `512` | guest memory in MB |
| `SMP` | `2` | guest CPU count |
| `LITE_QEMU_TB_SIZE` | `64` | TCG translation cache limit in MiB (1–4096) |
| `LITE_QEMU_ACCEL` | auto | force acceleration |
| `QEMU_BIN` | auto | QEMU binary path |

## QEMU lookup order

1. `--qemu-bin` / `QEMU_BIN`
2. `qemu/bin/`
3. `PATH`
