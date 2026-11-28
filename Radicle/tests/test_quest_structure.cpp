#include "quest_structure_fixture.hpp"
#include "fo4/c_api_pet_write.h"
#include "fo4/c_api_quest.h"
#include "fo4/localization/strings.hpp"
#include "fo4/plugin/field_access.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/quest/structure.hpp"
#include "fo4/schema/scalar.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>
#include <string>
#include <string_view>
#include <utility>

namespace {

using namespace fo4::test;
using fo4::quest::QuestScope;

class Snapshot {
public:
  explicit Snapshot(const fo4::Plugin& native) {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("quest_structure_" + std::to_string(id)) / "Quests.esp";
    std::filesystem::create_directories(path_.parent_path());
    fo4::write_plugin_file(native, path_);
    radicle_plugin* raw = nullptr;
    if (radicle_plugin_read_file(path_.string().c_str(), &raw) != RADICLE_STATUS_OK) {
      throw std::runtime_error(radicle_last_error());
    }
    const auto status = radicle_quest_structure_read(raw, "000800:Quests.esp", &value_);
    radicle_plugin_destroy(raw);
    if (status != RADICLE_STATUS_OK) throw std::runtime_error(radicle_last_error());
  }
  ~Snapshot() {
    radicle_quest_structure_destroy(value_);
    std::error_code error;
    std::filesystem::remove_all(path_.parent_path(), error);
  }
  radicle_quest_structure_view view() const {
    radicle_quest_structure_view out{};
    EXPECT_EQ(radicle_quest_structure_get_view(value_, &out), RADICLE_STATUS_OK);
    return out;
  }
private:
  std::filesystem::path path_;
  radicle_quest_structure* value_{nullptr};
};

}

TEST(QuestStructure, StoredValuesAndScopedConditionsSurvivePluginRelease) {
  auto plugin = quest_structure_plugin();
  const auto native = fo4::quest::read_quest_structure(plugin, structure_quest_key());
  ASSERT_TRUE(native.stages_supported);
  ASSERT_TRUE(native.objectives_supported);
  ASSERT_TRUE(native.aliases_supported);
  ASSERT_TRUE(native.unsupported.empty());
  const Snapshot snapshot(plugin);
  const auto view = snapshot.view();
  EXPECT_STREQ(view.source_plugin, "Quests.esp");
  ASSERT_EQ(view.stage_count, 2u);
  EXPECT_EQ(view.stages[0].index, 10u);
  EXPECT_EQ(view.stages[0].flags, 2u);
  EXPECT_EQ(view.stages[0].opaque_byte, 7u);
  EXPECT_EQ(view.stages[0].is_startup_stage, 1u);
  EXPECT_EQ(view.stages[1].is_startup_stage, 0u);
  EXPECT_EQ(view.stages[0].log_count, 1u);
  EXPECT_EQ(view.stages[1].log_count, 0u);
  ASSERT_EQ(view.log_count, 1u);
  EXPECT_EQ(view.logs[0].flags, 1u);
  EXPECT_EQ(view.logs[0].text.kind, RADICLE_NAME_INLINE);
  EXPECT_EQ(view.logs[0].text.size, 3u);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(view.logs[0].text.bytes), 3), "Log");
  EXPECT_NE(view.logs[0].note, nullptr);
  EXPECT_EQ(view.logs[0].note_size, 0u);
  ASSERT_EQ(view.objective_count, 1u);
  EXPECT_EQ(view.objectives[0].index, 30u);
  EXPECT_EQ(view.objectives[0].flags, 2u);
  EXPECT_NE(view.objectives[0].text.bytes, nullptr);
  EXPECT_EQ(view.objectives[0].text.size, 0u);
  ASSERT_EQ(view.target_count, 1u);
  EXPECT_EQ(view.targets[0].alias, -1);
  EXPECT_EQ(view.targets[0].flags, 6u);
  EXPECT_STREQ(view.targets[0].keyword.plugin, "Quests.esp");
  EXPECT_EQ(view.targets[0].keyword.local_id, 0x800u);
  ASSERT_EQ(view.alias_count, 4u);
  EXPECT_EQ(view.aliases[0].kind, RADICLE_QUEST_ALIAS_REFERENCE);
  EXPECT_EQ(view.aliases[0].id, 1u);
  EXPECT_EQ(view.aliases[0].flags, 7u);
  EXPECT_EQ(view.aliases[0].force_into_alias, -3);
  EXPECT_EQ(view.aliases[0].fill_type, RADICLE_QUEST_FILL_SPECIFIC_REFERENCE);
  EXPECT_EQ(view.aliases[0].fill_supported, 1u);
  EXPECT_EQ(view.aliases[0].target.local_id, 0x800u);
  EXPECT_EQ(view.aliases[1].kind, RADICLE_QUEST_ALIAS_LOCATION);
  EXPECT_EQ(view.aliases[1].fill_type, RADICLE_QUEST_FILL_REFERENCE_ALIAS_LOCATION);
  EXPECT_EQ(view.aliases[1].fill_alias, 1);
  EXPECT_EQ(view.aliases[1].closest_to_alias, -2);
  EXPECT_EQ(view.aliases[1].secondary_target.local_id, 0x800u);
  EXPECT_EQ(view.aliases[2].kind, RADICLE_QUEST_ALIAS_COLLECTION);
  EXPECT_EQ(view.aliases[3].kind, RADICLE_QUEST_ALIAS_COLLECTION);
  ASSERT_EQ(view.collection_entry_count, 2u);
  EXPECT_EQ(view.aliases[2].first_collection_entry, 0u);
  EXPECT_EQ(view.aliases[2].collection_entry_count, 1u);
  EXPECT_EQ(view.aliases[3].first_collection_entry, 1u);
  EXPECT_EQ(view.aliases[3].collection_entry_count, 1u);
  EXPECT_EQ(view.collection_entries[0].id, 3u);
  EXPECT_EQ(view.collection_entries[0].max_initial_fill_count, 2u);
  EXPECT_EQ(view.collection_entries[1].id, 4u);
  EXPECT_EQ(view.collection_entries[1].has_max_initial_fill_count, 0u);
  ASSERT_EQ(view.condition_list_count, 8u);
  ASSERT_EQ(view.condition_count, 6u);
  const std::uint16_t functions[] = {70, 277, 72, 73, 74, 75};
  for (std::size_t i = 0; i < 6; ++i) {
    EXPECT_EQ(view.conditions[i].function, functions[i]);
    EXPECT_EQ(view.conditions[i].comparison_raw, 0x3f800000u);
    EXPECT_EQ(view.conditions[i].param1, -3);
    EXPECT_EQ(view.conditions[i].param2, 5);
    EXPECT_EQ(view.conditions[i].reference, 0x800u);
  }
  EXPECT_EQ(view.condition_lists[0].scope, RADICLE_QUEST_SCOPE_DIALOGUE);
  EXPECT_EQ(view.condition_lists[1].scope, RADICLE_QUEST_SCOPE_STORY_MANAGER);
  EXPECT_EQ(view.condition_lists[2].scope, RADICLE_QUEST_SCOPE_LOG_ENTRY);
  EXPECT_NE(view.conditions[2].parameter1_string, nullptr);
  EXPECT_EQ(view.conditions[2].parameter1_size, 0u);
  EXPECT_EQ(view.conditions[2].parameter2_size, 12u);
  const auto again = snapshot.view();
  EXPECT_EQ(again.stages, view.stages);
  EXPECT_EQ(again.conditions, view.conditions);
  EXPECT_EQ(again.source_plugin, view.source_plugin);
  EXPECT_EQ(view.unsupported_count, 0u);
}

// Constraint: detects a startup marker read from the wrong INDX flag bit, which would start a quest at its done, shutdown or keep-instance stage.
TEST(QuestStructure, StartupStageIsExactlyFlagBitTwo) {
  const std::pair<std::uint8_t, std::uint8_t> cases[] = {
      {0x00, 0}, {0x01, 0}, {0x02, 1}, {0x04, 0}, {0x08, 0}, {0x0D, 0}, {0x10, 0}, {0xF0, 0}, {0x0A, 1}, {0xFF, 1}};
  for (const auto& [flags, expected] : cases) {
    auto plugin = quest_structure_plugin();
    change_quest(plugin, [&](fo4::Record& record) {
      for (auto& member : record.subrecords) {
        if (member.signature == fo4::Signature::from_string("INDX")) member.data[2] = std::byte{flags};
      }
    });
    const Snapshot snapshot(plugin);
    const auto view = snapshot.view();
    ASSERT_EQ(view.stage_count, 2u);
    EXPECT_EQ(view.stages[0].flags, flags);
    EXPECT_EQ(view.stages[0].is_startup_stage, expected) << "flags " << static_cast<int>(flags);
    EXPECT_EQ(view.stages[1].flags, flags);
    EXPECT_EQ(view.stages[1].is_startup_stage, expected) << "flags " << static_cast<int>(flags);
  }
}

TEST(QuestStructure, LocalizedReferencesRemainIds) {
  const Snapshot snapshot(quest_structure_plugin(true));
  const auto view = snapshot.view();
  ASSERT_EQ(view.log_count, 1u);
  EXPECT_EQ(view.logs[0].text.kind, RADICLE_NAME_STRING_ID);
  EXPECT_EQ(view.logs[0].text.string_id, 0x1234u);
  EXPECT_EQ(view.logs[0].text.bytes, nullptr);
  ASSERT_EQ(view.objective_count, 1u);
  EXPECT_EQ(view.objectives[0].text.string_id, 0x2345u);
}

TEST(QuestStructure, OpaqueConditionPrefixesRemainSupportedAndByteExact) {
  auto plugin = quest_structure_plugin();
  change_quest(plugin, [](fo4::Record& record) {
    for (auto& member : record.subrecords) {
      if (member.signature != fo4::Signature::from_string("CTDA")) continue;
      member.data[1] = std::byte{0x12};
      member.data[2] = std::byte{0x34};
      member.data[3] = std::byte{0x56};
    }
  });
  const auto before = fo4::write_plugin(plugin);
  const auto native = fo4::quest::read_quest_structure(plugin, structure_quest_key());
  ASSERT_TRUE(native.unsupported.empty());
  std::size_t count = 0;
  for (const auto& list : native.condition_lists) {
    ASSERT_TRUE(list.supported);
    count += list.conditions.size();
    for (const auto& condition : list.conditions) {
      const auto payload = fo4::serialize_ctda_condition(condition);
      ASSERT_EQ(payload.size(), 32u);
      EXPECT_EQ(payload[1], std::byte{0x12});
      EXPECT_EQ(payload[2], std::byte{0x34});
      EXPECT_EQ(payload[3], std::byte{0x56});
    }
  }
  ASSERT_EQ(count, 6u);
  const Snapshot snapshot(plugin);
  const auto view = snapshot.view();
  ASSERT_EQ(view.unsupported_count, 0u);
  ASSERT_EQ(view.condition_count, 6u);
  const std::uint16_t functions[] = {70, 277, 72, 73, 74, 75};
  for (std::size_t i = 0; i < 6; ++i) {
    EXPECT_EQ(view.conditions[i].function, functions[i]);
    EXPECT_EQ(view.conditions[i].comparison_raw, 0x3f800000u);
    EXPECT_EQ(view.conditions[i].param1, -3);
    EXPECT_EQ(view.conditions[i].param2, 5);
    EXPECT_EQ(view.conditions[i].reference, 0x800u);
  }
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(QuestStructure, FailedConditionRunPublishesNoPartialValues) {
  auto plugin = quest_structure_plugin();
  change_quest(plugin, [](fo4::Record& record) {
    const auto at = std::find_if(record.subrecords.begin(), record.subrecords.end(), [](const auto& member) {
      return member.signature == fo4::Signature::from_string("NAM2");
    });
    record.subrecords.insert(at, quest_member("CTDA", {std::byte{0}}));
  });
  const auto native = fo4::quest::read_quest_structure(plugin, structure_quest_key());
  ASSERT_EQ(native.condition_lists.size(), 8u);
  EXPECT_FALSE(native.condition_lists[2].supported);
  EXPECT_TRUE(native.condition_lists[2].conditions.empty());
  EXPECT_TRUE(native.condition_lists[3].supported);
  EXPECT_FALSE(native.unsupported.empty());
}

TEST(QuestStructure, MissingDuplicateAndNonemptyBoundariesAreUnsupported) {
  for (const int mode : {0, 1, 2}) {
    auto plugin = quest_structure_plugin();
    change_quest(plugin, [&](fo4::Record& record) {
      const auto at = std::find_if(record.subrecords.begin(), record.subrecords.end(), [](const auto& member) {
        return member.signature == fo4::Signature::from_string("NEXT");
      });
      if (mode == 0) record.subrecords.erase(at);
      if (mode == 1) record.subrecords.insert(at, quest_member("NEXT"));
      if (mode == 2) at->data.push_back(std::byte{0});
    });
    const auto native = fo4::quest::read_quest_structure(plugin, structure_quest_key());
    EXPECT_FALSE(native.condition_lists[0].supported);
    EXPECT_FALSE(native.condition_lists[1].supported);
    EXPECT_TRUE(native.condition_lists[0].conditions.empty());
    EXPECT_TRUE(native.condition_lists[1].conditions.empty());
  }
}

TEST(QuestStructure, MultipleRunsAndOrphanStringsAreUnsupported) {
  for (const bool orphan : {false, true}) {
    auto plugin = quest_structure_plugin();
    change_quest(plugin, [&](fo4::Record& record) {
      record.subrecords.insert(record.subrecords.begin() + 1,
                               orphan ? quest_string("CIS1", "orphan") : quest_condition(71));
      if (!orphan) record.subrecords.insert(record.subrecords.begin() + 2, quest_member("ZZZZ"));
    });
    const auto native = fo4::quest::read_quest_structure(plugin, structure_quest_key());
    EXPECT_FALSE(native.condition_lists[0].supported);
    EXPECT_TRUE(native.condition_lists[0].conditions.empty());
    EXPECT_TRUE(native.condition_lists[1].supported);
  }
}

TEST(QuestStructure, OneFailedFamilyDoesNotHideOthers) {
  auto plugin = quest_structure_plugin();
  change_quest(plugin, [](fo4::Record& record) {
    for (auto& member : record.subrecords) {
      if (member.signature == fo4::Signature::from_string("INDX")) {
        member.data.pop_back();
        break;
      }
    }
  });
  const auto native = fo4::quest::read_quest_structure(plugin, structure_quest_key());
  EXPECT_FALSE(native.stages_supported);
  EXPECT_TRUE(native.stages.stages.empty());
  EXPECT_TRUE(native.objectives_supported);
  EXPECT_TRUE(native.aliases_supported);
  ASSERT_FALSE(native.unsupported.empty());
  EXPECT_EQ(native.unsupported[0].scope, QuestScope::stages);
}

TEST(QuestStructure, AliasConditionsAfterExtrasAreUnsupported) {
  for (const bool location : {false, true}) {
    auto plugin = quest_structure_plugin();
    change_quest(plugin, [&](fo4::Record& record) {
      const auto at = std::find_if(record.subrecords.begin(), record.subrecords.end(), [&](const auto& member) {
        return member.signature == fo4::Signature::from_string(location ? "ALCC" : "ALFR");
      });
      ASSERT_NE(at, record.subrecords.end());
      if (location) {
        std::iter_swap(at, at - 1);
      } else {
        record.subrecords.insert(at + 1, quest_u32("ALSP", 0x800));
      }
    });
    const auto native = fo4::quest::read_quest_structure(plugin, structure_quest_key());
    ASSERT_TRUE(native.aliases_supported);
    const auto index = location ? 5u : 4u;
    ASSERT_LT(index, native.condition_lists.size());
    EXPECT_FALSE(native.condition_lists[index].supported);
    EXPECT_TRUE(native.condition_lists[index].conditions.empty());
    EXPECT_FALSE(native.unsupported.empty());
  }
}

TEST(QuestStructure, AliasConditionsBeforeFillAreUnsupported) {
  auto plugin = quest_structure_plugin();
  change_quest(plugin, [](fo4::Record& record) {
    const auto at = std::find_if(record.subrecords.begin(), record.subrecords.end(), [](const auto& member) {
      return member.signature == fo4::Signature::from_string("ALFR");
    });
    ASSERT_NE(at, record.subrecords.end());
    std::iter_swap(at, at + 1);
  });
  const auto native = fo4::quest::read_quest_structure(plugin, structure_quest_key());
  ASSERT_TRUE(native.aliases_supported);
  ASSERT_LT(4u, native.condition_lists.size());
  EXPECT_FALSE(native.condition_lists[4].supported);
  EXPECT_TRUE(native.condition_lists[4].conditions.empty());
  EXPECT_FALSE(native.unsupported.empty());
}

TEST(QuestStructure, LevelIsOnlyProjectedForCreateReference) {
  using namespace fo4::forms;
  auto plugin = quest_structure_plugin();
  QuestAliasCreation creation;
  creation.kind = QuestAliasKind::location;
  creation.id = 6;
  creation.name = "fill";
  creation.flags = 4;
  creation.fill = QuestSpecificLocationFill{structure_quest_key()};
  auto alias = encode_quest_alias(plugin, creation);
  alias.subrecords.push_back(quest_u32("ALCL", 3));
  change_quest(plugin, [&](fo4::Record& record) {
    record.subrecords = {quest_string("EDID", "FillQuest"), quest_member("NEXT"), quest_u32("ANAM", 7)};
    record.subrecords.insert(record.subrecords.end(), alias.subrecords.begin(), alias.subrecords.end());
  });
  const Snapshot snapshot(plugin);
  const auto view = snapshot.view();
  ASSERT_EQ(view.alias_count, 1u);
  EXPECT_EQ(view.aliases[0].has_level, 0u);
  EXPECT_EQ(view.aliases[0].level, 0u);
}

TEST(QuestStructure, UnresolvedFillIsDistinctFromNullFill) {
  for (const bool unresolved : {false, true}) {
    auto plugin = quest_structure_plugin();
    change_quest(plugin, [&](fo4::Record& record) {
      for (auto& member : record.subrecords) {
        if (member.signature == fo4::Signature::from_string("ALFR")) {
          member = quest_u32("ALFR", unresolved ? 0x07000800 : 0);
        }
      }
    });
    const Snapshot snapshot(plugin);
    const auto view = snapshot.view();
    ASSERT_EQ(view.alias_count, 4u);
    EXPECT_EQ(view.aliases[0].target.plugin, nullptr);
    EXPECT_EQ(view.aliases[0].fill_supported, unresolved ? 0u : 1u);
    EXPECT_EQ(view.unsupported_count, unresolved ? 1u : 0u);
  }
}

TEST(QuestStructure, InvalidArgumentsClearOutputs) {
  auto* handle = reinterpret_cast<radicle_quest_structure*>(1);
  EXPECT_EQ(radicle_quest_structure_read(nullptr, nullptr, &handle), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(handle, nullptr);
  radicle_quest_structure_view view{};
  view.stage_count = 9;
  EXPECT_EQ(radicle_quest_structure_get_view(nullptr, &view), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(view.stage_count, 0u);
  EXPECT_EQ(view.stages, nullptr);
  radicle_quest_structure_destroy(nullptr);
}

TEST(QuestStructure, EveryStoredFillVariantUsesItsOwnFields) {
  using namespace fo4::forms;
  const auto key = structure_quest_key();
  const std::vector<std::pair<QuestAliasKind, QuestAliasFill>> fills = {
      {QuestAliasKind::reference, std::monostate{}},
      {QuestAliasKind::reference, QuestSpecificReferenceFill{key}},
      {QuestAliasKind::reference, QuestUniqueActorFill{key}},
      {QuestAliasKind::reference, QuestLocationAliasReferenceFill{-7, key}},
      {QuestAliasKind::reference, QuestExternalAliasReferenceFill{key, -7}},
      {QuestAliasKind::reference, QuestCreateReferenceFill{key, -7, 0x8000, 3}},
      {QuestAliasKind::reference, QuestFindMatchingReferenceFill{QuestMatchingReferenceNearAlias{-7, 1}}},
      {QuestAliasKind::reference, QuestFindMatchingReferenceFill{QuestMatchingReferenceEvent{0x12345678, 0x87654321}}},
      {QuestAliasKind::reference, QuestFindMatchingReferenceFill{QuestMatchingReferenceClosest{-7}}},
      {QuestAliasKind::location, QuestSpecificLocationFill{key}},
      {QuestAliasKind::location, QuestReferenceAliasLocationFill{-7, key}},
      {QuestAliasKind::location, QuestExternalAliasLocationFill{key, -7}},
      {QuestAliasKind::location, QuestFindMatchingLocationFill{0x12345678, 0x87654321}}};
  for (std::size_t i = 0; i < fills.size(); ++i) {
    SCOPED_TRACE(i);
    auto plugin = quest_structure_plugin();
    QuestAliasCreation creation;
    creation.kind = fills[i].first;
    creation.id = 6;
    creation.name = "fill";
    creation.flags = 4;
    creation.fill = fills[i].second;
    const auto alias = encode_quest_alias(plugin, creation);
    change_quest(plugin, [&](fo4::Record& record) {
      record.subrecords = {quest_string("EDID", "FillQuest"), quest_member("NEXT"), quest_u32("ANAM", 7)};
      record.subrecords.insert(record.subrecords.end(), alias.subrecords.begin(), alias.subrecords.end());
    });
    const Snapshot snapshot(plugin);
    const auto view = snapshot.view();
    ASSERT_EQ(view.alias_count, 1u);
    ASSERT_EQ(view.unsupported_count, 0u);
    const auto& row = view.aliases[0];
    EXPECT_EQ(static_cast<std::size_t>(row.fill_type), i);
    EXPECT_EQ(row.fill_supported, 1u);
    const bool target = i == 1 || i == 2 || i == 4 || i == 5 || i == 9 || i == 11;
    const bool secondary = i == 3 || i == 10;
    EXPECT_EQ(row.target.local_id, target ? 0x800u : 0u);
    EXPECT_EQ(row.target.plugin != nullptr, target);
    EXPECT_EQ(row.secondary_target.local_id, secondary ? 0x800u : 0u);
    EXPECT_EQ(row.secondary_target.plugin != nullptr, secondary);
    const bool has_alias = i == 3 || i == 4 || i == 5 || i == 6 || i == 8 || i == 10 || i == 11;
    EXPECT_EQ(row.has_fill_alias, has_alias);
    EXPECT_EQ(row.fill_alias, has_alias ? -7 : 0);
    EXPECT_EQ(row.has_level, i == 5);
    EXPECT_EQ(row.level, i == 5 ? 3u : 0u);
    EXPECT_EQ(row.create_mode, i == 5 ? 0x8000u : 0u);
    EXPECT_EQ(row.near_type, i == 6 ? 1u : 0u);
    EXPECT_EQ(row.event, i == 7 || i == 12 ? 0x12345678u : 0u);
    EXPECT_EQ(row.event_data, i == 7 || i == 12 ? 0x87654321u : 0u);
  }
}

TEST(QuestStructure, ObjectiveFlagsAfterTargetsDoNotBecomeDefaultFlags) {
  auto plugin = quest_structure_plugin();
  change_quest(plugin, [](fo4::Record& record) {
    const auto objective = std::find_if(record.subrecords.begin(), record.subrecords.end(), [](const auto& member) {
      return member.signature == fo4::Signature::from_string("QOBJ");
    });
    const auto flags = *(objective + 1);
    record.subrecords.erase(objective + 1);
    const auto target = std::find_if(record.subrecords.begin(), record.subrecords.end(), [](const auto& member) {
      return member.signature == fo4::Signature::from_string("QSTA");
    });
    record.subrecords.insert(target + 1, flags);
  });
  const auto native = fo4::quest::read_quest_structure(plugin, structure_quest_key());
  EXPECT_FALSE(native.objectives_supported);
  EXPECT_TRUE(native.objectives.objectives.empty());
}

TEST(QuestStructure, ObjectiveWithoutFlagsReadsAsZeroFlags) {
  auto plugin = quest_structure_plugin();
  change_quest(plugin, [](fo4::Record& record) {
    const auto flags = std::find_if(record.subrecords.begin(), record.subrecords.end(), [](const auto& member) {
      return member.signature == fo4::Signature::from_string("FNAM");
    });
    record.subrecords.erase(flags);
  });
  const auto native = fo4::quest::read_quest_structure(plugin, structure_quest_key());
  EXPECT_TRUE(native.objectives_supported);
  ASSERT_EQ(native.objectives.objectives.size(), 1u);
  EXPECT_EQ(native.objectives.objectives[0].flags, 0u);
  EXPECT_EQ(native.objectives.objectives[0].targets.size(), 1u);
}

TEST(QuestStructure, DuplicateObjectiveFlagsAreUnsupported) {
  auto plugin = quest_structure_plugin();
  change_quest(plugin, [](fo4::Record& record) {
    const auto flags = std::find_if(record.subrecords.begin(), record.subrecords.end(), [](const auto& member) {
      return member.signature == fo4::Signature::from_string("FNAM");
    });
    record.subrecords.insert(flags + 1, quest_u32("FNAM", 4));
  });
  const auto native = fo4::quest::read_quest_structure(plugin, structure_quest_key());
  EXPECT_FALSE(native.objectives_supported);
  EXPECT_TRUE(native.objectives.objectives.empty());
}

TEST(QuestStructure, EightByteTargetReadsBeforeFormVersion82) {
  auto plugin = quest_structure_plugin();
  change_quest(plugin, [](fo4::Record& record) {
    record.header.form_version = 81;
    for (auto& member : record.subrecords) {
      if (member.signature == fo4::Signature::from_string("QSTA")) member.data.resize(8);
    }
  });
  const auto native = fo4::quest::read_quest_structure(plugin, structure_quest_key());
  EXPECT_TRUE(native.objectives_supported);
  ASSERT_EQ(native.objectives.objectives.size(), 1u);
  ASSERT_EQ(native.objectives.objectives[0].targets.size(), 1u);
  const auto& target = native.objectives.objectives[0].targets[0];
  EXPECT_EQ(target.alias, -1);
  EXPECT_EQ(target.flags, 6u);
  EXPECT_FALSE(target.keyword.has_value());
  const Snapshot snapshot(plugin);
  const auto view = snapshot.view();
  EXPECT_EQ(view.objectives_supported, 1u);
  ASSERT_EQ(view.target_count, 1u);
  EXPECT_EQ(view.targets[0].alias, -1);
  EXPECT_EQ(view.targets[0].keyword.plugin, nullptr);
  EXPECT_EQ(view.unsupported_count, 0u);
}

TEST(QuestStructure, ObjectiveTargetWidthMustMatchFormVersion) {
  const std::pair<std::uint16_t, std::size_t> cases[] = {{81, 12}, {131, 8}, {81, 9}};
  for (const auto& test_case : cases) {
    auto plugin = quest_structure_plugin();
    change_quest(plugin, [&](fo4::Record& record) {
      record.header.form_version = test_case.first;
      for (auto& member : record.subrecords) {
        if (member.signature == fo4::Signature::from_string("QSTA")) member.data.resize(test_case.second);
      }
    });
    const auto native = fo4::quest::read_quest_structure(plugin, structure_quest_key());
    EXPECT_FALSE(native.objectives_supported) << test_case.first << "/" << test_case.second;
    EXPECT_TRUE(native.objectives.objectives.empty()) << test_case.first << "/" << test_case.second;
  }
}

TEST(QuestStructure, EightByteTargetKeepsItsWidthOnRewrite) {
  auto plugin = quest_structure_plugin();
  change_quest(plugin, [](fo4::Record& record) {
    record.header.form_version = 81;
    for (auto& member : record.subrecords) {
      if (member.signature == fo4::Signature::from_string("QSTA")) member.data.resize(8);
    }
  });
  auto objectives = fo4::forms::decode_quest_objectives(plugin, structure_quest_key());
  fo4::forms::write_quest_objectives(plugin, structure_quest_key(), objectives);
  const auto* record = fo4::find_record(plugin, structure_quest_key());
  ASSERT_NE(record, nullptr);
  std::size_t targets = 0;
  for (const auto& member : record->subrecords) {
    if (member.signature != fo4::Signature::from_string("QSTA")) continue;
    ++targets;
    EXPECT_EQ(member.data.size(), 8u);
  }
  EXPECT_EQ(targets, 1u);
  objectives.objectives[0].targets[0].keyword = structure_quest_key();
  EXPECT_THROW(fo4::forms::write_quest_objectives(plugin, structure_quest_key(), objectives),
               fo4::forms::QuestError);
}

TEST(QuestStructure, UnresolvedTargetKeywordIsReportedForThatTargetOnly) {
  auto plugin = quest_structure_plugin();
  change_quest(plugin, [](fo4::Record& record) {
    ByteWriter second;
    second.i32(-2);
    second.u32(1);
    second.u32(0x800);
    const auto target = std::find_if(record.subrecords.begin(), record.subrecords.end(), [](const auto& member) {
      return member.signature == fo4::Signature::from_string("QSTA");
    });
    target->data[11] = std::byte{0x07};
    record.subrecords.insert(target + 1, quest_member("QSTA", second.bytes));
  });
  const auto native = fo4::quest::read_quest_structure(plugin, structure_quest_key());
  EXPECT_TRUE(native.objectives_supported);
  EXPECT_TRUE(native.aliases_supported);
  ASSERT_EQ(native.objectives.objectives.size(), 1u);
  const auto& objective = native.objectives.objectives[0];
  EXPECT_EQ(objective.index, 30u);
  EXPECT_EQ(objective.flags, 2u);
  ASSERT_EQ(objective.targets.size(), 2u);
  EXPECT_FALSE(objective.targets[0].keyword.has_value());
  EXPECT_TRUE(objective.targets[0].keyword_unresolved);
  ASSERT_TRUE(objective.targets[1].keyword.has_value());
  EXPECT_EQ(objective.targets[1].keyword->to_string(), "000800:Quests.esp");
  EXPECT_FALSE(objective.targets[1].keyword_unresolved);
  ASSERT_EQ(native.unsupported.size(), 1u);
  EXPECT_EQ(native.unsupported[0].scope, QuestScope::objective_target);
  EXPECT_EQ(native.unsupported[0].primary, 0u);
  EXPECT_EQ(native.unsupported[0].secondary, 0u);
  EXPECT_EQ(native.unsupported[0].signature.to_string(), "QSTA");
  const Snapshot snapshot(plugin);
  const auto view = snapshot.view();
  EXPECT_EQ(view.objectives_supported, 1u);
  ASSERT_EQ(view.target_count, 2u);
  EXPECT_EQ(view.targets[0].keyword.plugin, nullptr);
  EXPECT_EQ(view.targets[1].keyword.local_id, 0x800u);
  ASSERT_EQ(view.unsupported_count, 1u);
  EXPECT_EQ(view.unsupported[0].scope, RADICLE_QUEST_SCOPE_OBJECTIVE_TARGET);
  EXPECT_EQ(view.unsupported[0].primary, 0u);
  EXPECT_EQ(view.unsupported[0].secondary, 0u);
  EXPECT_STREQ(view.unsupported[0].signature, "QSTA");
}

TEST(QuestStructure, UnresolvedTargetKeywordIsNotRewritten) {
  auto plugin = quest_structure_plugin();
  change_quest(plugin, [](fo4::Record& record) {
    const auto target = std::find_if(record.subrecords.begin(), record.subrecords.end(), [](const auto& member) {
      return member.signature == fo4::Signature::from_string("QSTA");
    });
    target->data[11] = std::byte{0x07};
  });
  const auto before = fo4::write_plugin(plugin);
  EXPECT_THROW(static_cast<void>(fo4::forms::reorder_quest_objective(plugin, structure_quest_key(), 30, 0)),
               fo4::forms::QuestError);
  EXPECT_EQ(fo4::write_plugin(plugin), before);
}

TEST(QuestStructure, RemovingObjectiveWithUnresolvedTargetKeywordSucceeds) {
  auto plugin = quest_structure_plugin();
  change_quest(plugin, [](fo4::Record& record) {
    const auto target = std::find_if(record.subrecords.begin(), record.subrecords.end(), [](const auto& member) {
      return member.signature == fo4::Signature::from_string("QSTA");
    });
    target->data[11] = std::byte{0x07};
  });
  EXPECT_TRUE(fo4::forms::remove_quest_objective(plugin, structure_quest_key(), 30));
  EXPECT_TRUE(fo4::forms::decode_quest_objectives(plugin, structure_quest_key()).objectives.empty());
}

TEST(QuestStructure, NextAliasIdRowReadsAndWritesTheStoredCounter) {
  auto plugin = quest_structure_plugin();
  const auto field = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("QUST"),
                                                          "QUST\\NextAliasID");
  ASSERT_TRUE(field.has_value());
  EXPECT_EQ(field->scalar.subrecord, fo4::Signature::from_string("ANAM"));
  EXPECT_EQ(field->scalar.storage, fo4::schema::ScalarStorage::u32);
  EXPECT_EQ(fo4::record_field_count(plugin, structure_quest_key(), *field), 1u);

  const auto expect_stored = [&](const fo4::Plugin& current, std::uint8_t expected) {
    const auto reread = fo4::read_plugin(fo4::write_plugin(current), "Quests.esp");
    const auto* record = fo4::find_record(reread, structure_quest_key());
    ASSERT_NE(record, nullptr);
    const auto& quest = *record;
    const auto bytes = std::find_if(quest.subrecords.begin(), quest.subrecords.end(),
                                    [](const auto& member) {
                                      return member.signature == fo4::Signature::from_string("ANAM");
                                    });
    ASSERT_NE(bytes, quest.subrecords.end());
    EXPECT_EQ(fo4::schema::scalar_field_bytes(bytes->data, field->scalar).size(), 4u);
    EXPECT_EQ(std::to_integer<std::uint8_t>(bytes->data[0]), expected);
    EXPECT_EQ(std::to_integer<std::uint8_t>(bytes->data[1]), 0);
    EXPECT_TRUE(fo4::quest::read_quest_structure(reread, structure_quest_key()).aliases_supported);
  };

  expect_stored(plugin, 5);
  EXPECT_EQ(fo4::set_record_scalar(plugin, structure_quest_key(), *field, "1"),
            fo4::ScalarMutationStatus::next_alias_id_too_low);
  EXPECT_EQ(fo4::set_record_scalar(plugin, structure_quest_key(), *field, "2"),
            fo4::ScalarMutationStatus::next_alias_id_too_low);
  expect_stored(plugin, 5);
  EXPECT_EQ(fo4::set_record_scalar(plugin, structure_quest_key(), *field, "3"),
            fo4::ScalarMutationStatus::updated);
  expect_stored(plugin, 3);

  EXPECT_EQ(fo4::set_record_scalar(plugin, structure_quest_key(), *field, "9"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, structure_quest_key(), *field, "9"),
            fo4::ScalarMutationStatus::unchanged);
  expect_stored(plugin, 9);
}

TEST(QuestStructure, MissingQuestReturnsNotFound) {
  auto plugin = quest_structure_plugin();
  const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto dir = std::filesystem::temp_directory_path() / ("quest_structure_missing_" + std::to_string(id));
  std::filesystem::create_directories(dir);
  const auto path = dir / "Quests.esp";
  fo4::write_plugin_file(plugin, path);
  radicle_plugin* raw = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(path.string().c_str(), &raw), RADICLE_STATUS_OK) << radicle_last_error();
  radicle_quest_structure* handle = nullptr;
  EXPECT_EQ(radicle_quest_structure_read(raw, "000801:Quests.esp", &handle), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(handle, nullptr);
  radicle_plugin_destroy(raw);
  std::error_code error;
  std::filesystem::remove_all(dir, error);
}

// Constraint: quest bulk must return the winning quest's stages and must read that quest's own start-game flag.
TEST(QuestStructure, BulkReturnsWinningStages) {
  auto plugin = quest_structure_plugin();
  const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto dir = std::filesystem::temp_directory_path() / ("quest_bulk_" + std::to_string(id));
  std::filesystem::create_directories(dir);
  const auto path = dir / "Quests.esp";
  fo4::write_plugin_file(plugin, path);
  const auto text = path.string();
  const char* paths[] = {text.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK) << radicle_last_error();
  radicle_quest_bulk_row* rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_quest_bulk(env, &rows, &count), RADICLE_STATUS_OK) << radicle_last_error();
  const radicle_quest_bulk_row* quest = nullptr;
  for (size_t index = 0; index < count; ++index) {
    if (rows[index].key.local_id == 0x800u) quest = &rows[index];
  }
  ASSERT_NE(quest, nullptr);
  EXPECT_STREQ(quest->key.plugin, "Quests.esp");
  EXPECT_EQ(quest->start_game_enabled, 0);
  radicle_quest_structure_view view{};
  ASSERT_EQ(radicle_quest_structure_get_view(quest->structure, &view), RADICLE_STATUS_OK);
  EXPECT_EQ(view.stage_count, 2u);
  EXPECT_EQ(view.objective_count, 1u);
  EXPECT_EQ(view.alias_count, 4u);
  radicle_env_quest_bulk_free(rows, count);
  radicle_env_destroy(env);
  std::error_code error;
  std::filesystem::remove_all(dir, error);
}

// Constraint: the start-game flag must be set when DNAM bit 0 is set.
TEST(QuestStructure, BulkReadsTheStartGameFlag) {
  using namespace fo4::test;
  ByteWriter out;
  const auto tes4 = begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x900);
  write_subrecord(out, "HEDR", hedr.bytes);
  end_record(out, tes4);
  const auto group = begin_group(out, "QUST", 0);
  const auto record = begin_record(out, "QUST", 0, 0x800);
  write_subrecord(out, "EDID", std::string_view("StartQuest\0", 11));
  ByteWriter dnam;
  dnam.u16(1);
  dnam.u16(0);
  dnam.u32(0);
  dnam.u32(0);
  write_subrecord(out, "DNAM", dnam.bytes);
  end_record(out, record);
  end_group(out, group);

  const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto dir = std::filesystem::temp_directory_path() / ("quest_bulk_flag_" + std::to_string(id));
  std::filesystem::create_directories(dir);
  const auto path = dir / "QuestFlag.esp";
  {
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(out.bytes.data()), static_cast<std::streamsize>(out.bytes.size()));
  }
  const auto text = path.string();
  const char* paths[] = {text.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 1, &env), RADICLE_STATUS_OK) << radicle_last_error();
  radicle_quest_bulk_row* rows = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_quest_bulk(env, &rows, &count), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(count, 1u);
  EXPECT_EQ(rows[0].start_game_enabled, 1);
  EXPECT_EQ(rows[0].key.local_id, 0x800u);
  radicle_env_quest_bulk_free(rows, count);
  radicle_env_destroy(env);
  std::error_code error;
  std::filesystem::remove_all(dir, error);
}

std::size_t list_size(const fo4::Plugin& plugin, std::string_view path) {
  std::size_t size = 0;
  EXPECT_EQ(fo4::record_list_size(plugin, structure_quest_key(), path, size), fo4::FieldStatus::ok) << path;
  return size;
}

void insert_element(fo4::Plugin& plugin, std::string_view path, std::size_t index) {
  ASSERT_EQ(fo4::insert_record_list_element(plugin, structure_quest_key(), path, index, nullptr),
            fo4::FieldStatus::ok) << path;
}

void write_field(fo4::Plugin& plugin, std::string_view path, std::size_t index, std::string_view text) {
  ASSERT_EQ(fo4::write_record_field(plugin, structure_quest_key(), path, index, text), fo4::FieldStatus::ok)
      << path << "[" << index << "]";
}

fo4::quest::QuestStructure read_structure(const fo4::Plugin& plugin) {
  auto structure = fo4::quest::read_quest_structure(plugin, structure_quest_key());
  EXPECT_TRUE(structure.stages_supported);
  EXPECT_TRUE(structure.objectives_supported);
  EXPECT_TRUE(structure.aliases_supported);
  EXPECT_TRUE(structure.unsupported.empty());
  return structure;
}

// Constraint: detects a generic QUST list write that splits a stage from its log entries, an objective from its targets, or an alias from its ALED, or that lands outside its own region, which the quest readers would then decode as a different quest.
TEST(QuestStructure, StagesObjectivesAndAliasesRoundTripThroughTheGenericPaths) {
  auto plugin = quest_structure_plugin();
  EXPECT_EQ(list_size(plugin, "QUST\\Stages"), 2u);
  EXPECT_EQ(list_size(plugin, "QUST\\LogEntries"), 1u);
  EXPECT_EQ(list_size(plugin, "QUST\\Objectives"), 1u);
  EXPECT_EQ(list_size(plugin, "QUST\\ObjectiveTargets"), 1u);
  EXPECT_EQ(list_size(plugin, "QUST\\Aliases"), 2u);
  EXPECT_EQ(list_size(plugin, "QUST\\CollectionEntries"), 2u);

  insert_element(plugin, "QUST\\Stages", 1);
  write_field(plugin, "QUST\\StageIndex", 1, "15");
  write_field(plugin, "QUST\\StageFlags", 1, "2");
  write_field(plugin, "QUST\\LogEntryFlags", 1, "3");
  write_field(plugin, "QUST\\LogEntryText", 1, "Reach the vault");
  {
    const auto structure = read_structure(plugin);
    ASSERT_EQ(structure.stages.stages.size(), 3u);
    EXPECT_EQ(structure.stages.stages[0].index, 10u);
    EXPECT_EQ(structure.stages.stages[0].log_entries.size(), 1u);
    EXPECT_EQ(structure.stages.stages[1].index, 15u);
    EXPECT_EQ(structure.stages.stages[1].flags, 2u);
    ASSERT_EQ(structure.stages.stages[1].log_entries.size(), 1u);
    EXPECT_EQ(structure.stages.stages[1].log_entries[0].flags, 3u);
    EXPECT_EQ(structure.stages.stages[2].index, 20u);
    EXPECT_TRUE(structure.stages.stages[2].log_entries.empty());
    EXPECT_EQ(structure.objectives.objectives.size(), 1u);
  }
  EXPECT_EQ(list_size(plugin, "QUST\\LogEntries"), 2u);

  ASSERT_EQ(fo4::remove_record_list_element(plugin, structure_quest_key(), "QUST\\Stages", 0), fo4::FieldStatus::ok);
  {
    const auto structure = read_structure(plugin);
    ASSERT_EQ(structure.stages.stages.size(), 2u);
    EXPECT_EQ(structure.stages.stages[0].index, 15u);
    EXPECT_EQ(structure.stages.stages[1].index, 20u);
    EXPECT_EQ(list_size(plugin, "QUST\\LogEntries"), 1u);
  }

  insert_element(plugin, "QUST\\Objectives", 1);
  write_field(plugin, "QUST\\ObjectiveIndex", 1, "31");
  write_field(plugin, "QUST\\ObjectiveFlags", 1, "1");
  insert_element(plugin, "QUST\\ObjectiveTargets", 1);
  write_field(plugin, "QUST\\TargetAlias", 1, "-1");
  write_field(plugin, "QUST\\TargetFlags", 1, "6");
  {
    const auto structure = read_structure(plugin);
    ASSERT_EQ(structure.objectives.objectives.size(), 2u);
    EXPECT_EQ(structure.objectives.objectives[1].index, 31u);
    EXPECT_EQ(structure.objectives.objectives[1].flags, 1u);
    EXPECT_TRUE(structure.objectives.objectives[1].targets.size() == 1u);
    EXPECT_EQ(structure.objectives.objectives[0].index, 30u);
    EXPECT_EQ(structure.objectives.objectives[0].targets.size(), 1u);
  }

  insert_element(plugin, "QUST\\Aliases", 2);
  write_field(plugin, "QUST\\ReferenceAliasID", 1, "9");
  write_field(plugin, "QUST\\AliasName", 2, "spare");
  write_field(plugin, "QUST\\NextAliasID", 0, "10");
  insert_element(plugin, "QUST\\CollectionEntries", 2);
  write_field(plugin, "QUST\\CollectionEntryID", 2, "5");
  {
    const auto structure = read_structure(plugin);
    ASSERT_EQ(structure.aliases.aliases.size(), 6u);
    EXPECT_EQ(structure.aliases.next_alias_id, 10u);
    EXPECT_EQ(structure.aliases.aliases[0].name, "ref");
    EXPECT_EQ(structure.aliases.aliases[1].name, "loc");
    EXPECT_EQ(structure.aliases.aliases[2].id, 9u);
    EXPECT_EQ(structure.aliases.aliases[2].name, "spare");
    EXPECT_EQ(structure.aliases.aliases[2].kind, fo4::forms::QuestAliasKind::reference);
    ASSERT_EQ(structure.aliases.aliases[3].kind, fo4::forms::QuestAliasKind::collection);
    ASSERT_EQ(structure.aliases.aliases[3].collection_entries.size(), 1u);
    EXPECT_EQ(structure.aliases.aliases[3].collection_entries[0].id, 3u);
    EXPECT_EQ(structure.aliases.aliases[3].collection_entries[0].max_initial_fill_count, 2);
    ASSERT_EQ(structure.aliases.aliases[4].collection_entries.size(), 1u);
    EXPECT_EQ(structure.aliases.aliases[4].collection_entries[0].id, 4u);
    ASSERT_EQ(structure.aliases.aliases[5].collection_entries.size(), 1u);
    EXPECT_EQ(structure.aliases.aliases[5].collection_entries[0].id, 5u);
  }

  ASSERT_EQ(fo4::remove_record_list_element(plugin, structure_quest_key(), "QUST\\Aliases", 0), fo4::FieldStatus::ok);
  ASSERT_EQ(fo4::remove_record_list_element(plugin, structure_quest_key(), "QUST\\CollectionEntries", 0),
            fo4::FieldStatus::ok);
  {
    const auto structure = read_structure(plugin);
    ASSERT_EQ(structure.aliases.aliases.size(), 4u);
    EXPECT_EQ(structure.aliases.aliases[0].name, "loc");
    EXPECT_EQ(structure.aliases.aliases[1].name, "spare");
    ASSERT_EQ(structure.aliases.aliases[2].collection_entries.size(), 1u);
    EXPECT_EQ(structure.aliases.aliases[2].collection_entries[0].id, 4u);
    ASSERT_EQ(structure.aliases.aliases[3].collection_entries.size(), 1u);
    EXPECT_EQ(structure.aliases.aliases[3].collection_entries[0].id, 5u);
    EXPECT_EQ(structure.stages.stages.size(), 2u);
    EXPECT_EQ(structure.objectives.objectives.size(), 2u);
  }
}

std::string quest_layout(const fo4::Plugin& plugin) {
  const auto* record = fo4::find_record(plugin, structure_quest_key());
  std::string out;
  for (const auto& subrecord : record->subrecords) {
    if (!out.empty()) out += ' ';
    out += subrecord.signature.to_string();
  }
  return out;
}

std::vector<std::byte> quest_stage_vmad_fragment() {
  ByteWriter vmad;
  vmad.u16(6);
  vmad.u16(2);
  vmad.u16(0);
  vmad.u8(3);
  vmad.u16(1);
  vmad.u16(0);
  vmad.u16(10);
  vmad.u16(0);
  vmad.u32(0);
  vmad.u8(0);
  vmad.u16(0);
  vmad.u16(0);
  vmad.u16(0);
  return vmad.bytes;
}

static std::string bytes_of(const uint8_t* data, size_t size);

std::string read_field(const fo4::Plugin& plugin, std::string_view path, std::size_t index) {
  const auto* record = fo4::find_record(plugin, structure_quest_key());
  std::string text;
  const auto status = fo4::read_record_field(plugin, *record, path, index, text);
  return status == fo4::FieldStatus::ok ? text : "status " + std::to_string(static_cast<int>(status));
}

std::string quest_target_widths(const fo4::Plugin& plugin) {
  const auto* record = fo4::find_record(plugin, structure_quest_key());
  std::string out;
  for (const auto& subrecord : record->subrecords) {
    if (subrecord.signature != fo4::Signature::from_string("QSTA")) continue;
    if (!out.empty()) out += ' ';
    out += std::to_string(subrecord.data.size());
  }
  return out;
}

void remove_element(fo4::Plugin& plugin, std::string_view path, std::size_t index) {
  ASSERT_EQ(fo4::remove_record_list_element(plugin, structure_quest_key(), path, index), fo4::FieldStatus::ok)
      << path << "[" << index << "]";
}

// Constraint: detects an 8-byte QSTA (form version below 82) that the target list refuses, or an inserted QSTA whose width disagrees with the record form version, which the objective decoder then reports as unsupported.
TEST(QuestStructure, ObjectiveTargetListsFollowTheFormVersionWidth) {
  auto old_plugin = quest_structure_plugin();
  change_quest(old_plugin, [](fo4::Record& record) {
    record.header.form_version = 81;
    for (auto& member : record.subrecords) {
      if (member.signature == fo4::Signature::from_string("QSTA")) member.data.resize(8);
    }
  });
  EXPECT_EQ(list_size(old_plugin, "QUST\\ObjectiveTargets"), 1u);
  EXPECT_EQ(read_field(old_plugin, "QUST\\TargetAlias", 0), "-1");
  EXPECT_EQ(read_field(old_plugin, "QUST\\TargetFlags", 0), "6");
  write_field(old_plugin, "QUST\\TargetFlags", 0, "7");
  EXPECT_EQ(read_field(old_plugin, "QUST\\TargetFlags", 0), "7");
  insert_element(old_plugin, "QUST\\ObjectiveTargets", 1);
  EXPECT_EQ(quest_target_widths(old_plugin), "8 8");
  write_field(old_plugin, "QUST\\TargetAlias", 1, "4");
  {
    const auto structure = fo4::quest::read_quest_structure(old_plugin, structure_quest_key());
    EXPECT_TRUE(structure.objectives_supported);
    ASSERT_EQ(structure.objectives.objectives.size(), 1u);
    ASSERT_EQ(structure.objectives.objectives[0].targets.size(), 2u);
    EXPECT_EQ(structure.objectives.objectives[0].targets[1].alias, 4);
  }
  remove_element(old_plugin, "QUST\\ObjectiveTargets", 0);
  EXPECT_EQ(quest_target_widths(old_plugin), "8");

  auto plugin = quest_structure_plugin();
  insert_element(plugin, "QUST\\ObjectiveTargets", 1);
  EXPECT_EQ(quest_target_widths(plugin), "12 12");
  EXPECT_EQ(read_structure(plugin).objectives.objectives[0].targets.size(), 2u);
}

// Constraint: detects a stops or closer break in the greedy span walk; removing the last stage must not swallow the objectives and aliases, removing an objective must not swallow the aliases, and an element must end at its closer even when no leader follows.
TEST(QuestStructure, GreedyStopsAndCloserBoundEveryRemoval) {
  {
    auto plugin = quest_structure_plugin();
    remove_element(plugin, "QUST\\Stages", 1);
    EXPECT_EQ(quest_layout(plugin),
              "EDID CTDA NEXT CTDA INDX QSDT CTDA CIS1 CIS2 NAM2 CNAM QOBJ FNAM NNAM QSTA CTDA ANAM "
              "ALST ALID FNAM ALFI ALFR CTDA ALED ALLS ALID FNAM ALFA KNAM CTDA ALCC ALED ALCS ALMI ALCS NNAM");
  }
  {
    auto plugin = quest_structure_plugin();
    remove_element(plugin, "QUST\\Objectives", 0);
    EXPECT_EQ(quest_layout(plugin),
              "EDID CTDA NEXT CTDA INDX QSDT CTDA CIS1 CIS2 NAM2 CNAM INDX ANAM "
              "ALST ALID FNAM ALFI ALFR CTDA ALED ALLS ALID FNAM ALFA KNAM CTDA ALCC ALED ALCS ALMI ALCS NNAM");
    remove_element(plugin, "QUST\\Aliases", 1);
    EXPECT_EQ(quest_layout(plugin),
              "EDID CTDA NEXT CTDA INDX QSDT CTDA CIS1 CIS2 NAM2 CNAM INDX ANAM "
              "ALST ALID FNAM ALFI ALFR CTDA ALED ALCS ALMI ALCS NNAM");
  }
  {
    auto plugin = quest_structure_plugin();
    change_quest(plugin, [](fo4::Record& record) {
      const auto first_end = std::find_if(record.subrecords.begin(), record.subrecords.end(), [](const fo4::Subrecord& sub) {
        return sub.signature == fo4::Signature::from_string("ALED");
      });
      record.subrecords.insert(first_end + 1, quest_condition(76));
    });
    ASSERT_EQ(list_size(plugin, "QUST\\Aliases"), 2u);
    remove_element(plugin, "QUST\\Aliases", 0);
    EXPECT_EQ(quest_layout(plugin),
              "EDID CTDA NEXT CTDA INDX QSDT CTDA CIS1 CIS2 NAM2 CNAM INDX QOBJ FNAM NNAM QSTA CTDA ANAM "
              "CTDA ALLS ALID FNAM ALFA KNAM CTDA ALCC ALED ALCS ALMI ALCS NNAM");
  }
}

// Constraint: detects a QUST without ANAM whose stage, log, objective or target removal swallows the alias and collection subrecords that follow, because the greedy walk had no alias leader to stop at.
TEST(QuestStructure, RemovalsWithoutNextAliasIdLeaveTheAliasesIntact) {
  const auto without_anam = [] {
    auto plugin = quest_structure_plugin();
    change_quest(plugin, [](fo4::Record& record) {
      const auto anam = std::find_if(record.subrecords.begin(), record.subrecords.end(), [](const fo4::Subrecord& sub) {
        return sub.signature == fo4::Signature::from_string("ANAM");
      });
      record.subrecords.erase(anam);
    });
    return plugin;
  };
  const std::string aliases = "ALST ALID FNAM ALFI ALFR CTDA ALED ALLS ALID FNAM ALFA KNAM CTDA ALCC ALED ALCS ALMI ALCS NNAM";
  {
    auto plugin = without_anam();
    remove_element(plugin, "QUST\\Objectives", 0);
    EXPECT_EQ(quest_layout(plugin), "EDID CTDA NEXT CTDA INDX QSDT CTDA CIS1 CIS2 NAM2 CNAM INDX " + aliases);
    EXPECT_EQ(list_size(plugin, "QUST\\Aliases"), 2u);
    EXPECT_EQ(list_size(plugin, "QUST\\CollectionEntries"), 2u);
  }
  {
    auto plugin = without_anam();
    remove_element(plugin, "QUST\\Stages", 1);
    EXPECT_EQ(quest_layout(plugin), "EDID CTDA NEXT CTDA INDX QSDT CTDA CIS1 CIS2 NAM2 CNAM QOBJ FNAM NNAM QSTA CTDA " + aliases);
    EXPECT_EQ(list_size(plugin, "QUST\\Aliases"), 2u);
  }
  {
    auto plugin = without_anam();
    remove_element(plugin, "QUST\\ObjectiveTargets", 0);
    EXPECT_EQ(quest_layout(plugin), "EDID CTDA NEXT CTDA INDX QSDT CTDA CIS1 CIS2 NAM2 CNAM INDX QOBJ FNAM NNAM " + aliases);
  }
  {
    auto plugin = without_anam();
    remove_element(plugin, "QUST\\LogEntries", 0);
    EXPECT_EQ(list_size(plugin, "QUST\\Aliases"), 2u);
    EXPECT_EQ(list_size(plugin, "QUST\\CollectionEntries"), 2u);
  }
}

// Constraint: detects alias rows that are addressed as if they were Aliases[i]; the rows count only their own signature across the whole record, so a collection alias or a location alias without ALID shifts the mapping.
TEST(QuestStructure, AliasRowsAreFlatPerSignatureCountsNotAliasesIndexes) {
  auto plugin = quest_structure_plugin();
  change_quest(plugin, [](fo4::Record& record) {
    const auto anam = std::find_if(record.subrecords.begin(), record.subrecords.end(), [](const fo4::Subrecord& sub) {
      return sub.signature == fo4::Signature::from_string("ANAM");
    });
    record.subrecords.erase(anam + 1, record.subrecords.end());
    const std::vector<fo4::Subrecord> aliases = {
        quest_u32("ALCS", 3), quest_u32("ALST", 1), quest_string("ALID", "ref"), quest_u32("FNAM", 7),
        quest_member("ALED"), quest_u32("ALLS", 2), quest_u32("FNAM", 9), quest_member("ALED"),
        quest_u32("ALST", 5), quest_string("ALID", "ref2"), quest_u32("FNAM", 7), quest_member("ALED"),
        quest_u32("ALCS", 4)};
    for (const auto& subrecord : aliases) record.subrecords.insert(record.subrecords.end(), subrecord);
  });
  EXPECT_EQ(list_size(plugin, "QUST\\Aliases"), 3u);
  EXPECT_EQ(list_size(plugin, "QUST\\CollectionEntries"), 2u);
  EXPECT_EQ(read_field(plugin, "QUST\\ReferenceAliasID", 0), "1");
  EXPECT_EQ(read_field(plugin, "QUST\\ReferenceAliasID", 1), "5");
  EXPECT_EQ(read_field(plugin, "QUST\\LocationAliasID", 0), "2");
  EXPECT_EQ(read_field(plugin, "QUST\\LocationAliasID", 1), "status 5");
  EXPECT_EQ(read_field(plugin, "QUST\\AliasName", 0), "ref");
  EXPECT_EQ(read_field(plugin, "QUST\\AliasName", 1), "ref2");
  EXPECT_EQ(read_field(plugin, "QUST\\AliasName", 2), "status 5");
  EXPECT_EQ(read_field(plugin, "QUST\\CollectionEntryID", 0), "3");
  EXPECT_EQ(read_field(plugin, "QUST\\CollectionEntryID", 1), "4");
  remove_element(plugin, "QUST\\Aliases", 1);
  EXPECT_EQ(read_field(plugin, "QUST\\LocationAliasID", 0), "status 5");
  EXPECT_EQ(read_field(plugin, "QUST\\ReferenceAliasID", 1), "5");
}

// Constraint: detects an alias insert that starts picking ids or raising ANAM on its own; the new alias is ALST 0 and ANAM stays, so the caller sets the id and then a NextAliasID above it.
TEST(QuestStructure, AliasInsertLeavesTheIdAndNextAliasIdToTheCaller) {
  auto plugin = quest_structure_plugin();
  insert_element(plugin, "QUST\\Aliases", 2);
  EXPECT_EQ(read_field(plugin, "QUST\\ReferenceAliasID", 1), "0");
  EXPECT_EQ(read_field(plugin, "QUST\\NextAliasID", 0), "5");
  write_field(plugin, "QUST\\ReferenceAliasID", 1, "9");
  EXPECT_EQ(fo4::write_record_field(plugin, structure_quest_key(), "QUST\\NextAliasID", 0, "9"),
            fo4::FieldStatus::next_alias_id_too_low);
  write_field(plugin, "QUST\\NextAliasID", 0, "10");
  EXPECT_EQ(read_structure(plugin).aliases.next_alias_id, 10u);
}

// Constraint: detects a collection entry insert that produces anything but the complete collection alias; a collection alias is one ALCS with an optional ALMI and no ALED, so a bare ALCS inserted into an empty group must decode as a one-entry collection alias.
TEST(QuestStructure, InsertingIntoAnEmptyCollectionCreatesTheCompleteAlias) {
  auto plugin = quest_structure_plugin();
  remove_element(plugin, "QUST\\CollectionEntries", 1);
  remove_element(plugin, "QUST\\CollectionEntries", 0);
  EXPECT_EQ(list_size(plugin, "QUST\\CollectionEntries"), 0u);
  EXPECT_EQ(read_structure(plugin).aliases.aliases.size(), 2u);
  insert_element(plugin, "QUST\\CollectionEntries", 0);
  write_field(plugin, "QUST\\CollectionEntryID", 0, "6");
  EXPECT_EQ(quest_layout(plugin),
            "EDID CTDA NEXT CTDA INDX QSDT CTDA CIS1 CIS2 NAM2 CNAM INDX QOBJ FNAM NNAM QSTA CTDA ANAM "
            "ALCS ALST ALID FNAM ALFI ALFR CTDA ALED ALLS ALID FNAM ALFA KNAM CTDA ALCC ALED NNAM");
  const auto structure = read_structure(plugin);
  ASSERT_EQ(structure.aliases.aliases.size(), 3u);
  ASSERT_EQ(structure.aliases.aliases[0].kind, fo4::forms::QuestAliasKind::collection);
  ASSERT_EQ(structure.aliases.aliases[0].collection_entries.size(), 1u);
  EXPECT_EQ(structure.aliases.aliases[0].collection_entries[0].id, 6u);
}

// Constraint: detects a log entry insert that is silently treated as parent-addressed; the index is positional across the whole record, so an append lands in the last stage (empty or not) and an insert before entry j lands in the stage that owns entry j.
TEST(QuestStructure, LogEntryInsertIsPositionalNotAddressedToAStage) {
  auto plugin = quest_structure_plugin();
  insert_element(plugin, "QUST\\LogEntries", 1);
  EXPECT_EQ(quest_layout(plugin),
            "EDID CTDA NEXT CTDA INDX QSDT CTDA CIS1 CIS2 NAM2 CNAM INDX QSDT CNAM QOBJ FNAM NNAM QSTA CTDA ANAM "
            "ALST ALID FNAM ALFI ALFR CTDA ALED ALLS ALID FNAM ALFA KNAM CTDA ALCC ALED ALCS ALMI ALCS NNAM");
  insert_element(plugin, "QUST\\LogEntries", 0);
  EXPECT_EQ(quest_layout(plugin),
            "EDID CTDA NEXT CTDA INDX QSDT CNAM QSDT CTDA CIS1 CIS2 NAM2 CNAM INDX QSDT CNAM QOBJ FNAM NNAM QSTA CTDA ANAM "
            "ALST ALID FNAM ALFI ALFR CTDA ALED ALLS ALID FNAM ALFA KNAM CTDA ALCC ALED ALCS ALMI ALCS NNAM");
  EXPECT_EQ(list_size(plugin, "QUST\\Stages"), 2u);
  EXPECT_EQ(list_size(plugin, "QUST\\LogEntries"), 3u);
}

const std::string k_quest_aliases =
    "ALST ALID FNAM ALFI ALFR CTDA ALED ALLS ALID FNAM ALFA KNAM CTDA ALCC ALED ALCS ALMI ALCS NNAM";
const std::string k_quest_head = "EDID CTDA NEXT CTDA ";
const std::string k_quest_stage0 = "INDX QSDT CTDA CIS1 CIS2 NAM2 CNAM ";

fo4::FieldStatus insert_status(fo4::Plugin& plugin, std::string_view path, std::size_t index) {
  return fo4::insert_record_list_element(plugin, structure_quest_key(), path, index, nullptr);
}

fo4::FieldStatus size_status(const fo4::Plugin& plugin, std::string_view path) {
  std::size_t size = 0;
  return fo4::record_list_size(plugin, structure_quest_key(), path, size);
}

// Constraint: detects a Stages[i]\LogEntries insert, remove or size that ignores the stage index, so an entry lands in or is taken from another stage, or an empty stage's entry falls behind the following stages.
TEST(QuestStructure, NestedLogEntriesAreAddressedByTheirStage) {
  auto plugin = quest_structure_plugin();
  const std::string tail = "QOBJ FNAM NNAM QSTA CTDA ANAM " + k_quest_aliases;
  EXPECT_EQ(list_size(plugin, "QUST\\Stages[0]\\LogEntries"), 1u);
  EXPECT_EQ(list_size(plugin, "QUST\\Stages[1]\\LogEntries"), 0u);

  insert_element(plugin, "QUST\\Stages[0]\\LogEntries", 1);
  EXPECT_EQ(quest_layout(plugin), k_quest_head + k_quest_stage0 + "QSDT CNAM INDX " + tail);
  write_field(plugin, "QUST\\LogEntryFlags", 1, "3");
  insert_element(plugin, "QUST\\Stages[1]\\LogEntries", 0);
  EXPECT_EQ(quest_layout(plugin), k_quest_head + k_quest_stage0 + "QSDT CNAM INDX QSDT CNAM " + tail);
  EXPECT_EQ(list_size(plugin, "QUST\\Stages[0]\\LogEntries"), 2u);
  EXPECT_EQ(list_size(plugin, "QUST\\Stages[1]\\LogEntries"), 1u);
  EXPECT_EQ(list_size(plugin, "QUST\\LogEntries"), 3u);
  EXPECT_EQ(read_field(plugin, "QUST\\Stages[0]\\LogEntries", 1), "0300");
  EXPECT_EQ(read_field(plugin, "QUST\\Stages[1]\\LogEntries", 0), "0000");
  EXPECT_EQ(read_field(plugin, "QUST\\Stages[1]\\LogEntries", 1), "status 6");
  {
    const auto structure = read_structure(plugin);
    ASSERT_EQ(structure.stages.stages.size(), 2u);
    ASSERT_EQ(structure.stages.stages[0].log_entries.size(), 2u);
    EXPECT_EQ(structure.stages.stages[0].log_entries[1].flags, 3u);
    EXPECT_EQ(structure.stages.stages[1].log_entries.size(), 1u);
  }

  remove_element(plugin, "QUST\\Stages[0]\\LogEntries", 0);
  remove_element(plugin, "QUST\\Stages[0]\\LogEntries", 0);
  EXPECT_EQ(quest_layout(plugin), k_quest_head + "INDX INDX QSDT CNAM " + tail);
  EXPECT_EQ(list_size(plugin, "QUST\\Stages[0]\\LogEntries"), 0u);
  insert_element(plugin, "QUST\\Stages[0]\\LogEntries", 0);
  EXPECT_EQ(quest_layout(plugin), k_quest_head + "INDX QSDT CNAM INDX QSDT CNAM " + tail);
  EXPECT_EQ(read_structure(plugin).stages.stages[0].log_entries.size(), 1u);

  EXPECT_EQ(insert_status(plugin, "QUST\\Stages[2]\\LogEntries", 0), fo4::FieldStatus::index_out_of_range);
  EXPECT_EQ(insert_status(plugin, "QUST\\Stages[0]\\LogEntries", 2), fo4::FieldStatus::index_out_of_range);
  EXPECT_EQ(size_status(plugin, "QUST\\Stages[2]\\LogEntries"), fo4::FieldStatus::index_out_of_range);
  EXPECT_EQ(fo4::remove_record_list_element(plugin, structure_quest_key(), "QUST\\Stages[1]\\LogEntries", 1),
            fo4::FieldStatus::index_out_of_range);
  EXPECT_EQ(read_field(plugin, "QUST\\Stages[2]\\LogEntries", 0), "status 6");
  for (const std::string_view path : {"QUST\\Stages[x]\\LogEntries", "QUST\\Stages[]\\LogEntries", "QUST\\Stages[0]",
                                      "QUST\\Stages[0]\\", "QUST\\Stages[0]\\Aliases",
                                      "QUST\\Objectives[0]\\LogEntries", "QUST\\Stages[0]\\LogEntries[0]",
                                      "QUST\\Stages[0]\\LogEntryFlags", "QUST\\Stages[1234567890]\\LogEntries",
                                      "QUST\\Stages[0]\\\\LogEntries"}) {
    EXPECT_EQ(insert_status(plugin, path, 0), fo4::FieldStatus::unknown_path) << path;
    EXPECT_EQ(size_status(plugin, path), fo4::FieldStatus::unknown_path) << path;
  }
  EXPECT_EQ(quest_layout(plugin), k_quest_head + "INDX QSDT CNAM INDX QSDT CNAM " + tail);
}

// Constraint: detects a Stages[i]\LogEntries element write or append that lands in another stage, splits a variable-length CNAM wrongly, or leaves a layout the stage readers decode differently.
TEST(QuestStructure, NestedLogEntryElementsAreWrittenAndAppendedInTheirStage) {
  auto plugin = quest_structure_plugin();
  const std::string tail = "QOBJ FNAM NNAM QSTA CTDA ANAM " + k_quest_aliases;
  write_field(plugin, "QUST\\Stages[1]\\LogEntries", 0, "0500");
  EXPECT_EQ(quest_layout(plugin), k_quest_head + k_quest_stage0 + "INDX QSDT CNAM " + tail);
  EXPECT_EQ(list_size(plugin, "QUST\\Stages[0]\\LogEntries"), 1u);
  EXPECT_EQ(list_size(plugin, "QUST\\Stages[1]\\LogEntries"), 1u);
  EXPECT_EQ(read_field(plugin, "QUST\\Stages[1]\\LogEntries", 0), "0500");

  write_field(plugin, "QUST\\Stages[1]\\LogEntries", 0, "03686900");
  EXPECT_EQ(read_field(plugin, "QUST\\Stages[1]\\LogEntries", 0), "03686900");
  EXPECT_EQ(list_size(plugin, "QUST\\Stages[1]\\LogEntries"), 1u);
  write_field(plugin, "QUST\\Stages[1]\\LogEntries", 1, "0100");
  EXPECT_EQ(quest_layout(plugin), k_quest_head + k_quest_stage0 + "INDX QSDT CNAM QSDT CNAM " + tail);
  EXPECT_EQ(list_size(plugin, "QUST\\Stages[0]\\LogEntries"), 1u);
  EXPECT_EQ(list_size(plugin, "QUST\\Stages[1]\\LogEntries"), 2u);
  const auto structure = read_structure(plugin);
  ASSERT_EQ(structure.stages.stages.size(), 2u);
  EXPECT_EQ(structure.stages.stages[1].log_entries.size(), 2u);
  EXPECT_EQ(structure.stages.stages[1].log_entries[0].flags, 3u);
  EXPECT_EQ(structure.stages.stages[1].log_entries[1].flags, 1u);
  EXPECT_THROW(fo4::write_record_field(plugin, structure_quest_key(), "QUST\\Stages[1]\\LogEntries", 0, ""),
               fo4::schema::SubrecordGroupError);
  EXPECT_EQ(fo4::write_record_field(plugin, structure_quest_key(), "QUST\\Stages[5]\\LogEntries", 0, "0500"),
            fo4::FieldStatus::index_out_of_range);
  EXPECT_EQ(read_field(plugin, "QUST\\Stages[1]\\LogEntries", 0), "03686900");
}

// Constraint: detects a scoped read that resolves its parent element from the plugin's record with the same FormKey instead of the record it was given.
TEST(QuestStructure, ScopedReadsUseTheGivenRecordNotTheOneWithTheSameKey) {
  auto edited = quest_structure_plugin();
  insert_element(edited, "QUST\\Stages", 0);
  const auto& given = *fo4::find_record(edited, structure_quest_key());
  const auto other = quest_structure_plugin();
  for (const std::string_view path : {"QUST\\Stages[0]\\LogEntries", "QUST\\Stages[1]\\LogEntries"}) {
    for (std::size_t index = 0; index < 2; ++index) {
      std::string expected;
      std::string actual;
      const auto want = fo4::read_record_field(edited, given, path, index, expected);
      const auto got = fo4::read_record_field(other, given, path, index, actual);
      EXPECT_EQ(got, want) << path << " " << index;
      EXPECT_EQ(actual, expected) << path << " " << index;
    }
  }
}

// Constraint: detects an Objectives[i]\ObjectiveTargets insert or remove that lands in the wrong objective, or an 8-byte target list that is given the 12-byte width, which the objective decoder then reports as unsupported.
TEST(QuestStructure, NestedObjectiveTargetsAreAddressedByTheirObjective) {
  auto plugin = quest_structure_plugin();
  insert_element(plugin, "QUST\\Objectives", 1);
  const std::string first = k_quest_head + k_quest_stage0 + "INDX QOBJ FNAM NNAM QSTA CTDA ";
  const std::string second = "QOBJ FNAM NNAM ";
  EXPECT_EQ(quest_layout(plugin), first + second + "ANAM " + k_quest_aliases);
  {
    auto flat = plugin;
    insert_element(flat, "QUST\\ObjectiveTargets", 1);
    EXPECT_EQ(quest_layout(flat), first + second + "QSTA ANAM " + k_quest_aliases);
  }

  insert_element(plugin, "QUST\\Objectives[0]\\ObjectiveTargets", 1);
  EXPECT_EQ(quest_layout(plugin), first + "QSTA " + second + "ANAM " + k_quest_aliases);
  insert_element(plugin, "QUST\\Objectives[1]\\ObjectiveTargets", 0);
  EXPECT_EQ(quest_layout(plugin), first + "QSTA " + second + "QSTA ANAM " + k_quest_aliases);
  EXPECT_EQ(list_size(plugin, "QUST\\Objectives[0]\\ObjectiveTargets"), 2u);
  EXPECT_EQ(list_size(plugin, "QUST\\Objectives[1]\\ObjectiveTargets"), 1u);
  EXPECT_EQ(list_size(plugin, "QUST\\ObjectiveTargets"), 3u);
  write_field(plugin, "QUST\\TargetAlias", 1, "4");
  write_field(plugin, "QUST\\TargetAlias", 2, "5");
  {
    const auto structure = read_structure(plugin);
    ASSERT_EQ(structure.objectives.objectives.size(), 2u);
    ASSERT_EQ(structure.objectives.objectives[0].targets.size(), 2u);
    EXPECT_EQ(structure.objectives.objectives[0].targets[1].alias, 4);
    ASSERT_EQ(structure.objectives.objectives[1].targets.size(), 1u);
    EXPECT_EQ(structure.objectives.objectives[1].targets[0].alias, 5);
  }

  remove_element(plugin, "QUST\\Objectives[0]\\ObjectiveTargets", 0);
  EXPECT_EQ(quest_layout(plugin), k_quest_head + k_quest_stage0 + "INDX QOBJ FNAM NNAM QSTA " + second +
                                      "QSTA ANAM " + k_quest_aliases);
  EXPECT_EQ(read_structure(plugin).objectives.objectives[0].targets[0].alias, 4);
  EXPECT_EQ(insert_status(plugin, "QUST\\Objectives[2]\\ObjectiveTargets", 0), fo4::FieldStatus::index_out_of_range);

  auto old_plugin = quest_structure_plugin();
  change_quest(old_plugin, [](fo4::Record& record) {
    record.header.form_version = 81;
    for (auto& member : record.subrecords) {
      if (member.signature == fo4::Signature::from_string("QSTA")) member.data.resize(8);
    }
  });
  insert_element(old_plugin, "QUST\\Objectives[0]\\ObjectiveTargets", 1);
  EXPECT_EQ(quest_target_widths(old_plugin), "8 8");
  EXPECT_EQ(read_structure(old_plugin).objectives.objectives[0].targets.size(), 2u);
}

// Constraint: detects a nested insert or remove on a QUST without ANAM that runs past its parent element into the alias subrecords, which the alias decoder would then read as a different quest.
TEST(QuestStructure, NestedInsertsWithoutNextAliasIdStayInsideTheirParent) {
  auto plugin = quest_structure_plugin();
  change_quest(plugin, [](fo4::Record& record) {
    const auto anam = std::find_if(record.subrecords.begin(), record.subrecords.end(), [](const fo4::Subrecord& sub) {
      return sub.signature == fo4::Signature::from_string("ANAM");
    });
    record.subrecords.erase(anam);
  });
  insert_element(plugin, "QUST\\Objectives[0]\\ObjectiveTargets", 1);
  EXPECT_EQ(quest_layout(plugin), k_quest_head + k_quest_stage0 + "INDX QOBJ FNAM NNAM QSTA CTDA QSTA " + k_quest_aliases);
  remove_element(plugin, "QUST\\Objectives[0]\\ObjectiveTargets", 1);
  remove_element(plugin, "QUST\\Objectives[0]\\ObjectiveTargets", 0);
  EXPECT_EQ(quest_layout(plugin), k_quest_head + k_quest_stage0 + "INDX QOBJ FNAM NNAM " + k_quest_aliases);
  insert_element(plugin, "QUST\\Objectives[0]\\ObjectiveTargets", 0);
  EXPECT_EQ(quest_layout(plugin), k_quest_head + k_quest_stage0 + "INDX QOBJ FNAM NNAM QSTA " + k_quest_aliases);
  insert_element(plugin, "QUST\\Stages[1]\\LogEntries", 0);
  EXPECT_EQ(quest_layout(plugin),
            k_quest_head + k_quest_stage0 + "INDX QSDT CNAM QOBJ FNAM NNAM QSTA " + k_quest_aliases);
  remove_element(plugin, "QUST\\Stages[1]\\LogEntries", 0);
  EXPECT_EQ(list_size(plugin, "QUST\\Aliases"), 2u);
  EXPECT_EQ(list_size(plugin, "QUST\\CollectionEntries"), 2u);
}

// Constraint: detects a member clear that ignores the parent index and strips the entry of another stage, or a scoped walk accepted for a group that has no parent.
TEST(QuestStructure, NestedGroupMemberClearUsesTheParentRelativeIndex) {
  auto plugin = quest_structure_plugin();
  insert_element(plugin, "QUST\\Stages[1]\\LogEntries", 0);
  fo4::ScopedGroupPath scoped;
  ASSERT_EQ(fo4::resolve_group_path(plugin, structure_quest_key(), "QUST\\Stages[1]\\LogEntries", scoped),
            fo4::FieldStatus::ok);
  ASSERT_TRUE(scoped.within.has_value());
  EXPECT_EQ(fo4::set_record_subrecord_group_member(plugin, structure_quest_key(), scoped.group, 0,
                                                   fo4::Signature::from_string("CNAM"), false, &*scoped.within),
            fo4::SubrecordGroupMutationStatus::updated);
  EXPECT_EQ(quest_layout(plugin),
            k_quest_head + k_quest_stage0 + "INDX QSDT QOBJ FNAM NNAM QSTA CTDA ANAM " + k_quest_aliases);

  fo4::ScopedGroupPath flat;
  EXPECT_EQ(fo4::resolve_group_path(plugin, structure_quest_key(), "QUST\\LogEntries", flat), fo4::FieldStatus::ok);
  EXPECT_FALSE(flat.within.has_value());
  EXPECT_EQ(fo4::resolve_group_path(plugin, structure_quest_key(), "QUST\\Stages[2]\\LogEntries", flat),
            fo4::FieldStatus::index_out_of_range);
  EXPECT_EQ(fo4::resolve_group_path(plugin, structure_quest_key(), "QUST\\Stages[0]\\Aliases", flat),
            fo4::FieldStatus::unknown_path);
  const auto aliases = fo4::schema::subrecord_group_descriptor(fo4::Signature::from_string("QUST"), "QUST\\Aliases");
  ASSERT_TRUE(aliases.has_value());
  EXPECT_THROW(fo4::subrecord_group_spans(plugin, structure_quest_key(), *aliases, &*scoped.within),
               fo4::schema::SubrecordGroupError);
}

// Constraint: detects a parent-addressed path that works on the C++ accessors but is refused or mis-statused by the existing C list and group-member exports, which are the only entry points the engine calls.
TEST(QuestStructure, ParentAddressedMemberSetReportsBadRecordsAsInvalidArgument) {
  const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto dir = std::filesystem::temp_directory_path() / ("quest_nested_bad_" + std::to_string(id));
  std::filesystem::create_directories(dir);
  const radicle_form_key key{"Quests.esp", 0x800};
  const auto set_status = [&](const char* name, const std::function<void(fo4::Record&)>& change, const char* path) {
    auto value = quest_structure_plugin();
    change_quest(value, change);
    std::filesystem::create_directories(dir / name);
    const auto file = dir / name / "Quests.esp";
    fo4::write_plugin_file(value, file);
    radicle_plugin* plugin = nullptr;
    EXPECT_EQ(radicle_plugin_read_file(file.string().c_str(), &plugin), RADICLE_STATUS_OK) << radicle_last_error();
    const auto status = radicle_plugin_subrecord_group_member_set(plugin, key, path, 0, "CNAM", 0);
    radicle_plugin_destroy(plugin);
    return status;
  };
  const char* scoped = "QUST\\Stages[0]\\LogEntries";
  EXPECT_EQ(set_status("intact", [](fo4::Record&) {}, scoped), RADICLE_STATUS_OK) << radicle_last_error();
  const auto wrong_record = [](fo4::Record& record) { record.header.signature = fo4::Signature::from_string("KYWD"); };
  EXPECT_EQ(set_status("wrong", wrong_record, scoped), RADICLE_STATUS_INVALID_ARGUMENT);
  const auto short_index = [](fo4::Record& record) {
    for (auto& member : record.subrecords) {
      if (member.signature == fo4::Signature::from_string("INDX")) member.data.resize(3);
    }
  };
  EXPECT_EQ(set_status("short", short_index, scoped), RADICLE_STATUS_INVALID_ARGUMENT);
  std::error_code error;
  std::filesystem::remove_all(dir, error);
}

TEST(QuestStructure, ParentAddressedPathsWorkThroughTheExistingCExports) {
  const auto plugin_value = quest_structure_plugin();
  const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto dir = std::filesystem::temp_directory_path() / ("quest_nested_c_" + std::to_string(id));
  std::filesystem::create_directories(dir);
  const auto path = dir / "Quests.esp";
  fo4::write_plugin_file(plugin_value, path);
  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(path.string().c_str(), &plugin), RADICLE_STATUS_OK) << radicle_last_error();
  const radicle_form_key key{"Quests.esp", 0x800};
  size_t size = 99;
  EXPECT_EQ(radicle_plugin_list_size(plugin, key, "QUST\\Stages[0]\\LogEntries", &size), RADICLE_STATUS_OK);
  EXPECT_EQ(size, 1u);
  EXPECT_EQ(radicle_plugin_list_insert(plugin, key, "QUST\\Stages[1]\\LogEntries", 0, nullptr), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(radicle_plugin_list_size(plugin, key, "QUST\\Stages[1]\\LogEntries", &size), RADICLE_STATUS_OK);
  EXPECT_EQ(size, 1u);
  EXPECT_EQ(radicle_plugin_list_size(plugin, key, "QUST\\Stages[0]\\LogEntries", &size), RADICLE_STATUS_OK);
  EXPECT_EQ(size, 1u);
  EXPECT_EQ(radicle_plugin_subrecord_group_member_set(plugin, key, "QUST\\Stages[1]\\LogEntries", 0, "CNAM", 0),
            RADICLE_STATUS_OK) << radicle_last_error();
  {
    const auto saved_path = dir / "Saved.esp";
    ASSERT_EQ(radicle_plugin_write_file(plugin, saved_path.string().c_str()), RADICLE_STATUS_OK) << radicle_last_error();
    const auto saved = fo4::read_plugin_file(saved_path);
    const auto* record = fo4::find_record(saved, fo4::FormKey::parse("000800:Saved.esp"));
    ASSERT_NE(record, nullptr);
    std::string layout;
    for (const auto& subrecord : record->subrecords) layout += subrecord.signature.to_string() + ' ';
    EXPECT_EQ(layout, k_quest_head + k_quest_stage0 + "INDX QSDT QOBJ FNAM NNAM QSTA CTDA ANAM " + k_quest_aliases + " ");
  }
  EXPECT_EQ(radicle_plugin_list_remove(plugin, key, "QUST\\Stages[1]\\LogEntries", 0), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(radicle_plugin_list_size(plugin, key, "QUST\\Stages[1]\\LogEntries", &size), RADICLE_STATUS_OK);
  EXPECT_EQ(size, 0u);
  EXPECT_EQ(radicle_plugin_list_insert(plugin, key, "QUST\\Stages[2]\\LogEntries", 0, nullptr), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_subrecord_group_member_set(plugin, key, "QUST\\Stages[2]\\LogEntries", 0, "CNAM", 0),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_list_insert(plugin, key, "QUST\\Stages[x]\\LogEntries", 0, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_subrecord_group_member_set(plugin, key, "QUST\\Stages[0]\\Aliases", 0, "CNAM", 0),
            RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_plugin_destroy(plugin);
  std::error_code error;
  std::filesystem::remove_all(dir, error);
}

namespace {

radicle_form_key quest_form_key() { return radicle_form_key{"Quests.esp", 0x800u}; }

class EditablePlugin {
public:
  explicit EditablePlugin(const fo4::Plugin& native) {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    dir_ = std::filesystem::temp_directory_path() / ("quest_edit_" + std::to_string(id));
    std::filesystem::create_directories(dir_);
    const auto path = dir_ / "Quests.esp";
    fo4::write_plugin_file(native, path);
    if (radicle_plugin_read_file(path.string().c_str(), &plugin_) != RADICLE_STATUS_OK) {
      throw std::runtime_error(radicle_last_error());
    }
  }
  ~EditablePlugin() {
    radicle_plugin_destroy(plugin_);
    std::error_code error;
    std::filesystem::remove_all(dir_, error);
  }
  radicle_plugin* get() const { return plugin_; }
  fo4::Plugin readback() const {
    const auto path = dir_ / "Quests.esp";
    if (radicle_plugin_write_file(plugin_, path.string().c_str()) != RADICLE_STATUS_OK) {
      throw std::runtime_error(radicle_last_error());
    }
    return fo4::read_plugin_file(path);
  }

private:
  std::filesystem::path dir_;
  radicle_plugin* plugin_{nullptr};
};

class ReadBack {
public:
  explicit ReadBack(const EditablePlugin& plugin) {
    EXPECT_EQ(radicle_quest_structure_read(plugin.get(), "000800:Quests.esp", &value_), RADICLE_STATUS_OK)
        << radicle_last_error();
    EXPECT_EQ(radicle_quest_structure_get_view(value_, &view), RADICLE_STATUS_OK);
  }
  ~ReadBack() { radicle_quest_structure_destroy(value_); }
  ReadBack(const ReadBack&) = delete;
  ReadBack& operator=(const ReadBack&) = delete;
  radicle_quest_structure_view view{};

private:
  radicle_quest_structure* value_{nullptr};
};

std::string text_of(const radicle_quest_alias& alias) {
  return alias.name == nullptr ? std::string{} : std::string(reinterpret_cast<const char*>(alias.name), alias.name_size);
}

std::vector<std::byte> override_plugin_bytes() {
  ByteWriter out;
  out.bytes = make_tes4_plugin(0, 1.0f, {"Quests.esp"});
  const auto quests = begin_group(out, "QUST", 0);
  const auto quest = begin_record(out, "QUST", 0, 0x00000800);
  write_subrecord(out, "EDID", std::string_view("TestQuest\0", 10));
  ByteWriter first;
  first.u16(99);
  first.u8(2);
  first.u8(0);
  write_subrecord(out, "INDX", first.bytes);
  ByteWriter second;
  second.u16(98);
  second.u8(0);
  second.u8(0);
  write_subrecord(out, "INDX", second.bytes);
  ByteWriter next;
  next.u32(1);
  write_subrecord(out, "ANAM", next.bytes);
  end_record(out, quest);
  end_group(out, quests);
  const auto globals = begin_group(out, "GLOB", 0);
  const auto global = begin_record(out, "GLOB", 0, 0x01000801);
  write_subrecord(out, "EDID", std::string_view("TestGlobal\0", 11));
  end_record(out, global);
  end_group(out, globals);
  return out.bytes;
}

}

// Constraint: read_env must return the winning override's stages and plugin, not the master's.
TEST(QuestStructure, ReadEnvReturnsTheWinningOverride) {
  const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto dir = std::filesystem::temp_directory_path() / ("quest_read_env_" + std::to_string(id));
  std::filesystem::create_directories(dir);
  fo4::write_plugin_file(quest_structure_plugin(), dir / "Quests.esp");
  {
    const auto bytes = override_plugin_bytes();
    std::ofstream stream(dir / "Over.esp", std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  const auto master = (dir / "Quests.esp").string();
  const auto over = (dir / "Over.esp").string();
  const char* paths[] = {master.c_str(), over.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(paths, 2, &env), RADICLE_STATUS_OK) << radicle_last_error();

  radicle_quest_structure* winner = nullptr;
  ASSERT_EQ(radicle_quest_structure_read_env(env, quest_form_key(), &winner), RADICLE_STATUS_OK)
      << radicle_last_error();
  radicle_quest_structure_view view{};
  ASSERT_EQ(radicle_quest_structure_get_view(winner, &view), RADICLE_STATUS_OK);
  EXPECT_STREQ(view.source_plugin, "Over.esp");
  ASSERT_EQ(view.stage_count, 2u);
  EXPECT_EQ(view.stages[0].index, 99u);
  EXPECT_EQ(view.stages[1].index, 98u);
  EXPECT_EQ(view.objective_count, 0u);
  radicle_quest_start_stage start{};
  ASSERT_EQ(radicle_quest_structure_start_stage(winner, &start), RADICLE_STATUS_OK);
  EXPECT_EQ(start.resolved, 1u);
  EXPECT_EQ(start.stage, 99u);
  radicle_quest_structure_destroy(winner);

  radicle_quest_structure* missing = reinterpret_cast<radicle_quest_structure*>(1);
  EXPECT_EQ(radicle_quest_structure_read_env(env, radicle_form_key{"Quests.esp", 0x8ffu}, &missing),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(missing, nullptr);
  radicle_quest_structure* wrong_type = reinterpret_cast<radicle_quest_structure*>(1);
  EXPECT_EQ(radicle_quest_structure_read_env(env, radicle_form_key{"Over.esp", 0x801u}, &wrong_type),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(wrong_type, nullptr);
  EXPECT_EQ(radicle_quest_structure_read_env(nullptr, quest_form_key(), &wrong_type), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_env_destroy(env);
  std::error_code error;
  std::filesystem::remove_all(dir, error);
}

// Constraint: a quest with two marked stages or none must not report a start stage.
TEST(QuestStructure, StartStageResolvesOnlyForExactlyOneMarkedStage) {
  EditablePlugin plugin(quest_structure_plugin());
  radicle_quest_start_stage start{};
  const auto read = [&] {
    radicle_quest_structure* value = nullptr;
    EXPECT_EQ(radicle_quest_structure_read(plugin.get(), "000800:Quests.esp", &value), RADICLE_STATUS_OK);
    EXPECT_EQ(radicle_quest_structure_start_stage(value, &start), RADICLE_STATUS_OK);
    radicle_quest_structure_destroy(value);
  };
  read();
  EXPECT_EQ(start.stages_supported, 1u);
  EXPECT_EQ(start.resolved, 1u);
  EXPECT_EQ(start.stage, 10u);
  EXPECT_EQ(start.marked_count, 1u);
  ASSERT_EQ(radicle_plugin_set_quest_stage_flags(plugin.get(), quest_form_key(), 20, 0x02), RADICLE_STATUS_OK)
      << radicle_last_error();
  read();
  EXPECT_EQ(start.resolved, 0u);
  EXPECT_EQ(start.stage, 0u);
  EXPECT_EQ(start.marked_count, 2u);
  ASSERT_EQ(radicle_plugin_set_quest_stage_flags(plugin.get(), quest_form_key(), 20, 0x00), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_set_quest_stage_flags(plugin.get(), quest_form_key(), 10, 0x04), RADICLE_STATUS_OK);
  read();
  EXPECT_EQ(start.resolved, 0u);
  EXPECT_EQ(start.marked_count, 0u);
  EXPECT_EQ(radicle_quest_structure_start_stage(nullptr, &start), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(start.marked_count, 0u);
}

// Constraint: a stage write must change only the stage array; logs, objectives and aliases stay.
TEST(QuestStructure, StageWritesTouchOnlyTheStageArray) {
  EditablePlugin plugin(quest_structure_plugin());
  const auto key = quest_form_key();
  size_t conditions_before = 0;
  {
    ReadBack read(plugin);
    conditions_before = read.view.condition_list_count;
  }
  ASSERT_EQ(radicle_plugin_add_quest_stage(plugin.get(), key, 30, 0x0a), RADICLE_STATUS_OK) << radicle_last_error();
  {
    ReadBack read(plugin);
    ASSERT_EQ(read.view.stage_count, 3u);
    EXPECT_EQ(read.view.stages[2].index, 30u);
    EXPECT_EQ(read.view.stages[2].flags, 0x0au);
    EXPECT_EQ(read.view.stages[2].log_count, 0u);
    EXPECT_EQ(read.view.stages[0].log_count, 1u);
    EXPECT_EQ(read.view.objective_count, 1u);
    EXPECT_EQ(read.view.alias_count, 4u);
  }
  EXPECT_EQ(radicle_plugin_add_quest_stage(plugin.get(), key, 30, 0), RADICLE_STATUS_INVALID_ARGUMENT);
  ASSERT_EQ(radicle_plugin_set_quest_stage_flags(plugin.get(), key, 10, 0x08), RADICLE_STATUS_OK)
      << radicle_last_error();
  {
    ReadBack read(plugin);
    EXPECT_EQ(read.view.stages[0].flags, 0x08u);
    EXPECT_EQ(read.view.stages[0].opaque_byte, 7u);
    EXPECT_EQ(read.view.stages[0].is_startup_stage, 0u);
    EXPECT_EQ(read.view.stages[0].log_count, 1u);
    EXPECT_EQ(read.view.logs[0].flags, 1u);
    EXPECT_EQ(read.view.condition_list_count, conditions_before);
  }
  EXPECT_EQ(radicle_plugin_set_quest_stage_flags(plugin.get(), key, 77, 0), RADICLE_STATUS_NOT_FOUND);
  ASSERT_EQ(radicle_plugin_remove_quest_stage(plugin.get(), key, 20), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(radicle_plugin_remove_quest_stage(plugin.get(), key, 20), RADICLE_STATUS_NOT_FOUND);
  {
    ReadBack read(plugin);
    ASSERT_EQ(read.view.stage_count, 2u);
    EXPECT_EQ(read.view.stages[0].index, 10u);
    EXPECT_EQ(read.view.stages[1].index, 30u);
    EXPECT_EQ(read.view.objective_count, 1u);
    EXPECT_EQ(read.view.alias_count, 4u);
  }
  EXPECT_EQ(radicle_plugin_add_quest_stage(plugin.get(), radicle_form_key{"Quests.esp", 0x8ffu}, 1, 0),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_add_quest_stage(nullptr, key, 1, 0), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(QuestStructure, StageReorderPreservesLogsAndMapsErrors) {
  EditablePlugin plugin(quest_structure_plugin());
  const auto key = quest_form_key();
  ASSERT_EQ(radicle_plugin_reorder_quest_stage(plugin.get(), key, 20, 0), RADICLE_STATUS_OK)
      << radicle_last_error();
  {
    ReadBack read(plugin);
    ASSERT_EQ(read.view.stage_count, 2u);
    EXPECT_EQ(read.view.stages[0].index, 20u);
    EXPECT_EQ(read.view.stages[1].index, 10u);
    ASSERT_EQ(read.view.stages[1].log_count, 1u);
    EXPECT_EQ(read.view.logs[0].flags, 1u);
    ASSERT_EQ(read.view.logs[0].text.size, 3u);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(read.view.logs[0].text.bytes), 3), "Log");
  }
  EXPECT_EQ(radicle_plugin_reorder_quest_stage(plugin.get(), key, 99, 0), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_reorder_quest_stage(plugin.get(), key, 20, 2), RADICLE_STATUS_INVALID_ARGUMENT);
  ReadBack read(plugin);
  EXPECT_EQ(read.view.stages[0].index, 20u);
  EXPECT_EQ(read.view.stages[1].index, 10u);
  EXPECT_EQ(read.view.stages[1].log_count, 1u);
}

TEST(QuestStructure, LogEntrySetPreservesMembersAndOptionalFields) {
  auto native = quest_structure_plugin();
  change_quest(native, [](fo4::Record& record) {
    const auto cnam = std::find_if(record.subrecords.begin(), record.subrecords.end(), [](const auto& member) {
      return member.signature == fo4::Signature::from_string("CNAM");
    });
    record.subrecords.insert(cnam, quest_member("ZZZZ", {std::byte{0x5a}}));
  });
  const auto original_layout = quest_layout(native);
  EditablePlugin plugin(native);
  const auto key = quest_form_key();
  ASSERT_EQ(radicle_plugin_set_quest_log_entry(plugin.get(), key, 10, 0, 2, RADICLE_NAME_INLINE,
                                               "Updated", 0, "New note"),
            RADICLE_STATUS_OK) << radicle_last_error();
  auto saved = plugin.readback();
  EXPECT_EQ(quest_layout(saved), original_layout);
  const auto* record = fo4::find_record(saved, structure_quest_key());
  ASSERT_NE(record, nullptr);
  const auto unknown = std::find_if(record->subrecords.begin(), record->subrecords.end(), [](const auto& member) {
    return member.signature == fo4::Signature::from_string("ZZZZ");
  });
  ASSERT_NE(unknown, record->subrecords.end());
  EXPECT_EQ(unknown->data, (std::vector<std::byte>{std::byte{0x5a}}));
  {
    ReadBack read(plugin);
    ASSERT_EQ(read.view.stage_count, 2u);
    ASSERT_EQ(read.view.logs[0].flags, 2u);
    EXPECT_EQ(bytes_of(read.view.logs[0].text.bytes, read.view.logs[0].text.size), "Updated");
    EXPECT_EQ(bytes_of(read.view.logs[0].note, read.view.logs[0].note_size), "New note");
    ASSERT_EQ(read.view.condition_lists[2].scope, RADICLE_QUEST_SCOPE_LOG_ENTRY);
    ASSERT_EQ(read.view.condition_lists[2].condition_count, 1u);
    EXPECT_EQ(read.view.conditions[read.view.condition_lists[2].first_condition].function, 72u);
  }

  ASSERT_EQ(radicle_plugin_set_quest_log_entry(plugin.get(), key, 10, 0, 1, RADICLE_NAME_ABSENT,
                                               nullptr, 0, nullptr),
            RADICLE_STATUS_OK) << radicle_last_error();
  {
    ReadBack read(plugin);
    EXPECT_EQ(read.view.logs[0].flags, 1u);
    EXPECT_EQ(bytes_of(read.view.logs[0].text.bytes, read.view.logs[0].text.size), "Updated");
    EXPECT_EQ(bytes_of(read.view.logs[0].note, read.view.logs[0].note_size), "New note");
    EXPECT_EQ(read.view.condition_lists[2].condition_count, 1u);
  }
  EXPECT_EQ(radicle_plugin_set_quest_log_entry(plugin.get(), key, 77, 0, 0, RADICLE_NAME_ABSENT,
                                               nullptr, 0, nullptr),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_quest_log_entry(plugin.get(), key, 10, 1, 0, RADICLE_NAME_ABSENT,
                                               nullptr, 0, nullptr),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_quest_log_entry(plugin.get(), key, 10, 0, 0, RADICLE_NAME_ABSENT,
                                               nullptr, 0, "\xE6\xBC\xA2"),
            RADICLE_STATUS_INVALID_ARGUMENT);
  {
    ReadBack read(plugin);
    EXPECT_EQ(read.view.logs[0].flags, 1u);
    EXPECT_EQ(bytes_of(read.view.logs[0].text.bytes, read.view.logs[0].text.size), "Updated");
    EXPECT_EQ(bytes_of(read.view.logs[0].note, read.view.logs[0].note_size), "New note");
  }
}

TEST(QuestStructure, NewLogNoteIsInsertedBeforeExistingText) {
  auto native = quest_structure_plugin();
  change_quest(native, [](fo4::Record& record) {
    std::erase_if(static_cast<std::vector<fo4::Subrecord>&>(record.subrecords), [](const auto& member) {
      return member.signature == fo4::Signature::from_string("NAM2");
    });
    const auto cnam = std::find_if(record.subrecords.begin(), record.subrecords.end(), [](const auto& member) {
      return member.signature == fo4::Signature::from_string("CNAM");
    });
    record.subrecords.insert(cnam, quest_member("ZZZZ", {std::byte{0x5a}}));
  });
  auto expected_layout = quest_layout(native);
  const auto cnam_position = expected_layout.find(" CNAM");
  ASSERT_NE(cnam_position, std::string::npos);
  expected_layout.insert(cnam_position, " NAM2");
  EditablePlugin plugin(native);
  ASSERT_EQ(radicle_plugin_set_quest_log_entry(plugin.get(), quest_form_key(), 10, 0, 1,
                                               RADICLE_NAME_ABSENT, nullptr, 0, "New note"),
            RADICLE_STATUS_OK) << radicle_last_error();
  const auto saved = plugin.readback();
  EXPECT_EQ(quest_layout(saved), expected_layout);
  const auto* record = fo4::find_record(saved, structure_quest_key());
  ASSERT_NE(record, nullptr);
  const auto unknown = std::find_if(record->subrecords.begin(), record->subrecords.end(), [](const auto& member) {
    return member.signature == fo4::Signature::from_string("ZZZZ");
  });
  ASSERT_NE(unknown, record->subrecords.end());
  EXPECT_EQ(unknown->data, (std::vector<std::byte>{std::byte{0x5a}}));
  ReadBack read(plugin);
  EXPECT_EQ(bytes_of(read.view.logs[0].text.bytes, read.view.logs[0].text.size), "Log");
  EXPECT_EQ(bytes_of(read.view.logs[0].note, read.view.logs[0].note_size), "New note");
  EXPECT_EQ(read.view.condition_lists[2].condition_count, 1u);
}

// Constraint: xEdit orders a log entry QSDT, conditions, NAM2, CNAM, NAM0, so a missing CNAM or NAM2 must never land after NAM0.
TEST(QuestStructure, NewLogTextAndNoteAreInsertedBeforeNextQuest) {
  auto native = quest_structure_plugin();
  auto expected_layout = quest_layout(native);
  change_quest(native, [](fo4::Record& record) {
    std::erase_if(static_cast<std::vector<fo4::Subrecord>&>(record.subrecords), [](const auto& member) {
      return member.signature == fo4::Signature::from_string("NAM2");
    });
    const auto cnam = std::find_if(record.subrecords.begin(), record.subrecords.end(), [](const auto& member) {
      return member.signature == fo4::Signature::from_string("CNAM");
    });
    *cnam = quest_member("NAM0", {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}});
  });
  const auto cnam_position = expected_layout.find(" CNAM");
  ASSERT_NE(cnam_position, std::string::npos);
  expected_layout.insert(cnam_position + 5, " NAM0");
  EditablePlugin plugin(native);
  ASSERT_EQ(radicle_plugin_set_quest_log_entry(plugin.get(), quest_form_key(), 10, 0, 1,
                                               RADICLE_NAME_INLINE, "Fresh", 0, "New note"),
            RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(quest_layout(plugin.readback()), expected_layout);
}

TEST(QuestStructure, LocalizedLogEntrySetUsesStringId) {
  EditablePlugin plugin(quest_structure_plugin(true));
  EXPECT_EQ(radicle_plugin_set_quest_log_entry(plugin.get(), quest_form_key(), 10, 0, 2,
                                                RADICLE_NAME_INLINE, "inline", 0, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
  ASSERT_EQ(radicle_plugin_set_quest_log_entry(plugin.get(), quest_form_key(), 10, 0, 2,
                                                RADICLE_NAME_STRING_ID, nullptr, 0x7788, nullptr),
            RADICLE_STATUS_OK) << radicle_last_error();
  ReadBack read(plugin);
  EXPECT_EQ(read.view.logs[0].flags, 2u);
  EXPECT_EQ(read.view.logs[0].text.kind, RADICLE_NAME_STRING_ID);
  EXPECT_EQ(read.view.logs[0].text.string_id, 0x7788u);
  EXPECT_EQ(bytes_of(read.view.logs[0].note, read.view.logs[0].note_size), "");
}

TEST(QuestStructure, LogEntryAppendPreservesExistingConditionsAndVmad) {
  auto native = quest_structure_plugin();
  const auto vmad = quest_stage_vmad_fragment();
  change_quest(native, [&](fo4::Record& record) {
    record.subrecords.insert(record.subrecords.begin() + 1, quest_member("VMAD", vmad));
  });
  EditablePlugin plugin(native);
  ASSERT_EQ(radicle_plugin_add_quest_log_entry(plugin.get(), quest_form_key(), 10, 2,
                                               RADICLE_NAME_INLINE, "New text", 0, "New note"),
            RADICLE_STATUS_OK) << radicle_last_error();

  const auto saved = plugin.readback();
  const auto* record = fo4::find_record(saved, structure_quest_key());
  ASSERT_NE(record, nullptr);
  const auto saved_vmad = std::find_if(record->subrecords.begin(), record->subrecords.end(), [](const auto& member) {
    return member.signature == fo4::sig_vmad;
  });
  ASSERT_NE(saved_vmad, record->subrecords.end());
  EXPECT_EQ(saved_vmad->data, vmad);
  {
    ReadBack read(plugin);
    ASSERT_EQ(read.view.stage_count, 2u);
    ASSERT_EQ(read.view.stages[0].log_count, 2u);
    EXPECT_EQ(read.view.stages[1].log_count, 0u);
    EXPECT_EQ(read.view.logs[0].flags, 1u);
    EXPECT_EQ(bytes_of(read.view.logs[0].text.bytes, read.view.logs[0].text.size), "Log");
    EXPECT_EQ(read.view.logs[0].note_size, 0u);
    EXPECT_EQ(read.view.logs[1].flags, 2u);
    EXPECT_EQ(bytes_of(read.view.logs[1].text.bytes, read.view.logs[1].text.size), "New text");
    EXPECT_EQ(bytes_of(read.view.logs[1].note, read.view.logs[1].note_size), "New note");
    size_t existing_conditions = 0;
    size_t appended_conditions = 0;
    uint16_t existing_function = 0;
    for (size_t i = 0; i < read.view.condition_list_count; ++i) {
      const auto& list = read.view.condition_lists[i];
      if (list.scope != RADICLE_QUEST_SCOPE_LOG_ENTRY || list.primary != 0) continue;
      if (list.secondary == 0) {
        existing_conditions = list.condition_count;
        if (list.condition_count != 0) existing_function = read.view.conditions[list.first_condition].function;
      }
      if (list.secondary == 1) appended_conditions = list.condition_count;
    }
    EXPECT_EQ(existing_conditions, 1u);
    EXPECT_EQ(existing_function, 72u);
    EXPECT_EQ(appended_conditions, 0u);
  }

  const auto before_invalid = fo4::write_plugin(saved);
  EXPECT_EQ(radicle_plugin_add_quest_log_entry(plugin.get(), quest_form_key(), 10, 0,
                                                RADICLE_NAME_INLINE, "Valid text", 0, "\xE6\xBC\xA2"),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(fo4::write_plugin(plugin.readback()), before_invalid);
  EXPECT_EQ(radicle_plugin_add_quest_log_entry(plugin.get(), quest_form_key(), 99, 0,
                                                RADICLE_NAME_ABSENT, nullptr, 0, nullptr),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(fo4::write_plugin(plugin.readback()), before_invalid);
}

TEST(QuestStructure, LocalizedLogEntryAppendUsesStringId) {
  EditablePlugin plugin(quest_structure_plugin(true));
  EXPECT_EQ(radicle_plugin_add_quest_log_entry(plugin.get(), quest_form_key(), 10, 2,
                                                RADICLE_NAME_INLINE, "inline", 0, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
  ASSERT_EQ(radicle_plugin_add_quest_log_entry(plugin.get(), quest_form_key(), 10, 2,
                                                RADICLE_NAME_STRING_ID, nullptr, 0x7788, nullptr),
            RADICLE_STATUS_OK) << radicle_last_error();
  ReadBack read(plugin);
  ASSERT_EQ(read.view.stages[0].log_count, 2u);
  EXPECT_EQ(read.view.logs[1].flags, 2u);
  EXPECT_EQ(read.view.logs[1].text.kind, RADICLE_NAME_STRING_ID);
  EXPECT_EQ(read.view.logs[1].text.string_id, 0x7788u);
  EXPECT_EQ(read.view.logs[1].note, nullptr);
  EXPECT_EQ(read.view.logs[1].note_size, 0u);
}

// Constraint: objective writes keep the other arrays and refuse text of the wrong kind for the plugin.
TEST(QuestStructure, ObjectiveWritesKeepStagesAndAliasesAndCheckTextKind) {
  EditablePlugin plugin(quest_structure_plugin());
  const auto key = quest_form_key();
  ASSERT_EQ(radicle_plugin_add_quest_objective(plugin.get(), key, 40, 1, RADICLE_NAME_INLINE, "Find it", 0),
            RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(radicle_plugin_add_quest_objective(plugin.get(), key, 40, 0, RADICLE_NAME_ABSENT, nullptr, 0),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_quest_objective(plugin.get(), key, 41, 0, RADICLE_NAME_STRING_ID, nullptr, 5),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_quest_objective(plugin.get(), key, 41, 0, RADICLE_NAME_MALFORMED, "x", 0),
            RADICLE_STATUS_INVALID_ARGUMENT);
  {
    ReadBack read(plugin);
    ASSERT_EQ(read.view.objective_count, 2u);
    EXPECT_EQ(read.view.objectives[1].index, 40u);
    EXPECT_EQ(read.view.objectives[1].flags, 1u);
    ASSERT_EQ(read.view.objectives[1].text.kind, RADICLE_NAME_INLINE);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(read.view.objectives[1].text.bytes),
                          read.view.objectives[1].text.size),
              "Find it");
    EXPECT_EQ(read.view.stage_count, 2u);
    EXPECT_EQ(read.view.alias_count, 4u);
  }
  ASSERT_EQ(radicle_plugin_set_quest_objective(plugin.get(), key, 30, 3, RADICLE_NAME_ABSENT, nullptr, 0),
            RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(radicle_plugin_add_quest_objective_target(plugin.get(), key, 30, 4, 9, radicle_form_key{}),
            RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(radicle_plugin_add_quest_objective_target(plugin.get(), key, 30, 5, 1, quest_form_key()),
            RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(radicle_plugin_add_quest_objective_target(plugin.get(), key, 99, 1, 0, radicle_form_key{}),
            RADICLE_STATUS_NOT_FOUND);
  {
    ReadBack read(plugin);
    const auto& objective = read.view.objectives[0];
    EXPECT_EQ(objective.index, 30u);
    EXPECT_EQ(objective.flags, 3u);
    EXPECT_EQ(objective.text.kind, RADICLE_NAME_INLINE);
    ASSERT_EQ(objective.target_count, 3u);
    EXPECT_EQ(read.view.targets[objective.first_target].alias, -1);
    EXPECT_EQ(read.view.targets[objective.first_target + 1].alias, 4);
    EXPECT_EQ(read.view.targets[objective.first_target + 1].flags, 9u);
    EXPECT_EQ(read.view.targets[objective.first_target + 1].keyword.plugin, nullptr);
    EXPECT_EQ(read.view.targets[objective.first_target + 2].keyword.local_id, 0x800u);
  }
  ASSERT_EQ(radicle_plugin_remove_quest_objective_target(plugin.get(), key, 30, 1), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(radicle_plugin_remove_quest_objective_target(plugin.get(), key, 30, 5), RADICLE_STATUS_NOT_FOUND);
  ASSERT_EQ(radicle_plugin_remove_quest_objective(plugin.get(), key, 40), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(radicle_plugin_remove_quest_objective(plugin.get(), key, 40), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_quest_objective(plugin.get(), key, 40, 0, RADICLE_NAME_ABSENT, nullptr, 0),
            RADICLE_STATUS_NOT_FOUND);
  {
    ReadBack read(plugin);
    ASSERT_EQ(read.view.objective_count, 1u);
    EXPECT_EQ(read.view.objectives[0].target_count, 2u);
    EXPECT_EQ(read.view.stage_count, 2u);
    EXPECT_EQ(read.view.alias_count, 4u);
  }
}

TEST(QuestStructure, ObjectiveReorderPreservesTargetsAndMapsErrors) {
  EditablePlugin plugin(quest_structure_plugin());
  const auto key = quest_form_key();
  ASSERT_EQ(radicle_plugin_add_quest_objective(plugin.get(), key, 40, 1, RADICLE_NAME_INLINE, "Find it", 0),
            RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(radicle_plugin_reorder_quest_objective(plugin.get(), key, 40, 0), RADICLE_STATUS_OK)
      << radicle_last_error();
  {
    ReadBack read(plugin);
    ASSERT_EQ(read.view.objective_count, 2u);
    EXPECT_EQ(read.view.objectives[0].index, 40u);
    EXPECT_EQ(read.view.objectives[1].index, 30u);
    ASSERT_EQ(read.view.objectives[1].target_count, 1u);
    EXPECT_EQ(read.view.targets[read.view.objectives[1].first_target].alias, -1);
    EXPECT_EQ(read.view.targets[read.view.objectives[1].first_target].keyword.local_id, 0x800u);
  }
  EXPECT_EQ(radicle_plugin_reorder_quest_objective(plugin.get(), key, 99, 0), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_reorder_quest_objective(plugin.get(), key, 40, 2), RADICLE_STATUS_INVALID_ARGUMENT);
  ReadBack read(plugin);
  EXPECT_EQ(read.view.objectives[0].index, 40u);
  EXPECT_EQ(read.view.objectives[1].index, 30u);
  EXPECT_EQ(read.view.objectives[1].target_count, 1u);
}

TEST(QuestStructure, ObjectiveTargetSetReplacesFieldsAndKeepsConditions) {
  EditablePlugin plugin(quest_structure_plugin());
  const auto key = quest_form_key();
  ASSERT_EQ(radicle_plugin_set_quest_objective_target(plugin.get(), key, 30, 0, 4, 9, key), RADICLE_STATUS_OK)
      << radicle_last_error();
  {
    ReadBack read(plugin);
    ASSERT_EQ(read.view.objective_count, 1u);
    ASSERT_EQ(read.view.targets[0].alias, 4);
    EXPECT_EQ(read.view.targets[0].flags, 9u);
    EXPECT_EQ(read.view.targets[0].keyword.local_id, 0x800u);
    ASSERT_EQ(read.view.condition_list_count, 8u);
    const auto& target_conditions = read.view.condition_lists[3];
    EXPECT_EQ(target_conditions.scope, RADICLE_QUEST_SCOPE_OBJECTIVE_TARGET);
    EXPECT_EQ(target_conditions.condition_count, 1u);
    EXPECT_EQ(read.view.conditions[target_conditions.first_condition].function, 73u);
  }
  ASSERT_EQ(radicle_plugin_set_quest_objective_target(plugin.get(), key, 30, 0, -2, 1, radicle_form_key{}),
            RADICLE_STATUS_OK) << radicle_last_error();
  {
    ReadBack read(plugin);
    EXPECT_EQ(read.view.targets[0].alias, -2);
    EXPECT_EQ(read.view.targets[0].flags, 1u);
    EXPECT_EQ(read.view.targets[0].keyword.plugin, nullptr);
    EXPECT_EQ(read.view.condition_lists[3].condition_count, 1u);
  }
  EXPECT_EQ(radicle_plugin_set_quest_objective_target(plugin.get(), key, 99, 0, 1, 0, key),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_set_quest_objective_target(plugin.get(), key, 30, 1, 1, 0, key),
            RADICLE_STATUS_NOT_FOUND);
  {
    ReadBack read(plugin);
    EXPECT_EQ(read.view.targets[0].alias, -2);
    EXPECT_EQ(read.view.targets[0].flags, 1u);
    EXPECT_EQ(read.view.targets[0].keyword.plugin, nullptr);
  }
}

TEST(QuestStructure, ObjectiveTargetSetRejectsKeywordBeforeFormVersion82Atomically) {
  auto native = quest_structure_plugin();
  change_quest(native, [](fo4::Record& record) {
    record.header.form_version = 81;
    for (auto& member : record.subrecords) {
      if (member.signature == fo4::Signature::from_string("QSTA")) member.data.resize(8);
    }
  });
  EditablePlugin plugin(native);
  EXPECT_EQ(radicle_plugin_set_quest_objective_target(plugin.get(), quest_form_key(), 30, 0, 7, 8,
                                                       quest_form_key()),
            RADICLE_STATUS_INVALID_ARGUMENT);
  {
    ReadBack read(plugin);
    EXPECT_EQ(read.view.targets[0].alias, -1);
    EXPECT_EQ(read.view.targets[0].flags, 6u);
    EXPECT_EQ(read.view.targets[0].keyword.plugin, nullptr);
  }
}

TEST(QuestStructure, ObjectiveTargetSetCanReplaceAnUnresolvedKeyword) {
  auto native = quest_structure_plugin();
  change_quest(native, [](fo4::Record& record) {
    const auto target = std::find_if(record.subrecords.begin(), record.subrecords.end(), [](const auto& member) {
      return member.signature == fo4::Signature::from_string("QSTA");
    });
    target->data[11] = std::byte{0x07};
  });
  EditablePlugin plugin(native);
  ASSERT_EQ(radicle_plugin_set_quest_objective_target(plugin.get(), quest_form_key(), 30, 0, 2, 3,
                                                       quest_form_key()),
            RADICLE_STATUS_OK) << radicle_last_error();
  ReadBack read(plugin);
  EXPECT_EQ(read.view.targets[0].alias, 2);
  EXPECT_EQ(read.view.targets[0].flags, 3u);
  EXPECT_EQ(read.view.targets[0].keyword.local_id, 0x800u);
  EXPECT_EQ(read.view.unsupported_count, 0u);
}

// Constraint: a localized plugin stores a string id for objective text, never inline text.
TEST(QuestStructure, LocalizedObjectiveTextTakesAStringId) {
  EditablePlugin plugin(quest_structure_plugin(true));
  const auto key = quest_form_key();
  EXPECT_EQ(radicle_plugin_add_quest_objective(plugin.get(), key, 40, 0, RADICLE_NAME_INLINE, "Find it", 0),
            RADICLE_STATUS_INVALID_ARGUMENT);
  ASSERT_EQ(radicle_plugin_add_quest_objective(plugin.get(), key, 40, 0, RADICLE_NAME_STRING_ID, nullptr, 0x777),
            RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(radicle_plugin_set_quest_objective(plugin.get(), key, 30, 2, RADICLE_NAME_STRING_ID, nullptr, 0x888),
            RADICLE_STATUS_OK) << radicle_last_error();
  ReadBack read(plugin);
  ASSERT_EQ(read.view.objective_count, 2u);
  EXPECT_EQ(read.view.objectives[0].text.kind, RADICLE_NAME_STRING_ID);
  EXPECT_EQ(read.view.objectives[0].text.string_id, 0x888u);
  EXPECT_EQ(read.view.objectives[1].text.string_id, 0x777u);
}

// Constraint: alias add, replace and remove work by id and keep conditions the replace input does not carry.
TEST(QuestStructure, AliasWritesAddReplaceAndRemoveById) {
  EditablePlugin plugin(quest_structure_plugin());
  const auto key = quest_form_key();
  radicle_quest_alias_input missing_entries{};
  missing_entries.kind = RADICLE_QUEST_ALIAS_COLLECTION;
  missing_entries.collection_entry_count = 1;
  EXPECT_EQ(radicle_plugin_add_quest_alias(plugin.get(), key, &missing_entries), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_quest_alias_input added{};
  added.kind = RADICLE_QUEST_ALIAS_REFERENCE;
  added.id = 5;
  added.name = "Added";
  added.flags = 3;
  added.fill_type = RADICLE_QUEST_FILL_SPECIFIC_REFERENCE;
  added.target = quest_form_key();
  ASSERT_EQ(radicle_plugin_add_quest_alias(plugin.get(), key, &added), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(radicle_plugin_add_quest_alias(plugin.get(), key, &added), RADICLE_STATUS_INVALID_ARGUMENT);
  {
    ReadBack read(plugin);
    ASSERT_EQ(read.view.alias_count, 5u);
    const auto& alias = read.view.aliases[4];
    EXPECT_EQ(alias.id, 5u);
    EXPECT_EQ(text_of(alias), "Added");
    EXPECT_EQ(alias.flags, 3u);
    EXPECT_EQ(alias.fill_type, RADICLE_QUEST_FILL_SPECIFIC_REFERENCE);
    EXPECT_EQ(alias.target.local_id, 0x800u);
    EXPECT_EQ(read.view.stage_count, 2u);
    EXPECT_EQ(read.view.objective_count, 1u);
  }

  radicle_quest_alias_input replacement{};
  replacement.kind = RADICLE_QUEST_ALIAS_REFERENCE;
  replacement.id = 1;
  replacement.name = "renamed";
  replacement.flags = 7;
  replacement.fill_type = RADICLE_QUEST_FILL_UNIQUE_ACTOR;
  replacement.target = quest_form_key();
  ASSERT_EQ(radicle_plugin_replace_quest_alias(plugin.get(), key, 1, &replacement), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(radicle_plugin_replace_quest_alias(plugin.get(), key, 9, &replacement), RADICLE_STATUS_NOT_FOUND);
  replacement.id = 2;
  EXPECT_EQ(radicle_plugin_replace_quest_alias(plugin.get(), key, 1, &replacement), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_quest_alias_input create_without_alias{};
  create_without_alias.kind = RADICLE_QUEST_ALIAS_REFERENCE;
  create_without_alias.id = 6;
  create_without_alias.fill_type = RADICLE_QUEST_FILL_CREATE_REFERENCE;
  EXPECT_EQ(radicle_plugin_add_quest_alias(plugin.get(), key, &create_without_alias), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_quest_alias(plugin.get(), key, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  {
    ReadBack read(plugin);
    EXPECT_EQ(read.view.aliases[0].fill_type, RADICLE_QUEST_FILL_UNIQUE_ACTOR);
    EXPECT_EQ(text_of(read.view.aliases[0]), "renamed");
    EXPECT_EQ(read.view.aliases[0].flags, 7u);
    size_t alias_conditions = 0;
    for (size_t i = 0; i < read.view.condition_list_count; ++i) {
      const auto& list = read.view.condition_lists[i];
      if (list.scope == RADICLE_QUEST_SCOPE_ALIAS && list.primary == 0) alias_conditions = list.condition_count;
    }
    EXPECT_EQ(alias_conditions, 1u);
  }
  ASSERT_EQ(radicle_plugin_remove_quest_alias(plugin.get(), key, 5), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(radicle_plugin_remove_quest_alias(plugin.get(), key, 5), RADICLE_STATUS_NOT_FOUND);
  ReadBack read(plugin);
  ASSERT_EQ(read.view.alias_count, 4u);
  EXPECT_EQ(read.view.aliases[1].id, 2u);
  EXPECT_EQ(read.view.stage_count, 2u);
}

TEST(QuestStructure, AliasWritesRoundTripAliasReferenceFillTypes) {
  EditablePlugin plugin(quest_structure_plugin());
  const auto key = quest_form_key();
  const auto target = quest_form_key();
  const auto secondary = quest_form_key();
  const radicle_quest_fill_type fill_types[] = {
      RADICLE_QUEST_FILL_LOCATION_ALIAS_REFERENCE,
      RADICLE_QUEST_FILL_EXTERNAL_ALIAS_REFERENCE,
      RADICLE_QUEST_FILL_REFERENCE_ALIAS_LOCATION};

  for (std::size_t i = 0; i < 3; ++i) {
    radicle_quest_alias_input alias{};
    alias.kind = i == 2 ? RADICLE_QUEST_ALIAS_LOCATION : RADICLE_QUEST_ALIAS_REFERENCE;
    alias.id = static_cast<std::uint32_t>(10 + i);
    alias.name = "Fill";
    alias.fill_type = fill_types[i];
    alias.target = target;
    alias.secondary_target = secondary;
    alias.has_fill_alias = 1;
    alias.fill_alias = static_cast<std::int32_t>(20 + i);
    ASSERT_EQ(radicle_plugin_add_quest_alias(plugin.get(), key, &alias), RADICLE_STATUS_OK)
        << radicle_last_error();
  }

  radicle_quest_alias_input missing_alias{};
  missing_alias.kind = RADICLE_QUEST_ALIAS_REFERENCE;
  missing_alias.id = 13;
  missing_alias.fill_type = RADICLE_QUEST_FILL_LOCATION_ALIAS_REFERENCE;
  missing_alias.secondary_target = secondary;
  EXPECT_EQ(radicle_plugin_add_quest_alias(plugin.get(), key, &missing_alias), RADICLE_STATUS_INVALID_ARGUMENT);

  ReadBack read(plugin);
  ASSERT_EQ(read.view.alias_count, 7u);
  for (std::size_t i = 0; i < 3; ++i) {
    const auto& alias = read.view.aliases[i + 4];
    EXPECT_EQ(alias.id, 10u + i);
    EXPECT_EQ(alias.fill_type, fill_types[i]);
    EXPECT_EQ(alias.has_fill_alias, 1u);
    EXPECT_EQ(alias.fill_alias, 20 + static_cast<std::int32_t>(i));
    if (i == 1) {
      EXPECT_EQ(alias.target.local_id, 0x800u);
      EXPECT_EQ(alias.secondary_target.plugin, nullptr);
    } else {
      EXPECT_EQ(alias.target.plugin, nullptr);
      EXPECT_EQ(alias.secondary_target.local_id, 0x800u);
    }
  }
}

TEST(QuestStructure, AliasReorderPreservesRunsAndMapsErrors) {
  EditablePlugin plugin(quest_structure_plugin());
  const auto key = quest_form_key();
  ASSERT_EQ(radicle_plugin_reorder_quest_alias(plugin.get(), key, 2, 0), RADICLE_STATUS_OK)
      << radicle_last_error();
  {
    ReadBack read(plugin);
    ASSERT_EQ(read.view.alias_count, 4u);
    EXPECT_EQ(read.view.aliases[0].id, 2u);
    EXPECT_EQ(text_of(read.view.aliases[0]), "loc");
    EXPECT_EQ(read.view.aliases[0].flags, 9u);
    EXPECT_EQ(read.view.aliases[1].id, 1u);
    size_t first_alias_conditions = 0;
    for (size_t i = 0; i < read.view.condition_list_count; ++i) {
      const auto& list = read.view.condition_lists[i];
      if (list.scope == RADICLE_QUEST_SCOPE_ALIAS && list.primary == 0) {
        first_alias_conditions = list.condition_count;
      }
    }
    EXPECT_EQ(first_alias_conditions, 1u);
  }
  EXPECT_EQ(radicle_plugin_reorder_quest_alias(plugin.get(), key, 99, 0), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_reorder_quest_alias(plugin.get(), key, 2, 4), RADICLE_STATUS_INVALID_ARGUMENT);
  ReadBack read(plugin);
  EXPECT_EQ(read.view.aliases[0].id, 2u);
  EXPECT_EQ(text_of(read.view.aliases[0]), "loc");
  EXPECT_EQ(read.view.aliases[1].id, 1u);
}

static std::string bytes_of(const uint8_t* data, size_t size) {
  return data == nullptr ? std::string{} : std::string(reinterpret_cast<const char*>(data), size);
}

// Constraint: inline objective text and alias names are stored as Windows-1252, so Café is 43 61 66 E9, and unrepresentable text is refused.
TEST(QuestStructure, InlineTextIsStoredAsWindows1252) {
  EditablePlugin plugin(quest_structure_plugin());
  const auto key = quest_form_key();
  const std::string cafe = "Caf\xC3\xA9";
  const std::string stored = "Caf\xE9";
  ASSERT_EQ(radicle_plugin_add_quest_objective(plugin.get(), key, 40, 0, RADICLE_NAME_INLINE, cafe.c_str(), 0),
            RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(radicle_plugin_add_quest_objective(plugin.get(), key, 41, 0, RADICLE_NAME_INLINE, "\xE6\xBC\xA2", 0),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_quest_objective(plugin.get(), key, 41, 0, RADICLE_NAME_INLINE, "\xE9", 0),
            RADICLE_STATUS_INVALID_ARGUMENT);
  ASSERT_EQ(radicle_plugin_set_quest_objective(plugin.get(), key, 30, 0, RADICLE_NAME_INLINE, cafe.c_str(), 0),
            RADICLE_STATUS_OK) << radicle_last_error();
  radicle_quest_alias_input alias{};
  alias.kind = RADICLE_QUEST_ALIAS_REFERENCE;
  alias.id = 5;
  alias.name = cafe.c_str();
  ASSERT_EQ(radicle_plugin_add_quest_alias(plugin.get(), key, &alias), RADICLE_STATUS_OK) << radicle_last_error();
  alias.id = 6;
  alias.name = "\xE6\xBC\xA2";
  EXPECT_EQ(radicle_plugin_add_quest_alias(plugin.get(), key, &alias), RADICLE_STATUS_INVALID_ARGUMENT);
  ReadBack read(plugin);
  ASSERT_EQ(read.view.objective_count, 2u);
  EXPECT_EQ(bytes_of(read.view.objectives[0].text.bytes, read.view.objectives[0].text.size), stored);
  EXPECT_EQ(bytes_of(read.view.objectives[1].text.bytes, read.view.objectives[1].text.size), stored);
  ASSERT_EQ(read.view.alias_count, 5u);
  EXPECT_EQ(text_of(read.view.aliases[4]), stored);
  ASSERT_EQ(stored.size(), 4u);
  EXPECT_EQ(static_cast<unsigned char>(stored[3]), 0xE9u);
}

// Constraint: reference alias flags are always written (zero included), collections ignore them, and a bad record key is refused.
TEST(QuestStructure, AliasFlagsAreWrittenForReferencesAndIgnoredForCollections) {
  EditablePlugin plugin(quest_structure_plugin());
  const auto key = quest_form_key();
  radicle_quest_alias_input reference{};
  reference.kind = RADICLE_QUEST_ALIAS_REFERENCE;
  reference.id = 5;
  reference.name = "Five";
  ASSERT_EQ(radicle_plugin_add_quest_alias(plugin.get(), key, &reference), RADICLE_STATUS_OK) << radicle_last_error();
  reference.kind = RADICLE_QUEST_ALIAS_LOCATION;
  reference.id = 6;
  reference.flags = 0x21;
  ASSERT_EQ(radicle_plugin_add_quest_alias(plugin.get(), key, &reference), RADICLE_STATUS_OK) << radicle_last_error();
  radicle_quest_alias_input collection{};
  collection.kind = RADICLE_QUEST_ALIAS_COLLECTION;
  collection.flags = 0x99;
  ASSERT_EQ(radicle_plugin_add_quest_alias(plugin.get(), key, &collection), RADICLE_STATUS_OK) << radicle_last_error();
  {
    ReadBack read(plugin);
    ASSERT_EQ(read.view.alias_count, 7u);
    EXPECT_EQ(read.view.aliases[4].id, 5u);
    EXPECT_EQ(read.view.aliases[4].has_flags, 1u);
    EXPECT_EQ(read.view.aliases[4].flags, 0u);
    EXPECT_EQ(read.view.aliases[5].id, 6u);
    EXPECT_EQ(read.view.aliases[5].flags, 0x21u);
    EXPECT_EQ(read.view.aliases[6].kind, RADICLE_QUEST_ALIAS_COLLECTION);
    EXPECT_EQ(read.view.aliases[6].has_flags, 0u);
  }
  EXPECT_EQ(radicle_plugin_add_quest_stage(plugin.get(), radicle_form_key{nullptr, 0x800u}, 1, 0),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_quest_stage(plugin.get(), radicle_form_key{"nosuch", 0x800u}, 1, 0),
            RADICLE_STATUS_NOT_FOUND);
}

// Constraint: replace keeps unwritten members, so FILL_NONE leaves the existing fill and the alias conditions in place.
TEST(QuestStructure, ReplaceWithNoFillKeepsTheExistingFill) {
  EditablePlugin plugin(quest_structure_plugin());
  const auto key = quest_form_key();
  radicle_quest_alias_input replacement{};
  replacement.kind = RADICLE_QUEST_ALIAS_REFERENCE;
  replacement.id = 1;
  replacement.name = "renamed";
  replacement.flags = 7;
  replacement.fill_type = RADICLE_QUEST_FILL_NONE;
  ASSERT_EQ(radicle_plugin_replace_quest_alias(plugin.get(), key, 1, &replacement), RADICLE_STATUS_OK)
      << radicle_last_error();
  ReadBack read(plugin);
  EXPECT_EQ(text_of(read.view.aliases[0]), "renamed");
  EXPECT_EQ(read.view.aliases[0].flags, 7u);
  EXPECT_EQ(read.view.aliases[0].fill_type, RADICLE_QUEST_FILL_SPECIFIC_REFERENCE);
  EXPECT_EQ(read.view.aliases[0].target.local_id, 0x800u);
}

namespace {

std::string table_text(const fo4::localization::LocalizedStringTable& table, std::uint32_t id) {
  const auto* entry = fo4::localization::find_string(table, id);
  if (entry == nullptr) return "<missing>";
  std::string out;
  for (const auto byte : entry->text) out.push_back(static_cast<char>(std::to_integer<std::uint8_t>(byte)));
  return out;
}

std::uint32_t log_text_id(const fo4::Plugin& plugin) {
  const auto field = fo4::schema::scalar_field_descriptor("QUST\\LogEntryText");
  const auto* record = fo4::find_record(plugin, structure_quest_key());
  return fo4::record_localized_string_id(plugin, *record, *field, 0).value_or(0xFFFFFFFFu);
}

}  // namespace

// Constraint: detects a localized string write that stores UTF-8 instead of Windows-1252 in the table, puts a DL field in the STRINGS table, edits a master's id in place, or is allowed without tables.
TEST(QuestStructure, LocalizedLogEntryTextWriteEditsTheDlstringsTableAndRoundTrips) {
  constexpr auto path = "QUST\\LogEntryText";
  const std::string utf8_text = "Caf\xC3\xA9";
  const std::string stored = "Caf\xE9";

  auto plugin = quest_structure_plugin(true);
  EXPECT_THROW(fo4::write_record_field(plugin, structure_quest_key(), path, 0, "x"), fo4::schema::ScalarError);
  EXPECT_EQ(log_text_id(plugin), 0x1234u);

  fo4::localization::LocalizationTables own;
  fo4::localization::insert_string(own.dlstrings, 0x1234, "Log");
  ASSERT_EQ(fo4::write_record_field(plugin, structure_quest_key(), path, 0, utf8_text, &own), fo4::FieldStatus::ok);
  EXPECT_EQ(log_text_id(plugin), 0x1234u);
  EXPECT_EQ(table_text(own.dlstrings, 0x1234), stored);
  EXPECT_TRUE(own.strings.entries.empty());
  EXPECT_TRUE(own.ilstrings.entries.empty());

  fo4::localization::LocalizationTables master_id;
  ASSERT_EQ(fo4::write_record_field(plugin, structure_quest_key(), path, 0, "New", &master_id), fo4::FieldStatus::ok);
  EXPECT_EQ(log_text_id(plugin), 1u);
  EXPECT_EQ(table_text(master_id.dlstrings, 1), "New");
  EXPECT_TRUE(master_id.strings.entries.empty());

  const auto reloaded = fo4::read_plugin(fo4::write_plugin(plugin), "Quests.esp");
  EXPECT_EQ(log_text_id(reloaded), 1u);

  ASSERT_EQ(fo4::write_record_field(plugin, structure_quest_key(), path, 0, "", &master_id), fo4::FieldStatus::ok);
  EXPECT_EQ(log_text_id(plugin), 0u);
  const auto* record = fo4::find_record(plugin, structure_quest_key());
  std::string out = "stale";
  EXPECT_EQ(fo4::read_record_field(plugin, *record, path, 0, out, nullptr, 0), fo4::FieldStatus::ok);
  EXPECT_EQ(out, "");
  EXPECT_EQ(fo4::write_record_field(plugin, structure_quest_key(), path, 5, "x", &master_id),
            fo4::FieldStatus::occurrence_missing);
}

// Constraint: detects an in-place table rewrite of a string id that a second localized subrecord of the plugin still uses, which would change that other text too.
TEST(QuestStructure, LocalizedWriteDoesNotRewriteAnIdAnotherSubrecordShares) {
  auto plugin = quest_structure_plugin(true);
  change_quest(plugin, [](fo4::Record& record) {
    for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
      if (record.subrecords[i].signature != fo4::Signature::from_string("CNAM")) continue;
      record.subrecords.insert(record.subrecords.begin() + static_cast<std::ptrdiff_t>(i + 1), quest_member("QSDT", {std::byte{1}}));
      record.subrecords.insert(record.subrecords.begin() + static_cast<std::ptrdiff_t>(i + 2), quest_u32("CNAM", 0x1234));
      return;
    }
  });
  fo4::localization::LocalizationTables tables;
  fo4::localization::insert_string(tables.dlstrings, 0x1234, "Shared");
  ASSERT_EQ(fo4::write_record_field(plugin, structure_quest_key(), "QUST\\LogEntryText", 0, "Mine", &tables),
            fo4::FieldStatus::ok);
  EXPECT_EQ(table_text(tables.dlstrings, 0x1234), "Shared");
  EXPECT_NE(log_text_id(plugin), 0x1234u);
  EXPECT_NE(log_text_id(plugin), 0u);
  EXPECT_EQ(table_text(tables.dlstrings, log_text_id(plugin)), "Mine");
}

// Constraint: detects a localized-string write path that also changes a plugin that is not localized, which must keep storing inline text.
TEST(QuestStructure, InlineLogEntryTextWriteIgnoresTheTables) {
  auto plugin = quest_structure_plugin(false);
  fo4::localization::LocalizationTables tables;
  ASSERT_EQ(fo4::write_record_field(plugin, structure_quest_key(), "QUST\\LogEntryText", 0, "Inline", &tables),
            fo4::FieldStatus::ok);
  EXPECT_TRUE(tables.dlstrings.entries.empty());
  const auto* record = fo4::find_record(plugin, structure_quest_key());
  std::string out;
  ASSERT_EQ(fo4::read_record_field(plugin, *record, "QUST\\LogEntryText", 0, out, nullptr, 0), fo4::FieldStatus::ok);
  EXPECT_EQ(out, "Inline");
}
