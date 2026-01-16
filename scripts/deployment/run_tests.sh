#!/usr/bin/env bash
set -euo pipefail

if [ ! -d build ]; then
  "${CMAKE_BIN:-/usr/bin/cmake}" -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DENABLE_VULKAN=OFF \
    -DUSE_VKFFT=OFF
fi

/usr/bin/ctest --test-dir build --output-on-failure --parallel 2
