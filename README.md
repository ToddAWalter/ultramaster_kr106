# Ultramaster KR-106

A synthesizer plugin emulating the Roland Juno-106, built with [iPlug2](https://github.com/iPlug2/iPlug2).

6-voice polyphonic with per-voice analog variance, TPT ladder filter with OTA saturation,
BBD chorus emulation, arpeggiator, portamento/unison mode, and 205 factory presets.
Builds as AU, VST3, CLAP, and Standalone on macOS.

See [docs/DSP_ARCHITECTURE.md](docs/DSP_ARCHITECTURE.md) for a detailed writeup of the
signal chain and emulation techniques.

## Prerequisites

- macOS with **Xcode** installed (not just Command Line Tools)
- **iPlug2** cloned as a sibling directory at `../iPlug2`

## iPlug2 Setup (first time)

```bash
cd ~/src   # or wherever this repo lives
git clone https://github.com/iPlug2/iPlug2
cd iPlug2
git submodule update --init --recursive
cd Dependencies/Build/src
./download-prebuilt-libs.sh
cd ../../../IPlug/Extras
./download-iplug-sdks.sh
```

This project is tested against iPlug2 `v1.0.0-beta.508` (commit `3b32d40`). Any recent commit from `main` should work.

## Building

From the repo root:

```bash
make app       # Standalone .app
make vst3      # VST3 plugin
make au        # Audio Unit (AUv2)
make clap      # CLAP plugin
make all       # All formats at once
```

Default build configuration is `Debug`. For a release build:

```bash
CONFIG=Release make vst3
```

Built plugins are installed to the standard macOS locations (`~/Library/Audio/Plug-Ins/`).

Run `make help` for all available targets.

## Project Structure

```
KR106.cpp / KR106.h          Plugin class (UI layout, MIDI routing, param handling)
KR106Controls.h               Custom iPlug2 controls (keyboard, sliders, scope, etc.)
KR106_Presets.h                205 factory presets (auto-generated)
config.h                       Plugin metadata and resource declarations

DSP/
  KR106_DSP.h                 Top-level DSP orchestrator, HPF, signal routing
  KR106Voice.h                Per-voice: VCF, ADSR, oscillator mixing, portamento
  KR106Oscillators.h          PolyBLEP saw, pulse, sub, noise generators
  KR106Chorus.h               MN3009 BBD chorus with Hermite interpolation
  KR106LFO.h                  Global triangle LFO with delay envelope
  KR106Arpeggiator.h          Note sequencer (Up / Down / Up-Down)

docs/
  DSP_ARCHITECTURE.md         Detailed DSP design documentation
  HOLD_ARP_FLOW.md            Hold + arpeggiator interaction flow

tools/preset-gen/             Original patch files and conversion utilities
```

## License

This project is licensed under the [GNU General Public License v3.0](LICENSE).
