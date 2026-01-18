/**
 * @file test_auto_negotiation.cpp
 * @brief Unit tests for Auto-Negotiation logic (Issue #12)
 *
 * Tests the automatic negotiation of sample rates between input source
 * and DAC capabilities. Covers:
 * - Rate family detection
 * - Output rate negotiation
 * - Cross-family switching detection (requiresReconfiguration flag)
 * - DAC capability validation
 */

#include "audio/auto_negotiation.h"
#include "io/dac_capability.h"

#include <cassert>
#include <iostream>

using namespace AutoNegotiation;
using namespace AudioEngine;

// Helper to create a mock DAC capability that supports all rates
DacCapability::Capability createFullCapabilityDac() {
  DacCapability::Capability cap;
  cap.deviceName = "test:full";
  cap.minSampleRate = 44100;
  cap.maxSampleRate = 768000;
  cap.supportedRates = {44100,  48000,  88200,  96000,  176400,
                        192000, 352800, 384000, 705600, 768000};
  cap.maxChannels = 2;
  cap.isValid = true;
  cap.errorMessage.clear();
  return cap;
}

// Helper to create a mock DAC capability limited to 192kHz
DacCapability::Capability createLimitedDac() {
  DacCapability::Capability cap;
  cap.deviceName = "test:limited";
  cap.minSampleRate = 44100;
  cap.maxSampleRate = 192000;
  cap.supportedRates = {44100, 48000, 88200, 96000, 176400, 192000};
  cap.maxChannels = 2;
  cap.isValid = true;
  cap.errorMessage.clear();
  return cap;
}

// Helper to create an invalid DAC capability
DacCapability::Capability createInvalidDac() {
  DacCapability::Capability cap;
  cap.deviceName = "test:invalid";
  cap.isValid = false;
  cap.errorMessage = "Device not found";
  return cap;
}

// Test rate family detection
void testRateFamilyDetection() {
  std::cout << "Testing rate family detection..." << std::endl;

  // 44.1kHz family
  assert(getRateFamily(44100) == RateFamily::RATE_44K);
  assert(getRateFamily(88200) == RateFamily::RATE_44K);
  assert(getRateFamily(176400) == RateFamily::RATE_44K);
  assert(getRateFamily(352800) == RateFamily::RATE_44K);
  assert(getRateFamily(705600) == RateFamily::RATE_44K);

  // 48kHz family
  assert(getRateFamily(48000) == RateFamily::RATE_48K);
  assert(getRateFamily(96000) == RateFamily::RATE_48K);
  assert(getRateFamily(192000) == RateFamily::RATE_48K);
  assert(getRateFamily(384000) == RateFamily::RATE_48K);
  assert(getRateFamily(768000) == RateFamily::RATE_48K);

  std::cout << "  ✓ Rate family detection tests passed" << std::endl;
}

// Test same family detection
void testSameFamilyDetection() {
  std::cout << "Testing same family detection..." << std::endl;

  assert(isSameFamily(44100, 88200));
  assert(isSameFamily(44100, 176400));
  assert(isSameFamily(48000, 96000));
  assert(isSameFamily(48000, 192000));

  assert(!isSameFamily(44100, 48000));
  assert(!isSameFamily(88200, 96000));
  assert(!isSameFamily(176400, 192000));

  std::cout << "  ✓ Same family detection tests passed" << std::endl;
}

// Test upsampling ratio calculation
void testUpsampleRatio() {
  std::cout << "Testing upsampling ratio calculation..." << std::endl;

  assert(calculateUpsampleRatio(44100, 705600) == 16);
  assert(calculateUpsampleRatio(88200, 705600) == 8);
  assert(calculateUpsampleRatio(176400, 705600) == 4);
  assert(calculateUpsampleRatio(352800, 705600) == 2);

  assert(calculateUpsampleRatio(48000, 768000) == 16);
  assert(calculateUpsampleRatio(96000, 768000) == 8);
  assert(calculateUpsampleRatio(192000, 768000) == 4);
  assert(calculateUpsampleRatio(384000, 768000) == 2);

  // Invalid cases
  assert(calculateUpsampleRatio(0, 705600) == 0);
  assert(calculateUpsampleRatio(44100, 0) == 0);
  assert(calculateUpsampleRatio(44100, 100000) == 0);

  std::cout << "  ✓ Upsampling ratio tests passed" << std::endl;
}

// Test negotiation with full DAC
void testNegotiationFullDac() {
  std::cout << "Testing negotiation with full DAC..." << std::endl;

  auto dac = createFullCapabilityDac();

  // 44.1kHz family
  auto config = negotiate(44100, dac);
  assert(config.isValid);
  assert(config.inputRate == 44100);
  assert(config.inputFamily == RateFamily::RATE_44K);
  assert(config.outputRate == 705600);
  assert(config.upsampleRatio == 16);
  assert(config.requiresReconfiguration); // First time

  config = negotiate(88200, dac);
  assert(config.isValid);
  assert(config.outputRate == 705600);
  assert(config.upsampleRatio == 8);

  // 48kHz family
  config = negotiate(48000, dac);
  assert(config.isValid);
  assert(config.inputFamily == RateFamily::RATE_48K);
  assert(config.outputRate == 768000);
  assert(config.upsampleRatio == 16);

  std::cout << "  ✓ Negotiation with full DAC tests passed" << std::endl;
}

// Test reconfiguration detection
void testReconfigurationDetection() {
  std::cout << "Testing reconfiguration detection..." << std::endl;

  auto dac = createFullCapabilityDac();

  // Start with 44100Hz
  auto config1 = negotiate(44100, dac, 0);
  assert(config1.requiresReconfiguration); // First time

  // Switch to 88200Hz (same family) - no reconfiguration
  auto config2 = negotiate(88200, dac, 705600);
  assert(!config2.requiresReconfiguration);
  assert(config2.outputRate == 705600);

  // Switch to 48000Hz (different family) - requires reconfiguration
  auto config3 = negotiate(48000, dac, 705600);
  assert(config3.requiresReconfiguration);
  assert(config3.outputRate == 768000);

  std::cout << "  ✓ Reconfiguration detection tests passed" << std::endl;
}

// Test limited DAC
void testLimitedDac() {
  std::cout << "Testing limited DAC (192kHz max)..." << std::endl;

  auto dac = createLimitedDac();

  auto config44k = negotiate(44100, dac);
  assert(config44k.isValid);
  assert(config44k.outputRate == 176400); // Fallback to 176400
  assert(config44k.upsampleRatio == 4);

  auto config48k = negotiate(48000, dac);
  assert(config48k.isValid);
  assert(config48k.outputRate == 192000); // Fallback to 192000
  assert(config48k.upsampleRatio == 4);

  std::cout << "  ✓ Limited DAC tests passed" << std::endl;
}

// Test error cases
void testErrorCases() {
  std::cout << "Testing error cases..." << std::endl;

  auto dac = createFullCapabilityDac();
  auto invalidDac = createInvalidDac();

  // Invalid DAC
  auto config = negotiate(44100, invalidDac);
  assert(!config.isValid);
  assert(!config.errorMessage.empty());

  // Invalid input rate
  config = negotiate(0, dac);
  assert(!config.isValid);

  config = negotiate(-1, dac);
  assert(!config.isValid);

  // Unsupported ratio (11025Hz would require 64x)
  config = negotiate(11025, dac);
  assert(!config.isValid);

  std::cout << "  ✓ Error case tests passed" << std::endl;
}

int main() {
  std::cout << "\n=== Auto-Negotiation Tests (Issue #12) ===" << std::endl;

  testRateFamilyDetection();
  testSameFamilyDetection();
  testUpsampleRatio();
  testNegotiationFullDac();
  testReconfigurationDetection();
  testLimitedDac();
  testErrorCases();

  std::cout << "\n✓ All tests passed!\n" << std::endl;
  return 0;
}
