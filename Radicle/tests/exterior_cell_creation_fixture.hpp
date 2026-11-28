#pragma once

#include "fo4/environment/environment.hpp"
#include "fo4/forms/cell.hpp"
#include "fo4/forms/cell_relocate_doors.hpp"
#include "fo4/plugin/edit_history.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/writer.hpp"
#include "env_node_fixture.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace fo4::test::exterior_cell {

inline std::vector<std::byte> plugin_with_worldspace() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto wrld_group = fo4::test::begin_group(out, "WRLD", 0);
  const auto wrld = fo4::test::begin_record(out, "WRLD", 0, 0x00000800, 131);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"TestWorld\0", 10});
  fo4::test::end_record(out, wrld);
  fo4::test::end_group(out, wrld_group);
  return out.bytes;
}

inline const fo4::Group* top_wrld(const fo4::Plugin& plugin) {
  for (const auto& group : plugin.groups) {
    if (group.header.group_type == static_cast<std::int32_t>(fo4::GroupType::top) &&
        group.header.top_label() == fo4::Signature::from_string("WRLD")) {
      return &group;
    }
  }
  return nullptr;
}

inline const fo4::Group* child_group(const std::vector<fo4::GroupChild>& children,
                                     fo4::GroupType type,
                                     std::uint32_t label) {
  const fo4::Group* found = nullptr;
  for (const auto& child : children) {
    const auto* group = std::get_if<fo4::Group>(&child);
    if (group == nullptr ||
        group->header.group_type != static_cast<std::int32_t>(type) ||
        group->header.label_as_u32() != label) {
      continue;
    }
    EXPECT_EQ(found, nullptr);
    found = group;
  }
  return found;
}

inline std::size_t count_groups(const fo4::Group& parent, fo4::GroupType type) {
  std::size_t count = 0;
  for (const auto& child : parent.children) {
    const auto* group = std::get_if<fo4::Group>(&child);
    if (group != nullptr &&
        group->header.group_type == static_cast<std::int32_t>(type)) {
      ++count;
    }
  }
  return count;
}

inline std::int32_t read_i32(const std::vector<std::byte>& data, std::size_t offset) {
  std::int32_t value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

inline const fo4::Subrecord* unique_subrecord(const fo4::Record& record,
                                              std::string_view signature) {
  const auto sig = fo4::Signature::from_string(signature);
  const fo4::Subrecord* found = nullptr;
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature != sig) continue;
    EXPECT_EQ(found, nullptr);
    found = &subrecord;
  }
  return found;
}

inline fo4::forms::ExteriorCellCreation cell_creation(const fo4::FormKey& worldspace,
                                                      std::int32_t x,
                                                      std::int32_t y) {
  return {
      .worldspace = worldspace,
      .x = x,
      .y = y,
      .editor_id = std::nullopt,
  };
}

using fo4::test::envnode::bytes_of;

using fo4::test::envnode::floats;

using fo4::test::envnode::grp;

using fo4::test::envnode::make_plugin;

using fo4::test::envnode::rec;

using fo4::test::envnode::top;

using fo4::test::envnode::zstring;

constexpr std::uint32_t k_world_form_id = 0x800;

constexpr std::uint32_t k_placed_cell_form_id = 0x810;

constexpr std::uint32_t k_placed_reference_form_id = 0x820;

constexpr std::uint32_t k_stat_form_id = 0x2100;

inline std::vector<std::byte> xclc_data(std::int32_t x, std::int32_t y) {
  fo4::test::ByteWriter out;
  out.i32(x);
  out.i32(y);
  out.u32(0);
  return out.bytes;
}

inline std::uint32_t grid_group_label(std::int32_t x, std::int32_t y, bool block) {
  const auto layout = fo4::forms::exterior_cell_grid_layout(x, y);
  return fo4::forms::exterior_grid_group_label_u32(
      block ? layout.block_x : layout.subblock_x,
      block ? layout.block_y : layout.subblock_y);
}

inline fo4::test::envnode::Node stat_record() {
  return rec("STAT", k_stat_form_id,
             {{"EDID", zstring("TableStat")},
              {"MODL", zstring("Props\\Table.nif")}});
}

// The fixture must hold one exterior CELL under the groups of (x, y), with an optional children group behind it.
inline std::vector<std::byte> plugin_with_placed_exterior_cell(std::int32_t x,
                                                               std::int32_t y,
                                                               bool with_reference) {
  std::vector<fo4::test::envnode::Node> subblock_children{
      rec("CELL", k_placed_cell_form_id,
          {{"EDID", zstring("PlacedExterior")},
           {"DATA", {std::byte{0x02}, std::byte{0x00}}},
           {"XCLC", xclc_data(x, y)}})};
  if (with_reference) {
    subblock_children.push_back(grp(
        k_placed_cell_form_id, 6,
        {grp(k_placed_cell_form_id, 9,
             {rec("REFR", k_placed_reference_form_id,
                  {{"NAME", bytes_of({k_stat_form_id})},
                   {"DATA", floats({10.0f, 20.0f, 30.0f, 0.0f, 0.0f, 0.0f})}})})}));
  }
  return make_plugin(
      {}, {top("WRLD",
               {rec("WRLD", k_world_form_id, {{"EDID", zstring("TestWorld")}}),
                grp(k_world_form_id, 1,
                    {grp(grid_group_label(x, y, true), 4,
                         {grp(grid_group_label(x, y, false), 5,
                              std::move(subblock_children))})})}),
           top("STAT", {stat_record()})});
}

// The CELL must carry XCLC (1, 2) while its groups are those of (9, 3).
inline std::vector<std::byte> plugin_with_misfiled_exterior_cell() {
  return make_plugin(
      {}, {top("WRLD",
               {rec("WRLD", k_world_form_id, {{"EDID", zstring("TestWorld")}}),
                grp(k_world_form_id, 1,
                    {grp(grid_group_label(9, 3, true), 4,
                         {grp(grid_group_label(9, 3, false), 5,
                              {rec("CELL", k_placed_cell_form_id,
                                   {{"EDID", zstring("Misfiled")},
                                    {"DATA", {std::byte{0x02}, std::byte{0x00}}},
                                    {"XCLC", xclc_data(1, 2)}})})})})})});
}

// XCLC must carry at least eight bytes for the coordinate pair.
inline std::vector<std::byte> plugin_with_truncated_xclc() {
  return make_plugin(
      {}, {top("WRLD",
               {rec("WRLD", k_world_form_id, {{"EDID", zstring("TestWorld")}}),
                grp(k_world_form_id, 1,
                    {grp(grid_group_label(1, 2, true), 4,
                         {grp(grid_group_label(1, 2, false), 5,
                              {rec("CELL", k_placed_cell_form_id,
                                   {{"EDID", zstring("TruncatedXclc")},
                                    {"DATA", {std::byte{0x02}, std::byte{0x00}}},
                                    {"XCLC", {std::byte{0x01}, std::byte{0x00}}}})})})})})});
}

// The persistent exterior CELL must hang directly off the worldspace children group.
inline std::vector<std::byte> plugin_with_persistent_exterior_cell() {
  return make_plugin(
      {}, {top("WRLD",
               {rec("WRLD", k_world_form_id, {{"EDID", zstring("TestWorld")}}),
                grp(k_world_form_id, 1,
                    {rec("CELL", k_placed_cell_form_id,
                         {{"EDID", zstring("Persistent")},
                          {"DATA", {std::byte{0x02}, std::byte{0x00}}}})})})});
}

// Two sibling block groups must never share a label; the second one here is a stray empty copy of the first.
inline std::vector<std::byte> plugin_with_duplicate_source_block() {
  return make_plugin(
      {}, {top("WRLD",
               {rec("WRLD", k_world_form_id, {{"EDID", zstring("TestWorld")}}),
                grp(k_world_form_id, 1,
                    {grp(grid_group_label(1, 2, true), 4,
                         {grp(grid_group_label(1, 2, false), 5,
                              {rec("CELL", k_placed_cell_form_id,
                                   {{"EDID", zstring("Duplicated")},
                                    {"DATA", {std::byte{0x02}, std::byte{0x00}}},
                                    {"XCLC", xclc_data(1, 2)}})})}),
                     grp(grid_group_label(1, 2, true), 4,
                         {grp(grid_group_label(1, 2, false), 5, {})})})})});
}

inline const fo4::Group* world_children_group(const fo4::Plugin& plugin) {
  const auto* top_group = top_wrld(plugin);
  return top_group == nullptr
             ? nullptr
             : child_group(top_group->children, fo4::GroupType::world_children,
                           k_world_form_id);
}

inline fo4::FormKey placed_cell_key() { return fo4::FormKey::parse("000810:Cells.esp"); }

inline fo4::FormKey placed_world_key() { return fo4::FormKey::parse("000800:Cells.esp"); }

constexpr std::uint32_t k_navmesh_form_id = 0x830;

constexpr std::uint32_t k_deleted_flag = 0x20;

inline std::vector<std::byte> nvnm_for_grid(std::int16_t grid_x, std::int16_t grid_y) {
  const float bx = static_cast<float>(grid_x) * 4096.0f;
  const float by = static_cast<float>(grid_y) * 4096.0f;
  fo4::test::ByteWriter out;
  out.u32(15);
  out.u32(0xA5D3DE3Cu);
  out.u32(k_world_form_id);
  out.u16(static_cast<std::uint16_t>(grid_y));
  out.u16(static_cast<std::uint16_t>(grid_x));
  out.u32(3);
  for (const auto& vertex : {std::array<float, 3>{bx + 1.0f, by + 2.0f, 7.0f},
                             std::array<float, 3>{bx + 4000.5f, by + 10.0f, 8.0f},
                             std::array<float, 3>{bx + 4095.25f, by + 4000.0f, 9.0f}}) {
    for (const auto value : vertex) out.f32(value);
  }
  out.u32(1);
  for (std::uint16_t i = 0; i < 3; ++i) out.u16(i);
  for (int i = 0; i < 3; ++i) out.u16(0xFFFF);
  out.f32(4.5f);
  out.u8(0xAA);
  out.u32(0xABCD8012u);
  out.u32(0);
  out.u32(0);
  out.u32(0);
  out.u32(0);
  out.u32(1);
  out.f32(bx + 50.5f);
  out.f32(by + 60.25f);
  out.f32(11.0f);
  out.u16(0);
  out.u32(0);
  out.u32(1);
  out.f32(4095.0f);
  out.f32(3990.0f);
  out.f32(bx + 1.0f);
  out.f32(by + 2.0f);
  out.f32(7.0f);
  out.f32(bx + 4095.25f);
  out.f32(by + 4000.0f);
  out.f32(std::numeric_limits<float>::max());
  out.u32(1);
  out.u16(0);
  return out.bytes;
}

inline fo4::test::envnode::Node placed(const char* signature,
                                       std::uint32_t form_id,
                                       std::initializer_list<float> transform) {
  return rec(signature, form_id,
             {{"NAME", bytes_of({k_stat_form_id})}, {"DATA", floats(transform)}});
}

inline std::vector<std::byte> plugin_with_cell_contents(
           std::int32_t x,
           std::int32_t y,
           std::vector<fo4::test::envnode::Node> persistent,
           std::vector<fo4::test::envnode::Node> temporary) {
  return make_plugin(
      {}, {top("WRLD",
               {rec("WRLD", k_world_form_id, {{"EDID", zstring("TestWorld")}}),
                grp(k_world_form_id, 1,
                    {grp(grid_group_label(x, y, true), 4,
                         {grp(grid_group_label(x, y, false), 5,
                              {rec("CELL", k_placed_cell_form_id,
                                   {{"EDID", zstring("PlacedExterior")},
                                    {"DATA", {std::byte{0x02}, std::byte{0x00}}},
                                    {"XCLC", xclc_data(x, y)}}),
                               grp(k_placed_cell_form_id, 6,
                                   {grp(k_placed_cell_form_id, 8, std::move(persistent)),
                                    grp(k_placed_cell_form_id, 9,
                                        std::move(temporary))})})})})}),
           top("STAT", {stat_record()})});
}

inline fo4::FormKey cells_key(std::uint32_t form_id) {
  return {fo4::PluginKey::from_filename("Cells.esp"), fo4::LocalFormId{form_id}};
}

inline std::vector<std::byte> data_of(const fo4::Plugin& plugin, std::uint32_t form_id) {
  const auto* record = fo4::find_record(plugin, cells_key(form_id));
  if (record == nullptr) return {};
  const auto* data = unique_subrecord(*record, "DATA");
  return data == nullptr ? std::vector<std::byte>{} : data->data;
}

inline float float_at(const std::vector<std::byte>& data, std::size_t offset) {
  float value = 0;
  std::memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

inline std::vector<std::byte> nvnm_of(const fo4::Plugin& plugin) {
  const auto* record = fo4::find_record(plugin, cells_key(k_navmesh_form_id));
  if (record == nullptr) return {};
  const auto* nvnm = unique_subrecord(*record, "NVNM");
  return nvnm == nullptr ? std::vector<std::byte>{} : nvnm->data;
}

inline std::vector<std::byte> after_tes4(std::vector<std::byte> bytes) {
  constexpr std::size_t k_header_size = 24;
  std::uint32_t tes4_size = 0;
  std::memcpy(&tes4_size, bytes.data() + 4, sizeof(tes4_size));
  bytes.erase(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(k_header_size + tes4_size));
  return bytes;
}

constexpr std::uint32_t k_door_form_id = 0x820;

constexpr std::uint32_t k_unmoved_door_form_id = 0x821;

constexpr std::uint32_t k_persistent_door_form_id = 0x822;

constexpr std::uint32_t k_transition_form_id = 0x812;

constexpr std::uint32_t k_far_cell_form_id = 0x900;

constexpr std::uint32_t k_far_door_form_id = 0x910;

constexpr std::uint32_t k_far_persistent_door_form_id = 0x911;

constexpr std::uint32_t k_far_unrelated_door_form_id = 0x912;

constexpr std::uint32_t k_missing_door_form_id = 0x8FE;

constexpr std::uint32_t k_persistent_flag = 0x400;

constexpr float k_xtel_x = 100.5f;

constexpr float k_xtel_y = -200.25f;

inline std::vector<std::byte> xtel_data(std::uint32_t door, float x, float y) {
  fo4::test::ByteWriter out;
  out.u32(door);
  for (const auto value : {x, y, 12.0f, 0.5f, 0.25f, 1.5f}) out.f32(value);
  out.u32(2);
  out.u32(k_transition_form_id);
  return out.bytes;
}

inline fo4::test::envnode::Node far_door(std::uint32_t form_id, std::vector<std::byte> xtel) {
  return rec("REFR", form_id,
             {{"NAME", bytes_of({k_stat_form_id})},
              {"XTEL", std::move(xtel)},
              {"DATA", floats({0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f})}},
             k_persistent_flag);
}

inline fo4::test::envnode::Node far_interior(std::vector<fo4::test::envnode::Node> doors) {
  return top("CELL",
             {grp(0, 2,
                  {grp(0, 3,
                       {rec("CELL", k_far_cell_form_id,
                            {{"EDID", zstring("FarInterior")}, {"DATA", {std::byte{0x01}}}}),
                        grp(k_far_cell_form_id, 6,
                            {grp(k_far_cell_form_id, 8, std::move(doors))})})})});
}

inline fo4::test::envnode::Node door_cell_group(std::vector<fo4::test::envnode::Node> persistent,
                                                std::vector<fo4::test::envnode::Node> temporary) {
  return grp(k_world_form_id, 1,
             {grp(grid_group_label(1, 2, true), 4,
                  {grp(grid_group_label(1, 2, false), 5,
                       {rec("CELL", k_placed_cell_form_id,
                            {{"EDID", zstring("PlacedExterior")},
                             {"DATA", {std::byte{0x02}, std::byte{0x00}}},
                             {"XCLC", xclc_data(1, 2)}}),
                        grp(k_placed_cell_form_id, 6,
                            {grp(k_placed_cell_form_id, 8, std::move(persistent)),
                             grp(k_placed_cell_form_id, 9, std::move(temporary))})})})});
}

inline std::vector<std::byte> door_master_plugin(std::vector<fo4::test::envnode::Node> far_doors) {
  return make_plugin(
      {}, {top("WRLD", {rec("WRLD", k_world_form_id, {{"EDID", zstring("TestWorld")}}),
                        door_cell_group({}, {placed("REFR", k_door_form_id,
                                                    {5.0f, 6.0f, 7.0f, 0.0f, 0.0f, 0.0f}),
                                             placed("REFR", k_unmoved_door_form_id,
                                                    {8.0f, 9.0f, 7.0f, 0.0f, 0.0f, 0.0f})})}),
           top("STAT", {stat_record()}), far_interior(std::move(far_doors))});
}

inline std::vector<std::byte> door_patch_plugin(std::vector<fo4::test::envnode::Node> far_doors) {
  std::vector<fo4::test::envnode::Node> groups{
      top("WRLD", {rec("WRLD", k_world_form_id, {{"EDID", zstring("TestWorld")}}),
                   door_cell_group(
                       {placed("REFR", k_persistent_door_form_id, {1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 0.0f})},
                       {placed("REFR", k_door_form_id, {5.0f, 6.0f, 7.0f, 0.0f, 0.0f, 0.0f})})})};
  if (!far_doors.empty()) groups.push_back(far_interior(std::move(far_doors)));
  return make_plugin({"Master.esm"}, groups);
}

inline fo4::FormKey door_key(std::uint32_t form_id) {
  return {fo4::PluginKey::from_filename("Master.esm"), fo4::LocalFormId{form_id}};
}

inline std::vector<std::byte> xtel_of(const fo4::Plugin& plugin, std::uint32_t form_id) {
  const auto* record = fo4::find_record(plugin, door_key(form_id));
  if (record == nullptr) return {};
  const auto* xtel = unique_subrecord(*record, "XTEL");
  return xtel == nullptr ? std::vector<std::byte>{} : xtel->data;
}

inline std::size_t count_records(const fo4::Plugin& plugin, std::uint32_t form_id) {
  std::size_t count = 0;
  fo4::visit_records(plugin, [&](const fo4::Record& record) {
    if (record.header.signature != fo4::Signature::from_string("TES4") &&
        record.form_key == door_key(form_id)) {
      ++count;
    }
  });
  return count;
}

struct DoorFixture {
  fo4::Plugin master;
  fo4::Plugin patch;
  fo4::env::Environment env;
};

inline DoorFixture door_fixture(std::vector<fo4::test::envnode::Node> master_far_doors,
                                std::vector<fo4::test::envnode::Node> patch_far_doors = {}) {
  auto master = fo4::read_plugin(door_master_plugin(std::move(master_far_doors)), "Master.esm");
  auto patch = fo4::read_plugin(door_patch_plugin(std::move(patch_far_doors)), "Patch.esp");
  std::vector<fo4::Plugin> order;
  order.push_back(master);
  order.push_back(patch);
  return {std::move(master), std::move(patch),
          fo4::env::Environment::from_plugins(std::move(order))};
}

}  // namespace fo4::test::exterior_cell
