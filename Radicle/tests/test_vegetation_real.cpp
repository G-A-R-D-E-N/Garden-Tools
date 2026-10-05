#include "fo4/c_api_vegetation.h"
#include "fo4/environment/vegetation.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>

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

INSTANTIATE_TEST_SUITE_P(OfficialMasters, VegetationReal,
    testing::Values("Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm", "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));

}
