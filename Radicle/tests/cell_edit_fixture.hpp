#pragma once

#include "fo4/environment/environment.hpp"
#include "fo4/environment/world.hpp"
#include "fo4/forms/cell.hpp"
#include "fo4/forms/cell_edit.hpp"
#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/signature.hpp"
#include "byte_helpers.hpp"
#include "plugin_bytes.hpp"
#include "temp_data_dir.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::test::cell_edit {

struct Sub {
  std::string signature;
  std::vector<std::byte> data;
};

struct Node {
  bool group{false};
  std::string signature;
  std::uint32_t form_id{0};
  std::uint32_t flags{0};
  std::vector<Sub> subrecords;
  std::uint32_t label{0};
  std::int32_t group_type{0};
  std::vector<Node> children;
};

inline Node rec(std::string signature, std::uint32_t form_id, std::vector<Sub> subrecords = {},
         std::uint32_t flags = 0) {
  Node node;
  node.signature = std::move(signature);
  node.form_id = form_id;
  node.flags = flags;
  node.subrecords = std::move(subrecords);
  return node;
}

inline Node grp(std::uint32_t label, std::int32_t type, std::vector<Node> children) {
  Node node;
  node.group = true;
  node.label = label;
  node.group_type = type;
  node.children = std::move(children);
  return node;
}

inline Node top(std::string_view signature, std::vector<Node> children) {
  std::uint32_t label = 0;
  std::memcpy(&label, signature.data(), 4);
  return grp(label, 0, std::move(children));
}

inline std::vector<std::byte> bytes_of(std::initializer_list<std::uint32_t> words) {
  fo4::test::ByteWriter out;
  for (const auto word : words) out.u32(word);
  return out.bytes;
}

inline std::vector<std::byte> floats(std::initializer_list<float> values) {
  fo4::test::ByteWriter out;
  for (const auto value : values) out.f32(value);
  return out.bytes;
}

using fo4::test::zstr;

inline std::vector<std::byte> xtel_bytes() {
  fo4::test::ByteWriter out;
  out.u32(0x820);
  for (int i = 0; i < 6; ++i) out.f32(0.0f);
  out.u32(0);
  return out.bytes;
}

inline std::vector<std::byte> xprm_bytes() {
  fo4::test::ByteWriter out;
  for (int i = 0; i < 3; ++i) out.f32(1.0f);
  for (int i = 0; i < 5; ++i) out.u32(0);
  return out.bytes;
}

inline std::vector<std::byte> lighting_bytes(std::size_t size) {
  std::vector<std::byte> bytes(size, std::byte{0});
  for (std::size_t i = 40; i < size; ++i) bytes[i] = std::byte{0xAB};
  return bytes;
}

inline void write_node(fo4::test::ByteWriter& out, const Node& node) {
  if (node.group) {
    std::string label(4, '\0');
    std::memcpy(label.data(), &node.label, 4);
    const auto header = fo4::test::begin_group(out, label, node.group_type);
    for (const auto& child : node.children) write_node(out, child);
    fo4::test::end_group(out, header);
    return;
  }
  const auto header = fo4::test::begin_record(out, node.signature, node.flags, node.form_id);
  for (const auto& sub : node.subrecords) fo4::test::write_subrecord(out, sub.signature, sub.data);
  fo4::test::end_record(out, header);
}

inline std::vector<std::byte> make_plugin(const std::vector<std::string>& masters,
                                   const std::vector<Node>& groups) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  for (const auto& master : masters) {
    fo4::test::write_subrecord(out, "MAST", zstr(master));
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
  }
  fo4::test::end_record(out, tes4);
  for (const auto& group : groups) write_node(out, group);
  return out.bytes;
}

inline std::vector<std::byte> make_base() {
  const auto cell_802 = rec("CELL", 0x802, {{"DATA", {std::byte{0x02}, std::byte{0}}}, {"XCLC", bytes_of({1, 2, 0})}});
  const auto cell_803 = rec("CELL", 0x803, {{"DATA", {std::byte{0x02}, std::byte{0}}}, {"XCLC", bytes_of({2, 2, 0})}});
  const auto refs = grp(0x802, 6,
                        {grp(0x802, 8,
                             {rec("REFR", 0x815,
                                  {{"NAME", bytes_of({0x820})},
                                   {"DATA", floats({4190.0f, 8290.0f, 0.0f, 0.0f, 0.0f, 0.0f})}},
                                  0x400)}),
                         grp(0x802, 9,
                             {rec("REFR", 0x814,
                                  {{"NAME", bytes_of({0x820})},
                                   {"DATA", floats({4196.0f, 8292.0f, 0.0f, 0.0f, 0.0f, 0.0f})}},
                                  0x400),
                              rec("REFR", 0x811,
                                  {{"EDID", zstr("Crate")},
                                   {"NAME", bytes_of({0x820})},
                                   {"DATA", floats({4196.0f, 8292.0f, 0.0f, 0.0f, 0.0f, 0.0f})},
                                   {"XTEL", xtel_bytes()},
                                   {"XPRM", xprm_bytes()}}),
                              rec("REFR", 0x812,
                                  {{"NAME", bytes_of({0x820})},
                                   {"DATA", floats({4200.0f, 8300.0f, 0.0f, 0.0f, 0.0f, 0.0f})}}),
                              rec("REFR", 0x813,
                                  {{"NAME", bytes_of({0x820})},
                                   {"DATA", floats({4300.0f, 8300.0f, 0.0f, 0.0f, 0.0f, 0.0f})}})})});
  const auto interior_804 = rec("CELL", 0x804, {{"DATA", {std::byte{0x01}, std::byte{0}}}, {"XCLL", lighting_bytes(72)}});
  const auto interior_805 = rec("CELL", 0x805, {{"DATA", {std::byte{0x01}, std::byte{0}}}, {"XCLL", lighting_bytes(40)}});
  return make_plugin(
      {},
      {top("WRLD",
           {rec("WRLD", 0x800, {{"EDID", zstr("TestWorld")}}),
            grp(0x800, 1, {rec("CELL", 0x801, {{"DATA", {std::byte{0x02}, std::byte{0}}}}), grp(0, 4, {grp(0, 5, {cell_802, refs, cell_803})})})}),
       top("CELL", {interior_804, interior_805})});
}

inline std::vector<std::byte> make_patch() {
  const auto cell_802 = rec("CELL", 0x802, {{"DATA", {std::byte{0x02}, std::byte{0}}}, {"XCLC", bytes_of({1, 2, 0})}});
  return make_plugin(
      {"Base.esm"},
      {top("WRLD",
           {rec("WRLD", 0x800, {{"EDID", zstr("TestWorld")}}),
            grp(0x800, 1,
                {grp(0, 4,
                     {grp(0, 5,
                          {cell_802,
                           grp(0x802, 6,
                               {grp(0x802, 9,
                                    {rec("REFR", 0x812,
                                         {{"NAME", bytes_of({0x820})},
                                          {"DATA", floats({99.0f, 8300.0f, 0.0f, 0.0f, 0.0f, 0.0f})}}),
                                     rec("REFR", 0x813, {}, 0x20)})})})})})})});
}

inline const fo4::Plugin& plugin_named(const fo4::env::Environment& env, std::string_view name) {
  return env.plugin(*env.find_plugin(name));
}

inline fo4::Plugin empty_edit() {
  const auto bytes = make_plugin({"Base.esm"}, {});
  return fo4::read_plugin(bytes, "Edit.esp");
}

inline bool has_subrecord(const fo4::Record& record, std::string_view signature) {
  const auto wanted = fo4::Signature::from_string(signature);
  return std::any_of(record.subrecords.begin(), record.subrecords.end(),
                     [&](const fo4::Subrecord& sub) { return sub.signature == wanted; });
}

inline const fo4::Subrecord* subrecord_of(const fo4::Record& record, std::string_view signature) {
  const auto wanted = fo4::Signature::from_string(signature);
  const auto found = std::find_if(record.subrecords.begin(), record.subrecords.end(),
                                  [&](const fo4::Subrecord& sub) { return sub.signature == wanted; });
  return found == record.subrecords.end() ? nullptr : &*found;
}

inline bool has_reference(const fo4::Plugin& plugin, const fo4::FormKey& cell, const fo4::FormKey& reference) {
  for (const auto& view : fo4::forms::placed_references_for_cell(plugin, cell)) {
    if (view.record->form_key == reference) return true;
  }
  return false;
}

inline std::optional<std::int32_t> bucket_type_of(const fo4::Group& group, const fo4::FormKey& key) {
  for (const auto& child : group.children) {
    if (const auto* record = std::get_if<fo4::Record>(&child); record != nullptr && record->form_key == key) {
      return group.header.group_type;
    }
    if (const auto* nested = std::get_if<fo4::Group>(&child)) {
      if (const auto type = bucket_type_of(*nested, key)) return type;
    }
  }
  return std::nullopt;
}

inline std::optional<std::int32_t> bucket_type_of(const fo4::Plugin& plugin, const fo4::FormKey& key) {
  for (const auto& group : plugin.groups) {
    if (const auto type = bucket_type_of(group, key)) return type;
  }
  return std::nullopt;
}

inline std::vector<std::byte> make_targets() {
  return make_plugin({}, {top("WATR", {rec("WATR", 0x900)}), top("IMGS", {rec("IMGS", 0x901)}),
                          top("ECZN", {rec("ECZN", 0x902)}), top("LCTN", {rec("LCTN", 0x903)}),
                          top("LGTM", {rec("LGTM", 0x904)}), top("REGN", {rec("REGN", 0x905), rec("REGN", 0x907)}),
                          top("FACT", {rec("FACT", 0x906)})});
}

inline fo4::Plugin props_patch() {
  return fo4::read_plugin(make_plugin({"Base.esm", "Targets.esm"}, {}), "Props.esp");
}

inline fo4::env::Environment load_plugins(fo4::test::TempDataDir& dir, const std::vector<std::string>& names) {
  std::vector<std::filesystem::path> paths;
  for (const auto& name : names) paths.push_back(dir.path() / name);
  return fo4::env::Environment::load(paths);
}

inline fo4::env::Environment base_and_targets(fo4::test::TempDataDir& dir) {
  dir.write("Targets.esm", make_targets(), 1);
  return load_plugins(dir, {"Base.esm", "Targets.esm"});
}

inline fo4::env::Environment reread_patched(fo4::test::TempDataDir& dir, const fo4::Plugin& patch) {
  dir.write("Readback.esp", fo4::write_plugin(patch), 2);
  return load_plugins(dir, {"Base.esm", "Targets.esm", "Readback.esp"});
}

inline std::vector<std::string> signatures_of(const fo4::Record& record) {
  std::vector<std::string> signatures;
  for (const auto& sub : record.subrecords) signatures.push_back(sub.signature.to_string());
  return signatures;
}

class CellEditTest : public ::testing::Test {
protected:
  void SetUp() override {
    dir_.write("Base.esm", make_base(), 0);
    dir_.write("Patch.esp", make_patch(), 1);
    paths_ = {dir_.path() / "Base.esm", dir_.path() / "Patch.esp"};
  }

  fo4::test::TempDataDir dir_;
  std::vector<std::filesystem::path> paths_;
};
}  // namespace fo4::test::cell_edit
