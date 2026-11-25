# Psychic offline playback simulator

This small native host lets you test Psychic's track-scan-to-playback path without
building or launching Resonate. It is an offline simulator, not a real-time audio
device player.

## What it does

1. Loads a WAV file into memory.
2. Scans it using \`AdaptiveTrackScanSession\` (whole-track RMS, bass RMS, peak,
   crest and advisory track intelligence).
3. Finalizes the scan and builds the existing \`TrackPlaybackHint\`.
4. Creates a native Psychic \`DspEngine\` and runs the file through \`dsp_process()\`.
5. Writes processed audio as a float32 WAV and block-level telemetry as CSV.

Input WAV formats: PCM 16-bit, 24-bit or 32-bit integer, or IEEE float32;
1-8 channels; 8-384 kHz. MP3/FLAC are not decoded by this tool. Convert a local
licensed file with FFmpeg, for example:

\`\`\`sh
ffmpeg -i "your-track.mp3" -ar 48000 -ac 2 -c:a pcm_f32le "test-input.wav"
\`\`\`

No audio files are included in the repository. Keep source recordings local.

## Build

From the Psychic repository root:

\`\`\`sh
cmake -S . -B build -DDSP_BUILD_EXAMPLES=OFF
cmake --build build --target psychic_playback_simulator --config Release
\`\`\`

On single-configuration Linux/macOS generators, the executable is normally
\`build/tests/psychic_playback_simulator\`.

On Visual Studio Windows generators, it is normally
\`build/tests/Release/psychic_playback_simulator.exe\`. The CMake target copies
\`dsp_engine.dll\` beside the executable.

## Run

\`\`\`sh
build/tests/psychic_playback_simulator "test-input.wav" "processed.wav" both
\`\`\`

Windows example:

\`\`\`powershell
.\build\tests\Release\psychic_playback_simulator.exe "test-input.wav" "processed.wav" both
\`\`\`

The run creates:

- \`processed.wav\` — processed float32 WAV, ready to audition.
- \`processed.wav.csv\` — block-by-block input/output RMS and peak, limiter GR,
  auto-bass applied gain, and crest hint.

The console prints the track-analysis summary, detected traits, and overall
input/output levels.

## Hint comparison

Run the same input four times, using a distinct output filename:

\`\`\`sh
psychic_playback_simulator "test-input.wav" "out-none.wav" none
psychic_playback_simulator "test-input.wav" "out-bass.wav" bass
psychic_playback_simulator "test-input.wav" "out-crest.wav" crest
psychic_playback_simulator "test-input.wav" "out-both.wav" both
\`\`\`

- \`none\`: no offline hint is submitted.
- \`bass\`: submits the scanned bass-energy hint only.
- \`crest\`: submits the scanned crest hint only.
- \`both\`: uses \`make_track_playback_hint()\` and the host adapter.

The simulator intentionally enables modest auto-bass and content-aware virtual
bass so the bass-content hint can be observed, plus the limiter so crest
personality can be inspected. These are controlled test settings, not proposed
Resonate defaults. Compare the four output files and CSVs; do not treat a
numerical change alone as proof of better perceived sound.

If the scan reports insufficient active programme, use a longer track with at
least several seconds of non-silent audio. Keep a copy of the unprocessed source
and compare at matched playback loudness when listening.
