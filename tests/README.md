# Memory regression checks

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
