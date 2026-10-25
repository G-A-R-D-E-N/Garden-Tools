#include "fo4/environment/environment.hpp"
#include "fo4/environment/sound.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/sound_writer.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using Subs = std::vector<std::pair<std::string, Bytes>>;

class SoundWriterReal : public testing::TestWithParam<const char*> {};

constexpr int k_sweep_per_type = 60;

fo4::Signature sig(const char* text) { return fo4::Signature::from_string(text); }

bool is_sound(fo4::Signature signature) {
  return signature == sig("SNDR") || signature == sig("SOUN") || signature == sig("MUSC") || signature == sig("FSTP") ||
         signature == sig("FSTS");
}

bool rate_group_member(fo4::Signature signature) {
  return signature == sig("ITMS") || signature == sig("INTV") || signature == sig("FNAM") || signature == sig("ITME");
}

std::filesystem::path official(const char* name, bool& skip) {
  const auto* dir = std::getenv("FO4_DATA_DIR");
  skip = dir == nullptr || !std::filesystem::exists(std::filesystem::path{dir} / name);
  return skip ? std::filesystem::path{} : std::filesystem::path{dir} / name;
}

Subs subs_of(const fo4::Plugin& plugin, const fo4::FormKey& key) {
  Subs out;
  if (const auto* record = fo4::find_record(plugin, key)) {
    for (const auto& sub : record->subrecords) { out.emplace_back(sub.signature.to_string(), sub.data); }
  }
  return out;
}

std::map<fo4::FormKey, Bytes> snapshot(const fo4::Plugin& plugin) {
  std::map<fo4::FormKey, Bytes> out;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature == sig("TES4")) { return; }
    out[record.form_key] = fo4::test::record_original_bytes(plugin, record);
  });
  return out;
}

fo4::Plugin sound_sample(const fo4::Plugin& full, const std::filesystem::path& path) {
  auto sample = fo4::make_empty_plugin(full.key.filename());
  sample.header = full.header;
  sample.tes4 = full.tes4;
  sample.original_data = full.original_data;
  std::map<std::string, fo4::Group> groups;
  fo4::visit_records(full, [&](const fo4::Record& record) {
    if (!is_sound(record.header.signature)) { return; }
    const auto type = record.header.signature.to_string();
    auto& group = groups[type];
    group.header.label = {std::byte{static_cast<unsigned char>(type[0])}, std::byte{static_cast<unsigned char>(type[1])},
                          std::byte{static_cast<unsigned char>(type[2])}, std::byte{static_cast<unsigned char>(type[3])}};
    group.children.emplace_back(record);
  });
  for (auto& [type, group] : groups) { sample.groups.push_back(std::move(group)); }
  sample = fo4::read_plugin(fo4::write_plugin(sample), path);
  fo4::assign_record_form_keys(sample);
  return sample;
}

void decode(const fo4::env::Environment& env, const fo4::env::RecordRef& ref) {
  const auto type = ref.record->header.signature;
  if (type == sig("SNDR")) { static_cast<void>(fo4::env::decode_sound_descriptor(env, ref)); }
  if (type == sig("SOUN")) { static_cast<void>(fo4::env::decode_sound_marker(env, ref)); }
  if (type == sig("MUSC")) { static_cast<void>(fo4::env::decode_music_type(env, ref)); }
  if (type == sig("FSTP")) { static_cast<void>(fo4::env::decode_footstep(env, ref)); }
  if (type == sig("FSTS")) { static_cast<void>(fo4::env::decode_footstep_set(env, ref)); }
}

}

TEST_P(SoundWriterReal, EveryOfficialRecordIsListedOrderedAndPreflightClean) {
  bool skip = false;
  const auto path = official(GetParam(), skip);
  if (skip) { GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data"; }
  record_game_build(detect_game_build(path.parent_path()));
  auto plugin = sound_sample(fo4::read_plugin_file(path), path);
  std::map<std::string, int> totals;
  std::vector<std::string> violators;
  int rate_group_records = 0;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (!is_sound(record.header.signature)) { return; }
    ++totals[record.header.signature.to_string()];
    const auto rules = fo4::schema::authoring_subrecords(*fo4::schema::record_authoring_constraint(record.header.signature));
    bool grouped = false;
    for (const auto& sub : record.subrecords) {
      const auto listed = std::any_of(rules.begin(), rules.end(), [&](const auto& rule) { return rule.signature == sub.signature; });
      if (!listed && record.header.signature == sig("SNDR") && rate_group_member(sub.signature)) {
        grouped = true;
        continue;
      }
      EXPECT_TRUE(listed) << record.form_key.to_string() << ' ' << sub.signature.to_string();
    }
    rate_group_records += grouped ? 1 : 0;
    for (const auto& issue : fo4::schema::preflight_authoring_record(plugin, record.form_key)) {
      if (issue.severity == fo4::schema::PreflightSeverity::error) { violators.push_back(record.form_key.to_string() + ' ' + issue.path); }
    }
    for (const auto& issue : fo4::schema::preflight_record(record)) {
      EXPECT_NE(issue.severity, fo4::schema::PreflightSeverity::error) << record.form_key.to_string() << ' ' << issue.path << ' ' << issue.message;
    }
  });
  EXPECT_TRUE(violators.empty()) << violators.front();
  for (const auto& [type, count] : totals) { RecordProperty(type, count); }
  RecordProperty("order_violations", static_cast<int>(violators.size()));
  RecordProperty("rate_group_records", rate_group_records);
}

TEST_P(SoundWriterReal, EveryScalarStringAndLinkPathEditsFirstAndLastSlotsWithoutTouchingOtherBytes) {
  bool skip = false;
  const auto path = official(GetParam(), skip);
  if (skip) { GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data"; }
  record_game_build(detect_game_build(path.parent_path()));
  const auto full = fo4::read_plugin_file(path);
  auto sample = sound_sample(full, path);
  const auto original = sample;
  std::map<fo4::FormKey, fo4::FormKey> self_targets;
  std::map<fo4::FormKey, std::vector<Bytes>> expected;
  std::map<std::string, int> edits;
  int derived_rows = 0;
  fo4::visit_records(original, [&](const fo4::Record& record) {
    if (!is_sound(record.header.signature)) { return; }
    auto& want = expected[record.form_key];
    for (const auto& sub : record.subrecords) { want.push_back(sub.data); }
    for (const auto& field : fo4::schema::scalar_fields(record.header.signature)) {
      SCOPED_TRACE(record.form_key.to_string() + ' ' + std::string(field.path));
      if (fo4::schema::is_derived_scalar_row(field.record, field.scalar.subrecord)) { ++derived_rows; continue; }
      const auto* live = fo4::find_record(sample, record.form_key);
      ASSERT_NE(live, nullptr);
      const auto slots = fo4::schema::resolve_field_slots(*live, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
      std::set<std::size_t> picks;
      if (!slots.slots.empty()) { picks = {0, slots.slots.size() - 1}; }
      for (const auto index : picks) {
        const auto& slot = slots.slots[index];
        if (!slot.valid || slot.misfit) {
          EXPECT_EQ(fo4::set_record_scalar(sample, record.form_key, field, index, "1"), fo4::ScalarMutationStatus::subrecord_missing);
          continue;
        }
        if (field.scalar.storage == fo4::schema::ScalarStorage::zstring) {
          ASSERT_EQ(fo4::set_record_string(sample, record.form_key, field, index, "radicle\\edit.wav"), fo4::ScalarMutationStatus::updated);
          const std::string_view stored{"radicle\\edit.wav"};
          Bytes payload(reinterpret_cast<const std::byte*>(stored.data()), reinterpret_cast<const std::byte*>(stored.data()) + stored.size());
          payload.push_back(std::byte{0});
          want[slot.subrecord] = payload;
        } else {
          const auto existing = fo4::schema::scalar_slot_bytes(live->subrecords[slot.subrecord].data, slot, field.scalar);
          const std::string value = fo4::schema::encode_scalar("1", field.scalar) == Bytes(existing.begin(), existing.end()) ? "0" : "1";
          ASSERT_EQ(fo4::set_record_scalar(sample, record.form_key, field, index, value), fo4::ScalarMutationStatus::updated);
          const auto bytes = fo4::schema::encode_scalar(value, field.scalar);
          std::copy(bytes.begin(), bytes.end(), want[slot.subrecord].begin() + static_cast<std::ptrdiff_t>(slot.offset));
        }
        ++edits[std::string(field.path)];
      }
    }
    for (const auto& field : fo4::schema::formlink_fields(record.header.signature)) {
      SCOPED_TRACE(record.form_key.to_string() + ' ' + std::string(field.path));
      const fo4::schema::FieldShape shape{field.subrecord, field.offset, 4, 0, false};
      const auto* live = fo4::find_record(sample, record.form_key);
      ASSERT_NE(live, nullptr);
      const auto slots = fo4::schema::resolve_field_slots(*live, shape, field.address);
      if (slots.slots.empty()) { continue; }
      const auto index = slots.slots.size() - 1;
      const auto& slot = slots.slots[index];
      const auto targets = fo4::schema::formlink_field_targets(field);
      const bool self = std::find(targets.begin(), targets.end(), record.header.signature) != targets.end();
      if (!field.nullable && !self) { continue; }
      if (!slot.valid) {
        EXPECT_EQ(fo4::set_record_formlink_field(sample, record.form_key, field, index, record.form_key),
                  fo4::FormLinkFieldMutationStatus::subrecord_missing);
        continue;
      }
      const auto target = field.nullable ? fo4::FormKey{} : record.form_key;
      const auto status = fo4::set_record_formlink_field(sample, record.form_key, field, index, target);
      ASSERT_TRUE(status == fo4::FormLinkFieldMutationStatus::updated || status == fo4::FormLinkFieldMutationStatus::unchanged);
      const auto raw = field.nullable ? 0u : record.header.form_id.value();
      for (std::size_t i = 0; i < 4; ++i) {
        want[slot.subrecord][slot.offset + i] = std::byte{static_cast<std::uint8_t>(raw >> (8 * i))};
      }
      ++edits[std::string(field.path)];
    }
  });
  const auto reread = fo4::read_plugin(fo4::write_plugin(sample), path);
  int checked = 0;
  for (const auto& [key, want] : expected) {
    SCOPED_TRACE(key.to_string());
    const auto* after = fo4::find_record(reread, key);
    const auto* before = fo4::find_record(original, key);
    ASSERT_NE(after, nullptr);
    ASSERT_EQ(after->subrecords.size(), want.size());
    for (std::size_t i = 0; i < want.size(); ++i) {
      EXPECT_EQ(after->subrecords[i].signature, before->subrecords[i].signature);
      EXPECT_EQ(after->subrecords[i].data, want[i]) << before->subrecords[i].signature.to_string() << ' ' << i;
    }
    ++checked;
  }
  int total = 0;
  for (const auto& [name, count] : edits) { total += count; }
  RecordProperty("records", checked);
  RecordProperty("paths", static_cast<int>(edits.size()));
  RecordProperty("edits", total);
  RecordProperty("derived_rows", derived_rows);
}

TEST_P(SoundWriterReal, LinksAndOptionalSubrecordsEditOfficialRecordsAndKeepEveryOtherRecordByteIdentical) {
  bool skip = false;
  const auto path = official(GetParam(), skip);
  if (skip) { GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data"; }
  record_game_build(detect_game_build(path.parent_path()));
  auto plugin = fo4::read_plugin_file(path);
  fo4::assign_record_form_keys(plugin);
  const auto original = snapshot(plugin);
  const auto field = [](const char* name) { return *fo4::schema::formlink_field_descriptor(name); };
  std::vector<std::pair<std::string, fo4::FormKey>> targets;
  std::map<std::string, fo4::FormKey> first;
  std::map<std::string, int> per_type;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (!is_sound(record.header.signature)) { return; }
    const auto type = record.header.signature.to_string();
    first.emplace(type, record.form_key);
    const bool grouped = std::any_of(record.subrecords.begin(), record.subrecords.end(),
                                     [](const fo4::Subrecord& sub) { return sub.signature == sig("ITMS"); });
    if (per_type[type]++ < k_sweep_per_type || grouped) { targets.emplace_back(type, record.form_key); }
  });
  if (targets.empty()) {
    const auto bytes = fo4::write_plugin(plugin);
    const auto original_bytes = fo4::plugin_original_bytes(plugin);
    EXPECT_EQ(bytes, Bytes(original_bytes.begin(), original_bytes.end()));
  }
  const auto changed = [](fo4::FormLinkFieldMutationStatus status) {
    return status == fo4::FormLinkFieldMutationStatus::updated || status == fo4::FormLinkFieldMutationStatus::unchanged;
  };
  const auto list_changed = [](fo4::schema::FormLinkListMutationStatus status) {
    return status == fo4::schema::FormLinkListMutationStatus::updated || status == fo4::schema::FormLinkListMutationStatus::unchanged;
  };
  int edits = 0, added = 0, removed = 0;
  std::set<fo4::FormKey> touched;
  for (const auto& [type, record] : targets) {
    SCOPED_TRACE(record.to_string());
    const auto start = subs_of(plugin, record);
    const auto has = [&](const char* name) { return std::any_of(start.begin(), start.end(), [&](const auto& sub) { return sub.first == name; }); };
    const auto grouped = has("ITMS");
    if (type == "SNDR") {
      if (has("DNAM")) {
        const auto n = *fo4::record_field_count(plugin, record, "SNDR\\Descriptors");
        EXPECT_TRUE(changed(fo4::set_record_formlink_field(plugin, record, field("SNDR\\Descriptors"), n - 1, first.at("SNDR"))));
        ++edits;
      }
      EXPECT_EQ(fo4::remove_record_subrecord(plugin, record, sig("CNAM"), 0), fo4::SubrecordRemoveStatus::not_authorable);
      EXPECT_EQ(fo4::remove_record_subrecord(plugin, record, sig("BNAM"), 0), fo4::SubrecordRemoveStatus::not_authorable);
      if (grouped) {
        EXPECT_EQ(fo4::add_record_subrecord(plugin, record, sig("NNAM")), fo4::SubrecordAddStatus::unknown_order);
        EXPECT_TRUE(changed(fo4::set_record_formlink_field(plugin, record, field("SNDR\\BaseDescriptor"), 0, first.at("SNDR"))));
        ++edits;
      } else if (!has("NNAM")) {
        ASSERT_EQ(fo4::add_record_subrecord(plugin, record, sig("NNAM")), fo4::SubrecordAddStatus::added);
        ASSERT_EQ(fo4::set_record_string(plugin, record, "SNDR\\Notes", 0, "radicle"), fo4::ScalarMutationStatus::updated);
        ++added;
      }
      if (has("ANAM") && !grouped) {
        ASSERT_EQ(fo4::add_record_subrecord(plugin, record, sig("ANAM")), fo4::SubrecordAddStatus::added);
        const auto n = *fo4::record_field_count(plugin, record, "SNDR\\Sounds");
        ASSERT_EQ(fo4::remove_record_subrecord(plugin, record, sig("ANAM"), n - 1), fo4::SubrecordRemoveStatus::removed);
      }
    } else if (type == "SOUN") {
      if (has("REPT")) {
        ASSERT_EQ(fo4::remove_record_subrecord(plugin, record, sig("REPT"), 0), fo4::SubrecordRemoveStatus::removed);
        ++removed;
      } else {
        ASSERT_EQ(fo4::add_record_subrecord(plugin, record, sig("REPT")), fo4::SubrecordAddStatus::added);
        ++added;
      }
      EXPECT_EQ(fo4::remove_record_subrecord(plugin, record, sig("SDSC"), 0), fo4::SubrecordRemoveStatus::below_minimum);
      EXPECT_TRUE(changed(fo4::set_record_formlink_field(plugin, record, field("SOUN\\SoundDescriptor"), 0, first.at("SNDR"))));
      ++edits;
    } else if (type == "MUSC") {
      if (has("TNAM")) {
        EXPECT_TRUE(list_changed(fo4::schema::set_record_formlink_list_element(plugin, record, *fo4::schema::formlink_list_descriptor(fo4::Signature::from_string("MUSC"), "MUSC\\Tracks"), 0, fo4::FormKey{})));
        ++edits;
      } else {
        ASSERT_EQ(fo4::add_record_subrecord(plugin, record, sig("TNAM")), fo4::SubrecordAddStatus::added);
        ++added;
      }
      EXPECT_EQ(fo4::remove_record_subrecord(plugin, record, sig("PNAM"), 0), fo4::SubrecordRemoveStatus::below_minimum);
    } else if (type == "FSTP") {
      EXPECT_TRUE(changed(fo4::set_record_formlink_field(plugin, record, field("FSTP\\ImpactDataSet"), 0, fo4::FormKey{})));
      ++edits;
    } else {
      const auto n = *fo4::record_field_count(plugin, record, "FSTS\\Footsteps");
      ASSERT_GT(n, 0u);
      EXPECT_TRUE(changed(fo4::set_record_formlink_field(plugin, record, field("FSTS\\Footsteps"), n - 1, first.at("FSTP"))));
      EXPECT_EQ(fo4::remove_record_subrecord(plugin, record, sig("DATA"), 0), fo4::SubrecordRemoveStatus::not_authorable);
      ++edits;
    }
    touched.insert(record);
  }

  auto reread = fo4::read_plugin(fo4::write_plugin(plugin), path);
  fo4::assign_record_form_keys(reread);
  const auto after = snapshot(reread);
  for (const auto& [record, data] : original) {
    if (touched.count(record)) { continue; }
    ASSERT_TRUE(after.count(record)) << record.to_string();
    EXPECT_EQ(after.at(record), data) << record.to_string();
  }
  const auto edited_sample = sound_sample(reread, path);
  for (const auto& [type, record] : targets) {
    SCOPED_TRACE(record.to_string());
    EXPECT_EQ(subs_of(reread, record), subs_of(plugin, record));
    for (const auto& issue : fo4::schema::preflight_authoring_record(edited_sample, record)) {
      EXPECT_NE(issue.severity, fo4::schema::PreflightSeverity::error) << issue.path;
    }
  }
  const auto root = std::filesystem::temp_directory_path() / ("radicle_sound_real_" + std::string(GetParam()));
  std::filesystem::create_directories(root);
  fo4::write_plugin_file(reread, root / GetParam());
  {
    const std::filesystem::path paths[] = {root / GetParam()};
    const auto env = fo4::env::Environment::load(paths);
    for (const auto* ref : env.plugin_records(0)) {
      if (!is_sound(ref->record->header.signature)) { continue; }
      SCOPED_TRACE(ref->record->form_key.to_string());
      EXPECT_NO_THROW(decode(env, *ref));
    }
  }
  std::error_code error;
  std::filesystem::remove_all(root, error);
  RecordProperty("link_edits", edits);
  RecordProperty("subrecords_added", added);
  RecordProperty("subrecords_removed", removed);
  RecordProperty("records_edited", static_cast<int>(targets.size()));
}

TEST_P(SoundWriterReal, AddsAndRemovesFilesAndBaseLinksOnOfficialDescriptors) {
  bool skip = false;
  const auto path = official(GetParam(), skip);
  if (skip) { GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data"; }
  record_game_build(detect_game_build(path.parent_path()));
  auto sample = sound_sample(fo4::read_plugin_file(path), path);
  auto original = sample;
  int file_edits = 0;
  int base_edits = 0;
  bool base_proven = false;
  std::vector<fo4::FormKey> touched;
  fo4::FormKey first_descriptor{};
  fo4::visit_records(original, [&](const fo4::Record& record) {
    if (record.header.signature != sig("SNDR")) { return; }
    if (first_descriptor.owner().empty()) first_descriptor = record.form_key;
    const auto subs = subs_of(original, record.form_key);
    bool has_rate_group = false;
    bool auto_weapon = false;
    for (const auto& [name, bytes] : subs) {
      if (rate_group_member(sig(name.c_str()))) has_rate_group = true;
      if (name == "CNAM" && bytes.size() == 4) {
        std::uint32_t kind = 0;
        std::memcpy(&kind, bytes.data(), 4);
        auto_weapon = kind == 0xED157AE3u;
      }
    }
    const auto* live = fo4::find_record(sample, record.form_key);
    ASSERT_NE(live, nullptr);
    if (!has_rate_group || file_edits != 0) {
      if (auto_weapon && !base_proven) {
        const auto snapshot = *live;
        const auto removed = fo4::remove_sound_descriptor_base(sample, record.form_key);
        if (removed == fo4::SoundDescriptorMutationStatus::updated) {
          const auto added = fo4::add_sound_descriptor_base(sample, record.form_key, first_descriptor);
          if (added == fo4::SoundDescriptorMutationStatus::updated) {
            const auto restored = fo4::remove_sound_descriptor_base(sample, record.form_key);
            if (restored == fo4::SoundDescriptorMutationStatus::updated) {
              base_proven = true;
              ++base_edits;
              touched.push_back(record.form_key);
            }
          }
        }
        if (subs_of(sample, record.form_key) != subs_of(original, record.form_key)) {
          auto* restore = fo4::find_record(sample, record.form_key);
          if (restore != nullptr) *restore = snapshot;
        }
      }
      return;
    }
    if (fo4::add_sound_descriptor_file(sample, record.form_key, "radicle\\real.wav") !=
        fo4::SoundDescriptorMutationStatus::updated) {
      return;
    }
    const auto* record_now = fo4::find_record(sample, record.form_key);
    ASSERT_NE(record_now, nullptr);
    bool found_file = false;
    for (const auto& sub : record_now->subrecords) {
      if (sub.signature.to_string() != "ANAM") continue;
      const std::string_view text(reinterpret_cast<const char*>(sub.data.data()), sub.data.size());
      if (!text.empty() && text.back() == '\0' && text.substr(0, text.size() - 1) == "radicle\\real.wav") {
        found_file = true;
      }
    }
    EXPECT_TRUE(found_file);
    std::size_t file_index = 0;
    std::size_t seen = 0;
    for (const auto& sub : record_now->subrecords) {
      if (sub.signature.to_string() != "ANAM") continue;
      const std::string_view text(reinterpret_cast<const char*>(sub.data.data()), sub.data.size());
      if (!text.empty() && text.back() == '\0' && text.substr(0, text.size() - 1) == "radicle\\real.wav") {
        file_index = seen;
      }
      ++seen;
    }
    EXPECT_EQ(fo4::remove_sound_descriptor_file(sample, record.form_key, file_index),
              fo4::SoundDescriptorMutationStatus::updated);
    const auto restored = subs_of(sample, record.form_key);
    EXPECT_EQ(restored, subs);
    ++file_edits;
    touched.push_back(record.form_key);
  });
  for (const auto& key : touched) {
    const auto* live = fo4::find_record(sample, key);
    ASSERT_NE(live, nullptr);
    const auto before = subs_of(original, key);
    const auto after = subs_of(sample, key);
    EXPECT_EQ(after, before) << key.to_string();
  }
  const auto reread = fo4::read_plugin(fo4::write_plugin(sample), path);
  for (const auto& key : touched) {
    const auto* live = fo4::find_record(reread, key);
    ASSERT_NE(live, nullptr);
    EXPECT_EQ(subs_of(reread, key), subs_of(original, key)) << key.to_string();
  }
  RecordProperty("file_edits", file_edits);
  RecordProperty("base_edits", base_edits);
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, SoundWriterReal,
    testing::Values("Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm", "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));
