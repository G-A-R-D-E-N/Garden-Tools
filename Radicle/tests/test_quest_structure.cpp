#include "quest_structure_fixture.hpp"
#include "fo4/c_api_quest.h"
#include "fo4/plugin/writer.hpp"
#include "fo4/quest/structure.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

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
  const auto plugin = quest_structure_plugin();
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
  ASSERT_EQ(view.alias_count, 3u);
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
  ASSERT_EQ(view.collection_entry_count, 2u);
  EXPECT_EQ(view.collection_entries[0].id, 3u);
  EXPECT_EQ(view.collection_entries[0].max_initial_fill_count, 2u);
  EXPECT_EQ(view.collection_entries[1].id, 4u);
  EXPECT_EQ(view.collection_entries[1].has_max_initial_fill_count, 0u);
  ASSERT_EQ(view.condition_list_count, 7u);
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
  ASSERT_EQ(native.condition_lists.size(), 7u);
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
    ASSERT_EQ(view.alias_count, 3u);
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

TEST(QuestStructure, MissingQuestReturnsNotFound) {
  const auto plugin = quest_structure_plugin();
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
