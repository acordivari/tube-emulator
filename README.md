# Tube Emulator

A starting-point Audio Unit amp simulator voiced after a late-'60s (~'67)
Fender tube amp: clean headroom that breaks up into a warm, slightly overdriven
blues tone. Built with [JUCE](https://juce.com) and CMake.

## Signal chain

```
input drive
  -> 8x-oversampled asymmetric tube clipper   (even-order harmonics = warmth)
  -> DC blocker (20 Hz high-pass)
  -> real 3rd-order Fender ('59 Bassman) tone stack + makeup gain
  -> cabinet: convolution IR if loaded, else a 5 kHz speaker-rolloff low-pass
  -> reverb: convolution with a loaded reverb IR, else the algorithmic spring
     (dispersive allpass cascade in a damped feedback loop)
  -> tremolo (LFO amplitude modulation)
  -> output level -> output soft clip
```

The interesting code is `processBlock()` and `tubeShape()` in
`Source/PluginProcessor.cpp`, and the tone-stack circuit model in
`Source/ToneStack.h`.

## Tone stack

`Source/ToneStack.h` implements the actual passive Fender tone-stack circuit:
the analog transfer function `H(s)` (whose coefficients depend on all three pot
positions) is discretized with the bilinear transform into a 3rd-order IIR
filter. The Bass/Mid/Treble knobs therefore interact exactly like the real amp,
and the famous midrange scoop falls out of the circuit rather than being faked.

Coefficients and component values are from Yeh & Smith, *Discretization of the
'59 Fender Bassman Tone Stack* (DAFx-06). Verify them offline with:

```sh
c++ -std=c++17 -O2 tools/tonestack_check.cpp -o /tmp/ts && /tmp/ts
```

which prints the magnitude response (look for the ~400-700 Hz scoop).

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
| Sag    | Power-amp supply droop: 0 = stiff (solid-state feel), 10 = loose/spongy. Dynamic — best heard on picking transients |
| Reverb | Spring reverb wet mix (0 = dry) |
| Rate   | Tremolo speed in Hz |
| Depth  | Tremolo intensity (0 = tremolo off) |
| Level  | Output gain (dB) |

Use **Load Cabinet IR...** to load a real speaker impulse response (.wav/.aiff) —
this is the single biggest upgrade to realism. Search for free Fender 1x12 /
Deluxe cabinet IRs to test with.

Use **Load Reverb IR...** to convolve a real reverb/spring-tank impulse response
for the wet path instead of the algorithmic spring (the **Reverb** knob still
sets the wet mix). With no reverb IR loaded it falls back to the built-in spring.

**Test Tone** generates an internal signal (Sine / Saw / Noise at ~110 Hz) that
runs through the whole chain, so you can audition the amp without a guitar
plugged in. Saw is the best all-rounder; Noise makes the tone-stack scoop
obvious. Turn it off before recording real input.

## Next steps (roadmap)

1. ~~Real Fender passive tone-stack transfer function.~~ ✅ Done — see
   `Source/ToneStack.h`.
2. ~~Power-amp sag / compression for pick-dynamic "bloom".~~ ✅ Done — see
   `applySag()` in `PluginProcessor.cpp` (verify with `tools/sag_check.cpp`).
3. ~~Spring reverb and tremolo.~~ ✅ Done — algorithmic dispersive spring
   (`Source/SpringReverb.h`, stability-checked by `tools/reverb_check.cpp`), an
   LFO tremolo, and a **Load Reverb IR...** convolution path for using a real
   tank's impulse response.
4. A/B your DSP against a **SPICE model** of the real circuit (LiveSPICE).
5. Smooth parameter changes to remove any zipper noise on fast knob moves; add
   an **audio-taper** mapping for the Bass/Treble knobs to match pot feel.
6. Couple sag into the clipper headroom (right now it's a post-stage supply
   droop) so the amp also distorts a touch more as the rail sags.
