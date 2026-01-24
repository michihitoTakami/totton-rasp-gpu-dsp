# Coefficient Files

This directory ships the pre-generated FIR filters used by the Vulkan upsampler.
Each filter consists of a float32 LE `.bin` file and a JSON sidecar.
See `docs/filter_format.md` for the schema (additional metadata fields are allowed).

## Bundled filters (80k taps + alignment padding, minimum phase)
- `filter_44k_2x_80000_min_phase.{bin,json}`
- `filter_44k_4x_80000_min_phase.{bin,json}`
- `filter_44k_8x_80000_min_phase.{bin,json}`
- `filter_44k_16x_80000_min_phase.{bin,json}`
- `filter_48k_2x_80000_min_phase.{bin,json}`
- `filter_48k_4x_80000_min_phase.{bin,json}`
- `filter_48k_8x_80000_min_phase.{bin,json}`
- `filter_48k_16x_80000_min_phase.{bin,json}`

## Regeneration command
```bash
uv run python -m scripts.filters.generate_minimum_phase --generate-all --taps 80000 --kaiser-beta 25 --stopband-attenuation 140
```

## License / Notes
- These coefficients are generated from repository scripts and follow the repository license.
- No third-party datasets are embedded in the filter taps.
- The on-disk tap count may be padded so `(taps - 1)` is divisible by the upsample ratio.
- If you change tap count or phase, re-check stopband attenuation and gain normalization.
- Current target: Kaiser β=25, stopband attenuation 140 dB (temporary for 80k taps).
