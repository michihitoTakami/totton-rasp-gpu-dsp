#pragma once

#include <optional>
#include <string>

namespace totton::alsa {

struct FilterSelection {
  std::string path;
};

std::optional<FilterSelection> ResolveFilterPath(const std::string &filterPath,
                                                 const std::string &filterDir,
                                                 const std::string &phase,
                                                 unsigned int ratio,
                                                 unsigned int inputRate,
                                                 std::string *errorMessage);

} // namespace totton::alsa
