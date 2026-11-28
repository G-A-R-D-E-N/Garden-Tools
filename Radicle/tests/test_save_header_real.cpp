#include "fo4/save/save_header.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t k_plausible_max_level = 9999;

bool has_fos_extension(const std::filesystem::path& path) {
  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return extension == ".fos";
}

std::vector<std::filesystem::path> collect_saves(const std::filesystem::path& root) {
  std::vector<std::filesystem::path> saves;
  if (std::filesystem::is_regular_file(root)) {
    saves.push_back(root);
    return saves;
  }
  if (std::filesystem::is_directory(root)) {
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
      if (entry.is_regular_file() && has_fos_extension(entry.path())) {
        saves.push_back(entry.path());
      }
    }
  }
  std::sort(saves.begin(), saves.end());
  return saves;
}

}  // namespace

// Detects a header layout or version assumption that holds for synthetic saves but not for the game's own.
TEST(SaveHeaderReal, ParsesEveryRealSaveStructurally) {
  const char* location = std::getenv("RADICLE_FO4_SAVE_PATH");
  if (location == nullptr || *location == '\0') {
    GTEST_SKIP() << "set RADICLE_FO4_SAVE_PATH to a .fos file or a Saves folder";
  }
  const auto saves = collect_saves(location);
  if (saves.empty()) {
    GTEST_SKIP() << "RADICLE_FO4_SAVE_PATH holds no .fos save";
  }
  for (const auto& path : saves) {
    SCOPED_TRACE(path.filename().string());
    std::ifstream stream(path, std::ios::binary);
    ASSERT_TRUE(stream.is_open());
    std::vector<char> raw((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(raw.size());
    std::transform(raw.begin(), raw.end(), bytes.begin(), [](char ch) { return static_cast<std::byte>(ch); });

    fo4::save::SaveHeader header;
    std::string error;
    ASSERT_TRUE(fo4::save::parse_save_header(bytes, header, error)) << error;
    EXPECT_EQ(header.version, fo4::save::k_save_version);
    EXPECT_GE(header.level, 1U);
    EXPECT_LE(header.level, k_plausible_max_level);
    EXPECT_GT(header.filetime, 0U);
    EXPECT_EQ(header.screenshot.size(),
              std::size_t{header.screenshot_width} * header.screenshot_height * 4U);
    EXPECT_GE(header.screenshot.data(), bytes.data());
    EXPECT_LE(header.screenshot.data() + header.screenshot.size(), bytes.data() + bytes.size());
    EXPECT_FALSE(header.plugins.empty());
  }
  RecordProperty("saves_checked", std::to_string(saves.size()));
}
