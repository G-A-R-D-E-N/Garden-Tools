#include "fo4/forms/scene.hpp"

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/signature.hpp"
#include "placed_reference_copy_internal.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::forms {
namespace {

constexpr auto k_scen = Signature::from_string("SCEN");
constexpr auto k_qust = Signature::from_string("QUST");
constexpr auto k_pnam = Signature::from_string("PNAM");

std::vector<std::byte> zstring(std::string_view value) {
  std::vector<std::byte> bytes;
  bytes.reserve(value.size() + 1);
  for (const char ch : value) {
    bytes.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  }
  bytes.push_back(std::byte{0});
  return bytes;
}

std::string_view text_without_terminator(const Subrecord& subrecord) {
  const auto* begin = reinterpret_cast<const char*>(subrecord.data.data());
  auto size = subrecord.data.size();
  if (size != 0 && subrecord.data.back() == std::byte{0}) --size;
  return {begin, size};
}

bool equal_ignoring_ascii_case(std::string_view left, std::string_view right) {
  return std::equal(left.begin(), left.end(), right.begin(), right.end(), [](char a, char b) {
    return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
  });
}

bool editor_id_in_use(const std::vector<GroupChild>& children, std::string_view editor_id) {
  for (const auto& child : children) {
    if (const auto* group = std::get_if<Group>(&child)) {
      if (editor_id_in_use(group->children, editor_id)) return true;
      continue;
    }
    const auto& record = std::get<Record>(child);
    if (record.header.signature != k_scen) continue;
    for (const auto& subrecord : record.subrecords) {
      if (subrecord.signature == sig_edid &&
          equal_ignoring_ascii_case(text_without_terminator(subrecord), editor_id)) {
        return true;
      }
    }
  }
  return false;
}

Group* find_quest_children(Group& top, std::uint32_t label) {
  for (auto& child : top.children) {
    auto* group = std::get_if<Group>(&child);
    if (group == nullptr) continue;
    if (group->header.group_type != static_cast<std::int32_t>(GroupType::quest_children)) continue;
    if (group->header.label_as_u32() != label) continue;
    return group;
  }
  return nullptr;
}

}  // namespace

FormKey create_scene(Plugin& plugin, const FormKey& quest, std::string_view editor_id) {
  if (editor_id.empty()) {
    throw SceneError("SCEN requires a non-empty editor ID");
  }
  if (editor_id.find('\0') != std::string_view::npos) {
    throw SceneError("SCEN editor ID cannot contain an embedded NUL");
  }

  auto next = plugin;
  assign_record_form_keys(next);
  if (quest.owner() != next.key) {
    throw SceneError("SCEN parent quest must be owned by this plugin");
  }
  const auto* quest_record = find_record(next, quest);
  if (quest_record == nullptr || quest_record->header.signature != k_qust) {
    throw SceneError("SCEN parent is not a QUST record in this plugin");
  }
  const auto stored = stored_editor_id(editor_id);
  for (const auto& group : next.groups) {
    if (editor_id_in_use(group.children, stored)) {
      throw SceneError("SCEN editor ID is already used in this plugin");
    }
  }

  const auto key = reserve_local_form_key(next);
  const auto encoded_quest = encode_plugin_form_id(quest, plugin_form_id_context(next));

  Record record;
  record.header.signature = k_scen;
  record.header.form_id = encode_plugin_form_id(key, plugin_form_id_context(next));
  record.header.form_version = next.header.form_version;
  record.form_key = key;
  record.subrecords.push_back(Subrecord{sig_edid, zstring(stored), false});
  const auto quest_id = encoded_quest.value();
  record.subrecords.push_back(Subrecord{k_pnam,
                                        {std::byte{static_cast<std::uint8_t>(quest_id)},
                                         std::byte{static_cast<std::uint8_t>(quest_id >> 8)},
                                         std::byte{static_cast<std::uint8_t>(quest_id >> 16)},
                                         std::byte{static_cast<std::uint8_t>(quest_id >> 24)}},
                                        false});

  auto* top = find_or_create_top_group(next, k_qust);
  const auto label = encoded_quest.value();
  auto* children = find_quest_children(*top, label);
  if (children == nullptr) {
    Group created;
    created.header = detail::group_header(GroupType::quest_children, label);
    // Constraint: the child group directly follows its quest, matching the topic-children rule.
    auto position = top->children.end();
    for (auto it = top->children.begin(); it != top->children.end(); ++it) {
      const auto* existing = std::get_if<Record>(&*it);
      if (existing != nullptr && existing->form_key == quest) {
        position = it + 1;
        break;
      }
    }
    children = &std::get<Group>(*top->children.insert(position, std::move(created)));
  }
  children->children.emplace_back(std::move(record));

  next.original_range.clear();
  plugin = std::move(next);
  return key;
}

}  // namespace fo4::forms
