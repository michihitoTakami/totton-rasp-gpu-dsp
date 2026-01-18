#include "alsa/alsa_filter_selector.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

bool Expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    return false;
  }
  return true;
}

std::filesystem::path WriteDummyFilter(const std::filesystem::path &dir,
                                       const std::string &name) {
  std::filesystem::create_directories(dir);
  auto path = dir / name;
  std::ofstream file(path);
  file << "{}\n";
  return path;
}

} // namespace

int main() {
  std::string error;
  auto tempDir = std::filesystem::temp_directory_path() / "totton_alsa_filters";

  auto direct = WriteDummyFilter(tempDir, "direct.json");
  auto directSelection = totton::alsa::ResolveFilterPath(
      direct.string(), "", "min", 1, 44100, &error);
  if (!Expect(directSelection.has_value(), "direct selection")) {
    return 1;
  }
  if (!Expect(directSelection->path == direct.string(),
              "direct selection path")) {
    return 1;
  }

  auto autoPath =
      WriteDummyFilter(tempDir, "filter_44k_2x_80000_min_phase.json");
  auto legacyPath =
      WriteDummyFilter(tempDir, "filter_44k_2x_2m_min_phase.json");
  auto autoSelection = totton::alsa::ResolveFilterPath("", tempDir.string(),
                                                       "min", 2, 44100, &error);
  if (!Expect(autoSelection.has_value(), "auto selection")) {
    return 1;
  }
  if (!Expect(autoSelection->path == legacyPath.string(),
              "auto selection prefers highest taps")) {
    return 1;
  }

  error.clear();
  auto invalid = totton::alsa::ResolveFilterPath("", tempDir.string(), "min", 2,
                                                 32000, &error);
  if (!Expect(!invalid.has_value(), "invalid family")) {
    return 1;
  }
  if (!Expect(!error.empty(), "invalid family error")) {
    return 1;
  }

  error.clear();
  auto missingDir = tempDir / "missing";
  auto missingSelection = totton::alsa::ResolveFilterPath(
      "", missingDir.string(), "min", 2, 44100, &error);
  if (!Expect(!missingSelection.has_value(), "missing directory")) {
    return 1;
  }
  if (!Expect(!error.empty(), "missing directory error")) {
    return 1;
  }

  std::cout << "OK\n";
  return 0;
}
