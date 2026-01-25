# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Language
Think in English and answer in Japanese.

## Project Summary

Totton Raspberry Pi GPU DSP is a minimal Vulkan-based GPU upsampler package for Raspberry Pi.
It combines Vulkan (VkFFT) FIR convolution, OPRA-style EQ, ZeroMQ control, and a lightweight
FastAPI/Jinja2 Web UI/API so the system can be reused on Pi hardware.

## Architecture

### Control Plane (Python/FastAPI)
- Filter generation and analysis scripts in `scripts/`
- Web UI/API in `web/` (FastAPI + Jinja2)
- EQ profile management in `data/EQ/`

### Data Plane (C++/Vulkan)
- Vulkan/VkFFT upsampling engine in `src/vulkan/`
- ALSA streaming app in `src/alsa/`
- ZeroMQ control server in `src/zmq/`

## Key Constraints
- Minimum-phase FIR is the default (no pre-ringing).
- Bundled filters target 80k taps with 44.1k/48k families (2x/4x/8x/16x).

## Directory Layout

```
.
├── README.md              # User-facing documentation
├── CLAUDE.md              # AI development guide (this file)
├── AGENTS.md              # AI collaboration guidelines
├── CMakeLists.txt         # Build configuration
├── src/                   # C++ sources (Vulkan/ALSA/ZeroMQ)
│   ├── alsa/
│   ├── audio/
│   ├── io/
│   ├── vulkan/
│   └── zmq/
├── include/               # C++ headers
├── scripts/               # Python tools and helpers
├── data/
│   ├── coefficients/      # FIR filter coefficients
│   └── EQ/                # EQ profiles
├── docs/                  # Documentation
├── web/                   # FastAPI + Jinja2 templates
├── tests/                 # Test assets and smoke tests
└── build/                 # Build output (generated)
```

## Build & Run

### Filter generation
```bash
uv sync
uv run python -m scripts.filters.generate_minimum_phase \
  --generate-all --taps 80000 --kaiser-beta 25 --stopband-attenuation 140
```

### Build
```bash
cmake -B build -DENABLE_VULKAN=ON -DUSE_VKFFT=ON -DENABLE_ALSA=ON -DENABLE_ZMQ=ON
cmake --build build -j$(nproc)
```

### Run (examples)
```bash
./build/alsa_streamer --in hw:0 --out hw:0
./build/zmq_control_server --endpoint ipc:///tmp/totton_zmq.sock
```

## Web UI Guidelines

Jinja2 components already exist under `web/templates/components/`.
Always import and reuse them instead of hardcoding UI elements.

- `btn_primary(text, icon)`
- `card_panel(title)`
- `slider_input(value)`
