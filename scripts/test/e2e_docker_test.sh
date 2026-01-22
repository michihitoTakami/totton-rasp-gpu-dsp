#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
COMPOSE_FILE="$ROOT_DIR/docker/docker-compose.test.yml"
TMP_DIR=$(mktemp -d)

RATE_LIST=${RATE_LIST:-"48000"}
DURATION=${DURATION:-"3"}
CHANNELS=${CHANNELS:-"2"}
CONVERT_FORMAT=${CONVERT_FORMAT:-"S32_LE"}
STREAM_FORMAT=${STREAM_FORMAT:-"s32"}

cleanup() {
  docker compose -f "$COMPOSE_FILE" down >/dev/null 2>&1 || true
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

docker compose -f "$COMPOSE_FILE" build totton-dsp-test

for rate in $RATE_LIST; do
  export TOTTON_TEST_DIR="$TMP_DIR"

  uv run python3 "$ROOT_DIR/scripts/test/generate_test_audio.py" \
    --out-dir "$TMP_DIR" \
    --rates "$rate" \
    --duration "$DURATION" \
    --channels "$CHANNELS"

  input_wav="$TMP_DIR/sine_1k_${rate}.wav"
  input_raw_host="$TMP_DIR/input_${rate}.raw"
  output_raw_host="$TMP_DIR/output_${rate}.raw"
  output_wav="$TMP_DIR/output_${rate}.wav"
  container_test_dir="/opt/totton-dsp/test"
  input_raw_container="$container_test_dir/input_${rate}.raw"
  output_raw_container="$container_test_dir/output_${rate}.raw"

  uv run python3 "$ROOT_DIR/scripts/test/convert_audio.py" wav-to-raw \
    --input "$input_wav" \
    --output "$input_raw_host" \
    --format "$CONVERT_FORMAT"

  docker compose -f "$COMPOSE_FILE" run --rm --no-deps --entrypoint \
    /usr/local/bin/alsa_streamer totton-dsp-test \
    --in-file "$input_raw_container" \
    --out-file "$output_raw_container" \
    --rate "$rate" \
    --channels "$CHANNELS" \
    --format "$STREAM_FORMAT"

  uv run python3 "$ROOT_DIR/scripts/test/convert_audio.py" raw-to-wav \
    --input "$output_raw_host" \
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
