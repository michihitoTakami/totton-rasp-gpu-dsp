# Filter File Format (bin + json)

The Vulkan upsampler expects a JSON sidecar that describes the raw float32 tap file.

## JSON schema (minimal)

```json
{
  "coefficients_bin": "minimum_phase_44k.bin",
  "taps": 640000,
  "fft_size": 65536,
  "block_size": 32768,
  "upsample_factor": 1
}
```

## Notes

- `coefficients_bin` is a path to a float32 LE binary file.
- `taps` must be `<= fft_size` for the minimal overlap-save path.
- `fft_size` must be a power of two.
- `block_size` must be smaller than `fft_size`.
- `upsample_factor` is reserved for future use and defaults to `1`.
