#include "alsa/alsa_filter_selector.h"

#include <filesystem>
#include <string>

namespace totton::alsa {

std::optional<FilterSelection>
ResolveFilterPath(const std::string &filterPath, const std::string &filterDir,
                  const std::string &phase, unsigned int ratio,
                  unsigned int inputRate, std::string *errorMessage) {
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
      *errorMessage =
          "Unsupported input rate family: " + std::to_string(inputRate);
    }
    return std::nullopt;
  }

  std::string phaseSuffix = phase;
  if (phaseSuffix == "min") {
    phaseSuffix = "min_phase";
  } else if (phaseSuffix == "linear") {
    phaseSuffix = "linear_phase";
  }

  std::string prefix = "filter_" + std::to_string(family) + "k_" +
                       std::to_string(ratio) + "x_";
  std::string suffix = "_" + phaseSuffix + ".json";
  std::optional<std::filesystem::path> bestPath;
  unsigned int bestTaps = 0;

  for (const auto &entry : std::filesystem::directory_iterator(filterDir)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    std::string filename = entry.path().filename().string();
    if (filename.size() <= prefix.size() + suffix.size()) {
      continue;
    }
    if (filename.rfind(prefix, 0) != 0) {
      continue;
    }
    if (filename.compare(filename.size() - suffix.size(), suffix.size(),
                         suffix) != 0) {
      continue;
    }
    std::string tapsToken = filename.substr(
        prefix.size(), filename.size() - prefix.size() - suffix.size());
    unsigned int taps = 0;
    if (tapsToken == "2m") {
      taps = 640000;
    } else {
      try {
        size_t parsed = 0;
        taps = static_cast<unsigned int>(std::stoul(tapsToken, &parsed, 10));
        if (parsed != tapsToken.size()) {
          taps = 0;
        }
      } catch (const std::exception &) {
        taps = 0;
      }
    }
    if (taps == 0) {
      continue;
    }
    if (taps > bestTaps) {
      bestTaps = taps;
      bestPath = entry.path();
    }
  }

  if (!bestPath.has_value()) {
    if (errorMessage) {
      *errorMessage =
          "Filter file not found: " + filterDir + "/" + prefix + "*" + suffix;
    }
    return std::nullopt;
  }

  return FilterSelection{bestPath->string()};
}

} // namespace totton::alsa
