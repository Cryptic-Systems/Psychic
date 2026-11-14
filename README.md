# DSP ENGINE

Highly optimized **64-bit** audio processing engine for Flutter music players and native apps.

**Version 0.4.0** — Resonate integration surface (PCM16, control mailbox, preamp, speaker/VB, two-band limiter).

## Features

- **Direct Volume Control (DVC)** — clean gain with optional smoothing
- **Multi-band parametric EQ** (peak, shelves, LP/HP)
- **True-peak / lookahead / two-band limiter** with ceiling, knee, crossover
- **Preamp** (independent of DVC)
- **Speaker mode + virtual bass** (phone delivery)
- **PCM16 process path** for Media3 / JNI hosts (`dsp_process_pcm16`)
- **Lock-free control mailbox** (UI-thread safe setters)
- **Dither** (TPDF / noise-shaped)
- **Sample-rate conversion** (linear, cubic, HQ windowed-sinc)
- **Lock-free SPSC ring buffer**
- **Double-precision internal path** (optional)
- **Real-time thread priority** helpers
- **Platform audio backends**: AAudio, WASAPI, Core Audio, ALSA
- **C ABI** + Flutter `dart:ffi` bindings
- **Unit tests + benchmarks** + GitHub Actions CI matrix

## Pipeline

`EQ → speaker/VB → preamp → DVC → limiter → dither`

## Processing notes

- `dsp_process` / `dsp_process_pcm16` auto-chunk buffers larger than `max_block`.
- Control setters may be called from any thread; they apply at the next process block.
- `dsp_process` / `dsp_process_pcm16` / `dsp_process_src` must stay on a single audio thread.

## Quick start (CMake)

```bash
cmake -B build -DDSP_BUILD_TESTS=ON -DDSP_BUILD_EXAMPLES=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## License

MIT — see [LICENSE](LICENSE).
