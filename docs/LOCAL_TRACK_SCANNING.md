# Host-side local track scanning

Psychic now provides an internal `AdaptiveTrackScanSession` in `src/core/track_scan_session.h`. It is a lifecycle wrapper around the bounded offline analyzer and advisory intelligence classifier.

## Intended host flow

1. The host opens a local audio file and decodes it using its existing decoder.
2. A background worker computes per-block RMS, sample peak, bass-band RMS, and optional section-transition metadata.
3. The worker calls `begin(expected_duration_seconds)` once, then feeds each decoded block through `add_block(...)`.
4. The host/UI polls `snapshot()` for state, processed duration, and progress.
5. The worker calls `finalize()` at end-of-track. The completed snapshot contains both `TrackAnalysisSummary` and `TrackIntelligenceReport`.
6. If the user stops the scan, call `cancel()`. A cancelled snapshot retains partial measurements for diagnostics but does not publish an intelligence report as complete.

If duration is unknown, pass zero to `begin`; progress remains indeterminate until completion. The session rejects invalid/non-positive block durations and rejects further input after completion or cancellation. A new scan can begin after a previous session has ended.

## Threading and real-time boundaries

The session synchronizes its own lifecycle and snapshots with a mutex. It is intended for one background producer and concurrent host/UI readers. The mutex is acceptable because this API is explicitly not real-time. Do not call it from `dsp_process`, an audio callback, or any latency-sensitive audio thread.

The session retains only fixed-size histograms, scalar totals, and the final fixed-size report. It does not retain decoded PCM, perform file I/O, own a decoder, allocate per block, or spawn threads. The host owns file access, decoding, worker scheduling, cancellation policy, and persistence/cache decisions.

## API status and compatibility

This is an internal C++ host-side foundation, not yet a stable public API. It does not add C ABI symbols, change existing C structs, modify the Flutter package, or change Resonate integration identifiers. Public C/FFI exposure should be considered separately after the lifecycle and report semantics are exercised in the host application.
