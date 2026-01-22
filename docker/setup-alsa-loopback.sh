#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage:
  setup-alsa-loopback.sh setup [--index N] [--pcm-substreams N]
  setup-alsa-loopback.sh teardown
  setup-alsa-loopback.sh status
  setup-alsa-loopback.sh list

Commands:
  setup     Load snd-aloop kernel module.
  teardown  Unload snd-aloop kernel module.
  status    Show whether Loopback card is present.
  list      Show ALSA playback/capture devices.
USAGE
}

require_root() {
  if [[ $EUID -ne 0 ]]; then
    echo "This command must be run as root (no sudo usage in tests)." >&2
    exit 1
  fi
}

cmd=${1:-}
case "$cmd" in
  setup)
    shift
    index=""
    substreams=""
    while [[ $# -gt 0 ]]; do
      case "$1" in
        --index)
          index="$2"
          shift 2
          ;;
        --pcm-substreams)
          substreams="$2"
          shift 2
          ;;
        -h|--help)
          usage
          exit 0
          ;;
        *)
          echo "Unknown option: $1" >&2
          usage
          exit 1
          ;;
      esac
    done

    args=(snd-aloop)
    if [[ -n "$index" ]]; then
      args+=("index=$index")
    fi
    if [[ -n "$substreams" ]]; then
      args+=("pcm_substreams=$substreams")
    fi

    require_root
    modprobe "${args[@]}"
    ;;
  teardown)
    require_root
    modprobe -r snd-aloop
    ;;
  status)
    if [[ -r /proc/asound/cards ]] && grep -q "Loopback" /proc/asound/cards; then
      echo "Loopback card is loaded."
      cat /proc/asound/cards
    else
      echo "Loopback card is not loaded."
      exit 1
    fi
    ;;
  list)
    if command -v aplay >/dev/null 2>&1; then
      aplay -l || true
    else
      echo "aplay not found."
    fi
    if command -v arecord >/dev/null 2>&1; then
      arecord -l || true
    else
      echo "arecord not found."
    fi
    ;;
  -h|--help|"")
    usage
    exit 0
    ;;
  *)
    echo "Unknown command: $cmd" >&2
    usage
    exit 1
    ;;
esac
