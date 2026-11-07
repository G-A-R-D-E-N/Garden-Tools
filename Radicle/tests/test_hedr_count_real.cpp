#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "hedr_count.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

// Detects: explicit repair must write the recursive count and leave all other bytes unchanged on official masters.
TEST(HedrCountReal, ExplicitRepairWritesTheRebuiltCountAndCleanSavesStayByteExact) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  }
  record_game_build(detect_game_build(dir));

  const std::map<std::string, std::int64_t> expectedExcess{
      {"Fallout4.esm", 80196},      {"DLCCoast.esm", 0},          {"DLCNukaWorld.esm", 0},
      {"DLCRobot.esm", 0},          {"DLCworkshop01.esm", 0},     {"DLCworkshop02.esm", 0},
      {"DLCworkshop03.esm", 0}};

  int files = 0;
  for (const auto& [name, excess] : expectedExcess) {
    const auto path = std::filesystem::path{dir} / name;
    if (!std::filesystem::exists(path)) continue;
    ++files;
    auto plugin = fo4::read_plugin_file(path);
    const auto original = fo4::plugin_original_bytes(plugin);
    const std::vector<std::byte> originalBytes(original.begin(), original.end());

    EXPECT_EQ(static_cast<std::int64_t>(fo4::test::stored_hedr_count(originalBytes)) -
                  static_cast<std::int64_t>(fo4::test::rebuilt_hedr_count(plugin)),
              excess)
        << name;
    EXPECT_EQ(fo4::write_plugin(plugin), originalBytes) << name << ": clean write changed";

    fo4::repair_hedr_record_count(plugin);
    const auto written = fo4::write_plugin(plugin);
    EXPECT_EQ(fo4::test::stored_hedr_count(written), fo4::test::rebuilt_hedr_count(plugin)) << name;
    EXPECT_TRUE(fo4::test::hedr_count_only_difference(written, originalBytes, plugin)) << name;
  }
  if (files == 0) GTEST_SKIP() << "no official masters found under " << dir;
}
