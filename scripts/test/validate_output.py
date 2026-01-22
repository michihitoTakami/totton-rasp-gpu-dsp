#!/usr/bin/env python3
import argparse
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import signal


@dataclass
class ValidationResult:
    sample_rate_match: bool
    channels_match: bool
    correlation: float
    spectral_similarity: float
    rms_db_diff: float


def load_audio(path: Path) -> tuple[np.ndarray, int]:
    data, rate = sf.read(path, dtype="float64", always_2d=True)
    return data, rate


def mix_down(data: np.ndarray) -> np.ndarray:
    return np.mean(data, axis=1)


def align_signals(
    reference: np.ndarray,
    target: np.ndarray,
    rate: int,
    max_delay_ms: float,
) -> tuple[np.ndarray, np.ndarray, int]:
    max_samples = int(rate * max_delay_ms / 1000.0)
    ref = reference[:]
    tgt = target[:]

    length = min(len(ref), len(tgt), rate * 5)
    ref = ref[:length]
    tgt = tgt[:length]

    correlation = signal.correlate(tgt, ref, mode="full")
    lags = signal.correlation_lags(len(tgt), len(ref), mode="full")
    valid = np.where(np.abs(lags) <= max_samples)[0]
    best = valid[np.argmax(correlation[valid])]
    lag = lags[best]

    if lag > 0:
        aligned_ref = reference[:-lag] if lag < len(reference) else reference[:0]
        aligned_tgt = target[lag:]
    elif lag < 0:
        aligned_ref = reference[-lag:]
        aligned_tgt = target[:lag] if lag != 0 else target
    else:
        aligned_ref = reference
        aligned_tgt = target

    return aligned_ref, aligned_tgt, int(lag)


def compute_metrics(
    reference: np.ndarray,
    target: np.ndarray,
) -> tuple[float, float, float]:
    length = min(len(reference), len(target))
    if length == 0:
        return 0.0, 0.0
    ref = reference[:length]
    tgt = target[:length]

    corr = float(np.corrcoef(ref, tgt)[0, 1])

    window = np.hanning(length)
    ref_fft = np.fft.rfft(ref * window)
    tgt_fft = np.fft.rfft(tgt * window)
    ref_mag = np.abs(ref_fft)
    tgt_mag = np.abs(tgt_fft)
    denom = np.linalg.norm(ref_mag) * np.linalg.norm(tgt_mag)
    spectral_similarity = float(np.dot(ref_mag, tgt_mag) / denom) if denom else 0.0

    ref_rms = np.sqrt(np.mean(ref ** 2)) + 1e-12
    tgt_rms = np.sqrt(np.mean(tgt ** 2)) + 1e-12
    rms_db = 20.0 * np.log10(tgt_rms / ref_rms)

    return corr, spectral_similarity, rms_db


def validate(
    input_path: Path,
    output_path: Path,
    min_correlation: float,
    min_spectral_similarity: float,
    max_rms_db_diff: float,
    max_delay_ms: float,
) -> ValidationResult:
    input_data, input_rate = load_audio(input_path)
    output_data, output_rate = load_audio(output_path)

    sample_rate_match = input_rate == output_rate
    channels_match = input_data.shape[1] == output_data.shape[1]

    input_mono = mix_down(input_data)
    output_mono = mix_down(output_data)

    aligned_input, aligned_output, lag = align_signals(
        input_mono, output_mono, input_rate, max_delay_ms
    )
    corr, spectral_similarity, rms_db = compute_metrics(aligned_input, aligned_output)

    print(f"Sample rate match: {sample_rate_match}")
    print(f"Channels match: {channels_match}")
    print(f"Alignment lag (samples): {lag}")
    print(f"Correlation: {corr:.4f}")
    print(f"Spectral similarity: {spectral_similarity:.4f}")
    print(f"RMS dB diff: {rms_db:.2f} dB")

    ok = (
        sample_rate_match
        and channels_match
        and corr >= min_correlation
        and spectral_similarity >= min_spectral_similarity
        and abs(rms_db) <= max_rms_db_diff
    )
    if not ok:
        raise SystemExit(1)

    return ValidationResult(
        sample_rate_match=sample_rate_match,
        channels_match=channels_match,
        correlation=corr,
        spectral_similarity=spectral_similarity,
        rms_db_diff=rms_db,
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Validate recorded output audio.")
    parser.add_argument("--input", required=True, help="Input reference WAV")
    parser.add_argument("--output", required=True, help="Recorded output WAV")
    parser.add_argument("--min-correlation", type=float, default=0.7)
    parser.add_argument("--min-spectral-similarity", type=float, default=0.8)
    parser.add_argument("--max-rms-db-diff", type=float, default=6.0)
    parser.add_argument("--max-delay-ms", type=float, default=50.0)
    args = parser.parse_args()

    validate(
        Path(args.input),
        Path(args.output),
        args.min_correlation,
        args.min_spectral_similarity,
        args.max_rms_db_diff,
        args.max_delay_ms,
    )


if __name__ == "__main__":
    main()
