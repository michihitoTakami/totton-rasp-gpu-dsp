#include "vulkan/vulkan_streaming_upsampler.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <string>
#include <unistd.h>
#include <vector>

#if defined(ENABLE_VULKAN)
#include <vulkan/vulkan.h>
#endif

namespace {

#if defined(ENABLE_VULKAN)
bool HasVulkanDevice() {
  VkInstance instance = VK_NULL_HANDLE;
  VkApplicationInfo appInfo{};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "totton_vulkan_upsampler_test";
  appInfo.apiVersion = VK_API_VERSION_1_1;

  VkInstanceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;

  if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
    return false;
  }

  uint32_t deviceCount = 0;
  VkResult res = vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
  vkDestroyInstance(instance, nullptr);
  return res == VK_SUCCESS && deviceCount > 0;
}
#endif

std::filesystem::path WriteTempFilter(const std::filesystem::path &dir,
                                      std::size_t upsampleFactor) {
  std::filesystem::create_directories(dir);

  const std::filesystem::path binPath = dir / "coeffs.bin";
  const std::filesystem::path jsonPath = dir / "filter.json";

  const std::vector<float> taps = {1.0f, 2.0f, 3.0f, 2.0f, 1.0f};
  std::ofstream bin(binPath, std::ios::binary);
  bin.write(reinterpret_cast<const char *>(taps.data()),
            static_cast<std::streamsize>(taps.size() * sizeof(float)));
  bin.close();

  std::ofstream json(jsonPath);
  json << "{\n"
       << "  \"coefficients_bin\": \"coeffs.bin\",\n"
       << "  \"taps\": 5,\n"
       << "  \"fft_size\": 16,\n"
       << "  \"block_size\": 12,\n"
       << "  \"upsample_factor\": " << upsampleFactor << "\n"
       << "}\n";
  json.close();

  return jsonPath;
}

bool AlmostEqual(float a, float b, float eps = 1e-3f) {
  return std::abs(a - b) <= eps;
}

std::vector<float> Convolve(const std::vector<float> &input,
                            const std::vector<float> &filter) {
  std::vector<float> output(input.size() + filter.size() - 1, 0.0f);
  for (std::size_t i = 0; i < input.size(); ++i) {
    for (std::size_t j = 0; j < filter.size(); ++j) {
      output[i + j] += input[i] * filter[j];
    }
  }
  return output;
}

std::vector<float> ZeroStuff(const std::vector<float> &input,
                             std::size_t factor) {
  if (factor == 0) {
    return {};
  }
  std::vector<float> output(input.size() * factor, 0.0f);
  for (std::size_t i = 0; i < input.size(); ++i) {
    output[i * factor] = input[i];
  }
  return output;
}

bool CheckVectorNear(const std::vector<float> &actual,
                     const std::vector<float> &expected, float eps = 1e-3f) {
  if (actual.size() != expected.size()) {
    return false;
  }
  for (std::size_t i = 0; i < actual.size(); ++i) {
    if (!AlmostEqual(actual[i], expected[i], eps)) {
      return false;
    }
  }
  return true;
}

} // namespace

int main() {
#if defined(ENABLE_VULKAN)
  if (!HasVulkanDevice()) {
    std::cout << "No Vulkan device available, skipping GPU validation.\n";
    return 0;
  }
#endif
  const auto stamp =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto tempDir = std::filesystem::temp_directory_path() /
                       ("totton_upsampler_test_" + std::to_string(::getpid()) +
                        "_" + std::to_string(stamp));
  const auto jsonPath = WriteTempFilter(tempDir, 1);

  totton::vulkan::VulkanStreamingUpsampler upsampler;
  std::string error;
  if (!upsampler.LoadFilter(jsonPath.string(), &error)) {
    std::cerr << "LoadFilter failed: " << error << "\n";
    return 1;
  }

  const std::vector<float> taps = {1.0f, 2.0f, 3.0f, 2.0f, 1.0f};
  const std::size_t blockSize = 12;

  std::vector<float> impulseBlock(blockSize, 0.0f);
  impulseBlock[taps.size() - 1] = 1.0f;
  const auto impulseOut =
      upsampler.ProcessBlock(impulseBlock.data(), impulseBlock.size());
  if (impulseOut.size() != blockSize) {
    std::cerr << "Unexpected output size: " << impulseOut.size() << "\n";
    return 1;
  }
  const auto impulseConv = Convolve(impulseBlock, taps);
  const std::vector<float> expectedImpulse(impulseConv.begin(),
                                           impulseConv.begin() + blockSize);
  if (!CheckVectorNear(impulseOut, expectedImpulse)) {
    std::cerr << "Impulse response mismatch\n";
    return 1;
  }

  std::vector<float> blockA(blockSize, 0.0f);
  std::vector<float> blockB(blockSize, 0.0f);
  std::iota(blockA.begin(), blockA.end(), 1.0f);
  std::iota(blockB.begin(), blockB.end(), 101.0f);

  upsampler.Reset();
  const auto outA = upsampler.ProcessBlock(blockA.data(), blockA.size());
  const auto outB = upsampler.ProcessBlock(blockB.data(), blockB.size());

  std::vector<float> streamInput = blockA;
  streamInput.insert(streamInput.end(), blockB.begin(), blockB.end());
  const auto streamConv = Convolve(streamInput, taps);
  std::vector<float> expectedStream(streamConv.begin(),
                                    streamConv.begin() + 2 * blockSize);
  std::vector<float> actualStream = outA;
  actualStream.insert(actualStream.end(), outB.begin(), outB.end());
  if (!CheckVectorNear(actualStream, expectedStream)) {
    std::cerr << "Streaming overlap mismatch\n";
    return 1;
  }

  const auto upsampleDir = tempDir / "upsample2x";
  const auto upsampleJsonPath = WriteTempFilter(upsampleDir, 2);
  totton::vulkan::VulkanStreamingUpsampler upsampled;
  if (!upsampled.LoadFilter(upsampleJsonPath.string(), &error)) {
    std::cerr << "LoadFilter failed (2x): " << error << "\n";
    return 1;
  }

  const std::size_t inputBlockSize = 6;
  std::vector<float> impulseInput(inputBlockSize, 0.0f);
  impulseInput[2] = 1.0f;
  const auto upsampledOut =
      upsampled.ProcessBlock(impulseInput.data(), impulseInput.size());
  if (upsampledOut.size() != blockSize) {
    std::cerr << "Unexpected output size (2x): " << upsampledOut.size() << "\n";
    return 1;
  }
  const auto zeroStuffed = ZeroStuff(impulseInput, 2);
  const auto upsampleConv = Convolve(zeroStuffed, taps);
  const std::vector<float> expectedUpsample(upsampleConv.begin(),
                                            upsampleConv.begin() + blockSize);
  if (!CheckVectorNear(upsampledOut, expectedUpsample)) {
    std::cerr << "Upsampled impulse response mismatch\n";
    return 1;
  }

  std::cout << "OK\n";
  return 0;
}
