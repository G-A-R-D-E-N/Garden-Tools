#include "fo4/c_api_vegetation.h"
#include "fo4/binary/cursor.hpp"
#include "fo4/environment/vegetation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <map>
#include <sstream>

namespace {

class VegetationReal : public testing::TestWithParam<const char*> {};

constexpr const char* masters[] = {"Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm", "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"};

void expect_key(const std::optional<fo4::FormKey>& native, radicle_form_key value, const char* field) {
  SCOPED_TRACE(field);
  const bool present = native && !native->owner().empty();
  EXPECT_EQ(value.plugin != nullptr, present);
  EXPECT_EQ(value.local_id, present ? native->local().value() : 0u);
  if (present) {
    ASSERT_NE(value.plugin, nullptr);
    EXPECT_EQ(fo4::PluginKey::from_filename(value.plugin), native->owner());
  }
}

void expect_harvest(const fo4::env::HarvestData& native, const radicle_harvest_info& value,
                    const fo4::Record& record) {
  expect_key(native.ingredient, value.ingredient, "PFIG"); expect_key(native.sound, value.sound, "SNAM");
  EXPECT_EQ(value.has_seasons, native.seasons.has_value() ? 1 : 0);
  if (native.seasons) {
    for (std::size_t j = 0; j < 4; ++j) EXPECT_EQ(value.seasons[j], (*native.seasons)[j]);
  }
  for (const auto& sub : record.subrecords) {
    if (sub.signature.to_string() != "PFPC") continue;
    ASSERT_EQ(sub.data.size(), 4u);
    EXPECT_EQ(value.has_seasons, 1);
    for (std::size_t j = 0; j < 4; ++j) EXPECT_EQ(value.seasons[j], std::to_integer<std::uint8_t>(sub.data[j]));
  }
}

std::uint32_t raw_le(const std::vector<std::byte>& data, std::size_t offset, std::size_t width) {
  std::uint32_t out = 0;
  for (std::size_t i = 0; i < width && offset + i < data.size(); ++i)
    out |= std::to_integer<std::uint32_t>(data[offset + i]) << (8 * i);
  return out;
}

TEST_P(VegetationReal, EveryOfficialVegetationRecordDecodesAndMasterIsByteIdentical) {
  const auto* dir = std::getenv("FO4_DATA_DIR");
  if (!dir) GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data";
  const auto path = std::filesystem::path{dir} / GetParam();
  if (!std::filesystem::exists(path)) GTEST_SKIP() << "missing official master: " << path;
  std::vector<std::filesystem::path> native_paths; std::vector<std::string> names;
  for (const auto& master : fo4::read_plugin_file(path).header.masters)
    native_paths.emplace_back(std::filesystem::path{dir} / master.key.filename());
  native_paths.push_back(path);
  for (const auto& dependency : native_paths) {
    if (!std::filesystem::exists(dependency)) GTEST_SKIP() << "missing official master: " << dependency;
    names.push_back(dependency.string());
  }
  const auto env = fo4::env::Environment::load(native_paths);
  std::vector<const char*> paths; for (const auto& name : names) paths.push_back(name.c_str());
  radicle_env* abi = nullptr;
  ASSERT_EQ(radicle_env_load(paths.data(), paths.size(), &abi), RADICLE_STATUS_OK);
  const auto plugin_index = native_paths.size() - 1;
  const radicle_record** records = nullptr; std::size_t count = 0;
  ASSERT_EQ(radicle_env_plugin_records(abi, plugin_index, &records, &count), RADICLE_STATUS_OK);
  const auto refs = env.plugin_records(plugin_index); ASSERT_EQ(count, refs.size()); std::map<std::string, int> totals;
  for (std::size_t i = 0; i < count; ++i) {
    const auto type = refs[i]->record->header.signature.to_string();
    if (type != "GRAS" && type != "TREE" && type != "FLOR" && type != "LTEX") continue;
    const auto& record = *refs[i]->record;
    std::ostringstream trace; trace << GetParam() << ' ' << type << ' ' << record.form_key.to_string();
    for (const auto& sub : record.subrecords) {
      trace << ' ' << sub.signature.to_string() << '[' << std::dec << sub.data.size() << "]=";
      for (const auto byte : sub.data) trace << std::hex << std::setfill('0') << std::setw(2) << std::to_integer<unsigned>(byte);
    }
    SCOPED_TRACE(trace.str()); ++totals[type];
    if (type == "GRAS") {
      std::optional<fo4::env::GrassData> native; radicle_grass_info value{};
      ASSERT_NO_THROW(native = fo4::env::decode_grass(env, *refs[i]));
      ASSERT_EQ(radicle_grass_get_info(abi, records[i], &value), RADICLE_STATUS_OK);
      EXPECT_EQ(value.has_data, native.has_value() ? 1 : 0);
      for (const auto& sub : record.subrecords) {
        if (sub.signature.to_string() != "DATA") continue;
        const auto& bytes = sub.data; EXPECT_EQ(value.has_data, 1);
        EXPECT_EQ(value.density, raw_le(bytes, 0, 1)); EXPECT_EQ(value.min_slope, raw_le(bytes, 1, 1));
        EXPECT_EQ(value.max_slope, raw_le(bytes, 2, 1)); EXPECT_EQ(value.units_from_water, raw_le(bytes, 4, 2));
        EXPECT_EQ(value.water_type, raw_le(bytes, 8, 4)); EXPECT_EQ(value.flags, raw_le(bytes, 28, 1));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(value.position_range), raw_le(bytes, 12, 4));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(value.height_range), raw_le(bytes, 16, 4));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(value.color_range), raw_le(bytes, 20, 4));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(value.wave_period), raw_le(bytes, 24, 4));
      }
      if (native) {
        EXPECT_EQ(value.density, native->density); EXPECT_EQ(value.min_slope, native->min_slope);
        EXPECT_EQ(value.max_slope, native->max_slope); EXPECT_EQ(value.units_from_water, native->units_from_water);
        EXPECT_EQ(value.water_type, native->water_type); EXPECT_EQ(value.flags, native->flags);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(value.position_range), std::bit_cast<std::uint32_t>(native->position_range));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(value.height_range), std::bit_cast<std::uint32_t>(native->height_range));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(value.color_range), std::bit_cast<std::uint32_t>(native->color_range));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(value.wave_period), std::bit_cast<std::uint32_t>(native->wave_period));
      }
    } else if (type == "TREE") {
      fo4::env::TreeData native; radicle_tree_info value{};
      ASSERT_NO_THROW(native = fo4::env::decode_tree(env, *refs[i]));
      ASSERT_EQ(radicle_tree_get_info(abi, records[i], &value), RADICLE_STATUS_OK);
      expect_harvest(native.harvest, value.harvest, record);
      EXPECT_EQ(value.has_wind, native.wind.has_value() ? 1 : 0);
      if (native.wind) for (std::size_t j = 0; j < 12; ++j) {
        SCOPED_TRACE("wind index " + std::to_string(j));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(value.wind[j]), std::bit_cast<std::uint32_t>((*native.wind)[j]));
      }
      for (const auto& sub : record.subrecords) {
        if (sub.signature.to_string() != "CNAM" || sub.data.size() != 48) continue;
        EXPECT_EQ(value.has_wind, 1); fo4::binary::Cursor raw{sub.data};
        for (std::size_t j = 0; j < 12; ++j) {
          SCOPED_TRACE("CNAM index " + std::to_string(j));
          EXPECT_EQ(std::bit_cast<std::uint32_t>(value.wind[j]), raw.read_u32());
        }
      }
    } else if (type == "FLOR") {
      fo4::env::FloraData native; radicle_flora_info value{};
      ASSERT_NO_THROW(native = fo4::env::decode_flora(env, *refs[i]));
      ASSERT_EQ(radicle_flora_get_info(abi, records[i], &value), RADICLE_STATUS_OK);
      expect_harvest(native.harvest, value.harvest, record);
      EXPECT_EQ(value.has_flags, native.flags.has_value() ? 1 : 0);
      if (native.flags) { EXPECT_EQ(value.flags, *native.flags); }
      for (const auto& sub : record.subrecords) {
        if (sub.signature.to_string() != "FNAM") continue;
        ASSERT_EQ(sub.data.size(), 2u); EXPECT_EQ(value.has_flags, 1);
        EXPECT_EQ(value.flags, fo4::binary::Cursor{sub.data}.read_u16());
      }
    } else {
      std::vector<fo4::FormKey> native; const radicle_grass_links* value = nullptr;
      ASSERT_NO_THROW(native = fo4::env::decode_landscape_grasses(env, *refs[i]));
      ASSERT_EQ(radicle_landscape_grasses_decode(abi, records[i], &value), RADICLE_STATUS_OK);
      ASSERT_NE(value, nullptr);
      const auto gnam_count = static_cast<std::size_t>(std::count_if(record.subrecords.begin(), record.subrecords.end(),
          [](const auto& sub) { return sub.signature.to_string() == "GNAM"; }));
      ASSERT_EQ(value->grass_count, gnam_count); ASSERT_EQ(native.size(), gnam_count);
      std::size_t j = 0;
      for (const auto& sub : record.subrecords) {
        if (sub.signature.to_string() != "GNAM") continue;
        SCOPED_TRACE("GNAM index " + std::to_string(j)); ASSERT_EQ(sub.data.size(), 4u);
        const auto key = env.resolve_link(*refs[i], fo4::RawFormId{fo4::binary::Cursor{sub.data}.read_u32()});
        EXPECT_EQ(native[j], key); expect_key(key, value->grasses[j], "GNAM");
        ASSERT_NE(value->grasses[j].plugin, nullptr);
        const auto* resolved = env.winner(key); ASSERT_NE(resolved, nullptr);
        EXPECT_EQ(resolved->record->header.signature.to_string(), "GRAS");
        const radicle_record* target = nullptr;
        ASSERT_EQ(radicle_env_winner(abi, value->grasses[j], &target), RADICLE_STATUS_OK);
        ASSERT_NE(target, nullptr); radicle_record_info info{};
        ASSERT_EQ(radicle_record_get_info(abi, target, &info), RADICLE_STATUS_OK); EXPECT_STREQ(info.signature, "GRAS");
        ++j;
      }
      radicle_landscape_grasses_free(value);
    }
    if (HasFailure()) return;
  }
  radicle_free_record_array(records); radicle_env_destroy(abi);
  for (const auto& [type, count_value] : totals) RecordProperty(type, count_value);
  const auto bytes = fo4::write_plugin(env.plugin(plugin_index)); const auto original = fo4::plugin_original_bytes(env.plugin(plugin_index));
  ASSERT_EQ(bytes.size(), original.size()); EXPECT_EQ(std::memcmp(bytes.data(), original.data(), bytes.size()), 0);
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, VegetationReal,
    testing::ValuesIn(masters));

}
