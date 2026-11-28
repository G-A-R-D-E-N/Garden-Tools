#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/record_edit.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/localized_fields.hpp"
#include "fo4/schema/record_constraints.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

// Gated on FO4_DATA_DIR; CI has no game files and will skip.
constexpr std::array<const char*, 7> k_masters{
    "Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
    "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"};

const fo4::schema::SubrecordAuthoringConstraint* full_rule(fo4::Signature record) {
  static const auto full = fo4::Signature::from_string("FULL");
  const auto constraint = fo4::schema::record_authoring_constraint(record);
  if (!constraint.has_value()) return nullptr;
  for (const auto& rule : fo4::schema::authoring_subrecords(*constraint)) {
    if (rule.signature == full) return &rule;
  }
  return nullptr;
}

struct TemplateRun {
  std::size_t begin;
  std::size_t end;
  bool contains(std::size_t index) const { return index >= begin && index < end; }
};

// Constraint: an object-template run is the first OBTE through its STOP; a FULL inside it names a template combination. The record name is ahead of the run on WEAP, ARMO and FURN and after it on NPC_.
TemplateRun template_run(const fo4::Record& record) {
  const auto size = record.subrecords.size();
  for (std::size_t i = 0; i < size; ++i) {
    if (record.subrecords[i].signature != fo4::Signature::from_string("OBTE")) continue;
    for (std::size_t stop = i + 1; stop < size; ++stop) {
      if (record.subrecords[stop].signature == fo4::Signature::from_string("STOP")) return {i, stop + 1};
    }
    return {i, size};
  }
  return {size, size};
}

std::size_t header_full_index(const fo4::Record& record) {
  const auto run = template_run(record);
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    if (!run.contains(i) && record.subrecords[i].signature == fo4::Signature::from_string("FULL")) return i;
  }
  return record.subrecords.size();
}

bool has_unlisted_header_row(const fo4::Record& record) {
  const auto constraint = fo4::schema::record_authoring_constraint(record.header.signature);
  if (!constraint.has_value()) return false;
  const auto rules = fo4::schema::authoring_subrecords(*constraint);
  for (const auto& subrecord : record.subrecords) {
    bool listed = false;
    for (const auto& rule : rules) listed = listed || rule.signature == subrecord.signature;
    if (!listed) return true;
  }
  return false;
}

std::vector<fo4::Signature> signatures(const fo4::Record& record) {
  std::vector<fo4::Signature> out;
  for (const auto& sub : record.subrecords) out.push_back(sub.signature);
  return out;
}

std::string key_text(const fo4::Record& record) {
  return record.form_key.to_string() + " " + record.header.signature.to_string();
}

}  // namespace

struct NameTarget {
  fo4::FormKey key;
  fo4::Signature signature;
  fo4::schema::LocalizedString value;
};

std::vector<NameTarget> name_targets(const fo4::Plugin& plugin, bool compressed) {
  std::vector<NameTarget> targets;
  for (const auto* record : fo4::collect_records(plugin)) {
    if (record->compressed != compressed) continue;
    const auto index = header_full_index(*record);
    if (index == record->subrecords.size()) continue;
    const auto signature = record->header.signature;
    targets.push_back({record->form_key, signature,
                       fo4::schema::read_localized_string(plugin, signature,
                                                          fo4::Signature::from_string("FULL"),
                                                          record->subrecords[index])});
  }
  return targets;
}

void rename_then_restore(fo4::Plugin& plugin, const char* name, const std::vector<NameTarget>& targets,
                         std::vector<std::string>& failures) {
  for (const auto& target : targets) {
    fo4::schema::LocalizedString changed;
    if (target.value.id.has_value()) {
      changed.id = *target.value.id + 1;
    } else {
      changed.text = std::string("Renamed");
    }
    const auto status = fo4::set_record_full_name(plugin, target.key, changed);
    if (status != fo4::RecordNameStatus::updated) {
      failures.push_back(std::string(name) + " rename " + target.key.to_string() + " " +
                         target.signature.to_string());
    }
  }
  for (const auto& target : targets) {
    const auto status = fo4::set_record_full_name(plugin, target.key, target.value);
    if (status != fo4::RecordNameStatus::updated) {
      failures.push_back(std::string(name) + " restore " + target.key.to_string() + " " +
                         target.signature.to_string());
    }
  }
}

// Constraint: the authoring door refuses a required FULL (FLOR, KEYM) and a repeated one (NPC_), so the absent-name state is built by erasing the subrecord directly.
bool erase_full_by_ordinal(fo4::Plugin& plugin, const fo4::FormKey& key, std::size_t ordinal) {
  auto* record = fo4::detail::find_mutable_record(plugin, key);
  if (record == nullptr) return false;
  const auto full = fo4::Signature::from_string("FULL");
  std::size_t seen = 0;
  for (std::size_t index = 0; index < record->subrecords.size(); ++index) {
    if (record->subrecords[index].signature != full || seen++ != ordinal) continue;
    record->subrecords.erase(record->subrecords.begin() + static_cast<std::ptrdiff_t>(index));
    fo4::detail::mark_record_modified(plugin, *record);
    return true;
  }
  return false;
}

fo4::test::RecordState record_content(const fo4::Record& record) {
  fo4::test::RecordState state{.flags = record.header.flags};
  for (const auto& sub : record.subrecords) state.subrecords.emplace_back(sub.signature, sub.data);
  return state;
}

// Constraint: write_plugin recompresses a modified compressed record at zlib level 6 and vanilla uses 9, so compressed records are compared by decompressed content and the rest byte for byte.
TEST(RecordNameRealData, RenameThenRestoreEveryVanillaFullRestoresEveryRecord) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";

  std::size_t renamed = 0;
  std::size_t renamed_compressed = 0;
  std::vector<std::string> failures;
  for (const char* name : k_masters) {
    const auto path = std::filesystem::path{dir} / name;
    if (!std::filesystem::exists(path)) continue;

    {
      auto plugin = fo4::read_plugin_file(path);
      const auto original = fo4::test::original_bytes(plugin);
      const auto targets = name_targets(plugin, false);
      rename_then_restore(plugin, name, targets, failures);
      renamed += targets.size();

      const auto written = fo4::write_plugin(plugin);
      std::size_t first_difference = written.size() == original.size() ? original.size() : 0;
      for (std::size_t i = 0; i < written.size() && i < original.size(); ++i) {
        if (written[i] != original[i]) {
          first_difference = i;
          break;
        }
      }
      EXPECT_EQ(written.size(), original.size()) << name;
      EXPECT_EQ(first_difference, original.size()) << name << " first differing byte";
    }

    {
      auto plugin = fo4::read_plugin_file(path);
      const auto targets = name_targets(plugin, true);
      std::vector<fo4::test::RecordState> before;
      for (const auto& target : targets) before.push_back(record_content(*fo4::find_record(plugin, target.key)));
      rename_then_restore(plugin, name, targets, failures);
      renamed_compressed += targets.size();

      const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), path);
      for (std::size_t i = 0; i < targets.size(); ++i) {
        const auto* record = fo4::find_record(reread, targets[i].key);
        if (record == nullptr || !(record_content(*record) == before[i])) {
          failures.push_back(std::string(name) + " content " + targets[i].key.to_string() + " " +
                             targets[i].signature.to_string());
        }
      }
    }
  }

  EXPECT_GT(renamed, 0u);
  EXPECT_GT(renamed_compressed, 0u);
  for (const auto& failure : failures) ADD_FAILURE() << failure;
}

TEST(RecordNameRealData, EveryVanillaHeaderFullHasARowAndSitsWhereTheRowsPlaceIt) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";

  std::vector<std::string> failures;
  std::size_t checked = 0;
  for (const char* name : k_masters) {
    const auto path = std::filesystem::path{dir} / name;
    if (!std::filesystem::exists(path)) continue;
    const auto plugin = fo4::read_plugin_file(path);
    for (const auto* record : fo4::collect_records(plugin)) {
      const auto index = header_full_index(*record);
      if (index == record->subrecords.size()) continue;
      const auto* rule = full_rule(record->header.signature);
      if (rule == nullptr) {
        failures.push_back(std::string(name) + " no FULL row: " + key_text(*record));
        continue;
      }
      if (record->header.signature == fo4::Signature::from_string("REFR")) continue;
      ++checked;
      const auto constraint = fo4::schema::record_authoring_constraint(record->header.signature);
      const auto rules = fo4::schema::authoring_subrecords(*constraint);
      std::size_t full_rule_index = 0;
      for (std::size_t r = 0; r < rules.size(); ++r) {
        if (rules[r].signature == fo4::Signature::from_string("FULL")) full_rule_index = r;
      }
      // Constraint: a name ahead of the template run is checked against the rows ahead of the run, as the rows after it belong to other groups; a name after the run is checked against the whole record.
      const auto run = template_run(*record);
      const auto end = index < run.begin ? run.begin : record->subrecords.size();
      for (std::size_t i = 0; i < end; ++i) {
        if (i == index) continue;
        if (run.contains(i) && record->subrecords[i].signature == fo4::Signature::from_string("FULL")) continue;
        for (std::size_t r = 0; r < rules.size(); ++r) {
          if (rules[r].signature != record->subrecords[i].signature) continue;
          const bool before = i < index;
          if (before ? r >= full_rule_index : r <= full_rule_index) {
            failures.push_back(std::string(name) + " " + key_text(*record) + " " +
                               record->subrecords[i].signature.to_string() + " on the wrong side of FULL");
          }
        }
      }
    }
  }
  EXPECT_GT(checked, 0u);
  for (const auto& failure : failures) ADD_FAILURE() << failure;
}

TEST(RecordNameRealData, ReinsertingAVanillaFullReproducesItsVanillaPosition) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";

  std::size_t checked = 0;
  std::vector<std::string> failures;
  for (const char* name : k_masters) {
    const auto path = std::filesystem::path{dir} / name;
    if (!std::filesystem::exists(path)) continue;
    auto plugin = fo4::read_plugin_file(path);

    struct Target {
      fo4::FormKey key;
      fo4::Signature signature;
      fo4::schema::LocalizedString value;
      std::vector<fo4::Signature> order;
      bool refused;
      std::size_t ordinal;
    };
    std::vector<Target> targets;
    for (const auto* record : fo4::collect_records(plugin)) {
      const auto index = header_full_index(*record);
      if (index == record->subrecords.size()) continue;
      const auto signature = record->header.signature;
      if (full_rule(signature) == nullptr) continue;
      // Constraint: REFR FULL is coupled (map-marker name), so it is never inserted.
      if (signature == fo4::Signature::from_string("REFR")) continue;
      targets.push_back({record->form_key, signature,
                         fo4::schema::read_localized_string(plugin, signature,
                                                            fo4::Signature::from_string("FULL"),
                                                            record->subrecords[index]),
                         signatures(*record),
                         signature == fo4::Signature::from_string("LCTN") && has_unlisted_header_row(*record),
                         // Constraint: template-combination FULLs can precede the name (NPC_), so the name is removed by its own ordinal.
                         static_cast<std::size_t>(std::count_if(
                             record->subrecords.begin(), record->subrecords.begin() + static_cast<std::ptrdiff_t>(index),
                             [](const auto& sub) { return sub.signature == fo4::Signature::from_string("FULL"); }))});
    }

    for (const auto& target : targets) {
      ++checked;
      if (!erase_full_by_ordinal(plugin, target.key, target.ordinal)) {
        failures.push_back(std::string(name) + " remove " + target.key.to_string());
        continue;
      }
      const auto status = fo4::set_record_full_name(plugin, target.key, target.value);
      const auto expected = target.refused ? fo4::RecordNameStatus::not_insertable
                                           : fo4::RecordNameStatus::updated;
      if (status != expected) {
        failures.push_back(std::string(name) + " insert " + target.key.to_string());
        continue;
      }
      const auto* record = fo4::find_record(plugin, target.key);
      if (target.refused) {
        if (record == nullptr || header_full_index(*record) != record->subrecords.size()) {
          failures.push_back(std::string(name) + " refused insert wrote a FULL " + target.key.to_string());
        }
        continue;
      }
      if (record == nullptr || signatures(*record) != target.order) {
        failures.push_back(std::string(name) + " order " + target.key.to_string() + " " +
                           target.signature.to_string());
      }
    }
  }
  EXPECT_GT(checked, 0u);
  for (const auto& failure : failures) ADD_FAILURE() << failure;
}
