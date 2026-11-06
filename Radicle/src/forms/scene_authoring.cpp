#include "fo4/forms/scene.hpp"

#include "../plugin/record_edit.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "placed_reference_copy_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::forms {

namespace {

constexpr auto k_qust = Signature::from_string("QUST");
constexpr auto k_scen = Signature::from_string("SCEN");
constexpr auto k_edid = Signature::from_string("EDID");
constexpr auto k_fnam = Signature::from_string("FNAM");
constexpr auto k_pnam = Signature::from_string("PNAM");
constexpr auto k_inam = Signature::from_string("INAM");
constexpr auto k_vnam = Signature::from_string("VNAM");
constexpr auto k_xnam = Signature::from_string("XNAM");

using Located = std::optional<fo4::detail::MutableLocation>;

template <typename Value>
void append_scalar(std::vector<std::byte>& out, Value value) {
  const auto at = out.size();
  out.resize(at + sizeof(Value));
  std::memcpy(out.data() + at, &value, sizeof(Value));
}

template <typename... Values>
std::vector<std::byte> packed(Values... values) {
  std::vector<std::byte> out;
  ((append_scalar(out, values)), ...);
  return out;
}

std::vector<std::byte> zstring_bytes(std::string_view text) {
  std::vector<std::byte> out;
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  out.push_back(std::byte{0});
  return out;
}

bool contains_nul(std::string_view text) {
  return text.find('\0') != std::string_view::npos;
}

std::uint32_t raw_form_id(const FormKey& key, const Plugin& plugin) {
  return encode_plugin_form_id(key, plugin_form_id_context(plugin)).value();
}

Record make_record(const Plugin& plugin,
                   const FormKey& key,
                   Signature signature,
                   std::vector<Subrecord> subrecords) {
  Record record;
  record.header.signature = signature;
  record.header.form_id = encode_plugin_form_id(key, plugin_form_id_context(plugin));
  record.header.form_version = plugin.header.form_version;
  record.form_key = key;
  record.subrecords = std::move(subrecords);
  return record;
}

Group& ensure_adjacent_group(Group& parent,
                             std::size_t record_index,
                             const GroupHeader& wanted) {
  std::optional<std::size_t> found;
  for (std::size_t index = 0; index < parent.children.size(); ++index) {
    const auto* group = std::get_if<Group>(&parent.children[index]);
    if (group == nullptr || !detail::same_group_identity(group->header, wanted)) continue;
    if (found.has_value()) {
      throw std::runtime_error("duplicate quest child group");
    }
    found = index;
  }
  if (found.has_value()) {
    if (*found != record_index + 1) {
      throw std::runtime_error("quest child group is not adjacent to its record");
    }
    return std::get<Group>(parent.children[*found]);
  }
  Group created;
  created.header = wanted;
  const auto it = parent.children.insert(
      parent.children.begin() + static_cast<std::ptrdiff_t>(record_index + 1),
      GroupChild{std::move(created)});
  return std::get<Group>(*it);
}

std::optional<SceneAuthorStatus> refusal(const Located& location,
                                         Signature signature,
                                         SceneAuthorStatus missing) {
  if (!location.has_value() || location->record().header.signature != signature) {
    return missing;
  }
  if (location->record().undecodable) return SceneAuthorStatus::undecodable;
  return std::nullopt;
}

void commit(Plugin& plugin, Plugin& next) {
  next.original_range.clear();
  next.original_structure_fingerprint = 0;
  plugin = std::move(next);
}

SceneCreateResult scene_impl(Plugin& plugin, const FormKey& quest, const SceneSpec& spec) {
  if (quest.owner() != plugin.key) return {SceneAuthorStatus::not_local, {}};
  if (contains_nul(spec.editor_id)) return {SceneAuthorStatus::invalid_argument, {}};
  auto next = plugin;
  assign_record_form_keys(next);
  auto quest_location = fo4::detail::find_mutable_location(next, quest);
  if (auto refused = refusal(quest_location, k_qust, SceneAuthorStatus::quest_missing)) {
    return {*refused, {}};
  }
  if (quest_location->path.size() != 1) {
    throw std::runtime_error("QUST is not a top-level group member");
  }

  const auto key = reserve_local_form_key(next);
  const auto raw_quest = raw_form_id(quest, next);
  auto& quest_children = ensure_adjacent_group(
      *quest_location->path.front(), quest_location->index,
      detail::group_header(GroupType::quest_children, raw_quest));

  std::vector<Subrecord> subrecords;
  if (!spec.editor_id.empty()) {
    subrecords.push_back(Subrecord{k_edid, zstring_bytes(stored_editor_id(spec.editor_id)), false});
  }
  subrecords.push_back(Subrecord{k_fnam, packed(spec.flags), false});
  subrecords.push_back(Subrecord{k_pnam, packed(raw_quest), false});
  subrecords.push_back(Subrecord{k_inam, packed(std::uint32_t{0}), false});
  subrecords.push_back(Subrecord{k_vnam, std::vector<std::byte>(16), false});
  subrecords.push_back(Subrecord{k_xnam, packed(spec.index), false});

  quest_children.children.emplace_back(make_record(next, key, k_scen, std::move(subrecords)));
  commit(plugin, next);
  return {SceneAuthorStatus::ok, key};
}

}  // namespace

SceneCreateResult create_scene(Plugin& plugin, const FormKey& quest, const SceneSpec& spec) {
  try {
    return scene_impl(plugin, quest, spec);
  } catch (...) {
    return {SceneAuthorStatus::structure_invalid, {}};
  }
}

}  // namespace fo4::forms
