#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
TMP_DIR=$(mktemp -d)

RATE_LIST=${RATE_LIST:-"48000"}
DURATION=${DURATION:-"3"}
CHANNELS=${CHANNELS:-"2"}
CONVERT_FORMAT=${CONVERT_FORMAT:-"S32_LE"}
STREAM_FORMAT=${STREAM_FORMAT:-"s32"}

cleanup() {
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

for rate in $RATE_LIST; do
  input_wav="$TMP_DIR/sine_1k_${rate}.wav"
  input_raw="$TMP_DIR/input_${rate}.raw"
  output_raw="$TMP_DIR/output_${rate}.raw"
  output_wav="$TMP_DIR/output_${rate}.wav"

  uv run python3 "$ROOT_DIR/scripts/test/generate_test_audio.py" \
    --out-dir "$TMP_DIR" \
    --rates "$rate" \
    --duration "$DURATION" \
    --channels "$CHANNELS"

  uv run python3 "$ROOT_DIR/scripts/test/convert_audio.py" wav-to-raw \
    --input "$input_wav" \
    --output "$input_raw" \
    --format "$CONVERT_FORMAT"

  "$ROOT_DIR/build/alsa_streamer" \
    --in-file "$input_raw" \
    --out-file "$output_raw" \
    --rate "$rate" \
    --channels "$CHANNELS" \
    --format "$STREAM_FORMAT"

  if [[ ! -s "$output_raw" ]]; then
    echo "Output raw file is empty: $output_raw" >&2
    exit 1
  fi

  uv run python3 "$ROOT_DIR/scripts/test/convert_audio.py" raw-to-wav \
    --input "$output_raw" \
    --output "$output_wav" \
    --format "$CONVERT_FORMAT" \
    --rate "$rate" \
    --channels "$CHANNELS"

  uv run python3 "$ROOT_DIR/scripts/test/validate_output.py" \
    --input "$input_wav" \
    --output "$output_wav"

  echo "Rate ${rate} Hz OK"
  echo
  sleep 1
  done
