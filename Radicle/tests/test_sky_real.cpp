#include "fo4/environment/base_records.hpp"
#include "fo4/forms/weather.hpp"
#include "fo4/plugin/writer.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>

namespace {

class SkyRealData : public testing::TestWithParam<const char*> {};

TEST_P(SkyRealData, DecodesEveryClimateAndWeatherAndPreservesBytes) {
  const auto* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data";
  const auto path = std::filesystem::path{dir} / GetParam();
  if (!std::filesystem::exists(path)) GTEST_SKIP() << "missing official master: " << path;
  const std::filesystem::path paths[] = {path};
  const auto env = fo4::env::Environment::load(paths);
  std::size_t climates = 0;
  std::size_t weathers = 0;
  std::map<std::size_t, int> sizes;
  for (const auto* ref : env.plugin_records(0)) {
    const auto signature = ref->record->header.signature;
    SCOPED_TRACE(ref->record->form_key.to_string());
    if (signature == fo4::Signature::from_string("CLMT")) {
      const auto climate = fo4::env::decode_climate(env, *ref);
      EXPECT_TRUE(climate.timing.has_value());
      ++climates;
    } else if (signature == fo4::Signature::from_string("WTHR")) {
      const auto weather = fo4::env::decode_weather(env, *ref);
      EXPECT_TRUE(weather.parameters.has_value());
      const auto model = fo4::forms::decode_weather(env.plugin(0), ref->record->form_key);
      for (const auto& sub : ref->record->subrecords) {
        if (sub.signature == fo4::Signature::from_string("DATA")) ++sizes[sub.data.size()];
        if (sub.signature == fo4::Signature::from_string("RNAM") ||
            sub.signature == fo4::Signature::from_string("QNAM")) {
          ASSERT_EQ(sub.data.size(), fo4::forms::k_weather_cloud_layer_slots);
          const auto& speeds = sub.signature == fo4::Signature::from_string("RNAM")
              ? model.cloud_speed_y : model.cloud_speed_x;
          for (std::size_t i = 0; i < speeds.size(); ++i) {
            EXPECT_EQ(speeds[i], static_cast<std::uint8_t>(sub.data[i])) << i;
          }
        }
      }
      ++weathers;
    }
  }
  RecordProperty("CLMT", static_cast<int>(climates));
  RecordProperty("WTHR", static_cast<int>(weathers));
  for (const auto& [size, count] : sizes) RecordProperty("DATA_" + std::to_string(size), count);
  const auto& plugin = env.plugin(0);
  const auto bytes = fo4::write_plugin(plugin);
  const auto original = fo4::plugin_original_bytes(plugin);
  ASSERT_EQ(bytes.size(), original.size());
  EXPECT_EQ(std::memcmp(bytes.data(), original.data(), bytes.size()), 0);
}

INSTANTIATE_TEST_SUITE_P(
    OfficialMasters, SkyRealData,
    testing::Values("Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
                    "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));

}
