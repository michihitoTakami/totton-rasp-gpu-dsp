#pragma once

#include <alsa/asoundlib.h>

#include <atomic>
#include <optional>
#include <string>
#include <vector>

namespace totton::alsa {

struct AlsaHandle {
  snd_pcm_t *handle = nullptr;
  snd_pcm_uframes_t periodFrames = 0;
  snd_pcm_uframes_t bufferFrames = 0;
  unsigned int rate = 0;
};

snd_pcm_format_t ParseFormat(const std::string &format);
size_t BytesPerSample(snd_pcm_format_t format);

bool ConvertPcmToFloat(const void *src, snd_pcm_format_t format, size_t frames,
                       unsigned int channels, std::vector<float> *dst);
bool ConvertFloatToPcm(const std::vector<float> &src, snd_pcm_format_t format,
                       std::vector<uint8_t> *dst);

bool ConfigurePcm(snd_pcm_t *handle, snd_pcm_format_t format,
                  unsigned int channels, unsigned int rate,
                  snd_pcm_uframes_t requestedPeriod,
                  snd_pcm_uframes_t requestedBuffer,
                  snd_pcm_uframes_t *periodOut, snd_pcm_uframes_t *bufferOut,
                  unsigned int *rateOut, bool playback);

std::optional<AlsaHandle>
OpenPcm(const std::string &device, snd_pcm_stream_t stream,
        snd_pcm_format_t format, unsigned int channels, unsigned int rate,
        snd_pcm_uframes_t period, snd_pcm_uframes_t buffer);

std::optional<AlsaHandle>
OpenCaptureAutoRate(const std::string &device, snd_pcm_format_t format,
                    unsigned int channels, unsigned int requestedRate,
                    snd_pcm_uframes_t period, snd_pcm_uframes_t buffer);

bool RecoverPcm(snd_pcm_t *handle, int err, const char *label);
bool ReadFull(snd_pcm_t *handle, void *buffer, snd_pcm_uframes_t frames,
              const std::atomic<bool> &running);
bool WriteFull(snd_pcm_t *handle, const void *buffer, snd_pcm_uframes_t frames,
               const std::atomic<bool> &running);

} // namespace totton::alsa
