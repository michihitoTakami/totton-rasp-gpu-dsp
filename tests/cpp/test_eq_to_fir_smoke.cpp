#include "audio/eq_to_fir.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using namespace EQ;

constexpr double SAMPLE_RATE = 44100.0;

void ExpectNear(double value, double expected, double tol) {
  assert(std::abs(value - expected) <= tol);
}

void TestBiquadUnity() {
  EqBand band;
  band.enabled = false;
  band.gain = 6.0;
  BiquadCoeffs c = calculateBiquadCoeffs(band, SAMPLE_RATE);
  ExpectNear(c.b0, 1.0, 1e-9);
  ExpectNear(c.b1, 0.0, 1e-9);
  ExpectNear(c.b2, 0.0, 1e-9);
  ExpectNear(c.a1, 0.0, 1e-9);
  ExpectNear(c.a2, 0.0, 1e-9);
}

void TestFrequencyResponseAtCenter() {
  EqBand band;
  band.enabled = true;
  band.type = FilterType::PK;
  band.frequency = 1000.0;
  band.gain = 6.0;
  band.q = 1.41;

  BiquadCoeffs c = calculateBiquadCoeffs(band, SAMPLE_RATE);
  std::vector<double> freqs = {1000.0};
  auto response = biquadFrequencyResponse(freqs, c, SAMPLE_RATE);
  double magnitudeDb = 20.0 * std::log10(std::abs(response[0]));
  ExpectNear(magnitudeDb, 6.0, 0.6);
}

void TestEqMagnitudeUnity() {
  EqProfile profile;
  size_t fftSize = 1024;
  size_t numBins = fftSize / 2 + 1;
  double outputRate = SAMPLE_RATE * 16;
  auto magnitude =
      computeEqMagnitudeForFft(numBins, fftSize, outputRate, profile);
  assert(magnitude.size() == numBins);
  for (double value : magnitude) {
    ExpectNear(value, 1.0, 1e-6);
  }
}

void TestEqMagnitudeAutoNormalize() {
  EqProfile profile;
  EqBand band;
  band.enabled = true;
  band.type = FilterType::PK;
  band.frequency = 1000.0;
  band.gain = 6.0;
  band.q = 1.0;
  profile.bands.push_back(band);

  size_t fftSize = 1024;
  size_t numBins = fftSize / 2 + 1;
  double outputRate = SAMPLE_RATE * 16;
  auto magnitude =
      computeEqMagnitudeForFft(numBins, fftSize, outputRate, profile);
  assert(magnitude.size() == numBins);

  double maxValue = *std::max_element(magnitude.begin(), magnitude.end());
  double minValue = *std::min_element(magnitude.begin(), magnitude.end());
  ExpectNear(maxValue, 1.0, 1e-6);
  assert(minValue < 0.95);
}

int main() {
  TestBiquadUnity();
  TestFrequencyResponseAtCenter();
  TestEqMagnitudeUnity();
  TestEqMagnitudeAutoNormalize();
  std::cout << "EQ to FIR smoke tests passed.\n";
  return 0;
}
