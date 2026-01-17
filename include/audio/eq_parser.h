#ifndef EQ_PARSER_H
#define EQ_PARSER_H

#include <string>
#include <vector>

namespace EQ {

// Filter types supported by Equalizer APO format
// Reference:
// https://sourceforge.net/p/equalizerapo/wiki/Configuration%20reference/
enum class FilterType {
  // Peaking filters
  PK,
  MODAL,
  PEQ,
  // Pass filters
  LP,
  LPQ,
  HP,
  HPQ,
  BP,
  // Notch and All-pass
  NO,
  AP,
  // Shelf filters
  LS,
  HS,
  LSC,
  HSC,
  LSQ,
  HSQ,
  // Fixed-slope shelf filters
  LS_6DB,
  LS_12DB,
  HS_6DB,
  HS_12DB,
};

struct EqBand {
  bool enabled = true;
  FilterType type = FilterType::PK;
  double frequency = 1000.0;
  double gain = 0.0;
  double q = 1.0;
  bool hasBandwidthHz = false;
  double bandwidthHz = 0.0;
  bool hasBandwidthOct = false;
  double bandwidthOct = 0.0;
};

struct EqProfile {
  std::string name;
  double preampDb = 0.0;
  std::vector<EqBand> bands;

  bool isEmpty() const { return bands.empty() && preampDb == 0.0; }
  size_t activeBandCount() const;
};

bool parseEqFile(const std::string &filePath, EqProfile &profile);
bool parseEqString(const std::string &content, EqProfile &profile);
const char *filterTypeName(FilterType type);
FilterType parseFilterType(const std::string &typeStr);

} // namespace EQ

#endif // EQ_PARSER_H
