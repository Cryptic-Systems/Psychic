# Changelog

## [0.5.0] — 2026-11-20

### Intelligent headroom / auto-bass
- **Path A (offline):** `dsp_analyze_buffer` — sample peak, true-peak, RMS, crest, bass energy
- **Path A:** `dsp_probe_max_bass_gain_db` — binary-search max safe low-shelf under ceiling−margin
- **Path B (online):** running `DspLevelMeter` (in peak / true-peak, post-EQ peak, bass-band, headroom)
- **Path B:** `dsp_auto_bass_*` — dedicated low-shelf servo toward `min(target, headroom−margin)`
- Pipeline: EQ → auto-bass shelf → speaker/VB → preamp → DVC → limiter → dither
- Limiter remains the hard safety net; auto-bass only reduces boost when headroom is tight

### Tests
- `analyze_buffer` and `level_meter_and_auto_bass` regression coverage

## [0.4.0] — 2026-11-14

### EQ capacity
- Raised engine-side parametric EQ max from 16 → **31** bands (`DSP_EQ_MAX_BANDS`)
- Public header exposes `DSP_EQ_MAX_BANDS`; bulk setter retains full `DspEqBand` (q + type)
- Regression test: configure + process 31 bands with non-default Q and shelf/peak/LP/HP types

### Resonate integration surface
- `dsp_process_pcm16` in-place PCM16 path for Media3 / JNI
- Lock-free control mailbox (setters safe from UI thread; applied at process)
- Preamp (linear / dB) independent of DVC
- Speaker mode + virtual bass
- Two-band limiter (`DSP_LIMITER_TWOBAND`) with LR crossover + dual ceilings
- Master `dsp_set_processing_enabled` bypass
- `DspStats` process/CPU diagnostics
- Pipeline: EQ → speaker/VB → preamp → DVC → limiter → dither

### Tests
- 34 unit tests covering PCM16, preamp/speaker, two-band, bypass, stats
- Plus regression for 31-band EQ full params

## [0.3.2] — 2026-10-26

### Added
- Full cross-platform audio backends:
  - **AAudio** (Android) — low-latency / exclusive mode support
  - **WASAPI** (Windows) — shared + exclusive, Pro Audio thread priority
  - **Core Audio** (macOS/iOS) — RemoteIO / Default Output
- CMake auto-detection of host platform backends
- Complete public C API for limiter, dither, SRC and backends
- Self-contained unit-test framework + 12+ tests
- Throughput benchmarks (MSamples/s + realtime factor)
- GitHub Actions CI matrix (Linux / macOS / Windows / Android NDK)
- Flutter FFI bindings updated for all new features
- True-peak style limiter with configurable threshold & release
- TPDF / shaped dither
- Linear / cubic / HQ sample-rate conversion path

### Changed
- Internal processing uses double precision by default (`DSP_USE_DOUBLE`)
- Real-time safety hardened (no allocations on the process path)
- Version string now reports `0.3.2`

### Fixed
- Multi-channel no-allocation process path
- Limiter attack/release timing
- Backend factory stubs when a platform define is absent

## [0.2.0] — earlier
- Multi-band EQ + Flutter FFI
- Initial DVC and ring-buffer work
