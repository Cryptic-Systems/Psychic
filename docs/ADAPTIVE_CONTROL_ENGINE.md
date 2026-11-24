# Adaptive Audio Control Engine (AACE)

## Purpose

AACE is the coordination layer above Psychic's existing DSP stages. It does not replace EQ, auto-bass, speaker processing, preamp, DVC, or the limiter. It decides how much adaptive enhancement is appropriate and how quickly parameter targets should move.

The first implementation is intentionally internal. It adds no C ABI symbols and does not change the Flutter package or native library identifiers used by Resonate.

## Current processing model

```
Input
  |
User EQ
  |
AACE source measurements
  |-- post-EQ peak for headroom allocation
  |-- pre-enhancement RMS for loudness control
  |-- rolling fast/slow RMS trend and bass-band RMS
  |
AACE shared headroom allocator
  |-- bass enhancement budget
  |-- auto-preamp budget
  |-- auto-gain boost budget
  |
Auto-bass -> speaker / virtual bass -> auto-preamp
  |
Output-side RMS telemetry
  |
Auto-gain / DVC
  |
Limiter -> dither -> output
```

The audio callback remains the only place that processes audio samples. AACE uses fixed-size state and scalar block measurements; it performs no allocation, locking, file access, decoding, or track scanning. Rolling state uses approximately 450 ms fast loudness, 3 s slow loudness, and 1.2 s bass-energy envelopes. These are live-stream statistics, not whole-track analysis.

## First coordination changes

### Shared headroom

The allocator measures the current block's peak after user EQ and before adaptive enhancements. It subtracts the configured safety margin and a transient reserve, then distributes the remaining positive-gain budget according to the existing bass, preamp, and gain weights.

The headroom estimate contracts and recovers with time-based smoothing. It deliberately does not subtract the limiter's own gain reduction from the next block's enhancement budget: doing so can create a feedback loop in which a transient makes every adaptive stage withdraw gain, even after the transient has passed. The limiter remains the final peak-safety boundary.

A second detector separates isolated high-crest events from recurring pressure. It compares post-EQ source peak with unweighted source RMS. Crest excess above 12 dB is initially discounted for adaptive headroom; repeated high-crest blocks build a time-smoothed transient-density state, so recurring peaks progressively regain influence. The actual source peak remains recorded, and the limiter still sees and protects every sample. This changes enhancement budgeting only; it is not a replacement for clipping detection or limiting.

### Rolling live track state

AACE maintains three time-based envelopes from post-user-EQ measurements: fast full-band RMS (450 ms), slow full-band RMS (3 s), and low-band RMS (1.2 s). The fast-minus-slow difference is a lightweight section trend: positive values indicate a recent rise in level, while negative values indicate a recent fall. A separate confidence envelope requires sustained divergence before classifying a section as rising or falling; isolated transients are not sufficient. A reversal must build confidence independently rather than inheriting the prior direction's confidence. Confidence uses a 0.75 s attack and 1.10 s release, with a 1.5 dB trend floor and 2 dB directional floor. These initial thresholds are diagnostic only and do not drive gain. Bass prominence compares rolling low-band RMS with full-band RMS.

Content-aware virtual bass now uses rolling low-band RMS rather than the peak-hold bass meter. This avoids letting one kick transient suppress virtual bass through the following passage. A supplied offline analysis hint can still override the live estimate when it indicates stronger bass content. A bounded anticipatory gain hint is derived only after a rising/falling state is confidence-qualified. It is capped at ±1 dB, with rising trends suggesting a small cut and falling trends suggesting a small recovery. The hint is diagnostic telemetry only and is not consumed by the auto-gain servo. That keeps this first predictive signal diagnostic until its behaviour is validated on real programme material.

### Independent loudness reference

Auto-gain now uses a control RMS measured after user EQ but before automatic bass, virtual bass, and auto-preamp. The existing short-term RMS meter remains an output-side measurement after bass and preamp.

The control RMS also applies a gentle low-frequency de-emphasis below roughly 120 Hz, so bass-heavy passages have less influence on global gain than mid/high-frequency energy. This is a practical control signal, not a claim of standards-compliant LUFS measurement. It prevents automatic bass/preamp boosts from raising the detector and causing auto-gain to undo those boosts through global attenuation.

### Time-based target smoothing

Auto-gain, auto-bass, auto-preamp, and virtual-bass targets are smoothed using elapsed audio time rather than fixed per-block coefficients. Gain reduction and recovery use different time constants. Bass and preamp reductions move faster than increases; increases ease in more gradually.

Auto-gain now defaults to a 3 dB maximum cut. In addition, the negative side of the gain servo is gated by a separate 3 s reference of the same bass-de-emphasized control RMS used by the servo. A cut begins to earn confidence only when that sustained reference is more than 1.5 dB above the requested target, reaching full confidence across the next 6 dB. Confidence is eased back during rising/falling section transitions, so a brief loud section does not immediately receive the full cut allowance. This policy affects cuts only: positive gain remains governed by the shared headroom allocator, and the anticipatory hint remains diagnostic-only. The broader six-track evaluation showed that the 6 dB setting held the gain servo at its cut ceiling for most active blocks in the electronic and highly compressed samples (93.4% and 87.3% of blocks were more than 3 dB below the limiter-only baseline in the full adaptive stress profile). A 3 dB default is a more conservative starting point for already-mastered material; hosts can still explicitly request deeper cuts (up to 24 dB) when stronger loudness normalization is desired. This is a gain-level guardrail, not a replacement for the limiter or a reduction in peak protection. Signal measurements do not establish perceived pumping, so the default still needs listening validation across broader programme material.

## Decision telemetry

AACE now publishes a latest-block decision snapshot into fixed-size atomic state owned by the engine. It records:
- block index, post-user-EQ source peak and RMS, and effective budget peak;
- pre-enhancement control RMS and post-enhancement/pre-DVC RMS;
- available shared headroom and the bass, preamp, and gain allocations;
- requested target and applied value for auto-bass, auto-preamp, auto-gain, and virtual bass;
- sustained-loudness confidence and the effective per-block auto-gain cut limit;
- limiter gain-reduction peak-hold value;
- reason flags for each adaptive subsystem.

Reason flags currently identify active control, shared-budget limitation, safety-hint limitation, configured target/range limitation, silence recovery, disabled-control fade, and content-aware virtual-bass adaptation. Multiple reasons may apply in the same block.

The snapshot uses lock-free 32-bit and float atomics with a sequence counter. The audio thread only writes fixed scalar fields; readers retry a bounded number of times and never block the callback. No event queue, allocation, logging, or I/O is performed in the callback. The snapshot is internal and is not yet exposed through the public C ABI or Flutter binding; that API decision is deferred until the telemetry fields and semantics have been validated.

The limiter field is its existing peak-held gain-reduction meter, not a per-sample or per-block instantaneous GR trace. Gain-cut confidence is derived from a separate 3 s envelope of the bass-de-emphasized control RMS and is reduced during confidence-qualified section transitions; its effective cut limit is published alongside the existing requested/applied gain telemetry. The output RMS field is measured after adaptive bass/preamp and before auto-gain DVC and limiting. Rolling telemetry additionally reports fast and slow post-EQ RMS, their dB difference as a loudness trend, rolling low-band RMS, bass prominence relative to full-band RMS, a confidence-qualified rising/falling/steady section state, and the bounded anticipatory gain hint.

## Real-time and compatibility constraints

- No dynamic allocation or locks in the processing callback.
- No analysis of entire tracks in the processing callback.
- Offline track analysis remains a host/background responsibility.
- The limiter remains the hard peak-protection stage.
- Existing C ABI, public structs, Flutter package name, native library name, and Resonate integration remain unchanged.
- Existing user controls remain authoritative; AACE only coordinates their automatic targets.

## Validation

A focused unit-test target covers weighted headroom allocation, isolated-versus-repeated high-crest events, rolling loudness/bass envelopes, sustained-level gain-cut confidence, section-transition protection, telemetry coherence, and block-size-independent smoothing. A full-engine regression also verifies that a newly loud section receives less immediate cut than the same level after it has remained sustained. Section-transition tests additionally exercise abrupt level steps, a gradual fade, return to steady state after a step, direction reversal, isolated transients, modulation noise, and bass-only balance changes. These deterministic tests validate the detector's response to controlled block-energy patterns; they do not replace listening tests on real programme material. A separate `adaptive_signal_scenarios` test now generates deterministic 48 kHz mono audio blocks and runs the same rolling-state interface over compressed-style multitone modulation, sparse percussion transients, a clipped/limited-style static multitone, and gradual fade-in/fade-out envelopes. It checks that steady material settles without a transition, fades produce the expected transition direction, and the diagnostic hint polarity is correct. This remains synthetic signal validation, not a claim of listening validation or a substitute for testing licensed/representative music in a host application. The full-engine offline evaluator also reports the percentage of active programme blocks that fall more than 3 dB and 6 dB below the limiter-only reference, plus the longest contiguous run more than 3 dB below that reference. These persistence metrics help separate brief transient corrections from sustained attenuation; they are comparative signal diagnostics, not perceptual pumping scores.

Before merging, run the complete CMake build and test suite on the supported CI platforms, then run listening comparisons with:
- isolated transient peaks;
- sustained bass-heavy material;
- quiet-to-loud section transitions;
- heavily limited masters;
- tracks that receive positive auto-gain;
- different callback block sizes.

Record limiter gain reduction, auto-gain applied dB, auto-bass applied dB, preamp applied dB, source RMS, output RMS, and headroom while comparing the same source material against the previous controller.

## Offline whole-track analysis foundation

The host/background-only `AdaptiveTrackAnalyzer` consumes block measurements during a decoded track scan and retains a bounded summary. It reports duration and active/silence time, time-weighted integrated RMS, active-programme loudness percentiles (P10/P50/P95) and percentile range, sample peak, mean/maximum crest, rolling bass RMS/prominence, and confidence-qualified rising/falling transition duration. Loudness percentiles exclude blocks at or below -60 dBFS so leading/trailing silence does not dominate the distribution. The accumulator uses a fixed 145-bin histogram and scalar totals; it does not retain audio or allocate per block. The offline evaluator now prints this profile after running the actual engine against the supplied WAV. These are diagnostic signal features, not standards-compliant LUFS, mastering-quality scores, genre labels, or automatic processing instructions. They are not read by the real-time callback and do not yet change DSP behaviour.

A dedicated deterministic test covers mixed quiet/loud sections, silence, bass prominence, a one-block high peak, transition timing, reset, and non-finite input handling.

## Next AACE stages

1. Validate sustained-loudness gain-cut gating and decision telemetry against listening tests, then decide which stable diagnostics should be exposed to the host.
2. Validate transient-density thresholds and attack/release against isolated clicks, percussion, clipped masters, and sustained overload material; then tune limiter recovery.
3. Calibrate advisory track-trait thresholds against a wider licensed programme set and listening review; keep recommendations separate from processing controls.
4. Design a host-facing local-track scan API only after the report semantics and lifecycle are validated.
5. Add listener and output-device profiles, with safety constraints always taking priority.
6. Expose opt-in controls and observability to the host only after the internal behaviour is validated.


## Advisory whole-track intelligence

`AdaptiveTrackIntelligence` converts the offline summary into a fixed-size report of descriptive signal traits: low-level/quiet, consistently loud, high dynamic range, bass prominent, transient rich, and section variable. Each finding carries a bounded confidence value and the underlying measurements used to support it. Transient-rich classification uses a high-crest percentile and the share of active programme time above a crest threshold; a single maximum peak is deliberately insufficient. The analyzer therefore also accumulates a bounded crest histogram and high-crest time ratio.

Recommendations are cautious review prompts, not parameter changes. Examples include preserving dynamic range, reviewing low-frequency enhancement, retaining transient protection, and using conservative gain control on consistently loud material. A profile shorter than five seconds or with less than three seconds of active programme is reported as insufficient data. Silence does not create a profile. This stage is deterministic, fixed-size, offline-only, and does not affect the callback, C ABI, Flutter API, or playback settings. Thresholds are initial engineering heuristics, not trained labels or perceptual truth; validate them against broader programme material before exposing them to Resonate.

The dedicated `track_intelligence_tests` target checks loud/steady and quiet/dynamic profiles, bass prominence, sustained crest density versus a single peak, section variability, insufficient data, and silence handling. The offline full-engine evaluator now prints the report's findings, confidence values, evidence metrics, and advisory text alongside the raw track profile.


## Advisory classifier calibration and input validation

The current confidence ramps are explicit engineering heuristics, not learned probabilities. A calibration regression target probes threshold, midpoint, and saturation behaviour for loudness consistency, dynamic range, bass prominence, crest density, section variability, and quiet level. These tests guard intended monotonic response and make future threshold changes reviewable. They do not establish perceptual accuracy or replace listening validation on a representative licensed programme set.

Before classification, the report validates profile duration, active/silence/transition time, finite metric values, expected metric ranges, and loudness percentile ordering. Malformed or non-finite summaries fail closed as insufficient data rather than producing misleading high-confidence findings. This also protects future callers that may construct summaries independently of the current analyzer.
