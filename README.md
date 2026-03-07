# Puddle

Puddle is a Disting NT plugin that lives in the same haunted alley as Fairfield Circuitry's Shallow Water, but it is not trying to cosplay as a perfect forensic clone.

It is a small unstable modulation box built out of three things that tend to make audio sound more interesting and less trustworthy:

- a short modulated delay line
- a random, slewed control voltage
- an envelope-followed low-pass stage on the wet path

The result is warble, drag, softening, motion, and the kind of low-key damage that sounds intentional if you say it with confidence.

## What It Does

Puddle processes mono audio through a bucket-brigade-style delay voice with random pitch/time drift. The wet signal is filtered, blended with the dry signal, and pushed through an output gain stage. Parameters are smoothed inside the DSP so turning knobs on the NT does not produce zipper-step nonsense.

This project is split cleanly:

- `puddle_dsp.h` / `puddle_dsp.cpp`: hardware-agnostic DSP core
- `nt_puddle.cpp`: Disting NT wrapper, UI, parameter mapping, memory setup
- `test_puddle.cpp`: native DSP tests
- `Makefile`: hardware build, desktop build, checks, and NT push

## Parameters

All performance parameters use two decimal places on the NT.

| Parameter | Range | Default | What It Actually Does |
| --- | --- | --- | --- |
| `RATE` | `0.00%` to `100.00%` | `50.00%` | Sets how often the random modulation target changes. Low values drift lazily. High values twitch more often. |
| `DAMP` | `0.00%` to `100.00%` | `50.00%` | Sets how quickly the random modulation slews toward each new target. Lower is more abrupt. Higher is softer and slower. |
| `DEPTH` | `0.00%` to `100.00%` | `50.00%` | Sets how far the delay time is pushed around by the random modulation. |
| `LPG` | `0.00%` to `100.00%` | `50.00%` | Controls how much the input envelope opens the wet low-pass filter. This is envelope following, not magic. Louder input opens the filter more. |
| `MIX` | `0.00%` to `100.00%` | `100.00%` | Dry/wet blend. The default is fully wet because the effect should show up without being asked twice. |
| `VOLUME` | `-60.00 dB` to `+24.00 dB` | `-10.00 dB` | Output gain after the dry/wet mix. |

## LPG, In Plain English

Yes, the `LPG` control performs envelope following.

The plugin tracks the absolute level of the input signal with a fast attack and slower release, then uses that envelope to modulate the cutoff of the wet low-pass filter. In other words:

- quiet input: darker wet path
- loud input: brighter wet path
- higher `LPG`: more of that behavior

What it does **not** currently do is act like a true low-pass gate in the amplitude sense. It is not closing a VCA. It is opening and closing brightness.

## Smoothing

All user-facing parameters are smoothed in the DSP with a short one-pole glide. That matters because raw parameter stepping on embedded hardware sounds cheap, and this plugin has no interest in sounding cheap for accidental reasons.

Current smoothing behavior:

- parameter resolution: `0.01`
- internal parameter smoothing: about `10 ms`
- smoothed at audio rate: `rate`, `damp`, `depth`, `lpg`, `mix`, `volume`

## Memory Layout

The memory split is deliberate.

- `SRAM`: wrapper object and light bookkeeping
- `DTC`: hot DSP state
- `DRAM`: delay buffer

Approximate instance memory:

- `SRAM`: ~`40 bytes`
- `DTC`: ~`136 bytes`
- `DRAM @ 48 kHz`: ~`9608 bytes`
- `DRAM @ 96 kHz`: ~`19208 bytes`

The delay line stays in `DRAM` because that is where large buffers belong. Moving it to `DTC` would be the embedded equivalent of solving a paper cut with a shotgun.

## Build

Clone with submodules, or initialize the API submodule after checkout.

```bash
git clone --recurse-submodules git@github.com:thorinside/puddle.git
cd puddle
```

If you already cloned it without submodules:

```bash
git submodule update --init --recursive
```

### Native DSP Tests

```bash
make test
```

This builds and runs `test_puddle.cpp` against the hardware-agnostic DSP layer.

### Hardware Build

```bash
make hardware
```

Output:

- `plugins/nt_puddle.o`

### Desktop Plugin Build

```bash
make plugin-test
```

This builds a native shared library for `nt_emu`.

### Symbol Check

```bash
make check
```

### Size Report

```bash
make size
```

## Push To The NT

If `ntpush` is installed and your NT is reachable:

```bash
make push
```

That builds the hardware object if needed and pushes `plugins/nt_puddle.o` straight to the module.

## Release Flow

GitHub Actions builds release artifacts on tags matching `v*`.

The workflow:

- runs the DSP tests
- builds the hardware plugin
- checks unresolved symbols
- packages the result as:
  - `programs/plug-ins/puddle/puddle.o`
- uploads `puddle-plugin.zip` to the GitHub release

If you want a release, cut a tag:

```bash
git tag v0.0.2
git push origin v0.0.2
```

## Current Character

The current DSP is intentionally simple and stable enough to live on the NT without drama:

- short modulated delay centered around `35 ms`
- random CV modulation with controlled slew
- envelope-followed wet low-pass filter
- low feedback for texture without runaway collapse
- deterministic output for the same input and seed

If you are expecting a one-to-one, every-component-accounted-for behavioral clone of the original pedal, this is not that. It is a compact interpretation with the right kind of bad attitude.

## Development Notes

Things that matter in this codebase:

- parameter order should stay stable unless you want preset breakage
- Disting audio/CV bus parameters are `1`-based
- `numFramesBy4` must be multiplied by `4`
- the DSP core is written to run without hardware dependencies
- the NT wrapper is where memory classes and host behavior belong

## License / Disclaimer

This project is an independent emulation effort for educational and personal use. It is inspired by the Fairfield Circuitry Shallow Water concept, but it is not affiliated with or endorsed by Fairfield Circuitry.
