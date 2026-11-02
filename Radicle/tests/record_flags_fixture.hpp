#pragma once

#include "fo4/environment/environment.hpp"
#include "fo4/plugin/container.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/signature.hpp"
#include "plugin_bytes.hpp"
#include "temp_data_dir.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::test {

using Bytes = std::vector<std::byte>;
using Subrecords = std::vector<std::pair<std::string_view, std::string>>;

struct CellPlugin {
  std::string_view name{"Cells.esp"};
  std::vector<std::string_view> masters{};
  std::uint32_t cell_form{0x900};
  std::uint32_t cell_flags{0};
  std::uint8_t cell_data{1};
  fo4::GroupType bucket{fo4::GroupType::cell_temporary_children};
  std::string_view ref_signature{"REFR"};
  std::uint32_t ref_flags{0};
  std::uint32_t ref_form{0x800};
  Subrecords ref_subs{
      {"NAME", std::string{'\x01', '\x08', '\0', '\0'}},
      {"DATA", std::string(24, '\0')},
  };
};

inline std::string label_of(std::uint32_t value) {
  std::string out;
  for (std::size_t shift = 0; shift < 32; shift += 8) {
    out.push_back(static_cast<char>((value >> shift) & 0xFFu));
  }
  return out;
}

inline void write_header(fo4::test::ByteWriter& out, const std::vector<std::string_view>& masters) {
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  for (const auto master : masters) {
    std::string name{master};
    name.push_back('\0');
    fo4::test::write_subrecord(out, "MAST", name);
    fo4::test::ByteWriter size;
    size.u32(0);
    size.u32(0);
    fo4::test::write_subrecord(out, "DATA", size.bytes);
  }
  fo4::test::end_record(out, tes4);
}

inline void put_record(fo4::test::ByteWriter& out,
                       std::string_view signature,
                       std::uint32_t flags,
                       std::uint32_t form_id,
                       const Subrecords& subrecords) {
  fo4::test::ByteWriter payload;
  for (const auto& [name, data] : subrecords) {
    fo4::test::write_subrecord(payload, name, data);
  }
  const auto record = fo4::test::begin_record(out, signature, flags, form_id);
  if ((flags & fo4::RecordFlags::compressed) != 0) {
    out.u32(static_cast<std::uint32_t>(payload.size()));
    out.raw(fo4::test::zlib_compress(payload.bytes));
  } else {
    out.raw(payload.bytes);
  }
  fo4::test::end_record(out, record);
}

inline Bytes build_plugin(const CellPlugin& layout) {
  fo4::test::ByteWriter out;
  write_header(out, layout.masters);
  const auto top = fo4::test::begin_group(out, "CELL", 0);
  const auto block = fo4::test::begin_group(out, label_of(3), 2);
  const auto sub = fo4::test::begin_group(out, label_of(5), 3);
  put_record(out, "CELL", layout.cell_flags, layout.cell_form,
             {{"DATA", std::string{static_cast<char>(layout.cell_data), '\0'}}});
  const auto children = fo4::test::begin_group(out, label_of(layout.cell_form), 6);
  const auto bucket_group = fo4::test::begin_group(
      out, label_of(layout.cell_form), static_cast<std::int32_t>(layout.bucket));
  put_record(out, layout.ref_signature, layout.ref_flags, layout.ref_form, layout.ref_subs);
  fo4::test::end_group(out, bucket_group);
  fo4::test::end_group(out, children);
  fo4::test::end_group(out, sub);
  fo4::test::end_group(out, block);
  fo4::test::end_group(out, top);
  return out.bytes;
}

inline fo4::Plugin load(const CellPlugin& layout) {
  return fo4::read_plugin(build_plugin(layout), layout.name);
}

inline fo4::env::Environment load_order(const CellPlugin& master, const CellPlugin& patch) {
  fo4::test::TempDataDir dir;
  dir.write(std::string{master.name}, build_plugin(master), 1);
  dir.write(std::string{patch.name}, build_plugin(patch), 2);
  const std::vector<std::filesystem::path> paths{dir.path() / std::string{master.name},
                                                 dir.path() / std::string{patch.name}};
  return fo4::env::Environment::load(paths);
}

inline fo4::env::Environment load_order(std::initializer_list<CellPlugin> plugins) {
  fo4::test::TempDataDir dir;
  std::vector<std::filesystem::path> paths;
  int rank = 0;
  for (const auto& plugin : plugins) {
    dir.write(std::string{plugin.name}, build_plugin(plugin), ++rank);
    paths.push_back(dir.path() / std::string{plugin.name});
  }
  return fo4::env::Environment::load(paths);
}

inline CellPlugin two_level_master_plugin() {
  return CellPlugin{
      .name = "Machine.esm",
      .masters = {"Fallout4.esm"},
      .cell_form = 0x01000900,
      .ref_form = 0x01000800,
      .ref_subs = {{"NAME", std::string{'\x01', '\x08', '\0', '\x01'}},
                   {"DATA", std::string(24, '\0')}},
  };
}

inline CellPlugin two_level_patch_plugin() {
  return CellPlugin{
      .name = "Patch.esp",
      .masters = {"Fallout4.esm", "Coast.esm", "Machine.esm"},
      .cell_form = 0x03000900,
      .ref_flags = fo4::RecordFlags::deleted,
      .ref_form = 0x02000800,
      .ref_subs = {{"NAME", std::string{'\x01', '\x08', '\0', '\0'}}},
  };
}

inline fo4::env::Environment two_level_load_order() {
  return load_order({CellPlugin{.name = "Fallout4.esm"}, CellPlugin{.name = "Coast.esm"},
                     two_level_master_plugin(), two_level_patch_plugin()});
}

inline const fo4::Group* find_group(const fo4::Group& group, fo4::GroupType type, std::uint32_t label) {
  if (group.header.group_type == static_cast<std::int32_t>(type) &&
      group.header.label_as_u32() == label) {
    return &group;
  }
  for (const auto& child : group.children) {
    const auto* nested = std::get_if<fo4::Group>(&child);
    if (nested == nullptr) continue;
    if (const auto* found = find_group(*nested, type, label)) return found;
  }
  return nullptr;
}

inline const fo4::Group* find_group(const fo4::Plugin& plugin, fo4::GroupType type, std::uint32_t label) {
  for (const auto& group : plugin.groups) {
    if (const auto* found = find_group(group, type, label)) return found;
  }
  return nullptr;
}

inline const fo4::Group* parent_group(const fo4::Group& group, const fo4::FormKey& key) {
  for (const auto& child : group.children) {
    if (const auto* record = std::get_if<fo4::Record>(&child)) {
      if (record->form_key == key) return &group;
    } else if (const auto* found = parent_group(std::get<fo4::Group>(child), key)) {
      return found;
    }
  }
  return nullptr;
}

inline const fo4::Group* parent_group(const fo4::Plugin& plugin, const fo4::FormKey& key) {
  for (const auto& group : plugin.groups) {
    if (const auto* found = parent_group(group, key)) return found;
  }
  return nullptr;
}

inline std::string position_data(float x, float y, float z) {
  fo4::test::ByteWriter out;
  out.f32(x);
  out.f32(y);
  out.f32(z);
  out.f32(0.0f);
  out.f32(0.0f);
  out.f32(0.0f);
  return std::string(reinterpret_cast<const char*>(out.bytes.data()), out.bytes.size());
}

inline std::string as_text(const Bytes& bytes) {
  return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

inline const fo4::Subrecord* subrecord_named(const fo4::Record& record, std::string_view name) {
  const auto signature = fo4::Signature::from_string(name);
  for (const auto& sub : record.subrecords) {
    if (sub.signature == signature) return &sub;
  }
  return nullptr;
}

inline fo4::RawFormId form_id_of(const fo4::Subrecord& sub) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4 && i < sub.data.size(); ++i) {
    value |= std::to_integer<std::uint32_t>(sub.data[i]) << (8 * i);
  }
  return fo4::RawFormId{value};
}

inline fo4::FormKey resolved_link(const fo4::Plugin& plugin, const fo4::Subrecord& sub) {
  return fo4::resolve_plugin_form_id(form_id_of(sub), fo4::plugin_form_id_context(plugin));
}

inline fo4::env::Environment load_order_with_undecodable_reference(const CellPlugin& master,
                                                                   const CellPlugin& patch) {
  fo4::test::ByteWriter out;
  write_header(out, {});
  const auto group = fo4::test::begin_group(out, "REFR", 0);
  const auto record = fo4::test::begin_record(
      out, master.ref_signature, master.ref_flags | fo4::RecordFlags::compressed, master.ref_form);
  out.u32(32);
  out.raw(std::string_view{"not-zlib-data-at-all"});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  fo4::test::TempDataDir dir;
  dir.write(std::string{master.name}, out.bytes, 1);
  dir.write(std::string{patch.name}, build_plugin(patch), 2);
  const std::vector<std::filesystem::path> paths{dir.path() / std::string{master.name},
                                                 dir.path() / std::string{patch.name}};
  return fo4::env::Environment::load(paths);
}

}  // namespace fo4::test
