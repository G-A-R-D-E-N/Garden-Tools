#include "fo4/c_api_env.h"
#include "fo4/environment/base_records.hpp"
#include "fo4/plugin/writer.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <string>

namespace {

class WaterRealData : public testing::TestWithParam<const char*> {
protected:
  void SetUp() override {
    const auto* dir = std::getenv("FO4_DATA_DIR");
    if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data";
    path_ = std::filesystem::path{dir} / GetParam();
    if (!std::filesystem::exists(path_)) GTEST_SKIP() << "missing official master: " << path_;
  }

  std::filesystem::path path_;
};

TEST_P(WaterRealData, DecodesEveryWaterAndPreservesMasterBytes) {
  const std::filesystem::path paths[] = {path_};
  const auto env = fo4::env::Environment::load(paths);
  std::size_t seen = 0;
  std::map<std::size_t, int> sizes;
  for (const auto* ref : env.plugin_records(0)) {
    if (ref->record->header.signature != fo4::Signature::from_string("WATR")) continue;
    SCOPED_TRACE(ref->record->form_key.to_string());
    const auto water = fo4::env::decode_water(env, *ref);
    EXPECT_TRUE(water.visual.has_value());
    for (const auto& sub : ref->record->subrecords) {
      if (sub.signature == fo4::Signature::from_string("DNAM")) ++sizes[sub.data.size()];
    }
    ++seen;
  }
  RecordProperty("WATR", static_cast<int>(seen));
  for (const auto& [size, count] : sizes) RecordProperty("DNAM_" + std::to_string(size), count);
  const auto& plugin = env.plugin(0);
  const auto bytes = fo4::write_plugin(plugin);
  const auto original = fo4::plugin_original_bytes(plugin);
  ASSERT_EQ(bytes.size(), original.size());
  EXPECT_EQ(std::memcmp(bytes.data(), original.data(), bytes.size()), 0);
}

TEST_P(WaterRealData, CApiExposesEveryWater) {
  const auto utf8 = path_.u8string();
  const std::string path{utf8.begin(), utf8.end()};
  const char* paths[] = {path.c_str()};
  radicle_env* loaded = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &loaded), RADICLE_STATUS_OK) << radicle_last_error();
  const std::unique_ptr<radicle_env, decltype(&radicle_env_destroy)> env{loaded, radicle_env_destroy};
  const radicle_record** records = nullptr;
  std::size_t count = 0;
  ASSERT_EQ(radicle_env_winners_of(env.get(), "WATR", &records, &count), RADICLE_STATUS_OK);
  const std::unique_ptr<const radicle_record*, decltype(&radicle_free_record_array)> array{
      records, radicle_free_record_array};
  for (std::size_t i = 0; i < count; ++i) {
    radicle_record_info record{};
    ASSERT_EQ(radicle_record_get_info(env.get(), records[i], &record), RADICLE_STATUS_OK);
    SCOPED_TRACE(std::to_string(record.local_id));
    radicle_water_info water{};
    ASSERT_EQ(radicle_water_get_info(env.get(), records[i], &water), RADICLE_STATUS_OK) << radicle_last_error();
    EXPECT_EQ(water.has_visual, 1);
  }
  RecordProperty("WATR", static_cast<int>(count));
}

INSTANTIATE_TEST_SUITE_P(
    OfficialMasters, WaterRealData,
    testing::Values("Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
                    "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));

}
