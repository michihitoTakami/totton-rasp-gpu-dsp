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

if [[ -f "$CONFIG_PATH" ]] && command -v jq >/dev/null 2>&1; then
  config_alsa_in=$(jq -r '.alsa.inputDevice // empty' "$CONFIG_PATH")
  config_alsa_out=$(jq -r '.alsa.outputDevice // empty' "$CONFIG_PATH")
  config_alsa_rate=$(jq -r '.alsa.sampleRate // empty' "$CONFIG_PATH")
  config_alsa_channels=$(jq -r '.alsa.channels // empty' "$CONFIG_PATH")
  config_alsa_format=$(jq -r '.alsa.format // empty' "$CONFIG_PATH")
  config_alsa_period=$(jq -r '.alsa.periodFrames // empty' "$CONFIG_PATH")
  config_alsa_buffer=$(jq -r '.alsa.bufferFrames // empty' "$CONFIG_PATH")

  config_filter_dir=$(jq -r '.filter.directory // empty' "$CONFIG_PATH")
  config_filter_ratio=$(jq -r '.filter.ratio // empty' "$CONFIG_PATH")
  config_filter_phase=$(jq -r '.filter.phaseType // empty' "$CONFIG_PATH")

  if [[ -n "$config_alsa_in" ]]; then
    TOTTON_ALSA_IN="$config_alsa_in"
  fi
  if [[ -n "$config_alsa_out" ]]; then
    TOTTON_ALSA_OUT="$config_alsa_out"
  fi
  if [[ "$config_alsa_rate" =~ ^[0-9]+$ ]] && [[ "$config_alsa_rate" -gt 0 ]]; then
    TOTTON_ALSA_RATE="$config_alsa_rate"
  fi
  if [[ "$config_alsa_channels" =~ ^[0-9]+$ ]] && [[ "$config_alsa_channels" -gt 0 ]]; then
    TOTTON_ALSA_CHANNELS="$config_alsa_channels"
  fi
  if [[ -n "$config_alsa_format" ]]; then
    TOTTON_ALSA_FORMAT="$config_alsa_format"
  fi
  if [[ "$config_alsa_period" =~ ^[0-9]+$ ]] && [[ "$config_alsa_period" -gt 0 ]]; then
    TOTTON_ALSA_PERIOD="$config_alsa_period"
  fi
  if [[ "$config_alsa_buffer" =~ ^[0-9]+$ ]] && [[ "$config_alsa_buffer" -gt 0 ]]; then
    TOTTON_ALSA_BUFFER="$config_alsa_buffer"
  fi

  if [[ -n "$config_filter_dir" ]]; then
    TOTTON_FILTER_DIR="$config_filter_dir"
  fi
  if [[ "$config_filter_ratio" =~ ^[0-9]+$ ]] && [[ "$config_filter_ratio" -gt 0 ]]; then
    TOTTON_FILTER_RATIO="$config_filter_ratio"
  fi
  if [[ -n "$config_filter_phase" ]]; then
    if [[ "$config_filter_phase" == "minimum" ]]; then
      TOTTON_FILTER_PHASE="min"
    elif [[ "$config_filter_phase" == "linear" ]]; then
      TOTTON_FILTER_PHASE="linear"
    else
      TOTTON_FILTER_PHASE="$config_filter_phase"
    fi
  fi
fi

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

# Keep container alive as long as control plane is running.
wait "$ZMQ_PID"
