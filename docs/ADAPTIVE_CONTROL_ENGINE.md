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

AACE maintains three time-based envelopes from post-user-EQ measurements: fast full-band RMS (450 ms), slow full-band RMS (3 s), and low-band RMS (1.2 s). The fast-minus-slow difference is a lightweight section trend: positive values indicate a recent rise in level, while negative values indicate a recent fall. A separate confidence envelope requires sustained divergence before classifying a section as rising or falling; isolated transients are not sufficient. Confidence uses a 0.75 s attack and 1.10 s release, with a 1.5 dB trend floor and 2 dB directional floor. These initial thresholds are diagnostic only and do not drive gain. Bass prominence compares rolling low-band RMS with full-band RMS.

Content-aware virtual bass now uses rolling low-band RMS rather than the peak-hold bass meter. This avoids letting one kick transient suppress virtual bass through the following passage. A supplied offline analysis hint can still override the live estimate when it indicates stronger bass content. The trend is currently observable through internal telemetry; it does not yet drive auto-gain or other gain changes. That keeps this first predictive signal diagnostic until its behaviour is validated on real programme material.

### Independent loudness reference

Auto-gain now uses a control RMS measured after user EQ but before automatic bass, virtual bass, and auto-preamp. The existing short-term RMS meter remains an output-side measurement after bass and preamp.

The control RMS also applies a gentle low-frequency de-emphasis below roughly 120 Hz, so bass-heavy passages have less influence on global gain than mid/high-frequency energy. This is a practical control signal, not a claim of standards-compliant LUFS measurement. It prevents automatic bass/preamp boosts from raising the detector and causing auto-gain to undo those boosts through global attenuation.

### Time-based target smoothing

Auto-gain, auto-bass, auto-preamp, and virtual-bass targets are smoothed using elapsed audio time rather than fixed per-block coefficients. Gain reduction and recovery use different time constants. Bass and preamp reductions move faster than increases; increases ease in more gradually.

These are initial engineering defaults, not final perceptual tuning. They must be validated with representative music and measured output.

## Decision telemetry

AACE now publishes a latest-block decision snapshot into fixed-size atomic state owned by the engine. It records:
- block index, post-user-EQ source peak and RMS, and effective budget peak;
- pre-enhancement control RMS and post-enhancement/pre-DVC RMS;
- available shared headroom and the bass, preamp, and gain allocations;
- requested target and applied value for auto-bass, auto-preamp, auto-gain, and virtual bass;
- limiter gain-reduction peak-hold value;
- reason flags for each adaptive subsystem.

Reason flags currently identify active control, shared-budget limitation, safety-hint limitation, configured target/range limitation, silence recovery, disabled-control fade, and content-aware virtual-bass adaptation. Multiple reasons may apply in the same block.

The snapshot uses lock-free 32-bit and float atomics with a sequence counter. The audio thread only writes fixed scalar fields; readers retry a bounded number of times and never block the callback. No event queue, allocation, logging, or I/O is performed in the callback. The snapshot is internal and is not yet exposed through the public C ABI or Flutter binding; that API decision is deferred until the telemetry fields and semantics have been validated.

The limiter field is its existing peak-held gain-reduction meter, not a per-sample or per-block instantaneous GR trace. The output RMS field is measured after adaptive bass/preamp and before auto-gain DVC and limiting. Rolling telemetry additionally reports fast and slow post-EQ RMS, their dB difference as a loudness trend, rolling low-band RMS, bass prominence relative to full-band RMS, and a confidence-qualified rising/falling/steady section state.

## Real-time and compatibility constraints

- No dynamic allocation or locks in the processing callback.
- No analysis of entire tracks in the processing callback.
- Offline track analysis remains a host/background responsibility.
- The limiter remains the hard peak-protection stage.
- Existing C ABI, public structs, Flutter package name, native library name, and Resonate integration remain unchanged.
- Existing user controls remain authoritative; AACE only coordinates their automatic targets.

## Validation

A focused unit-test target covers weighted headroom allocation, isolated-versus-repeated high-crest events, rolling loudness/bass envelopes, telemetry coherence, and block-size-independent smoothing.

Before merging, run the complete CMake build and test suite on the supported CI platforms, then run listening comparisons with:
- isolated transient peaks;
- sustained bass-heavy material;
- quiet-to-loud section transitions;
- heavily limited masters;
- tracks that receive positive auto-gain;
- different callback block sizes.

Record limiter gain reduction, auto-gain applied dB, auto-bass applied dB, preamp applied dB, source RMS, output RMS, and headroom while comparing the same source material against the previous controller.

## Next AACE stages

1. Validate decision telemetry against listening tests, then decide which stable diagnostics should be exposed to the host.
2. Validate transient-density thresholds and attack/release against isolated clicks, percussion, clipped masters, and sustained overload material; then tune limiter recovery.
3. Validate section-transition confidence and calibrated low-frequency balance against real music, then add richer pre-analysis hints for local tracks.
4. Add listener and output-device profiles, with safety constraints always taking priority.
5. Expose opt-in controls and observability to the host only after the internal behaviour is validated.
