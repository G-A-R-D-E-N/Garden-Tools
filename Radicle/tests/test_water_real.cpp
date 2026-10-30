#include "fo4/c_api_env.h"
#include "fo4/environment/base_records.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/localized_fields.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"
#include "real_data_build.hpp"
#include "real_sample_plugin.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

class WaterRealData : public testing::TestWithParam<const char*> {
protected:
  void SetUp() override {
    const auto* dir = std::getenv("FO4_DATA_DIR");
    if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data";
    path_ = std::filesystem::path{dir} / GetParam();
    if (!std::filesystem::exists(path_)) GTEST_SKIP() << "missing official master: " << path_;
    build_ = detect_game_build(dir);
    record_game_build(build_);
  }

  std::filesystem::path path_;
  GameBuild build_ = game_build_from_version("");
};

TEST_P(WaterRealData, DecodesEveryWaterAndPreservesMasterBytes) {
  const std::filesystem::path paths[] = {path_};
  const auto env = fo4::env::Environment::load(paths);
  std::size_t seen = 0;
  std::map<std::size_t, int> sizes;
  std::map<std::string, int> populated{{"DNAM", 0}, {"NAM0", 0}, {"NAM1", 0}, {"NAM2", 0}, {"NAM3", 0}, {"NAM4", 0}};
  const auto nonzero = [](const auto& vec) {
    return vec && std::any_of(vec->begin(), vec->end(), [](float x) { return x != 0.0f; });
  };
  for (const auto* ref : env.plugin_records(0)) {
    if (ref->record->header.signature != fo4::Signature::from_string("WATR")) continue;
    SCOPED_TRACE(ref->record->form_key.to_string());
    const auto water = fo4::env::decode_water(env, *ref);
    EXPECT_TRUE(water.visual.has_value());
    if (water.visual) ++populated["DNAM"];
    if (nonzero(water.linear_velocity)) ++populated["NAM0"];
    if (nonzero(water.angular_velocity)) ++populated["NAM1"];
    for (std::size_t i = 0; i < water.noise_textures.size(); ++i) {
      if (water.noise_textures[i] && !water.noise_textures[i]->empty()) ++populated["NAM" + std::to_string(i + 2)];
    }
    for (const auto& sub : ref->record->subrecords) {
      if (sub.signature == fo4::Signature::from_string("DNAM")) ++sizes[sub.data.size()];
    }
    ++seen;
  }
  RecordProperty("WATR", static_cast<int>(seen));
  for (const auto& [size, count] : sizes) RecordProperty("DNAM_" + std::to_string(size), count);
  for (const auto& [name, count] : populated) RecordProperty("populated_" + name, count);
  const auto& plugin = env.plugin(0);
  const auto bytes = fo4::write_plugin(plugin);
  const auto original = fo4::plugin_original_bytes(plugin);
  ASSERT_EQ(bytes.size(), original.size());
  EXPECT_EQ(std::memcmp(bytes.data(), original.data(), bytes.size()), 0);
}

TEST_P(WaterRealData, CApiExposesEveryWater) {
  const auto utf8 = path_.u8string();
  const std::string path{utf8.begin(), utf8.end()};
  const char* paths[] = {path.c_str()};
  radicle_env* loaded = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &loaded), RADICLE_STATUS_OK) << radicle_last_error();
  const std::unique_ptr<radicle_env, decltype(&radicle_env_destroy)> env{loaded, radicle_env_destroy};
  const radicle_record** records = nullptr;
  std::size_t count = 0;
  ASSERT_EQ(radicle_env_winners_of(env.get(), "WATR", &records, &count), RADICLE_STATUS_OK);
  const std::unique_ptr<const radicle_record*, decltype(&radicle_free_record_array)> array{
      records, radicle_free_record_array};
  for (std::size_t i = 0; i < count; ++i) {
    radicle_record_info record{};
    ASSERT_EQ(radicle_record_get_info(env.get(), records[i], &record), RADICLE_STATUS_OK);
    SCOPED_TRACE(std::to_string(record.local_id));
    radicle_water_info water{};
    ASSERT_EQ(radicle_water_get_info(env.get(), records[i], &water), RADICLE_STATUS_OK) << radicle_last_error();
    EXPECT_EQ(water.has_visual, 1);
  }
  RecordProperty("WATR", static_cast<int>(count));
}

TEST_P(WaterRealData, MasterHasExpectedWaterCountAndDnamSizes) {
  const std::map<std::string, std::size_t> expected{
      {"Fallout4.esm", 42},      {"DLCCoast.esm", 2},       {"DLCNukaWorld.esm", 3}, {"DLCRobot.esm", 0},
      {"DLCworkshop01.esm", 0}, {"DLCworkshop02.esm", 0}, {"DLCworkshop03.esm", 0}};
  auto plugin = fo4::read_plugin_file(path_);
  std::size_t count = 0;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature != fo4::Signature::from_string("WATR")) { return; }
    ++count;
    SCOPED_TRACE(record.form_key.to_string());
    for (const auto& sub : record.subrecords) {
      if (sub.signature != fo4::Signature::from_string("DNAM")) { continue; }
      EXPECT_TRUE(sub.data.size() == 188 || sub.data.size() == 201) << sub.data.size();
    }
  });
  RecordProperty("WATR_master", static_cast<int>(count));
  if (exact_counts_known(build_)) { EXPECT_EQ(count, expected.at(GetParam())); }
}

INSTANTIATE_TEST_SUITE_P(
    OfficialMasters, WaterRealData,
    testing::Values("Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
                    "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));

using Bytes = std::vector<std::byte>;

const fo4::Signature k_watr = fo4::Signature::from_string("WATR");

fo4::Signature sig(const char* text) { return fo4::Signature::from_string(text); }

using fo4::test::snapshot;
using fo4::test::subs_of;

const std::map<std::string, fo4::FormKey>& base_targets(const std::filesystem::path& data) {
  static const auto targets = [&] {
    std::map<std::string, fo4::FormKey> out;
    const auto base = fo4::read_plugin_file(data / "Fallout4.esm");
    fo4::visit_records(base, [&](const fo4::Record& record) {
      const auto type = record.header.signature.to_string();
      if ((type == "MATT" || type == "SNDR" || type == "SPEL" || type == "IMGS") && !out.count(type)) { out[type] = record.form_key; }
    });
    return out;
  }();
  return targets;
}

fo4::Plugin water_sample(const std::filesystem::path& path) {
  std::set<fo4::FormKey> anchors;
  for (const auto& [type, key] : base_targets(path.parent_path())) { anchors.insert(key); }
  return fo4::test::make_sample(fo4::read_plugin_file(path), [&](const fo4::Record& record) {
    return record.header.signature == k_watr || anchors.count(record.form_key) > 0;
  }, 100);
}

TEST_P(WaterRealData, EveryOfficialWaterIsListedInXEditOrderAndPreflightClean) {
  auto plugin = fo4::read_plugin_file(path_);
  const auto rules = fo4::schema::authoring_subrecords(*fo4::schema::record_authoring_constraint(k_watr));
  int seen = 0;
  std::vector<std::string> violators;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature != k_watr) { return; }
    ++seen;
    std::size_t last = 0;
    for (const auto& sub : record.subrecords) {
      const auto rule = std::find_if(rules.begin(), rules.end(), [&](const auto& item) { return item.signature == sub.signature; });
      EXPECT_NE(rule, rules.end()) << record.form_key.to_string() << ' ' << sub.signature.to_string();
      if (rule == rules.end()) { continue; }
      const auto index = static_cast<std::size_t>(rule - rules.begin());
      EXPECT_GE(index, last) << record.form_key.to_string() << ' ' << sub.signature.to_string();
      last = index;
    }
    for (const auto& issue : fo4::schema::preflight_authoring_record(plugin, record.form_key)) {
      if (issue.severity == fo4::schema::PreflightSeverity::error) { violators.push_back(record.form_key.to_string() + ' ' + issue.path); }
    }
    for (const auto& issue : fo4::schema::preflight_record(record)) {
      EXPECT_NE(issue.severity, fo4::schema::PreflightSeverity::error) << record.form_key.to_string() << ' ' << issue.path << ' ' << issue.message;
    }
  });
  EXPECT_TRUE(violators.empty()) << (violators.empty() ? std::string{} : violators.front());
  RecordProperty("WATR", seen);
  RecordProperty("order_violations", static_cast<int>(violators.size()));
}

TEST_P(WaterRealData, EveryScalarAndStringPathEditsWithoutTouchingOtherBytes) {
  auto plugin = water_sample(path_);
  fo4::assign_record_form_keys(plugin);
  const auto original = plugin;
  const auto before = snapshot(plugin);
  std::map<fo4::FormKey, std::vector<Bytes>> expected;
  int edits = 0, refusals = 0;
  fo4::visit_records(original, [&](const fo4::Record& record) {
    if (record.header.signature != k_watr) { return; }
    auto& want = expected[record.form_key];
    for (const auto& sub : record.subrecords) { want.push_back(sub.data); }
    for (const auto& field : fo4::schema::scalar_fields(k_watr)) {
      SCOPED_TRACE(record.form_key.to_string() + ' ' + std::string(field.path));
      const auto slots = fo4::schema::resolve_field_slots(record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
      const auto is_string = field.scalar.storage == fo4::schema::ScalarStorage::zstring;
      if (slots.slots.empty()) {
        const auto status = is_string ? fo4::set_record_string(plugin, record.form_key, field, 0, "x")
                                      : fo4::set_record_scalar(plugin, record.form_key, field, 0, "1");
        EXPECT_EQ(status, fo4::ScalarMutationStatus::subrecord_missing);
        continue;
      }
      ASSERT_EQ(slots.slots.size(), 1u);
      const auto& slot = slots.slots[0];
      if (!slot.valid || slot.misfit) {
        EXPECT_EQ(fo4::set_record_scalar(plugin, record.form_key, field, 0, "1"), fo4::ScalarMutationStatus::subrecord_missing);
        ++refusals;
        continue;
      }
      if (is_string) {
        if (plugin.header.tes4_flags.is_localized() && fo4::schema::is_localized_field(k_watr, field.scalar.subrecord)) {
          EXPECT_THROW(fo4::set_record_string(plugin, record.form_key, field, 0, "x"), fo4::schema::ScalarError);
          ++refusals;
          continue;
        }
        ASSERT_EQ(fo4::set_record_string(plugin, record.form_key, field, 0, "radicle\\edit.dds"), fo4::ScalarMutationStatus::updated);
        const std::string_view stored{"radicle\\edit.dds"};
        Bytes text(reinterpret_cast<const std::byte*>(stored.data()), reinterpret_cast<const std::byte*>(stored.data()) + stored.size());
        text.push_back(std::byte{0});
        want[slot.subrecord] = text;
      } else {
        const auto existing = fo4::schema::scalar_slot_bytes(record.subrecords[slot.subrecord].data, slot, field.scalar);
        const std::string value = fo4::schema::encode_scalar("1", field.scalar) == Bytes(existing.begin(), existing.end()) ? "0" : "1";
        ASSERT_EQ(fo4::set_record_scalar(plugin, record.form_key, field, 0, value), fo4::ScalarMutationStatus::updated);
        const auto bytes = fo4::schema::encode_scalar(value, field.scalar);
        std::copy(bytes.begin(), bytes.end(), want[slot.subrecord].begin() + static_cast<std::ptrdiff_t>(slot.offset));
      }
      ++edits;
    }
  });
  auto reread = fo4::read_plugin(fo4::write_plugin(plugin), path_);
  fo4::assign_record_form_keys(reread);
  for (const auto& [key, want] : expected) {
    SCOPED_TRACE(key.to_string());
    const auto* after = fo4::find_record(reread, key);
    const auto* start = fo4::find_record(original, key);
    ASSERT_NE(after, nullptr);
    ASSERT_EQ(after->subrecords.size(), want.size());
    for (std::size_t i = 0; i < want.size(); ++i) {
      EXPECT_EQ(after->subrecords[i].signature, start->subrecords[i].signature);
      EXPECT_EQ(after->subrecords[i].data, want[i]) << start->subrecords[i].signature.to_string();
    }
  }
  for (const auto& [record, bytes] : snapshot(reread)) {
    if (!expected.count(record)) { EXPECT_EQ(before.at(record), bytes) << record.to_string(); }
  }
  RecordProperty("records", static_cast<int>(expected.size()));
  RecordProperty("edits", edits);
  RecordProperty("refusals", refusals);
}

TEST_P(WaterRealData, LinksAndOptionalSubrecordsEditAndKeepEveryOtherRecordByteIdentical) {
  const auto& pick = base_targets(path_.parent_path());
  ASSERT_EQ(pick.size(), 4u);
  auto plugin = water_sample(path_);
  fo4::assign_record_form_keys(plugin);
  const auto original = snapshot(plugin);
  const auto field = [](const char* name) { return *fo4::schema::formlink_field_descriptor(name); };
  const auto changed = [](fo4::FormLinkFieldMutationStatus status) {
    return status == fo4::FormLinkFieldMutationStatus::updated || status == fo4::FormLinkFieldMutationStatus::unchanged;
  };
  std::vector<fo4::FormKey> targets;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature == k_watr) { targets.push_back(record.form_key); }
  });
  int edits = 0, added = 0;
  const std::pair<const char*, const char*> links[] = {
      {"WATR\\MaterialUnused", "MATT"}, {"WATR\\ConsumeSpell", "SPEL"}, {"WATR\\ContactSpell", "SPEL"}, {"WATR\\ImageSpace", "IMGS"}};
  for (const auto& record : targets) {
    SCOPED_TRACE(record.to_string());
    for (const auto& [path, type] : links) {
      const auto present = *fo4::record_field_count(plugin, record, path) == 1u;
      const auto status = fo4::set_record_formlink_field(plugin, record, field(path), 0, pick.at(type));
      if (present) {
        EXPECT_TRUE(changed(status)) << path;
        ++edits;
      } else {
        EXPECT_EQ(status, fo4::FormLinkFieldMutationStatus::subrecord_missing) << path;
      }
    }
    if (*fo4::record_field_count(plugin, record, "WATR\\OpenSound") == 1u) {
      EXPECT_TRUE(changed(fo4::set_record_formlink_field(plugin, record, field("WATR\\OpenSound"), 0, fo4::FormKey{})));
      ASSERT_EQ(fo4::remove_record_subrecord(plugin, record, sig("SNAM"), 0), fo4::SubrecordRemoveStatus::removed);
    }
    ASSERT_EQ(fo4::add_record_subrecord(plugin, record, sig("SNAM")), fo4::SubrecordAddStatus::added);
    ASSERT_EQ(fo4::set_record_formlink_field(plugin, record, field("WATR\\OpenSound"), 0, pick.at("SNDR")), fo4::FormLinkFieldMutationStatus::updated);
    ++added;
    EXPECT_EQ(fo4::add_record_subrecord(plugin, record, sig("GNAM")), fo4::SubrecordAddStatus::already_present);
    EXPECT_EQ(fo4::remove_record_subrecord(plugin, record, sig("DNAM"), 0), fo4::SubrecordRemoveStatus::below_minimum);
    EXPECT_EQ(fo4::remove_record_subrecord(plugin, record, sig("FNAM"), 0), fo4::SubrecordRemoveStatus::below_minimum);
    if (fo4::record_field_count(plugin, record, "WATR\\Name").value_or(0) == 0) {
      EXPECT_EQ(fo4::add_record_subrecord(plugin, record, sig("FULL")), fo4::SubrecordAddStatus::no_default);
    }
  }
  auto reread = fo4::read_plugin(fo4::write_plugin(plugin), path_);
  fo4::assign_record_form_keys(reread);
  const auto after = snapshot(reread);
  const std::set<fo4::FormKey> touched(targets.begin(), targets.end());
  for (const auto& [record, data] : original) {
    if (touched.count(record)) { continue; }
    ASSERT_TRUE(after.count(record)) << record.to_string();
    EXPECT_EQ(after.at(record), data) << record.to_string();
  }
  const std::filesystem::path paths[] = {path_};
  const auto env = fo4::env::Environment::load(paths);
  const auto image = fo4::schema::formlink_field_descriptor("WATR\\ImageSpace");
  const auto sound = fo4::schema::formlink_field_descriptor("WATR\\OpenSound");
  ASSERT_TRUE(image.has_value() && sound.has_value());
  for (const auto& record : targets) {
    SCOPED_TRACE(record.to_string());
    EXPECT_EQ(subs_of(reread, record), subs_of(plugin, record));
    for (const auto& issue : fo4::schema::preflight_authoring_record(reread, record)) {
      EXPECT_NE(issue.severity, fo4::schema::PreflightSeverity::error) << issue.path;
    }
    const auto* stored = fo4::find_record(reread, record);
    EXPECT_TRUE(fo4::env::decode_water(env, fo4::env::RecordRef{0, stored}).visual.has_value());
    EXPECT_EQ(fo4::set_record_formlink_field(reread, record, *image, 0, pick.at("IMGS")), fo4::FormLinkFieldMutationStatus::unchanged);
    EXPECT_EQ(fo4::set_record_formlink_field(reread, record, *sound, 0, pick.at("SNDR")), fo4::FormLinkFieldMutationStatus::unchanged);
  }
  RecordProperty("link_edits", edits);
  RecordProperty("subrecords_added", added);
  RecordProperty("records_edited", static_cast<int>(targets.size()));
}

}
