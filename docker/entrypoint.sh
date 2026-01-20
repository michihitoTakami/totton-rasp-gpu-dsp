#!/usr/bin/env bash
set -euo pipefail

CONFIG_PATH="${TOTTON_CONFIG_PATH:-/var/lib/totton-dsp/config.json}"
EQ_DIR="${TOTTON_EQ_DIR:-/var/lib/totton-dsp/eq}"

mkdir -p "$(dirname "$CONFIG_PATH")" "$EQ_DIR"

: "${TOTTON_ZMQ_ENDPOINT:=ipc:///tmp/totton_zmq.sock}"
: "${TOTTON_ZMQ_PUB_ENDPOINT:=ipc:///tmp/totton_zmq_pub.sock}"
: "${TOTTON_WEB_PORT:=8080}"

: "${TOTTON_ALSA_IN:=hw:0,0}"
: "${TOTTON_ALSA_OUT:=hw:0,0}"
: "${TOTTON_FILTER_DIR:=/opt/totton-dsp/data/coefficients}"
: "${TOTTON_FILTER_RATIO:=2}"
: "${TOTTON_FILTER_PHASE:=min}"

alsa_args=(--in "$TOTTON_ALSA_IN" --out "$TOTTON_ALSA_OUT")

if [[ -n "${TOTTON_ALSA_RATE:-}" ]]; then
  alsa_args+=(--rate "$TOTTON_ALSA_RATE")
fi
if [[ -n "${TOTTON_ALSA_CHANNELS:-}" ]]; then
  alsa_args+=(--channels "$TOTTON_ALSA_CHANNELS")
fi
if [[ -n "${TOTTON_ALSA_FORMAT:-}" ]]; then
  alsa_args+=(--format "$TOTTON_ALSA_FORMAT")
fi
if [[ -n "${TOTTON_ALSA_PERIOD:-}" ]]; then
  alsa_args+=(--period "$TOTTON_ALSA_PERIOD")
fi
if [[ -n "${TOTTON_ALSA_BUFFER:-}" ]]; then
  alsa_args+=(--buffer "$TOTTON_ALSA_BUFFER")
fi

if [[ -n "${TOTTON_FILTER_PATH:-}" ]]; then
  alsa_args+=(--filter "$TOTTON_FILTER_PATH")
else
  alsa_args+=(--filter-dir "$TOTTON_FILTER_DIR" --ratio "$TOTTON_FILTER_RATIO" --phase "$TOTTON_FILTER_PHASE")
fi

/usr/local/bin/zmq_control_server --endpoint "$TOTTON_ZMQ_ENDPOINT" \
  --pub-endpoint "$TOTTON_ZMQ_PUB_ENDPOINT" &
ZMQ_PID=$!

/usr/local/bin/alsa_streamer "${alsa_args[@]}" &
ALSA_PID=$!

uvicorn web.main:app --host 0.0.0.0 --port "$TOTTON_WEB_PORT" &
WEB_PID=$!

cleanup() {
  kill "$ZMQ_PID" "$ALSA_PID" "$WEB_PID" 2>/dev/null || true
  wait "$ZMQ_PID" "$ALSA_PID" "$WEB_PID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

wait -n "$ZMQ_PID" "$ALSA_PID" "$WEB_PID"
