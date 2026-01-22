#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
COMPOSE_FILE="$ROOT_DIR/docker/docker-compose.test.yml"
TMP_DIR=$(mktemp -d)

RATE_LIST=${RATE_LIST:-"48000"}
DURATION=${DURATION:-"3"}
CHANNELS=${CHANNELS:-"2"}
ALSA_FORMAT=${ALSA_FORMAT:-"S32_LE"}

LOOPBACK_PLAYBACK_DEVICE=${LOOPBACK_PLAYBACK_DEVICE:-"hw:Loopback,0,0"}
LOOPBACK_CAPTURE_DEVICE=${LOOPBACK_CAPTURE_DEVICE:-"hw:Loopback,0,0"}
CONTAINER_ALSA_IN=${CONTAINER_ALSA_IN:-"hw:Loopback,1,0"}
CONTAINER_ALSA_OUT=${CONTAINER_ALSA_OUT:-"hw:Loopback,1,0"}

cleanup() {
  docker compose -f "$COMPOSE_FILE" down >/dev/null 2>&1 || true
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

"$ROOT_DIR/docker/setup-alsa-loopback.sh" setup --pcm-substreams 2

for rate in $RATE_LIST; do
  export TOTTON_ALSA_RATE="$rate"
  export TOTTON_ALSA_IN="$CONTAINER_ALSA_IN"
  export TOTTON_ALSA_OUT="$CONTAINER_ALSA_OUT"
  export TOTTON_ALSA_CHANNELS="$CHANNELS"
  export TOTTON_ALSA_FORMAT="$ALSA_FORMAT"
  export TOTTON_FILTER_RATIO="${TOTTON_FILTER_RATIO:-1}"

  compose_args=("-f" "$COMPOSE_FILE" "up" "-d")
  if [[ "${DOCKER_COMPOSE_BUILD:-0}" == "1" ]]; then
    compose_args+=("--build")
  fi
  docker compose "${compose_args[@]}"
  sleep 2

  python3 "$ROOT_DIR/scripts/test/generate_test_audio.py" \
    --out-dir "$TMP_DIR" \
    --rates "$rate" \
    --duration "$DURATION" \
    --channels "$CHANNELS"

  input_file="$TMP_DIR/sine_1k_${rate}.wav"
  output_file="$TMP_DIR/output_${rate}.wav"

  arecord -D "$LOOPBACK_CAPTURE_DEVICE" -f "$ALSA_FORMAT" -r "$rate" -c "$CHANNELS" \
    -d "$DURATION" "$output_file" &
  capture_pid=$!
  sleep 0.2

  aplay -D "$LOOPBACK_PLAYBACK_DEVICE" -f "$ALSA_FORMAT" -r "$rate" -c "$CHANNELS" \
    "$input_file"

  wait "$capture_pid"

  python3 "$ROOT_DIR/scripts/test/validate_output.py" \
    --input "$input_file" \
    --output "$output_file"

  docker compose -f "$COMPOSE_FILE" down
  echo "Rate ${rate} Hz OK"
  echo
  sleep 1
  done
