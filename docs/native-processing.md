# Lite native audio processing

This prototype starts from `work` at `a185b9cab2ec40f98672940cbd3ef46e8cdfe531`. It carries forward the stream diagnostics from `cf49336`. That commit's host-only candidate took one lock for a whole batch and raised median playback P95 by 34%. The new adapter packages several blocks into one HTTP exchange. Each block still has its own turn and SDK call. [Previous measurement](https://github.com/Andrew05213/wrapper/pull/1)

## Private capability discovery

Standalone Lite keeps batch transport disabled. A private service embedder can call `native_batch::register_http_routes(server, enabled, authorize)` from `lite/native_batch_http.h` before starting media workers. It must opt in and supply its gateway and current-session authorization check. The adapter requires IPv4 loopback at both ends. The callback receives only the request metadata before reading the body. It runs at preflight and before queuing each block, outside the playback locks. It must reject requests outside the private service, expired or revoked session grants and requests from a different session. Embedders should keep the per-block check local and cheap.

A permitted `GET /native/decrypt/batch` returns `200` with `{"version":1,"maxBlocks":4,"maxBytes":65536}`. Probe capability on the authenticated route before sending any batch data. An enabled C++ embedding can use the same processing entry in `lite/native_batch.h`. Its input buffers and callbacks live through the synchronous call and it returns owning output strings. Activation is a startup setting for the lifetime of the service process.

## Request and response v1

Send `POST /native/decrypt/batch?adamId=<id>&uri=<URL-encoded-context-URI>` with the existing `X-AMDL-Stream` header (32 lowercase hex characters) and `Content-Type: application/vnd.wrapper.native-batch`. Use a single fixed `Content-Length`. The request body must fit in 65,562 bytes. Keep playback on the existing single-block route. The batch endpoint rejects `X-AMDL-Playback` on a POST.

The request body uses unsigned big-endian integers, followed by binary data.

| Field | Size | Value |
| --- | ---: | --- |
| Version | 1 byte | `1` |
| Block count | 1 byte | `1..4` |
| First block | 8 bytes | Zero-based ordinal in this live stream |
| Length and bytes, repeated for each block | 4 bytes + length | Positive length divisible by 16, then exactly that many bytes |

The sum of block lengths is at most 65,536 bytes. The server validates the entire frame before execution, including exact framing, input sizes and ordinal overflow. Blocks in an envelope are consecutive in array order and share one stream, asset and context URI. Use a fresh stream ID at a context boundary, or use a successful legacy single-block call to switch the context before sending a later batch. While a stream lives, every successful single block also advances its ordinal by one. A valid single block larger than 64 KiB can still use the existing 1 MiB limit and counts as one ordinal. Send at most one outstanding processing request per stream. A batch reserves its stream between blocks, so a concurrent request cannot change its context or interleave blocks. Close and expiry invalidate the reservation.

A framed response uses HTTP `200`, the same binary content type and `Cache-Control: no-store`. The body is at most 65,564 bytes. Check the entire response frame before using any outputs. Validate the version, echoed ordinal and count, the status-specific prefix count, the per-block output lengths against the corresponding inputs, and absence of trailing bytes.

| Field | Size | Value |
| --- | ---: | --- |
| Version | 1 byte | `1` |
| Status | 1 byte | See table below |
| Block count | 1 byte | Echoes the request |
| Completed prefix | 1 byte | Number of complete outputs, in order |
| First block | 8 bytes | Echoes the request |
| Length and output, repeated for the completed prefix | 4 bytes + length | Length and bytes of that output |

The failed position, when present, is `first block + completed prefix`. Each completed output has the original block length. The bridge uses the existing SDK operation and existing definition of a successful block call.

## Failures, cancellation and retries

| Result | What may have run | Caller action |
| --- | --- | --- |
| Discovery indicates unavailable or a version the caller cannot use | No batch was sent | Use singles |
| HTTP 400 with `native_batch_invalid` from Lite | The entire batch was rejected before processing | Singles are safe for valid legacy blocks if the stream is current. Fix malformed metadata before retrying |
| HTTP 401 with `native_batch_unauthorized` or trusted guest session rejection before dispatch | Nothing in this request was processed | Apply existing session handling and revalidate before subsequent use |
| `0` Complete | All blocks. The prefix count equals the request count | Consume the outputs and advance by the count |
| `1` Cancelled | Only the indicated prefix. The suffix was unstarted | Stop the task or close the stream after keeping any usable prefix |
| `2` Conflict | Zero in this request. The stream, context or ordinal did not allow admission. A previous request may have advanced it | Resolve stream state or start a new stream at a known restart boundary |
| `3` BlockFailed | The indicated prefix completed. The next block failed and may have advanced the backend. Later blocks were unstarted | Stop, close the stream and handle the failure. No replay of the failed block or whole batch |
| `4` Unauthorized | The indicated prefix completed before the between-block authorization check failed. The suffix was unstarted | Apply existing session handling. Stop and close this stream |
| `5` Busy | Zero. The stream limit prevented admission | Apply backpressure without advancing the ordinal |
| Lost reply, timeout after dispatch, malformed/truncated reply or generic proxy error | Unknown, up to the entire batch | Treat the stream as indeterminate. Close it and recover from an independently known boundary with a fresh stream ID |

Only a confirmed pre-execution rejection can permit a switch to the legacy interface for that request. A zero prefix alone is insufficient, including on `BlockFailed` and on a conflict after a prior lost reply. The v1 protocol has no replay journal or persistent response cache. The live stream ordinal rejects stale frames while their entry exists. Close, expiry, SDK failure and service restart can remove the entry. Use a new ID for each new stream and avoid automatic retries or HTTP redirects after dispatch.

Cancellation is checked before enqueue and again just after admission. It is also checked before every successor block is queued. A committed SDK call finishes under the same SDK lock as a legacy block. The C++ entry accepts a nonblocking cancellation probe. HTTP uses peer disconnect as a best-effort cancellation signal. If a known prefix exists when a stop is reported, the stream still reflects that prefix unless the context was closed or failed. A cleanup request using the existing close route can invalidate an active batch before its next turn. A lost reply stays indeterminate even if the connection close triggered cancellation.

## Playback scheduling and downloader integration

Opting in enables FIFO admission to the existing SDK mutex for Lite media operations. Each batch block joins the tail for its next turn. A playback operation already queued in Lite runs before that succeeding block. A queued download also advances as earlier turns finish. The mutex still covers context acquisition and exactly one SDK operation per processing turn. An SDK or license operation already in progress can still block others. This guarantee applies to requests that have reached Lite's queue.

The guest and client currently serialize around whole HTTP operations. For the downloader integration:

1. Expose a private capability probe after authenticating the guest request. Keep the capability token, `session.Ensure` and existing session-error mappings in the guest server. Register the Lite routes only in the opted-in private service and supply a trusted gate that checks the current session grant at each enqueue.
2. In `internal/guestclient/client.go`, avoid holding the capacity-one `mediaGate` from batch dispatch through response read. Give batches bounded in-flight capacity independent of playback and keep each stream ordered. Continue to send playback as single-block requests.
3. In `internal/guestagent/server.go`, the gate acquired via `s.mediaGate.acquire` is currently released by `defer release()` after `s.media(w, r)`. For the opted-in batch path, yield this outer gate **before waiting for the complete Lite response**. Admit bounded concurrent dispatch so queued playback and cleanup can reach Lite between blocks. Preserve playback priority and download progress in guest scheduling. Leave enough Lite HTTP workers for singles and cleanup.
4. In `internal/guestagent/media_proxy.go`, forward the bounded body with a fixed length, versioned content type, stream metadata and the request lifetime. Preserve framed prefix results and keep trusted pre-dispatch errors distinguishable from transport errors. Forward cancellation by closing the upstream request. Allow authorized stream cleanup even after session expiry.
5. Verify the whole client–guest–Lite path with the real queue topology before enabling it. Lower HTTP request counts depend on the downloader packaging blocks and using the new route. Keep per-request deadlines separate from queue delays and treat a lost POST result as unknown. [Downloader guest server](https://github.com/Andrew05213/applemusic-downloader-native/blob/7eceb896611b1332b107350bdbc6cf546cfb73cc/apple-music-downloader/internal/guestagent/server.go) · [Downloader guest client](https://github.com/Andrew05213/applemusic-downloader-native/blob/7eceb896611b1332b107350bdbc6cf546cfb73cc/apple-music-downloader/internal/guestclient/client.go)

## Redacted stream summaries

Lite emits one INFO `native_stream_summary` on close, idle expiry, processing failure or shutdown. `transport_requests` counts admitted envelopes (single or batch) once. The `first` and `reuse` groups contain block attempts (`requests`), actual SDK invocations (`sdk_calls`), input and completed bytes and monotonic timings. A factory failure can increment attempts without an SDK invocation. Rejected or cancelled envelopes with no admitted block produce no stream counters.

The timing fields are `wait_ns` (admission and mutex wait), `held_ns` (protected processing), `create_ns` (context factory including key lookup), `copy_ns` (copy into the writable buffer) and `sdk_ns` (backend call). Creation, copy and SDK times are included in held time. `bytes_per_second` divides completed bytes by the sum of wait and held seconds. Logging occurs after unlocking and omits request metadata and payloads.

## Paired loopback benchmark

Build in the configured tree with assertions enabled and run on offline host loopback.

```sh
c++ -std=c++11 -O2 -ffunction-sections -fdata-sections -pthread \
  -I. -Ibuild/_deps/cjson-src -Ibuild/_deps/dobby-src/include \
  tests/native_processing_bench.cpp lite/native_batch_http.cpp \
  -Wl,--gc-sections -ldl -o /tmp/native-processing-bench
/tmp/native-processing-bench > /tmp/native-processing.csv
```

The fixture runs one warm-up pair followed by ten measured pairs in alternating order. Each starts from a fresh process configuration. Three download streams share 26,372 fixed synthetic inputs spanning approximately 239.6 MiB. One playback stream issues 1,648 single-block HTTP requests, paced at identical download completion milestones in each mode. The baseline serves the legacy route with batch disabled. The other mode enables the private adapter and sends up to four download blocks per HTTP request.

Wall time goes from releasing all four clients through their completion. Throughput divides download bytes by that wall time. Playback percentiles use nearest-rank latency from submitting each loopback request to receiving its reply. Results cover synthetic work on host loopback. The downloader's gates, QEMU transport and Android SDK costs require separate measurement.

Measured on x86-64 Linux, Intel Xeon Platinum 8573C, four-CPU quota and GCC 14.2.0. Timing and throughput cells are medians of the ten measured runs for each mode.

| Download transport | Download HTTP requests | Download SDK calls | Wall ms | Download MiB/s | Playback P95 µs | Playback P99 µs |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Single block | 26,372 | 26,372 | 903.32 | 265.28 | 255.28 | 465.75 |
| Batch, one turn per block | 6,594 | 26,372 | 419.18 | 571.78 | 277.97 | 468.84 |

Download held time across the 26,369 reused calls fell from 28.33 ms to 23.32 ms, while cumulative admission and lock wait increased from 20.64 ms to 203.99 ms. The waits can overlap. All 1,648 playback requests in each measured run started before the downloads finished. Playback P95 increased in eight pairs and P99 in three pairs. Batch transport stays opt-in. The PR includes all per-run CSV rows, including the warm-up and maximum latencies.
