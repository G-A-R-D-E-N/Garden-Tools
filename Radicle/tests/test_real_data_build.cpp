#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::vector<std::byte> fixed_file_info(std::uint32_t file_version_ms, std::uint32_t file_version_ls) {
  std::vector<std::byte> bytes{std::byte{'M'}, std::byte{'Z'}, std::byte{0}};
  for (const auto word : {0xFEEF04BDu, 0x00010000u, file_version_ms, file_version_ls}) {
    for (int shift = 0; shift < 32; shift += 8) {
      bytes.push_back(std::byte{static_cast<std::uint8_t>((word >> shift) & 0xFFu)});
    }
  }
  return bytes;
}

std::vector<std::byte> version_1_11_240_0() { return fixed_file_info(0x0001000Bu, 0x00F00000u); }
std::vector<std::byte> version_1_10_163_0() { return fixed_file_info(0x0001000Au, 0x00A30000u); }
std::vector<std::byte> version_1_10_984_0() { return fixed_file_info(0x0001000Au, 0x03D80000u); }

void set_env(const char* name, const char* value) {
#ifdef _WIN32
  _putenv_s(name, value);
#else
  setenv(name, value, 1);
#endif
}

}  // namespace

TEST(RealDataBuild, ParsesFixedFileInfoVersion) {
  EXPECT_EQ(parse_file_version(version_1_11_240_0()), "1.11.240.0");
  EXPECT_EQ(parse_file_version(version_1_10_984_0()), "1.10.984.0");
}

TEST(RealDataBuild, AeBuildIsTheOnlyExactCountBuild) {
  const auto build = game_build_from_version(parse_file_version(version_1_11_240_0()));
  EXPECT_EQ(build.release, GameRelease::AE);
  EXPECT_TRUE(exact_counts_known(build));
}

TEST(RealDataBuild, OgBuildRecordsInsteadOfAsserting) {
  const auto build = game_build_from_version(parse_file_version(version_1_10_163_0()));
  EXPECT_EQ(build.release, GameRelease::OG);
  EXPECT_FALSE(exact_counts_known(build));
}

TEST(RealDataBuild, NgBuildsMapToNg) {
  const auto build = game_build_from_version(parse_file_version(version_1_10_984_0()));
  EXPECT_EQ(build.release, GameRelease::NG);
  EXPECT_FALSE(exact_counts_known(build));
  EXPECT_EQ(game_build_from_version("1.10.980.0").release, GameRelease::NG);
}

TEST(RealDataBuild, OtherBuildsAreUnknown) {
  const std::vector<std::byte> garbage{std::byte{0xBD}, std::byte{0x04}, std::byte{0xEF}, std::byte{0xFE}};
  EXPECT_EQ(parse_file_version(garbage), "");
  EXPECT_EQ(game_build_from_version(parse_file_version(garbage)).release, GameRelease::Unknown);
  EXPECT_EQ(game_build_from_version("1.10.155.0").release, GameRelease::Unknown);
  EXPECT_EQ(game_build_from_version("").release, GameRelease::Unknown);
}

TEST(RealDataBuild, MissingExeIsUnknown) {
  set_env("FO4_GAME_BUILD", "");
  const auto build = detect_game_build(std::filesystem::path{"no-such-folder-for-real-data-build"} / "Data");
  EXPECT_EQ(build.release, GameRelease::Unknown);
  EXPECT_FALSE(exact_counts_known(build));
}

TEST(RealDataBuild, DetectReadsFalloutExeNextToDataFolder) {
  set_env("FO4_GAME_BUILD", "");
  const auto root = std::filesystem::temp_directory_path() / "radicle-real-data-build-test";
  std::filesystem::create_directories(root / "Data");
  const auto bytes = version_1_11_240_0();
  {
    std::ofstream out{root / "Fallout4.exe", std::ios::binary};
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  const auto build = detect_game_build(root / "Data");
  std::filesystem::remove_all(root);
  EXPECT_EQ(build.file_version, "1.11.240.0");
  EXPECT_TRUE(exact_counts_known(build));
}

TEST(RealDataBuildReal, InstalledDataBuildIsKnown) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data";
  const auto build = detect_game_build(dir);
  EXPECT_NE(build.release, GameRelease::Unknown)
      << "Fallout4.exe was not found next to the Data folder, or its version is not a known build";
  EXPECT_TRUE(exact_counts_known(build))
      << "exact counts are pinned to 1.11.240.0; re-check the pinned counts for the installed build, file version "
      << build.file_version;
}

TEST(RealDataBuild, EnvOverrideTakesPrecedence) {
  set_env("FO4_GAME_BUILD", "1.10.163.0");
  const auto og = detect_game_build(std::filesystem::path{"no-such-folder-for-real-data-build"} / "Data");
  EXPECT_EQ(og.file_version, "1.10.163.0");
  EXPECT_EQ(og.release, GameRelease::OG);
  EXPECT_FALSE(exact_counts_known(og));
  set_env("FO4_GAME_BUILD", "1.11.240.0");
  const auto ae = detect_game_build(std::filesystem::path{"no-such-folder-for-real-data-build"} / "Data");
  EXPECT_EQ(ae.release, GameRelease::AE);
  EXPECT_TRUE(exact_counts_known(ae));
  set_env("FO4_GAME_BUILD", "");
}
