# Tube Emulator

A starting-point Audio Unit amp simulator voiced after a blackface-era (~'67)
Fender tube amp: clean headroom that breaks up into a warm, slightly overdriven
blues tone. Built with [JUCE](https://juce.com) and CMake.

## Signal chain

```
input drive
  -> 8x-oversampled asymmetric tube clipper   (even-order harmonics = warmth)
  -> DC blocker (20 Hz high-pass)
  -> Fender-voiced tone stack (Bass / Mid / Treble, mid slightly scooped)
  -> cabinet: convolution IR if loaded, else a 5 kHz speaker-rolloff low-pass
  -> output level
```

The interesting code is `processBlock()` and `tubeShape()` in
`Source/PluginProcessor.cpp`.

## Prerequisites

- macOS with the Xcode **Command Line Tools** (`xcode-select --install`)
- **CMake** ≥ 3.22 — not currently installed. Install with Homebrew:
  ```sh
  brew install cmake
  ```
  (No Projucer and no full Xcode install required — JUCE is fetched automatically
  by CMake on first configure.)

## Build

```sh
cd /Users/andrewcordivari/dev26/tube-emulator
cmake -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The first configure clones JUCE (a few minutes); later builds are fast.

`COPY_PLUGIN_AFTER_BUILD` installs the `.component` to
`~/Library/Audio/Plug-Ins/Components/`. Restart GarageBand (or run
`killall -9 AudioComponentRegistrar`) and it appears under
**Audio Units > AndrewCordivari > Tube Emulator**.

There's also a **Standalone** app under `build/` for quick iteration without a DAW.

## Knobs

| Knob   | What it does |
|--------|--------------|
| Drive  | How hard the signal is pushed into the tube nonlinearity |
| Bias   | Triode asymmetry — higher = more even harmonics / warmth |
| Bass   | Tone stack low shelf (120 Hz) |
| Mid    | Tone stack peak (500 Hz); defaults below 5 for the Fender scoop |
| Treble | Tone stack high shelf (3 kHz) |
| Level  | Output gain (dB) |

Use **Load Cabinet IR...** to load a real speaker impulse response (.wav/.aiff) —
this is the single biggest upgrade to realism. Search for free Fender 1x12 /
Deluxe cabinet IRs to test with.

## Next steps (roadmap)

1. Replace the voiced tone stack with the **real Fender passive RC transfer
   function** (Duncan Amps Tone Stack Calculator) — the knobs become interactive
   like the real amp.
2. Add **power-amp sag / compression** for pick-dynamic "bloom".
3. Add **spring reverb** (convolution of a real tank) and **tremolo** (LFO on
   gain) — 50% of the Fender identity.
4. A/B your DSP against a **SPICE model** of the real circuit (LiveSPICE).
5. Smooth parameter changes to remove any zipper noise on fast knob moves.
