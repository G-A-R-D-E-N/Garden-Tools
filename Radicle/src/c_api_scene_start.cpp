#include "c_api_env_internal.hpp"
#include "c_api_forms_internal.hpp"
#include "fo4/c_api_dialogue.h"
#include "fo4/forms/scene.hpp"
#include "fo4/text/encoding.hpp"

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace radicle_c_detail;
using namespace radicle_c_env_detail;
using namespace radicle_c_forms_detail;
using Status = fo4::forms::SceneStartSceneStatus;

radicle_status status_result(Status status) {
  switch (status) {
    case Status::ok:
    case Status::unchanged:
      return RADICLE_STATUS_OK;
    case Status::record_missing:
      return fail(RADICLE_STATUS_NOT_FOUND, "plugin has no such record");
    case Status::action_missing:
      return fail(RADICLE_STATUS_NOT_FOUND, "scene has no action at that index");
    case Status::action_not_start_scene:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT, "action is not a type-4 Start Scene action");
    case Status::index_out_of_range:
      return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                  "the start scene index is past the entries the action holds");
    case Status::structure_invalid:
      return fail(RADICLE_STATUS_PARSE_ERROR,
                  "the entry does not read back at its index, so the plugin is unchanged");
  }
  return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown start scene authoring status");
}

struct StartSceneHolder {
  radicle_scene_start_entries view{};
  std::vector<radicle_scene_start_entry> entries;
  std::vector<std::vector<radicle_dialogue_condition>> conditions;
  std::deque<std::string> text;
  // Constraint: a returned form key points at its owner's file name, so the names live in a deque that keeps every c_str() stable for the holder's lifetime.
  std::deque<std::string> plugins;
};

radicle_form_key owned_key(StartSceneHolder& holder, const fo4::FormKey& key) {
  if (key.owner().empty()) return {nullptr, 0};
  holder.plugins.emplace_back(key.owner().filename());
  return {holder.plugins.back().c_str(), key.local().value()};
}

const char* utf8_text(std::deque<std::string>& text, const std::optional<std::string>& value) {
  if (!value) return nullptr;
  text.push_back(fo4::text::windows1252_to_utf8(*value));
  return text.back().c_str();
}

radicle_dialogue_condition condition_row(std::deque<std::string>& text,
                                        const fo4::CtdaCondition& condition) {
  return {condition.flags_byte,
          condition.comparison_raw,
          condition.function,
          condition.unknown2,
          condition.param1,
          condition.param2,
          condition.run_on,
          condition.reference,
          condition.unknown3,
          utf8_text(text, condition.parameter1_string),
          utf8_text(text, condition.parameter2_string)};
}

radicle_status entry_from_spec(const radicle_scene_start_entry_spec& spec,
                               fo4::forms::SceneStartSceneEntry& entry) {
  if (spec.has_phase_name != 0 && spec.has_phase_name != 1) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "has_phase_name must be 0 or 1");
  }
  const auto scene = to_form_key(spec.scene);
  if (!scene) return fail(RADICLE_STATUS_INVALID_ARGUMENT, "a valid scene form key is required");
  // Constraint: SSPN is stored Windows-1252 and read back through windows1252_to_utf8, so the UTF-8 name is
  // converted here; storing the caller's bytes would turn every non-ASCII name into mojibake on the next read.
  std::optional<std::string> phase_name;
  if (spec.phase_name != nullptr) {
    if (!fo4::text::is_valid_utf8(spec.phase_name)) return bad_text();
    phase_name = fo4::text::utf8_to_windows1252(spec.phase_name);
    if (!phase_name) return bad_text();
  }
  if (spec.conditions == nullptr && spec.condition_count != 0) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "conditions must be present when the count is not");
  }
  entry.scene = *scene;
  entry.phase_index = spec.phase_index;
  if (spec.has_phase_name != 0) {
    entry.phase_name = phase_name ? std::move(*phase_name) : std::string{};
  }
  // Constraint: SCEN condition text is stored Windows-1252, so a spec string that cannot be represented is
  // refused before the writer copies the plugin, which must keep a refused call byte-identical.
  entry.conditions.resize(spec.condition_count);
  for (std::size_t at = 0; at < spec.condition_count; ++at) {
    if (!to_ctda(spec.conditions[at], entry.conditions[at])) {
      return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                  "condition text is not representable in Windows-1252");
    }
  }
  return RADICLE_STATUS_OK;
}

radicle_status author(radicle_plugin* plugin, radicle_form_key scene, size_t action_index,
                      size_t index, const radicle_scene_start_entry_spec* spec, bool insert) {
  clear_error();
  const auto key = to_form_key(scene);
  if (plugin == nullptr || !key || spec == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "plugin, a valid scene key and a spec are required");
  }
  return guarded("unknown exception while writing a Start Scene entry", [&] {
    fo4::forms::SceneStartSceneEntry entry;
    if (const auto refused = entry_from_spec(*spec, entry); refused != RADICLE_STATUS_OK) {
      return refused;
    }
    try {
      const auto status = insert ? fo4::forms::insert_scene_start_scene(plugin->value, *key,
                                                                       action_index, index, entry)
                                 : fo4::forms::update_scene_start_scene(plugin->value, *key,
                                                                       action_index, index, entry);
      return status_result(status);
    } catch (const std::exception& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

}  // namespace

extern "C" {

radicle_status radicle_plugin_scene_start_scenes(const radicle_plugin* plugin,
                                                 radicle_form_key scene,
                                                 size_t action_index,
                                                 const radicle_scene_start_entries** out_entries) {
  clear_error();
  const auto key = to_form_key(scene);
  if (plugin == nullptr || !key || out_entries == nullptr) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT,
                "plugin, a valid scene key and out_entries are required");
  }
  *out_entries = nullptr;
  return guarded("unknown exception while decoding a Start Scene action", [&] {
    try {
      const auto entries =
          fo4::forms::read_scene_start_scenes(plugin->value, *key, action_index);
      if (!entries.has_value()) {
        return fail(RADICLE_STATUS_NOT_FOUND,
                    "scene has no action at that index or the action is not a type-4 Start Scene action");
      }
      auto holder = std::make_unique<StartSceneHolder>();
      holder->entries.reserve(entries->size());
      holder->conditions.resize(entries->size());
      for (std::size_t at = 0; at < entries->size(); ++at) {
        const auto& source = (*entries)[at];
        radicle_scene_start_entry row{};
        row.scene = owned_key(*holder, source.scene);
        row.phase_index = source.phase_index;
        if (source.phase_name) {
          row.has_phase_name = 1;
          row.phase_name = utf8_text(holder->text, source.phase_name);
        }
        for (const auto& condition : source.conditions) {
          holder->conditions[at].push_back(condition_row(holder->text, condition));
        }
        row.conditions = holder->conditions[at].data();
        row.condition_count = holder->conditions[at].size();
        holder->entries.push_back(row);
      }
      holder->view.entries = holder->entries.data();
      holder->view.count = holder->entries.size();
      holder->view.internal = holder.get();
      *out_entries = &holder.release()->view;
      return RADICLE_STATUS_OK;
    } catch (const std::exception& error) {
      return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
    }
  });
}

void radicle_scene_start_entries_free(const radicle_scene_start_entries* entries) {
  clear_error();
  if (entries != nullptr) delete static_cast<StartSceneHolder*>(entries->internal);
}

radicle_status radicle_plugin_insert_scene_start_scene(radicle_plugin* plugin,
                                                       radicle_form_key scene,
                                                       size_t action_index,
                                                       size_t index,
                                                       const radicle_scene_start_entry_spec* spec) {
  return author(plugin, scene, action_index, index, spec, true);
}

radicle_status radicle_plugin_update_scene_start_scene(radicle_plugin* plugin,
                                                       radicle_form_key scene,
                                                       size_t action_index,
                                                       size_t index,
                                                       const radicle_scene_start_entry_spec* spec) {
  return author(plugin, scene, action_index, index, spec, false);
}

radicle_status radicle_plugin_remove_scene_start_scene(radicle_plugin* plugin,
                                                       radicle_form_key scene,
                                                       size_t action_index,
                                                       size_t index) {
  clear_error();
  const auto key = to_form_key(scene);
  if (plugin == nullptr || !key) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "plugin and a valid scene key are required");
  }
  return guarded("unknown exception while removing a Start Scene entry", [&] {
    try {
      return status_result(
          fo4::forms::remove_scene_start_scene(plugin->value, *key, action_index, index));
    } catch (const std::exception& error) {
      return fail_exception(RADICLE_STATUS_INVALID_ARGUMENT, error);
    }
  });
}

}  // namespace
