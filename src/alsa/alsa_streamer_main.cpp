#include "alsa/alsa_common.h"
#include "alsa/alsa_filter_selector.h"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "vulkan/vulkan_streaming_upsampler.h"

namespace {

struct CliOptions {
  std::string inputDevice;
  std::string outputDevice;
  std::string inputFile;
  std::string outputFile;
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

class FloatRingBuffer {
public:
  explicit FloatRingBuffer(std::size_t capacity)
      : data_(std::max<std::size_t>(capacity, 1), 0.0f) {}

  std::size_t size() const { return size_; }
  std::size_t capacity() const { return data_.size(); }
  std::size_t available() const { return data_.size() - size_; }

  bool Push(const float *input, std::size_t count) {
    if (!input || count > available()) {
      return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
      data_[(head_ + size_ + i) % data_.size()] = input[i];
    }
    size_ += count;
    return true;
  }

  bool Pop(float *output, std::size_t count) {
    if (!output || count > size_) {
      return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
      output[i] = data_[(head_ + i) % data_.size()];
    }
    head_ = (head_ + count) % data_.size();
    size_ -= count;
    return true;
  }

  void Clear() {
    head_ = 0;
    size_ = 0;
  }

private:
  std::vector<float> data_;
  std::size_t head_ = 0;
  std::size_t size_ = 0;
};

void PrintUsage(const char *argv0) {
  std::cout
      << "Usage: " << argv0 << " --in <device> --out <device> [options]\n"
      << "   or: " << argv0
      << " --in-file <path> --out-file <path> --rate <hz> [options]\n\n"
      << "Options:\n"
      << "  --in-file <path>        Raw PCM input file (interleaved)\n"
      << "  --out-file <path>       Raw PCM output file (interleaved)\n"
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
    if (arg == "--in-file") {
      const char *val = requireValue("--in-file");
      if (!val) {
        return false;
      }
      options->inputFile = val;
      continue;
    }
    if (arg == "--out-file") {
      const char *val = requireValue("--out-file");
      if (!val) {
        return false;
      }
      options->outputFile = val;
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

bool ProcessFilePipeline(
    const CliOptions &options, snd_pcm_format_t format,
    std::vector<totton::vulkan::VulkanStreamingUpsampler> *channelUpsamplers,
    unsigned int periodFrames) {
  if (options.requestedRate == 0) {
    std::cerr << "--rate is required for file processing\n";
    return false;
  }
  if (options.inputFile.empty() || options.outputFile.empty()) {
    std::cerr << "--in-file and --out-file must be specified together\n";
    return false;
  }

  std::ifstream input(options.inputFile, std::ios::binary);
  if (!input) {
    std::cerr << "Failed to open input file: " << options.inputFile << "\n";
    return false;
  }
  std::ofstream output(options.outputFile, std::ios::binary | std::ios::trunc);
  if (!output) {
    std::cerr << "Failed to open output file: " << options.outputFile << "\n";
    return false;
  }

  const size_t frameBytes =
      totton::alsa::BytesPerSample(format) * options.channels;
  std::vector<uint8_t> rawBuffer(periodFrames * frameBytes);
  std::vector<float> floatBuffer;
  std::vector<float> processed;
  std::vector<uint8_t> outBuffer;

  std::cerr << "File processing started: input " << options.requestedRate
            << " Hz, period " << periodFrames << " frames\n";

  while (gRunning.load()) {
    input.read(reinterpret_cast<char *>(rawBuffer.data()),
               static_cast<std::streamsize>(rawBuffer.size()));
    std::streamsize bytesRead = input.gcount();
    if (bytesRead <= 0) {
      break;
    }

    size_t framesRead = static_cast<size_t>(bytesRead) / frameBytes;
    if (framesRead == 0) {
      break;
    }

    if (framesRead < periodFrames) {
      std::fill(rawBuffer.begin() + framesRead * frameBytes, rawBuffer.end(),
                0);
    }

    if (!totton::alsa::ConvertPcmToFloat(rawBuffer.data(), format, periodFrames,
                                         options.channels, &floatBuffer)) {
      std::cerr << "PCM conversion failed\n";
      return false;
    }

    processed.assign(floatBuffer.size(), 0.0f);

    if (channelUpsamplers && !channelUpsamplers->empty()) {
      const size_t frames = periodFrames;
      for (unsigned int ch = 0; ch < options.channels; ++ch) {
        std::vector<float> channel(frames, 0.0f);
        for (size_t i = 0; i < frames; ++i) {
          channel[i] = floatBuffer[i * options.channels + ch];
        }
        std::vector<float> out = (*channelUpsamplers)[ch].ProcessBlock(
            channel.data(), channel.size());
        if (out.size() != frames) {
          std::cerr << "Filter output size mismatch\n";
          return false;
        }
        for (size_t i = 0; i < frames; ++i) {
          processed[i * options.channels + ch] = out[i];
        }
      }
    } else {
      processed = floatBuffer;
    }

    if (!totton::alsa::ConvertFloatToPcm(processed, format, &outBuffer)) {
      std::cerr << "PCM output conversion failed\n";
      return false;
    }

    output.write(reinterpret_cast<const char *>(outBuffer.data()),
                 static_cast<std::streamsize>(framesRead * frameBytes));
  }

  std::cerr << "File processing stopped\n";
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
  const bool fileMode =
      !options.inputFile.empty() || !options.outputFile.empty();
  if (fileMode) {
    if (options.inputFile.empty() || options.outputFile.empty()) {
      std::cerr << "--in-file and --out-file must be specified together\n";
      PrintUsage(argv[0]);
      return 1;
    }
  } else if (options.inputDevice.empty() || options.outputDevice.empty()) {
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

  if (fileMode) {
    if (!ProcessFilePipeline(options, format, &channelUpsamplers,
                             periodFrames)) {
      return 1;
    }
    return 0;
  }

  auto capture = totton::alsa::OpenCaptureAutoRate(
      options.inputDevice, format, options.channels, options.requestedRate,
      periodFrames, options.bufferFrames);
  if (!capture) {
    return 1;
  }

  unsigned int outputRate = capture->rate;
  std::size_t upsampleFactor = 1;
  std::size_t blockInputFrames = capture->periodFrames;
  std::size_t blockOutputFrames = capture->periodFrames;
  if (filterConfig) {
    upsampleFactor = std::max<std::size_t>(filterConfig->upsampleFactor, 1);
    outputRate = static_cast<unsigned int>(capture->rate * upsampleFactor);
    blockOutputFrames = filterConfig->blockSize;
    blockInputFrames = filterConfig->blockSize / upsampleFactor;
    if (blockInputFrames == 0) {
      std::cerr << "Invalid filter block size for input buffering\n";
      return 1;
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
  std::vector<FloatRingBuffer> inputBuffers;
  std::optional<FloatRingBuffer> outputBuffer;
  std::vector<std::vector<float>> channelBlocks;
  std::vector<float> interleavedBlock;

  std::cerr << "ALSA streaming started: input " << capture->rate << " Hz, "
            << "output " << outputRate << " Hz, "
            << "period " << capture->periodFrames << " frames\n";

  if (!channelUpsamplers.empty()) {
    const std::size_t inputCapacity =
        std::max(blockInputFrames, static_cast<size_t>(capture->periodFrames)) *
        3;
    const std::size_t outputCapacityFrames =
        std::max(blockOutputFrames, outputFrames) * 3;

    inputBuffers.reserve(options.channels);
    for (unsigned int ch = 0; ch < options.channels; ++ch) {
      inputBuffers.emplace_back(inputCapacity);
    }
    outputBuffer.emplace(outputCapacityFrames * options.channels);

    channelBlocks.assign(options.channels,
                         std::vector<float>(blockInputFrames, 0.0f));
    interleavedBlock.assign(blockOutputFrames * options.channels, 0.0f);
  }

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

    if (!channelUpsamplers.empty()) {
      const size_t frames = capture->periodFrames;
      for (unsigned int ch = 0; ch < options.channels; ++ch) {
        std::vector<float> channel(frames, 0.0f);
        for (size_t i = 0; i < frames; ++i) {
          channel[i] = floatBuffer[i * options.channels + ch];
        }
        if (!inputBuffers[ch].Push(channel.data(), channel.size())) {
          std::cerr << "Input buffer overflow; dropping accumulated audio\n";
          for (auto &buffer : inputBuffers) {
            buffer.Clear();
          }
          break;
        }
      }

      while (gRunning.load()) {
        bool ready = outputBuffer->available() >=
                     (blockOutputFrames * options.channels);
        for (const auto &buffer : inputBuffers) {
          if (buffer.size() < blockInputFrames) {
            ready = false;
            break;
          }
        }
        if (!ready) {
          break;
        }

        for (unsigned int ch = 0; ch < options.channels; ++ch) {
          if (!inputBuffers[ch].Pop(channelBlocks[ch].data(),
                                    channelBlocks[ch].size())) {
            gRunning.store(false);
            break;
          }
          std::vector<float> out = channelUpsamplers[ch].ProcessBlock(
              channelBlocks[ch].data(), channelBlocks[ch].size());
          if (out.size() != blockOutputFrames) {
            std::cerr << "Filter output size mismatch\n";
            gRunning.store(false);
            break;
          }
          for (size_t i = 0; i < blockOutputFrames; ++i) {
            interleavedBlock[i * options.channels + ch] = out[i];
          }
        }
        if (!gRunning.load()) {
          break;
        }
        if (!outputBuffer->Push(interleavedBlock.data(),
                                interleavedBlock.size())) {
          std::cerr << "Output buffer overflow; dropping accumulated audio\n";
          outputBuffer->Clear();
          break;
        }
      }
    } else {
      processed = floatBuffer;
    }

    if (channelUpsamplers.empty()) {
      if (!totton::alsa::ConvertFloatToPcm(processed, format, &outBuffer)) {
        std::cerr << "PCM output conversion failed\n";
        break;
      }

      if (!totton::alsa::WriteFull(playback->handle, outBuffer.data(),
                                   outputFrames, gRunning)) {
        break;
      }
    } else {
      processed.assign(outputFrames * options.channels, 0.0f);
      while (outputBuffer->size() >= outputFrames * options.channels &&
             gRunning.load()) {
        if (!outputBuffer->Pop(processed.data(),
                               outputFrames * options.channels)) {
          std::cerr << "Output buffer underrun\n";
          break;
        }
        if (!totton::alsa::ConvertFloatToPcm(processed, format, &outBuffer)) {
          std::cerr << "PCM output conversion failed\n";
          gRunning.store(false);
          break;
        }
        if (!totton::alsa::WriteFull(playback->handle, outBuffer.data(),
                                     outputFrames, gRunning)) {
          gRunning.store(false);
          break;
        }
      }
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
