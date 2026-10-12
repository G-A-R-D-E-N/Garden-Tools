#include "fo4/forms/race.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

namespace {

class RaceReal : public testing::TestWithParam<const char*> {};

TEST_P(RaceReal, EveryRaceDecodesWithoutFailure) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (!directory) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / GetParam();
  ASSERT_TRUE(std::filesystem::is_regular_file(path));
  const auto plugin = fo4::read_plugin_file(path);
  std::vector<fo4::FormKey> races;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature == fo4::Signature::from_string("RACE")) races.push_back(record.form_key);
  });
  for (const auto& key : races) {
    try {
      static_cast<void>(fo4::forms::decode_race(plugin, key));
    } catch (const std::exception& error) {
      ADD_FAILURE() << GetParam() << ' ' << key.to_string() << ": " << error.what();
    }
  }
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, RaceReal, testing::Values(
    "Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
    "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));

}
