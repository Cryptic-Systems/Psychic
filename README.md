# DSP ENGINE

Highly optimized **64-bit** audio processing engine for Flutter music players and native apps.

**Version 0.5.0** — Intelligent headroom + auto-bass (offline scan + online servo).

## Features

- **Direct Volume Control (DVC)** — clean gain with optional smoothing
- **Multi-band parametric EQ** (peak, shelves, LP/HP; up to 31 bands)
- **True-peak / lookahead / two-band limiter** with ceiling, knee, crossover
- **Preamp** (independent of DVC)
- **Speaker mode + virtual bass** (phone delivery)
- **PCM16 process path** for Media3 / JNI hosts (`dsp_process_pcm16`)
- **Lock-free control mailbox** (UI-thread safe setters)
- **Offline analysis** — `dsp_analyze_buffer` / `dsp_probe_max_bass_gain_db` (per-track safe bass)
- **Online auto-bass** — level meters + low-shelf servo under ceiling−margin
- **Level meters** — input / true-peak / post-EQ / bass-band / headroom
- **Dither** (TPDF / noise-shaped)
- **Sample-rate conversion** (linear, cubic, HQ windowed-sinc)
- **Lock-free SPSC ring buffer**
- **Double-precision internal path** (optional)
- **Real-time thread priority** helpers
- **Platform audio backends**: AAudio, WASAPI, Core Audio, ALSA
- **C ABI** + Flutter `dart:ffi` bindings
- **Unit tests + benchmarks** + GitHub Actions CI matrix

## Pipeline

`EQ → auto-bass shelf → speaker/VB → preamp → DVC → limiter → dither`

## License

See LICENSE.
