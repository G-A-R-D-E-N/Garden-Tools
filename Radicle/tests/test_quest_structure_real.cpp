#include "fo4/c_api_quest.h"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/quest/structure.hpp"
#include "real_data_build.hpp"
#include "real_sample_plugin.hpp"
#include "temp_data_dir.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <type_traits>

namespace {

void same_bytes(const std::optional<std::string>& source, const uint8_t* bytes, size_t size) {
  EXPECT_EQ(size, source ? source->size() : 0u);
  if (!source) {
    EXPECT_EQ(bytes, nullptr);
  } else {
    ASSERT_NE(bytes, nullptr);
    ASSERT_EQ(size, source->size());
    EXPECT_EQ(std::memcmp(bytes, source->data(), size), 0);
  }
}

void same_key(const std::optional<fo4::FormKey>& source, radicle_form_key key) {
  if (!source) {
    EXPECT_EQ(key.plugin, nullptr);
    EXPECT_EQ(key.local_id, 0u);
  } else {
    ASSERT_NE(key.plugin, nullptr);
    EXPECT_EQ(std::string(key.plugin), source->owner().filename());
    EXPECT_EQ(key.local_id, source->local().value());
  }
}

void same_text(const std::optional<fo4::schema::LocalizedString>& source, const radicle_quest_text& text) {
  if (!source) {
    EXPECT_EQ(text.kind, RADICLE_NAME_ABSENT);
    EXPECT_EQ(text.bytes, nullptr);
    EXPECT_EQ(text.size, 0u);
  } else if (source->id) {
    EXPECT_EQ(text.kind, RADICLE_NAME_STRING_ID);
    EXPECT_EQ(text.string_id, *source->id);
    EXPECT_EQ(text.bytes, nullptr);
    EXPECT_EQ(text.size, 0u);
  } else if (source->raw_id) {
    EXPECT_EQ(text.kind, RADICLE_NAME_MALFORMED);
    const auto* raw = reinterpret_cast<const std::byte*>(text.bytes);
    EXPECT_EQ(std::vector<std::byte>(raw, raw + text.size), *source->raw_id);
  } else {
    EXPECT_EQ(text.kind, RADICLE_NAME_INLINE);
    same_bytes(source->text, text.bytes, text.size);
  }
}

void same_fill(const fo4::forms::QuestAlias& source, const radicle_quest_alias& row) {
  using namespace fo4::forms;
  std::visit([&](const auto& fill) {
    using T = std::decay_t<decltype(fill)>;
    if constexpr (std::is_same_v<T, std::monostate>) {
      EXPECT_EQ(row.fill_type, RADICLE_QUEST_FILL_NONE);
    } else if constexpr (std::is_same_v<T, QuestSpecificReferenceFill>) {
      EXPECT_EQ(row.fill_type, RADICLE_QUEST_FILL_SPECIFIC_REFERENCE);
      same_key(fill.reference, row.target);
    } else if constexpr (std::is_same_v<T, QuestUniqueActorFill>) {
      EXPECT_EQ(row.fill_type, RADICLE_QUEST_FILL_UNIQUE_ACTOR);
      same_key(fill.actor, row.target);
    } else if constexpr (std::is_same_v<T, QuestLocationAliasReferenceFill>) {
      EXPECT_EQ(row.fill_type, RADICLE_QUEST_FILL_LOCATION_ALIAS_REFERENCE);
      EXPECT_EQ(row.has_fill_alias, 1u);
      EXPECT_EQ(row.fill_alias, fill.alias);
      same_key(fill.reference_type, row.secondary_target);
    } else if constexpr (std::is_same_v<T, QuestExternalAliasReferenceFill>) {
      EXPECT_EQ(row.fill_type, RADICLE_QUEST_FILL_EXTERNAL_ALIAS_REFERENCE);
      same_key(fill.quest, row.target);
      EXPECT_EQ(row.has_fill_alias, 1u);
      EXPECT_EQ(row.fill_alias, fill.alias);
    } else if constexpr (std::is_same_v<T, QuestCreateReferenceFill>) {
      EXPECT_EQ(row.fill_type, RADICLE_QUEST_FILL_CREATE_REFERENCE);
      same_key(fill.object, row.target);
      EXPECT_EQ(row.has_fill_alias, 1u);
      EXPECT_EQ(row.fill_alias, fill.alias);
      EXPECT_EQ(row.create_mode, fill.create_mode);
      EXPECT_EQ(row.level, fill.level);
      EXPECT_EQ(row.has_level, std::any_of(source.subrecords.begin(), source.subrecords.end(), [](const auto& member) {
        return member.signature == fo4::Signature::from_string("ALCL");
      }));
    } else if constexpr (std::is_same_v<T, QuestFindMatchingReferenceFill>) {
      std::visit([&](const auto& selector) {
        using S = std::decay_t<decltype(selector)>;
        if constexpr (std::is_same_v<S, QuestMatchingReferenceNearAlias>) {
          EXPECT_EQ(row.fill_type, RADICLE_QUEST_FILL_MATCHING_NEAR_ALIAS);
          EXPECT_EQ(row.has_fill_alias, 1u);
          EXPECT_EQ(row.fill_alias, selector.alias);
          EXPECT_EQ(row.near_type, selector.type);
        } else if constexpr (std::is_same_v<S, QuestMatchingReferenceEvent>) {
          EXPECT_EQ(row.fill_type, RADICLE_QUEST_FILL_MATCHING_REFERENCE_EVENT);
          EXPECT_EQ(row.event, selector.event);
          EXPECT_EQ(row.event_data, selector.data);
        } else if constexpr (std::is_same_v<S, QuestMatchingReferenceClosest>) {
          EXPECT_EQ(row.fill_type, RADICLE_QUEST_FILL_MATCHING_CLOSEST);
          EXPECT_EQ(row.has_fill_alias, 1u);
          EXPECT_EQ(row.fill_alias, selector.alias);
        }
      }, fill.selector);
    } else if constexpr (std::is_same_v<T, QuestSpecificLocationFill>) {
      EXPECT_EQ(row.fill_type, RADICLE_QUEST_FILL_SPECIFIC_LOCATION);
      same_key(fill.location, row.target);
    } else if constexpr (std::is_same_v<T, QuestReferenceAliasLocationFill>) {
      EXPECT_EQ(row.fill_type, RADICLE_QUEST_FILL_REFERENCE_ALIAS_LOCATION);
      same_key(fill.keyword, row.secondary_target);
      EXPECT_EQ(row.has_fill_alias, 1u);
      EXPECT_EQ(row.fill_alias, fill.alias);
    } else if constexpr (std::is_same_v<T, QuestExternalAliasLocationFill>) {
      EXPECT_EQ(row.fill_type, RADICLE_QUEST_FILL_EXTERNAL_ALIAS_LOCATION);
      same_key(fill.quest, row.target);
      EXPECT_EQ(row.has_fill_alias, fill.alias.has_value());
      EXPECT_EQ(row.fill_alias, fill.alias.value_or(0));
    } else if constexpr (std::is_same_v<T, QuestFindMatchingLocationFill>) {
      EXPECT_EQ(row.fill_type, RADICLE_QUEST_FILL_MATCHING_LOCATION_EVENT);
      EXPECT_EQ(row.event, fill.event);
      EXPECT_EQ(row.event_data, fill.data);
    }
  }, source.fill);
}

void same_view(const fo4::quest::QuestStructure& source, const radicle_quest_structure_view& view) {
  ASSERT_EQ(view.stage_count, source.stages.stages.size());
  ASSERT_EQ(view.objective_count, source.objectives.objectives.size());
  ASSERT_EQ(view.alias_count, source.aliases.aliases.size());
  EXPECT_EQ(view.stages_supported, source.stages_supported);
  EXPECT_EQ(view.objectives_supported, source.objectives_supported);
  EXPECT_EQ(view.aliases_supported, source.aliases_supported);
  size_t log = 0;
  for (size_t s = 0; s < view.stage_count; ++s) {
    const auto& stage = source.stages.stages[s];
    const auto& row = view.stages[s];
    EXPECT_EQ(row.index, stage.index);
    EXPECT_EQ(row.flags, stage.flags);
    EXPECT_EQ(row.opaque_byte, stage.unknown);
    ASSERT_EQ(row.first_log, log);
    ASSERT_EQ(row.log_count, stage.log_entries.size());
    for (const auto& entry : stage.log_entries) {
      ASSERT_LT(log, view.log_count);
      EXPECT_EQ(view.logs[log].flags, entry.flags);
      same_text(entry.log_text, view.logs[log].text);
      same_bytes(entry.note, view.logs[log].note, view.logs[log].note_size);
      ++log;
    }
  }
  EXPECT_EQ(log, view.log_count);
  size_t target = 0;
  for (size_t o = 0; o < view.objective_count; ++o) {
    const auto& objective = source.objectives.objectives[o];
    const auto& row = view.objectives[o];
    EXPECT_EQ(row.index, objective.index);
    EXPECT_EQ(row.flags, objective.flags);
    same_text(objective.display_text, row.text);
    ASSERT_EQ(row.first_target, target);
    ASSERT_EQ(row.target_count, objective.targets.size());
    for (const auto& item : objective.targets) {
      ASSERT_LT(target, view.target_count);
      EXPECT_EQ(view.targets[target].alias, item.alias);
      EXPECT_EQ(view.targets[target].flags, item.flags);
      same_key(item.keyword, view.targets[target].keyword);
      ++target;
    }
  }
  EXPECT_EQ(target, view.target_count);
  size_t collection = 0;
  for (size_t a = 0; a < view.alias_count; ++a) {
    const auto& alias = source.aliases.aliases[a];
    const auto& row = view.aliases[a];
    EXPECT_EQ(static_cast<int>(row.kind), static_cast<int>(alias.kind));
    EXPECT_EQ(row.id, alias.id);
    same_bytes(alias.name, row.name, row.name_size);
    EXPECT_EQ(row.has_flags, alias.flags.has_value());
    EXPECT_EQ(row.flags, alias.flags.value_or(0));
    EXPECT_EQ(row.has_force_into_alias, alias.force_into_alias.has_value());
    EXPECT_EQ(row.force_into_alias, alias.force_into_alias.value_or(0));
    EXPECT_EQ(row.has_closest_to_alias, alias.closest_to_alias.has_value());
    EXPECT_EQ(row.closest_to_alias, alias.closest_to_alias.value_or(0));
    const bool fill_supported = alias.unresolved_links.empty() &&
        std::none_of(source.unsupported.begin(), source.unsupported.end(), [&](const auto& item) {
          return item.scope == fo4::quest::QuestScope::alias && item.primary == a &&
              item.signature != fo4::Signature::from_string("CTDA") &&
              item.signature != fo4::Signature::from_string("CIS1") &&
              item.signature != fo4::Signature::from_string("CIS2");
        });
    EXPECT_EQ(row.fill_supported, fill_supported);
    same_fill(alias, row);
    ASSERT_EQ(row.first_collection_entry, collection);
    ASSERT_EQ(row.collection_entry_count, alias.collection_entries.size());
    for (const auto& item : alias.collection_entries) {
      ASSERT_LT(collection, view.collection_entry_count);
      EXPECT_EQ(view.collection_entries[collection].id, item.id);
      EXPECT_EQ(view.collection_entries[collection].has_max_initial_fill_count, item.max_initial_fill_count.has_value());
      EXPECT_EQ(view.collection_entries[collection].max_initial_fill_count, item.max_initial_fill_count.value_or(0));
      ++collection;
    }
  }
  EXPECT_EQ(collection, view.collection_entry_count);
  ASSERT_EQ(view.condition_list_count, source.condition_lists.size());
  size_t condition = 0;
  for (size_t l = 0; l < view.condition_list_count; ++l) {
    const auto& list = source.condition_lists[l];
    const auto& row = view.condition_lists[l];
    EXPECT_EQ(static_cast<int>(row.scope), static_cast<int>(list.scope));
    EXPECT_EQ(row.primary, list.primary);
    EXPECT_EQ(row.secondary, list.secondary);
    EXPECT_EQ(row.supported, list.supported);
    ASSERT_EQ(row.first_condition, condition);
    ASSERT_EQ(row.condition_count, list.conditions.size());
    for (const auto& item : list.conditions) {
      ASSERT_LT(condition, view.condition_count);
      const auto& c = view.conditions[condition++];
      EXPECT_EQ(c.flags_byte, item.flags_byte);
      EXPECT_EQ(c.comparison_raw, item.comparison_raw);
      EXPECT_EQ(c.function, item.function);
      EXPECT_EQ(c.unknown2, item.unknown2);
      EXPECT_EQ(c.param1, item.param1);
      EXPECT_EQ(c.param2, item.param2);
      EXPECT_EQ(c.run_on, item.run_on);
      EXPECT_EQ(c.reference, item.reference);
      EXPECT_EQ(c.unknown3, item.unknown3);
      same_bytes(item.parameter1_string, c.parameter1_string, c.parameter1_size);
      same_bytes(item.parameter2_string, c.parameter2_string, c.parameter2_size);
    }
  }
  EXPECT_EQ(condition, view.condition_count);
  ASSERT_EQ(view.unsupported_count, source.unsupported.size());
  for (size_t u = 0; u < view.unsupported_count; ++u) {
    const auto& item = source.unsupported[u];
    const auto& row = view.unsupported[u];
    EXPECT_EQ(static_cast<int>(row.scope), static_cast<int>(item.scope));
    EXPECT_EQ(row.primary, item.primary);
    EXPECT_EQ(row.secondary, item.secondary);
    EXPECT_EQ(row.member, item.member);
    EXPECT_EQ(std::memcmp(row.signature, item.signature.bytes().data(), 4), 0);
    EXPECT_EQ(std::string(row.reason), item.reason);
  }
}

class QuestStructureReal : public testing::TestWithParam<const char*> {};

TEST_P(QuestStructureReal, EveryStoredQuestAgreesAndRoundTrips) {
  const auto* directory = std::getenv("FO4_DATA_DIR");
  if (!directory) {
    GTEST_SKIP() << "set FO4_DATA_DIR to run official-master coverage";
  }
  record_game_build(detect_game_build(directory));
  const auto path = std::filesystem::path(directory) / GetParam();
  ASSERT_TRUE(std::filesystem::is_regular_file(path));
  const auto reduced = fo4::test::make_sample(fo4::read_plugin_file(path), [](const fo4::Record& record) {
    return record.header.signature == fo4::Signature::from_string("QUST");
  }, 0);
  fo4::test::TempDataDir data;
  const auto file = data.path() / GetParam();
  fo4::write_plugin_file(reduced, file);
  auto plugin = fo4::read_plugin_file(file);
  radicle_plugin* raw = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(file.string().c_str(), &raw), RADICLE_STATUS_OK) << radicle_last_error();
  const std::unique_ptr<radicle_plugin, decltype(&radicle_plugin_destroy)> handle(raw, radicle_plugin_destroy);
  auto rebuilt = plugin;
  rebuilt.original_range.clear();
  rebuilt.tes4.original_range.clear();
  const auto reloaded = fo4::read_plugin(fo4::write_plugin(rebuilt), GetParam());
  size_t quests = 0, stages = 0, logs = 0, objectives = 0, targets = 0, aliases = 0;
  size_t conditions = 0, unsupported = 0, failed_families = 0, failed_lists = 0;
  bool found_mq101 = false;
  for (const auto* record : fo4::collect_records(plugin)) {
    if (record->header.signature != fo4::Signature::from_string("QUST")) continue;
    SCOPED_TRACE(record->form_key.to_string());
    const auto decoded = fo4::quest::read_quest_structure(plugin, record->form_key);
    radicle_quest_structure* raw_structure = nullptr;
    ASSERT_EQ(radicle_quest_structure_read(handle.get(), record->form_key.to_string().c_str(), &raw_structure),
              RADICLE_STATUS_OK) << radicle_last_error();
    const std::unique_ptr<radicle_quest_structure, decltype(&radicle_quest_structure_destroy)>
        snapshot(raw_structure, radicle_quest_structure_destroy);
    radicle_quest_structure_view view{};
    ASSERT_EQ(radicle_quest_structure_get_view(snapshot.get(), &view), RADICLE_STATUS_OK);
    const auto count = [&](std::string_view signature, auto begin, auto end) {
      return static_cast<size_t>(std::count_if(begin, end, [&](const auto& member) {
        return member.signature == fo4::Signature::from_string(signature);
      }));
    };
    const auto begin = record->subrecords.begin();
    const auto end = record->subrecords.end();
    const auto alias_begin = std::find_if(begin, end, [](const auto& member) {
      return member.signature == fo4::Signature::from_string("ANAM");
    });
    const auto stage_end = std::find_if(begin, alias_begin, [](const auto& member) {
      return member.signature == fo4::Signature::from_string("QOBJ");
    });
    if (view.stages_supported) {
      EXPECT_EQ(view.stage_count, count("INDX", begin, stage_end));
      EXPECT_EQ(view.log_count, count("QSDT", begin, stage_end));
    }
    if (view.objectives_supported) {
      EXPECT_EQ(view.objective_count, count("QOBJ", stage_end, alias_begin));
      EXPECT_EQ(view.target_count, count("QSTA", stage_end, alias_begin));
    }
    if (view.aliases_supported) {
      const auto named = static_cast<size_t>(std::count_if(decoded.aliases.aliases.begin(), decoded.aliases.aliases.end(), [](const auto& alias) {
        return alias.kind != fo4::forms::QuestAliasKind::collection;
      }));
      EXPECT_EQ(named, count("ALST", alias_begin, end) + count("ALLS", alias_begin, end));
      EXPECT_EQ(view.collection_entry_count, count("ALCS", alias_begin, end));
    }
    const bool all_lists_supported = std::all_of(decoded.condition_lists.begin(), decoded.condition_lists.end(), [](const auto& list) {
      return list.supported;
    });
    if (view.stages_supported && view.objectives_supported && view.aliases_supported && all_lists_supported) {
      EXPECT_EQ(view.condition_count, count("CTDA", begin, end));
    }
    for (const auto family : {fo4::quest::QuestScope::stages, fo4::quest::QuestScope::objectives, fo4::quest::QuestScope::aliases}) {
      const bool supported = family == fo4::quest::QuestScope::stages ? view.stages_supported :
          family == fo4::quest::QuestScope::objectives ? view.objectives_supported : view.aliases_supported;
      if (!supported) {
        EXPECT_TRUE(std::any_of(decoded.unsupported.begin(), decoded.unsupported.end(), [&](const auto& item) {
          return item.scope == family;
        }));
      }
    }
    EXPECT_EQ(std::string(view.source_plugin), plugin.key.filename());
    ASSERT_NO_FATAL_FAILURE(same_view(decoded, view));
    const auto redecoded = fo4::quest::read_quest_structure(reloaded, record->form_key);
    ASSERT_NO_FATAL_FAILURE(same_view(redecoded, view));
    const auto* after = fo4::find_record(reloaded, record->form_key);
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->header.signature, record->header.signature);
    EXPECT_EQ(after->header.flags, record->header.flags);
    EXPECT_EQ(after->header.form_version, record->header.form_version);
    ASSERT_EQ(after->subrecords.size(), record->subrecords.size());
    for (size_t m = 0; m < record->subrecords.size(); ++m) {
      EXPECT_EQ(after->subrecords[m].signature, record->subrecords[m].signature);
      EXPECT_EQ(after->subrecords[m].extended_size, record->subrecords[m].extended_size);
      EXPECT_EQ(after->subrecords[m].data, record->subrecords[m].data);
    }
    ++quests;
    stages += view.stage_count;
    logs += view.log_count;
    objectives += view.objective_count;
    targets += view.target_count;
    aliases += view.alias_count;
    conditions += view.condition_count;
    unsupported += view.unsupported_count;
    failed_families += static_cast<size_t>(!view.stages_supported) +
        static_cast<size_t>(!view.objectives_supported) + static_cast<size_t>(!view.aliases_supported);
    for (size_t l = 0; l < view.condition_list_count; ++l) failed_lists += !view.condition_lists[l].supported;
    if (record->form_key == fo4::FormKey::parse("01ED86:Fallout4.esm")) {
      found_mq101 = true;
      std::cout << GetParam() << " MQ101 stages=" << view.stage_count << " logs=" << view.log_count
                << " objectives=" << view.objective_count << " aliases=" << view.alias_count
                << " conditions=" << view.condition_count << " unsupported=" << view.unsupported_count << '\n';
      RecordProperty("MQ101Stages", view.stage_count);
      RecordProperty("MQ101Logs", view.log_count);
      RecordProperty("MQ101Objectives", view.objective_count);
      RecordProperty("MQ101Aliases", view.alias_count);
      RecordProperty("MQ101Conditions", view.condition_count);
      RecordProperty("MQ101Unsupported", view.unsupported_count);
      std::cout << "MQ101 raw INDX=" << count("INDX", begin, stage_end) << " QSDT=" << count("QSDT", begin, stage_end)
                << " QOBJ=" << count("QOBJ", stage_end, alias_begin) << " ALID=" << count("ALID", alias_begin, end)
                << " CTDA=" << count("CTDA", begin, end) << '\n';
      for (const auto& item : decoded.unsupported) {
        std::cout << "MQ101 unsupported scope=" << static_cast<int>(item.scope)
                  << " primary=" << item.primary << " secondary=" << item.secondary
                  << " signature=" << item.signature.to_string() << " reason=" << item.reason << '\n';
      }
    }
  }
  std::cout << GetParam() << " QUST=" << quests << " stages=" << stages << " logs=" << logs
            << " objectives=" << objectives << " targets=" << targets << " aliases=" << aliases
            << " conditions=" << conditions << " unsupported=" << unsupported
            << " failed_families=" << failed_families << " failed_lists=" << failed_lists << '\n';
  RecordProperty("QuestCount", quests);
  RecordProperty("StageCount", stages);
  RecordProperty("LogCount", logs);
  RecordProperty("ObjectiveCount", objectives);
  RecordProperty("TargetCount", targets);
  RecordProperty("AliasCount", aliases);
  RecordProperty("ConditionCount", conditions);
  RecordProperty("UnsupportedCount", unsupported);
  RecordProperty("FailedFamilies", failed_families);
  RecordProperty("FailedLists", failed_lists);
  EXPECT_GT(quests, 0u);
  if (std::string_view(GetParam()) == "Fallout4.esm") {
    EXPECT_TRUE(found_mq101);
  }
}

INSTANTIATE_TEST_SUITE_P(OfficialMasters, QuestStructureReal, testing::Values(
    "Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm",
    "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCNukaWorld.esm"));

}
