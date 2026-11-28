#include "c_api_internal.hpp"
#include "c_api_env_internal.hpp"
#include "fo4/c_api_perk.h"
#include "fo4/forms/perk.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/text/encoding.hpp"

#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;

constexpr auto k_perk = fo4::Signature::from_string("PERK");

// Constraint: the effect vector is the array itself, so the first element's internal is the handle radicle_perk_effects_free gets back.
struct PerkEffectsHolder {
  std::vector<radicle_perk_effect> effects;
  std::vector<std::vector<radicle_perk_condition_tab>> tabs;
  std::vector<std::vector<std::vector<radicle_perk_condition>>> conditions;
  std::deque<std::string> texts;
  std::deque<std::vector<std::uint8_t>> payloads;
  std::deque<std::vector<std::uint8_t>> raw_labels;

  radicle_form_key key(const fo4::FormKey& value) {
    if (value.owner().empty()) return {nullptr, 0};
    texts.emplace_back(value.owner().filename());
    return {texts.back().c_str(), value.local().value()};
  }

  const char* text(const std::string& stored) {
    texts.push_back(fo4::text::windows1252_to_utf8(stored));
    return texts.back().c_str();
  }

  const std::uint8_t* bytes(const std::vector<std::byte>& value, std::size_t& size) {
    std::vector<std::uint8_t> copy(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) copy[i] = std::to_integer<std::uint8_t>(value[i]);
    payloads.push_back(std::move(copy));
    size = payloads.back().size();
    return payloads.back().data();
  }
};

radicle_perk_condition to_c_condition(const fo4::CtdaCondition& in, PerkEffectsHolder& holder) {
  radicle_perk_condition out{};
  out.flags = in.flags_byte;
  std::memcpy(out.padding, in.padding.data(), sizeof(out.padding));
  out.comparison_raw = in.comparison_raw;
  out.function = in.function;
  out.unknown2 = in.unknown2;
  out.param1 = in.param1;
  out.param2 = in.param2;
  out.run_on = in.run_on;
  out.reference = in.reference;
  out.unknown3 = in.unknown3;
  if (in.parameter1_string.has_value()) out.parameter1 = holder.text(*in.parameter1_string);
  if (in.parameter2_string.has_value()) out.parameter2 = holder.text(*in.parameter2_string);
  return out;
}

std::string stored_text(const char* utf8) {
  const auto stored = fo4::text::utf8_to_windows1252(utf8);
  if (!stored) {
    throw std::invalid_argument("PERK text is not valid UTF-8 representable in Windows-1252");
  }
  return *stored;
}

std::vector<fo4::forms::PerkEffectEntry> to_entries(const radicle_perk_effect* effects,
                                                    std::size_t count) {
  std::vector<fo4::forms::PerkEffectEntry> entries;
  entries.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    const auto& in = effects[index];
    if (in.type > 2) throw std::invalid_argument("PERK effect type must be 0, 1 or 2");
    fo4::forms::PerkEffectEntry entry;
    entry.type = static_cast<fo4::forms::PerkEffectType>(in.type);
    entry.rank = in.rank;
    entry.priority = in.priority;
    entry.quest_stage = in.quest_stage;
    entry.entry_point = in.entry_point;
    entry.function = in.function;
// Constraint: The count byte exists only in the entry-point DATA arm; the other arms carry their tab runs without it, so only that arm is validated and derived here.
    if (in.type == 2) {
      if (in.tab_count > 0xFF || in.condition_tab_count != in.tab_count) {
        throw std::invalid_argument(
            "PERK effect condition_tab_count must match tab_count");
      }
      entry.condition_tab_count = static_cast<std::uint8_t>(in.tab_count);
    }

    if (entry.type == fo4::forms::PerkEffectType::quest_stage) {
      const auto quest = to_form_key(in.quest);
      if (!quest) throw std::invalid_argument("a quest effect needs a quest form key");
      entry.quest = *quest;
    } else if (entry.type == fo4::forms::PerkEffectType::ability) {
      const auto ability = to_form_key(in.ability);
      if (!ability) throw std::invalid_argument("an ability effect needs an ability form key");
      entry.ability = *ability;
    }

    if (in.tab_count != 0 && in.tabs == nullptr) {
      throw std::invalid_argument("PERK effect tabs are required when tab_count is non-zero");
    }
    for (std::size_t tab_index = 0; tab_index < in.tab_count; ++tab_index) {
      const auto& in_tab = in.tabs[tab_index];
      if (in_tab.condition_count != 0 && in_tab.conditions == nullptr) {
        throw std::invalid_argument("PERK condition array is required when its count is non-zero");
      }
      fo4::forms::PerkConditionTab tab;
      tab.run_on = in_tab.run_on;
      for (std::size_t condition_index = 0; condition_index < in_tab.condition_count;
           ++condition_index) {
        const auto& in_condition = in_tab.conditions[condition_index];
        fo4::CtdaCondition condition;
        condition.flags_byte = in_condition.flags;
        std::memcpy(condition.padding.data(), in_condition.padding, condition.padding.size());
        condition.comparison_raw = in_condition.comparison_raw;
        condition.function = in_condition.function;
        condition.unknown2 = in_condition.unknown2;
        condition.param1 = in_condition.param1;
        condition.param2 = in_condition.param2;
        condition.run_on = in_condition.run_on;
        condition.reference = in_condition.reference;
        condition.unknown3 = in_condition.unknown3;
        if (in_condition.parameter1 != nullptr) {
          condition.parameter1_string = stored_text(in_condition.parameter1);
        }
        if (in_condition.parameter2 != nullptr) {
          condition.parameter2_string = stored_text(in_condition.parameter2);
        }
        tab.conditions.push_back(std::move(condition));
      }
      entry.tabs.push_back(std::move(tab));
    }

    if (in.has_parameter_type) entry.parameters.type = in.parameter_type;
    if (in.has_entry_id) entry.parameters.entry_id = in.entry_id;
    if (in.has_script_flags) entry.parameters.script_flags = in.script_flags;
    if (in.has_button_label) {
      fo4::schema::LocalizedString label;
      if (in.button_label_raw_size != 0) {
        if (in.button_label_raw == nullptr) {
          throw std::invalid_argument("button_label_raw is required when its size is non-zero");
        }
        std::vector<std::byte> raw(in.button_label_raw_size);
        std::memcpy(raw.data(), in.button_label_raw, in.button_label_raw_size);
        label.raw_id = std::move(raw);
      } else if (in.button_label != nullptr) {
        label.text = stored_text(in.button_label);
      } else {
        label.id = in.button_label_id;
      }
      entry.parameters.button_label = std::move(label);
    }
    if (in.data_size != 0) {
      if (in.data == nullptr) {
        throw std::invalid_argument("data is required when data_size is non-zero");
      }
      std::vector<std::byte> data(in.data_size);
      std::memcpy(data.data(), in.data, in.data_size);
      entry.parameters.data = std::move(data);
    }
    entries.push_back(std::move(entry));
  }
  return entries;
}

radicle_status create_status(fo4::RecordCreateStatus status) {
  switch (status) {
    case fo4::RecordCreateStatus::not_authorable:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "PERK has no authoring schema");
    case fo4::RecordCreateStatus::no_default:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a required PERK subrecord has no registered default");
    case fo4::RecordCreateStatus::invalid_editor_id:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "editor ID cannot be stored for a PERK record");
    case fo4::RecordCreateStatus::preflight_failed:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "the default PERK record failed authoring preflight");
    case fo4::RecordCreateStatus::created:
      break;
  }
  return RADICLE_STATUS_OK;
}

radicle_status effect_status(fo4::forms::PerkEffectWrite status) {
  switch (status) {
    case fo4::forms::PerkEffectWrite::written:
      return RADICLE_STATUS_OK;
    case fo4::forms::PerkEffectWrite::record_missing:
      return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
    case fo4::forms::PerkEffectWrite::unsupported_record:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record is not a PERK");
    case fo4::forms::PerkEffectWrite::unexpected_layout:
      return fail(RADICLE_STATUS_PARSE_ERROR, "PERK effect block is not a trailing subrecord run");
  }
  return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown PERK effect write status");
}

}  // namespace

extern "C" {

radicle_status radicle_plugin_perk_effects(const radicle_plugin* plugin,
                                           radicle_form_key key,
                                           radicle_perk_effect** out_effects,
                                           size_t* out_count) {
  clear_error();
  if (out_effects != nullptr) *out_effects = nullptr;
  if (out_count != nullptr) *out_count = 0;
  if (plugin == nullptr || out_effects == nullptr || out_count == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, effects and count outputs are required");
  }
  const auto record_key = to_form_key(key);
  if (!record_key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "invalid PERK form key");
  return guarded("unknown exception while reading PERK effects", [&] {
    const auto* record = fo4::find_record(plugin->value, *record_key);
    if (record == nullptr) return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
    if (record->header.signature != k_perk) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "record is not a PERK");
    }

    std::vector<fo4::forms::PerkEffectEntry> entries;
    try {
      entries = fo4::forms::read_perk_effects(plugin->value, *record);
    } catch (const fo4::forms::PerkError& error) {
      return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
    }
    if (entries.empty()) return RADICLE_STATUS_OK;

    auto holder = std::make_unique<PerkEffectsHolder>();
    holder->effects.resize(entries.size());
    holder->tabs.resize(entries.size());
    holder->conditions.resize(entries.size());

    for (std::size_t index = 0; index < entries.size(); ++index) {
      const auto& source = entries[index];
      auto& view = holder->effects[index];
      view.type = static_cast<std::uint8_t>(source.type);
      view.rank = source.rank;
      view.priority = source.priority;
      view.quest = holder->key(source.quest);
      view.quest_stage = source.quest_stage;
      view.ability = holder->key(source.ability);
      view.entry_point = source.entry_point;
      view.function = source.function;
      view.condition_tab_count = source.condition_tab_count;

      auto& tabs = holder->tabs[index];
      auto& condition_storage = holder->conditions[index];
      tabs.resize(source.tabs.size());
      condition_storage.resize(source.tabs.size());
      for (std::size_t tab_index = 0; tab_index < source.tabs.size(); ++tab_index) {
        const auto& source_tab = source.tabs[tab_index];
        auto& conditions = condition_storage[tab_index];
        conditions.reserve(source_tab.conditions.size());
        for (const auto& condition : source_tab.conditions) {
          conditions.push_back(to_c_condition(condition, *holder));
        }
        auto& tab = tabs[tab_index];
        tab.run_on = source_tab.run_on;
        tab.conditions = conditions.empty() ? nullptr : conditions.data();
        tab.condition_count = conditions.size();
      }
      view.tabs = tabs.empty() ? nullptr : tabs.data();
      view.tab_count = tabs.size();

      if (source.parameters.type.has_value()) {
        view.has_parameter_type = 1;
        view.parameter_type = *source.parameters.type;
      }
      if (source.parameters.entry_id.has_value()) {
        view.has_entry_id = 1;
        view.entry_id = *source.parameters.entry_id;
      }
      if (source.parameters.script_flags.has_value()) {
        view.has_script_flags = 1;
        view.script_flags = *source.parameters.script_flags;
      }
      if (source.parameters.button_label.has_value()) {
        const auto& label = *source.parameters.button_label;
        view.has_button_label = 1;
        if (label.text.has_value()) {
          view.button_label = holder->text(*label.text);
        } else if (label.id.has_value()) {
          view.button_label_id = *label.id;
        } else if (label.raw_id.has_value()) {
          std::size_t size = 0;
          view.button_label_raw = holder->bytes(*label.raw_id, size);
          view.button_label_raw_size = size;
        }
      }
      if (source.parameters.data.has_value()) {
        std::size_t size = 0;
        view.data = holder->bytes(*source.parameters.data, size);
        view.data_size = size;
      }
      view.internal = holder.get();
    }

    auto* released = holder.release();
    *out_effects = released->effects.data();
    *out_count = released->effects.size();
    return RADICLE_STATUS_OK;
  });
}

void radicle_perk_effects_free(radicle_perk_effect* effects) {
  if (effects == nullptr || effects[0].internal == nullptr) return;
  delete static_cast<PerkEffectsHolder*>(effects[0].internal);
}

radicle_status radicle_plugin_set_perk_effects(radicle_plugin* plugin,
                                               radicle_form_key key,
                                               const radicle_perk_effect* effects,
                                               size_t count) {
  clear_error();
  if (plugin == nullptr || (count != 0 && effects == nullptr)) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and an effects array are required");
  }
  const auto record_key = to_form_key(key);
  if (!record_key) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "invalid PERK form key");
  return guarded("unknown exception while writing PERK effects", [&] {
    std::vector<fo4::forms::PerkEffectEntry> entries;
    try {
      entries = to_entries(effects, count);
      return effect_status(fo4::forms::set_perk_effects(plugin->value, *record_key, entries));
    } catch (const std::invalid_argument& error) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, error.what());
    } catch (const fo4::forms::PerkError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    } catch (const fo4::FormIdError& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

radicle_status radicle_plugin_create_perk(radicle_plugin* plugin,
                                          const char* editor_id_utf8,
                                          const radicle_perk_effect* effects,
                                          size_t count,
                                          radicle_form_key* out_key) {
  clear_error();
  if (out_key != nullptr) *out_key = {nullptr, 0};
  if (plugin == nullptr || editor_id_utf8 == nullptr || out_key == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin, editor ID and out_key are required");
  }
  if (count != 0 && effects == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "an effects array is required when count is non-zero");
  }
  return guarded("unknown exception while creating a PERK", [&] {
    const auto stored = fo4::text::utf8_to_windows1252(editor_id_utf8);
    if (!stored) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                  "editor ID is not valid UTF-8 representable in Windows-1252");
    }

    std::vector<fo4::forms::PerkEffectEntry> entries;
    try {
      entries = to_entries(effects, count);
    } catch (const std::invalid_argument& error) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, error.what());
    }

    auto copy = plugin->value;
    const auto created = fo4::create_record(copy, k_perk, *stored);
    const auto create_result = create_status(created.status);
    if (create_result != RADICLE_STATUS_OK) return create_result;

    if (!entries.empty()) {
      auto written = fo4::forms::PerkEffectWrite::written;
      try {
        written = fo4::forms::set_perk_effects(copy, created.key, entries);
      } catch (const fo4::forms::PerkError& error) {
        return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
      } catch (const fo4::FormIdError& error) {
        return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
      }
      const auto effect_result = effect_status(written);
      if (effect_result != RADICLE_STATUS_OK) return effect_result;
    }

    plugin->value = std::move(copy);
    *out_key = {plugin->value.key.filename().data(), created.key.local().value()};
    return RADICLE_STATUS_OK;
  });
}

}  // extern "C"
