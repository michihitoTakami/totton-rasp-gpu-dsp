#include "alsa/alsa_filter_selector.h"

#include <filesystem>
#include <string>

namespace totton::alsa {

std::optional<FilterSelection> ResolveFilterPath(
    const std::string &filterPath, const std::string &filterDir,
    const std::string &phase, unsigned int ratio, unsigned int inputRate,
    std::string *errorMessage) {
  if (!filterPath.empty()) {
    if (!std::filesystem::exists(filterPath)) {
      if (errorMessage) {
        *errorMessage = "Filter file not found: " + filterPath;
      }
      return std::nullopt;
    }
    return FilterSelection{filterPath};
  }

  if (filterDir.empty()) {
    return std::nullopt;
  }

  unsigned int family = 0;
  if (inputRate % 44100 == 0) {
    family = 44;
  } else if (inputRate % 48000 == 0) {
    family = 48;
  } else {
    if (errorMessage) {
      *errorMessage = "Unsupported input rate family: " +
                      std::to_string(inputRate);
    }
    return std::nullopt;
  }

  std::string phaseSuffix = phase;
  if (phaseSuffix == "min") {
    phaseSuffix = "min_phase";
  } else if (phaseSuffix == "linear") {
    phaseSuffix = "linear_phase";
  }

  std::string path = filterDir + "/filter_" + std::to_string(family) + "k_" +
                     std::to_string(ratio) + "x_2m_" + phaseSuffix + ".json";
  if (!std::filesystem::exists(path)) {
    if (errorMessage) {
      *errorMessage = "Filter file not found: " + path;
    }
    return std::nullopt;
  }

  return FilterSelection{path};
}

} // namespace totton::alsa
