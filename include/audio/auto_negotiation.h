/**
 * @file auto_negotiation.h
 * @brief Auto-negotiation of sample rates between input and DAC capabilities
 *
 * This module implements Issue #12: Auto-Negotiation v1
 * - Probes output ALSA device capabilities
 * - Automatically selects optimal output rate and upsampling ratio
 * - Detects 44.1k vs 48k family and selects appropriate filter
 * - Provides fallback when DAC doesn't support high rates
 */

#pragma once

#include "audio/audio_types.h"
#include "io/dac_capability.h"

#include <string>

namespace AutoNegotiation {

// Result of auto-negotiation
struct NegotiatedConfig {
    int inputRate;                          // Input sample rate
    AudioEngine::RateFamily inputFamily;    // RATE_44K or RATE_48K
    int outputRate;                         // Negotiated output rate
    int upsampleRatio;                      // Upsampling ratio (outputRate / inputRate)
    bool isValid;                           // Whether negotiation succeeded
    bool requiresReconfiguration;           // True if ALSA needs reconfiguration (family change)
    std::string errorMessage;               // Error message if failed
};

/**
 * @brief Negotiate optimal output rate based on input and DAC capabilities
 *
 * @param inputRate Current input sample rate (e.g., 44100, 48000, 96000)
 * @param dacCap DAC capability information from DacCapability::scan()
 * @param currentOutputRate Currently configured output rate (0 if not configured)
 * @return NegotiatedConfig with optimal settings
 *
 * Design Decision (Issue #12):
 *   Cross-family switching (44.1k <-> 48k) sets requiresReconfiguration=true.
 *   This causes ~1 second soft mute during ALSA reconfiguration.
 *   Same-family switching is instant and glitch-free.
 *   No resampling is used to preserve ultimate audio quality.
 */
NegotiatedConfig negotiate(int inputRate, const DacCapability::Capability& dacCap,
                           int currentOutputRate = 0);

/**
 * @brief Get the target output rate for a given family
 * @return TARGET_RATE_44K_FAMILY (705600) or TARGET_RATE_48K_FAMILY (768000)
 */
int getTargetRateForFamily(AudioEngine::RateFamily family);

/**
 * @brief Get the best supported rate for a family on a specific DAC
 *
 * Falls back to lower multiples if the ideal rate is not supported
 */
int getBestRateForFamily(AudioEngine::RateFamily family,
                         const DacCapability::Capability& dacCap);

/**
 * @brief Calculate the upsampling ratio
 * @return Upsampling ratio, or 0 if not an integer ratio
 */
int calculateUpsampleRatio(int inputRate, int outputRate);

/**
 * @brief Check if two rates belong to the same family
 */
bool isSameFamily(int rate1, int rate2);

/**
 * @brief Determine the rate family for a given sample rate
 */
AudioEngine::RateFamily getRateFamily(int sampleRate);

}  // namespace AutoNegotiation
