# Offline full-engine audio evaluation

Raw programme recordings are intentionally not stored in this public repository. Keep licensed or otherwise restricted audio in a local, untracked directory and do not commit it.

## Evaluation input

The evaluator accepts one decoded WAV per run:

- 48 kHz sample rate
- stereo
- IEEE float32 PCM

For an MP3 source, decode locally with ffmpeg:

```bash
ffmpeg -y -v error -i "/path/to/source.mp3" -ar 48000 -ac 2 -c:a pcm_f32le "/tmp/psychic-input.wav"
```

Build Psychic with tests enabled, then run:

```bash
build/tests/adaptive_audio_evaluation "/tmp/psychic-input.wav" "/tmp/psychic-evaluation.csv"
```

The evaluator runs the actual `dsp_process()` implementation through five comparison profiles and writes per-block measurements to CSV. It reports programme RMS deltas, attenuation persistence, adaptive gain range, limiter gain reduction, and section-transition telemetry.

## Interpretation

Measurements describe the decoded source, not necessarily the original master. Lossy codec overs are not proof of clipping in the original recording. Attenuation statistics are signal diagnostics, not perceptual pumping scores; listening checks remain necessary for conclusions about sound quality.

Keep source files unchanged and retain any required attribution and license records locally. The automated CI suite builds the evaluator and runs deterministic unit/synthetic-signal tests; it does not download or redistribute programme audio.
