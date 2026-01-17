#ifndef EQ_TO_FIR_H
#define EQ_TO_FIR_H

#include "audio/eq_parser.h"

#include <complex>
#include <vector>

namespace EQ {

struct BiquadCoeffs {
  double b0, b1, b2;
  double a1, a2;
};

BiquadCoeffs calculateBiquadCoeffs(const EqBand &band, double sampleRate);

std::vector<std::complex<double>>
biquadFrequencyResponse(const std::vector<double> &frequencies,
                        const BiquadCoeffs &coeffs, double sampleRate);

std::vector<std::complex<double>>
computeEqFrequencyResponse(const std::vector<double> &frequencies,
                           const EqProfile &profile, double sampleRate);

std::vector<double> generateR2cFftFrequencies(size_t numBins,
                                              size_t fullFftSize,
                                              double sampleRate);

std::vector<std::complex<double>>
computeEqResponseForFft(size_t filterFftSize, size_t fullFftSize,
                        double outputSampleRate, const EqProfile &profile);

std::vector<double> computeEqMagnitudeForFft(size_t filterFftSize,
                                             size_t fullFftSize,
                                             double outputSampleRate,
                                             const EqProfile &profile);

} // namespace EQ

#endif // EQ_TO_FIR_H
