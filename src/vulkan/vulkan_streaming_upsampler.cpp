#include "vulkan/vulkan_streaming_upsampler.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>

#include "fft_utils.h"

#if defined(ENABLE_VULKAN) && defined(USE_VKFFT)
#include <vulkan/vulkan.h>

#include "vkFFT/vkFFT.h"
#endif

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

#if defined(ENABLE_VULKAN) && defined(USE_VKFFT)
struct VulkanStreamingUpsampler::VkfftContext {
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t queueFamilyIndex = 0;
  VkCommandPool commandPool = VK_NULL_HANDLE;
  VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory bufferMemory = VK_NULL_HANDLE;
  uint64_t bufferSize = 0;
  VkFFTApplication app = VKFFT_ZERO_INIT;
  VkFFTConfiguration config = VKFFT_ZERO_INIT;
  VkFFTLaunchParams launchParams = VKFFT_ZERO_INIT;
  bool initialized = false;

  ~VkfftContext() { Destroy(); }

  bool Initialize(std::size_t fftSize, std::string *errorMessage) {
    auto fail = [&](const char *message) {
      if (errorMessage) {
        *errorMessage = message;
      }
      Destroy();
      return false;
    };

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "totton_vulkan_upsampler";
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
      return fail("Failed to create Vulkan instance");
    }

    uint32_t deviceCount = 0;
    if (vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr) !=
            VK_SUCCESS ||
        deviceCount == 0) {
      return fail("No Vulkan physical devices available");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount, VK_NULL_HANDLE);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    physicalDevice = devices.front();

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount,
                                             nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount,
                                             queueFamilies.data());
    bool foundQueue = false;
    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
      if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
        queueFamilyIndex = i;
        foundQueue = true;
        break;
      }
    }
    if (!foundQueue) {
      return fail("No Vulkan compute queue available");
    }

    const float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = queueFamilyIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo deviceCreateInfo{};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    if (vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device) !=
        VK_SUCCESS) {
      return fail("Failed to create Vulkan device");
    }
    vkGetDeviceQueue(device, queueFamilyIndex, 0, &queue);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) !=
        VK_SUCCESS) {
      return fail("Failed to create Vulkan command pool");
    }

    VkCommandBufferAllocateInfo commandBufferInfo{};
    commandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferInfo.commandPool = commandPool;
    commandBufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device, &commandBufferInfo, &commandBuffer) !=
        VK_SUCCESS) {
      return fail("Failed to allocate Vulkan command buffer");
    }

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(device, &fenceInfo, nullptr, &fence) != VK_SUCCESS) {
      return fail("Failed to create Vulkan fence");
    }

    bufferSize = static_cast<uint64_t>(sizeof(float) * 2 * fftSize);
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                       VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
      return fail("Failed to create Vulkan buffer");
    }

    VkMemoryRequirements memReq{};
    vkGetBufferMemoryRequirements(device, buffer, &memReq);
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    uint32_t memoryTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
      if ((memReq.memoryTypeBits & (1u << i)) &&
          (memProps.memoryTypes[i].propertyFlags &
           (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
              (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        memoryTypeIndex = i;
        break;
      }
    }
    if (memoryTypeIndex == UINT32_MAX) {
      return fail("Failed to find Vulkan host-visible memory");
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;
    if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) !=
        VK_SUCCESS) {
      return fail("Failed to allocate Vulkan buffer memory");
    }

    if (vkBindBufferMemory(device, buffer, bufferMemory, 0) != VK_SUCCESS) {
      return fail("Failed to bind Vulkan buffer memory");
    }

    config = VKFFT_ZERO_INIT;
    config.FFTdim = 1;
    config.size[0] = fftSize;
    config.device = &device;
    config.physicalDevice = &physicalDevice;
    config.queue = &queue;
    config.commandPool = &commandPool;
    config.fence = &fence;
    config.buffer = &buffer;
    config.bufferSize = &bufferSize;
    config.normalize = 1;

    VkFFTResult res = initializeVkFFT(&app, config);
    if (res != VKFFT_SUCCESS) {
      return fail("Failed to initialize VkFFT");
    }

    launchParams = VKFFT_ZERO_INIT;
    launchParams.buffer = &buffer;
    launchParams.commandBuffer = &commandBuffer;
    initialized = true;
    return true;
  }

  void Destroy() {
    if (device != VK_NULL_HANDLE) {
      vkDeviceWaitIdle(device);
    }
    if (initialized) {
      deleteVkFFT(&app);
    }
    if (buffer != VK_NULL_HANDLE) {
      vkDestroyBuffer(device, buffer, nullptr);
      buffer = VK_NULL_HANDLE;
    }
    if (bufferMemory != VK_NULL_HANDLE) {
      vkFreeMemory(device, bufferMemory, nullptr);
      bufferMemory = VK_NULL_HANDLE;
    }
    if (fence != VK_NULL_HANDLE) {
      vkDestroyFence(device, fence, nullptr);
      fence = VK_NULL_HANDLE;
    }
    if (commandPool != VK_NULL_HANDLE) {
      vkDestroyCommandPool(device, commandPool, nullptr);
      commandPool = VK_NULL_HANDLE;
    }
    if (device != VK_NULL_HANDLE) {
      vkDestroyDevice(device, nullptr);
      device = VK_NULL_HANDLE;
    }
    if (instance != VK_NULL_HANDLE) {
      vkDestroyInstance(instance, nullptr);
      instance = VK_NULL_HANDLE;
    }
    initialized = false;
  }

  bool Execute(int direction, std::string *errorMessage) {
    if (!initialized) {
      if (errorMessage) {
        *errorMessage = "VkFFT context not initialized";
      }
      return false;
    }

    vkResetCommandBuffer(commandBuffer, 0);
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
      if (errorMessage) {
        *errorMessage = "Failed to begin Vulkan command buffer";
      }
      return false;
    }

    launchParams.commandBuffer = &commandBuffer;
    VkFFTResult res = VkFFTAppend(&app, direction, &launchParams);
    if (res != VKFFT_SUCCESS) {
      if (errorMessage) {
        *errorMessage = "VkFFT execution failed";
      }
      return false;
    }

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
      if (errorMessage) {
        *errorMessage = "Failed to end Vulkan command buffer";
      }
      return false;
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    if (vkQueueSubmit(queue, 1, &submitInfo, fence) != VK_SUCCESS) {
      if (errorMessage) {
        *errorMessage = "Failed to submit Vulkan queue";
      }
      return false;
    }
    if (vkWaitForFences(device, 1, &fence, VK_TRUE, 100000000000) !=
        VK_SUCCESS) {
      if (errorMessage) {
        *errorMessage = "Failed to wait for Vulkan fence";
      }
      return false;
    }
    vkResetFences(device, 1, &fence);
    return true;
  }

  bool Map(float **data, std::string *errorMessage) {
    if (vkMapMemory(device, bufferMemory, 0, bufferSize, 0,
                    reinterpret_cast<void **>(data)) != VK_SUCCESS) {
      if (errorMessage) {
        *errorMessage = "Failed to map Vulkan buffer memory";
      }
      return false;
    }
    return true;
  }

  void Unmap() { vkUnmapMemory(device, bufferMemory); }
};
#else
struct VulkanStreamingUpsampler::VkfftContext {};
#endif

VulkanStreamingUpsampler::VulkanStreamingUpsampler() = default;

VulkanStreamingUpsampler::VulkanStreamingUpsampler(
    const VulkanStreamingUpsampler &other) {
  *this = other;
}

VulkanStreamingUpsampler &
VulkanStreamingUpsampler::operator=(const VulkanStreamingUpsampler &other) {
  if (this == &other) {
    return *this;
  }
  config_ = other.config_;
  coefficients_ = other.coefficients_;
  overlap_ = other.overlap_;
  filterSpectrum_ = other.filterSpectrum_;
  initialized_ = other.initialized_;
#if defined(ENABLE_VULKAN) && defined(USE_VKFFT)
  vkfft_.reset();
  if (initialized_) {
    std::string error;
    auto context = std::make_unique<VkfftContext>();
    if (context->Initialize(config_.fftSize, &error)) {
      vkfft_ = std::move(context);
    } else {
      std::cerr << "VkFFT initialization failed; falling back to CPU: "
                << error << "\n";
    }
  }
#endif
  return *this;
}

VulkanStreamingUpsampler::~VulkanStreamingUpsampler() = default;

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
  if (count == 0) {
    return {};
  }

  const std::size_t upsampleFactor =
      std::max<std::size_t>(config_.upsampleFactor, 1);
  if (config_.blockSize % upsampleFactor != 0) {
    return {};
  }
  const std::size_t maxInputSamples = upsampleFactor > 1
                                          ? (config_.blockSize / upsampleFactor)
                                          : config_.blockSize;
  if (maxInputSamples == 0 || count != maxInputSamples) {
    return {};
  }

  const std::size_t fftSize = config_.fftSize;
  const std::size_t overlapSize = overlap_.size();
  const std::size_t upsampledCount = count * upsampleFactor;
  if (overlapSize + upsampledCount > fftSize) {
    return {};
  }

  std::vector<float> timeBuffer(fftSize, 0.0f);
  for (std::size_t i = 0; i < overlapSize; ++i) {
    timeBuffer[i] = overlap_[i];
  }
  for (std::size_t i = 0; i < count; ++i) {
    timeBuffer[overlapSize + i * upsampleFactor] = input[i];
  }

#if defined(ENABLE_VULKAN) && defined(USE_VKFFT)
  if (vkfft_) {
    float *mapped = nullptr;
    if (!vkfft_->Map(&mapped, nullptr)) {
      return {};
    }
    for (std::size_t i = 0; i < fftSize; ++i) {
      mapped[2 * i] = timeBuffer[i];
      mapped[2 * i + 1] = 0.0f;
    }
    vkfft_->Unmap();
    if (!vkfft_->Execute(-1, nullptr)) {
      return {};
    }
    if (!vkfft_->Map(&mapped, nullptr)) {
      return {};
    }
    for (std::size_t i = 0; i < fftSize; ++i) {
      const std::complex<float> value(mapped[2 * i], mapped[2 * i + 1]);
      const std::complex<float> filtered = value * filterSpectrum_[i];
      mapped[2 * i] = filtered.real();
      mapped[2 * i + 1] = filtered.imag();
    }
    vkfft_->Unmap();
    if (!vkfft_->Execute(1, nullptr)) {
      return {};
    }
    if (!vkfft_->Map(&mapped, nullptr)) {
      return {};
    }
    std::vector<float> output(upsampledCount, 0.0f);
    for (std::size_t i = 0; i < upsampledCount; ++i) {
      output[i] = mapped[2 * (overlapSize + i)];
    }
    vkfft_->Unmap();
    overlap_.assign(timeBuffer.end() - static_cast<std::ptrdiff_t>(overlapSize),
                    timeBuffer.end());
    return output;
  }
#endif

  std::vector<std::complex<float>> freqBuffer(fftSize);
  for (std::size_t i = 0; i < fftSize; ++i) {
    freqBuffer[i] = std::complex<float>(timeBuffer[i], 0.0f);
  }

  fft::Fft(freqBuffer, false);
  for (std::size_t i = 0; i < fftSize; ++i) {
    freqBuffer[i] *= filterSpectrum_[i];
  }
  fft::Fft(freqBuffer, true);

  std::vector<float> output(upsampledCount, 0.0f);
  for (std::size_t i = 0; i < upsampledCount; ++i) {
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
  if (config->upsampleFactor > 1 &&
      (config->blockSize % config->upsampleFactor) != 0) {
    if (errorMessage) {
      *errorMessage = "block_size must be divisible by upsample_factor";
    }
    return false;
  }

  return true;
}

bool VulkanStreamingUpsampler::LoadCoefficients(const FilterConfig &config,
                                                std::string *errorMessage) {
  std::error_code ec;
  const auto fileSize = std::filesystem::file_size(config.coefficientsPath, ec);
  if (ec) {
    if (errorMessage) {
      *errorMessage =
          BuildError("Failed to stat coefficients", config.coefficientsPath);
    }
    return false;
  }

  std::ifstream file(config.coefficientsPath, std::ios::binary);
  if (!file) {
    if (errorMessage) {
      *errorMessage =
          BuildError("Failed to open coefficients", config.coefficientsPath);
    }
    return false;
  }

  const std::size_t expectedBytes = config.taps * sizeof(float);
  if (fileSize != expectedBytes) {
    if (errorMessage) {
      *errorMessage = "Coefficient file size does not match taps";
    }
    return false;
  }
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

#if defined(ENABLE_VULKAN) && defined(USE_VKFFT)
  vkfft_ = std::make_unique<VkfftContext>();
  std::string vkfftError;
  if (!vkfft_->Initialize(config_.fftSize, &vkfftError)) {
    std::cerr << "VkFFT initialization failed; falling back to CPU: "
              << vkfftError << "\n";
    vkfft_.reset();
  }
#endif
  return true;
}

} // namespace totton::vulkan
