/// Flutter FFI bindings for [Psychic](https://github.com/Cryptic-Systems/Psychic).
///
/// High-level Dart API over the C ABI (`dsp_engine.h`).
///
/// **App identity:** This is a library plugin. When used inside Resonate it
/// inherits Resonate's application id (`com.aetherion.resonate` /
/// `com.Aetherion.Resonate`). It does **not** use a separate
/// `com.aetherion.resonatedsp` app id.
library dsp_engine;

export 'src/dsp_engine.dart';
export 'src/dsp_config.dart';
export 'src/dsp_exception.dart';
