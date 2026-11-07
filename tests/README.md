# Regression checks

After configuring the normal CMake build, run all host checks:

```bash
bash tests/run_checks.sh
```

The checks need a host C++ compiler, Python, OpenSSL and host libcurl. They cover
4,096 native context lifetimes, key changes, independent active owners, cache
detachment, exceptions, the 32-stream capacity and idle recovery; bounded binary
token data; long-password 2FA input; actual HTTP status/body-size options; and
certificate/hostname verification against local fixtures. The HTTP/TLS fixtures
use loopback sockets only and need no account or external service.

They first verify the pinned runtime hashes with `tests/verify_library_hashes.sh`.
Each C++ fixture has a 120-second timeout, including reentrant authentication
event tests (registration, state queries, nested attempts, handler exceptions,
presence flags, dialog events, code-provider ordering and final outcomes).

Two further fixtures exercise interaction v1: `auth_interaction.cpp` checks
correlation, invalid/stale/duplicate replies, constraints, sequential requests,
cancel/deadline/unregistration, dispatch exceptions, reentrant capture destruction
and the worker limit. `auth_sdk_interaction.cpp` uses SDK boundary mocks to check
explicit local/Retry choices, field maps, credentials/repeated codes, object
references, single-thread/exactly-once completion, failure latching and privacy.

In a sandbox that prevents LeakSanitizer from inspecting processes, run
`ASAN_OPTIONS=detect_leaks=0 bash tests/run_checks.sh`. AddressSanitizer,
UndefinedBehaviorSanitizer and the explicit owner-count assertions still run.

Run the host launcher tests:

```bash
python3 -m unittest discover -s tests -v
```

After CMake has fetched Dobby, exercise the real M3U8 functions against mock
library returns, including success, empty results, failures and exceptions:

```bash
g++ -std=c++11 -O1 -g -fsanitize=address,undefined \
  -fno-omit-frame-pointer -ffunction-sections -fdata-sections \
  -I. -Ibuild/_deps/dobby-src/include tests/playback_ownership.cpp \
  -Wl,--gc-sections -pthread -ldl -o /tmp/test-playback
/tmp/test-playback
```

The test checks that every owning reference, request and returned string is
released across 16,000 calls. Sandboxes that prevent LeakSanitizer from reading
process state can use `ASAN_OPTIONS=detect_leaks=0 /tmp/test-playback`; the
explicit lifetime checks and AddressSanitizer checks still run.

`library_abi.cpp` checks the actual bundled Android libraries rather than mocks.
Build it with the same NDK and libraries as lite, then include the executable
in a test guest and run it there. It needs no account or network:

```bash
"$ANDROID_NDK_PATH/toolchains/llvm/prebuilt/linux-x86_64/bin/x86_64-linux-android22-clang++" \
  -std=c++11 -O1 -ffunction-sections -fdata-sections \
  -I. -Ibuild/_deps/cjson-src tests/library_abi.cpp -Wl,--gc-sections \
  -Lrootfs/system/lib64 -landroidappmusic -lstoreservicescore \
  -lmediaplatform -lc++_shared -llog -lcurl -ldl -o library-abi
```

This verifies actual Android Data lengths and curl option constants, plus 10,000
cycles of HTTP message destruction, weak-reference expiry, returned-string
destruction and shared-pointer vector cleanup. It does not
measure authenticated batch-download memory or throughput. Rebuild the guest
initramfs with the updated lite binary to apply the service fixes; rebuilding
only the host launcher applies the TCG cache limit.

Authentication checks also cover separate Store/protocol/HTTP fields, safe
dialog selection, exact stdin/JSON/argument preservation, repeated code
challenges, failure categories and token-cache persistence failures. See
[the audit and AMDL handoff](../docs/authentication.md).

`auth_library_abi.cpp` is an additional NDK guest fixture. After building lite,
run `ANDROID_NDK_PATH=/path/to/ndk bash tests/build_auth_guest.sh` to build
`/tmp/wrapper-auth-library-abi` (an optional first argument changes that path).
Run it in a guest with a writable `/data` and no account. It constructs local dialogs/messages
and protocol responses; it never calls the authentication network request.

To run both Android ABI fixtures through the same path as CI, build them as
above and run:

```bash
python3 tests/run_guest_checks.py --kernel /path/to/vmlinuz \
  --auth-binary /tmp/wrapper-auth-library-abi --ownership-binary /tmp/library-abi
```

The runner needs static BusyBox (`--busybox` overrides `/bin/busybox`), cpio and
QEMU (`--qemu` / `--firmware-dir` are optional overrides). It creates a fresh
initramfs, excludes local account data, disables networking and persistent
disks, and fails on a fixture error, timeout or private sentinel in debug logs.
`offline-checks.yml` runs this plus host regressions on work/lite pushes and PRs;
guest logs are uploaded even on failure.

The authentication ABI fixture also uses the real SDK presentation interface,
provided completion bases, text-field response map, CredentialsRequest/Response
constructors and code challenges. It verifies UI-selected Retry and native
ownership without running AuthenticateFlow's network request. The guest remains
network-disabled, account-free and ephemeral.
