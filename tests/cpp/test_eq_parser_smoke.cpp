#include "audio/eq_parser.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>

using namespace EQ;

void ExpectNear(double value, double expected, double tol) {
  assert(std::abs(value - expected) <= tol);
}

void TestFilterTypeName() {
  assert(std::string(filterTypeName(FilterType::PK)) == "PK");
  assert(std::string(filterTypeName(FilterType::LS)) == "LS");
  assert(std::string(filterTypeName(FilterType::HS)) == "HS");
}

void TestParseFilterType() {
  assert(parseFilterType("PK") == FilterType::PK);
  assert(parseFilterType("modal") == FilterType::MODAL);
  assert(parseFilterType("LPQ") == FilterType::LPQ);
  assert(parseFilterType("HS 12DB") == FilterType::HS_12DB);
  assert(parseFilterType("unknown") == FilterType::PK);
}

void TestParseEqString() {
  EqProfile profile;
  std::string content =
      "Preamp: -6 dB\n"
      "Filter 1: ON PK Fc 1000 Hz Gain -3 dB Q 1.41\n"
      "Filter: OFF LS Fc 80 Hz Gain 2 dB Q 0.7\n"
      "Filter 3: ON PK Fc 500 Hz Gain -2 dB BW 100 Hz\n";

  assert(parseEqString(content, profile));
  ExpectNear(profile.preampDb, -6.0, 1e-9);
  assert(profile.bands.size() == 3);
  assert(profile.bands[0].enabled);
  assert(profile.bands[1].enabled == false);
  ExpectNear(profile.bands[0].frequency, 1000.0, 1e-9);
  ExpectNear(profile.bands[0].gain, -3.0, 1e-9);
  ExpectNear(profile.bands[0].q, 1.41, 1e-9);
  assert(profile.bands[2].hasBandwidthHz);
  ExpectNear(profile.bands[2].q, 5.0, 1e-9);
}

int main() {
  TestFilterTypeName();
  TestParseFilterType();
  TestParseEqString();
  std::cout << "EQ parser smoke tests passed.\n";
  return 0;
}
