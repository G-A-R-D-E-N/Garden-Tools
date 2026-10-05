#include "fo4/c_api_env.h"
#include "fo4/environment/base_records.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

namespace {

std::vector<std::byte> sequence(std::size_t size) {
  std::vector<std::byte> bytes;
  for (std::size_t i = 0; i < size; ++i) bytes.push_back(std::byte{static_cast<std::uint8_t>(i)});
  return bytes;
}

class SkyParametersTest : public testing::Test {
protected:
  void SetUp() override {
    root_ = std::filesystem::temp_directory_path() /
            ("radicle_sky_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root_);
    path_ = root_ / "Sky.esm";
  }

  void TearDown() override {
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  void write(std::optional<std::vector<std::byte>> timing, std::optional<std::vector<std::byte>> data,
             bool duplicate = false) {
    fo4::test::ByteWriter bytes;
    const auto tes4 = fo4::test::begin_record(bytes, "TES4", 1, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(2);
    hedr.u32(0x802);
    fo4::test::write_subrecord(bytes, "HEDR", hedr.bytes);
    fo4::test::end_record(bytes, tes4);
    const auto record = [&](const char* signature, const char* subrecord, std::uint32_t id,
                            const std::optional<std::vector<std::byte>>& value) {
      const auto group = fo4::test::begin_group(bytes, signature, 0);
      const auto rec = fo4::test::begin_record(bytes, signature, 0, id, 131);
      if (value) {
        fo4::test::write_subrecord(bytes, subrecord, *value);
        if (duplicate) fo4::test::write_subrecord(bytes, subrecord, *value);
      }
      fo4::test::end_record(bytes, rec);
      fo4::test::end_group(bytes, group);
    };
    record("CLMT", "TNAM", 0x800, timing);
    record("WTHR", "DATA", 0x801, data);
    std::ofstream file(path_, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.bytes.data()), static_cast<std::streamsize>(bytes.bytes.size()));
  }

  fo4::env::Environment load() {
    const std::filesystem::path paths[] = {path_};
    return fo4::env::Environment::load(paths);
  }

  static fo4::FormKey key(std::uint32_t id) {
    return {fo4::PluginKey::from_filename("Sky.esm"), fo4::LocalFormId{id}};
  }

  std::filesystem::path root_;
  std::filesystem::path path_;
};

TEST_F(SkyParametersTest, DecodesEveryStoredByteAndLegacyTail) {
  for (const std::size_t size : {19u, 20u}) {
    write(sequence(6), sequence(size));
    const auto env = load();
    const auto timing = fo4::env::decode_climate(env, *env.winner(key(0x800))).timing;
    ASSERT_TRUE(timing.has_value());
    EXPECT_EQ(timing->sunrise_begin, 0);
    EXPECT_EQ(timing->sunrise_end, 1);
    EXPECT_EQ(timing->sunset_begin, 2);
    EXPECT_EQ(timing->sunset_end, 3);
    EXPECT_EQ(timing->volatility, 4);
    EXPECT_EQ(timing->moons_phase_length, 5);
    const auto p = fo4::env::decode_weather(env, *env.winner(key(0x801))).parameters;
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->wind_speed, 0);
    EXPECT_EQ(p->transition_delta, 3);
    EXPECT_EQ(p->sun_glare, 4);
    EXPECT_EQ(p->sun_damage, 5);
    EXPECT_EQ(p->precipitation_begin, 6);
    EXPECT_EQ(p->precipitation_end, 7);
    EXPECT_EQ(p->lightning_begin, 8);
    EXPECT_EQ(p->lightning_end, 9);
    EXPECT_EQ(p->lightning_frequency, 10);
    EXPECT_EQ(p->flags, 11);
    EXPECT_EQ(p->lightning_color.red, 12);
    EXPECT_EQ(p->lightning_color.green, 13);
    EXPECT_EQ(p->lightning_color.blue, 14);
    EXPECT_EQ(p->visual_effect_begin, 15);
    EXPECT_EQ(p->visual_effect_end, 16);
    EXPECT_EQ(p->wind_direction, 17);
    EXPECT_EQ(p->wind_direction_range, 18);
    EXPECT_EQ(p->wind_turbulence, size == 20 ? std::optional<std::uint8_t>{19} : std::nullopt);
  }
}

TEST_F(SkyParametersTest, MissingFieldsStayAbsent) {
  write(std::nullopt, std::nullopt);
  const auto env = load();
  EXPECT_FALSE(fo4::env::decode_climate(env, *env.winner(key(0x800))).timing.has_value());
  EXPECT_FALSE(fo4::env::decode_weather(env, *env.winner(key(0x801))).parameters.has_value());
}

TEST_F(SkyParametersTest, RejectsUnknownLengthsAndDuplicates) {
  for (const std::size_t size : {5u, 7u}) {
    write(sequence(size), std::nullopt);
    const auto env = load();
    EXPECT_THROW(fo4::env::decode_climate(env, *env.winner(key(0x800))), fo4::env::WorldDecodeError);
  }
  for (const std::size_t size : {18u, 21u}) {
    write(std::nullopt, sequence(size));
    const auto env = load();
    EXPECT_THROW(fo4::env::decode_weather(env, *env.winner(key(0x801))), fo4::env::WorldDecodeError);
  }
  write(sequence(6), sequence(20), true);
  const auto env = load();
  EXPECT_THROW(fo4::env::decode_climate(env, *env.winner(key(0x800))), fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::decode_weather(env, *env.winner(key(0x801))), fo4::env::WorldDecodeError);
}

TEST_F(SkyParametersTest, CAbiExposesTimingAndParameters) {
  write(sequence(6), sequence(20));
  const auto path = path_.string();
  const char* paths[] = {path.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK);
  const radicle_record* record = nullptr;
  ASSERT_EQ(radicle_env_winner(env, {"Sky.esm", 0x800}, &record), RADICLE_STATUS_OK);
  radicle_climate* climate = nullptr;
  ASSERT_EQ(radicle_climate_decode(env, record, &climate), RADICLE_STATUS_OK);
  EXPECT_EQ(climate->has_timing, 1);
  EXPECT_EQ(climate->timing.sunset_end, 3);
  EXPECT_FLOAT_EQ(climate->timing.sunset_end_hours, 0.5f);
  EXPECT_EQ(climate->timing.days_per_phase, 5);
  EXPECT_EQ(climate->timing.uses_secunda, 0);
  EXPECT_EQ(climate->timing.uses_masser, 0);
  radicle_climate_free(climate);
  ASSERT_EQ(radicle_env_winner(env, {"Sky.esm", 0x801}, &record), RADICLE_STATUS_OK);
  radicle_weather_info* weather = nullptr;
  ASSERT_EQ(radicle_weather_decode(env, record, &weather), RADICLE_STATUS_OK);
  EXPECT_EQ(weather->has_parameters, 1);
  EXPECT_EQ(weather->parameters.transition_delta, 3);
  EXPECT_EQ(weather->parameters.lightning_color.blue, 14);
  EXPECT_EQ(weather->parameters.wind_turbulence, 19);
  radicle_weather_free(weather);
  radicle_env_destroy(env);
}

TEST_F(SkyParametersTest, ConvertsUnsignedTimingAndMoonBoundaries) {
  const std::vector<std::byte> bytes{std::byte{128}, std::byte{255}, std::byte{36}, std::byte{114}, std::byte{0}, std::byte{255}};
  write(bytes, std::nullopt);
  const auto env = load();
  const auto timing = fo4::env::decode_climate(env, *env.winner(key(0x800))).timing;
  ASSERT_TRUE(timing);
  EXPECT_FLOAT_EQ(timing->sunrise_begin_hours(), 128.0f * (1.0f / 6.0f));
  EXPECT_EQ(timing->sunrise_end_hours(), 42.5f);
  EXPECT_EQ(timing->sunset_begin_hours(), 6.0f);
  EXPECT_EQ(timing->sunset_end_hours(), 19.0f);
  EXPECT_EQ(timing->days_per_phase(), 63);
  EXPECT_TRUE(timing->uses_masser());
  EXPECT_TRUE(timing->uses_secunda());
}

}
