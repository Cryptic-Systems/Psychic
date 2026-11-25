# Changelog

## [Unreleased] — Adaptive Audio Control Engine
### Added
- Conservative host-side adapter from completed track scans to the existing bass-energy / crest playback hints, with fail-closed validation and no fabricated true-peak or safe-bass data
- Analysis hints now use the existing atomic control mailbox and are applied at the next audio block boundary, avoiding direct host-thread writes to live limiter state
- Regression tests for completed/incomplete scans, malformed values, bounded hint mapping, and existing C API application
- Bounded host-side LRU cache for completed local track-analysis snapshots, keyed by stable media identity and host-supplied revision metadata
- Stale-report invalidation, explicit per-track invalidation, cache statistics, and capacity-based eviction tests
- Calibration regression coverage for advisory confidence thresholds, midpoint response, monotonicity, and malformed/non-finite profile rejection
- Fail-closed validation of track summary duration, metric ranges, and percentile ordering before classification
- Confidence-rated advisory whole-track traits for consistent loudness, dynamic range, bass prominence, sustained transient density, quiet level, and section variability
- Bounded crest-percentile and high-crest-duration statistics so transient-rich findings cannot be triggered by one isolated peak
- Explainable review recommendations kept separate from DSP controls and playback processing
- Rolling live track-state analysis with fast/slow loudness envelopes, loudness trend, rolling bass RMS, and bass prominence
- Content-aware virtual-bass decisions now use rolling bass energy rather than a peak-hold transient meter
- Transient-density-aware headroom budgeting that discounts isolated high-crest events while allowing recurring peaks to build sustained pressure
- Internal per-block decision telemetry for rolling fast/slow loudness, section trend, rolling bass energy, and adaptive subsystem reasons, requested/applied targets, shared headroom, loudness references, and limiter gain-reduction peak hold
- Bounded lock-free snapshot reads using lock-free scalar atomics; no callback logging, allocation, or event queue
- Deterministic section-transition validation for abrupt steps, gradual fades, steady-state recovery, and bass-only balance changes
- Generated 48 kHz signal scenarios for compressed-style modulation, sparse percussion, clipped/limited-style material, and programme-level fades
- Bounded offline whole-track profile statistics: integrated RMS, active loudness percentiles/range, peak/crest, bass prominence, silence ratio, and section-transition duration
- Host-side incremental track scan lifecycle with progress, cancellation, and decoded PCM ingestion

### Added
- Internal AACE coordination layer for shared headroom allocation and time-based adaptive control smoothing
- Separate pre-enhancement control RMS so auto-gain does not counteract automatic bass/preamp boosts
- Focused tests for rolling track-state trends, transient pressure, sustained-level gain-cut confidence, section-transition protection, headroom allocation, telemetry, and block-size-independent smoothing
- Full-engine regression verifying that gain cuts build after a loud section persists rather than immediately following a level transition
- Deterministic offline track-analysis tests and evaluator summary output
- Architecture and validation notes in `docs/ADAPTIVE_CONTROL_ENGINE.md`

### Changed
- Added a 3 s sustained-control-level confidence gate for auto-gain cuts, eased during detected section transitions; positive gain/headroom allocation is unchanged
- Reduced the default auto-gain maximum cut from 6 dB to 3 dB after expanded programme measurements showed sustained cut-ceiling behaviour on already-mastered material; applications can still explicitly request deeper cuts up to 24 dB
- Auto-gain headroom uses post-user-EQ peak and RMS with transient-density handling, rather than stale post-limiter telemetry
- Auto-bass, auto-preamp, virtual-bass, and auto-gain target transitions use elapsed-time smoothing

### Compatibility
- No public C ABI, Flutter package, or Resonate integration identifiers changed

## [0.6.1] — 2026-11-24

### Branding
- Product / project renamed to **Psychic** (adaptive audio DSP engine)
- C ABI unchanged (`dsp_*`, `dsp_engine.h`, link targets `dsp_engine*`) for host compatibility
- Docs and banners updated; Resonate integration guide retitled

## [0.6.0] — 2026-11-20

### Full intelligent stack
- Shared headroom budget (`dsp_headroom_budget_set`) for bass / preamp / auto-gain
- Clip-aware auto-preamp (`dsp_auto_preamp_*`)
- Content-aware virtual bass (`dsp_auto_vb_*`)
- Crest-based limiter personality (`dsp_limiter_set_crest_hint`)
- `dsp_apply_analysis_hint` — feed offline scan into live engine
