#include "fo4/c_api_vegetation.h"
#include "fo4/binary/cursor.hpp"
#include "fo4/environment/vegetation.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/preflight.hpp"
#include "plugin_bytes.hpp"
#include "real_data_build.hpp"
#include "real_sample_plugin.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <iomanip>
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
    for (std::size_t j = 0; j < 4; ++j) { EXPECT_EQ(value.seasons[j], (*native.seasons)[j]); }
  }
  for (const auto& sub : record.subrecords) {
    if (sub.signature.to_string() != "PFPC") continue;
    ASSERT_EQ(sub.data.size(), 4u);
    EXPECT_EQ(value.has_seasons, 1);
    for (std::size_t j = 0; j < 4; ++j) { EXPECT_EQ(value.seasons[j], std::to_integer<std::uint8_t>(sub.data[j])); }
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
  if (!dir) { GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data"; }
  record_game_build(detect_game_build(dir));
  const auto path = std::filesystem::path{dir} / GetParam();
  if (!std::filesystem::exists(path)) { GTEST_SKIP() << "missing official master: " << path; }
  std::vector<std::filesystem::path> native_paths; std::vector<std::string> names;
  for (const auto& master : fo4::read_plugin_file(path).header.masters)
    native_paths.emplace_back(std::filesystem::path{dir} / master.key.filename());
  native_paths.push_back(path);
  for (const auto& dependency : native_paths) {
    if (!std::filesystem::exists(dependency)) { GTEST_SKIP() << "missing official master: " << dependency; }
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
  for (const auto& [type, count_value] : totals) { RecordProperty(type, count_value); }
  const auto bytes = fo4::write_plugin(env.plugin(plugin_index)); const auto original = fo4::plugin_original_bytes(env.plugin(plugin_index));
  ASSERT_EQ(bytes.size(), original.size()); EXPECT_EQ(std::memcmp(bytes.data(), original.data(), bytes.size()), 0);
}

TEST_P(VegetationReal, EveryNewScalarPathEditsOfficialRecordsWithoutLosingData) {
  const auto* dir = std::getenv("FO4_DATA_DIR");
  if (!dir) { GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data"; }
  record_game_build(detect_game_build(dir));
  const auto path = std::filesystem::path{dir} / GetParam();
  if (!std::filesystem::exists(path)) { GTEST_SKIP() << "missing official master: " << path; }
  const std::filesystem::path native_paths[] = {path}; const auto env = fo4::env::Environment::load(native_paths);
  auto sample = fo4::make_empty_plugin(env.plugin(0).key.filename());
  sample.header = env.plugin(0).header; sample.tes4 = env.plugin(0).tes4; sample.original_data = env.plugin(0).original_data;
  std::map<std::string, fo4::Group> groups;
  for (const auto* ref : env.plugin_records(0)) {
    const auto type = ref->record->header.signature.to_string();
    if (type != "GRAS" && type != "TREE" && type != "FLOR") continue;
    auto& group = groups[type];
    group.header.label = {std::byte{static_cast<unsigned char>(type[0])}, std::byte{static_cast<unsigned char>(type[1])},
                          std::byte{static_cast<unsigned char>(type[2])}, std::byte{static_cast<unsigned char>(type[3])}};
    group.children.emplace_back(*ref->record);
  }
  for (auto& [type, group] : groups) sample.groups.push_back(std::move(group));
  sample = fo4::read_plugin(fo4::write_plugin(sample), path);
  const auto original = sample;
  std::map<std::string, std::string> applied;
  std::map<std::string, std::size_t> applied_subrecord;
  std::map<std::string, int> edits;
  int string_rows = 0;
  for (const auto& group : original.groups) {
    for (const auto& child : group.children) {
      const auto& record = std::get<fo4::Record>(child);
      for (const auto& field : fo4::schema::scalar_fields(record.header.signature)) {
        SCOPED_TRACE(record.form_key.to_string() + " " + std::string(field.path));
        if (field.scalar.storage == fo4::schema::ScalarStorage::zstring) { ++string_rows; continue; }
        const auto slots = fo4::schema::resolve_field_slots(record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
        if (slots.slots.empty() || !slots.slots.front().valid) continue;
        const auto& slot = slots.slots.front();
        const auto existing = fo4::schema::scalar_slot_bytes(record.subrecords[slot.subrecord].data, slot, field.scalar);
        const std::string value = fo4::schema::encode_scalar("1", field.scalar) == std::vector<std::byte>(existing.begin(), existing.end()) ? "2" : "1";
        ASSERT_EQ(fo4::set_record_scalar(sample, record.form_key, field, 0, value), fo4::ScalarMutationStatus::updated);
        const auto key = record.form_key.to_string() + std::string(field.path);
        applied[key] = value;
        applied_subrecord[key] = slot.subrecord;
        ++edits[std::string(field.path)];
      }
    }
  }
  for (const auto& [name, count] : edits) RecordProperty(name, count);
  const auto reread = fo4::read_plugin(fo4::write_plugin(sample), path);
  std::size_t checked = 0;
  for (const auto& group : original.groups) {
    for (const auto& child : group.children) {
      const auto& before = std::get<fo4::Record>(child);
      const auto* after = fo4::find_record(reread, before.form_key);
      ASSERT_NE(after, nullptr);
      ASSERT_EQ(after->subrecords.size(), before.subrecords.size());
      for (std::size_t i = 0; i < before.subrecords.size(); ++i) {
        const auto& old_sub = before.subrecords[i]; const auto& new_sub = after->subrecords[i];
        ASSERT_EQ(new_sub.signature, old_sub.signature); ASSERT_EQ(new_sub.data.size(), old_sub.data.size());
        auto expected = old_sub.data;
        for (const auto& field : fo4::schema::scalar_fields(before.header.signature)) {
          const auto key = before.form_key.to_string() + std::string(field.path);
          const auto found = applied.find(key);
          if (field.scalar.subrecord != old_sub.signature || found == applied.end()) continue;
          const auto written = applied_subrecord.find(key);
          if (written == applied_subrecord.end() || written->second != i) continue;
          fo4::schema::patch_scalar_field(expected, field.scalar, fo4::schema::encode_scalar(found->second, field.scalar));
          ++checked;
        }
        EXPECT_EQ(new_sub.data, expected) << before.form_key.to_string() << ' ' << old_sub.signature.to_string();
      }
      const auto type = before.header.signature.to_string();
      if (type == "GRAS") {
        const auto grass = fo4::env::decode_grass(env, fo4::env::RecordRef{0, after});
        const auto density = applied.find(before.form_key.to_string() + "GRAS\\Density");
        if (grass && density != applied.end()) { EXPECT_EQ(grass->density, std::stoi(density->second)); }
      } else if (type == "FLOR") {
        const auto flora = fo4::env::decode_flora(env, fo4::env::RecordRef{0, after});
        const auto flags = applied.find(before.form_key.to_string() + "FLOR\\Flags");
        if (flora.flags && flags != applied.end()) { EXPECT_EQ(*flora.flags, std::stoi(flags->second)); }
      }
    }
  }
  RecordProperty("fields", static_cast<int>(checked));
  RecordProperty("string_rows", string_rows);
}

using fo4::test::snapshot;
using fo4::test::subs_of;

const std::map<std::string, fo4::FormKey>& official_targets(const std::filesystem::path& data) {
  static const auto targets = [&] {
    std::map<std::string, fo4::FormKey> out;
    const auto base = fo4::read_plugin_file(data / "Fallout4.esm");
    fo4::visit_records(base, [&](const fo4::Record& record) {
      const auto type = record.header.signature.to_string();
      if ((type == "MISC" || type == "SNDR" || type == "GRAS") && !out.count(type)) { out[type] = record.form_key; }
    });
    return out;
  }();
  return targets;
}

bool is_vegetation(const std::string& type) { return type == "GRAS" || type == "TREE" || type == "FLOR" || type == "LTEX"; }

TEST_P(VegetationReal, EveryOfficialRecordIsListedAndOrderedInTheAuthoringSchema) {
  const auto* dir = std::getenv("FO4_DATA_DIR");
  if (!dir) { GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data"; }
  record_game_build(detect_game_build(dir));
  const auto path = std::filesystem::path{dir} / GetParam();
  if (!std::filesystem::exists(path)) { GTEST_SKIP() << "missing official master: " << path; }
  auto plugin = fo4::read_plugin_file(path);
  std::map<std::string, int> totals;
  std::vector<std::string> violators;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    const auto type = record.header.signature.to_string();
    if (!is_vegetation(type)) { return; }
    ++totals[type];
    const auto rules = fo4::schema::authoring_subrecords(*fo4::schema::record_authoring_constraint(record.header.signature));
    std::size_t stages = 0;
    for (const auto& sub : record.subrecords) {
      const auto listed = std::any_of(rules.begin(), rules.end(), [&](const auto& rule) { return rule.signature == sub.signature; });
      EXPECT_TRUE(listed) << record.form_key.to_string() << ' ' << sub.signature.to_string();
      if (sub.signature == fo4::Signature::from_string("DSTD")) { ++stages; }
    }
    bool clean = true;
    for (const auto& issue : fo4::schema::preflight_authoring_record(plugin, record.form_key)) {
      if (issue.severity == fo4::schema::PreflightSeverity::error) { clean = false; }
    }
    if (!clean) {
      violators.push_back(record.form_key.to_string());
      EXPECT_GT(stages, 1u) << record.form_key.to_string();
    } else {
      EXPECT_LE(stages, 1u) << record.form_key.to_string();
    }
  });
  for (const auto& [type, count] : totals) { RecordProperty(type, count); }
  RecordProperty("order_violations", static_cast<int>(violators.size()));
}

TEST_P(VegetationReal, LinksGrassListsAndOptionalSubrecordsEditOfficialRecordsAndKeepEveryOtherRecordByteIdentical) {
  const auto* dir = std::getenv("FO4_DATA_DIR");
  if (!dir) { GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data"; }
  record_game_build(detect_game_build(dir));
  const auto path = std::filesystem::path{dir} / GetParam();
  if (!std::filesystem::exists(path)) { GTEST_SKIP() << "missing official master: " << path; }
  const auto& pick = official_targets(dir);
  ASSERT_EQ(pick.size(), 3u);
  std::set<fo4::FormKey> anchors;
  for (const auto& [type, key] : pick) { anchors.insert(key); }
  auto plugin = fo4::test::make_sample(fo4::read_plugin_file(path), [&](const fo4::Record& record) {
    const auto type = record.header.signature.to_string();
    return type == "TREE" || type == "FLOR" || type == "LTEX" || anchors.count(record.form_key) > 0;
  }, 100);
  const auto original = snapshot(plugin);
  const auto misc = pick.at("MISC"), sound = pick.at("SNDR"), grass = pick.at("GRAS");
  const auto field = [](const char* name) { return *fo4::schema::formlink_field_descriptor(name); };
  const auto grass_list = *fo4::schema::formlink_list_descriptor(fo4::Signature::from_string("LTEX"), "LTEX\\Grasses");
  std::vector<std::pair<std::string, fo4::FormKey>> targets;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    const auto type = record.header.signature.to_string();
    if (type == "TREE" || type == "FLOR" || type == "LTEX") { targets.emplace_back(type, record.form_key); }
  });
  RecordProperty("eligible_records", static_cast<int>(targets.size()));
  std::map<fo4::FormKey, std::size_t> grass_counts;
  int edits = 0, added = 0;
  for (const auto& [type, record] : targets) {
    SCOPED_TRACE(record.to_string());
    const auto start = subs_of(plugin, record);
    const auto has = [&](const char* name) {
      return std::any_of(start.begin(), start.end(), [&](const auto& sub) { return sub.first == name; });
    };
    if (type == "TREE") {
      const bool had_links = has("PFIG") || has("SNAM");
      for (const char* name : {"PFIG", "SNAM"}) {
        if (has(name)) { continue; }
        ASSERT_EQ(fo4::add_record_subrecord(plugin, record, fo4::Signature::from_string(name)), fo4::SubrecordAddStatus::added);
        ++added;
      }
      EXPECT_EQ(fo4::set_record_formlink_field(plugin, record, field("TREE\\Ingredient"), 0, misc), fo4::FormLinkFieldMutationStatus::updated);
      EXPECT_EQ(fo4::set_record_formlink_field(plugin, record, field("TREE\\HarvestSound"), 0, sound), fo4::FormLinkFieldMutationStatus::updated);
      edits += 2;
      if (!had_links) {
        const auto linked = subs_of(plugin, record);
        ASSERT_EQ(fo4::remove_record_subrecord(plugin, record, fo4::Signature::from_string("PFIG"), 0), fo4::SubrecordRemoveStatus::removed);
        ASSERT_EQ(fo4::remove_record_subrecord(plugin, record, fo4::Signature::from_string("SNAM"), 0), fo4::SubrecordRemoveStatus::removed);
        EXPECT_EQ(subs_of(plugin, record), start);
        ASSERT_EQ(fo4::add_record_subrecord(plugin, record, fo4::Signature::from_string("PFIG")), fo4::SubrecordAddStatus::added);
        ASSERT_EQ(fo4::add_record_subrecord(plugin, record, fo4::Signature::from_string("SNAM")), fo4::SubrecordAddStatus::added);
        ASSERT_EQ(fo4::set_record_formlink_field(plugin, record, field("TREE\\Ingredient"), 0, misc), fo4::FormLinkFieldMutationStatus::updated);
        ASSERT_EQ(fo4::set_record_formlink_field(plugin, record, field("TREE\\HarvestSound"), 0, sound), fo4::FormLinkFieldMutationStatus::updated);
        EXPECT_EQ(subs_of(plugin, record), linked);
      }
    } else if (type == "FLOR") {
      if (has("PFIG")) {
        const auto status = fo4::set_record_formlink_field(plugin, record, field("FLOR\\Ingredient"), 0, misc);
        EXPECT_TRUE(status == fo4::FormLinkFieldMutationStatus::updated || status == fo4::FormLinkFieldMutationStatus::unchanged);
        ++edits;
      }
      if (has("SNAM")) {
        const auto status = fo4::set_record_formlink_field(plugin, record, field("FLOR\\HarvestSound"), 0, sound);
        EXPECT_TRUE(status == fo4::FormLinkFieldMutationStatus::updated || status == fo4::FormLinkFieldMutationStatus::unchanged);
        ++edits;
      }
      EXPECT_EQ(fo4::remove_record_subrecord(plugin, record, fo4::Signature::from_string("PFPC"), 0), fo4::SubrecordRemoveStatus::below_minimum);
    } else {
      const auto count = *fo4::schema::record_formlink_list_size(plugin, record, grass_list);
      for (std::size_t i = 0; i < count; ++i) {
        const auto status = fo4::schema::set_record_formlink_list_element(plugin, record, grass_list, i, grass);
        EXPECT_TRUE(status == fo4::schema::FormLinkListMutationStatus::updated || status == fo4::schema::FormLinkListMutationStatus::unchanged);
        ++edits;
      }
      const auto settled = subs_of(plugin, record);
      ASSERT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, record, grass_list, count, grass), fo4::schema::FormLinkListMutationStatus::updated);
      EXPECT_EQ(*fo4::schema::record_formlink_list_size(plugin, record, grass_list), count + 1);
      ASSERT_EQ(fo4::remove_record_subrecord(plugin, record, fo4::Signature::from_string("GNAM"), count), fo4::SubrecordRemoveStatus::removed);
      EXPECT_EQ(subs_of(plugin, record), settled);
      ASSERT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, record, grass_list, count, grass), fo4::schema::FormLinkListMutationStatus::updated);
      ++added;
      grass_counts[record] = count + 1;
    }
  }

  const auto bytes = fo4::write_plugin(plugin);
  auto reread = fo4::read_plugin(bytes, path);
  fo4::assign_record_form_keys(reread);
  const auto after = snapshot(reread);
  const auto context = fo4::plugin_form_id_context(reread);
  const auto raw = [&](const fo4::FormKey& target) {
    fo4::test::ByteWriter out;
    out.u32(fo4::encode_plugin_form_id(target, context).value());
    return out.bytes;
  };
  std::set<fo4::FormKey> touched;
  for (const auto& [type, record] : targets) { touched.insert(record); }
  for (const auto& [record, data] : original) {
    SCOPED_TRACE(record.to_string());
    if (touched.count(record)) { continue; }
    ASSERT_TRUE(after.count(record));
    EXPECT_EQ(after.at(record), data);
  }
  for (const auto& [type, record] : targets) {
    SCOPED_TRACE(record.to_string());
    const auto subs = subs_of(reread, record);
    for (const auto& [name, data] : subs) {
      if (type == "TREE" && name == "PFIG") { EXPECT_EQ(data, raw(misc)); }
      if (type == "TREE" && name == "SNAM") { EXPECT_EQ(data, raw(sound)); }
      if (type == "FLOR" && name == "PFIG") { EXPECT_EQ(data, raw(misc)); }
      if (type == "FLOR" && name == "SNAM") { EXPECT_EQ(data, raw(sound)); }
      if (type == "LTEX" && name == "GNAM") { EXPECT_EQ(data, raw(grass)); }
    }
    if (type == "LTEX") { EXPECT_EQ(*fo4::schema::record_formlink_list_size(reread, record, grass_list), grass_counts.at(record)); }
    EXPECT_EQ(subs, subs_of(plugin, record));
  }
  RecordProperty("link_edits", edits);
  RecordProperty("subrecords_added", added);
  RecordProperty("records_edited", static_cast<int>(targets.size()));
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, VegetationReal,
    testing::ValuesIn(masters));

}
