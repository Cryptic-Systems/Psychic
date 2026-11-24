# Psychic Flutter FFI (`dsp_engine`)

Flutter bindings for **[Psychic](https://github.com/Cryptic-Systems/Psychic)** — 64-bit audio processing with Direct Volume Control (DVC).

## App identity

This package is a **library**, not an application.

| | Value |
|--|--------|
| Resonate applicationId | `com.aetherion.resonate` / `com.Aetherion.Resonate` |
| This package | `dsp_engine` (legacy package identifier; retained for Resonate compatibility) |
| Separate DSP app id? | **No** — do not use `com.aetherion.resonatedsp` for the player |

When integrated into Resonate, native code runs inside Resonate’s process and package name.

## Usage

```yaml
dependencies:
  dsp_engine:
    path: path/to/Psychic/bindings/flutter/dsp_engine
```

```dart
import 'package:dsp_engine/dsp_engine.dart';

final engine = DspEngine.create(DspConfig.mobile());
engine.setVolumeDb(-6.0);
engine.process(input, output, frameCount);
engine.dispose();
```

## Native library

Ship `libdsp_engine` built from the Psychic CMake project at the repo root. The native library name is retained for compatibility.

## Status

Skeleton / Phase 0 — high-level Dart API + manual FFI bindings.
