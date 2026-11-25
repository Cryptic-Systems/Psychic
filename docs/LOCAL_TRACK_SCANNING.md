# Host-side local track scanning

Psychic provides an internal `AdaptiveTrackScanSession` in `src/core/track_scan_session.h`. It wraps the bounded offline analyzer and advisory intelligence classifier.

## Intended host flow

1. The host opens a local audio file and decodes it using its existing decoder.
2. A background worker calls `begin(expected_duration_seconds)`.
3. Feed decoded interleaved float PCM through `add_audio_block(samples, frames, channels, sample_rate)`. Psychic calculates full-band RMS, sample peak, and 120 Hz low-pass RMS for each block.
4. The host/UI polls `snapshot()` for state, processed duration, and progress.
5. The worker calls `finalize()` at end-of-track. The completed snapshot contains both `TrackAnalysisSummary` and `TrackIntelligenceReport`.
6. If the user stops the scan, call `cancel()`. A cancelled snapshot retains partial measurements for diagnostics but does not publish an intelligence report as complete.

If duration is unknown, pass zero to `begin`; progress remains indeterminate until completion. `add_audio_block` accepts 1-8 interleaved float channels and sample rates from 8 kHz to 384 kHz. Invalid pointers, frame counts, channel counts, and sample rates are rejected. Non-finite sample values are treated as silence. The host can alternatively call `add_block` when it already has validated measurements or transition metadata.

The session rejects further input after completion or cancellation. A new scan can begin after a previous session has ended.

## Track analysis cache

`AdaptiveTrackAnalysisCache` in `src/core/track_analysis_cache.h` provides a bounded, thread-safe in-memory LRU cache for completed reports.

The host supplies a `TrackCacheIdentity` containing:
- `stable_id`: stable media-library ID or canonical URI/path.
- `file_size_bytes`: current file size.
- `modified_time_ns`: host-provided modification timestamp.
- `content_fingerprint`: optional content hash/fingerprint.

A cache lookup hits only when the stable ID and all revision fields match. If the stable ID exists but revision metadata differs, the old report is invalidated and the lookup misses. Storing a completed report under the changed identity replaces the old revision. Partial, cancelled, or insufficient-data reports are not cached. Capacity is bounded (default 256); least-recently-used entries are evicted when full. Explicit `invalidate(stable_id)` and `clear()` support media-library change events.

The cache is deliberately memory-only. Psychic does not canonicalize paths, inspect timestamps, hash files, read metadata, persist records, or decide when to scan. Resonate remains responsible for supplying trustworthy identity/revision metadata, durable persistence if desired, and invalidation when its library detects a change. If reliable file revision metadata is unavailable, the host should supply a content fingerprint or invalidate explicitly.

## Threading and real-time boundaries

The scan session and cache synchronize their lifecycle and snapshots with mutexes. They are intended for background work and concurrent host/UI readers. The mutexes are acceptable because these APIs are explicitly not real-time. Do not call them from `dsp_process`, an audio callback, or any latency-sensitive audio thread.

The scanner retains only fixed-size histograms, scalar totals, filter state, and the final fixed-size report. The cache retains a bounded number of report snapshots and identity strings. Neither retains decoded PCM, performs file I/O, owns a decoder, spawns threads, nor participates in playback. The host owns file access, decoding, worker scheduling, cancellation policy, and durable cache storage.

## API status and compatibility

These are internal C++ host-side foundations, not yet stable public APIs. They do not add C ABI symbols, change existing C structs, modify the Flutter package, or change Resonate integration identifiers. Public C/FFI exposure should be considered separately after lifecycle, cache invalidation, and report semantics are exercised in the host application.

## Conservative playback-hint adapter

`src/core/track_playback_hint.h` translates a completed, sufficient scan snapshot into the existing `dsp_apply_analysis_hint()` call. It forwards the scanner's 120 Hz low-pass RMS as the bass-energy hint and its time-weighted mean block crest as the crest hint. The mapping is bounded and rejects incomplete, insufficient, or malformed snapshots.

The scanner does not calculate true peak or probe a safe bass-boost ceiling. The adapter therefore does not fabricate either measurement and passes the existing 24 dB sentinel to indicate that no safe-bass cap is available. The current C API consumes only the bass-energy and crest fields from `DspAnalysisResult`; the other struct fields are deliberately not treated as scanner measurements.

This adapter does not set EQ, auto-gain targets, auto-bass targets, preamp, or enhancement enablement. It does not interpret advisory trait labels as processing commands. The host may call it once when a track starts, after a cache hit or a completed scan, on the same non-audio thread used for existing control calls. The analysis-hint values are queued atomically and applied by the audio thread at the next process-block boundary; the call does not mutate limiter coefficients or playback state directly. Never call the scanner or cache from the audio callback This remains an internal C++ host helper; no public C ABI or Flutter/Resonate binding changed.
