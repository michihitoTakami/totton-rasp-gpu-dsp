#include "alsa/alsa_common.h"
#include "alsa/alsa_filter_selector.h"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <filesystem>
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
  bool filterDirSpecified = false;
  std::string phase = "min";
  unsigned int channels = 2;
  unsigned int requestedRate = 0;
  unsigned int periodFrames = 0;
  unsigned int bufferFrames = 0;
  unsigned int ratio = 1;
  std::string format = "s32";
  bool showHelp = false;
};

std::atomic<bool> gRunning{true};

void SignalHandler(int) { gRunning.store(false); }

void PrintUsage(const char *argv0) {
  std::cout
      << "Usage: " << argv0 << " --in <device> --out <device> [options]\n\n"
      << "Options:\n"
      << "  --filter <path>         Filter JSON path (docs/filter_format.md)\n"
      << "  --filter-dir <path>     Filter directory (default: "
         "data/coefficients)\n"
      << "  --phase <min|linear>    Filter phase suffix for auto lookup "
         "(default: min)\n"
      << "  --ratio <1|2|4|8|16>     Upsample ratio suffix for auto lookup "
         "(default: 1)\n"
      << "  --rate <hz>             Requested input sample rate (auto if "
         "omitted)\n"
      << "  --channels <n>          Channel count (default: 2)\n"
      << "  --format <s16|s24|s32>  PCM format (default: s32)\n"
      << "  --period <frames>       ALSA period frames (default: filter block "
         "size)\n"
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
      options->filterDirSpecified = true;
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

bool PrepareFilter(
    const CliOptions &options, snd_pcm_format_t format,
    totton::vulkan::VulkanStreamingUpsampler *upsampler,
    std::vector<totton::vulkan::VulkanStreamingUpsampler> *channelUpsamplers,
    std::optional<totton::vulkan::FilterConfig> *filterConfig) {
  const bool filterRequired = !options.filterPath.empty();
  const bool autoFilterRequested =
      options.filterDirSpecified || !options.filterPath.empty();

  if (!filterRequired && !autoFilterRequested) {
    return true;
  }

  unsigned int inputRate = options.requestedRate;
  if (inputRate == 0) {
    auto preview = totton::alsa::OpenCaptureAutoRate(
        options.inputDevice, format, options.channels, options.requestedRate,
        options.periodFrames, options.bufferFrames);
    if (!preview) {
      return false;
    }
    inputRate = preview->rate;
    snd_pcm_close(preview->handle);
  }

  std::string filterError;
  auto selection = totton::alsa::ResolveFilterPath(
      options.filterPath, options.filterDir, options.phase, options.ratio,
      inputRate, &filterError);
  if (!selection) {
    if (filterRequired) {
      std::cerr << "Filter load failed: " << filterError << "\n";
      return false;
    }
    if (!filterError.empty()) {
      std::cerr << "Filter not available, continuing without filter: "
                << filterError << "\n";
    }
    return true;
  }

  if (!upsampler->LoadFilter(selection->path, &filterError)) {
    std::cerr << "Filter load failed: " << filterError << "\n";
    std::cerr << "Filter path: " << selection->path << "\n";
    return false;
  }

  if (filterConfig) {
    *filterConfig = upsampler->GetConfig();
  }
  if (channelUpsamplers) {
    channelUpsamplers->assign(options.channels, *upsampler);
  }
  return true;
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

  snd_pcm_format_t format = totton::alsa::ParseFormat(options.format);
  if (format == SND_PCM_FORMAT_UNKNOWN) {
    std::cerr << "Unsupported format: " << options.format << "\n";
    return 1;
  }

  std::signal(SIGINT, SignalHandler);
  std::signal(SIGTERM, SignalHandler);

  totton::vulkan::VulkanStreamingUpsampler upsampler;
  std::vector<totton::vulkan::VulkanStreamingUpsampler> channelUpsamplers;
  std::optional<totton::vulkan::FilterConfig> filterConfig;

  if (!PrepareFilter(options, format, &upsampler, &channelUpsamplers,
                     &filterConfig)) {
    return 1;
  }
  if (filterConfig) {
    if (filterConfig->upsampleFactor > 1) {
      const std::size_t inputFrames =
          filterConfig->blockSize / filterConfig->upsampleFactor;
      if (inputFrames == 0) {
        std::cerr << "Invalid filter block size for upsampling.\n";
        return 1;
      }
      options.periodFrames = static_cast<unsigned int>(inputFrames);
    } else {
      options.periodFrames = static_cast<unsigned int>(filterConfig->blockSize);
    }
  }

  unsigned int periodFrames = options.periodFrames;
  if (periodFrames == 0) {
    periodFrames = 1024;
  }

  auto capture = totton::alsa::OpenCaptureAutoRate(
      options.inputDevice, format, options.channels, options.requestedRate,
      periodFrames, options.bufferFrames);
  if (!capture) {
    return 1;
  }

  unsigned int outputRate = capture->rate;
  std::size_t upsampleFactor = 1;
  if (filterConfig) {
    upsampleFactor = std::max<std::size_t>(filterConfig->upsampleFactor, 1);
    outputRate = static_cast<unsigned int>(capture->rate * upsampleFactor);
    if (upsampleFactor > 1) {
      const auto expectedInputFrames = static_cast<snd_pcm_uframes_t>(
          filterConfig->blockSize / upsampleFactor);
      if (capture->periodFrames != expectedInputFrames) {
        std::cerr << "ALSA capture period mismatch: expected "
                  << expectedInputFrames << " frames, got "
                  << capture->periodFrames << " frames\n";
        return 1;
      }
    }
  }

  const auto outputFrames =
      static_cast<size_t>(capture->periodFrames) * upsampleFactor;
  auto playback = totton::alsa::OpenPcm(
      options.outputDevice, SND_PCM_STREAM_PLAYBACK, format, options.channels,
      outputRate, outputFrames, options.bufferFrames);
  if (!playback) {
    return 1;
  }

  const size_t frameBytes =
      totton::alsa::BytesPerSample(format) * options.channels;
  std::vector<uint8_t> rawBuffer(capture->periodFrames * frameBytes);
  std::vector<float> floatBuffer;
  std::vector<float> processed;
  std::vector<uint8_t> outBuffer;

  std::cerr << "ALSA streaming started: input " << capture->rate << " Hz, "
            << "output " << outputRate << " Hz, "
            << "period " << capture->periodFrames << " frames\n";

  while (gRunning.load()) {
    if (!totton::alsa::ReadFull(capture->handle, rawBuffer.data(),
                                capture->periodFrames, gRunning)) {
      break;
    }

    if (!totton::alsa::ConvertPcmToFloat(rawBuffer.data(), format,
                                         capture->periodFrames,
                                         options.channels, &floatBuffer)) {
      std::cerr << "PCM conversion failed\n";
      break;
    }

    processed.assign(outputFrames * options.channels, 0.0f);

    if (!channelUpsamplers.empty()) {
      const size_t frames = capture->periodFrames;
      for (unsigned int ch = 0; ch < options.channels; ++ch) {
        std::vector<float> channel(frames, 0.0f);
        for (size_t i = 0; i < frames; ++i) {
          channel[i] = floatBuffer[i * options.channels + ch];
        }
        std::vector<float> out =
            channelUpsamplers[ch].ProcessBlock(channel.data(), channel.size());
        if (out.size() != outputFrames) {
          std::cerr << "Filter output size mismatch\n";
          gRunning.store(false);
          break;
        }
        for (size_t i = 0; i < outputFrames; ++i) {
          processed[i * options.channels + ch] = out[i];
        }
      }
    } else {
      processed = floatBuffer;
    }

    if (!totton::alsa::ConvertFloatToPcm(processed, format, &outBuffer)) {
      std::cerr << "PCM output conversion failed\n";
      break;
    }

    if (!totton::alsa::WriteFull(playback->handle, outBuffer.data(),
                                 outputFrames, gRunning)) {
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
