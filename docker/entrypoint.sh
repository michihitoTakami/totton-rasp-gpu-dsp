#!/usr/bin/env bash
set -euo pipefail

CONFIG_PATH="${TOTTON_CONFIG_PATH:-/var/lib/totton-dsp/config.json}"
EQ_DIR="${TOTTON_EQ_DIR:-/var/lib/totton-dsp/eq}"

mkdir -p "$(dirname "$CONFIG_PATH")" "$EQ_DIR"

: "${TOTTON_ZMQ_ENDPOINT:=ipc:///tmp/totton_zmq.sock}"
: "${TOTTON_ZMQ_PUB_ENDPOINT:=ipc:///tmp/totton_zmq_pub.sock}"
: "${TOTTON_WEB_PORT:=8080}"

: "${TOTTON_FILTER_DIR:=/opt/totton-dsp/data/coefficients}"
: "${TOTTON_FILTER_RATIO:=2}"
: "${TOTTON_FILTER_PHASE:=min}"

alsa_in_override="${TOTTON_ALSA_IN-}"
alsa_out_override="${TOTTON_ALSA_OUT-}"
alsa_rate_override="${TOTTON_ALSA_RATE-}"
alsa_channels_override="${TOTTON_ALSA_CHANNELS-}"
alsa_format_override="${TOTTON_ALSA_FORMAT-}"
alsa_period_override="${TOTTON_ALSA_PERIOD-}"
alsa_buffer_override="${TOTTON_ALSA_BUFFER-}"

alsa_in=""
alsa_out=""
alsa_rate=""
alsa_channels=""
alsa_format=""
alsa_period=""
alsa_buffer=""

if [[ -f "$CONFIG_PATH" ]] && command -v jq >/dev/null 2>&1; then
  config_alsa_in=$(jq -r '.alsa.inputDevice // .alsaInputDevice // empty' "$CONFIG_PATH")
  config_alsa_out=$(jq -r '.alsa.outputDevice // .alsaOutputDevice // empty' "$CONFIG_PATH")
  config_alsa_rate=$(jq -r '.alsa.sampleRate // .alsaSampleRate // empty' "$CONFIG_PATH")
  config_alsa_channels=$(jq -r '.alsa.channels // .alsaChannels // empty' "$CONFIG_PATH")
  config_alsa_format=$(jq -r '.alsa.format // .alsaFormat // empty' "$CONFIG_PATH")
  config_alsa_period=$(jq -r '.alsa.periodFrames // empty' "$CONFIG_PATH")
  config_alsa_buffer=$(jq -r '.alsa.bufferFrames // empty' "$CONFIG_PATH")

  config_filter_dir=$(jq -r '.filter.directory // empty' "$CONFIG_PATH")
  config_filter_ratio=$(jq -r '.filter.ratio // empty' "$CONFIG_PATH")
  config_filter_phase=$(jq -r '.filter.phaseType // empty' "$CONFIG_PATH")

  if [[ -n "$config_alsa_in" ]]; then
    alsa_in="$config_alsa_in"
  fi
  if [[ -n "$config_alsa_out" ]]; then
    alsa_out="$config_alsa_out"
  fi
  if [[ "$config_alsa_rate" =~ ^[0-9]+$ ]] && [[ "$config_alsa_rate" -gt 0 ]]; then
    alsa_rate="$config_alsa_rate"
  fi
  if [[ "$config_alsa_channels" =~ ^[0-9]+$ ]] && [[ "$config_alsa_channels" -gt 0 ]]; then
    alsa_channels="$config_alsa_channels"
  fi
  if [[ -n "$config_alsa_format" ]]; then
    alsa_format="$config_alsa_format"
  fi
  if [[ "$config_alsa_period" =~ ^[0-9]+$ ]] && [[ "$config_alsa_period" -gt 0 ]]; then
    alsa_period="$config_alsa_period"
  fi
  if [[ "$config_alsa_buffer" =~ ^[0-9]+$ ]] && [[ "$config_alsa_buffer" -gt 0 ]]; then
    alsa_buffer="$config_alsa_buffer"
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

if [[ -n "$alsa_in_override" ]]; then
  alsa_in="$alsa_in_override"
fi
if [[ -n "$alsa_out_override" ]]; then
  alsa_out="$alsa_out_override"
fi
if [[ -n "$alsa_rate_override" ]]; then
  alsa_rate="$alsa_rate_override"
fi
if [[ -n "$alsa_channels_override" ]]; then
  alsa_channels="$alsa_channels_override"
fi
if [[ -n "$alsa_format_override" ]]; then
  alsa_format="$alsa_format_override"
fi
if [[ -n "$alsa_period_override" ]]; then
  alsa_period="$alsa_period_override"
fi
if [[ -n "$alsa_buffer_override" ]]; then
  alsa_buffer="$alsa_buffer_override"
fi

if [[ -z "$alsa_in" ]]; then
  alsa_in="hw:0,0"
fi
if [[ -z "$alsa_out" ]]; then
  alsa_out="hw:0,0"
fi

alsa_args=(--in "$alsa_in" --out "$alsa_out")

if [[ -n "$alsa_rate" ]]; then
  alsa_args+=(--rate "$alsa_rate")
fi
if [[ -n "$alsa_channels" ]]; then
  alsa_args+=(--channels "$alsa_channels")
fi
if [[ -n "$alsa_format" ]]; then
  alsa_args+=(--format "$alsa_format")
fi
if [[ -n "$alsa_period" ]]; then
  alsa_args+=(--period "$alsa_period")
fi
if [[ -n "$alsa_buffer" ]]; then
  alsa_args+=(--buffer "$alsa_buffer")
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
