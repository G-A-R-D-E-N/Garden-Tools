#include "fo4/c_api_vegetation.h"
#include "fo4/environment/vegetation.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/preflight.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

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

TEST_P(VegetationReal, EveryNewScalarPathEditsOfficialRecordsWithoutLosingData) {
  const auto* dir = std::getenv("FO4_DATA_DIR");
  if (!dir) GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data";
  const auto path = std::filesystem::path{dir} / GetParam();
  if (!std::filesystem::exists(path)) GTEST_SKIP() << "missing official master: " << path;
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
  std::map<std::string, int> edits;
  for (const auto& group : original.groups) {
    for (const auto& child : group.children) {
      const auto& record = std::get<fo4::Record>(child);
      for (const auto& field : fo4::schema::scalar_fields(record.header.signature)) {
        SCOPED_TRACE(record.form_key.to_string() + " " + std::string(field.path));
        const auto* subrecord = [&]() -> const fo4::Subrecord* {
          for (const auto& sub : record.subrecords) { if (sub.signature == field.scalar.subrecord) return &sub; }
          return nullptr;
        }();
        if (subrecord == nullptr) continue;
        const auto existing = fo4::schema::scalar_field_bytes(subrecord->data, field.scalar);
        if (existing.empty()) continue;
        const std::string value = fo4::schema::encode_scalar("1", field.scalar) == std::vector<std::byte>(existing.begin(), existing.end()) ? "2" : "1";
        ASSERT_EQ(fo4::set_record_scalar(sample, record.form_key, field, value), fo4::ScalarMutationStatus::updated);
        applied[record.form_key.to_string() + std::string(field.path)] = value;
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
          const auto found = applied.find(before.form_key.to_string() + std::string(field.path));
          if (field.scalar.subrecord != old_sub.signature || found == applied.end()) continue;
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
}

using Subs = std::vector<std::pair<std::string, std::vector<std::byte>>>;

Subs subs_of(const fo4::Plugin& plugin, const fo4::FormKey& key) {
  Subs out;
  if (const auto* record = fo4::find_record(plugin, key)) {
    for (const auto& sub : record->subrecords) { out.emplace_back(sub.signature.to_string(), sub.data); }
  }
  return out;
}

std::map<fo4::FormKey, std::vector<std::byte>> snapshot(const fo4::Plugin& plugin) {
  std::map<fo4::FormKey, std::vector<std::byte>> out;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature == fo4::Signature::from_string("TES4")) { return; }
    out[record.form_key] = fo4::test::record_original_bytes(plugin, record);
  });
  return out;
}

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
  if (!dir) GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data";
  const auto path = std::filesystem::path{dir} / GetParam();
  if (!std::filesystem::exists(path)) GTEST_SKIP() << "missing official master: " << path;
  const auto plugin = fo4::read_plugin_file(path);
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
  if (!dir) GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data";
  const auto path = std::filesystem::path{dir} / GetParam();
  if (!std::filesystem::exists(path)) GTEST_SKIP() << "missing official master: " << path;
  const auto& pick = official_targets(dir);
  ASSERT_EQ(pick.size(), 3u);
  auto plugin = fo4::read_plugin_file(path);
  const auto original = snapshot(plugin);
  const auto misc = pick.at("MISC"), sound = pick.at("SNDR"), grass = pick.at("GRAS");
  const auto field = [](const char* name) { return *fo4::schema::formlink_field_descriptor(name); };
  const auto grass_list = *fo4::schema::formlink_list_descriptor(fo4::Signature::from_string("LTEX"), "LTEX\\Grasses");
  std::vector<std::pair<std::string, fo4::FormKey>> targets;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    const auto type = record.header.signature.to_string();
    if (type == "TREE" || type == "FLOR" || type == "LTEX") { targets.emplace_back(type, record.form_key); }
  });
  if (targets.empty()) GTEST_SKIP() << "master has no TREE, FLOR or LTEX";
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
      const auto count = *fo4::record_field_count(plugin, record, "LTEX\\Grass");
      for (std::size_t i = 0; i < count; ++i) {
        const auto status = fo4::set_record_formlink_field(plugin, record, field("LTEX\\Grass"), i, grass);
        EXPECT_TRUE(status == fo4::FormLinkFieldMutationStatus::updated || status == fo4::FormLinkFieldMutationStatus::unchanged);
        ++edits;
      }
      const auto settled = subs_of(plugin, record);
      ASSERT_EQ(fo4::schema::insert_record_formlink_list_element(plugin, record, grass_list, count, grass), fo4::schema::FormLinkListMutationStatus::updated);
      EXPECT_EQ(*fo4::record_field_count(plugin, record, "LTEX\\Grass"), count + 1);
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
    if (type == "LTEX") { EXPECT_EQ(*fo4::record_field_count(reread, record, "LTEX\\Grass"), grass_counts.at(record)); }
    EXPECT_EQ(subs, subs_of(plugin, record));
  }
  RecordProperty("link_edits", edits);
  RecordProperty("subrecords_added", added);
  RecordProperty("records_edited", static_cast<int>(targets.size()));
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, VegetationReal,
    testing::Values("Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm", "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));

}
