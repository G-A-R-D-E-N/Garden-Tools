#include "fo4/c_api_quest.h"

#include "c_api_env_internal.hpp"
#include "fo4/quest/structure.hpp"

#include <algorithm>
#include <cstring>
#include <deque>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

using namespace radicle_c_env_detail;

struct radicle_quest_structure {
  std::deque<std::string> strings;
  std::vector<radicle_quest_stage> stages;
  std::vector<radicle_quest_log> logs;
  std::vector<radicle_quest_objective> objectives;
  std::vector<radicle_quest_target> targets;
  std::vector<radicle_quest_alias> aliases;
  std::vector<radicle_quest_collection_entry> collection_entries;
  std::vector<radicle_quest_condition_list> condition_lists;
  std::vector<radicle_quest_condition> conditions;
  std::vector<radicle_quest_unsupported> unsupported;
  radicle_quest_structure_view view{};

  const char* text(std::string_view bytes) {
    strings.emplace_back(bytes);
    return strings.back().c_str();
  }

  const uint8_t* bytes(const std::optional<std::string>& source, size_t& size) {
    size = source ? source->size() : 0;
    return source ? reinterpret_cast<const uint8_t*>(text(*source)) : nullptr;
  }

  const uint8_t* bytes(const std::optional<std::vector<std::byte>>& source, size_t& size) {
    size = source ? source->size() : 0;
    if (!source) return nullptr;
    const std::string_view chars(reinterpret_cast<const char*>(source->data()), source->size());
    return reinterpret_cast<const uint8_t*>(text(chars));
  }

  radicle_form_key key(const std::optional<fo4::FormKey>& source) {
    return source ? radicle_form_key{text(source->owner().filename()), source->local().value()}
                  : radicle_form_key{};
  }

  radicle_quest_text localized(const std::optional<fo4::schema::LocalizedString>& source) {
    radicle_quest_text out{};
    if (!source) return out;
    if (source->id) {
      out.kind = RADICLE_NAME_STRING_ID;
      out.string_id = *source->id;
    } else if (source->raw_id) {
      out.kind = RADICLE_NAME_MALFORMED;
      out.bytes = bytes(source->raw_id, out.size);
    } else {
      out.kind = RADICLE_NAME_INLINE;
      out.bytes = bytes(source->text, out.size);
    }
    return out;
  }

  void fill(radicle_quest_alias& row, const fo4::forms::QuestAliasFill& source) {
    using namespace fo4::forms;
    std::visit([&](const auto& value) {
      using T = std::decay_t<decltype(value)>;
      if constexpr (std::is_same_v<T, QuestSpecificReferenceFill>) {
        row.fill_type = RADICLE_QUEST_FILL_SPECIFIC_REFERENCE;
        row.target = key(value.reference);
      } else if constexpr (std::is_same_v<T, QuestUniqueActorFill>) {
        row.fill_type = RADICLE_QUEST_FILL_UNIQUE_ACTOR;
        row.target = key(value.actor);
      } else if constexpr (std::is_same_v<T, QuestLocationAliasReferenceFill>) {
        row.fill_type = RADICLE_QUEST_FILL_LOCATION_ALIAS_REFERENCE;
        row.has_fill_alias = 1;
        row.fill_alias = value.alias;
        row.secondary_target = key(value.reference_type);
      } else if constexpr (std::is_same_v<T, QuestExternalAliasReferenceFill>) {
        row.fill_type = RADICLE_QUEST_FILL_EXTERNAL_ALIAS_REFERENCE;
        row.target = key(value.quest);
        row.has_fill_alias = 1;
        row.fill_alias = value.alias;
      } else if constexpr (std::is_same_v<T, QuestCreateReferenceFill>) {
        row.fill_type = RADICLE_QUEST_FILL_CREATE_REFERENCE;
        row.target = key(value.object);
        row.has_fill_alias = 1;
        row.fill_alias = value.alias;
        row.create_mode = value.create_mode;
        row.level = value.level;
      } else if constexpr (std::is_same_v<T, QuestFindMatchingReferenceFill>) {
        std::visit([&](const auto& selector) {
          using S = std::decay_t<decltype(selector)>;
          if constexpr (std::is_same_v<S, QuestMatchingReferenceNearAlias>) {
            row.fill_type = RADICLE_QUEST_FILL_MATCHING_NEAR_ALIAS;
            row.has_fill_alias = 1;
            row.fill_alias = selector.alias;
            row.near_type = selector.type;
          } else if constexpr (std::is_same_v<S, QuestMatchingReferenceEvent>) {
            row.fill_type = RADICLE_QUEST_FILL_MATCHING_REFERENCE_EVENT;
            row.event = selector.event;
            row.event_data = selector.data;
          } else if constexpr (std::is_same_v<S, QuestMatchingReferenceClosest>) {
            row.fill_type = RADICLE_QUEST_FILL_MATCHING_CLOSEST;
            row.has_fill_alias = 1;
            row.fill_alias = selector.alias;
          }
        }, value.selector);
      } else if constexpr (std::is_same_v<T, QuestSpecificLocationFill>) {
        row.fill_type = RADICLE_QUEST_FILL_SPECIFIC_LOCATION;
        row.target = key(value.location);
      } else if constexpr (std::is_same_v<T, QuestReferenceAliasLocationFill>) {
        row.fill_type = RADICLE_QUEST_FILL_REFERENCE_ALIAS_LOCATION;
        row.has_fill_alias = 1;
        row.fill_alias = value.alias;
        row.secondary_target = key(value.keyword);
      } else if constexpr (std::is_same_v<T, QuestExternalAliasLocationFill>) {
        row.fill_type = RADICLE_QUEST_FILL_EXTERNAL_ALIAS_LOCATION;
        row.target = key(value.quest);
        row.has_fill_alias = value.alias.has_value();
        row.fill_alias = value.alias.value_or(0);
      } else if constexpr (std::is_same_v<T, QuestFindMatchingLocationFill>) {
        row.fill_type = RADICLE_QUEST_FILL_MATCHING_LOCATION_EVENT;
        row.event = value.event;
        row.event_data = value.data;
      }
    }, source);
  }

  void build(const fo4::Plugin& plugin, const fo4::quest::QuestStructure& source) {
    view.source_plugin = text(plugin.key.filename());
    view.stages_supported = source.stages_supported;
    view.objectives_supported = source.objectives_supported;
    view.aliases_supported = source.aliases_supported;
    for (const auto& stage : source.stages.stages) {
      stages.push_back({stage.index, stage.flags, stage.unknown, logs.size(), stage.log_entries.size()});
      for (const auto& entry : stage.log_entries) {
        radicle_quest_log row{};
        row.flags = entry.flags;
        row.text = localized(entry.log_text);
        row.note = bytes(entry.note, row.note_size);
        logs.push_back(row);
      }
    }
    for (const auto& objective : source.objectives.objectives) {
      objectives.push_back({objective.index, objective.flags, localized(objective.display_text),
                            targets.size(), objective.targets.size()});
      for (const auto& target : objective.targets) {
        targets.push_back({target.alias, target.flags, key(target.keyword)});
      }
    }
    for (std::size_t i = 0; i < source.aliases.aliases.size(); ++i) {
      const auto& alias = source.aliases.aliases[i];
      radicle_quest_alias row{};
      row.kind = static_cast<radicle_quest_alias_kind>(alias.kind);
      row.id = alias.id;
      row.name = bytes(alias.name, row.name_size);
      row.has_flags = alias.flags.has_value();
      row.flags = alias.flags.value_or(0);
      row.has_force_into_alias = alias.force_into_alias.has_value();
      row.force_into_alias = alias.force_into_alias.value_or(0);
      row.has_closest_to_alias = alias.closest_to_alias.has_value();
      row.closest_to_alias = alias.closest_to_alias.value_or(0);
      row.fill_supported = alias.unresolved_links.empty() &&
          std::none_of(source.unsupported.begin(), source.unsupported.end(), [&](const auto& item) {
            return item.scope == fo4::quest::QuestScope::alias && item.primary == i &&
                   item.signature != fo4::sig_ctda &&
                   item.signature != fo4::Signature::from_string("CIS1") &&
                   item.signature != fo4::Signature::from_string("CIS2");
          });
      fill(row, alias.fill);
      row.has_level = row.fill_type == RADICLE_QUEST_FILL_CREATE_REFERENCE &&
          std::any_of(alias.subrecords.begin(), alias.subrecords.end(), [](const auto& member) {
        return member.signature == fo4::Signature::from_string("ALCL");
      });
      row.first_collection_entry = collection_entries.size();
      row.collection_entry_count = alias.collection_entries.size();
      for (const auto& entry : alias.collection_entries) {
        collection_entries.push_back({entry.id, static_cast<uint8_t>(entry.max_initial_fill_count.has_value()),
                                       entry.max_initial_fill_count.value_or(0)});
      }
      aliases.push_back(row);
    }
    for (const auto& list : source.condition_lists) {
      condition_lists.push_back({static_cast<radicle_quest_scope>(list.scope), list.primary, list.secondary,
                                 static_cast<uint8_t>(list.supported), conditions.size(), list.conditions.size()});
      for (const auto& condition : list.conditions) {
        radicle_quest_condition row{};
        row.flags_byte = condition.flags_byte;
        row.comparison_raw = condition.comparison_raw;
        row.function = condition.function;
        row.unknown2 = condition.unknown2;
        row.param1 = condition.param1;
        row.param2 = condition.param2;
        row.run_on = condition.run_on;
        row.reference = condition.reference;
        row.unknown3 = condition.unknown3;
        row.parameter1_string = bytes(condition.parameter1_string, row.parameter1_size);
        row.parameter2_string = bytes(condition.parameter2_string, row.parameter2_size);
        conditions.push_back(row);
      }
    }
    for (const auto& item : source.unsupported) {
      radicle_quest_unsupported row{};
      row.scope = static_cast<radicle_quest_scope>(item.scope);
      row.primary = item.primary;
      row.secondary = item.secondary;
      row.member = item.member;
      std::memcpy(row.signature, item.signature.bytes().data(), 4);
      row.reason = text(item.reason);
      unsupported.push_back(row);
    }
    view.stages = stages.empty() ? nullptr : stages.data();
    view.stage_count = stages.size();
    view.logs = logs.empty() ? nullptr : logs.data();
    view.log_count = logs.size();
    view.objectives = objectives.empty() ? nullptr : objectives.data();
    view.objective_count = objectives.size();
    view.targets = targets.empty() ? nullptr : targets.data();
    view.target_count = targets.size();
    view.aliases = aliases.empty() ? nullptr : aliases.data();
    view.alias_count = aliases.size();
    view.collection_entries = collection_entries.empty() ? nullptr : collection_entries.data();
    view.collection_entry_count = collection_entries.size();
    view.condition_lists = condition_lists.empty() ? nullptr : condition_lists.data();
    view.condition_list_count = condition_lists.size();
    view.conditions = conditions.empty() ? nullptr : conditions.data();
    view.condition_count = conditions.size();
    view.unsupported = unsupported.empty() ? nullptr : unsupported.data();
    view.unsupported_count = unsupported.size();
  }
};

extern "C" {

radicle_status radicle_quest_structure_read(const radicle_plugin* plugin, const char* form_key,
                                            radicle_quest_structure** out_structure) {
  using namespace radicle_c_detail;
  clear_error();
  if (out_structure) *out_structure = nullptr;
  if (!plugin || !form_key || !out_structure) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, form_key and out_structure are required");
  }
  return guarded("unknown exception while reading quest structure", [&] {
    fo4::FormKey key;
    try {
      key = fo4::FormKey::parse(form_key);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
    try {
      const auto source = fo4::quest::read_quest_structure(plugin->value, key);
      auto result = std::make_unique<radicle_quest_structure>();
      result->build(plugin->value, source);
      *out_structure = result.release();
      return RADICLE_STATUS_OK;
    } catch (const std::out_of_range& error) {
      return fail_exception(RADICLE_STATUS_NOT_FOUND, error);
    } catch (const std::invalid_argument& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_quest_structure_get_view(const radicle_quest_structure* structure,
                                                radicle_quest_structure_view* out_view) {
  using namespace radicle_c_detail;
  clear_error();
  if (out_view) *out_view = {};
  if (!structure || !out_view) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "structure and out_view are required");
  }
  *out_view = structure->view;
  return RADICLE_STATUS_OK;
}

void radicle_quest_structure_destroy(radicle_quest_structure* structure) {
  delete structure;
}

}
