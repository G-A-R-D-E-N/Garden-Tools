#include "fo4/c_api_quest.h"

#include "c_api_env_internal.hpp"
#include "fo4/forms/quest.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/schema/scalar.hpp"
#include "fo4/signature.hpp"
#include "fo4/text/encoding.hpp"
#include "fo4/plugin_core/subrecord_ops.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;
namespace forms = fo4::forms;

struct Refusal {
  radicle_status status;
  std::string message;
};

[[noreturn]] void refuse_argument(std::string message) {
  throw Refusal{RADICLE_STATUS_INVALID_ARGUMENT, std::move(message)};
}

[[noreturn]] void refuse_missing(std::string message) {
  throw Refusal{RADICLE_STATUS_NOT_FOUND, std::move(message)};
}

template <class Edit>
radicle_status quest_edit(radicle_plugin* plugin, radicle_form_key key, Edit edit) {
  clear_error();
  if (plugin == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin is required");
  return guarded("unknown exception while editing a quest", [&] {
    const auto record_key = to_form_key(key);
    if (!record_key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid record key is required");
    try {
      const auto* record = fo4::find_record(plugin->value, *record_key);
      if (record == nullptr) refuse_missing("plugin has no such record");
      if (record->header.signature != fo4::sig_qust) refuse_argument("record is not a QUST");
      edit(plugin->value, *record_key);
      return RADICLE_STATUS_OK;
    } catch (const Refusal& refusal) {
      return fail(refusal.status, refusal.message.c_str());
    } catch (const forms::QuestError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::schema::ScalarError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

std::optional<fo4::FormKey> optional_key(radicle_form_key key) {
  if (key.plugin == nullptr) return std::nullopt;
  const auto parsed = to_form_key(key);
  if (!parsed) refuse_argument("invalid FormKey");
  return parsed;
}

fo4::FormKey required_key(radicle_form_key key, const char* what) {
  const auto parsed = optional_key(key);
  if (!parsed) refuse_argument(std::string(what) + " is required for this fill type");
  return *parsed;
}

std::string stored_text(const char* utf8, const char* what) {
  auto converted = fo4::text::utf8_to_windows1252(utf8);
  if (!converted) refuse_argument(std::string(what) + " must be valid UTF-8 representable in Windows-1252");
  return std::move(*converted);
}

std::optional<fo4::schema::LocalizedString> localized_text(const fo4::Plugin& plugin, radicle_name_kind kind,
                                                           const char* text, uint32_t string_id,
                                                           const char* field) {
  const bool localized = plugin.header.tes4_flags.is_localized();
  fo4::schema::LocalizedString value;
  switch (kind) {
    case RADICLE_NAME_ABSENT:
      return std::nullopt;
    case RADICLE_NAME_INLINE:
      if (text == nullptr) refuse_argument(std::string("inline ") + field + " is required");
      if (localized) refuse_argument(std::string("a localized plugin takes a string id, not inline ") + field);
      value.text = stored_text(text, field);
      return value;
    case RADICLE_NAME_STRING_ID:
      if (!localized) {
        refuse_argument(std::string("a non-localized plugin takes inline ") + field + ", not a string id");
      }
      value.id = string_id;
      return value;
    default:
      refuse_argument(std::string("unsupported ") + field + " kind");
  }
}

void set_quest_log_member(std::vector<fo4::Subrecord>& members, fo4::Signature signature,
                          std::vector<std::byte> data) {
  const auto found = std::find_if(members.begin(), members.end(),
                                  [&](const fo4::Subrecord& member) { return member.signature == signature; });
  if (found == members.end()) {
    const auto rank = [](fo4::Signature sig) { return sig == fo4::sig_nam2 ? 1 : sig == fo4::sig_cnam ? 2 : sig == fo4::sig_nam0 ? 3 : 0; };
    const auto insertion = std::find_if(members.begin(), members.end(), [&](const fo4::Subrecord& member) {
      return rank(member.signature) > rank(signature);
    });
    members.insert(insertion, fo4::Subrecord{signature, std::move(data), false});
  } else {
    found->data = std::move(data);
  }
}

forms::QuestObjective& find_objective(forms::QuestObjectives& objectives, uint16_t index) {
  const auto found = std::find_if(objectives.objectives.begin(), objectives.objectives.end(),
                                  [&](const forms::QuestObjective& item) { return item.index == index; });
  if (found == objectives.objectives.end()) refuse_missing("quest has no objective with that index");
  return *found;
}

void alias_fill(forms::QuestAliasFill& fill, const radicle_quest_alias_input& input) {
  using namespace forms;
  const auto alias = [&] {
    if (!input.has_fill_alias) refuse_argument("fill_alias is required for this fill type");
    return input.fill_alias;
  };
  switch (input.fill_type) {
    case RADICLE_QUEST_FILL_NONE:
      fill.emplace<std::monostate>();
      return;
    case RADICLE_QUEST_FILL_SPECIFIC_REFERENCE:
      fill.emplace<QuestSpecificReferenceFill>(optional_key(input.target));
      return;
    case RADICLE_QUEST_FILL_UNIQUE_ACTOR:
      fill.emplace<QuestUniqueActorFill>(required_key(input.target, "target"));
      return;
    case RADICLE_QUEST_FILL_LOCATION_ALIAS_REFERENCE: {
      const auto id = alias();
      fill.emplace<QuestLocationAliasReferenceFill>(id, required_key(input.secondary_target, "secondary_target"));
      return;
    }
    case RADICLE_QUEST_FILL_EXTERNAL_ALIAS_REFERENCE: {
      auto target = optional_key(input.target);
      fill.emplace<QuestExternalAliasReferenceFill>(std::move(target), alias());
      return;
    }
    case RADICLE_QUEST_FILL_CREATE_REFERENCE: {
      const auto value = alias();
      if (value < INT16_MIN || value > INT16_MAX) refuse_argument("create reference alias does not fit 16 bits");
      fill.emplace<QuestCreateReferenceFill>(optional_key(input.target), static_cast<std::int16_t>(value),
                                             input.create_mode, input.level);
      return;
    }
    case RADICLE_QUEST_FILL_MATCHING_NEAR_ALIAS: {
      auto& matching = fill.emplace<QuestFindMatchingReferenceFill>();
      matching.selector.emplace<QuestMatchingReferenceNearAlias>(alias(), input.near_type);
      return;
    }
    case RADICLE_QUEST_FILL_MATCHING_REFERENCE_EVENT: {
      auto& matching = fill.emplace<QuestFindMatchingReferenceFill>();
      matching.selector.emplace<QuestMatchingReferenceEvent>(input.event, input.event_data);
      return;
    }
    case RADICLE_QUEST_FILL_MATCHING_CLOSEST: {
      auto& matching = fill.emplace<QuestFindMatchingReferenceFill>();
      matching.selector.emplace<QuestMatchingReferenceClosest>(alias());
      return;
    }
    case RADICLE_QUEST_FILL_SPECIFIC_LOCATION:
      fill.emplace<QuestSpecificLocationFill>(required_key(input.target, "target"));
      return;
    case RADICLE_QUEST_FILL_REFERENCE_ALIAS_LOCATION: {
      const auto id = alias();
      fill.emplace<QuestReferenceAliasLocationFill>(id, optional_key(input.secondary_target));
      return;
    }
    case RADICLE_QUEST_FILL_EXTERNAL_ALIAS_LOCATION:
      fill.emplace<QuestExternalAliasLocationFill>(optional_key(input.target),
                                                    input.has_fill_alias ? std::optional(input.fill_alias) : std::nullopt);
      return;
    case RADICLE_QUEST_FILL_MATCHING_LOCATION_EVENT:
      fill.emplace<QuestFindMatchingLocationFill>(input.event, input.event_data);
      return;
  }
  refuse_argument("unsupported alias fill type");
}

forms::QuestAliasCreation alias_creation(const radicle_quest_alias_input& input, std::string& name_storage) {
  if (input.kind != RADICLE_QUEST_ALIAS_REFERENCE && input.kind != RADICLE_QUEST_ALIAS_LOCATION &&
      input.kind != RADICLE_QUEST_ALIAS_COLLECTION) {
    refuse_argument("unsupported alias kind");
  }
  if (input.collection_entry_count != 0 && input.collection_entries == nullptr) {
    refuse_argument("collection_entries is required when collection_entry_count is not zero");
  }
  forms::QuestAliasCreation creation;
  creation.kind = static_cast<forms::QuestAliasKind>(input.kind);
  creation.id = input.id;
  if (input.name != nullptr) {
    name_storage = stored_text(input.name, "alias name");
    creation.name = std::string_view(name_storage);
  }
  if (input.kind != RADICLE_QUEST_ALIAS_COLLECTION) creation.flags = input.flags;
  if (input.has_force_into_alias) creation.force_into_alias = input.force_into_alias;
  if (input.has_closest_to_alias) creation.closest_to_alias = input.closest_to_alias;
  alias_fill(creation.fill, input);
  for (size_t i = 0; i < input.collection_entry_count; ++i) {
    const auto& entry = input.collection_entries[i];
    creation.collection_entries.push_back(
        {entry.id, entry.has_max_initial_fill_count ? std::optional(entry.max_initial_fill_count) : std::nullopt});
  }
  return creation;
}

}  // namespace

extern "C" {

radicle_status radicle_plugin_add_quest_stage(radicle_plugin* plugin, radicle_form_key key, uint16_t index,
                                              uint8_t flags) {
  return quest_edit(plugin, key, [&](fo4::Plugin& value, const fo4::FormKey& quest) {
    forms::QuestStage stage;
    stage.index = index;
    stage.flags = flags;
    forms::add_quest_stage(value, quest, stage);
  });
}

radicle_status radicle_plugin_set_quest_stage_flags(radicle_plugin* plugin, radicle_form_key key, uint16_t index,
                                                    uint8_t flags) {
  return quest_edit(plugin, key, [&](fo4::Plugin& value, const fo4::FormKey& quest) {
    auto stages = forms::decode_quest_stages(value, quest);
    const auto found = std::find_if(stages.stages.begin(), stages.stages.end(),
                                    [&](const forms::QuestStage& stage) { return stage.index == index; });
    if (found == stages.stages.end()) refuse_missing("quest has no stage with that index");
    found->flags = flags;
    forms::write_quest_stages(value, quest, stages);
  });
}

radicle_status radicle_plugin_remove_quest_stage(radicle_plugin* plugin, radicle_form_key key, uint16_t index) {
  return quest_edit(plugin, key, [&](fo4::Plugin& value, const fo4::FormKey& quest) {
    if (!forms::remove_quest_stage(value, quest, index)) refuse_missing("quest has no stage with that index");
  });
}

radicle_status radicle_plugin_reorder_quest_stage(radicle_plugin* plugin, radicle_form_key key, uint16_t index,
                                                  size_t to) {
  return quest_edit(plugin, key, [&](fo4::Plugin& value, const fo4::FormKey& quest) {
    if (!forms::reorder_quest_stage(value, quest, index, to)) refuse_missing("quest has no stage with that index");
  });
}

radicle_status radicle_plugin_set_quest_log_entry(radicle_plugin* plugin, radicle_form_key key,
                                                  uint16_t stage_index, size_t log_index, uint8_t flags,
                                                  radicle_name_kind text_kind, const char* text,
                                                  uint32_t text_string_id, const char* note) {
  return quest_edit(plugin, key, [&](fo4::Plugin& value, const fo4::FormKey& quest) {
    auto stages = forms::decode_quest_stages(value, quest);
    const auto stage = std::find_if(stages.stages.begin(), stages.stages.end(),
                                    [&](const forms::QuestStage& item) { return item.index == stage_index; });
    if (stage == stages.stages.end()) refuse_missing("quest has no stage with that index");
    if (log_index >= stage->log_entries.size()) refuse_missing("quest stage has no log entry at that index");

    auto& entry = stage->log_entries[log_index];
    entry.flags = flags;
    if (auto replacement = localized_text(value, text_kind, text, text_string_id, "quest log text")) {
      set_quest_log_member(entry.members, fo4::sig_cnam, fo4::schema::write_localized_string(*replacement));
      entry.log_text = std::move(replacement);
    }
    if (note != nullptr) {
      auto stored = stored_text(note, "quest log note");
      set_quest_log_member(entry.members, fo4::sig_nam2, fo4::encode_zstring(stored));
      entry.note = std::move(stored);
    }
    forms::write_quest_stages(value, quest, stages);
  });
}

radicle_status radicle_plugin_add_quest_log_entry(radicle_plugin* plugin, radicle_form_key key,
                                                  uint16_t stage_index, uint8_t flags,
                                                  radicle_name_kind text_kind, const char* text,
                                                  uint32_t text_string_id, const char* note) {
  return quest_edit(plugin, key, [&](fo4::Plugin& value, const fo4::FormKey& quest) {
    auto stages = forms::decode_quest_stages(value, quest);
    const auto stage = std::find_if(stages.stages.begin(), stages.stages.end(),
                                    [&](const forms::QuestStage& item) { return item.index == stage_index; });
    if (stage == stages.stages.end()) refuse_missing("quest has no stage with that index");

    forms::QuestLogEntry entry;
    entry.flags = flags;
    if (auto text_value = localized_text(value, text_kind, text, text_string_id, "quest log text")) {
      entry.log_text = *text_value;
    }
    if (note != nullptr) entry.note = stored_text(note, "quest log note");
    if (entry.note.has_value()) {
      entry.members.push_back(fo4::Subrecord{fo4::sig_nam2, fo4::encode_zstring(*entry.note), false});
    }
    if (entry.log_text.has_value()) {
      entry.members.push_back(fo4::Subrecord{fo4::sig_cnam,
                                            fo4::schema::write_localized_string(*entry.log_text), false});
    }
    stage->log_entries.push_back(std::move(entry));
    forms::write_quest_stages(value, quest, stages);
  });
}

radicle_status radicle_plugin_add_quest_objective(radicle_plugin* plugin, radicle_form_key key, uint16_t index,
                                                  uint32_t flags, radicle_name_kind text_kind, const char* text,
                                                  uint32_t text_string_id) {
  return quest_edit(plugin, key, [&](fo4::Plugin& value, const fo4::FormKey& quest) {
    forms::QuestObjective objective;
    objective.index = index;
    objective.flags = flags;
    objective.display_text = localized_text(value, text_kind, text, text_string_id, "objective text");
    forms::add_quest_objective(value, quest, objective);
  });
}

radicle_status radicle_plugin_set_quest_objective(radicle_plugin* plugin, radicle_form_key key, uint16_t index,
                                                  uint32_t flags, radicle_name_kind text_kind, const char* text,
                                                  uint32_t text_string_id) {
  return quest_edit(plugin, key, [&](fo4::Plugin& value, const fo4::FormKey& quest) {
    auto objectives = forms::decode_quest_objectives(value, quest);
    auto& objective = find_objective(objectives, index);
    objective.flags = flags;
    if (auto replacement = localized_text(value, text_kind, text, text_string_id, "objective text")) {
      objective.display_text = std::move(replacement);
    }
    forms::write_quest_objectives(value, quest, objectives);
  });
}

radicle_status radicle_plugin_remove_quest_objective(radicle_plugin* plugin, radicle_form_key key, uint16_t index) {
  return quest_edit(plugin, key, [&](fo4::Plugin& value, const fo4::FormKey& quest) {
    if (!forms::remove_quest_objective(value, quest, index)) refuse_missing("quest has no objective with that index");
  });
}

radicle_status radicle_plugin_reorder_quest_objective(radicle_plugin* plugin, radicle_form_key key, uint16_t index,
                                                      size_t to) {
  return quest_edit(plugin, key, [&](fo4::Plugin& value, const fo4::FormKey& quest) {
    if (!forms::reorder_quest_objective(value, quest, index, to)) {
      refuse_missing("quest has no objective with that index");
    }
  });
}

radicle_status radicle_plugin_add_quest_objective_target(radicle_plugin* plugin, radicle_form_key key,
                                                         uint16_t objective_index, int32_t alias, uint32_t flags,
                                                         radicle_form_key keyword) {
  return quest_edit(plugin, key, [&](fo4::Plugin& value, const fo4::FormKey& quest) {
    auto objectives = forms::decode_quest_objectives(value, quest);
    auto& objective = find_objective(objectives, objective_index);
    forms::QuestObjectiveTarget target;
    target.alias = alias;
    target.flags = flags;
    target.keyword = optional_key(keyword);
    objective.targets.push_back(std::move(target));
    forms::write_quest_objectives(value, quest, objectives);
  });
}

radicle_status radicle_plugin_set_quest_objective_target(radicle_plugin* plugin, radicle_form_key key,
                                                         uint16_t objective_index, size_t target_index,
                                                         int32_t alias, uint32_t flags,
                                                         radicle_form_key keyword) {
  return quest_edit(plugin, key, [&](fo4::Plugin& value, const fo4::FormKey& quest) {
    auto objectives = forms::decode_quest_objectives(value, quest);
    auto& objective = find_objective(objectives, objective_index);
    if (target_index >= objective.targets.size()) refuse_missing("objective has no target at that index");
    auto& target = objective.targets[target_index];
    target.alias = alias;
    target.flags = flags;
    target.keyword = optional_key(keyword);
    target.keyword_unresolved = false;
    forms::write_quest_objectives(value, quest, objectives);
  });
}

radicle_status radicle_plugin_remove_quest_objective_target(radicle_plugin* plugin, radicle_form_key key,
                                                            uint16_t objective_index, size_t target_index) {
  return quest_edit(plugin, key, [&](fo4::Plugin& value, const fo4::FormKey& quest) {
    auto objectives = forms::decode_quest_objectives(value, quest);
    auto& objective = find_objective(objectives, objective_index);
    if (target_index >= objective.targets.size()) refuse_missing("objective has no target at that index");
    objective.targets.erase(objective.targets.begin() + static_cast<std::ptrdiff_t>(target_index));
    forms::write_quest_objectives(value, quest, objectives);
  });
}

radicle_status radicle_plugin_add_quest_alias(radicle_plugin* plugin, radicle_form_key key,
                                              const radicle_quest_alias_input* alias) {
  if (alias == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "alias is required");
  return quest_edit(plugin, key, [&](fo4::Plugin& value, const fo4::FormKey& quest) {
    std::string name;
    forms::add_quest_alias(value, quest, alias_creation(*alias, name));
  });
}

radicle_status radicle_plugin_replace_quest_alias(radicle_plugin* plugin, radicle_form_key key, uint32_t alias_id,
                                                  const radicle_quest_alias_input* alias) {
  if (alias == nullptr) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "alias is required");
  return quest_edit(plugin, key, [&](fo4::Plugin& value, const fo4::FormKey& quest) {
    std::string name;
    const auto creation = alias_creation(*alias, name);
    if (creation.kind == forms::QuestAliasKind::collection) refuse_argument("a collection alias cannot be replaced by id");
    const auto aliases = forms::decode_quest_aliases(value, quest);
    const auto found = std::any_of(aliases.aliases.begin(), aliases.aliases.end(), [&](const forms::QuestAlias& item) {
      return item.kind != forms::QuestAliasKind::collection && item.id == alias_id;
    });
    if (!found) refuse_missing("quest has no alias with that id");
    forms::replace_quest_alias(value, quest, alias_id, creation);
  });
}

radicle_status radicle_plugin_remove_quest_alias(radicle_plugin* plugin, radicle_form_key key, uint32_t alias_id) {
  return quest_edit(plugin, key, [&](fo4::Plugin& value, const fo4::FormKey& quest) {
    if (!forms::remove_quest_alias(value, quest, alias_id)) refuse_missing("quest has no alias with that id");
  });
}

radicle_status radicle_plugin_reorder_quest_alias(radicle_plugin* plugin, radicle_form_key key, uint32_t alias_id,
                                                  size_t to) {
  return quest_edit(plugin, key, [&](fo4::Plugin& value, const fo4::FormKey& quest) {
    if (!forms::reorder_quest_alias(value, quest, alias_id, to)) refuse_missing("quest has no alias with that id");
  });
}

}
