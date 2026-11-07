# wrapper-lite

A lightweight single-port HTTP wrapper for Apple Music decryption.

It provides five endpoints on one HTTP port: M3U8, Key, Lyrics, License, and WebPlayback.

> **Recommended way to run**: use the `wrapper-lite-qemu` launcher — it boots a
> self-contained QEMU guest and forwards the HTTP API to the host, so you don't
> need a rooted device or a native chroot. See
> [Run with QEMU](#run-with-qemu-recommended).

## Build

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

Debug build:

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug
```

Build outputs:

- `rootfs/system/bin/lite` — Android-layer binary
- `wrapper-lite` — privileged host launcher
- `wrapper-lite-rootless` — rootless host launcher (user namespaces)

## Run natively

```bash
# Login, cache tokens, then exit
./wrapper-lite-rootless --login user:pass --code-from-file --base-dir /data

# Start the HTTP service
./wrapper-lite-rootless --base-dir /data --host 0.0.0.0 --port 12340
```

2FA code:

- Interactive prompt when a TTY is available.
- With `--code-from-file`, the code is read from `data/2fa.txt`.

## Run with QEMU (recommended)

`wrapper-lite-qemu` is the recommended launcher. It boots the prebuilt guest
(the `qemu/` assets) in QEMU and forwards the guest's HTTP service to the host,
so no rooted device or native chroot is required. Prebuilt releases already
bundle QEMU plus its firmware in `qemu/bin/` for Linux, macOS, Windows and
Android.

```bash
# one-time: build the launcher (or download a prebuilt release)
c++ -std=c++11 -O2 -o wrapper-lite-qemu wrapper-lite-qemu.cpp

# login first (forwards --login to the guest lite), then serve
./wrapper-lite-qemu --login user:pass --code-from-file
./wrapper-lite-qemu
```

The launcher locates the QEMU binary in this order:

1. `--qemu-bin <path>` / `QEMU_BIN`
2. `qemu/bin/` (bundled)
3. `PATH`

The bundled QEMU is preferred so that the firmware (`-L qemu/bin`), the QEMU
modules (`QEMU_MODULE_DIR`) and the support libraries (`qemu/lib`) always come
from the same package. `-L`, `LD_LIBRARY_PATH` and `QEMU_MODULE_DIR` are only
adjusted for the bundled QEMU; a system QEMU keeps its own libraries, modules
and firmware.

A package never ships glibc (`libc.so.6`, `libm.so.6`): the ELF interpreter is
always the host's and cannot be replaced through `LD_LIBRARY_PATH`, so the
loader and libc must stay a matched pair from the host. Only non-glibc support
libraries are bundled, in `qemu/lib/` (`qemu/bin/` on Android, where the Termux
packages place them next to the binary).

See [QEMU launcher arguments](#qemu-launcher-arguments) for the full option
list.

## Docker

```bash
docker build -t wrapper-lite:local .
docker run --privileged -p 12340:12340 \
  -v ./rootfs/data:/app/rootfs/data \
  -e USERNAME=... -e PASSWORD=... \
  wrapper-lite:local
```

## HTTP API

All responses use:

```json
{"code":0,"msg":"SUCCESS","data":{...}}
```

| Endpoint | Method | Parameters |
|----------|--------|------------|
| `/m3u8` | GET | `adamId` |
| `/key` | GET | `adamId`, `uri` (required; the prefetch `skd://itunes.apple.com/P000000000/s1/e1` is rejected unless `adamId=0`) |
| `/lyrics` | GET | `adamId`, optional `language`, optional `syllable` (`1`=word-timed syllable-lyrics with translations/romanization, `0`=line-timed lyrics converted in-wrapper **and still carrying translations/romanization as whole-line text**), optional `script` (transliteration script, default `en-Latn`) |
| `/webplayback` | GET | `adamId` |
| `/license` | POST | JSON: `adamId`, `challenge`, `uri`, optional `drm-type` (`wv`=Widevine (default), `pr`=PlayReady) |
| `/status` | GET | returns `regions` (list of storefront codes this wrapper can serve) |

## lite arguments

| Argument | Default | Description |
|----------|---------|-------------|
| `--login user:pass` | — | login, cache tokens, then exit |
| `--code-from-file` | off | read 2FA code from file |
| `--host` | `127.0.0.1` | listen address |
| `--port` | `12340` | listen port |
| `--base-dir` | `data` | data directory |
| `--device-info` | auto | override the entire device-info string (see [Device info](#device-info)) |
| `--proxy` | — | proxy URL |
| `--debug` | off | debug logging; TLS verification remains enabled |
| `--log-level` | `info` | `debug`, `info`, `warn`, `error` |
| `--log-file` | — | log file path |
| `--token-refresh-interval` | `1800` | background refresh interval in seconds |
| `--cors` | off | send CORS headers (`Access-Control-Allow-Origin: *`) and answer `OPTIONS` preflights so browser pages on any origin can call the API; the API has no authentication, so only enable it on instances you trust to be reachable |

## Device info

`lite` identifies itself to Apple's servers with a device-info string of the
form:

```
ClientIdentifier/VersionIdentifier/PlatformIdentifier/ProductVersion/DeviceModel/BuildVersion/LocaleIdentifier/LanguageIdentifier/AndroidID
```

Example: `Music/5.0.2/Android/10/Pixel 8/7663314/en-US/en-US/<android-id>`.

The trailing `AndroidID` is derived automatically:

- During `--login`, it is the FNV-1a 64-bit hash of the account username and is
  persisted to `<base-dir>/ANDROID_ID`, so each account keeps a stable device
  identity across runs.
- In service mode (no username), the persisted `ANDROID_ID` is reused; if none
  exists, a built-in default is used.

Pass `--device-info <string>` to override the entire string explicitly.

## QEMU launcher arguments

| Argument | Default | Description |
|----------|---------|-------------|
| `--host <addr>` | `127.0.0.1` | host address the forwarded port binds to (`0.0.0.0` to expose) |
| `--host-port <port>` | `12340` | host port |
| `--guest-port <port>` | `12340` | guest port |
| `--guest-host <addr>` | `0.0.0.0` | address the guest lite listens on (must be reachable by QEMU forwarding) |
| `--memory <MB>` | `512` | guest memory in MB |
| `--tb-size <MiB>` | `64` | TCG translation cache limit (1–4096); used only with software emulation |
| `--smp <N>` | `2` | guest CPU count |
| `--accel <accel>` | auto | force acceleration (`kvm`, `hvf`, `whpx`, `tcg`) |
| `--qemu-bin <path>` | auto | QEMU binary path |

Environment variables (fallbacks; command-line flags take precedence):

| Variable | Default | Description |
|----------|---------|-------------|
| `LITE_QEMU_HOST` | `127.0.0.1` | host address the forwarded port binds to |
| `HOST_PORT` | `12340` | host port |
| `GUEST_PORT` | `12340` | guest port |
| `LITE_GUEST_HOST` | `0.0.0.0` | address the guest lite listens on |
| `MEMORY` | `512` | guest memory in MB |
| `SMP` | `2` | guest CPU count |
| `LITE_QEMU_TB_SIZE` | `64` | TCG translation cache limit in MiB |
| `LITE_QEMU_ACCEL` | auto | force acceleration |
| `QEMU_BIN` | auto | QEMU binary path |

### QEMU memory use

`--memory` controls guest RAM, not the total memory used by the host QEMU
process. With TCG software emulation, QEMU also stores translated machine code.
Its automatic cache size can reach 1 GiB on larger hosts and fill progressively
as more tracks are processed. The launcher limits that cache to 64 MiB by
default, including when hardware acceleration falls back to TCG.

```bash
./wrapper-lite-qemu --memory 512 --tb-size 64
# If repeated translation slows down your workload, try a larger cache:
./wrapper-lite-qemu --tb-size 128
```

A smaller cache can cause more frequent translation work. These limits do not
cap all QEMU allocations; guest RAM, translated-code metadata, devices and
shared libraries also contribute to host memory use. KVM/HVF/WHPX do not use
the TCG cache setting.

## Build notes

- Release and Debug builds verify TLS certificates and hostnames. `--debug`
  only changes logging verbosity.
- The QEMU build includes a CA bundle at `/usr/local/android/cert.pem`. Set
  `SSL_CERT_FILE` to supply a different bundle when building or running natively.

## AMDL integration

See [authentication diagnostics and AMDL handoff](docs/authentication.md) for
separate Store/protocol/HTTP error fields, safe dialog handling, stdin input,
the verification-code provider API, offline checks and current limitations.

The Android binary now provides `native_decrypt(stream, adamId, uri, input,
output)`, `native_decrypt_close(stream)` and `native_decrypt_close_all()` in
`lite/lite.h`. These are C++ entry points for AMDL's private HTTP adapter;
the public HTTP endpoints and authentication model are unchanged.

Each native stream owns its FairPlay context. The controller's extra cache
references are detached while active streams keep their own references. Closing
or changing a key releases that stream's context; it does not release another
active stream's owner. Failed operations also release their owners. At most 32
streams can be active, and the service collects streams idle for 120 seconds,
including streams abandoned when a client exits or its DELETE request is lost.

When switching AMDL's locked wrapper input to this branch:

- Remove the `NativeStream` map and `native_decrypt`/`native_decrypt_close`
  implementations appended by the old `lite-session.patch`. Keep AMDL's private
  routes and call the implementations supplied here.
- Rebase the native login, TLS/DNS and store-binding patches against this source.
  This branch changes authentication input handling, curl setup and token parsing;
  the patches against `2fd7e0e` are not drop-in patches for this revision.
- Pin the new wrapper commit and archive hash, then rebuild AMDL's guest and its
  bundle manifest through AMDL's own runtime build. The generic QEMU guest from
  this repository does not contain AMDL's guest agent or private session protocol.
- AMDL launches QEMU directly from its Go runtime. Apply the TCG cache limit to
  that launcher's `-accel` argument as well; changing `wrapper-lite-qemu` affects
  only this repository's launcher.

AMDL-side cleanup retries and download memory buffering are separate changes in
the AMDL repository. In particular, its per-track `max-memory-limit` is not a
shared memory budget across parallel downloads.

## License

This project is licensed under the [MIT License](LICENSE).
