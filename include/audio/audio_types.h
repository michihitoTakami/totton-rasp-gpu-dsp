/**
 * @file audio_types.h
 * @brief Common audio type definitions for totton-rasp-gpu-dsp
 */

#pragma once

namespace AudioEngine {

// Rate family enumeration
enum class RateFamily {
    RATE_UNKNOWN = 0,
    RATE_44K = 1,  // 44.1kHz family (44.1k, 88.2k, 176.4k, 352.8k, 705.6k)
    RATE_48K = 2   // 48kHz family (48k, 96k, 192k, 384k, 768k)
};

}  // namespace AudioEngine
