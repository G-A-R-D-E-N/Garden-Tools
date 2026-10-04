#include "fo4/c_api_vegetation.h"
#include "fo4/environment/vegetation.hpp"
#include "fo4/plugin/writer.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>

namespace {

class VegetationReal : public testing::TestWithParam<const char*> {};

TEST_P(VegetationReal, EveryOfficialVegetationRecordDecodesAndMasterIsByteIdentical) {
  const auto* dir = std::getenv("FO4_DATA_DIR");
  if (!dir) GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data";
  const auto path = std::filesystem::path{dir} / GetParam();
  if (!std::filesystem::exists(path)) GTEST_SKIP() << "missing official master: " << path;
  const std::filesystem::path native_paths[] = {path}; const auto env = fo4::env::Environment::load(native_paths);
  const auto name = path.string(); const char* paths[] = {name.c_str()}; radicle_env* abi = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &abi), RADICLE_STATUS_OK);
  const radicle_record** records = nullptr; std::size_t count = 0;
  ASSERT_EQ(radicle_env_plugin_records(abi, 0, &records, &count), RADICLE_STATUS_OK);
  const auto refs = env.plugin_records(0); ASSERT_EQ(count, refs.size()); std::map<std::string, int> totals;
  for (std::size_t i = 0; i < count; ++i) {
    const auto type = refs[i]->record->header.signature.to_string();
    if (type != "GRAS" && type != "TREE" && type != "FLOR" && type != "LTEX") continue;
    SCOPED_TRACE(refs[i]->record->form_key.to_string()); ++totals[type];
    if (type == "GRAS") {
      EXPECT_NO_THROW(fo4::env::decode_grass(env, *refs[i])); radicle_grass_info value{};
      EXPECT_EQ(radicle_grass_get_info(abi, records[i], &value), RADICLE_STATUS_OK);
    } else if (type == "TREE") {
      EXPECT_NO_THROW(fo4::env::decode_tree(env, *refs[i])); radicle_tree_info value{};
      EXPECT_EQ(radicle_tree_get_info(abi, records[i], &value), RADICLE_STATUS_OK);
    } else if (type == "FLOR") {
      EXPECT_NO_THROW(fo4::env::decode_flora(env, *refs[i])); radicle_flora_info value{};
      EXPECT_EQ(radicle_flora_get_info(abi, records[i], &value), RADICLE_STATUS_OK);
    } else {
      EXPECT_NO_THROW(fo4::env::decode_landscape_grasses(env, *refs[i])); const radicle_grass_links* value = nullptr;
      EXPECT_EQ(radicle_landscape_grasses_decode(abi, records[i], &value), RADICLE_STATUS_OK); radicle_landscape_grasses_free(value);
    }
  }
  radicle_free_record_array(records); radicle_env_destroy(abi);
  for (const auto& [type, count_value] : totals) RecordProperty(type, count_value);
  const auto bytes = fo4::write_plugin(env.plugin(0)); const auto original = fo4::plugin_original_bytes(env.plugin(0));
  ASSERT_EQ(bytes.size(), original.size()); EXPECT_EQ(std::memcmp(bytes.data(), original.data(), bytes.size()), 0);
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, VegetationReal,
    testing::Values("Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm", "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));

}
