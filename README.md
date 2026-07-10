# MooDuck

MooDuck is a stereo ducking (sidechain compression) plugin built with [JUCE](https://juce.com), available as VST3, AU, and a standalone app.

## Features

- Sidechain-driven ducking with an exponential envelope follower (Peak / MS / RMS detection)
- Compressor/expander curve with soft-knee, plus a Hard Duck (∞:1 brick-wall) mode
- Wideband, multiband, and spectral sidechain detection modes
- Host-compensated lookahead (0–50 ms) and an uncompensated Xtreme lookahead (100 ms–10 s) for creative delay effects
- Post-ducking EQ (low shelf, parametric mid, high shelf)
- Gain-reduction output parameter (`grOut`) for host automation
- Light/dark themed UI with IN / SC / GR level metering

## Building

Requires CMake ≥ 3.22 and a C++17 compiler. JUCE 8.0.6 is fetched automatically via CMake FetchContent — no local JUCE installation needed.

```bash
cmake -B build-cmake
cmake --build build-cmake --config Release
```

The built plugin lands in `build-cmake/MooDuck_artefacts/`.

On Linux you'll need the usual JUCE build dependencies (ALSA, FreeType, fontconfig, GTK3, X11/Xrandr/Xinerama/Xcursor, libcurl, WebKit2GTK).

### Tests

A headless DSP test suite (no plugin host required) lives in `tests/`. It compiles against a local copy of the JUCE modules:

```bash
make -C tests JUCE_MODULES=/path/to/JUCE/modules
./tests/runtests
```

A host-level latency/PDC regression test is also built as part of the CMake project (`MooDuckLatencyTest` target, disable with `-DMOODUCK_BUILD_TESTS=OFF`).

## License

MooDuck is free software, licensed under the [GNU Affero General Public License v3.0](LICENSE) (AGPL-3.0).

Third-party components:

- [JUCE](https://github.com/juce-framework/JUCE) — AGPL-3.0
- [chowdsp_wdf](https://github.com/Chowdhury-DSP/chowdsp_wdf) (vendored in `Source/vendor/`) — BSD-3-Clause
- [IBM Plex Sans Condensed](https://github.com/IBM/plex) fonts (bundled in `Resources/fonts/`) — SIL Open Font License 1.1
