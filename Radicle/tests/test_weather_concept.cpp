#include "fo4/forms/weather_concept.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/plugin/visit.hpp"
#include "plugin_bytes.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct WeatherPluginOptions {
  std::size_t pnam_jnam_size = 1024;
  std::size_t imsp_size = 32;
  std::size_t data_size = 20;
  int dalc_count = 8;
  std::size_t dalc_size = 32;
  bool include_nam4 = true;
  int unam_size = 24;
  std::size_t fnam_size = 72;
  std::size_t nam0_size = 608;
  std::uint32_t weather_form_id = 0x00000800;
  std::uint16_t form_version = 131;
  std::uint32_t snam_form_id = 0x00000810;
  std::uint32_t tnam_form_id = 0x00000820;
  std::uint32_t mnam = 0;
  std::uint32_t nnam = 0;
  bool include_local_spgd = false;
  std::uint32_t spgd_form_id = 0x00000830;
  bool include_local_misc = false;
  std::uint32_t misc_form_id = 0x00000840;
  std::vector<std::string> masters;
};

void write_zeros(fo4::test::ByteWriter& out, std::size_t count) {
  for (std::size_t i = 0; i < count; ++i) out.u8(0);
}

void write_master(fo4::test::ByteWriter& out, std::string_view filename) {
  std::string mast{filename};
  mast.push_back('\0');
  fo4::test::write_subrecord(out, "MAST", mast);
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
}

void write_minimal_spgd(fo4::test::ByteWriter& out, std::uint32_t form_id) {
  const auto group = fo4::test::begin_group(out, "SPGD", 0);
  const auto record = fo4::test::begin_record(out, "SPGD", 0, form_id);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"TestSpgd\0", 9});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

void write_minimal_misc(fo4::test::ByteWriter& out, std::uint32_t form_id) {
  const auto group = fo4::test::begin_group(out, "MISC", 0);
  const auto record = fo4::test::begin_record(out, "MISC", 0, form_id);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"TestMisc\0", 9});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

void write_weather_record(fo4::test::ByteWriter& out, const WeatherPluginOptions& options) {
  const auto group = fo4::test::begin_group(out, "WTHR", 0);
  const auto record = fo4::test::begin_record(out, "WTHR", 0, options.weather_form_id, options.form_version);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"TestWeather\0", 12});
  fo4::test::write_subrecord(out, "00TX", std::string_view{"Textures\\Sky\\Clouds\\Test.dds\0", 29});
  {
    fo4::test::ByteWriter lnam;
    lnam.u32(0);
    fo4::test::write_subrecord(out, "LNAM", lnam.bytes);
  }
  {
    fo4::test::ByteWriter body;
    body.u32(options.mnam);
    fo4::test::write_subrecord(out, "MNAM", body.bytes);
  }
  {
    fo4::test::ByteWriter body;
    body.u32(options.nnam);
    fo4::test::write_subrecord(out, "NNAM", body.bytes);
  }
  {
    fo4::test::ByteWriter body;
    write_zeros(body, 32);
    fo4::test::write_subrecord(out, "RNAM", body.bytes);
    fo4::test::write_subrecord(out, "QNAM", body.bytes);
  }
  {
    fo4::test::ByteWriter body;
    write_zeros(body, options.pnam_jnam_size);
    fo4::test::write_subrecord(out, "PNAM", body.bytes);
    fo4::test::write_subrecord(out, "JNAM", body.bytes);
  }
  {
    fo4::test::ByteWriter body;
    write_zeros(body, options.nam0_size);
    fo4::test::write_subrecord(out, "NAM0", body.bytes);
  }
  if (options.include_nam4) {
    fo4::test::ByteWriter body;
    write_zeros(body, 128);
    fo4::test::write_subrecord(out, "NAM4", body.bytes);
  }
  {
    fo4::test::ByteWriter body;
    write_zeros(body, options.fnam_size);
    fo4::test::write_subrecord(out, "FNAM", body.bytes);
  }
  {
    fo4::test::ByteWriter body;
    write_zeros(body, options.data_size);
    fo4::test::write_subrecord(out, "DATA", body.bytes);
  }
  {
    fo4::test::ByteWriter body;
    body.u32(0);
    fo4::test::write_subrecord(out, "NAM1", body.bytes);
  }
  {
    fo4::test::ByteWriter body;
    body.u32(options.snam_form_id);
    body.u32(0);
    fo4::test::write_subrecord(out, "SNAM", body.bytes);
  }
  {
    fo4::test::ByteWriter body;
    body.u32(options.tnam_form_id);
    fo4::test::write_subrecord(out, "TNAM", body.bytes);
  }
  {
    fo4::test::ByteWriter body;
    const auto slots = options.imsp_size / 4;
    for (std::size_t i = 0; i < slots; ++i) body.u32(0);
    fo4::test::write_subrecord(out, "IMSP", body.bytes);
  }
  {
    fo4::test::ByteWriter body;
    write_zeros(body, 32);
    fo4::test::write_subrecord(out, "WGDR", body.bytes);
  }
  for (int i = 0; i < options.dalc_count; ++i) {
    fo4::test::ByteWriter body;
    write_zeros(body, options.dalc_size);
    fo4::test::write_subrecord(out, "DALC", body.bytes);
  }
  if (options.unam_size > 0) {
    fo4::test::ByteWriter body;
    write_zeros(body, static_cast<std::size_t>(options.unam_size));
    fo4::test::write_subrecord(out, "UNAM", body.bytes);
  }
  {
    fo4::test::ByteWriter body;
    body.f32(1.0f);
    fo4::test::write_subrecord(out, "VNAM", body.bytes);
    fo4::test::write_subrecord(out, "WNAM", body.bytes);
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

std::vector<std::byte> make_weather_plugin(const WeatherPluginOptions& options = {}) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x801);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  for (const auto& master : options.masters) {
    write_master(out, master);
  }
  fo4::test::end_record(out, tes4);

  if (options.include_local_spgd) {
    write_minimal_spgd(out, options.spgd_form_id);
  }
  if (options.include_local_misc) {
    write_minimal_misc(out, options.misc_form_id);
  }
  write_weather_record(out, options);
  return out.bytes;
}

std::vector<std::byte> make_spgd_only_plugin(std::uint32_t form_id = 0x00000800) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x801);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  write_minimal_spgd(out, form_id);
  return out.bytes;
}

fo4::Record* find_mutable_record(fo4::Plugin& plugin, const fo4::FormKey& key) {
  fo4::Record* found = nullptr;
  fo4::visit_records(plugin, [&](fo4::Record& record) {
    if (found == nullptr && record.form_key == key) found = &record;
  });
  return found;
}


template <class Configure>
WeatherPluginOptions make_options(Configure configure) {
  WeatherPluginOptions options;
  configure(options);
  return options;
}

struct WeatherLayoutCase {
  const char* name;
  WeatherPluginOptions options;
  std::uint32_t expected_cloud_layers;
  std::uint8_t expected_imsp_count;
  std::size_t expected_data_size;
  std::size_t expected_dalc_count;
  bool expected_has_nam4;
  bool expected_has_unam;
  std::size_t expected_fnam_size;
  std::size_t expected_nam0_size;
};

std::ostream& operator<<(std::ostream& out, const WeatherLayoutCase& value) {
  return out << value.name;
}

void legacy_layout(WeatherPluginOptions& o) {
  o.form_version = 110;
  o.pnam_jnam_size = 512;
  o.data_size = 19;
  o.fnam_size = 32;
  o.nam0_size = 272;
  o.imsp_size = 16;
  o.dalc_count = 4;
  o.unam_size = 12;
}

const std::array<WeatherLayoutCase, 10> kWeatherLayoutCases{{
    {"default", {}, 32u, 8u, 20u, 8u, true, true, 72u, 608u},
    {"imsp16", make_options(legacy_layout), 32u, 4u, 19u, 4u, true, true, 32u, 272u},
    {"pre119", make_options([](WeatherPluginOptions& o) {
       o.form_version = 118;
       o.data_size = 19;
       o.fnam_size = 32;
       o.nam0_size = 544;
       o.unam_size = 12;
     }),
     32u, 8u, 19u, 8u, true, true, 32u, 544u},
    {"dalc4", make_options(legacy_layout), 32u, 4u, 19u, 4u, true, true, 32u, 272u},
    {"noNam4", make_options([](WeatherPluginOptions& o) { o.include_nam4 = false; }), 32u, 8u, 20u, 8u, false, true, 72u, 608u},
    {"unam12", make_options([](WeatherPluginOptions& o) {
       o.form_version = 129;
       o.unam_size = 12;
     }),
     32u, 8u, 20u, 8u, true, true, 72u, 608u},
    {"noUnam", make_options([](WeatherPluginOptions& o) { o.unam_size = 0; }), 32u, 8u, 20u, 8u, true, false, 72u, 608u},
    {"v119", make_options([](WeatherPluginOptions& o) {
       o.form_version = 119;
       o.fnam_size = 56;
       o.unam_size = 12;
     }),
     32u, 8u, 20u, 8u, true, true, 56u, 608u},
    {"v110", make_options(legacy_layout), 32u, 4u, 19u, 4u, true, true, 32u, 272u},
    {"full",
     make_options([](WeatherPluginOptions& o) {
       legacy_layout(o);
       o.include_nam4 = false;
     }),
     32u, 4u, 19u, 4u, false, true, 32u, 272u},
}};

std::vector<std::filesystem::path> corpus_plugin_paths(const char* dir) {
  static constexpr const char* kNames[] = {
      "Fallout4.esm",      "DLCRobot.esm",      "DLCworkshop01.esm", "DLCworkshop02.esm",
      "DLCworkshop03.esm", "DLCCoast.esm",      "DLCNukaWorld.esm",
  };
  std::vector<std::filesystem::path> out;
  for (const char* name : kNames) {
    const auto path = std::filesystem::path{dir} / name;
    if (std::filesystem::exists(path)) out.push_back(path);
  }
  return out;
}

}

TEST(WeatherConcept, DecodesProvenCloudAndLinkSlots) {
  auto plugin = fo4::read_plugin(make_weather_plugin(), "WeatherPatch.esp");
  const auto key = fo4::FormKey::parse("000800:WeatherPatch.esp");
  const auto model = fo4::forms::decode_weather(plugin, key);

  ASSERT_TRUE(model.editor_id.has_value());
  EXPECT_EQ(*model.editor_id, "TestWeather");
  ASSERT_TRUE(model.cloud_textures[0].has_value());
  EXPECT_EQ(*model.cloud_textures[0], "Textures\\Sky\\Clouds\\Test.dds");
  EXPECT_EQ(model.cloud_color_layer_count, 32u);
  EXPECT_FALSE(model.mnam.has_value());
  EXPECT_FALSE(model.nnam.has_value());
  ASSERT_EQ(model.snam.size(), 1u);
  ASSERT_TRUE(model.snam[0].sound.has_value());
  EXPECT_EQ(model.snam[0].sound->to_string(), "000810:WeatherPatch.esp");
  ASSERT_EQ(model.tnam.size(), 1u);
  EXPECT_EQ(model.tnam[0]->to_string(), "000820:WeatherPatch.esp");
  EXPECT_EQ(model.imsp_count, 8);
  EXPECT_TRUE(model.has_wgdr);
  EXPECT_EQ(model.dalc.size(), 8u);
  EXPECT_TRUE(model.has_unam);
  ASSERT_TRUE(model.vnam.has_value());
  EXPECT_FLOAT_EQ(*model.vnam, 1.0f);
}

TEST(WeatherConcept, RejectsMalformedPnamSize) {
  auto bytes = make_weather_plugin();
  auto plugin = fo4::read_plugin(bytes, "WeatherPatch.esp");
  const auto key = fo4::FormKey::parse("000800:WeatherPatch.esp");
  auto* found = find_mutable_record(plugin, key);
  ASSERT_NE(found, nullptr);
  for (auto& subrecord : found->subrecords) {
    if (subrecord.signature == fo4::Signature::from_string("PNAM")) {
      subrecord.data.resize(100);
    }
  }
  EXPECT_THROW(fo4::forms::decode_weather(plugin, key), fo4::forms::WeatherError);
}

TEST(WeatherConcept, CloudSpeedsPreserveUnsignedStoredBytes) {
  auto plugin = fo4::read_plugin(make_weather_plugin(), "WeatherPatch.esp");
  const auto key = fo4::FormKey::parse("000800:WeatherPatch.esp");
  auto* record = find_mutable_record(plugin, key);
  ASSERT_NE(record, nullptr);
  constexpr std::array<std::uint8_t, 5> values{0, 127, 128, 254, 255};
  for (auto& sub : record->subrecords) {
    if (sub.signature == fo4::Signature::from_string("RNAM") ||
        sub.signature == fo4::Signature::from_string("QNAM")) {
      for (std::size_t i = 0; i < sub.data.size(); ++i) {
        sub.data[i] = std::byte{values[i % values.size()]};
      }
    }
  }
  const auto model = fo4::forms::decode_weather(plugin, key);
  for (std::size_t i = 0; i < fo4::forms::k_weather_cloud_layer_slots; ++i) {
    SCOPED_TRACE(i);
    EXPECT_EQ(model.cloud_speed_y[i], values[i % values.size()]);
    EXPECT_EQ(model.cloud_speed_x[i], values[i % values.size()]);
  }
}

class WeatherLayoutFamily : public testing::TestWithParam<WeatherLayoutCase> {};

TEST_P(WeatherLayoutFamily, LayoutFamiliesDecode) {
  const auto& param = GetParam();
  auto plugin = fo4::read_plugin(make_weather_plugin(param.options), "WeatherPatch.esp");
  const auto key = fo4::FormKey::parse("000800:WeatherPatch.esp");
  const auto model = fo4::forms::decode_weather(plugin, key);

  EXPECT_EQ(model.cloud_color_layer_count, param.expected_cloud_layers);
  EXPECT_EQ(model.imsp_count, param.expected_imsp_count);
  EXPECT_EQ(model.data.size(), param.expected_data_size);
  EXPECT_EQ(model.dalc.size(), param.expected_dalc_count);
  EXPECT_EQ(!model.nam4.empty(), param.expected_has_nam4);
  EXPECT_EQ(model.has_unam, param.expected_has_unam);
  EXPECT_EQ(model.fnam.size(), param.expected_fnam_size);
  EXPECT_EQ(model.nam0.size(), param.expected_nam0_size);
}

INSTANTIATE_TEST_SUITE_P(WeatherConcept, WeatherLayoutFamily, testing::ValuesIn(kWeatherLayoutCases));

TEST(WeatherConcept, MnamNullAccepted) {
  WeatherPluginOptions options;
  options.include_local_spgd = true;
  auto plugin = fo4::read_plugin(make_weather_plugin(options), "WeatherPatch.esp");
  const auto key = fo4::FormKey::parse("000800:WeatherPatch.esp");
  const auto spgd = fo4::FormKey::parse("000830:WeatherPatch.esp");
  ASSERT_TRUE(fo4::forms::set_weather_mnam(plugin, key, spgd));
  EXPECT_TRUE(fo4::forms::set_weather_mnam(plugin, key, std::nullopt));
  const auto model = fo4::forms::decode_weather(plugin, key);
  EXPECT_FALSE(model.mnam.has_value());
}

TEST(WeatherConcept, NullSkyStaticTnamDecodesAsAbsent) {
  WeatherPluginOptions options;
  options.tnam_form_id = 0;
  auto plugin = fo4::read_plugin(make_weather_plugin(options), "WeatherPatch.esp");
  const auto key = fo4::FormKey::parse("000800:WeatherPatch.esp");
  const auto model = fo4::forms::decode_weather(plugin, key);
  ASSERT_EQ(model.tnam.size(), 1u);
  EXPECT_FALSE(model.tnam[0].has_value());
}

TEST(WeatherConcept, DalcDecodesSpecularAndFresnelOnlyWhenPresent) {
  const auto key = fo4::FormKey::parse("000800:WeatherPatch.esp");
  const struct {
    std::size_t size;
    bool specular;
    bool fresnel;
  } cases[] = {{24, false, false}, {28, true, false}, {32, true, true}};
  for (const auto& c : cases) {
    SCOPED_TRACE(c.size);
    WeatherPluginOptions options;
    options.dalc_size = c.size;
    auto plugin = fo4::read_plugin(make_weather_plugin(options), "WeatherPatch.esp");
    const auto model = fo4::forms::decode_weather(plugin, key);
    ASSERT_EQ(model.dalc.size(), 8u);
    EXPECT_EQ(model.dalc[0].specular.has_value(), c.specular);
    EXPECT_EQ(model.dalc[0].fresnel.has_value(), c.fresnel);
  }

  WeatherPluginOptions options;
  options.dalc_size = 20;
  auto plugin = fo4::read_plugin(make_weather_plugin(options), "WeatherPatch.esp");
  EXPECT_THROW(fo4::forms::decode_weather(plugin, key), fo4::forms::WeatherError);
}

TEST(WeatherConcept, MnamRejectsNonSpgd) {
  WeatherPluginOptions options;
  options.include_local_misc = true;
  auto plugin = fo4::read_plugin(make_weather_plugin(options), "WeatherPatch.esp");
  const auto key = fo4::FormKey::parse("000800:WeatherPatch.esp");
  const auto misc = fo4::FormKey::parse("000840:WeatherPatch.esp");
  EXPECT_THROW(fo4::forms::set_weather_mnam(plugin, key, misc), fo4::forms::WeatherError);
}

TEST(WeatherConcept, MnamRejectsUnresolvedExternal) {
  auto plugin = fo4::read_plugin(make_weather_plugin(), "WeatherPatch.esp");
  const auto key = fo4::FormKey::parse("000800:WeatherPatch.esp");
  const auto external = fo4::FormKey::parse("000800:FalloutSpgd.esm");
  EXPECT_THROW(fo4::forms::set_weather_mnam(plugin, key, external), fo4::forms::WeatherError);
}

TEST(WeatherConcept, MnamSamePluginSpgd) {
  WeatherPluginOptions options;
  options.include_local_spgd = true;
  auto plugin = fo4::read_plugin(make_weather_plugin(options), "WeatherPatch.esp");
  const auto key = fo4::FormKey::parse("000800:WeatherPatch.esp");
  const auto spgd = fo4::FormKey::parse("000830:WeatherPatch.esp");
  EXPECT_TRUE(fo4::forms::set_weather_mnam(plugin, key, spgd));
  const auto model = fo4::forms::decode_weather(plugin, key);
  ASSERT_TRUE(model.mnam.has_value());
  EXPECT_EQ(model.mnam->to_string(), "000830:WeatherPatch.esp");
}

TEST(WeatherConcept, MnamExistingMasterSpgd) {
  WeatherPluginOptions options;
  options.masters = {"FalloutSpgd.esm"};
  options.weather_form_id = 0x01000800;
  options.snam_form_id = 0x01000810;
  options.tnam_form_id = 0x01000820;
  auto plugin = fo4::read_plugin(make_weather_plugin(options), "WeatherPatch.esp");
  const auto spgd_plugin = fo4::read_plugin(make_spgd_only_plugin(0x00000100), "FalloutSpgd.esm");
  const fo4::Plugin* environment[] = {&spgd_plugin};
  const auto key = fo4::FormKey::parse("000800:WeatherPatch.esp");
  const auto target = fo4::FormKey::parse("000100:FalloutSpgd.esm");
  EXPECT_TRUE(fo4::forms::set_weather_mnam(plugin, key, target, environment));
  const auto model = fo4::forms::decode_weather(plugin, key);
  ASSERT_TRUE(model.mnam.has_value());
  EXPECT_EQ(model.mnam->to_string(), "000100:FalloutSpgd.esm");
  ASSERT_EQ(plugin.header.masters.size(), 1u);
  EXPECT_EQ(plugin.header.masters[0].key.filename(), std::string_view{"FalloutSpgd.esm"});
}

TEST(WeatherConcept, MnamExternalMasterInsertionRemaps) {
  auto plugin = fo4::read_plugin(make_weather_plugin(), "WeatherPatch.esp");
  const auto key = fo4::FormKey::parse("000800:WeatherPatch.esp");
  const auto before = fo4::forms::decode_weather(plugin, key);
  ASSERT_FALSE(before.snam.empty());
  ASSERT_FALSE(before.tnam.empty());
  ASSERT_TRUE(before.snam[0].sound.has_value());
  const auto snam_before = before.snam[0].sound->to_string();
  const auto tnam_before = before.tnam[0]->to_string();

  const auto spgd_plugin = fo4::read_plugin(make_spgd_only_plugin(0x00000100), "FalloutSpgd.esm");
  const fo4::Plugin* environment[] = {&spgd_plugin};
  const auto target = fo4::FormKey::parse("000100:FalloutSpgd.esm");
  EXPECT_TRUE(fo4::forms::set_weather_mnam(plugin, key, target, environment));

  ASSERT_EQ(plugin.header.masters.size(), 1u);
  EXPECT_EQ(plugin.header.masters[0].key.filename(), std::string_view{"FalloutSpgd.esm"});
  const auto after = fo4::forms::decode_weather(plugin, key);
  ASSERT_TRUE(after.mnam.has_value());
  EXPECT_EQ(after.mnam->to_string(), "000100:FalloutSpgd.esm");
  ASSERT_FALSE(after.snam.empty());
  ASSERT_FALSE(after.tnam.empty());
  ASSERT_TRUE(after.snam[0].sound.has_value());
  EXPECT_EQ(after.snam[0].sound->to_string(), snam_before);
  EXPECT_EQ(after.tnam[0]->to_string(), tnam_before);
  EXPECT_FLOAT_EQ(after.vnam.value_or(-1.0f), 1.0f);
  EXPECT_FLOAT_EQ(after.wnam.value_or(-1.0f), 1.0f);

  const auto bytes = fo4::write_plugin(plugin);
  const auto reloaded = fo4::read_plugin(bytes, "WeatherPatch.esp");
  const auto roundtrip = fo4::forms::decode_weather(reloaded, key);
  ASSERT_TRUE(roundtrip.mnam.has_value());
  EXPECT_EQ(roundtrip.mnam->to_string(), "000100:FalloutSpgd.esm");
  ASSERT_FALSE(roundtrip.snam.empty());
  ASSERT_TRUE(roundtrip.snam[0].sound.has_value());
  EXPECT_EQ(roundtrip.snam[0].sound->to_string(), snam_before);
}

TEST(WeatherConcept, MnamExtensionOnlyLightMasterInsertion) {
  auto plugin = fo4::read_plugin(make_weather_plugin(), "WeatherPatch.esp");
  const auto key = fo4::FormKey::parse("000800:WeatherPatch.esp");
  const auto spgd_plugin = fo4::read_plugin(make_spgd_only_plugin(0x00000100), "FalloutSpgd.ESL");
  ASSERT_FALSE(spgd_plugin.header.tes4_flags.is_small_file());
  const fo4::Plugin* environment[] = {&spgd_plugin};
  const auto target = fo4::FormKey::parse("000100:FalloutSpgd.ESL");

  EXPECT_TRUE(fo4::forms::set_weather_mnam(plugin, key, target, environment));
  ASSERT_EQ(plugin.header.masters.size(), 1u);
  EXPECT_EQ(plugin.header.masters[0].kind, fo4::PluginKind::light);
  const auto model = fo4::forms::decode_weather(plugin, key);
  ASSERT_TRUE(model.mnam.has_value());
  EXPECT_EQ(*model.mnam, target);
  EXPECT_FALSE(spgd_plugin.header.tes4_flags.is_small_file());
}

TEST(WeatherConcept, MnamNoOpWhenUnchanged) {
  WeatherPluginOptions options;
  options.include_local_spgd = true;
  auto plugin = fo4::read_plugin(make_weather_plugin(options), "WeatherPatch.esp");
  const auto key = fo4::FormKey::parse("000800:WeatherPatch.esp");
  const auto spgd = fo4::FormKey::parse("000830:WeatherPatch.esp");
  EXPECT_FALSE(fo4::forms::set_weather_mnam(plugin, key, std::nullopt));
  EXPECT_TRUE(fo4::forms::set_weather_mnam(plugin, key, spgd));
  EXPECT_FALSE(fo4::forms::set_weather_mnam(plugin, key, spgd));
  EXPECT_TRUE(fo4::forms::set_weather_mnam(plugin, key, std::nullopt));
  EXPECT_FALSE(fo4::forms::set_weather_mnam(plugin, key, std::nullopt));
}

TEST(WeatherConcept, MnamMissingOrDuplicateFailsClosed) {
  auto plugin = fo4::read_plugin(make_weather_plugin(), "WeatherPatch.esp");
  const auto key = fo4::FormKey::parse("000800:WeatherPatch.esp");
  auto* record = find_mutable_record(plugin, key);
  ASSERT_NE(record, nullptr);
  record->subrecords.erase(
      std::remove_if(record->subrecords.begin(), record->subrecords.end(),
                     [](const fo4::Subrecord& sub) {
                       return sub.signature == fo4::Signature::from_string("MNAM");
                     }),
      record->subrecords.end());
  EXPECT_THROW(fo4::forms::set_weather_mnam(plugin, key, std::nullopt), fo4::forms::WeatherError);

  plugin = fo4::read_plugin(make_weather_plugin(), "WeatherPatch.esp");
  record = find_mutable_record(plugin, key);
  ASSERT_NE(record, nullptr);
  fo4::Subrecord duplicate;
  duplicate.signature = fo4::Signature::from_string("MNAM");
  duplicate.data = {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}};
  record->subrecords.push_back(duplicate);
  EXPECT_THROW(fo4::forms::set_weather_mnam(plugin, key, std::nullopt), fo4::forms::WeatherError);
}


TEST(WeatherConcept, MnamExternalMasterMissingIsTransactional) {
  auto plugin = fo4::read_plugin(make_weather_plugin(), "WeatherPatch.esp");
  const auto key = fo4::FormKey::parse("000800:WeatherPatch.esp");
  auto* record = find_mutable_record(plugin, key);
  ASSERT_NE(record, nullptr);
  record->subrecords.erase(
      std::remove_if(record->subrecords.begin(), record->subrecords.end(),
                     [](const fo4::Subrecord& sub) {
                       return sub.signature == fo4::Signature::from_string("MNAM");
                     }),
      record->subrecords.end());
  const auto before_bytes = fo4::write_plugin(plugin);
  const auto before_masters = plugin.header.masters.size();

  const auto spgd_plugin = fo4::read_plugin(make_spgd_only_plugin(0x00000100), "FalloutSpgd.esm");
  const fo4::Plugin* environment[] = {&spgd_plugin};
  const auto target = fo4::FormKey::parse("000100:FalloutSpgd.esm");
  EXPECT_THROW(fo4::forms::set_weather_mnam(plugin, key, target, environment), fo4::forms::WeatherError);
  EXPECT_EQ(plugin.header.masters.size(), before_masters);
  EXPECT_EQ(fo4::write_plugin(plugin), before_bytes);
}

TEST(WeatherConcept, MnamExternalMasterDuplicateIsTransactional) {
  auto plugin = fo4::read_plugin(make_weather_plugin(), "WeatherPatch.esp");
  const auto key = fo4::FormKey::parse("000800:WeatherPatch.esp");
  auto* record = find_mutable_record(plugin, key);
  ASSERT_NE(record, nullptr);
  fo4::Subrecord duplicate;
  duplicate.signature = fo4::Signature::from_string("MNAM");
  duplicate.data = {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}};
  record->subrecords.push_back(duplicate);
  const auto before_bytes = fo4::write_plugin(plugin);
  const auto before_masters = plugin.header.masters.size();

  const auto spgd_plugin = fo4::read_plugin(make_spgd_only_plugin(0x00000100), "FalloutSpgd.esm");
  const fo4::Plugin* environment[] = {&spgd_plugin};
  const auto target = fo4::FormKey::parse("000100:FalloutSpgd.esm");
  EXPECT_THROW(fo4::forms::set_weather_mnam(plugin, key, target, environment), fo4::forms::WeatherError);
  EXPECT_EQ(plugin.header.masters.size(), before_masters);
  EXPECT_EQ(fo4::write_plugin(plugin), before_bytes);
}

TEST(WeatherConcept, MnamExternalMasterMalformedIsTransactional) {
  auto plugin = fo4::read_plugin(make_weather_plugin(), "WeatherPatch.esp");
  const auto key = fo4::FormKey::parse("000800:WeatherPatch.esp");
  auto* record = find_mutable_record(plugin, key);
  ASSERT_NE(record, nullptr);
  for (auto& subrecord : record->subrecords) {
    if (subrecord.signature == fo4::Signature::from_string("MNAM")) {
      subrecord.data = {std::byte{0}, std::byte{0}};
      break;
    }
  }
  const auto before_bytes = fo4::write_plugin(plugin);
  const auto before_masters = plugin.header.masters.size();

  const auto spgd_plugin = fo4::read_plugin(make_spgd_only_plugin(0x00000100), "FalloutSpgd.esm");
  const fo4::Plugin* environment[] = {&spgd_plugin};
  const auto target = fo4::FormKey::parse("000100:FalloutSpgd.esm");
  EXPECT_THROW(fo4::forms::set_weather_mnam(plugin, key, target, environment), fo4::forms::WeatherError);
  EXPECT_EQ(plugin.header.masters.size(), before_masters);
  EXPECT_EQ(fo4::write_plugin(plugin), before_bytes);
}

TEST(WeatherConcept, MnamExternalMasterMissingRecordIsTransactional) {
  auto plugin = fo4::read_plugin(make_weather_plugin(), "WeatherPatch.esp");
  const auto before_bytes = fo4::write_plugin(plugin);
  const auto before_masters = plugin.header.masters.size();
  const auto missing = fo4::FormKey::parse("000801:WeatherPatch.esp");

  const auto spgd_plugin = fo4::read_plugin(make_spgd_only_plugin(0x00000100), "FalloutSpgd.esm");
  const fo4::Plugin* environment[] = {&spgd_plugin};
  const auto target = fo4::FormKey::parse("000100:FalloutSpgd.esm");
  EXPECT_THROW(fo4::forms::set_weather_mnam(plugin, missing, target, environment), fo4::forms::WeatherError);
  EXPECT_EQ(plugin.header.masters.size(), before_masters);
  EXPECT_EQ(fo4::write_plugin(plugin), before_bytes);
}


TEST(WeatherConcept, UntouchedBytePreservation) {
  WeatherPluginOptions options;
  options.include_local_spgd = true;
  auto plugin = fo4::read_plugin(make_weather_plugin(options), "WeatherPatch.esp");
  const auto key = fo4::FormKey::parse("000800:WeatherPatch.esp");
  auto* before_record = find_mutable_record(plugin, key);
  ASSERT_NE(before_record, nullptr);
  std::vector<std::pair<fo4::Signature, std::vector<std::byte>>> before_subs;
  before_subs.reserve(before_record->subrecords.size());
  for (const auto& subrecord : before_record->subrecords) {
    before_subs.emplace_back(subrecord.signature, subrecord.data);
  }
  const auto target = fo4::FormKey::parse("000830:WeatherPatch.esp");
  EXPECT_TRUE(fo4::forms::set_weather_mnam(plugin, key, target));
  const auto after_model = fo4::forms::decode_weather(plugin, key);
  ASSERT_TRUE(after_model.mnam.has_value());
  EXPECT_EQ(after_model.mnam->to_string(), "000830:WeatherPatch.esp");
  auto* after_record = find_mutable_record(plugin, key);
  ASSERT_NE(after_record, nullptr);
  ASSERT_EQ(before_subs.size(), after_record->subrecords.size());
  std::size_t changed_subs = 0;
  for (std::size_t i = 0; i < before_subs.size(); ++i) {
    EXPECT_EQ(before_subs[i].first, after_record->subrecords[i].signature);
    if (before_subs[i].second != after_record->subrecords[i].data) {
      ++changed_subs;
      EXPECT_EQ(after_record->subrecords[i].signature, fo4::Signature::from_string("MNAM"));
      ASSERT_EQ(after_record->subrecords[i].data.size(), 4u);
    }
  }
  EXPECT_EQ(changed_subs, 1u);
  const auto after = fo4::write_plugin(plugin);
  auto reloaded = fo4::read_plugin(after, "WeatherPatch.esp");
  const auto round_trip = fo4::forms::decode_weather(reloaded, key);
  ASSERT_TRUE(round_trip.mnam.has_value());
  EXPECT_EQ(round_trip.mnam->to_string(), "000830:WeatherPatch.esp");
  EXPECT_EQ(round_trip.cloud_textures[0], after_model.cloud_textures[0]);
  EXPECT_EQ(round_trip.tnam.size(), after_model.tnam.size());
}

TEST(WeatherConcept, CorpusDecodeSweepWhenPresent) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) {
    GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  }
  const auto files = corpus_plugin_paths(dir);
  if (files.empty()) {
    GTEST_SKIP() << "no corpus plugins found under " << dir;
  }
  const auto build = detect_game_build(dir);
  record_game_build(build);
  std::size_t seen = 0;
  std::size_t failed = 0;
  std::vector<std::string> failures;
  const auto fallout_weather_key = fo4::FormKey::parse("0000015E:Fallout4.esm");
  bool fallout_weather_checked = false;
  for (const auto& path : files) {
    const auto plugin = fo4::read_plugin_file(path);
    for (const auto* record : fo4::collect_records(plugin)) {
      if (record->header.signature != fo4::Signature::from_string("WTHR")) continue;
      ++seen;
      try {
        const auto model = fo4::forms::decode_weather(plugin, record->form_key);
        if (record->form_key == fallout_weather_key) {
          fallout_weather_checked = true;
          EXPECT_TRUE(model.editor_id.has_value());
          EXPECT_EQ(model.cloud_color_layer_count, 32u);
          EXPECT_TRUE(model.data.size() == 19 || model.data.size() == 20);
          EXPECT_TRUE(model.dalc.size() == 4u || model.dalc.size() == 8u);
        }
      } catch (const std::exception& error) {
        ++failed;
        failures.push_back(path.filename().string() + " " + record->form_key.to_string() + ": " +
                           error.what());
      }
    }
  }
  for (std::size_t i = 0; i < failures.size() && i < 20; ++i) {
    ADD_FAILURE() << failures[i];
  }
  if (std::any_of(files.begin(), files.end(), [](const auto& file) { return file.filename() == "Fallout4.esm"; })) {
    EXPECT_TRUE(fallout_weather_checked);
  }
  EXPECT_EQ(failed, 0u) << seen << " WTHR records scanned, " << failed << " failed to decode";
  RecordProperty("WTHR_scanned", static_cast<int>(seen));
  if (files.size() == 7 && exact_counts_known(build)) { EXPECT_EQ(seen, 107u); }
  if (seen == 0) {
    GTEST_SKIP() << "no WTHR records found in available corpus plugins";
  }
}
