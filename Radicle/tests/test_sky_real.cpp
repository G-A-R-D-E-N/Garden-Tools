#include "fo4/environment/base_records.hpp"
#include "fo4/forms/weather.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>

namespace {

class SkyRealData : public testing::TestWithParam<const char*> {};

struct SkyCounts {
  std::size_t weathers;
  std::size_t climates;
};

const std::map<std::string, SkyCounts> kOfficialSkyCounts = {
    {"Fallout4.esm", {71, 7}},
    {"DLCRobot.esm", {0, 0}},
    {"DLCworkshop01.esm", {0, 0}},
    {"DLCCoast.esm", {16, 3}},
    {"DLCworkshop02.esm", {3, 0}},
    {"DLCworkshop03.esm", {0, 0}},
    {"DLCNukaWorld.esm", {17, 1}},
};

TEST_P(SkyRealData, DecodesSkyAndEditsOfficialClimateSamplesWithoutLosingData) {
  const auto* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data";
  const auto path = std::filesystem::path{dir} / GetParam();
  if (!std::filesystem::exists(path)) GTEST_SKIP() << "missing official master: " << path;
  const auto build = detect_game_build(dir);
  record_game_build(build);
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
      ASSERT_TRUE(climate.timing.has_value());
      auto sample = fo4::make_empty_plugin(env.plugin(0).key.filename());
      sample.header = env.plugin(0).header;
      sample.tes4 = env.plugin(0).tes4;
      sample.original_data = env.plugin(0).original_data;
      fo4::Group group;
      group.header.label = {std::byte{'C'}, std::byte{'L'}, std::byte{'M'}, std::byte{'T'}};
      group.children.emplace_back(*ref->record);
      sample.groups.push_back(std::move(group));
      sample = fo4::read_plugin(fo4::write_plugin(sample), path);
      const auto before = fo4::write_plugin(sample);
      ASSERT_TRUE(fo4::env::set_climate_timing(sample, ref->record->form_key, *climate.timing));
      EXPECT_EQ(fo4::write_plugin(sample), before);
      auto requested = *climate.timing;
      requested.sunrise_begin ^= 1;
      requested.sunrise_end ^= 1;
      requested.sunset_begin ^= 1;
      requested.sunset_end ^= 1;
      requested.volatility ^= 1;
      requested.moons_phase_length ^= 1;
      ASSERT_TRUE(fo4::env::set_climate_timing(sample, ref->record->form_key, requested));
      const auto reread = fo4::read_plugin(fo4::write_plugin(sample), path);
      const auto* record = fo4::find_record(reread, ref->record->form_key);
      ASSERT_NE(record, nullptr);
      const auto actual = fo4::env::decode_climate(env, fo4::env::RecordRef{0, record});
      ASSERT_TRUE(actual.timing);
      EXPECT_EQ(actual.timing->sunrise_begin, requested.sunrise_begin);
      EXPECT_EQ(actual.timing->sunrise_end, requested.sunrise_end);
      EXPECT_EQ(actual.timing->sunset_begin, requested.sunset_begin);
      EXPECT_EQ(actual.timing->sunset_end, requested.sunset_end);
      EXPECT_EQ(actual.timing->volatility, requested.volatility);
      EXPECT_EQ(actual.timing->moons_phase_length, requested.moons_phase_length);
      ASSERT_EQ(record->subrecords.size(), ref->record->subrecords.size());
      for (std::size_t i = 0; i < record->subrecords.size(); ++i) {
        const auto& original = ref->record->subrecords[i];
        const auto& stored = record->subrecords[i];
        EXPECT_EQ(stored.signature, original.signature);
        EXPECT_EQ(stored.extended_size, original.extended_size);
        if (original.signature != fo4::Signature::from_string("TNAM")) {
          EXPECT_EQ(stored.data, original.data);
        }
      }
      ++climates;
    } else if (signature == fo4::Signature::from_string("WTHR")) {
      const auto weather = fo4::env::decode_weather(env, *ref);
      EXPECT_TRUE(weather.parameters.has_value());
      const auto model = fo4::forms::decode_weather(env.plugin(0), ref->record->form_key);
      EXPECT_EQ(model.cloud_color_layer_count, ref->record->header.form_version < 35 ? 4u : 32u);
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
  const auto& expected = kOfficialSkyCounts.at(GetParam());
  if (exact_counts_known(build)) {
    EXPECT_EQ(weathers, expected.weathers);
    EXPECT_EQ(climates, expected.climates);
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
