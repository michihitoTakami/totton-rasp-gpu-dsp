#include <alsa/asoundlib.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cctype>
#include <csignal>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "vulkan/vulkan_streaming_upsampler.h"

namespace {

struct CliOptions {
  std::string inputDevice;
  std::string outputDevice;
  std::string filterPath;
  std::string filterDir = "data/coefficients";
  std::string phase = "min";
  unsigned int channels = 2;
  unsigned int requestedRate = 0;
  unsigned int periodFrames = 0;
  unsigned int bufferFrames = 0;
  unsigned int ratio = 1;
  std::string format = "s32";
  bool showHelp = false;
};

struct AlsaHandle {
  snd_pcm_t *handle = nullptr;
  snd_pcm_uframes_t periodFrames = 0;
  snd_pcm_uframes_t bufferFrames = 0;
  unsigned int rate = 0;
};

std::atomic<bool> gRunning{true};

void SignalHandler(int) { gRunning.store(false); }

void PrintUsage(const char *argv0) {
  std::cout
      << "Usage: " << argv0
      << " --in <device> --out <device> [options]\n\n"
      << "Options:\n"
      << "  --filter <path>         Filter JSON path (docs/filter_format.md)\n"
      << "  --filter-dir <path>     Filter directory (default: data/coefficients)\n"
      << "  --phase <min|linear>    Filter phase suffix for auto lookup (default: min)\n"
      << "  --ratio <1|2|4|8|16>     Upsample ratio suffix for auto lookup (default: 1)\n"
      << "  --rate <hz>             Requested input sample rate (auto if omitted)\n"
      << "  --channels <n>          Channel count (default: 2)\n"
      << "  --format <s16|s24|s32>  PCM format (default: s32)\n"
      << "  --period <frames>       ALSA period frames (default: filter block size)\n"
      << "  --buffer <frames>       ALSA buffer frames (default: period*4)\n"
      << "  --help                  Show this help\n";
}

bool ParseArgs(int argc, char **argv, CliOptions *options) {
  if (!options) {
    return false;
  }
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    auto requireValue = [&](const char *name) -> const char * {
      if (i + 1 >= argc) {
        std::cerr << "Missing value for " << name << "\n";
        return nullptr;
      }
      return argv[++i];
    };

    if (arg == "--help") {
      options->showHelp = true;
      return true;
    }
    if (arg == "--in") {
      const char *val = requireValue("--in");
      if (!val) {
        return false;
      }
      options->inputDevice = val;
      continue;
    }
    if (arg == "--out") {
      const char *val = requireValue("--out");
      if (!val) {
        return false;
      }
      options->outputDevice = val;
      continue;
    }
    if (arg == "--filter") {
      const char *val = requireValue("--filter");
      if (!val) {
        return false;
      }
      options->filterPath = val;
      continue;
    }
    if (arg == "--filter-dir") {
      const char *val = requireValue("--filter-dir");
      if (!val) {
        return false;
      }
      options->filterDir = val;
      continue;
    }
    if (arg == "--phase") {
      const char *val = requireValue("--phase");
      if (!val) {
        return false;
      }
      options->phase = val;
      continue;
    }
    if (arg == "--ratio") {
      const char *val = requireValue("--ratio");
      if (!val) {
        return false;
      }
      options->ratio = static_cast<unsigned int>(std::stoul(val));
      continue;
    }
    if (arg == "--rate") {
      const char *val = requireValue("--rate");
      if (!val) {
        return false;
      }
      options->requestedRate = static_cast<unsigned int>(std::stoul(val));
      continue;
    }
    if (arg == "--channels") {
      const char *val = requireValue("--channels");
      if (!val) {
        return false;
      }
      options->channels = static_cast<unsigned int>(std::stoul(val));
      continue;
    }
    if (arg == "--format") {
      const char *val = requireValue("--format");
      if (!val) {
        return false;
      }
      options->format = val;
      continue;
    }
    if (arg == "--period") {
      const char *val = requireValue("--period");
      if (!val) {
        return false;
      }
      options->periodFrames = static_cast<unsigned int>(std::stoul(val));
      continue;
    }
    if (arg == "--buffer") {
      const char *val = requireValue("--buffer");
      if (!val) {
        return false;
      }
      options->bufferFrames = static_cast<unsigned int>(std::stoul(val));
      continue;
    }

    std::cerr << "Unknown argument: " << arg << "\n";
    return false;
  }

  return true;
}

snd_pcm_format_t ParseFormat(const std::string &format) {
  std::string lower = format;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (lower == "s16" || lower == "s16_le") {
    return SND_PCM_FORMAT_S16_LE;
  }
  if (lower == "s24" || lower == "s24_3le") {
    return SND_PCM_FORMAT_S24_3LE;
  }
  if (lower == "s32" || lower == "s32_le") {
    return SND_PCM_FORMAT_S32_LE;
  }
  return SND_PCM_FORMAT_UNKNOWN;
}

size_t BytesPerSample(snd_pcm_format_t format) {
  switch (format) {
  case SND_PCM_FORMAT_S16_LE:
    return 2;
  case SND_PCM_FORMAT_S24_3LE:
    return 3;
  case SND_PCM_FORMAT_S32_LE:
    return 4;
  default:
    return 0;
  }
}

bool ConvertPcmToFloat(const void *src, snd_pcm_format_t format, size_t frames,
                       unsigned int channels, std::vector<float> *dst) {
  if (!dst) {
    return false;
  }
  const size_t samples = frames * static_cast<size_t>(channels);
  dst->assign(samples, 0.0f);

  if (format == SND_PCM_FORMAT_S16_LE) {
    const auto *in = static_cast<const int16_t *>(src);
    constexpr float scale = 1.0f / 32768.0f;
    for (size_t i = 0; i < samples; ++i) {
      (*dst)[i] = static_cast<float>(in[i]) * scale;
    }
    return true;
  }

  if (format == SND_PCM_FORMAT_S32_LE) {
    const auto *in = static_cast<const int32_t *>(src);
    constexpr float scale = 1.0f / 2147483648.0f;
    for (size_t i = 0; i < samples; ++i) {
      (*dst)[i] = static_cast<float>(in[i]) * scale;
    }
    return true;
  }

  if (format == SND_PCM_FORMAT_S24_3LE) {
    const auto *in = static_cast<const uint8_t *>(src);
    constexpr float scale = 1.0f / 8388608.0f;
    for (size_t i = 0; i < samples; ++i) {
      size_t idx = i * 3;
      int32_t value = static_cast<int32_t>(in[idx]) |
                      (static_cast<int32_t>(in[idx + 1]) << 8) |
                      (static_cast<int32_t>(in[idx + 2]) << 16);
      if (value & 0x00800000) {
        value |= 0xFF000000;
      }
      (*dst)[i] = static_cast<float>(value) * scale;
    }
    return true;
  }

  return false;
}

bool ConvertFloatToPcm(const std::vector<float> &src, snd_pcm_format_t format,
                       std::vector<uint8_t> *dst) {
  if (!dst) {
    return false;
  }

  if (format == SND_PCM_FORMAT_S16_LE) {
    dst->resize(src.size() * 2);
    auto *out = reinterpret_cast<int16_t *>(dst->data());
    for (size_t i = 0; i < src.size(); ++i) {
      float clamped = std::max(-1.0f, std::min(0.9999695f, src[i]));
      out[i] = static_cast<int16_t>(clamped * 32768.0f);
    }
    return true;
  }

  if (format == SND_PCM_FORMAT_S32_LE) {
    dst->resize(src.size() * 4);
    auto *out = reinterpret_cast<int32_t *>(dst->data());
    for (size_t i = 0; i < src.size(); ++i) {
      float clamped = std::max(-1.0f, std::min(0.9999999f, src[i]));
      out[i] = static_cast<int32_t>(clamped * 2147483648.0f);
    }
    return true;
  }

  if (format == SND_PCM_FORMAT_S24_3LE) {
    dst->resize(src.size() * 3);
    for (size_t i = 0; i < src.size(); ++i) {
      float clamped = std::max(-1.0f, std::min(0.9999999f, src[i]));
      int32_t value = static_cast<int32_t>(clamped * 8388608.0f);
      size_t idx = i * 3;
      (*dst)[idx] = static_cast<uint8_t>(value & 0xFF);
      (*dst)[idx + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
      (*dst)[idx + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    }
    return true;
  }

  return false;
}

bool ConfigurePcm(snd_pcm_t *handle, snd_pcm_format_t format,
                  unsigned int channels, unsigned int rate,
                  snd_pcm_uframes_t requestedPeriod,
                  snd_pcm_uframes_t requestedBuffer,
                  snd_pcm_uframes_t *periodOut,
                  snd_pcm_uframes_t *bufferOut, unsigned int *rateOut,
                  bool playback) {
  snd_pcm_hw_params_t *hwParams;
  snd_pcm_hw_params_alloca(&hwParams);
  snd_pcm_hw_params_any(handle, hwParams);

  int err = snd_pcm_hw_params_set_access(handle, hwParams,
                                        SND_PCM_ACCESS_RW_INTERLEAVED);
  if (err < 0) {
    std::cerr << "ALSA: Cannot set access: " << snd_strerror(err) << "\n";
    return false;
  }
  err = snd_pcm_hw_params_set_format(handle, hwParams, format);
  if (err < 0) {
    std::cerr << "ALSA: Cannot set format: " << snd_strerror(err) << "\n";
    return false;
  }
  err = snd_pcm_hw_params_set_channels(handle, hwParams, channels);
  if (err < 0) {
    std::cerr << "ALSA: Cannot set channels: " << snd_strerror(err) << "\n";
    return false;
  }

  unsigned int rateNear = rate;
  err = snd_pcm_hw_params_set_rate_near(handle, hwParams, &rateNear, nullptr);
  if (err < 0) {
    std::cerr << "ALSA: Cannot set rate: " << snd_strerror(err) << "\n";
    return false;
  }

  snd_pcm_uframes_t period = requestedPeriod;
  err = snd_pcm_hw_params_set_period_size_near(handle, hwParams, &period,
                                               nullptr);
  if (err < 0) {
    std::cerr << "ALSA: Cannot set period: " << snd_strerror(err) << "\n";
    return false;
  }

  snd_pcm_uframes_t buffer = requestedBuffer;
  if (buffer == 0) {
    buffer = std::max<snd_pcm_uframes_t>(period * 4, period * 2);
  }
  err = snd_pcm_hw_params_set_buffer_size_near(handle, hwParams, &buffer);
  if (err < 0) {
    std::cerr << "ALSA: Cannot set buffer: " << snd_strerror(err) << "\n";
    return false;
  }

  err = snd_pcm_hw_params(handle, hwParams);
  if (err < 0) {
    std::cerr << "ALSA: Cannot apply hw params: " << snd_strerror(err) << "\n";
    return false;
  }

  snd_pcm_hw_params_get_period_size(hwParams, &period, nullptr);
  snd_pcm_hw_params_get_buffer_size(hwParams, &buffer);

  if (playback) {
    snd_pcm_sw_params_t *swParams;
    snd_pcm_sw_params_alloca(&swParams);
    if (snd_pcm_sw_params_current(handle, swParams) == 0) {
      snd_pcm_sw_params_set_start_threshold(handle, swParams, buffer);
      snd_pcm_sw_params_set_avail_min(handle, swParams, period);
      snd_pcm_sw_params(handle, swParams);
    }
  }

  err = snd_pcm_prepare(handle);
  if (err < 0) {
    std::cerr << "ALSA: Cannot prepare: " << snd_strerror(err) << "\n";
    return false;
  }

  if (periodOut) {
    *periodOut = period;
  }
  if (bufferOut) {
    *bufferOut = buffer;
  }
  if (rateOut) {
    *rateOut = rateNear;
  }

  return true;
}

std::optional<AlsaHandle> OpenPcm(const std::string &device,
                                 snd_pcm_stream_t stream,
                                 snd_pcm_format_t format,
                                 unsigned int channels, unsigned int rate,
                                 snd_pcm_uframes_t period,
                                 snd_pcm_uframes_t buffer) {
  snd_pcm_t *handle = nullptr;
  int err = snd_pcm_open(&handle, device.c_str(), stream, 0);
  if (err < 0) {
    std::cerr << "ALSA: Cannot open device " << device << ": "
              << snd_strerror(err) << "\n";
    return std::nullopt;
  }

  AlsaHandle result;
  result.handle = handle;
  if (!ConfigurePcm(handle, format, channels, rate, period, buffer,
                    &result.periodFrames, &result.bufferFrames, &result.rate,
                    stream == SND_PCM_STREAM_PLAYBACK)) {
    snd_pcm_close(handle);
    return std::nullopt;
  }

  return result;
}

std::optional<AlsaHandle> OpenCaptureAutoRate(
    const std::string &device, snd_pcm_format_t format, unsigned int channels,
    unsigned int requestedRate, snd_pcm_uframes_t period,
    snd_pcm_uframes_t buffer) {
  if (requestedRate != 0) {
    return OpenPcm(device, SND_PCM_STREAM_CAPTURE, format, channels,
                   requestedRate, period, buffer);
  }

  const unsigned int candidates[] = {44100, 48000, 88200, 96000, 176400, 192000};
  for (unsigned int candidate : candidates) {
    auto handle = OpenPcm(device, SND_PCM_STREAM_CAPTURE, format, channels,
                          candidate, period, buffer);
    if (handle.has_value()) {
      return handle;
    }
  }

  std::cerr << "ALSA: Failed to open capture device with common rates\n";
  return std::nullopt;
}

std::optional<std::string> ResolveFilterPath(const CliOptions &options,
                                             unsigned int inputRate) {
  if (!options.filterPath.empty()) {
    return options.filterPath;
  }

  unsigned int family = 0;
  if (inputRate % 44100 == 0) {
    family = 44;
  } else if (inputRate % 48000 == 0) {
    family = 48;
  } else {
    std::cerr << "Unsupported input rate family: " << inputRate << "\n";
    return std::nullopt;
  }

  std::string phaseSuffix = options.phase;
  if (phaseSuffix == "min") {
    phaseSuffix = "min_phase";
  } else if (phaseSuffix == "linear") {
    phaseSuffix = "linear_phase";
  }

  std::string path = options.filterDir + "/filter_" + std::to_string(family) +
                     "k_" + std::to_string(options.ratio) + "x_2m_" +
                     phaseSuffix + ".json";
  return path;
}

bool RecoverPcm(snd_pcm_t *handle, int err, const char *label) {
  if (err >= 0) {
    return true;
  }
  int recover = snd_pcm_recover(handle, err, 1);
  if (recover < 0) {
    std::cerr << label << ": recover failed: " << snd_strerror(recover) << "\n";
    return false;
  }
  std::cerr << label << ": XRUN recovered\n";
  return true;
}

bool ReadFull(snd_pcm_t *handle, void *buffer, snd_pcm_uframes_t frames) {
  auto *ptr = static_cast<uint8_t *>(buffer);
  snd_pcm_uframes_t remaining = frames;
  size_t frameBytes = static_cast<size_t>(snd_pcm_frames_to_bytes(handle, 1));

  while (remaining > 0 && gRunning.load()) {
    snd_pcm_sframes_t n = snd_pcm_readi(handle, ptr, remaining);
    if (n == -EPIPE || n == -ESTRPIPE || n == -EINTR) {
      if (!RecoverPcm(handle, static_cast<int>(n), "ALSA capture")) {
        return false;
      }
      continue;
    }
    if (n < 0) {
      std::cerr << "ALSA capture error: " << snd_strerror(static_cast<int>(n))
                << "\n";
      return false;
    }
    if (n == 0) {
      continue;
    }
    ptr += static_cast<size_t>(n) * frameBytes;
    remaining -= static_cast<snd_pcm_uframes_t>(n);
  }
  return remaining == 0;
}

bool WriteFull(snd_pcm_t *handle, const void *buffer, snd_pcm_uframes_t frames) {
  auto *ptr = static_cast<const uint8_t *>(buffer);
  snd_pcm_uframes_t remaining = frames;
  size_t frameBytes = static_cast<size_t>(snd_pcm_frames_to_bytes(handle, 1));

  while (remaining > 0 && gRunning.load()) {
    snd_pcm_sframes_t n = snd_pcm_writei(handle, ptr, remaining);
    if (n == -EPIPE || n == -ESTRPIPE || n == -EINTR) {
      if (!RecoverPcm(handle, static_cast<int>(n), "ALSA playback")) {
        return false;
      }
      continue;
    }
    if (n < 0) {
      std::cerr << "ALSA playback error: "
                << snd_strerror(static_cast<int>(n)) << "\n";
      return false;
    }
    if (n == 0) {
      continue;
    }
    ptr += static_cast<size_t>(n) * frameBytes;
    remaining -= static_cast<snd_pcm_uframes_t>(n);
  }
  return remaining == 0;
}

} // namespace

int main(int argc, char **argv) {
  CliOptions options;
  if (!ParseArgs(argc, argv, &options)) {
    PrintUsage(argv[0]);
    return 1;
  }
  if (options.showHelp) {
    PrintUsage(argv[0]);
    return 0;
  }
  if (options.inputDevice.empty() || options.outputDevice.empty()) {
    std::cerr << "--in and --out are required\n";
    PrintUsage(argv[0]);
    return 1;
  }

  snd_pcm_format_t format = ParseFormat(options.format);
  if (format == SND_PCM_FORMAT_UNKNOWN) {
    std::cerr << "Unsupported format: " << options.format << "\n";
    return 1;
  }

  std::signal(SIGINT, SignalHandler);
  std::signal(SIGTERM, SignalHandler);

  totton::vulkan::VulkanStreamingUpsampler upsampler;
  std::vector<totton::vulkan::VulkanStreamingUpsampler> channelUpsamplers;

  std::optional<totton::vulkan::FilterConfig> filterConfig;
  if (!options.filterPath.empty() || !options.filterDir.empty()) {
    std::string filterError;
    std::optional<AlsaHandle> capturePreview;

    auto preview = OpenCaptureAutoRate(options.inputDevice, format,
                                       options.channels, options.requestedRate,
                                       options.periodFrames,
                                       options.bufferFrames);
    if (!preview) {
      return 1;
    }
    unsigned int inputRate = preview->rate;
    snd_pcm_close(preview->handle);

    auto filterPath = ResolveFilterPath(options, inputRate);
    if (filterPath && upsampler.LoadFilter(*filterPath, &filterError)) {
      filterConfig = upsampler.GetConfig();
      channelUpsamplers.assign(options.channels, upsampler);
      options.periodFrames =
          static_cast<unsigned int>(filterConfig->blockSize);
    } else if (filterPath) {
      std::cerr << "Filter load failed: " << filterError << "\n";
      std::cerr << "Filter path: " << *filterPath << "\n";
      return 1;
    }
  }

  unsigned int periodFrames = options.periodFrames;
  if (periodFrames == 0) {
    periodFrames = 1024;
  }

  auto capture = OpenCaptureAutoRate(
      options.inputDevice, format, options.channels, options.requestedRate,
      periodFrames, options.bufferFrames);
  if (!capture) {
    return 1;
  }

  unsigned int outputRate = capture->rate;
  if (filterConfig && filterConfig->upsampleFactor > 1) {
    std::cerr
        << "Filter upsample_factor > 1 is not supported in this streamer.\n";
    return 1;
  }

  auto playback = OpenPcm(options.outputDevice, SND_PCM_STREAM_PLAYBACK, format,
                          options.channels, outputRate, capture->periodFrames,
                          options.bufferFrames);
  if (!playback) {
    return 1;
  }

  const size_t frameBytes = BytesPerSample(format) * options.channels;
  std::vector<uint8_t> rawBuffer(capture->periodFrames * frameBytes);
  std::vector<float> floatBuffer;
  std::vector<float> processed;
  std::vector<uint8_t> outBuffer;

  std::cerr << "ALSA streaming started: input " << capture->rate << " Hz, "
            << "period " << capture->periodFrames << " frames\n";

  while (gRunning.load()) {
    if (!ReadFull(capture->handle, rawBuffer.data(), capture->periodFrames)) {
      break;
    }

    if (!ConvertPcmToFloat(rawBuffer.data(), format, capture->periodFrames,
                           options.channels, &floatBuffer)) {
      std::cerr << "PCM conversion failed\n";
      break;
    }

    processed.assign(floatBuffer.size(), 0.0f);

    if (!channelUpsamplers.empty()) {
      const size_t frames = capture->periodFrames;
      for (unsigned int ch = 0; ch < options.channels; ++ch) {
        std::vector<float> channel(frames, 0.0f);
        for (size_t i = 0; i < frames; ++i) {
          channel[i] = floatBuffer[i * options.channels + ch];
        }
        std::vector<float> out = channelUpsamplers[ch].ProcessBlock(
            channel.data(), channel.size());
        if (out.size() != frames) {
          std::cerr << "Filter output size mismatch\n";
          gRunning.store(false);
          break;
        }
        for (size_t i = 0; i < frames; ++i) {
          processed[i * options.channels + ch] = out[i];
        }
      }
    } else {
      processed = floatBuffer;
    }

    if (!ConvertFloatToPcm(processed, format, &outBuffer)) {
      std::cerr << "PCM output conversion failed\n";
      break;
    }

    if (!WriteFull(playback->handle, outBuffer.data(), playback->periodFrames)) {
      break;
    }
  }

  if (capture->handle) {
    snd_pcm_drop(capture->handle);
    snd_pcm_close(capture->handle);
  }
  if (playback->handle) {
    snd_pcm_drain(playback->handle);
    snd_pcm_close(playback->handle);
  }

  std::cerr << "ALSA streaming stopped\n";
  return 0;
}
