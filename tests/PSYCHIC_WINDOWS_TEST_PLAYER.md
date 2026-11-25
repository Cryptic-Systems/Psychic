# Psychic Windows Test Player

A simple Windows desktop front-end for the native Psychic playback simulator.

## Run

1. Extract the complete ZIP to a folder (do not run from inside the ZIP).
2. Open `psychic_test_player.exe`.
3. Browse to a WAV file. Supported input: PCM 16/24/32-bit or IEEE float32 WAV, 1-8 channels.
4. Choose **Both hints**, **No hints**, **Bass only**, or **Crest only**.
5. Click **Process track**.
6. Click **Open processed audio** to open the output in your default Windows audio player.

The tool creates a float32 WAV and a CSV telemetry file beside it. The GUI is a lightweight offline test front-end, not a real-time WASAPI player. It performs whole-track scanning before processing; longer tracks can take a while. MP3/FLAC are not decoded directly—convert to WAV first using FFmpeg.

Keep all audio files local; the app does not upload them.
