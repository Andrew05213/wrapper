# Lite native audio processing

The base of this measurement is `work` at `a185b9cab2ec40f98672940cbd3ef46e8cdfe531`, also the Wrapper revision pinned by AMDL beta.6. The injected HTTP handler lives in the downloader's `native-runtime/native_service.h`. Its request count and client-side queue are outside Wrapper's measurements.

## Reading a stream summary

At INFO level, Lite emits one `native_stream_summary` line per admitted stream when it closes, expires, fails processing or shuts down. The line contains an `end` value of `close`, `idle`, `failure` or `shutdown` and two groups of numeric counters. `first` counts calls that attempted to create a context, including after a context change. `reuse` counts calls on an existing context. Both groups appear on the same line so consumers can associate them without logging an identifier. Validation and stream-capacity rejections never acquire a stream entry.

| Counter in each group | Measurement |
| --- | --- |
| `requests`, `bytes`, `completed_bytes` | Admitted calls, submitted bytes and bytes completed through the single-block bridge |
| `wait_ns` | Immediately before acquiring the global playback mutex to acquisition |
| `create_ns` | Context factory call, including key lookup, SDK setup and factory cleanup |
| `copy_ns` | Assignment of the input to the writable output buffer, including any allocation |
| `sdk_ns` | Elapsed time inside the in-place backend call |
| `held_ns` | Acquisition through end of protected processing, including expiry, lookup and measured subphases |
| `bytes_per_second` | Completed bytes divided by the sum of wait and held seconds, rounded to a whole byte/s |

All intervals use a monotonic clock. The counters measure elapsed time including descheduling. Copy, creation and SDK times are parts of held time. Waits on different threads can overlap. A stream summary excludes HTTP work, validation before locking, idle time between calls, and logging after unlock. Overall task throughput uses total download bytes and one wall-clock interval across workers.

## Offline comparison

From a configured build tree, run this host benchmark with compiler assertions enabled.

```sh
c++ -std=c++11 -O2 -ffunction-sections -fdata-sections -pthread \
  -I. -Ibuild/_deps/cjson-src -Ibuild/_deps/dobby-src/include \
  tests/native_processing_bench.cpp -Wl,--gc-sections -ldl -o /tmp/native-processing-bench
/tmp/native-processing-bench
```

The fixture splits 8,188 synthetic blocks (79.96 MiB) across three download streams. Sizes cycle through 4, 8, 12 and 16 KiB. One playback stream issues 511 single-block requests, one per 16 completed download blocks. Both modes use the same buffers, progress milestones, stateful fake SDK and global mutex. The experiment in `tests/native_batch_candidate.h` takes one lock for at most four blocks and 64 KiB of a single context. It validates the entire batch before processing and calls the backend once per block in order.

Wall time runs from releasing the four workers through completion of downloads and playback. Throughput counts download bytes only. Playback P95 is the nearest-rank duration from entering the single-block bridge through return. The benchmark prints CSV for a warm-up pair and ten measured pairs in alternating order, including per-run maximum latency and how many playback calls began while downloads were still active.

Measured on Linux x86-64, Intel Xeon Platinum 8573C, four-CPU quota, GCC 14.2.0, host Release flags above. Each table cell is the median of ten per-run measurements.

| Download calls | Wall ms | Download MiB/s | Playback P95 µs | Reused wait ms | Reused copy ms | Reused fake SDK ms | Reused held ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Single | 17.67 | 4,547 | 76.38 | 25.89 | 4.74 | 2.22 | 8.52 |
| Up to four per lock | 15.62 | 5,124 | 102.69 | 18.51 | 5.40 | 2.20 | 8.30 |

The last four columns sum 8,185 reused download calls per run. The three first download calls spent a combined median of 5.02 µs (single) or 4.64 µs (batch) creating fake contexts. Single-call wall times ranged from 11.05 to 23.46 ms, batch times from 12.75 to 18.06 ms. Playback P95 increased in eight of ten paired runs. The largest individual playback call observed was 3.97 ms for single and 5.06 ms for batch. A bound on block count and bytes gives no deadline for mutex acquisition or SDK completion.

## Downstream impact

This change ships the stream summaries and leaves the batch candidate in the host fixtures. The candidate trades higher median download throughput for worse playback tail latency. The stateful fake measures contention, copying and scheduling with synthetic work. Real Android context creation, per-block SDK time and beta.6 end-to-end latency on ALAC require an authorized runtime workload. The offline ABI checks exercise separate SDK operations.

A downloader built with this Wrapper can collect the INFO summaries for real workloads. The shipping C++ interface continues to accept one block, with one injected HTTP request per block in beta.6. Reducing that count requires a future versioned batch capability on both sides. A transport adopting a batch API would need to preserve per-block boundaries, check capability before sending, and distinguish a rejected unstarted request from a partially consumed batch. In the experiment, outputs contain the completed prefix. A backend failure can consume one additional attempted block, while cancellation between blocks leaves the suffix untouched. A lost reply leaves consumption unknown. Replaying the entire batch in those cases could advance stream state twice.
