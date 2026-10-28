#include "write_model.hpp"

#include "fo4/binary/cursor.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::nif::write_detail {
namespace {

std::string read_short_string(binary::Cursor& cursor) {
  const auto text = cursor.read_bytes(cursor.read_u8());
  return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::string read_sized_string(binary::Cursor& cursor) {
  const auto text = cursor.read_bytes(cursor.read_u32());
  return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

}  // namespace

Model load_model(const NifFile& nif) {
  const std::span<const std::byte> all{nif.bytes()};
  const auto header_length = static_cast<std::size_t>(std::find(all.begin(), all.end(), std::byte{'\n'}) - all.begin());
  Model model;
  model.banner.assign(reinterpret_cast<const char*>(all.data()), header_length);
  binary::Cursor cursor(all.subspan(header_length + 1), header_length + 1);
  model.version = cursor.read_u32();
  cursor.read_u8();
  model.user_version = cursor.read_u32();
  const auto block_count = cursor.read_u32();
  model.bs_version = cursor.read_u32();
  for (std::size_t i = 0; i < 4; ++i) model.export_strings.push_back(read_short_string(cursor));
  const auto type_count = cursor.read_u16();
  for (std::size_t i = 0; i < type_count; ++i) model.block_types.push_back(read_sized_string(cursor));
  for (std::size_t i = 0; i < block_count; ++i) model.block_type_index.push_back(cursor.read_u16());
  std::vector<std::uint32_t> sizes;
  for (std::size_t i = 0; i < block_count; ++i) sizes.push_back(cursor.read_u32());
  const auto string_count = cursor.read_u32();
  model.max_string_length = cursor.read_u32();
  for (std::size_t i = 0; i < string_count; ++i) model.strings.push_back(read_sized_string(cursor));
  const auto group_count = cursor.read_u32();
  for (std::size_t i = 0; i < group_count; ++i) model.groups.push_back(cursor.read_u32());
  std::size_t offset = cursor.absolute_offset();
  for (const auto size : sizes) {
    const auto payload = all.subspan(offset, size);
    model.blocks.emplace_back(payload.begin(), payload.end());
    offset += size;
  }
  binary::Cursor footer(all.subspan(offset), offset);
  const auto root_count = footer.read_u32();
  for (std::size_t i = 0; i < root_count; ++i) model.roots.push_back(footer.read_i32());
  return model;
}

Bytes serialize(const Model& model) {
  Bytes out;
  append_text(out, model.banner);
  out.push_back(std::byte{'\n'});
  append_int(out, model.version);
  append_int(out, std::uint8_t{1});
  append_int(out, model.user_version);
  append_int(out, static_cast<std::uint32_t>(model.blocks.size()));
  append_int(out, model.bs_version);
  for (const auto& text : model.export_strings) {
    append_int(out, static_cast<std::uint8_t>(text.size()));
    append_text(out, text);
  }
  append_int(out, static_cast<std::uint16_t>(model.block_types.size()));
  for (const auto& type : model.block_types) append_sized(out, type);
  for (const auto index : model.block_type_index) append_int(out, index);
  for (const auto& block : model.blocks) append_int(out, static_cast<std::uint32_t>(block.size()));
  append_int(out, static_cast<std::uint32_t>(model.strings.size()));
  append_int(out, model.max_string_length);
  for (const auto& text : model.strings) append_sized(out, text);
  append_int(out, static_cast<std::uint32_t>(model.groups.size()));
  for (const auto group : model.groups) append_int(out, group);
  for (const auto& block : model.blocks) out.insert(out.end(), block.begin(), block.end());
  append_int(out, static_cast<std::uint32_t>(model.roots.size()));
  for (const auto root : model.roots) append_int(out, root);
  return out;
}

std::string_view block_type(const Model& model, std::uint32_t block) {
  return model.block_types[model.block_type_index[block]];
}

std::int32_t intern_string(Model& model, const std::string& text) {
  if (text.empty()) return -1;
  const auto found = std::find(model.strings.begin(), model.strings.end(), text);
  if (found != model.strings.end()) return static_cast<std::int32_t>(found - model.strings.begin());
  if (model.max_string_length != 0 && text.size() > model.max_string_length) {
    model.max_string_length = static_cast<std::uint32_t>(text.size());
  }
  model.strings.push_back(text);
  return static_cast<std::int32_t>(model.strings.size() - 1);
}

}  // namespace fo4::nif::write_detail
