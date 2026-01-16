#include "vulkan/vulkan_streaming_upsampler.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

std::filesystem::path WriteTempFilter(const std::filesystem::path &dir) {
  std::filesystem::create_directories(dir);

  const std::filesystem::path binPath = dir / "coeffs.bin";
  const std::filesystem::path jsonPath = dir / "filter.json";

  const std::vector<float> taps = {1.0f, 0.0f, 0.0f};
  std::ofstream bin(binPath, std::ios::binary);
  bin.write(reinterpret_cast<const char *>(taps.data()),
            static_cast<std::streamsize>(taps.size() * sizeof(float)));
  bin.close();

  std::ofstream json(jsonPath);
  json << "{\n"
       << "  \"coefficients_bin\": \"coeffs.bin\",\n"
       << "  \"taps\": 3,\n"
       << "  \"fft_size\": 8,\n"
       << "  \"block_size\": 6,\n"
       << "  \"upsample_factor\": 1\n"
       << "}\n";
  json.close();

  return jsonPath;
}

bool AlmostEqual(float a, float b, float eps = 1e-5f) {
  return std::abs(a - b) <= eps;
}

} // namespace

int main() {
  const auto stamp =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto tempDir = std::filesystem::temp_directory_path() /
                       ("totton_upsampler_test_" + std::to_string(::getpid()) +
                        "_" + std::to_string(stamp));
  const auto jsonPath = WriteTempFilter(tempDir);

  totton::vulkan::VulkanStreamingUpsampler upsampler;
  std::string error;
  if (!upsampler.LoadFilter(jsonPath.string(), &error)) {
    std::cerr << "LoadFilter failed: " << error << "\n";
    return 1;
  }

  const std::vector<float> input = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f};
  const auto output = upsampler.ProcessBlock(input.data(), input.size());
  if (output.size() != input.size()) {
    std::cerr << "Unexpected output size: " << output.size() << "\n";
    return 1;
  }

  for (std::size_t i = 0; i < input.size(); ++i) {
    if (!AlmostEqual(output[i], input[i])) {
      std::cerr << "Mismatch at " << i << ": " << output[i] << " vs "
                << input[i] << "\n";
      return 1;
    }
  }

  std::cout << "OK\n";
  return 0;
}
