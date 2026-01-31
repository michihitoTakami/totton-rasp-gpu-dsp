#include "alsa/alsa_common.h"
#include "alsa/alsa_filter_selector.h"
#include "io/audio_ring_buffer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
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
         "size; 1024 if no filter)\n"
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
  std::size_t upsampleFactor = 1;
  std::size_t blockInputFrames = 0;
  std::size_t blockOutputFrames = 0;
  if (filterConfig) {
    upsampleFactor = std::max<std::size_t>(filterConfig->upsampleFactor, 1);
    blockOutputFrames = filterConfig->blockSize;
    blockInputFrames = filterConfig->blockSize / upsampleFactor;
    if (blockInputFrames == 0) {
      std::cerr << "Invalid filter block size for input buffering.\n";
      return 1;
    }
  }

  unsigned int periodFrames = options.periodFrames;
  if (fileMode && blockInputFrames > 0) {
    periodFrames = static_cast<unsigned int>(blockInputFrames);
  } else if (periodFrames == 0) {
    if (blockInputFrames > 0) {
      periodFrames = static_cast<unsigned int>(blockInputFrames);
    } else {
      periodFrames = 1024;
    }
  }

  if (fileMode) {
    if (!ProcessFilePipeline(options, format, &channelUpsamplers,
                             periodFrames)) {
      return 1;
    }
    return 0;
  }

  snd_pcm_uframes_t captureBufferFrames =
      static_cast<snd_pcm_uframes_t>(options.bufferFrames);
  if (captureBufferFrames == 0 && blockInputFrames > 0 &&
      upsampleFactor > 1) {
    const std::size_t multiplier =
        std::min<std::size_t>(std::max<std::size_t>(4, upsampleFactor * 2), 16);
    captureBufferFrames =
        static_cast<snd_pcm_uframes_t>(periodFrames * multiplier);
    std::cerr << "ALSA capture buffer auto-scaled: period " << periodFrames
              << " frames, buffer " << captureBufferFrames << " frames\n";
  }

  auto capture = totton::alsa::OpenCaptureAutoRate(
      options.inputDevice, format, options.channels, options.requestedRate,
      periodFrames, captureBufferFrames);
  if (!capture) {
    return 1;
  }

  unsigned int outputRate = capture->rate;
  std::size_t streamInputFrames = capture->periodFrames;
  std::size_t streamOutputFrames = capture->periodFrames;
  if (filterConfig) {
    outputRate = static_cast<unsigned int>(capture->rate * upsampleFactor);
    streamInputFrames = blockInputFrames;
    streamOutputFrames = blockOutputFrames;
  }

  const auto outputFrames =
      static_cast<size_t>(capture->periodFrames) * upsampleFactor;
  const snd_pcm_uframes_t outputBufferFrames =
      (options.bufferFrames > 0)
          ? static_cast<snd_pcm_uframes_t>(options.bufferFrames) *
                upsampleFactor
          : 0;
  auto playback = totton::alsa::OpenPcm(
      options.outputDevice, SND_PCM_STREAM_PLAYBACK, format, options.channels,
      outputRate, outputFrames, outputBufferFrames);
  if (!playback) {
    return 1;
  }

  const size_t frameBytes =
      totton::alsa::BytesPerSample(format) * options.channels;
  std::vector<uint8_t> rawBuffer(capture->periodFrames * frameBytes);
  std::vector<float> floatBuffer;
  std::vector<float> processed;
  std::vector<uint8_t> outBuffer;
  std::vector<std::unique_ptr<AudioRingBuffer>> inputBuffers;
  AudioRingBuffer outputBuffer;
  std::thread processingThread;

  std::cerr << "ALSA streaming started: input " << capture->rate << " Hz, "
            << "output " << outputRate << " Hz, "
            << "period " << capture->periodFrames << " frames\n";

  if (!channelUpsamplers.empty()) {
    const std::size_t inputCapacity =
        std::max(streamInputFrames,
                 static_cast<size_t>(capture->periodFrames)) *
        3;
    const std::size_t outputCapacityFrames =
        std::max(streamOutputFrames, outputFrames) * 3;

    inputBuffers.clear();
    inputBuffers.reserve(options.channels);
    for (unsigned int ch = 0; ch < options.channels; ++ch) {
      auto buffer = std::make_unique<AudioRingBuffer>();
      buffer->init(inputCapacity);
      inputBuffers.emplace_back(std::move(buffer));
    }
    outputBuffer.init(outputCapacityFrames * options.channels);

    processingThread = std::thread([&] {
      std::vector<std::vector<float>> channelBlocks(
          options.channels, std::vector<float>(streamInputFrames, 0.0f));
      std::vector<float> interleavedBlock(streamOutputFrames * options.channels,
                                          0.0f);
      std::size_t dropCount = 0;
      auto lastDropLog = std::chrono::steady_clock::now();

      while (gRunning.load()) {
        bool ready = outputBuffer.availableToWrite() >=
                     (streamOutputFrames * options.channels);
        for (const auto &buffer : inputBuffers) {
          if (buffer->availableToRead() < streamInputFrames) {
            ready = false;
            break;
          }
        }
        if (!ready) {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
          continue;
        }

        bool blockReady = true;
        for (unsigned int ch = 0; ch < options.channels; ++ch) {
          if (!inputBuffers[ch]->read(channelBlocks[ch].data(),
                                      channelBlocks[ch].size())) {
            blockReady = false;
            break;
          }
          std::vector<float> out = channelUpsamplers[ch].ProcessBlock(
              channelBlocks[ch].data(), channelBlocks[ch].size());
          if (out.size() != streamOutputFrames) {
            std::cerr << "Filter output size mismatch\n";
            gRunning.store(false);
            blockReady = false;
            break;
          }
          for (size_t i = 0; i < streamOutputFrames; ++i) {
            interleavedBlock[i * options.channels + ch] = out[i];
          }
        }
        if (!gRunning.load()) {
          break;
        }
        if (!blockReady) {
          continue;
        }
        if (!outputBuffer.write(interleavedBlock.data(),
                                interleavedBlock.size())) {
          ++dropCount;
          const auto now = std::chrono::steady_clock::now();
          if (now - lastDropLog > std::chrono::seconds(1)) {
            std::cerr << "Output buffer overflow; dropping audio ("
                      << dropCount << ")\n";
            lastDropLog = now;
          }
        }
      }
    });
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
        if (!inputBuffers[ch]->write(channel.data(), channel.size())) {
          std::cerr << "Input buffer overflow; dropping audio\n";
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
      bool wroteOutput = false;
      while (outputBuffer.availableToRead() >=
                 outputFrames * options.channels &&
             gRunning.load()) {
        if (!outputBuffer.read(processed.data(),
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
        wroteOutput = true;
      }
      if (!wroteOutput && gRunning.load()) {
        if (!totton::alsa::ConvertFloatToPcm(processed, format, &outBuffer)) {
          std::cerr << "PCM output conversion failed\n";
          gRunning.store(false);
        } else if (!totton::alsa::WriteFull(playback->handle, outBuffer.data(),
                                            outputFrames, gRunning)) {
          gRunning.store(false);
        }
      }
    }
  }

  if (processingThread.joinable()) {
    processingThread.join();
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
