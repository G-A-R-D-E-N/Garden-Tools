#pragma once

#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

inline std::vector<std::string> installed_master_and_cc_plugins(const std::filesystem::path& dir) {
  const std::vector<std::string> official{"Fallout4.esm",    "DLCRobot.esm",      "DLCworkshop01.esm",
                                          "DLCCoast.esm",    "DLCworkshop02.esm", "DLCworkshop03.esm",
                                          "DLCNukaWorld.esm"};
  std::vector<std::string> plugins;
  std::error_code error;
  for (const auto& name : official) {
    if (std::filesystem::is_regular_file(dir / name, error)) plugins.push_back(name);
  }
  for (std::filesystem::directory_iterator it(dir, error), end; !error && it != end; it.increment(error)) {
    const auto name = it->path().filename().string();
    std::string lowered = name;
    for (auto& ch : lowered) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    std::error_code file_error;
    const bool cc_plugin = lowered.starts_with("cc") && (lowered.ends_with(".esl") || lowered.ends_with(".esm"));
    if (cc_plugin && it->is_regular_file(file_error)) {
      plugins.push_back(name);
    }
  }
  return plugins;
}
