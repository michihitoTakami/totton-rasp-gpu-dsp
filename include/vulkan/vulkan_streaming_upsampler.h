#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace totton::vulkan {

struct FilterConfig {
  std::string coefficientsPath;
  std::size_t taps = 0;
  std::size_t fftSize = 0;
  std::size_t blockSize = 0;
  std::size_t upsampleFactor = 1;
};

class VulkanStreamingUpsampler {
public:
  VulkanStreamingUpsampler();

  bool LoadFilter(const std::string &jsonPath, std::string *errorMessage);
  std::vector<float> ProcessBlock(const float *input, std::size_t count);
  void Reset();

  const FilterConfig &GetConfig() const;

private:
  bool LoadFilterConfig(const std::string &jsonPath, FilterConfig *config,
                        std::string *errorMessage);
  bool LoadCoefficients(const FilterConfig &config, std::string *errorMessage);
  bool PrepareSpectrum(std::string *errorMessage);

  FilterConfig config_{};
  std::vector<float> coefficients_{};
  std::vector<float> overlap_{};
  std::vector<std::complex<float>> filterSpectrum_{};
  bool initialized_ = false;
};

} // namespace totton::vulkan
