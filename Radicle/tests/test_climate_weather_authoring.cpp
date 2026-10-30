#include "climate_weather_fixture.hpp"

#include "fo4/environment/base_records.hpp"
#include "fo4/forms/weather.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/schema/scalar.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace fo4::test::sky;

fo4::ScalarMutationStatus scalar(fo4::Plugin& plugin, std::uint32_t id, const char* path, std::size_t index, const char* value) {
  const auto field = fo4::schema::scalar_field_descriptor(path);
  EXPECT_TRUE(field.has_value()) << path;
  return fo4::set_record_scalar(plugin, key(id), *field, index, value);
}

fo4::ScalarMutationStatus text(fo4::Plugin& plugin, std::uint32_t id, const char* path, std::size_t index, const char* value) {
  return fo4::set_record_string(plugin, key(id), path, index, value);
}

std::size_t count(const fo4::Plugin& plugin, std::uint32_t id, const char* path) {
  return fo4::record_field_count(plugin, key(id), path).value_or(9999);
}

bool scalar_refused(fo4::Plugin& plugin, std::uint32_t id, const char* path, std::size_t index, const char* value) {
  const auto before = fo4::write_plugin(plugin);
  bool threw = false;
  try {
    scalar(plugin, id, path, index, value);
  } catch (const std::exception&) {
    threw = true;
  }
  return threw && fo4::write_plugin(plugin) == before;
}

constexpr auto k_updated = fo4::ScalarMutationStatus::updated;
constexpr auto k_missing = fo4::ScalarMutationStatus::subrecord_missing;
constexpr auto k_link_updated = fo4::FormLinkFieldMutationStatus::updated;
constexpr auto k_link_missing = fo4::FormLinkFieldMutationStatus::subrecord_missing;

Bytes fixture_payload(std::uint32_t id, const char* name) {
  return payloads(read(build(fixture(), true)), key(id), name).at(0);
}

}

TEST(ClimateWeatherAuthoring, ClimateScalarsStridedChanceAndStringsWriteOnlyTheirBytes) {
  auto plugin = read(build(fixture(), true));
  const auto before = snapshot(plugin);
  EXPECT_EQ(scalar(plugin, k_clmt, "CLMT\\SunriseBegin", 0, "200"), k_updated);
  EXPECT_EQ(scalar(plugin, k_clmt, "CLMT\\MoonsPhaseLength", 0, "195"), k_updated);
  EXPECT_EQ(scalar(plugin, k_clmt_bare, "CLMT\\Volatility", 0, "7"), k_updated);
  EXPECT_EQ(payloads(plugin, key(k_clmt), "TNAM").at(0), (Bytes{std::byte{200}, std::byte{54}, std::byte{102}, std::byte{126}, std::byte{0}, std::byte{195}}));
  EXPECT_EQ(count(plugin, k_clmt, "CLMT\\WeatherChance"), 2u);
  EXPECT_EQ(count(plugin, k_clmt_bare, "CLMT\\WeatherChance"), 0u);
  EXPECT_EQ(scalar(plugin, k_clmt, "CLMT\\WeatherChance", 1, "-5"), k_updated);
  EXPECT_EQ(scalar(plugin, k_clmt, "CLMT\\WeatherChance", 2, "1"), k_missing);
  EXPECT_EQ(scalar(plugin, k_clmt_bare, "CLMT\\WeatherChance", 0, "1"), k_missing);
  EXPECT_EQ(payloads(plugin, key(k_clmt), "WLST").at(0), words({k_local | k_wthr, 60, 0, k_local | k_wthr_old, 0xFFFFFFFBu, k_local | k_glob}));
  EXPECT_EQ(text(plugin, k_clmt, "CLMT\\SunTexture", 0, "sky\\new.dds"), k_updated);
  EXPECT_EQ(text(plugin, k_clmt, "CLMT\\Model", 0, "sky\\moon.nif"), k_updated);
  EXPECT_EQ(text(plugin, k_clmt_bare, "CLMT\\SunGlareTexture", 0, "x.dds"), k_missing);
  EXPECT_TRUE(scalar_refused(plugin, k_clmt, "CLMT\\SunsetEnd", 0, "256"));
  EXPECT_TRUE(scalar_refused(plugin, k_clmt, "CLMT\\WeatherChance", 0, "2147483648"));
  EXPECT_TRUE(scalar_refused(plugin, k_wthr, "CLMT\\SunsetEnd", 0, "1"));

  const auto reloaded = reload(plugin);
  const auto after = snapshot(reloaded);
  for (const auto& [record, bytes] : before) {
    if (record != key(k_clmt) && record != key(k_clmt_bare)) { EXPECT_EQ(after.at(record), bytes) << record.to_string(); }
  }
  EXPECT_EQ(order(reloaded, k_clmt), order(read(build(fixture(), true)), k_clmt));
  EXPECT_EQ(payloads(reloaded, key(k_clmt), "FNAM").at(0), zstr("sky\\new.dds"));
  EXPECT_EQ(payloads(reloaded, key(k_clmt), "MODT").at(0), filled(12, 3));
  EXPECT_EQ(payloads(reloaded, key(k_clmt_bare), "TNAM").at(0), (Bytes{std::byte{30}, std::byte{54}, std::byte{102}, std::byte{126}, std::byte{7}, std::byte{68}}));
}

TEST(ClimateWeatherAuthoring, WeatherFixedFieldsFollowTheirPayloadSizeAndPrefixRules) {
  auto plugin = read(build(fixture(), true));
  const auto before = snapshot(plugin);
  EXPECT_EQ(scalar(plugin, k_wthr, "WTHR\\MaxCloudLayers", 0, "8"), k_updated);
  EXPECT_EQ(count(plugin, k_wthr, "WTHR\\CloudSpeedY"), 32u);
  EXPECT_EQ(scalar(plugin, k_wthr, "WTHR\\CloudSpeedY", 31, "216"), k_updated);
  EXPECT_EQ(scalar(plugin, k_wthr, "WTHR\\CloudSpeedX", 0, "62"), k_updated);
  EXPECT_EQ(scalar(plugin, k_wthr, "WTHR\\CloudSpeedX", 32, "62"), k_missing);
  EXPECT_TRUE(scalar_refused(plugin, k_wthr, "WTHR\\CloudSpeedY", 0, "-1"));
  EXPECT_EQ(payloads(plugin, key(k_wthr), "RNAM").at(0)[31], std::byte{216});

  EXPECT_EQ(count(plugin, k_wthr, "WTHR\\CloudAlphaDay"), 32u);
  EXPECT_EQ(scalar(plugin, k_wthr, "WTHR\\CloudAlphaLateSunset", 31, "0.5"), k_updated);
  EXPECT_EQ(scalar(plugin, k_wthr_old, "WTHR\\CloudAlphaDay", 0, "0.5"), k_missing);
  EXPECT_EQ(scalar(plugin, k_wthr, "WTHR\\CloudColorEarlySunsetGreen", 2, "9"), k_updated);
  EXPECT_EQ(payloads(plugin, key(k_wthr), "PNAM").at(0)[2 * 32 + 6 * 4 + 1], std::byte{9});
  EXPECT_EQ(scalar(plugin, k_wthr_old, "WTHR\\CloudColorDayRed", 0, "9"), k_missing);
  EXPECT_EQ(count(plugin, k_wthr, "WTHR\\WeatherColorDayBlue"), 19u);
  EXPECT_EQ(scalar(plugin, k_wthr, "WTHR\\WeatherColorDayBlue", 18, "77"), k_updated);
  EXPECT_EQ(payloads(plugin, key(k_wthr), "NAM0").at(0)[18 * 32 + 4 + 2], std::byte{77});
  EXPECT_EQ(scalar(plugin, k_wthr, "WTHR\\WeatherColorDayBlue", 19, "77"), k_missing);
  EXPECT_EQ(scalar(plugin, k_wthr_old, "WTHR\\WeatherColorDayBlue", 0, "77"), k_missing);
  EXPECT_EQ(scalar(plugin, k_wthr, "WTHR\\UnknownNAM4", 31, "2.5"), k_updated);
  EXPECT_EQ(scalar(plugin, k_wthr_old, "WTHR\\UnknownNAM4", 0, "2.5"), k_missing);

  EXPECT_EQ(scalar(plugin, k_wthr, "WTHR\\FogNightFarHeightRange", 0, "1500000"), k_updated);
  EXPECT_EQ(scalar(plugin, k_wthr_old, "WTHR\\FogNightFarHeightRange", 0, "1"), k_missing);
  EXPECT_EQ(scalar(plugin, k_wthr_old, "WTHR\\FogDayNearHeightMid", 0, "1"), k_missing);
  EXPECT_EQ(scalar(plugin, k_wthr_old, "WTHR\\FogNightMax", 0, "-6400"), k_updated);
  EXPECT_EQ(scalar(plugin, k_wthr, "WTHR\\WindTurbulence", 0, "4"), k_updated);
  EXPECT_EQ(scalar(plugin, k_wthr_old, "WTHR\\WindTurbulence", 0, "4"), k_missing);
  EXPECT_EQ(scalar(plugin, k_wthr_old, "WTHR\\LightningFrequency", 0, "255"), k_updated);
  EXPECT_EQ(scalar(plugin, k_wthr_old, "WTHR\\LightningColorBlue", 0, "12"), k_updated);
  EXPECT_EQ(payloads(plugin, key(k_wthr_old), "DATA").at(0)[14], std::byte{12});
  EXPECT_EQ(scalar(plugin, k_wthr, "WTHR\\DisabledCloudLayers", 0, "0"), k_updated);
  EXPECT_EQ(scalar(plugin, k_wthr, "WTHR\\VolatilityMult", 0, "5"), k_updated);
  EXPECT_EQ(scalar(plugin, k_wthr_old, "WTHR\\VisibilityMult", 0, "0.7"), k_missing);

  EXPECT_EQ(scalar(plugin, k_wthr, "WTHR\\SoundType", 0, "3"), k_updated);
  EXPECT_EQ(scalar(plugin, k_wthr, "WTHR\\SoundType", 1, "3"), k_missing);
  EXPECT_TRUE(scalar_refused(plugin, k_wthr, "WTHR\\SoundType", 0, "4"));

  EXPECT_EQ(count(plugin, k_wthr, "WTHR\\AmbientFresnelPower"), 8u);
  EXPECT_EQ(count(plugin, k_wthr_old, "WTHR\\AmbientFresnelPower"), 4u);
  EXPECT_EQ(scalar(plugin, k_wthr, "WTHR\\AmbientFresnelPower", 7, "2"), k_updated);
  EXPECT_EQ(scalar(plugin, k_wthr_old, "WTHR\\AmbientFresnelPower", 0, "2"), k_missing);
  EXPECT_EQ(scalar(plugin, k_wthr_old, "WTHR\\AmbientFresnelPower", 1, "2"), k_missing);
  EXPECT_EQ(scalar(plugin, k_wthr_old, "WTHR\\AmbientFresnelPower", 2, "2"), k_updated);
  EXPECT_EQ(scalar(plugin, k_wthr_old, "WTHR\\AmbientSpecularRed", 0, "5"), k_missing);
  EXPECT_EQ(scalar(plugin, k_wthr_old, "WTHR\\AmbientSpecularRed", 1, "5"), k_updated);
  EXPECT_EQ(scalar(plugin, k_wthr_old, "WTHR\\AmbientZMinusBlue", 0, "6"), k_updated);
  auto short_dalc = filled(24, 0);
  short_dalc[22] = std::byte{6};
  EXPECT_EQ(payloads(plugin, key(k_wthr_old), "DALC").at(0), short_dalc);
  EXPECT_EQ(payloads(plugin, key(k_wthr_old), "DALC").at(1).size(), 28u);

  EXPECT_EQ(scalar(plugin, k_wthr, "WTHR\\ActivateThreshold", 0, "0.25"), k_updated);
  EXPECT_EQ(scalar(plugin, k_wthr_old, "WTHR\\ActivateThreshold", 0, "0.25"), k_missing);
  EXPECT_EQ(scalar(plugin, k_wthr_old, "WTHR\\LightningThreshold", 0, "0.25"), k_updated);
  EXPECT_EQ(text(plugin, k_wthr, "WTHR\\CloudTexture0", 0, "sky\\b.dds"), k_updated);
  EXPECT_EQ(text(plugin, k_wthr, "WTHR\\CloudTexture31", 0, "sky\\b.dds"), k_missing);
  EXPECT_EQ(text(plugin, k_wthr, "WTHR\\AuroraModel", 0, "a.nif"), k_missing);

  const auto reloaded = reload(plugin);
  const auto after = snapshot(reloaded);
  for (const auto& [record, bytes] : before) {
    if (record != key(k_wthr) && record != key(k_wthr_old)) { EXPECT_EQ(after.at(record), bytes) << record.to_string(); }
  }
  EXPECT_EQ(order(reloaded, k_wthr), order(read(build(fixture(), true)), k_wthr));
  EXPECT_EQ(order(reloaded, k_wthr_old), order(read(build(fixture(), true)), k_wthr_old));
  EXPECT_TRUE(authoring_clean(reloaded, key(k_wthr)));
  EXPECT_TRUE(authoring_clean(reloaded, key(k_wthr_old)));
  const auto model = fo4::forms::decode_weather(reloaded, key(k_wthr));
  EXPECT_EQ(model.cloud_speed_y[31], 216);
  EXPECT_EQ(model.cloud_speed_x[0], 62);
  EXPECT_FLOAT_EQ(model.cloud_alphas[31 * 8 + 7], 0.5f);
  EXPECT_EQ(model.snam.at(0).type, 3u);
  EXPECT_FLOAT_EQ(model.vnam.value_or(0.0f), 5.0f);
}

TEST(ClimateWeatherAuthoring, PartialOptionalSpecularColorsStayAbsentAndUnchanged) {
  for (const std::size_t size : {24u, 25u, 26u, 27u}) {
    auto entries = fixture();
    for (auto& [name, data] : entries[2].subs) {
      if (name == "DALC") { data = filled(size, 0); }
    }
    for (const char* path : {"WTHR\\AmbientSpecularRed", "WTHR\\AmbientSpecularGreen", "WTHR\\AmbientSpecularBlue"}) {
      SCOPED_TRACE(std::to_string(size) + ' ' + path);
      auto plugin = read(build(entries, true));
      const auto before = fo4::write_plugin(plugin);
      EXPECT_EQ(scalar(plugin, k_wthr, path, 0, "5"), k_missing);
      EXPECT_EQ(fo4::write_plugin(plugin), before);
      const auto field = fo4::schema::scalar_field_descriptor(path);
      ASSERT_TRUE(field.has_value());
      auto payload = filled(size, 0);
      EXPECT_TRUE(fo4::schema::scalar_field_bytes(payload, field->scalar).empty());
      EXPECT_THROW(fo4::schema::patch_scalar_field(payload, field->scalar,
          fo4::schema::encode_scalar("5", field->scalar)), fo4::schema::ScalarError);
      EXPECT_EQ(payload, filled(size, 0));
    }
  }
}

TEST(ClimateWeatherAuthoring, WeatherSubrecordsBelowTheirXEditSizeRefuseScalarWrites) {
  auto entries = fixture();
  for (auto& [name, data] : entries[2].subs) {
    if (name == "DATA") { data = filled(18, 0); }
    if (name == "FNAM") { data = filled(31, 0); }
  }
  auto plugin = read(build(entries, true));
  const auto before = fo4::write_plugin(plugin);
  EXPECT_EQ(scalar(plugin, k_wthr, "WTHR\\WindSpeed", 0, "5"), k_missing);
  EXPECT_EQ(scalar(plugin, k_wthr, "WTHR\\FogDayNear", 0, "1"), k_missing);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
  EXPECT_EQ(scalar(plugin, k_wthr_old, "WTHR\\WindSpeed", 0, "5"), k_updated);

  auto sized = read(build(fixture(), true));
  EXPECT_EQ(scalar(sized, k_wthr, "WTHR\\WindSpeed", 0, "5"), k_updated);
  EXPECT_EQ(scalar(sized, k_wthr, "WTHR\\FogDayNear", 0, "1"), k_updated);
}

TEST(ClimateWeatherAuthoring, LinksCheckTargetsNullabilityStrideAndOccurrence) {
  auto plugin = read(build(fixture(), true));
  EXPECT_EQ(link(plugin, k_clmt, "CLMT\\Weather", 1, key(k_wthr)), k_link_updated);
  EXPECT_EQ(link(plugin, k_clmt, "CLMT\\Weather", 0, fo4::FormKey{}), k_link_updated);
  EXPECT_EQ(link(plugin, k_clmt, "CLMT\\Weather", 2, key(k_wthr)), k_link_missing);
  EXPECT_EQ(link(plugin, k_clmt, "CLMT\\WeatherGlobal", 0, key(k_glob)), k_link_updated);
  EXPECT_EQ(link(plugin, k_clmt, "CLMT\\WeatherGlobal", 1, fo4::FormKey{}), k_link_updated);
  EXPECT_EQ(payloads(plugin, key(k_clmt), "WLST").at(0), words({0, 60, k_local | k_glob, k_local | k_wthr, 40, 0}));
  EXPECT_TRUE(refused(plugin, k_clmt, "CLMT\\Weather", 0, key(k_misc)));
  EXPECT_TRUE(refused(plugin, k_clmt, "CLMT\\WeatherGlobal", 0, key(k_stat)));
  EXPECT_TRUE(refused(plugin, k_clmt, "CLMT\\Weather", 0, fo4::FormKey{fo4::PluginKey::from_filename("Other.esm"), fo4::LocalFormId{0x900}}));

  EXPECT_EQ(link(plugin, k_wthr, "WTHR\\Precipitation", 0, key(k_spgd)), k_link_updated);
  EXPECT_EQ(link(plugin, k_wthr, "WTHR\\Precipitation", 0, fo4::FormKey{}), k_link_updated);
  EXPECT_TRUE(refused(plugin, k_wthr, "WTHR\\Precipitation", 0, key(k_misc)));
  EXPECT_EQ(link(plugin, k_wthr, "WTHR\\VisualEffect", 0, key(k_rfct)), k_link_updated);
  EXPECT_TRUE(refused(plugin, k_wthr, "WTHR\\VisualEffect", 0, key(k_spgd)));
  EXPECT_EQ(link(plugin, k_wthr, "WTHR\\Sound", 0, key(k_soun)), k_link_updated);
  EXPECT_EQ(link(plugin, k_wthr, "WTHR\\Sound", 0, key(k_sndr)), k_link_updated);
  EXPECT_EQ(link(plugin, k_wthr, "WTHR\\Sound", 1, key(k_sndr)), k_link_missing);
  EXPECT_TRUE(refused(plugin, k_wthr, "WTHR\\Sound", 0, key(k_stat)));
  EXPECT_EQ(count(plugin, k_wthr, "WTHR\\SkyStatic"), 2u);
  EXPECT_EQ(link(plugin, k_wthr, "WTHR\\SkyStatic", 1, key(k_stat)), k_link_updated);
  EXPECT_EQ(link(plugin, k_wthr, "WTHR\\SkyStatic", 0, fo4::FormKey{}), k_link_updated);
  EXPECT_TRUE(refused(plugin, k_wthr, "WTHR\\SkyStatic", 0, key(k_misc)));
  EXPECT_EQ(payloads(plugin, key(k_wthr), "TNAM"), (std::vector<Bytes>{words({0}), words({k_local | k_stat})}));

  EXPECT_EQ(count(plugin, k_wthr, "WTHR\\ImageSpace"), 8u);
  EXPECT_EQ(count(plugin, k_wthr_old, "WTHR\\ImageSpace"), 4u);
  EXPECT_EQ(link(plugin, k_wthr, "WTHR\\ImageSpace", 7, key(k_imgs)), k_link_updated);
  EXPECT_EQ(link(plugin, k_wthr_old, "WTHR\\ImageSpace", 3, key(k_imgs)), k_link_updated);
  EXPECT_EQ(link(plugin, k_wthr_old, "WTHR\\ImageSpace", 4, key(k_imgs)), k_link_missing);
  EXPECT_EQ(payloads(plugin, key(k_wthr_old), "IMSP").at(0), words({0, 0, 0, k_local | k_imgs}));
  EXPECT_EQ(link(plugin, k_wthr, "WTHR\\GodRays", 7, key(k_gdry)), k_link_updated);
  EXPECT_EQ(link(plugin, k_wthr_old, "WTHR\\GodRays", 0, key(k_gdry)), k_link_missing);
  EXPECT_TRUE(refused(plugin, k_wthr, "WTHR\\GodRays", 0, key(k_imgs)));
  EXPECT_EQ(link(plugin, k_wthr, "WTHR\\SunGlareLensFlare", 0, key(k_lens)), fo4::FormLinkFieldMutationStatus::unchanged);
  EXPECT_TRUE(refused(plugin, k_wthr, "WTHR\\SunGlareLensFlare", 0, fo4::FormKey{}));
  EXPECT_EQ(link(plugin, k_wthr_old, "WTHR\\SunGlareLensFlare", 0, key(k_lens)), k_link_missing);
  EXPECT_EQ(link(plugin, k_wthr_old, "WTHR\\LightningSpell", 0, key(k_spel)), k_link_updated);
  EXPECT_EQ(link(plugin, k_wthr_old, "WTHR\\ActivateSpell", 0, key(k_spel)), k_link_updated);
  EXPECT_TRUE(refused(plugin, k_wthr, "WTHR\\ActivateSpell", 0, key(k_misc)));
  EXPECT_EQ(payloads(plugin, key(k_wthr_old), "UNAM").at(0), words({k_local | k_spel, 0, k_local | k_spel}));

  const auto reloaded = reload(plugin);
  EXPECT_EQ(fo4::forms::decode_weather(reloaded, key(k_wthr)).tnam.at(0), std::nullopt);
  EXPECT_EQ(fo4::forms::decode_weather(reloaded, key(k_wthr)).imsp[7], key(k_imgs));
  EXPECT_TRUE(authoring_clean(reloaded, key(k_clmt)));
}

TEST(ClimateWeatherAuthoring, UnknownWeatherLinkShapesRefuseWithoutChangingBytes) {
  struct Case {
    const char* subrecord;
    const char* path;
    std::size_t size;
    std::size_t index;
    std::uint32_t target;
  };
  for (const auto& test : {
      Case{"IMSP", "WTHR\\ImageSpace", 36, 8, k_imgs},
      Case{"IMSP", "WTHR\\ImageSpace", 20, 0, k_imgs},
      Case{"WGDR", "WTHR\\GodRays", 36, 8, k_gdry},
      Case{"WGDR", "WTHR\\GodRays", 28, 0, k_gdry},
      Case{"UNAM", "WTHR\\LightningSpell", 16, 0, k_spel},
      Case{"UNAM", "WTHR\\ActivateSpell", 28, 0, k_spel},
      Case{"MNAM", "WTHR\\Precipitation", 8, 0, k_spgd},
      Case{"NNAM", "WTHR\\VisualEffect", 8, 0, k_rfct},
      Case{"SNAM", "WTHR\\Sound", 12, 0, k_sndr},
      Case{"TNAM", "WTHR\\SkyStatic", 8, 0, k_stat},
      Case{"GNAM", "WTHR\\SunGlareLensFlare", 8, 0, k_lens}}) {
    SCOPED_TRACE(std::string(test.path) + ' ' + std::to_string(test.size));
    auto entries = fixture();
    for (auto& [name, data] : entries[2].subs) {
      if (name == test.subrecord) { data = filled(test.size, 0); }
    }
    auto plugin = read(build(entries, true));
    const auto before = fo4::write_plugin(plugin);
    EXPECT_THROW(link(plugin, k_wthr, test.path, test.index, key(test.target)),
                 fo4::schema::FormLinkFieldError);
    EXPECT_EQ(fo4::write_plugin(plugin), before);
    EXPECT_THROW(count(plugin, k_wthr, test.path), fo4::schema::FormLinkFieldError);
  }
}

TEST(ClimateWeatherAuthoring, OptionalSubrecordsAddAndRemoveAtTheirSchemaPosition) {
  auto plugin = read(build(fixture(), true));
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_clmt_bare), sig("GNAM")), fo4::SubrecordAddStatus::added);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_clmt_bare), sig("WLST")), fo4::SubrecordAddStatus::added);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_clmt_bare), sig("FNAM")), fo4::SubrecordAddStatus::added);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_clmt_bare), sig("FNAM")), fo4::SubrecordAddStatus::already_present);
  EXPECT_EQ(order(plugin, k_clmt_bare), (Order{"EDID", "WLST", "FNAM", "GNAM", "TNAM"}));
  EXPECT_EQ(payloads(plugin, key(k_clmt_bare), "WLST").at(0), filled(12, 0));
  EXPECT_EQ(payloads(plugin, key(k_clmt_bare), "FNAM").at(0), zstr(""));
  EXPECT_EQ(count(plugin, k_clmt_bare, "CLMT\\Weather"), 1u);
  EXPECT_EQ(link(plugin, k_clmt_bare, "CLMT\\Weather", 0, key(k_wthr)), k_link_updated);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_clmt_bare), sig("MODL")), fo4::SubrecordAddStatus::not_authorable);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_clmt_bare), sig("MODT")), fo4::SubrecordAddStatus::no_default);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_clmt_bare), sig("XXXX")), fo4::SubrecordAddStatus::not_authorable);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(k_clmt_bare), sig("TNAM"), 0), fo4::SubrecordRemoveStatus::below_minimum);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(k_clmt), sig("MODL"), 0), fo4::SubrecordRemoveStatus::not_authorable);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(k_clmt), sig("MODT"), 0), fo4::SubrecordRemoveStatus::removed);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(k_clmt), sig("WLST"), 0), fo4::SubrecordRemoveStatus::removed);
  EXPECT_EQ(order(plugin, k_clmt), (Order{"EDID", "FNAM", "GNAM", "MODL", "TNAM"}));

  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_wthr), sig("SNAM")), fo4::SubrecordAddStatus::added);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_wthr), sig("TNAM")), fo4::SubrecordAddStatus::added);
  EXPECT_EQ(payloads(plugin, key(k_wthr), "SNAM").at(1), filled(8, 0));
  EXPECT_EQ(payloads(plugin, key(k_wthr), "TNAM").at(2), filled(4, 0));
  EXPECT_EQ(count(plugin, k_wthr, "WTHR\\SoundType"), 2u);
  EXPECT_EQ(scalar(plugin, k_wthr, "WTHR\\SoundType", 1, "2"), k_updated);
  EXPECT_EQ(link(plugin, k_wthr, "WTHR\\Sound", 1, key(k_soun)), k_link_updated);
  EXPECT_EQ(link(plugin, k_wthr, "WTHR\\SkyStatic", 2, key(k_stat)), k_link_updated);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(k_wthr), sig("TNAM"), 0), fo4::SubrecordRemoveStatus::removed);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(k_wthr), sig("SNAM"), 5), fo4::SubrecordRemoveStatus::absent);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_wthr_old), sig("TNAM")), fo4::SubrecordAddStatus::added);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_wthr_old), sig("SNAM")), fo4::SubrecordAddStatus::added);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_wthr_old), sig("10TX")), fo4::SubrecordAddStatus::added);
  EXPECT_EQ(order(plugin, k_wthr_old), (Order{"EDID", "10TX", "LNAM", "MNAM", "NNAM", "RNAM", "QNAM", "PNAM", "JNAM", "NAM0", "FNAM", "DATA", "NAM1",
                                              "SNAM", "TNAM", "IMSP", "DALC", "DALC", "DALC", "DALC", "UNAM"}));
  EXPECT_EQ(text(plugin, k_wthr_old, "WTHR\\CloudTexture1", 0, "sky\\c.dds"), k_updated);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_wthr), sig("00TX")), fo4::SubrecordAddStatus::already_present);
  for (const char* required : {"LNAM", "MNAM", "NNAM", "RNAM", "QNAM", "PNAM", "JNAM", "NAM0", "FNAM", "DATA", "NAM1", "IMSP"}) {
    EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(k_wthr), sig(required), 0), fo4::SubrecordRemoveStatus::below_minimum) << required;
  }
  for (const char* coupled : {"DALC", "NAM4", "WGDR", "UNAM", "VNAM", "WNAM"}) {
    EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(k_wthr), sig(coupled), 0), fo4::SubrecordRemoveStatus::not_authorable) << coupled;
  }
  for (const char* coupled : {"DALC", "NAM4", "WGDR", "VNAM", "WNAM", "MODL"}) {
    EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_wthr_old), sig(coupled)), fo4::SubrecordAddStatus::not_authorable) << coupled;
  }
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(k_wthr), sig("GNAM"), 0), fo4::SubrecordRemoveStatus::removed);
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_wthr), sig("GNAM")), fo4::SubrecordAddStatus::no_default);
  EXPECT_EQ(fo4::remove_record_subrecord(plugin, key(k_wthr), sig("00TX"), 0), fo4::SubrecordRemoveStatus::removed);

  const auto reloaded = reload(plugin);
  for (const auto id : {k_clmt, k_clmt_bare, k_wthr, k_wthr_old}) { EXPECT_TRUE(authoring_clean(reloaded, key(id))) << id; }
  const auto old = fo4::forms::decode_weather(reloaded, key(k_wthr_old));
  EXPECT_EQ(old.cloud_textures[1], std::optional<std::string>{"sky\\c.dds"});
  ASSERT_EQ(old.tnam.size(), 1u);
  EXPECT_EQ(old.tnam[0], std::nullopt);
  ASSERT_EQ(old.snam.size(), 1u);
  EXPECT_EQ(old.snam[0].sound, std::nullopt);
}

TEST(ClimateWeatherAuthoring, UnknownSubrecordsBlockAddButKeepFieldEdits) {
  auto entries = fixture();
  entries[1].subs.push_back({"XUNK", filled(3, 9)});
  auto plugin = read(build(entries, true));
  EXPECT_EQ(fo4::add_record_subrecord(plugin, key(k_clmt_bare), sig("FNAM")), fo4::SubrecordAddStatus::unknown_order);
  EXPECT_EQ(scalar(plugin, k_clmt_bare, "CLMT\\SunriseEnd", 0, "60"), k_updated);
  EXPECT_EQ(payloads(reload(plugin), key(k_clmt_bare), "XUNK").at(0), filled(3, 9));
}

TEST(ClimateWeatherAuthoring, ClimateCreatesFromProvenDefaultsAndWeatherCreationIsRefused) {
  auto plugin = read(build({}, false));
  const auto refused_before = fo4::write_plugin(plugin);
  EXPECT_EQ(fo4::create_record(plugin, sig("WTHR"), "NewWeather").status, fo4::RecordCreateStatus::no_default);
  EXPECT_EQ(fo4::write_plugin(plugin), refused_before);
  const auto created = fo4::create_record(plugin, sig("CLMT"), "NewClimate");
  ASSERT_EQ(created.status, fo4::RecordCreateStatus::created);
  const auto id = created.key.local().value();
  EXPECT_EQ(order(plugin, id), (Order{"EDID", "TNAM"}));
  EXPECT_EQ(payloads(plugin, created.key, "TNAM").at(0), hex("000000000003"));
  EXPECT_TRUE(authoring_clean(plugin, created.key));
  ASSERT_EQ(fo4::add_record_subrecord(plugin, created.key, sig("WLST")), fo4::SubrecordAddStatus::added);
  ASSERT_EQ(fo4::add_record_subrecord(plugin, created.key, sig("FNAM")), fo4::SubrecordAddStatus::added);
  EXPECT_EQ(fo4::set_record_scalar(plugin, created.key, *fo4::schema::scalar_field_descriptor("CLMT\\WeatherChance"), 0, "100"), k_updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, created.key, *fo4::schema::scalar_field_descriptor("CLMT\\SunriseBegin"), 0, "30"), k_updated);
  EXPECT_EQ(fo4::set_record_string(plugin, created.key, "CLMT\\SunTexture", 0, "sky\\sun.dds"), k_updated);

  const auto root = std::filesystem::temp_directory_path() / ("radicle_sky_create_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto path = root / k_plugin;
  fo4::write_plugin_file(plugin, path);
  {
    const std::filesystem::path paths[] = {path};
    const auto env = fo4::env::Environment::load(paths);
    const auto climate = fo4::env::decode_climate(env, *env.winner(created.key));
    ASSERT_TRUE(climate.timing.has_value());
    EXPECT_EQ(climate.timing->sunrise_begin, 30);
    EXPECT_EQ(climate.timing->moons_phase_length, 3);
    ASSERT_EQ(climate.weathers.size(), 1u);
    EXPECT_EQ(climate.weathers[0].chance, 100);
    EXPECT_EQ(climate.sun_texture, std::optional<std::string_view>{"sky\\sun.dds"});
  }
  std::error_code error;
  std::filesystem::remove_all(root, error);
}

TEST(ClimateWeatherReader, CloudSpeedsAreUnsignedBytesWithTheirStoredBias) {
  auto entries = fixture();
  for (auto& [name, data] : entries[2].subs) {
    if (name == "RNAM") { data[0] = std::byte{216}; data[1] = std::byte{62}; }
  }
  const auto model = fo4::forms::decode_weather(read(build(entries, true)), key(k_wthr));
  EXPECT_EQ(model.cloud_speed_y[0], 216);
  EXPECT_EQ(model.cloud_speed_y[1], 62);
  EXPECT_EQ(model.cloud_speed_y[2], 127);
}

TEST(ClimateWeatherReader, NullSkyStaticsAndShortAmbientColorsDecode) {
  auto plugin = read(build(fixture(), true));
  const auto modern = fo4::forms::decode_weather(plugin, key(k_wthr));
  ASSERT_EQ(modern.tnam.size(), 2u);
  EXPECT_EQ(modern.tnam[0], key(k_stat));
  EXPECT_EQ(modern.tnam[1], std::nullopt);
  const auto old = fo4::forms::decode_weather(plugin, key(k_wthr_old));
  ASSERT_EQ(old.dalc.size(), 4u);
  EXPECT_FALSE(old.dalc[0].specular.has_value());
  EXPECT_FALSE(old.dalc[0].fresnel.has_value());
  EXPECT_TRUE(old.dalc[1].specular.has_value());
  EXPECT_FALSE(old.dalc[1].fresnel.has_value());
  EXPECT_TRUE(old.dalc[2].specular.has_value());
  EXPECT_TRUE(old.dalc[2].fresnel.has_value());

  for (const std::size_t size : {20u, 30u, 36u}) {
    auto entries = fixture();
    for (auto& [name, data] : entries[3].subs) {
      if (name == "DALC") { data = filled(size, 0); }
    }
    EXPECT_THROW(fo4::forms::decode_weather(read(build(entries, true)), key(k_wthr_old)), fo4::forms::WeatherError) << size;
  }
}

TEST(ClimateWeatherReader, CloudColorLayerCountsMatchTheStoredLayouts) {
  for (const std::size_t size : {64u, 512u, 1024u}) {
    SCOPED_TRACE(size);
    auto entries = fixture();
    entries[3].version = size == 64 ? 34 : size == 512 ? 110 : 131;
    if (size == 1024) { for (int i = 0; i < 4; ++i) { entries[3].subs.push_back({"DALC", filled(32, 0)}); } }
    for (auto& [name, data] : entries[3].subs) {
      if (name == "PNAM") { data = filled(size, 1); }
      if (name == "JNAM") { data = filled(size == 64 ? 512 : size, 0); }
      if (name == "NAM0") { data = filled(size == 64 ? 208 : size == 512 ? 272 : 608, 0); }
      if (name == "FNAM") { data = filled(size == 1024 ? 72 : 32, 0); }
      if (name == "DATA") { data = filled(size == 1024 ? 20 : 19, 0); }
      if (name == "IMSP") { data = filled(size == 1024 ? 32 : 16, 0); }
      if (name == "UNAM") { data = filled(size == 1024 ? 24 : 12, 0); }
    }
    const auto model = fo4::forms::decode_weather(read(build(entries, true)), key(k_wthr_old));
    EXPECT_EQ(model.cloud_color_layer_count, size == 64 ? 4u : 32u);
  }
}

TEST(ClimateWeatherReader, PnamAndJnamLengthFollowTheFormVersionNotTheSize) {
  auto entries = fixture();
  entries[3].version = 110;
  for (auto& [name, data] : entries[3].subs) {
    if (name == "PNAM" || name == "JNAM") { data = filled(1024, 0); }
    if (name == "NAM0") { data = filled(272, 0); }
  }
  EXPECT_THROW(fo4::forms::decode_weather(read(build(entries, true)), key(k_wthr_old)), fo4::forms::WeatherError);
}

TEST(ClimateWeatherReader, FnamLengthFollowsTheFormVersionNotTheSize) {
  auto entries = fixture();
  entries[2].version = 120;
  for (auto& [name, data] : entries[2].subs) {
    if (name == "FNAM") { data = filled(32, 0); }
    if (name == "UNAM") { data = filled(12, 0); }
  }
  EXPECT_THROW(fo4::forms::decode_weather(read(build(entries, true)), key(k_wthr)), fo4::forms::WeatherError);
}

TEST(ClimateWeatherReader, UnamAndImspSizesFollowTheFormVersionNotTheSize) {
  auto modern = fixture();
  modern[2].version = 129;
  EXPECT_THROW(fo4::forms::decode_weather(read(build(modern, true)), key(k_wthr)), fo4::forms::WeatherError);

  auto legacy = fixture();
  legacy[3].version = 110;
  for (auto& [name, data] : legacy[3].subs) {
    if (name == "IMSP") { data = filled(32, 0); }
  }
  EXPECT_THROW(fo4::forms::decode_weather(read(build(legacy, true)), key(k_wthr_old)), fo4::forms::WeatherError);
}

TEST(ClimateWeatherReader, FourLayerCloudColorsDecodeOnlyBeforeFormVersion35) {
  for (const std::uint16_t version : {std::uint16_t{34}, std::uint16_t{35}}) {
    auto entries = fixture();
    entries[3].version = version;
    for (auto& [name, data] : entries[3].subs) {
      if (name == "PNAM") { data = filled(64, 1); }
      if (name == "NAM0") { data = filled(version < 35 ? 208 : 224, 0); }
    }
    auto plugin = read(build(entries, true));
    if (version < 35) {
      const auto model = fo4::forms::decode_weather(plugin, key(k_wthr_old));
      EXPECT_EQ(model.cloud_colors.size(), 16u);
      EXPECT_EQ(model.cloud_alphas.size(), 128u);
    } else {
      EXPECT_THROW(fo4::forms::decode_weather(plugin, key(k_wthr_old)), fo4::forms::WeatherError);
    }
  }
  auto mismatched = fixture();
  mismatched[3].version = 34;
  for (auto& [name, data] : mismatched[3].subs) {
    if (name == "PNAM") { data = filled(64, 1); }
    if (name == "JNAM") { data = filled(1024, 0); }
  }
  EXPECT_THROW(fo4::forms::decode_weather(read(build(mismatched, true)), key(k_wthr_old)), fo4::forms::WeatherError);
}

TEST(ClimateWeatherAuthoring, SetClimateWeathersRewritesOnlyWlstAndRefusesUnlistedOwners) {
  auto plugin = read(build(fixture(), true));
  const auto before = snapshot(plugin);
  const std::vector<fo4::env::ClimateWeather> weathers{{key(k_wthr_old), -3, key(k_glob)}, {key(k_wthr), 7, {}}};
  const std::vector<fo4::env::ClimateWeather> foreign{{fo4::FormKey{fo4::PluginKey::from_filename("Other.esp"), fo4::LocalFormId{0x12}}, 1, {}}};
  EXPECT_FALSE(fo4::env::set_climate_weathers(plugin, key(0x999), weathers));
  EXPECT_THROW(fo4::env::set_climate_weathers(plugin, key(k_wthr), weathers), fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::set_climate_weathers(plugin, key(k_clmt), foreign), fo4::env::WorldDecodeError);
  EXPECT_TRUE(snapshot(plugin) == before);
  EXPECT_TRUE(fo4::env::set_climate_weathers(plugin, key(k_clmt), weathers));

  const auto reloaded = reload(plugin);
  EXPECT_EQ(payloads(reloaded, key(k_clmt), "WLST").at(0),
            words({k_local | k_wthr_old, 0xFFFFFFFDu, k_local | k_glob, k_local | k_wthr, 7u, 0u}));
  EXPECT_EQ(payloads(reloaded, key(k_clmt), "TNAM").at(0), fixture_payload(k_clmt, "TNAM"));
  EXPECT_EQ(payloads(reloaded, key(k_clmt), "FNAM").at(0), fixture_payload(k_clmt, "FNAM"));
  EXPECT_EQ(payloads(reloaded, key(k_clmt), "MODT").at(0), fixture_payload(k_clmt, "MODT"));
  const auto after = snapshot(reloaded);
  for (const auto& [record, bytes] : before) {
    if (record != key(k_clmt)) { EXPECT_EQ(after.at(record), bytes) << record.to_string(); }
  }
  const auto env = fo4::env::Environment::from_plugins({reloaded});
  const auto climate = fo4::env::decode_climate(env, *env.winner(key(k_clmt)));
  ASSERT_EQ(climate.weathers.size(), 2u);
  EXPECT_EQ(climate.weathers[0].weather, key(k_wthr_old));
  EXPECT_EQ(climate.weathers[0].chance, -3);
  EXPECT_EQ(climate.weathers[0].global, key(k_glob));
  EXPECT_EQ(climate.weathers[1].weather, key(k_wthr));
  EXPECT_EQ(climate.weathers[1].chance, 7);
  EXPECT_TRUE(climate.weathers[1].global.owner().empty());
}

TEST(ClimateWeatherAuthoring, SetClimateWeathersAddsAnAbsentWlstAfterEdidAndRefusesOddWlst) {
  auto plugin = read(build(fixture(), true));
  const auto before = snapshot(plugin);
  EXPECT_TRUE(fo4::env::set_climate_weathers(plugin, key(k_clmt_bare), {}));
  EXPECT_TRUE(snapshot(plugin) == before);
  const std::vector<fo4::env::ClimateWeather> weathers{{key(k_wthr), 7, {}}};
  EXPECT_TRUE(fo4::env::set_climate_weathers(plugin, key(k_clmt_bare), weathers));

  const auto reloaded = reload(plugin);
  EXPECT_EQ(order(reloaded, k_clmt_bare), (Order{"EDID", "WLST", "TNAM"}));
  EXPECT_EQ(payloads(reloaded, key(k_clmt_bare), "WLST").at(0), words({k_local | k_wthr, 7u, 0u}));
  EXPECT_EQ(payloads(reloaded, key(k_clmt_bare), "TNAM").at(0), fixture_payload(k_clmt_bare, "TNAM"));
  const auto env = fo4::env::Environment::from_plugins({reloaded});
  const auto climate = fo4::env::decode_climate(env, *env.winner(key(k_clmt_bare)));
  ASSERT_EQ(climate.weathers.size(), 1u);
  EXPECT_EQ(climate.weathers[0].weather, key(k_wthr));
  EXPECT_EQ(climate.weathers[0].chance, 7);

  auto entries = fixture();
  entries[0].subs[1].second.push_back(std::byte{0});
  auto odd = read(build(entries, true));
  const auto odd_before = snapshot(odd);
  EXPECT_THROW(fo4::env::set_climate_weathers(odd, key(k_clmt), weathers), fo4::env::WorldDecodeError);
  EXPECT_TRUE(snapshot(odd) == odd_before);
}

TEST(ClimateWeatherAuthoring, SetWeatherColorsWritesOneGroupsRgbAndKeepsTheUnusedByte) {
  auto entries = fixture();
  for (auto& [name, data] : entries[2].subs) {
    if (name == "NAM0") { data = filled(608, 0xA5); }
  }
  for (auto& [name, data] : entries[3].subs) {
    if (name == "NAM0") { data = filled(272, 0xA5); }
  }
  auto plugin = read(build(entries, true));
  const auto before = snapshot(plugin);
  const std::vector<fo4::env::ByteColor> times{{1, 2, 3}, {4, 5, 6}, {7, 8, 9}, {10, 11, 12},
                                               {13, 14, 15}, {16, 17, 18}, {19, 20, 21}, {22, 23, 24}};
  const auto group = fo4::env::WeatherColorGroup::sun_glare;
  const auto legacy_times = std::span<const fo4::env::ByteColor>(times).first(4);
  EXPECT_THROW(fo4::env::set_weather_colors(plugin, key(k_wthr), group, legacy_times), fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::set_weather_colors(plugin, key(k_wthr_old), fo4::env::WeatherColorGroup::fog_far_high, legacy_times),
               fo4::env::WorldDecodeError);
  EXPECT_TRUE(fo4::env::set_weather_colors(plugin, key(k_wthr), group, times));
  EXPECT_TRUE(fo4::env::set_weather_colors(plugin, key(k_wthr_old), group, legacy_times));

  const auto reloaded = reload(plugin);
  const auto g = static_cast<std::size_t>(group);
  Bytes modern = filled(608, 0xA5);
  for (std::size_t time = 0; time < 8; ++time) {
    const auto at = (g * 8 + time) * 4;
    modern[at] = std::byte{times[time].red};
    modern[at + 1] = std::byte{times[time].green};
    modern[at + 2] = std::byte{times[time].blue};
  }
  Bytes legacy = filled(272, 0xA5);
  for (std::size_t time = 0; time < 4; ++time) {
    const auto at = (g * 4 + time) * 4;
    legacy[at] = std::byte{times[time].red};
    legacy[at + 1] = std::byte{times[time].green};
    legacy[at + 2] = std::byte{times[time].blue};
  }
  EXPECT_EQ(payloads(reloaded, key(k_wthr), "NAM0").at(0), modern);
  EXPECT_EQ(payloads(reloaded, key(k_wthr_old), "NAM0").at(0), legacy);
  const auto after = snapshot(reloaded);
  for (const auto& [record, bytes] : before) {
    if (record != key(k_wthr) && record != key(k_wthr_old)) { EXPECT_EQ(after.at(record), bytes) << record.to_string(); }
  }
  const auto env = fo4::env::Environment::from_plugins({reloaded});
  const auto color = fo4::env::decode_weather(env, *env.winner(key(k_wthr))).colors->color(group, fo4::env::k_weather_day);
  ASSERT_TRUE(color.has_value());
  EXPECT_EQ(color->red, 4);
  EXPECT_EQ(color->green, 5);
  EXPECT_EQ(color->blue, 6);
}

TEST(ClimateWeatherAuthoring, SetWeatherFogRewritesFnamAndRefusesAWrongDistanceCount) {
  auto plugin = read(build(fixture(), true));
  const auto before = snapshot(plugin);
  std::vector<float> fog(18);
  for (std::size_t i = 0; i < fog.size(); ++i) { fog[i] = static_cast<float>(i + 1); }
  const std::vector<float> legacy_fog(fog.begin(), fog.begin() + 8);
  EXPECT_THROW(fo4::env::set_weather_fog(plugin, key(k_wthr), legacy_fog), fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::set_weather_fog(plugin, key(k_wthr_old), fog), fo4::env::WorldDecodeError);
  EXPECT_TRUE(snapshot(plugin) == before);
  EXPECT_TRUE(fo4::env::set_weather_fog(plugin, key(k_wthr), fog));
  EXPECT_TRUE(fo4::env::set_weather_fog(plugin, key(k_wthr_old), legacy_fog));

  const auto reloaded = reload(plugin);
  EXPECT_EQ(payloads(reloaded, key(k_wthr), "NAM0").at(0), fixture_payload(k_wthr, "NAM0"));
  EXPECT_EQ(payloads(reloaded, key(k_wthr), "DATA").at(0), fixture_payload(k_wthr, "DATA"));
  const auto after = snapshot(reloaded);
  for (const auto& [record, bytes] : before) {
    if (record != key(k_wthr) && record != key(k_wthr_old)) { EXPECT_EQ(after.at(record), bytes) << record.to_string(); }
  }
  const auto env = fo4::env::Environment::from_plugins({reloaded});
  EXPECT_EQ(fo4::env::decode_weather(env, *env.winner(key(k_wthr))).fog_distances, fog);
  EXPECT_EQ(fo4::env::decode_weather(env, *env.winner(key(k_wthr_old))).fog_distances, legacy_fog);
}

TEST(ClimateWeatherAuthoring, SetWeatherTransitionWritesOnlyTheDeltaByte) {
  auto plugin = read(build(fixture(), true));
  const auto before = snapshot(plugin);
  EXPECT_TRUE(fo4::env::set_weather_transition(plugin, key(k_wthr), 200));
  EXPECT_TRUE(fo4::env::set_weather_transition(plugin, key(k_wthr_old), 9));
  EXPECT_FALSE(fo4::env::set_weather_transition(plugin, key(0x999), 9));

  const auto reloaded = reload(plugin);
  auto expected = fixture_payload(k_wthr, "DATA");
  expected[3] = std::byte{200};
  EXPECT_EQ(payloads(reloaded, key(k_wthr), "DATA").at(0), expected);
  auto expected_old = fixture_payload(k_wthr_old, "DATA");
  expected_old[3] = std::byte{9};
  EXPECT_EQ(payloads(reloaded, key(k_wthr_old), "DATA").at(0), expected_old);
  EXPECT_EQ(payloads(reloaded, key(k_wthr), "FNAM").at(0), fixture_payload(k_wthr, "FNAM"));
  const auto after = snapshot(reloaded);
  for (const auto& [record, bytes] : before) {
    if (record != key(k_wthr) && record != key(k_wthr_old)) { EXPECT_EQ(after.at(record), bytes) << record.to_string(); }
  }
  const auto env = fo4::env::Environment::from_plugins({reloaded});
  const auto parameters = fo4::env::decode_weather(env, *env.winner(key(k_wthr))).parameters;
  ASSERT_TRUE(parameters.has_value());
  EXPECT_EQ(parameters->transition_delta, 200);
}
