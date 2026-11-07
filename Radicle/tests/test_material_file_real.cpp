#include "fo4/archive/ba2.hpp"
#include "fo4/material/material_file.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>

namespace {

std::string lower_extension(const std::string& path) {
  std::string extension = std::filesystem::path(path).extension().string();
  for (auto& ch : extension) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return extension;
}

}  // namespace

TEST(MaterialFileRealData, RoundTripsEveryVanillaMaterialByteIdentically) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder";
  }
  record_game_build(detect_game_build(dir));
  const auto archive_path = std::filesystem::path(dir) / "Fallout4 - Materials.ba2";
  if (!std::filesystem::exists(archive_path)) {
    GTEST_SKIP() << "Fallout4 - Materials.ba2 is not in FO4_DATA_DIR";
  }

  const auto archive = fo4::archive::read_ba2_general_file(archive_path);
  std::size_t bgsm_count = 0;
  std::size_t bgem_count = 0;
  std::map<std::string, std::size_t> histogram;
  for (const auto& entry : archive.entries) {
    const auto extension = lower_extension(entry.path);
    if (extension != ".bgsm" && extension != ".bgem") {
      continue;
    }
    const auto bytes = fo4::archive::extract_ba2_general_entry(entry);
    const auto file = fo4::material::read_material_file(bytes);
    EXPECT_TRUE(file.trailing.empty()) << entry.path;
    EXPECT_TRUE(fo4::material::write_material_file(file) == bytes) << entry.path;
    if (extension == ".bgsm") {
      ++bgsm_count;
    } else {
      ++bgem_count;
    }
    ++histogram[extension.substr(1) + " v" + std::to_string(file.version())];
  }

  EXPECT_GT(bgsm_count, 0u);
  EXPECT_GT(bgem_count, 0u);
  RecordProperty("bgsm_files", std::to_string(bgsm_count));
  RecordProperty("bgem_files", std::to_string(bgem_count));
  std::string versions;
  for (const auto& [key, count] : histogram) {
    if (!versions.empty()) {
      versions += ' ';
    }
    versions += key + "=" + std::to_string(count);
  }
  RecordProperty("version_histogram", versions);
}
