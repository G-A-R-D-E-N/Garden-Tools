#pragma once

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

namespace fo4::test {

inline std::optional<std::string> read_data_dir_env() {
  const char* value = std::getenv("FO4_DATA_DIR");
  if (value == nullptr) return std::nullopt;
  return std::string{value};
}

inline const std::optional<std::string>& fo4_data_dir_text() {
  static const std::optional<std::string> value = read_data_dir_env();
  return value;
}

inline std::optional<std::filesystem::path> fo4_data_dir() {
  const auto& text = fo4_data_dir_text();
  if (!text) return std::nullopt;
  return std::filesystem::path{*text};
}

inline const char* fo4_data_dir_cstr() {
  const auto& text = fo4_data_dir_text();
  return text ? text->c_str() : nullptr;
}

}  // namespace fo4::test

// Name must stay FO4_REQUIRE_DATA_DIR: check_real_data_regex.py matches it.
#define FO4_REQUIRE_DATA_DIR(var, message) \
  const char* var = ::fo4::test::fo4_data_dir_cstr(); \
  if (var == nullptr) GTEST_SKIP() << message
