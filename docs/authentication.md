# Lite authentication diagnostics and AMDL handoff

This change is based on `work` at `15fe0cb75b866e7460bf8d28d155e813a870e076`.
It fixes local credential handling and response classification. It does not
establish why Apple's server rejected a particular account, and does not claim
that successful Windows/web authentication guarantees this Android Store flow
will accept the same exchange.

## Diagnostic fields

All authentication log values are numeric; event names come from a fixed list.
Library log text, server messages, dialog titles/buttons, credentials, codes,
tokens, cookies and response bodies are not forwarded, including in debug mode.

| Event field | Source | Meaning |
| --- | --- | --- |
| `auth_store_error` | `StoreErrorCondition::errorCode()` | Store's internal error enum; e.g. `10` |
| `auth_failure_type` | `StoreErrorCondition::externalErrorCode()` | Store protocol `failureType`; e.g. `-5000` |
| `auth_protocol_error` | URLResponse protocol dictionary's **`errorCode`** | Independent protocol value; e.g. `-22947` |
| `auth_response_type` | `AuthenticateResponse::responseType()` | Authentication response enum |
| `auth_status` | `AuthenticateResponse::status()` | Authentication status enum, returned by reference |
| `auth_http_status` | Underlying `mediaplatform::HTTPResponse` | Actual HTTP response status |

Missing protocol/HTTP values produce `auth_protocol_error_unavailable` or
`auth_http_status_unavailable`. Missing is not logged as zero or inferred from
an authentication enum. Responses generated locally may have no HTTP response.
The observation hook runs after the original AuthenticateResponse constructor;
it does not change the request or response. Hook installation failure produces
`auth_response_observer_unavailable`.

The bundled library maps `failureType=-5000` to response type `1` only when the
separate protocol `errorCode` is `-22947`; otherwise it maps to `0`. In both
offline fixtures the authentication `status` was `6` and HTTP status was `200`.
Therefore neither `-5000`, nor `status=6`, nor HTTP 200 alone means success or a
verification challenge. A generic rejection is `auth_rejected`, without making
an incorrect-password diagnosis. An observed type `1` with both matching
protocol codes is `auth_verification_required`.

Other final classifications are `auth_authenticated`, `auth_cancelled`,
`auth_unsupported_dialog`, `auth_invalid_credentials`,
`auth_invalid_verification_code`, `auth_transport_failed`,
`auth_missing_response`, `auth_unexpected_response` and `auth_internal_error`.
`last_auth_outcome()` exposes the result to a C++ adapter. `login()` remains a
boolean API for compatibility. An unsupported/failed callback overrides a later
native success response. A native success combined with an HTTP rejection or
Store/protocol error is a failure.

`auth_authenticated` describes the Store authentication exchange. The CLI emits
`login_complete` only after the child succeeds, usable token/region files are
read, and the token cache is successfully saved. Token persistence failure is
`login_token_cache_failed` and a nonzero exit. Callers must wait for process
completion rather than infer login completion from an intermediate event.

## Dialog and code handling

The local initial dialog in this library is kind `2`, has no input fields, and
has three actionless buttons in this order: existing account, create account,
cancel. The native callback maps the first button to existing-account result
`2`. The guest ABI test exercises this callback, without starting authentication.
This signature works with localized titles. An account-choice dialog with one
unambiguous `authenticate` action is also supported. Only dialogs from the
identified native `_resolveEmptyAccount` flow can be selected automatically.
Failure type and input
fields must allow account selection; other dialogs are left unselected and
default actions are disabled. No Retry, terms, URL or validation dialog is
automatically accepted. A type `0` response's subsequent Cancel/Retry prompt is
recorded as `dialog_auth_failure_prompt` and remains a rejection.

The bundled default ProtocolDialog constructor does not initialize its
`failureType` member. Hooks identify the native account-choice and failure-prompt
flows; their dialog callbacks initialize this field to zero. Server-supplied
protocol dialogs retain their original values. This
prevents stale allocation contents from influencing classification or logs.
Hook failure is `auth_dialog_classifier_unavailable` or
`auth_failure_prompt_classifier_unavailable`. The host fixture starts the
field with poisoned bytes; the native fixture exercises the real local dialog
and Cancel/Retry callbacks, verifying that the latter returns false without
retrying. The default constructor itself is not intercepted.

`amPassword` now always holds the original input. Each code challenge creates a
temporary `original_password + current_code` for that one CredentialsResponse.
Invalid/missing credentials or codes submit cancellation, not incomplete
credentials. Temporary code/password buffers are wiped after the native
setters copy them. Repeated challenges and ordinary retries cannot accumulate
old codes. Response and flow owning references are released after use.

For downstream stdin transports, register
`set_verification_code_provider(bool (*)(std::string&))` before `login()`.
Return a fresh six-digit code, or false to cancel. The provider must not modify
`amPassword`, terminate the process, or log its input.

## Input and request audit

* `--login user:password` splits only the first colon. Password bytes are not
  trimmed or escaped. `LITE_ARGS_FILE` is **LF-delimited**, one argument per
  line, with whitespace preserved. NUL is rejected instead of removed. Use stdin
  JSON for passwords containing line breaks; CRLF argument files are not the
  transport format.
* `--login-stdin` reads a JSON line with `appleEmail` and `applePassword`, and
  subsequent `{"code":"123456"}` lines on code challenges. JSON is decoded
  once. Limits are 512 UTF-8 bytes for the account, 4096 for the password and
  32768 for the JSON record. Truncated records, duplicate fields, escaped NUL
  and trailing data are rejected. This generic transport accepts only those
  credential fields: downstream `storeBinding` is rejected rather than silently
  discarded. AMDL should retain its own binding-aware transport.
* The native AuthenticateRequest obtains the `authenticateAccount` URL from
  its URL bag and builds an XML plist POST. The guest fixture passes a synthetic
  URL directly to `_makeMessage`, then decodes the generated plist to compare
  the exact account and special/4096-byte passwords, attempt numbers `0`/`1`,
  method and URL. It never sends this fixture to Apple.
* Native AuthenticateFlow initializes the attempt counter to zero and sets it
  on each AuthenticateRequest. RequestContext creates and retains its cookie
  storage; wrapper does not clear/replace it between challenges. The guest
  fixture verifies that requests retain the same cookie-storage object.
* RequestContext builds its User-Agent from configured device information. The
  fixture verifies Music/5.0.2, Android/10 and Pixel 8, and compares each request's
  GUID with RequestContext's device identifier. Account-derived/persisted
  Android IDs and explicit `--device-info` values are retained. No IDs are
  randomly changed to try to pass authentication.
* URLRequest's native send path calls `_adjustedRequest`,
  `_applyMachineDataHeaders`, and the native redirection handler. Wrapper does
  not replace this authentication transport with its separate CurlEasy web API
  client. Redirect behavior and acceptance of real device-authentication
  material have been reviewed statically, **not tested against a live Store**.
* The later `createMusicToken` request previously sent `g_base_dir` as
  `X-Apple-Store-Front` and a hardcoded Music/4.9 Samsung device string as
  `X-Apple-Requesting-Bundle-Version`. It now uses the fetched account storefront
  and current RequestContext User-Agent. These were separate token-exchange
  bugs; fixing them does not explain an earlier `-5000` authentication rejection.
* Debug builds and `--debug` now keep certificate and hostname verification
  enabled. `--debug` only changes logging. Existing CA-bundle support remains.

AMDL's current `native_login.h` / `lite-session.patch` were compared read-only.
That adapter decodes JSON once and preserves ordinary non-NUL password bytes,
but its old credential callback still appends codes to `amPassword`. Rebase it
to the code-provider API above instead of restoring that hunk. Retain AMDL's
private events and its binding-aware credential parser. Its current
`native_store_bridge.h` adds an identity token and an allowlisted set of device
authentication headers to the native authenticate endpoint. This repository
does not manufacture those values. Preserve that bridge and its endpoint
restrictions when repinning; do not replace binding-aware login with the
generic stdin mode. A local format/initialization check cannot establish that
a supplied PET/device-authentication tuple is valid for Apple's Store service.

No AMDL source files, manifests or locks were changed by this work.

## Verification and ABI boundary

Run host checks with `ASAN_OPTIONS=detect_leaks=0 bash tests/run_checks.sh` when
the sandbox prevents LeakSanitizer inspection. ASan/UBSan still run. Fixtures
cover protocol-code distinctions, reference-return ABI mocks, actual HTTP
status separation, unknown/non-English dialogs, malformed input, special and
long passwords, repeated challenges, cancellation, exceptions, false success,
and token cache write failure. Existing memory/TLS/QEMU launcher checks remain.

Build with `ANDROID_NDK_PATH=/path/to/ndk bash tests/build_auth_guest.sh` after
building lite. `tests/auth_library_abi.cpp` runs offline inside QEMU with the real
Android libraries, including the installed constructor observation hook. It
verifies the local native dialog callback, real generated request plist,
stable GUID/cookie storage, configured User-Agent, both native `-5000` mappings,
and actual HTTP status extraction. The HTTPResponse status member is at +128;
this is pinned to the included x86_64 library, not a general C++ ABI contract.

The library hashes used for this audit are:

* `libstoreservicescore.so`: `6238261363d7943f1a0a27116b52544831f3e295e8533a634641fe5b09d92c7e`
* `libmediaplatform.so`: `96ec8f0a9bdd597534fb876d1945f153077d688826dcc7673807e113280d5203`

Revalidate enum values, object layouts, reference-return getters, local dialog
ordering and the observer when updating these libraries. Rebuild the Android
binary and the downstream guest bundle after repinning. Neither source changes
alone nor rebuilding only the QEMU launcher updates an installed desktop guest.
