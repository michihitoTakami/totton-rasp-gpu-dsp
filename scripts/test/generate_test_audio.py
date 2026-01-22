#!/usr/bin/env python3
import argparse
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import signal


def generate_sine(
    freq_hz: float, rate: int, duration: float, amplitude: float
) -> np.ndarray:
    t = np.arange(int(rate * duration), dtype=np.float64) / rate
    data = amplitude * np.sin(2.0 * np.pi * freq_hz * t)
    return data


def generate_sweep(
    start_hz: float, end_hz: float, rate: int, duration: float, amplitude: float
) -> np.ndarray:
    t = np.arange(int(rate * duration), dtype=np.float64) / rate
    data = amplitude * signal.chirp(
        t, f0=start_hz, f1=end_hz, t1=duration, method="logarithmic"
    )
    return data


def generate_white_noise(rate: int, duration: float, amplitude: float) -> np.ndarray:
    rng = np.random.default_rng(0)
    data = amplitude * rng.standard_normal(int(rate * duration))
    return data


def write_wav(path: Path, data: np.ndarray, rate: int, channels: int) -> None:
    if channels == 2:
        data = np.column_stack([data, data])
    elif channels != 1:
        data = np.repeat(data[:, None], channels, axis=1)
    sf.write(path, data, rate, subtype="PCM_32")


def parse_rates(text: str) -> list[int]:
    return [int(token.strip()) for token in text.split(",") if token.strip()]


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate test audio WAV files.")
    parser.add_argument("--out-dir", required=True, help="Output directory")
    parser.add_argument(
        "--rates",
        default="44100,48000,88200,96000",
        help="Comma-separated sample rates",
    )
    parser.add_argument("--duration", type=float, default=3.0, help="Duration seconds")
    parser.add_argument("--amplitude", type=float, default=0.2, help="Signal amplitude")
    parser.add_argument("--channels", type=int, default=2, help="Channel count")
    args = parser.parse_args()

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    rates = parse_rates(args.rates)
    for rate in rates:
        sine_440 = generate_sine(440.0, rate, args.duration, args.amplitude)
        sine_1k = generate_sine(1000.0, rate, args.duration, args.amplitude)
        sweep = generate_sweep(20.0, 20000.0, rate, args.duration, args.amplitude)
        noise = generate_white_noise(rate, args.duration, args.amplitude)

        write_wav(out_dir / f"sine_440_{rate}.wav", sine_440, rate, args.channels)
        write_wav(out_dir / f"sine_1k_{rate}.wav", sine_1k, rate, args.channels)
        write_wav(out_dir / f"sweep_20_20k_{rate}.wav", sweep, rate, args.channels)
        write_wav(out_dir / f"white_noise_{rate}.wav", noise, rate, args.channels)


if __name__ == "__main__":
    main()
