#include "vulkan/vulkan_streaming_upsampler.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "fft_utils.h"

namespace totton::vulkan {
namespace {

bool ExtractJsonString(const std::string &json, const std::string &key,
                       std::string *out) {
  const std::string pattern = "\"" + key + "\"";
  std::size_t pos = json.find(pattern);
  if (pos == std::string::npos) {
    return false;
  }
  pos = json.find(':', pos + pattern.size());
  if (pos == std::string::npos) {
    return false;
  }
  pos = json.find('"', pos);
  if (pos == std::string::npos) {
    return false;
  }
  const std::size_t end = json.find('"', pos + 1);
  if (end == std::string::npos) {
    return false;
  }
  *out = json.substr(pos + 1, end - pos - 1);
  return true;
}

bool ExtractJsonUnsigned(const std::string &json, const std::string &key,
                         std::size_t *out) {
  const std::string pattern = "\"" + key + "\"";
  std::size_t pos = json.find(pattern);
  if (pos == std::string::npos) {
    return false;
  }
  pos = json.find(':', pos + pattern.size());
  if (pos == std::string::npos) {
    return false;
  }
  ++pos;
  while (pos < json.size() &&
         std::isspace(static_cast<unsigned char>(json[pos]))) {
    ++pos;
  }
  std::size_t end = pos;
  while (end < json.size() &&
         std::isdigit(static_cast<unsigned char>(json[end]))) {
    ++end;
  }
  if (end == pos) {
    return false;
  }
  *out = static_cast<std::size_t>(std::stoull(json.substr(pos, end - pos)));
  return true;
}

std::string ReadFileToString(const std::filesystem::path &path) {
  std::ifstream file(path);
  if (!file) {
    return {};
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

std::string BuildError(const std::string &message, const std::string &detail) {
  if (detail.empty()) {
    return message;
  }
  return message + ": " + detail;
}

} // namespace

VulkanStreamingUpsampler::VulkanStreamingUpsampler() = default;

bool VulkanStreamingUpsampler::LoadFilter(const std::string &jsonPath,
                                          std::string *errorMessage) {
  FilterConfig config;
  if (!LoadFilterConfig(jsonPath, &config, errorMessage)) {
    return false;
  }
  if (!LoadCoefficients(config, errorMessage)) {
    return false;
  }
  config_ = config;
  if (!PrepareSpectrum(errorMessage)) {
    return false;
  }
  initialized_ = true;
  return true;
}

std::vector<float> VulkanStreamingUpsampler::ProcessBlock(const float *input,
                                                          std::size_t count) {
  if (!initialized_ || !input) {
    return {};
  }

  const std::size_t blockSize = config_.blockSize;
  const std::size_t fftSize = config_.fftSize;
  const std::size_t overlapSize = overlap_.size();

  std::vector<float> timeBuffer(fftSize, 0.0f);
  for (std::size_t i = 0; i < overlapSize; ++i) {
    timeBuffer[i] = overlap_[i];
  }
  const std::size_t copyCount = std::min(count, blockSize);
  for (std::size_t i = 0; i < copyCount; ++i) {
    timeBuffer[overlapSize + i] = input[i];
  }

  std::vector<std::complex<float>> freqBuffer(fftSize);
  for (std::size_t i = 0; i < fftSize; ++i) {
    freqBuffer[i] = std::complex<float>(timeBuffer[i], 0.0f);
  }

  fft::Fft(freqBuffer, false);
  for (std::size_t i = 0; i < fftSize; ++i) {
    freqBuffer[i] *= filterSpectrum_[i];
  }
  fft::Fft(freqBuffer, true);

  std::vector<float> output(blockSize, 0.0f);
  for (std::size_t i = 0; i < blockSize; ++i) {
    output[i] = freqBuffer[overlapSize + i].real();
  }

  overlap_.assign(timeBuffer.end() - static_cast<std::ptrdiff_t>(overlapSize),
                  timeBuffer.end());
  return output;
}

void VulkanStreamingUpsampler::Reset() {
  std::fill(overlap_.begin(), overlap_.end(), 0.0f);
}

const FilterConfig &VulkanStreamingUpsampler::GetConfig() const {
  return config_;
}

bool VulkanStreamingUpsampler::LoadFilterConfig(const std::string &jsonPath,
                                                FilterConfig *config,
                                                std::string *errorMessage) {
  const std::filesystem::path path(jsonPath);
  const std::string json = ReadFileToString(path);
  if (json.empty()) {
    if (errorMessage) {
      *errorMessage = BuildError("Failed to read filter config", jsonPath);
    }
    return false;
  }

  std::string binPath;
  if (!ExtractJsonString(json, "coefficients_bin", &binPath)) {
    if (errorMessage) {
      *errorMessage = "Missing coefficients_bin in filter config";
    }
    return false;
  }

  std::size_t taps = 0;
  std::size_t fftSize = 0;
  std::size_t blockSize = 0;
  ExtractJsonUnsigned(json, "taps", &taps);
  ExtractJsonUnsigned(json, "fft_size", &fftSize);
  ExtractJsonUnsigned(json, "block_size", &blockSize);
  ExtractJsonUnsigned(json, "upsample_factor", &config->upsampleFactor);

  if (taps == 0 || fftSize == 0 || blockSize == 0) {
    if (errorMessage) {
      *errorMessage = "taps/fft_size/block_size must be set and non-zero";
    }
    return false;
  }
  if (!fft::IsPowerOfTwo(fftSize)) {
    if (errorMessage) {
      *errorMessage = "fft_size must be power of two";
    }
    return false;
  }
  if (blockSize >= fftSize) {
    if (errorMessage) {
      *errorMessage = "block_size must be smaller than fft_size";
    }
    return false;
  }
  const std::size_t overlapSize = fftSize - blockSize;
  if (taps == 0 || overlapSize != (taps - 1)) {
    if (errorMessage) {
      *errorMessage =
          "block_size must satisfy fft_size - block_size == taps - 1";
    }
    return false;
  }

  std::filesystem::path bin = binPath;
  if (!bin.is_absolute()) {
    bin = path.parent_path() / bin;
  }

  config->coefficientsPath = bin.string();
  config->taps = taps;
  config->fftSize = fftSize;
  config->blockSize = blockSize;
  if (config->upsampleFactor == 0) {
    config->upsampleFactor = 1;
  }

  return true;
}

bool VulkanStreamingUpsampler::LoadCoefficients(const FilterConfig &config,
                                                std::string *errorMessage) {
  std::ifstream file(config.coefficientsPath, std::ios::binary);
  if (!file) {
    if (errorMessage) {
      *errorMessage =
          BuildError("Failed to open coefficients", config.coefficientsPath);
    }
    return false;
  }

  const std::size_t expectedBytes = config.taps * sizeof(float);
  std::vector<float> coefficients(config.taps, 0.0f);
  file.read(reinterpret_cast<char *>(coefficients.data()),
            static_cast<std::streamsize>(expectedBytes));
  if (static_cast<std::size_t>(file.gcount()) != expectedBytes) {
    if (errorMessage) {
      *errorMessage = "Coefficient file size does not match taps";
    }
    return false;
  }

  coefficients_ = std::move(coefficients);
  return true;
}

bool VulkanStreamingUpsampler::PrepareSpectrum(std::string *errorMessage) {
  if (config_.taps > config_.fftSize) {
    if (errorMessage) {
      *errorMessage = "taps must be <= fft_size for minimal overlap-save";
    }
    return false;
  }

  filterSpectrum_.assign(config_.fftSize, std::complex<float>(0.0f, 0.0f));
  for (std::size_t i = 0; i < config_.taps; ++i) {
    filterSpectrum_[i] = std::complex<float>(coefficients_[i], 0.0f);
  }

  fft::Fft(filterSpectrum_, false);

  overlap_.assign(config_.fftSize - config_.blockSize, 0.0f);
  return true;
}

} // namespace totton::vulkan
