#!/usr/bin/env python3
import argparse
from pathlib import Path

import numpy as np
import soundfile as sf


FORMAT_MAP = {
    "S16_LE": (np.dtype("<i2"), "PCM_16"),
    "S32_LE": (np.dtype("<i4"), "PCM_32"),
}


def wav_to_raw(input_path: Path, output_path: Path, fmt: str) -> None:
    if fmt not in FORMAT_MAP:
        raise ValueError(f"Unsupported format: {fmt}")
    dtype, _ = FORMAT_MAP[fmt]
    data, _ = sf.read(input_path, dtype=dtype, always_2d=True)
    data.astype(dtype).tofile(output_path)


def raw_to_wav(
    input_path: Path, output_path: Path, fmt: str, rate: int, channels: int
) -> None:
    if fmt not in FORMAT_MAP:
        raise ValueError(f"Unsupported format: {fmt}")
    dtype, subtype = FORMAT_MAP[fmt]
    raw = np.fromfile(input_path, dtype=dtype)
    if raw.size % channels != 0:
        raise ValueError("Raw data length is not divisible by channels")
    data = raw.reshape(-1, channels)
    sf.write(output_path, data, rate, subtype=subtype)


def main() -> None:
    parser = argparse.ArgumentParser(description="Convert WAV<->RAW PCM audio.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    wav_to_raw_parser = subparsers.add_parser("wav-to-raw")
    wav_to_raw_parser.add_argument("--input", required=True)
    wav_to_raw_parser.add_argument("--output", required=True)
    wav_to_raw_parser.add_argument("--format", default="S32_LE")

    raw_to_wav_parser = subparsers.add_parser("raw-to-wav")
    raw_to_wav_parser.add_argument("--input", required=True)
    raw_to_wav_parser.add_argument("--output", required=True)
    raw_to_wav_parser.add_argument("--format", default="S32_LE")
    raw_to_wav_parser.add_argument("--rate", type=int, required=True)
    raw_to_wav_parser.add_argument("--channels", type=int, required=True)

    args = parser.parse_args()

    if args.command == "wav-to-raw":
        wav_to_raw(Path(args.input), Path(args.output), args.format)
        return
    if args.command == "raw-to-wav":
        raw_to_wav(
            Path(args.input),
            Path(args.output),
            args.format,
            args.rate,
            args.channels,
        )
        return


if __name__ == "__main__":
    main()
