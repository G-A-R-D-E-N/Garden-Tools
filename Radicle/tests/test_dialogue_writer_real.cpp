#include "fo4/environment/dialogue.hpp"
#include "fo4/environment/environment.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/scene_writer.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/formlink_lists.hpp"
#include "fo4/schema/localized_fields.hpp"
#include "fo4/schema/preflight.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"
#include "real_data_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;

class DialogueWriterReal : public testing::TestWithParam<const char*> {};

constexpr int k_path_cap = 10;
constexpr int k_record_cap = 20;
constexpr int k_preflight_sample = 40;

fo4::Signature sig(const char* text) { return fo4::Signature::from_string(text); }

bool is_dialogue(fo4::Signature s) { return s == sig("DIAL") || s == sig("INFO") || s == sig("SCEN"); }

bool has(const fo4::Record& record, const char* name) {
  return std::any_of(record.subrecords.begin(), record.subrecords.end(),
                     [&](const fo4::Subrecord& sub) { return sub.signature == sig(name); });
}

std::filesystem::path official(const char* name, bool& skip) {
  const auto* dir = std::getenv("FO4_DATA_DIR");
  skip = dir == nullptr || !std::filesystem::exists(std::filesystem::path{dir} / name);
  return skip ? std::filesystem::path{} : std::filesystem::path{dir} / name;
}

void walk(const fo4::Group& group, const std::function<void(const fo4::Record&, std::int32_t)>& visit) {
  for (const auto& child : group.children) {
    if (const auto* record = std::get_if<fo4::Record>(&child)) {
      visit(*record, group.header.group_type);
    } else {
      walk(std::get<fo4::Group>(child), visit);
    }
  }
}

std::string order_problem(const fo4::Record& record, std::int32_t parent) {
  const auto constraint = fo4::schema::record_authoring_constraint(record.header.signature);
  const auto rules = fo4::schema::authoring_subrecords(*constraint);
  const auto wanted = constraint->placement == fo4::schema::RecordPlacement::topic_children ? 7 : 10;
  if (parent != wanted) { return "placement"; }
  std::vector<int> counts(rules.size(), 0);
  std::size_t last = 0;
  for (const auto& sub : record.subrecords) {
    const auto it = std::find_if(rules.begin(), rules.end(), [&](const auto& rule) { return rule.signature == sub.signature; });
    if (it == rules.end()) { continue; }
    const auto index = static_cast<std::size_t>(it - rules.begin());
    if (index < last) { return "order " + sub.signature.to_string(); }
    last = index;
    ++counts[index];
  }
  for (std::size_t i = 0; i < rules.size(); ++i) {
    if (counts[i] < rules[i].minimum || counts[i] > rules[i].maximum) { return "count " + rules[i].signature.to_string(); }
  }
  return {};
}

bool responses_are_complete(const fo4::Record& record) {
  const auto& subs = record.subrecords;
  const char* members[] = {"NAM1", "NAM2", "NAM3", "NAM4"};
  std::map<std::string, int> counts;
  for (std::size_t i = 0; i < subs.size(); ++i) {
    ++counts[subs[i].signature.to_string()];
    if (subs[i].signature != sig("TRDA")) { continue; }
    if (subs[i].data.size() != 20 || i + 4 >= subs.size()) { return false; }
    for (std::size_t m = 0; m < 4; ++m) {
      if (subs[i + 1 + m].signature != sig(members[m])) { return false; }
    }
  }
  for (const char* member : members) {
    if (counts[member] != counts["TRDA"]) { return false; }
  }
  return true;
}

std::map<fo4::FormKey, Bytes> snapshot(const fo4::Plugin& plugin) {
  std::map<fo4::FormKey, Bytes> out;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature == sig("TES4")) { return; }
    out[record.form_key] = fo4::test::record_original_bytes(plugin, record);
  });
  return out;
}

}

TEST_P(DialogueWriterReal, EveryOfficialRecordFollowsTheAuthoringOrderPlacementAndResponseShape) {
  bool skip = false;
  const auto path = official(GetParam(), skip);
  if (skip) { GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data"; }
  record_game_build(detect_game_build(path.parent_path()));
  auto plugin = fo4::read_plugin_file(path);
  fo4::assign_record_form_keys(plugin);
  std::map<std::string, int> totals{{"DIAL", 0}, {"INFO", 0}, {"SCEN", 0}}, unlisted, sampled;
  std::vector<std::string> violators, incomplete, preflight;
  for (const auto& group : plugin.groups) {
    walk(group, [&](const fo4::Record& record, std::int32_t parent) {
      if (!is_dialogue(record.header.signature)) { return; }
      const auto type = record.header.signature.to_string();
      ++totals[type];
      if (const auto problem = order_problem(record, parent); !problem.empty()) {
        violators.push_back(record.form_key.to_string() + ' ' + problem);
      }
      const auto rules = fo4::schema::authoring_subrecords(*fo4::schema::record_authoring_constraint(record.header.signature));
      const bool extra = std::any_of(record.subrecords.begin(), record.subrecords.end(), [&](const fo4::Subrecord& sub) {
        return std::none_of(rules.begin(), rules.end(), [&](const auto& rule) { return rule.signature == sub.signature; });
      });
      unlisted[type] += extra ? 1 : 0;
      if (type == "INFO" && !responses_are_complete(record)) { incomplete.push_back(record.form_key.to_string()); }
      for (const auto& issue : fo4::schema::preflight_record(record, plugin.header.tes4_flags.is_localized())) {
        if (issue.severity == fo4::schema::PreflightSeverity::error) {
          preflight.push_back(record.form_key.to_string() + ' ' + issue.path + ' ' + issue.message);
        }
      }
      if (sampled[type]++ < k_preflight_sample) {
        for (const auto& issue : fo4::schema::preflight_authoring_record(plugin, record.form_key)) {
          EXPECT_NE(issue.severity, fo4::schema::PreflightSeverity::error) << record.form_key.to_string() << ' ' << issue.code;
        }
      }
    });
  }
  EXPECT_TRUE(violators.empty()) << violators.size() << " violators, first " << violators.front();
  EXPECT_TRUE(incomplete.empty()) << incomplete.size() << " INFO with an incomplete response, first " << incomplete.front();
  EXPECT_TRUE(preflight.empty()) << preflight.size() << " preflight errors, first " << preflight.front();
  for (const auto& [type, count] : totals) {
    RecordProperty(type, count);
    RecordProperty(type + "_with_unlisted_subrecords", unlisted[type]);
  }
}

TEST_P(DialogueWriterReal, CappedFieldSceneAndSubrecordEditsKeepEveryOtherRecordByteIdentical) {
  bool skip = false;
  const auto path = official(GetParam(), skip);
  if (skip) { GTEST_SKIP() << "set FO4_DATA_DIR to installed Fallout 4 data"; }
  record_game_build(detect_game_build(path.parent_path()));
  auto plugin = fo4::read_plugin_file(path);
  fo4::assign_record_form_keys(plugin);
  const auto original = snapshot(plugin);
  const bool localized = plugin.header.tes4_flags.is_localized();
  const auto context = fo4::plugin_form_id_context(plugin);

  std::map<fo4::Signature, fo4::FormKey> first_of;
  std::vector<const fo4::Record*> records;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    first_of.emplace(record.header.signature, record.form_key);
    if (is_dialogue(record.header.signature)) { records.push_back(&record); }
  });

  std::map<fo4::FormKey, std::vector<Bytes>> expected;
  std::map<std::string, int> per_path;
  int edits = 0, refusals = 0;
  for (const auto* record : records) {
    const auto key = record->form_key;
    for (const auto& field : fo4::schema::scalar_fields(record->header.signature)) {
      const auto slots = fo4::schema::resolve_field_slots(*record, fo4::schema::scalar_field_shape(field.scalar), field.scalar.address);
      if (slots.slots.empty() || per_path[std::string(field.path)] >= k_path_cap) { continue; }
      ++per_path[std::string(field.path)];
      SCOPED_TRACE(key.to_string() + ' ' + std::string(field.path));
      if (!expected.count(key)) {
        for (const auto& sub : record->subrecords) { expected[key].push_back(sub.data); }
      }
      auto& want = expected[key];
      for (const auto index : std::set<std::size_t>{0, slots.slots.size() - 1}) {
        const auto& slot = slots.slots[index];
        ASSERT_TRUE(slot.valid);
        if (field.scalar.storage == fo4::schema::ScalarStorage::zstring) {
          if (localized && fo4::schema::is_localized_field(record->header.signature, field.scalar.subrecord)) {
            EXPECT_THROW(static_cast<void>(fo4::set_record_string(plugin, key, field, index, "x")), fo4::schema::ScalarError);
            ++refusals;
            continue;
          }
          ASSERT_EQ(fo4::set_record_string(plugin, key, field, index, "radicle edit"), fo4::ScalarMutationStatus::updated);
          want[slot.subrecord] = fo4::schema::encode_scalar("radicle edit", field.scalar);
        } else if (field.scalar.storage == fo4::schema::ScalarStorage::bytes) {
          const std::string value = fo4::schema::decode_scalar(want[slot.subrecord], field.scalar) == "00" ? "00ff" : "00";
          ASSERT_EQ(fo4::set_record_scalar(plugin, key, field, index, value), fo4::ScalarMutationStatus::updated);
          want[slot.subrecord] = fo4::schema::encode_scalar(value, field.scalar);
        } else {
          const auto existing = fo4::schema::scalar_slot_bytes(want[slot.subrecord], slot, field.scalar);
          const std::string value = fo4::schema::encode_scalar("1", field.scalar) == Bytes(existing.begin(), existing.end()) ? "0" : "1";
          ASSERT_EQ(fo4::set_record_scalar(plugin, key, field, index, value), fo4::ScalarMutationStatus::updated);
          const auto bytes = fo4::schema::encode_scalar(value, field.scalar);
          std::copy(bytes.begin(), bytes.end(), want[slot.subrecord].begin() + static_cast<std::ptrdiff_t>(slot.offset));
        }
        ++edits;
      }
    }
    for (const auto& field : fo4::schema::formlink_fields(record->header.signature)) {
      const fo4::schema::FieldShape shape{field.subrecord, field.offset, 4, 0, false};
      const auto slots = fo4::schema::resolve_field_slots(*record, shape, field.address);
      if (slots.slots.empty() || per_path[std::string(field.path)] >= k_path_cap) { continue; }
      const auto targets = fo4::schema::formlink_field_targets(field);
      const auto found = first_of.find(targets[0]);
      if (!field.nullable && found == first_of.end()) { continue; }
      ++per_path[std::string(field.path)];
      SCOPED_TRACE(key.to_string() + ' ' + std::string(field.path));
      if (!expected.count(key)) {
        for (const auto& sub : record->subrecords) { expected[key].push_back(sub.data); }
      }
      const auto index = slots.slots.size() - 1;
      const auto& slot = slots.slots[index];
      const auto target = field.nullable ? fo4::FormKey{} : found->second;
      const auto status = fo4::set_record_formlink_field(plugin, key, field, index, target);
      ASSERT_TRUE(status == fo4::FormLinkFieldMutationStatus::updated || status == fo4::FormLinkFieldMutationStatus::unchanged);
      const auto raw = field.nullable ? 0u : fo4::encode_plugin_form_id(target, context).value();
      for (std::size_t i = 0; i < 4; ++i) {
        expected[key][slot.subrecord][slot.offset + i] = std::byte{static_cast<std::uint8_t>(raw >> (8 * i))};
      }
      ++edits;
    }
  }

  std::map<std::string, int> per_type;
  std::set<fo4::FormKey> structural, flagged;
  int scene_edits = 0, added = 0, removed = 0;
  for (const auto* record : records) {
    const auto type = record->header.signature.to_string();
    const auto key = record->form_key;
    const bool optional_present =
        type == "SCEN" || has(*record, "BNAM") || has(*record, "KNAM") || has(*record, "FULL") ||
        (type == "INFO" && (!has(*record, "TRDA") || has(*record, "ONAM") || has(*record, "ALFA") || has(*record, "GREE")));
    if (expected.count(key) || !optional_present || per_type[type]++ >= k_record_cap) { continue; }
    SCOPED_TRACE(key.to_string());
    structural.insert(key);
    if (type == "DIAL") {
      EXPECT_EQ(fo4::remove_record_subrecord(plugin, key, sig("QNAM"), 0), fo4::SubrecordRemoveStatus::not_authorable);
      EXPECT_EQ(fo4::remove_record_subrecord(plugin, key, sig("PNAM"), 0), fo4::SubrecordRemoveStatus::below_minimum);
      for (const char* optional : {"BNAM", "KNAM", "FULL"}) {
        if (has(*record, optional)) {
          ASSERT_EQ(fo4::remove_record_subrecord(plugin, key, sig(optional), 0), fo4::SubrecordRemoveStatus::removed);
          ++removed;
        }
      }
      EXPECT_EQ(fo4::add_record_subrecord(plugin, key, sig("FULL")),
                localized ? fo4::SubrecordAddStatus::no_default : fo4::SubrecordAddStatus::added);
    } else if (type == "INFO") {
      const bool grouped = has(*record, "TRDA") || has(*record, "CIS1") || has(*record, "CIS2");
      const auto status = fo4::add_record_subrecord(plugin, key, sig("IOVR"));
      EXPECT_EQ(status, grouped ? fo4::SubrecordAddStatus::unknown_order : fo4::SubrecordAddStatus::added);
      added += status == fo4::SubrecordAddStatus::added ? 1 : 0;
      for (const char* optional : {"ONAM", "ALFA", "GREE", "TIQS", "INCC"}) {
        if (has(*record, optional)) {
          ASSERT_EQ(fo4::remove_record_subrecord(plugin, key, sig(optional), 0), fo4::SubrecordRemoveStatus::removed);
          ++removed;
        }
      }
      EXPECT_EQ(fo4::remove_record_subrecord(plugin, key, sig("TRDA"), 0), fo4::SubrecordRemoveStatus::not_authorable);
    } else {
      EXPECT_EQ(fo4::add_record_subrecord(plugin, key, sig("NNAM")), fo4::SubrecordAddStatus::unknown_order);
      for (const char* optional : {"NNAM", "XNAM"}) {
        if (has(*record, optional)) {
          ASSERT_EQ(fo4::remove_record_subrecord(plugin, key, sig(optional), 0), fo4::SubrecordRemoveStatus::removed);
          ++removed;
        }
      }
      EXPECT_EQ(fo4::remove_record_subrecord(plugin, key, sig("FNAM"), 0), fo4::SubrecordRemoveStatus::not_authorable);
      const auto actions = *fo4::scene_group_count(plugin, key, fo4::ScenePart::action);
      const auto phases = *fo4::scene_group_count(plugin, key, fo4::ScenePart::phase);
      if (actions > 0) {
        const auto start = fo4::set_scene_scalar(plugin, key, "SCEN\\Action\\StartPhase", actions - 1, "0");
        const auto end = fo4::set_scene_scalar(plugin, key, "SCEN\\Action\\EndPhase", actions - 1,
                                               std::to_string(phases > 0 ? phases - 1 : 0));
        EXPECT_NE(start, fo4::ScalarMutationStatus::record_missing);
        EXPECT_NE(end, fo4::ScalarMutationStatus::record_missing);
        ++scene_edits;
      }
      const auto flags = fo4::set_scene_scalar(plugin, key, "SCEN\\Flags", 0, "4");
      EXPECT_NE(flags, fo4::ScalarMutationStatus::record_missing);
      if (flags != fo4::ScalarMutationStatus::subrecord_missing) { flagged.insert(key); }
      EXPECT_TRUE(fo4::set_scene_formlink(plugin, key, "SCEN\\ParentQuest", first_of.at(sig("QUST"))) !=
                  fo4::FormLinkFieldMutationStatus::subrecord_missing);
      scene_edits += 2;
    }
  }

  auto reread = fo4::read_plugin(fo4::write_plugin(plugin), path);
  fo4::assign_record_form_keys(reread);
  const auto after = snapshot(reread);
  for (const auto& [record, data] : original) {
    if (expected.count(record) || structural.count(record)) { continue; }
    ASSERT_TRUE(after.count(record)) << record.to_string();
    EXPECT_EQ(after.at(record), data) << record.to_string();
  }
  for (const auto& [key, want] : expected) {
    SCOPED_TRACE(key.to_string());
    const auto* edited = fo4::find_record(reread, key);
    ASSERT_NE(edited, nullptr);
    ASSERT_EQ(edited->subrecords.size(), want.size());
    for (std::size_t i = 0; i < want.size(); ++i) { EXPECT_EQ(edited->subrecords[i].data, want[i]) << i; }
  }
  const auto root = std::filesystem::temp_directory_path() / ("radicle_dialogue_real_" + std::string(GetParam()));
  std::filesystem::create_directories(root);
  fo4::write_plugin_file(reread, root / GetParam());
  {
    const std::filesystem::path paths[] = {root / GetParam()};
    const auto env = fo4::env::Environment::load(paths);
    for (const auto* ref : env.plugin_records(0)) {
      const auto key = ref->record->form_key;
      if (!expected.count(key) && !structural.count(key)) { continue; }
      SCOPED_TRACE(key.to_string());
      const auto type = ref->record->header.signature;
      if (type == sig("DIAL")) { EXPECT_NO_THROW(static_cast<void>(fo4::env::decode_dialogue_topic(env, *ref))); }
      if (type == sig("INFO")) { EXPECT_NO_THROW(static_cast<void>(fo4::env::decode_dialogue_info(env, *ref))); }
      if (type == sig("SCEN")) {
        EXPECT_NO_THROW(static_cast<void>(fo4::env::decode_scene(env, *ref)));
        if (flagged.count(key)) { EXPECT_EQ(fo4::env::decode_scene(env, *ref).flags, 4u); }
      }
    }
  }
  std::error_code error;
  std::filesystem::remove_all(root, error);
  RecordProperty("field_edits", edits);
  RecordProperty("localized_refusals", refusals);
  RecordProperty("paths", static_cast<int>(per_path.size()));
  RecordProperty("scene_edits", scene_edits);
  RecordProperty("subrecords_added", added);
  RecordProperty("subrecords_removed", removed);
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, DialogueWriterReal,
    testing::Values("Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm", "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));
