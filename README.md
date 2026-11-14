# DSP ENGINE

Highly optimized 64-bit audio processing engine for Flutter music players and native apps.

**Version 0.3.2** — cross-platform with AAudio (Android), WASAPI (Windows) and Core Audio (macOS/iOS).

## Features

- **Direct Volume Control (DVC)** — clean gain without digital clipping artefacts
- **Multi-band parametric EQ** (peak, shelves, LP/HP)
- **True-peak limiter** with fast attack / configurable release
- **Dither** (TPDF / noise-shaped)
- **Sample-rate conversion** (linear, cubic, HQ windowed-sinc)
- **Lock-free SPSC ring buffer**
- **Double-precision internal path** (optional)
- **Real-time thread priority** helpers
- **Platform audio backends**:
  - Android → AAudio (low-latency / exclusive)
  - Windows → WASAPI (shared + exclusive)
  - macOS / iOS → Core Audio (RemoteIO / Default Output)
  - Linux → ALSA (optional)
- **C ABI** + Flutter `dart:ffi` bindings
- **Unit tests + benchmarks** + GitHub Actions CI matrix

## Processing notes

- `dsp_process` **auto-chunks** buffers larger than `max_block` (set in `DspConfig`, default often 1024). You can pass long offline buffers without manual splitting.
- For live I/O, keep the backend callback realtime-safe: no heap alloc, locks, or blocking I/O inside the render callback.

## Quick start (CMake)

```bash
cmake -B build -DDSP_BUILD_TESTS=ON -DDSP_BUILD_EXAMPLES=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/tests/benchmarks
./build/examples/dsp_example_offline
./build/examples/dsp_example_playback   # live I/O when a device is available
```

## Minimal playback pattern

```c
DspEngine* eng = dsp_create(&cfg);
DspBackend* be = dsp_backend_create();
dsp_backend_open(be, &bcfg);
dsp_backend_start(be, my_render_cb, user);  /* callback calls dsp_process */
/* ... */
dsp_backend_stop(be);
dsp_backend_destroy(be);
dsp_destroy(eng);
```

See `examples/playback_loop.cpp` for a full sine → DVC → limiter → backend example.

## License

MIT — see [LICENSE](LICENSE).
