#pragma once

#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <system_error>
#include <vector>

inline std::string lowered_name(std::string name) {
  for (auto& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return name;
}

inline std::set<std::string> listed_in_ccc(const std::filesystem::path& ccc) {
  std::set<std::string> names;
  std::ifstream in(ccc);
  for (std::string line; std::getline(in, line);) {
    if (line.starts_with("\xEF\xBB\xBF")) line.erase(0, 3);
    const auto first = line.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) continue;
    names.insert(lowered_name(line.substr(first, line.find_last_not_of(" \t\r\n") - first + 1)));
  }
  return names;
}

inline std::vector<std::string> installed_master_and_cc_plugins(const std::filesystem::path& dir) {
  const std::vector<std::string> official{"Fallout4.esm",    "DLCRobot.esm",      "DLCworkshop01.esm",
                                          "DLCCoast.esm",    "DLCworkshop02.esm", "DLCworkshop03.esm",
                                          "DLCNukaWorld.esm"};
  std::vector<std::string> plugins;
  std::error_code error;
  for (const auto& name : official) {
    if (std::filesystem::is_regular_file(dir / name, error)) plugins.push_back(name);
  }
  const auto ccc = dir / ".." / "Fallout4.ccc";
  std::error_code ccc_error;
  const auto listed = std::filesystem::is_regular_file(ccc, ccc_error) ? listed_in_ccc(ccc) : std::set<std::string>{};
  const bool use_ccc = !listed.empty();
  for (std::filesystem::directory_iterator it(dir, error), end; !error && it != end; it.increment(error)) {
    const auto name = it->path().filename().string();
    const auto lowered = lowered_name(name);
    std::error_code file_error;
    const bool cc_plugin = lowered.starts_with("cc") && (lowered.ends_with(".esl") || lowered.ends_with(".esm"));
    const bool in_ccc = !use_ccc || listed.contains(lowered);
    if (cc_plugin && in_ccc && it->is_regular_file(file_error)) {
      plugins.push_back(name);
    }
  }
  return plugins;
}
