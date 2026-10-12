#include "climate_weather_fixture.hpp"

#include "fo4/c_api.h"
#include "fo4/c_api_authoring.h"
#include "fo4/c_api_env.h"
#include "fo4/c_api_records.h"
#include "fo4/forms/weather.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

using namespace fo4::test::sky;

class ClimateWeatherCApi : public testing::Test {
protected:
  void SetUp() override {
    root = std::filesystem::temp_directory_path() /
           ("radicle_sky_capi_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root / "out");
    path = root / k_plugin;
    const auto bytes = build(fixture(false), false);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  void TearDown() override { std::error_code error; std::filesystem::remove_all(root, error); }

  std::filesystem::path root;
  std::filesystem::path path;
};

radicle_form_key rkey(std::uint32_t id) { return radicle_form_key{k_plugin, id}; }

}

TEST_F(ClimateWeatherCApi, FieldLinkAuthoringAndCreateStatusesMapAndSurviveReload) {
  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(path.string().c_str(), &plugin), RADICLE_STATUS_OK);
  const radicle_form_key weather = rkey(k_wthr), old = rkey(k_wthr_old), climate = rkey(k_clmt), bare = rkey(k_clmt_bare);
  const radicle_form_key glob = rkey(k_glob), stat = rkey(k_stat), misc = rkey(k_misc), imgs = rkey(k_imgs), lens = rkey(k_lens);
  const radicle_form_key unknown_master{"Other.esm", 1};

  EXPECT_EQ(radicle_plugin_set_record_scalar(plugin, climate, "CLMT\\SunriseBegin", "36"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, climate, "CLMT\\WeatherChance", 1, "-7"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, climate, "CLMT\\WeatherChance", 2, "1"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, climate, "CLMT\\Volatility", 0, "256"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, weather, "CLMT\\Volatility", 0, "1"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, climate, "CLMT\\NoSuchField", 0, "1"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, rkey(0x999), "CLMT\\Volatility", 0, "1"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, weather, "WTHR\\CloudSpeedY", 3, "200"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, old, "WTHR\\CloudAlphaDay", 0, "0.5"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, weather, "WTHR\\WindSpeed", 0, "40"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, old, "WTHR\\WindTurbulence", 0, "4"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, old, "WTHR\\AmbientSpecularGreen", 1, "8"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_string(plugin, climate, "CLMT\\SunGlareTexture", 0, "sky\\gl\xC3\xA4re.dds"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_string(plugin, weather, "WTHR\\CloudTexture0", 0, "sky\\\xE4"), RADICLE_STATUS_INVALID_ARGUMENT);

  size_t count = 0;
  EXPECT_EQ(radicle_plugin_record_field_count(plugin, weather, "WTHR\\ImageSpace", &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 8u);
  EXPECT_EQ(radicle_plugin_record_field_count(plugin, old, "WTHR\\ImageSpace", &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 4u);
  EXPECT_EQ(radicle_plugin_record_field_count(plugin, climate, "CLMT\\Weather", &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 2u);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, climate, "CLMT\\WeatherGlobal", 0, &glob), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, climate, "CLMT\\Weather", 1, nullptr), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, climate, "CLMT\\Weather", 0, &misc), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, climate, "CLMT\\Weather", 0, &unknown_master), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, weather, "WTHR\\SkyStatic", 1, &stat), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, weather, "WTHR\\SkyStatic", 2, &stat), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, old, "WTHR\\ImageSpace", 4, &imgs), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, weather, "WTHR\\ImageSpace", 5, &imgs), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, weather, "WTHR\\SunGlareLensFlare", 0, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, old, "WTHR\\SunGlareLensFlare", 0, &lens), RADICLE_STATUS_NOT_FOUND);

  EXPECT_EQ(radicle_plugin_add_subrecord(plugin, bare, "WLST"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin, bare, "WLST"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin, bare, "MODL"), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin, rkey(0x999), "WLST"), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_record_formlink(plugin, bare, "CLMT\\Weather", 0, &weather), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin, bare, "TNAM", 0), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin, weather, "DALC", 0), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin, weather, "VNAM", 0), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin, old, "SNAM"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_set_record_scalar_at(plugin, old, "WTHR\\SoundType", 0, "2"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin, weather, "TNAM"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin, weather, "TNAM", 0), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin, weather, "TNAM", 9), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin, weather, "GNAM"), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_remove_subrecord(plugin, weather, "GNAM", 0), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_add_subrecord(plugin, weather, "GNAM"), RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_form_key created{}, refused_key{};
  EXPECT_EQ(radicle_plugin_create_record(plugin, "WTHR", "CWeather", &refused_key), RADICLE_STATUS_INVALID_ARGUMENT);
  ASSERT_EQ(radicle_plugin_create_record(plugin, "CLMT", "CClimate", &created), RADICLE_STATUS_OK);
  const auto created_id = created.local_id;
  EXPECT_EQ(created_id, 0x900u);
  ASSERT_EQ(radicle_plugin_add_subrecord(plugin, created, "WLST"), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_record_formlink(plugin, created, "CLMT\\Weather", 0, &old), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_record_scalar_at(plugin, created, "CLMT\\WeatherChance", 0, "100"), RADICLE_STATUS_OK);
  const radicle_climate_timing_edit timing{30, 54, 102, 126, 5, 68};
  ASSERT_EQ(radicle_plugin_set_climate_timing(plugin, created, &timing), RADICLE_STATUS_OK);

  const auto saved = (root / "out" / k_plugin).string();
  ASSERT_EQ(radicle_plugin_write_file(plugin, saved.c_str()), RADICLE_STATUS_OK);
  radicle_plugin_destroy(plugin);

  const char* paths[] = {saved.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK) << radicle_last_error();
  const radicle_record* handle = nullptr;
  radicle_climate* decoded = nullptr;
  ASSERT_EQ(radicle_env_winner(env, climate, &handle), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_climate_decode(env, handle, &decoded), RADICLE_STATUS_OK);
  EXPECT_EQ(decoded->timing.sunrise_begin, 36);
  ASSERT_EQ(decoded->weather_count, 2u);
  EXPECT_EQ(decoded->weathers[0].global.local_id, k_glob);
  EXPECT_EQ(decoded->weathers[1].chance, -7);
  EXPECT_EQ(decoded->weathers[1].weather.plugin, nullptr);
  EXPECT_STREQ(decoded->sun_glare_texture, "sky\\gl\xC3\xA4re.dds");
  radicle_climate_free(decoded);
  ASSERT_EQ(radicle_env_winner(env, rkey(created_id), &handle), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_climate_decode(env, handle, &decoded), RADICLE_STATUS_OK);
  ASSERT_EQ(decoded->weather_count, 1u);
  EXPECT_EQ(decoded->weathers[0].weather.local_id, k_wthr_old);
  EXPECT_EQ(decoded->weathers[0].chance, 100);
  EXPECT_EQ(decoded->timing.volatility, 5);
  radicle_climate_free(decoded);
  radicle_weather_info* info = nullptr;
  ASSERT_EQ(radicle_env_winner(env, weather, &handle), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_weather_decode(env, handle, &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info->parameters.wind_speed, 40);
  radicle_weather_free(info);
  radicle_env_destroy(env);

  const auto reread = read([&] {
    std::ifstream file(saved, std::ios::binary);
    std::vector<char> chars{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    Bytes out(chars.size());
    std::memcpy(out.data(), chars.data(), chars.size());
    return out;
  }());
  const auto model = fo4::forms::decode_weather(reread, key(k_wthr));
  EXPECT_EQ(model.cloud_speed_y[3], 200);
  EXPECT_EQ(model.imsp[5], key(k_imgs));
  ASSERT_EQ(model.tnam.size(), 2u);
  EXPECT_EQ(model.tnam[0], key(k_stat));
  EXPECT_EQ(model.tnam[1], std::nullopt);
  EXPECT_EQ(model.gnam, std::nullopt);
  EXPECT_EQ(fo4::forms::decode_weather(reread, key(k_wthr_old)).snam.at(0).type, 2u);
  EXPECT_EQ(payloads(reread, key(k_wthr_old), "DALC").at(1)[25], std::byte{8});
  for (const auto id : {k_clmt, k_clmt_bare, k_wthr, k_wthr_old, created_id}) { EXPECT_TRUE(authoring_clean(reread, key(id))) << id; }
}

TEST(ClimateDerivedHelpersCApi, DerivedTimingHelpersFollowTheirFixtures) {
  int32_t has_phase = 0;
  uint8_t phase = 0xFF;
  ASSERT_EQ(radicle_climate_moon_phase(0xC3, 10, &has_phase, &phase), RADICLE_STATUS_OK);
  EXPECT_EQ(has_phase, 1);
  EXPECT_EQ(phase, 3);
  ASSERT_EQ(radicle_climate_moon_phase(0x00, 10, &has_phase, &phase), RADICLE_STATUS_OK);
  EXPECT_EQ(has_phase, 0);
  ASSERT_EQ(radicle_climate_moon_phase(0x3F, 63, &has_phase, &phase), RADICLE_STATUS_OK);
  EXPECT_EQ(phase, 1);
  EXPECT_EQ(radicle_climate_moon_phase(0x3F, 0, nullptr, &phase), RADICLE_STATUS_INVALID_ARGUMENT);

  float hours = 0.0f;
  ASSERT_EQ(radicle_climate_volatility_hold_hours(255, 1.0f, 1.0f, 23.0f, &hours), RADICLE_STATUS_OK);
  EXPECT_FLOAT_EQ(hours, 1.0f);
  ASSERT_EQ(radicle_climate_volatility_hold_hours(0, 1.0f, 1.0f, 23.0f, &hours), RADICLE_STATUS_OK);
  EXPECT_FLOAT_EQ(hours, 23.0f);
  ASSERT_EQ(radicle_climate_volatility_hold_hours(128, 1.0f, 1.0f, 23.0f, &hours), RADICLE_STATUS_OK);
  EXPECT_NEAR(hours, 11.9569f, 1e-4f);
  EXPECT_EQ(radicle_climate_volatility_hold_hours(128, 1.0f, 1.0f, 23.0f, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);

  ASSERT_EQ(radicle_weather_transition_hours(0, 0.01f, 1.0f, &hours), RADICLE_STATUS_OK);
  EXPECT_NEAR(hours, 0.01f, 1e-6f);
  ASSERT_EQ(radicle_weather_transition_hours(128, 0.01f, 3.0f, &hours), RADICLE_STATUS_OK);
  EXPECT_NEAR(hours, 1.5108628f, 1e-6f);
  ASSERT_EQ(radicle_weather_transition_hours(255, 0.01f, 3.0f, &hours), RADICLE_STATUS_OK);
  EXPECT_NEAR(hours, 3.0f, 1e-6f);
  EXPECT_EQ(radicle_weather_transition_hours(255, 0.01f, 3.0f, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
}
