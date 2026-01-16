#include "alsa/alsa_common.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

bool Expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    return false;
  }
  return true;
}

bool AlmostEqual(float a, float b, float eps) {
  return std::fabs(a - b) <= eps;
}

bool TestFormatParsing() {
  using totton::alsa::BytesPerSample;
  using totton::alsa::ParseFormat;

  if (!Expect(ParseFormat("s16") == SND_PCM_FORMAT_S16_LE,
              "ParseFormat s16")) {
    return false;
  }
  if (!Expect(ParseFormat("s24") == SND_PCM_FORMAT_S24_3LE,
              "ParseFormat s24")) {
    return false;
  }
  if (!Expect(ParseFormat("s32") == SND_PCM_FORMAT_S32_LE,
              "ParseFormat s32")) {
    return false;
  }
  if (!Expect(ParseFormat("bogus") == SND_PCM_FORMAT_UNKNOWN,
              "ParseFormat unknown")) {
    return false;
  }

  if (!Expect(BytesPerSample(SND_PCM_FORMAT_S16_LE) == 2,
              "BytesPerSample s16")) {
    return false;
  }
  if (!Expect(BytesPerSample(SND_PCM_FORMAT_S24_3LE) == 3,
              "BytesPerSample s24")) {
    return false;
  }
  if (!Expect(BytesPerSample(SND_PCM_FORMAT_S32_LE) == 4,
              "BytesPerSample s32")) {
    return false;
  }

  return true;
}

bool TestConversions(snd_pcm_format_t format, float eps) {
  std::vector<float> input = {-0.9f, -0.5f, 0.0f, 0.5f, 0.9f};
  std::vector<uint8_t> pcm;
  std::vector<float> output;

  if (!totton::alsa::ConvertFloatToPcm(input, format, &pcm)) {
    std::cerr << "FAIL: ConvertFloatToPcm\n";
    return false;
  }
  if (!totton::alsa::ConvertPcmToFloat(pcm.data(), format, input.size(), 1,
                                       &output)) {
    std::cerr << "FAIL: ConvertPcmToFloat\n";
    return false;
  }
  if (!Expect(output.size() == input.size(), "conversion size")) {
    return false;
  }
  for (size_t i = 0; i < input.size(); ++i) {
    if (!AlmostEqual(input[i], output[i], eps)) {
      std::cerr << "FAIL: conversion mismatch at " << i << " got "
                << output[i] << " expected " << input[i] << "\n";
      return false;
    }
  }
  return true;
}

bool TestAlsaNullDevice() {
  constexpr unsigned int kChannels = 2;
  constexpr unsigned int kRate = 44100;
  constexpr snd_pcm_uframes_t kPeriod = 128;

  auto capture = totton::alsa::OpenCaptureAutoRate(
      "null", SND_PCM_FORMAT_S32_LE, kChannels, kRate, kPeriod, 0);
  if (!Expect(capture.has_value(), "OpenCaptureAutoRate null")) {
    return false;
  }

  auto playback = totton::alsa::OpenPcm("null", SND_PCM_STREAM_PLAYBACK,
                                       SND_PCM_FORMAT_S32_LE, kChannels,
                                       capture->rate, capture->periodFrames,
                                       0);
  if (!Expect(playback.has_value(), "OpenPcm null")) {
    return false;
  }

  const size_t frameBytes =
      totton::alsa::BytesPerSample(SND_PCM_FORMAT_S32_LE) * kChannels;
  std::vector<uint8_t> raw(capture->periodFrames * frameBytes, 0);
  std::vector<float> floatSamples;
  std::atomic<bool> running{true};

  if (!totton::alsa::ReadFull(capture->handle, raw.data(),
                              capture->periodFrames, running)) {
    std::cerr << "FAIL: ReadFull null\n";
    return false;
  }
  if (!totton::alsa::ConvertPcmToFloat(raw.data(), SND_PCM_FORMAT_S32_LE,
                                       capture->periodFrames, kChannels,
                                       &floatSamples)) {
    std::cerr << "FAIL: ConvertPcmToFloat null\n";
    return false;
  }
  for (float sample : floatSamples) {
    if (!AlmostEqual(sample, 0.0f, 1e-6f)) {
      std::cerr << "FAIL: null capture not zero\n";
      return false;
    }
  }

  std::vector<uint8_t> out;
  if (!totton::alsa::ConvertFloatToPcm(floatSamples, SND_PCM_FORMAT_S32_LE,
                                       &out)) {
    std::cerr << "FAIL: ConvertFloatToPcm null\n";
    return false;
  }
  if (!totton::alsa::WriteFull(playback->handle, out.data(),
                               playback->periodFrames, running)) {
    std::cerr << "FAIL: WriteFull null\n";
    return false;
  }

  snd_pcm_drop(capture->handle);
  snd_pcm_close(capture->handle);
  snd_pcm_drain(playback->handle);
  snd_pcm_close(playback->handle);

  return true;
}

} // namespace

int main() {
  if (!TestFormatParsing()) {
    return 1;
  }
  if (!TestConversions(SND_PCM_FORMAT_S16_LE, 1e-3f)) {
    return 1;
  }
  if (!TestConversions(SND_PCM_FORMAT_S24_3LE, 2e-5f)) {
    return 1;
  }
  if (!TestConversions(SND_PCM_FORMAT_S32_LE, 1e-7f)) {
    return 1;
  }
  if (!TestAlsaNullDevice()) {
    return 1;
  }

  std::cout << "OK\n";
  return 0;
}
