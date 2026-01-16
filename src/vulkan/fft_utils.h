#pragma once

#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace totton::vulkan::fft {

inline bool IsPowerOfTwo(std::size_t value) {
  return value != 0 && (value & (value - 1)) == 0;
}

inline void BitReverse(std::vector<std::complex<float>> &data) {
  const std::size_t n = data.size();
  std::size_t j = 0;
  for (std::size_t i = 1; i < n; ++i) {
    std::size_t bit = n >> 1;
    while (j & bit) {
      j ^= bit;
      bit >>= 1;
    }
    j ^= bit;
    if (i < j) {
      std::swap(data[i], data[j]);
    }
  }
}

inline void Fft(std::vector<std::complex<float>> &data, bool inverse) {
  const std::size_t n = data.size();
  if (n <= 1) {
    return;
  }

  BitReverse(data);

  constexpr float kPi = 3.14159265358979323846f;
  for (std::size_t len = 2; len <= n; len <<= 1) {
    const float angle =
        (inverse ? 2.0f : -2.0f) * kPi / static_cast<float>(len);
    const std::complex<float> wlen(std::cos(angle), std::sin(angle));
    for (std::size_t i = 0; i < n; i += len) {
      std::complex<float> w(1.0f, 0.0f);
      for (std::size_t j = 0; j < len / 2; ++j) {
        const std::complex<float> u = data[i + j];
        const std::complex<float> v = data[i + j + len / 2] * w;
        data[i + j] = u + v;
        data[i + j + len / 2] = u - v;
        w *= wlen;
      }
    }
  }

  if (inverse) {
    const float invN = 1.0f / static_cast<float>(n);
    for (auto &value : data) {
      value *= invN;
    }
  }
}

} // namespace totton::vulkan::fft
