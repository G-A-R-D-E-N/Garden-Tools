#include "fo4/plugin/scene_writer.hpp"

#include "fo4/plugin/record_edit.hpp"
#include "scene_writer_internal.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace fo4 {
namespace {

Signature tag(const char* value) { return Signature::from_string(value); }

enum class Edit { insert, set, remove };

void validate_phase_name(const Record& record, std::pair<std::size_t, std::size_t> entry) {
  for (auto i = entry.first + 1; i < entry.second; ++i) {
    const auto& sub = record.subrecords[i];
    if (sub.signature != tag("SSPN")) continue;
    if (std::find(sub.data.begin(), sub.data.end() - 1, std::byte{0}) != sub.data.end() - 1)
      throw schema::FormLinkFieldError("Start Scene phase name contains an embedded NUL");
  }
}

FormLinkFieldMutationStatus edit_phase(Plugin& plugin, const FormKey& scene, std::size_t action, std::size_t index,
                                       Signature field, const std::optional<std::vector<std::byte>>& value) {
  auto* record = detail::locate_mutable_record(plugin, scene);
  if (!record) return FormLinkFieldMutationStatus::record_missing;
  if (record->header.signature != tag("SCEN")) return FormLinkFieldMutationStatus::record_mismatch;
  const auto parsed = detail::start_scene_layout(*record, action);
  if (!parsed || index >= parsed->entries.size()) return FormLinkFieldMutationStatus::subrecord_missing;
  const auto entry = parsed->entries[index];
  validate_phase_name(*record, entry);
  auto found = entry.second, anchor = entry.second;
  for (auto i = entry.first + 1; i < entry.second; ++i) {
    const auto current = record->subrecords[i].signature;
    if (current == field) { found = i; break; }
    if (field == tag("INTT") || current != tag("INTT")) { anchor = i; break; }
  }
  if ((!value && found == entry.second) || (value && found != entry.second && record->subrecords[found].data == *value))
    return FormLinkFieldMutationStatus::unchanged;
  auto updated = record->subrecords;
  if (!value) updated.erase(updated.begin() + static_cast<std::ptrdiff_t>(found));
  else if (found != entry.second) updated[found].data = *value;
  else updated.insert(updated.begin() + static_cast<std::ptrdiff_t>(anchor), Subrecord{field, *value, false});
  record->subrecords = std::move(updated);
  detail::mark_record_modified(plugin, *record);
  return FormLinkFieldMutationStatus::updated;
}

FormLinkFieldMutationStatus edit_scene_start(Plugin& plugin, const FormKey& scene, std::size_t action,
                                              std::size_t index, const FormKey* target, Edit edit) {
  auto* record = detail::locate_mutable_record(plugin, scene);
  if (!record) return FormLinkFieldMutationStatus::record_missing;
  if (record->header.signature != tag("SCEN")) return FormLinkFieldMutationStatus::record_mismatch;
  const auto parsed = detail::start_scene_layout(*record, action);
  if (!parsed) return FormLinkFieldMutationStatus::subrecord_missing;
  const auto& entries = parsed->entries;
  const auto marker = parsed->marker;
  if ((edit == Edit::insert && index > entries.size()) || (edit != Edit::insert && index >= entries.size())) {
    return FormLinkFieldMutationStatus::subrecord_missing;
  }
  std::vector<std::byte> encoded;
  if (target) {
    if (target->owner().empty()) throw schema::FormLinkFieldError("Start Scene target requires an owner");
    if (target->owner() == plugin.key) {
      const auto* found = find_record(plugin, *target);
      if (!found || found->header.signature != tag("SCEN")) throw schema::FormLinkFieldError("Start Scene target must be SCEN");
    }
    const auto raw = encode_plugin_form_id(*target, plugin_form_id_context(plugin)).value();
    if (raw == 0) throw schema::FormLinkFieldError("Start Scene target cannot encode as null");
    for (unsigned shift = 0; shift != 32; shift += 8) encoded.push_back(std::byte{static_cast<std::uint8_t>(raw >> shift)});
  }
  if (edit == Edit::set && record->subrecords[entries[index].first].data == encoded) return FormLinkFieldMutationStatus::unchanged;
  auto updated = record->subrecords;
  if (edit == Edit::insert) {
    const auto at = index == entries.size() ? marker : entries[index].first;
    updated.insert(updated.begin() + static_cast<std::ptrdiff_t>(at), Subrecord{tag("LCEP"), std::move(encoded), false});
  } else if (edit == Edit::set) {
    updated[entries[index].first].data = std::move(encoded);
  } else {
    updated.erase(updated.begin() + static_cast<std::ptrdiff_t>(entries[index].first),
                  updated.begin() + static_cast<std::ptrdiff_t>(entries[index].second));
  }
  record->subrecords = std::move(updated);
  detail::mark_record_modified(plugin, *record);
  return FormLinkFieldMutationStatus::updated;
}

}

std::optional<SceneStartPhase> read_scene_action_start_phase(const Plugin& plugin, const FormKey& scene,
                                                            std::size_t action, std::size_t index) {
  const auto* record = find_record(plugin, scene);
  if (!record || record->header.signature != tag("SCEN")) return std::nullopt;
  require_decodable(*record);
  const auto parsed = detail::start_scene_layout(*record, action);
  if (!parsed || index >= parsed->entries.size()) return std::nullopt;
  const auto entry = parsed->entries[index];
  validate_phase_name(*record, entry);
  SceneStartPhase result;
  for (auto i = entry.first + 1; i < entry.second; ++i) {
    const auto& sub = record->subrecords[i];
    if (sub.signature == tag("INTT")) {
      result.index = static_cast<std::uint16_t>(std::to_integer<std::uint16_t>(sub.data[0]) |
                                               (std::to_integer<std::uint16_t>(sub.data[1]) << 8));
    } else if (sub.signature == tag("SSPN")) {
      result.name = std::string(reinterpret_cast<const char*>(sub.data.data()), sub.data.size() - 1);
    }
  }
  return result;
}

FormLinkFieldMutationStatus set_scene_action_start_phase_index(Plugin& plugin, const FormKey& scene,
    std::size_t action, std::size_t index, std::optional<std::uint16_t> value) {
  std::optional<std::vector<std::byte>> data;
  if (value) data = std::vector<std::byte>{std::byte{static_cast<std::uint8_t>(*value)},
                                            std::byte{static_cast<std::uint8_t>(*value >> 8)}};
  return edit_phase(plugin, scene, action, index, tag("INTT"), data);
}

FormLinkFieldMutationStatus set_scene_action_start_phase_name(Plugin& plugin, const FormKey& scene,
    std::size_t action, std::size_t index, std::optional<std::string_view> value) {
  std::optional<std::vector<std::byte>> data;
  if (value) {
    if (value->size() >= UINT32_MAX) throw schema::FormLinkFieldError("Start Scene phase name is too large");
    if (value->find('\0') != std::string_view::npos) throw schema::FormLinkFieldError("Start Scene phase name contains NUL");
    data.emplace();
    for (const auto c : *value) data->push_back(std::byte{static_cast<std::uint8_t>(c)});
    data->push_back(std::byte{0});
  }
  return edit_phase(plugin, scene, action, index, tag("SSPN"), data);
}

FormLinkFieldMutationStatus insert_scene_action_start_scene(Plugin& plugin, const FormKey& scene, std::size_t action,
                                                             std::size_t index, const FormKey& target) {
  return edit_scene_start(plugin, scene, action, index, &target, Edit::insert);
}
FormLinkFieldMutationStatus set_scene_action_start_scene(Plugin& plugin, const FormKey& scene, std::size_t action,
                                                          std::size_t index, const FormKey& target) {
  return edit_scene_start(plugin, scene, action, index, &target, Edit::set);
}
FormLinkFieldMutationStatus remove_scene_action_start_scene(Plugin& plugin, const FormKey& scene, std::size_t action,
                                                             std::size_t index) {
  return edit_scene_start(plugin, scene, action, index, nullptr, Edit::remove);
}

}
