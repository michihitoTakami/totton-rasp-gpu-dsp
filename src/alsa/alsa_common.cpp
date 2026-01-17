#include "alsa/alsa_common.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <vector>

namespace totton::alsa {

snd_pcm_format_t ParseFormat(const std::string &format) {
  std::string lower = format;
  std::transform(
      lower.begin(), lower.end(), lower.begin(),
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
                  snd_pcm_uframes_t *periodOut, snd_pcm_uframes_t *bufferOut,
                  unsigned int *rateOut, bool playback) {
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

std::optional<AlsaHandle>
OpenPcm(const std::string &device, snd_pcm_stream_t stream,
        snd_pcm_format_t format, unsigned int channels, unsigned int rate,
        snd_pcm_uframes_t period, snd_pcm_uframes_t buffer) {
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

std::optional<AlsaHandle>
OpenCaptureAutoRate(const std::string &device, snd_pcm_format_t format,
                    unsigned int channels, unsigned int requestedRate,
                    snd_pcm_uframes_t period, snd_pcm_uframes_t buffer) {
  if (requestedRate != 0) {
    return OpenPcm(device, SND_PCM_STREAM_CAPTURE, format, channels,
                   requestedRate, period, buffer);
  }

  const unsigned int candidates[] = {44100, 48000,  88200,
                                     96000, 176400, 192000};
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

bool ReadFull(snd_pcm_t *handle, void *buffer, snd_pcm_uframes_t frames,
              const std::atomic<bool> &running) {
  auto *ptr = static_cast<uint8_t *>(buffer);
  snd_pcm_uframes_t remaining = frames;
  size_t frameBytes = static_cast<size_t>(snd_pcm_frames_to_bytes(handle, 1));

  while (remaining > 0 && running.load()) {
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

bool WriteFull(snd_pcm_t *handle, const void *buffer, snd_pcm_uframes_t frames,
               const std::atomic<bool> &running) {
  auto *ptr = static_cast<const uint8_t *>(buffer);
  snd_pcm_uframes_t remaining = frames;
  size_t frameBytes = static_cast<size_t>(snd_pcm_frames_to_bytes(handle, 1));

  while (remaining > 0 && running.load()) {
    snd_pcm_sframes_t n = snd_pcm_writei(handle, ptr, remaining);
    if (n == -EPIPE || n == -ESTRPIPE || n == -EINTR) {
      if (!RecoverPcm(handle, static_cast<int>(n), "ALSA playback")) {
        return false;
      }
      continue;
    }
    if (n < 0) {
      std::cerr << "ALSA playback error: " << snd_strerror(static_cast<int>(n))
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

} // namespace totton::alsa
