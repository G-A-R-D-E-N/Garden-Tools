#include "fo4/forms/cell.hpp"

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
#include <vector>

namespace fo4::forms {
namespace {

constexpr auto k_cell = Signature::from_string("CELL");
constexpr std::uint32_t k_object_id_mask = 0x00FFFFFFu;

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
  return std::equal(left.begin(), left.end(), right.begin(), right.end(),
                    [](char a, char b) {
                      return std::tolower(static_cast<unsigned char>(a)) ==
                             std::tolower(static_cast<unsigned char>(b));
                    });
}

bool editor_id_in_use(const std::vector<GroupChild>& children, std::string_view editor_id) {
  for (const auto& child : children) {
    if (const auto* group = std::get_if<Group>(&child)) {
      if (editor_id_in_use(group->children, editor_id)) return true;
      continue;
    }
    const auto& record = std::get<Record>(child);
    if (record.header.signature != k_cell) continue;
    for (const auto& subrecord : record.subrecords) {
      if (subrecord.signature == sig_edid &&
          equal_ignoring_ascii_case(text_without_terminator(subrecord), editor_id)) {
        return true;
      }
    }
  }
  return false;
}

Group* ensure_sorted_group(std::vector<GroupChild>& children, GroupType type, std::uint32_t label) {
  const auto type_value = static_cast<std::int32_t>(type);
  auto position = children.begin();
  for (; position != children.end(); ++position) {
    const auto* group = std::get_if<Group>(&*position);
    if (group == nullptr || group->header.group_type != type_value) continue;
    if (group->header.label_as_u32() == label) return &std::get<Group>(*position);
    if (group->header.label_as_u32() > label) break;
  }

  Group created;
  created.header.group_type = type_value;
  created.header.label = detail::u32_label(label);
  return &std::get<Group>(*children.insert(position, std::move(created)));
}

void insert_cell(Group& subblock, Record record) {
  const auto form_id = record.header.form_id.value();
  auto position = subblock.children.begin();
  for (; position != subblock.children.end(); ++position) {
    const auto* existing = std::get_if<Record>(&*position);
    if (existing != nullptr && existing->header.signature == k_cell &&
        existing->header.form_id.value() > form_id) {
      break;
    }
  }
  subblock.children.emplace(position, std::move(record));
}

}  // namespace

FormKey create_interior_cell(Plugin& plugin, std::string_view editor_id) {
  if (editor_id.empty()) {
    throw CellError("interior CELL requires a non-empty editor ID");
  }
  if (editor_id.find('\0') != std::string_view::npos) {
    throw CellError("CELL editor ID cannot contain an embedded NUL");
  }

  auto next = plugin;
  assign_record_form_keys(next);
  const auto stored = stored_editor_id(editor_id);
  for (const auto& group : next.groups) {
    if (editor_id_in_use(group.children, stored)) {
      throw CellError("CELL editor ID is already used in this plugin");
    }
  }

  const auto key = reserve_local_form_key(next);
  const auto form_id = encode_plugin_form_id(key, plugin_form_id_context(next));
  const auto object_id = form_id.value() & k_object_id_mask;

  Record record;
  record.header.signature = k_cell;
  record.header.form_id = form_id;
  record.header.form_version = next.header.form_version;
  record.form_key = key;
  record.subrecords.push_back(Subrecord{sig_edid, zstring(stored), false});
  record.subrecords.push_back(Subrecord{sig_data, {std::byte{1}, std::byte{0}}, false});

  auto* top = find_or_create_top_group(next, k_cell);
  auto* block = ensure_sorted_group(top->children, GroupType::interior_cell_block, object_id % 10u);
  auto* subblock = ensure_sorted_group(
      block->children, GroupType::interior_cell_subblock, (object_id / 10u) % 10u);
  insert_cell(*subblock, std::move(record));

  next.original_range.clear();
  plugin = std::move(next);
  return key;
}

}  // namespace fo4::forms
