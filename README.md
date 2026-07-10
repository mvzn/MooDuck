# MooDuck

MooDuck is a stereo ducking (sidechain compression) plugin built with [JUCE](https://juce.com), available as VST3 and AU.

## Features

- Sidechain-driven ducking with an exponential envelope follower (Peak / MS / RMS detection)
- Compressor/expander curve with soft-knee, plus a Hard Duck (∞:1 brick-wall) mode
- Wideband, multiband, and spectral sidechain detection modes
- Host-compensated lookahead (0–50 ms) and an uncompensated Xtreme lookahead (100 ms–10 s) for some reason
- Post-ducking EQ: a Pultec EQP-1A emulation (passive LC filter modeled as a Wave Digital Filter), with an optional tube saturation stage modeling the EQP-1A's makeup amplifier (Norman-Koren tube model, cascaded 12AX7 + 12AU7 stages with global negative feedback)
- Gain-reduction output parameter (`grOut`) for host automation
- Light/dark themed UI with IN / SC / GR level metering

## Building

Requires CMake ≥ 3.22 and a C++17 compiler. JUCE 8.0.6 is fetched automatically via CMake FetchContent — no local JUCE installation needed.

### Linux

Install the usual JUCE build dependencies first:

```bash
sudo apt-get install libasound2-dev libfreetype-dev libfontconfig1-dev \
    libgtk-3-dev libwebkit2gtk-4.1-dev libcurl4-openssl-dev \
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev
```

```bash
cmake -B build-cmake
cmake --build build-cmake --config Release
```

### macOS

Requires the Xcode command line tools (`xcode-select --install`).

```bash
cmake -B build-cmake -G Xcode
cmake --build build-cmake --config Release
```

### Windows

Requires Visual Studio 2022 (or the Build Tools) with the "Desktop development with C++" workload.

```powershell
cmake -B build-cmake -G "Visual Studio 17 2022"
cmake --build build-cmake --config Release
```

The built plugin lands in `build-cmake/MooDuck_artefacts/<Config>/`, and is auto-installed to the OS's plugin folder (`COPY_PLUGIN_AFTER_BUILD`). To reinstall an already-built plugin without recompiling, use the installer scripts in `scripts/`:

```bash
./scripts/install.sh          # Linux / macOS
```
```powershell
.\scripts\install.ps1          # Windows
```

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
