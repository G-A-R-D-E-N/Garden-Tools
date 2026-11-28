#include "fo4/c_api_authoring.h"
#include "fo4/c_api_navmesh.h"
#include "fo4/environment/navmesh.hpp"
#include "fo4/plugin/navmesh_structure.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/scalar.hpp"
#include "navmesh_writer_fixture.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace fo4;
using fo4::test::ByteWriter;

FormKey key(std::uint32_t id) {
  return {PluginKey::from_filename("Navigation.esm"), LocalFormId{id}};
}

ByteWriter navmesh_geometry(std::uint32_t cell) {
  ByteWriter b;
  b.u32(15); b.u32(0x12345678); b.u32(0); b.u32(cell);
  for (int i = 0; i < 7; ++i) b.u32(0);
  for (int i = 0; i < 9; ++i) b.u32(0);
  return b;
}

ByteWriter info_bytes(std::uint32_t navmesh, std::uint32_t flags, std::uint32_t cell) {
  ByteWriter b;
  b.u32(navmesh); b.u32(flags);
  for (int i = 0; i < 4; ++i) b.f32(0);
  b.u32(0); b.u32(0); b.u32(0);
  b.u8(0);
  b.u32(0); b.u32(0); b.u32(cell);
  return b;
}

Plugin navi_plugin() {
  ByteWriter out;
  auto record = fo4::test::begin_record(out, "TES4", 1, 0, 131);
  ByteWriter header; header.f32(1); header.u32(2); header.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", header.bytes);
  fo4::test::end_record(out, record);

  auto group = fo4::test::begin_group(out, "NAVM", 0);
  for (const auto id : {0x800u, 0x802u}) {
    record = fo4::test::begin_record(out, "NAVM", 0, id, 131);
    fo4::test::write_subrecord(out, "NVNM", navmesh_geometry(0x810).bytes);
    fo4::test::end_record(out, record);
  }
  fo4::test::end_group(out, group);

  group = fo4::test::begin_group(out, "NAVI", 0);
  record = fo4::test::begin_record(out, "NAVI", 0, 0x801, 131);
  ByteWriter version; version.u32(15);
  fo4::test::write_subrecord(out, "NVER", version.bytes);
  ByteWriter paths; paths.u32(0); paths.u32(0);
  fo4::test::write_subrecord(out, "NVPP", paths.bytes);
  fo4::test::write_subrecord(out, "NVMI", info_bytes(0x800, 0, 0x810).bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  group = fo4::test::begin_group(out, "CELL", 0);
  record = fo4::test::begin_record(out, "CELL", 0, 0x810, 131);
  ByteWriter interior; interior.u8(1); interior.u8(0);
  fo4::test::write_subrecord(out, "DATA", interior.bytes);
  fo4::test::end_record(out, record);
  record = fo4::test::begin_record(out, "CELL", 0, 0x811, 131);
  ByteWriter exterior; exterior.u8(0); exterior.u8(0);
  fo4::test::write_subrecord(out, "DATA", exterior.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  group = fo4::test::begin_group(out, "REFR", 0);
  record = fo4::test::begin_record(out, "REFR", 0, 0x812, 131);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);

  return read_plugin(out.bytes, "Navigation.esm");
}

std::array<char, 4> group_label_bytes(std::uint32_t value) {
  return {
      static_cast<char>(value & 0xFFu),
      static_cast<char>((value >> 8) & 0xFFu),
      static_cast<char>((value >> 16) & 0xFFu),
      static_cast<char>((value >> 24) & 0xFFu),
  };
}

Plugin navi_plugin_with_exterior() {
  ByteWriter out;
  auto record = fo4::test::begin_record(out, "TES4", 1, 0, 131);
  ByteWriter header; header.f32(1); header.u32(2); header.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", header.bytes);
  fo4::test::end_record(out, record);

  const auto world_label = group_label_bytes(0x820);
  const auto grid_label = group_label_bytes(0);
  const auto world_top = fo4::test::begin_group(out, "WRLD", 0);
  record = fo4::test::begin_record(out, "WRLD", 0, 0x820, 131);
  fo4::test::end_record(out, record);
  const auto world_children = fo4::test::begin_group(out, std::string_view{world_label.data(), 4}, 1);
  const auto block = fo4::test::begin_group(out, std::string_view{grid_label.data(), 4}, 4);
  const auto subblock = fo4::test::begin_group(out, std::string_view{grid_label.data(), 4}, 5);
  record = fo4::test::begin_record(out, "CELL", 0, 0x830, 131);
  ByteWriter exterior; exterior.u8(0); exterior.u8(0);
  fo4::test::write_subrecord(out, "DATA", exterior.bytes);
  ByteWriter grid; grid.i32(7); grid.i32(3);
  fo4::test::write_subrecord(out, "XCLC", grid.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, subblock);
  fo4::test::end_group(out, block);
  fo4::test::end_group(out, world_children);
  fo4::test::end_group(out, world_top);

  return read_plugin(out.bytes, "Navigation.esm");
}

// An override of a master CELL keeps the master as owner, the shape ownership must not gate.
Plugin navi_plugin_with_override_cell() {
  ByteWriter out;
  auto record = fo4::test::begin_record(out, "TES4", 1, 0, 131);
  ByteWriter header; header.f32(1); header.u32(1); header.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", header.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  ByteWriter data; data.u32(0); data.u32(0);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, record);

  const auto cell_label = group_label_bytes(0x810);
  const auto group = fo4::test::begin_group(out, "CELL", 0);
  const auto children = fo4::test::begin_group(out, std::string_view{cell_label.data(), 4}, 2);
  record = fo4::test::begin_record(out, "CELL", 0, 0x810, 131);
  ByteWriter interior; interior.u8(1); interior.u8(0);
  fo4::test::write_subrecord(out, "DATA", interior.bytes);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, children);
  fo4::test::end_group(out, group);
  return read_plugin(out.bytes, "Navigation.esm");
}

template <class Check> void expect_reparsed(const Plugin& plugin, Check&& check) {
  const auto bytes = write_plugin(plugin);
  const auto loaded = env::Environment::from_plugins({read_plugin(bytes, "Navigation.esm")});
  check(loaded);
}

template <class Check> void expect_infos(const Plugin& plugin, Check&& check) {
  expect_reparsed(plugin, [&](const env::Environment& loaded) {
    const auto infos = env::decode_navmesh_info(loaded, *loaded.winner(key(0x801)));
    check(infos);
  });
}

std::optional<std::vector<std::byte>> subrecord_bytes(Plugin& plugin, const FormKey& record_key, const char* signature) {
  const auto* found = find_record(plugin, record_key);
  if (found == nullptr) return std::nullopt;
  for (const auto& sub : found->subrecords) {
    if (sub.signature == Signature::from_string(signature)) return sub.data;
  }
  return std::nullopt;
}

std::uint32_t read_u32(const std::vector<std::byte>& data, std::size_t at) {
  std::uint32_t value = 0;
  std::memcpy(&value, data.data() + at, sizeof value);
  return value;
}

bool temporary_contains(const Group& group, const FormKey& record_key, bool temporary) {
  for (const auto& child : group.children) {
    if (const auto* record = std::get_if<Record>(&child)) {
      if (temporary && record->form_key == record_key) return true;
    } else if (const auto* nested = std::get_if<Group>(&child)) {
      const bool inside = nested->header.group_type == static_cast<std::int32_t>(GroupType::cell_temporary_children);
      if (temporary_contains(*nested, record_key, temporary || inside)) return true;
    }
  }
  return false;
}

TEST(NaviStructure, AddInfoAppendsAndRoundTrips) {
  auto plugin = navi_plugin();
  env::NavmeshInfo info;
  info.navmesh = key(0x802);
  info.location = {1.0f, 2.0f, 3.0f};
  info.preferred = 4.5f;
  info.parent.cell = key(0x810);
  EXPECT_EQ(add_navi_info(plugin, key(0x801), info), ScalarMutationStatus::updated);
  expect_infos(plugin, [](const std::vector<env::NavmeshInfo>& infos) {
    ASSERT_EQ(infos.size(), 2u);
    EXPECT_EQ(infos[0].navmesh, key(0x800));
    EXPECT_EQ(infos[1].navmesh, key(0x802));
    EXPECT_EQ(infos[1].location, (std::array<float, 3>{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(infos[1].preferred, 4.5f);
    EXPECT_EQ(infos[1].parent.cell, key(0x810));
  });
}

TEST(NaviStructure, RefusedEditsLeaveBytesUnchanged) {
  auto plugin = navi_plugin();
  const auto before = write_plugin(plugin);

  env::NavmeshInfo duplicate;
  duplicate.navmesh = key(0x800);
  duplicate.parent.cell = key(0x810);
  EXPECT_THROW(add_navi_info(plugin, key(0x801), duplicate), schema::ScalarError);

  env::NavmeshInfo anchored;
  anchored.navmesh = key(0x802);
  anchored.flags = 0x40;
  anchored.parent.cell = key(0x810);
  EXPECT_THROW(add_navi_info(plugin, key(0x801), anchored), schema::ScalarError);

  env::NavmeshInfo not_navm;
  not_navm.navmesh = key(0x812);
  not_navm.parent.cell = key(0x810);
  EXPECT_THROW(add_navi_info(plugin, key(0x801), not_navm), schema::ScalarError);

  env::NavmeshInfo islanded;
  islanded.navmesh = key(0x802);
  islanded.has_island = true;
  islanded.parent.cell = key(0x810);
  EXPECT_THROW(add_navi_info(plugin, key(0x801), islanded), schema::ScalarError);

  EXPECT_THROW(add_navi_info(plugin, key(0x802), anchored), schema::ScalarError);
  EXPECT_THROW(add_navi_link(plugin, key(0x801), 0, NaviLinkKind::edge, key(0x812)), schema::ScalarError);
  EXPECT_THROW(add_navi_link(plugin, key(0x801), 0, NaviLinkKind::door, key(0x802)), schema::ScalarError);

  env::NaviDoorLink wrong_door;
  wrong_door.door = key(0x802);
  EXPECT_THROW(add_navi_door(plugin, key(0x801), 0, wrong_door), schema::ScalarError);
  EXPECT_THROW(add_navi_deleted(plugin, key(0x801), key(0x812)), schema::ScalarError);
  EXPECT_EQ(write_plugin(plugin), before);
}

TEST(NaviStructure, LinksAddAndRemoveByKind) {
  auto plugin = navi_plugin();
  EXPECT_EQ(add_navi_link(plugin, key(0x801), 0, NaviLinkKind::edge, key(0x802)), ScalarMutationStatus::updated);
  EXPECT_EQ(add_navi_link(plugin, key(0x801), 0, NaviLinkKind::preferred_edge, key(0x802)),
            ScalarMutationStatus::updated);
  env::NaviDoorLink door;
  door.crc = 0xABCDEF01u;
  door.door = key(0x812);
  EXPECT_EQ(add_navi_door(plugin, key(0x801), 0, door), ScalarMutationStatus::updated);
  expect_infos(plugin, [](const std::vector<env::NavmeshInfo>& infos) {
    ASSERT_EQ(infos.size(), 1u);
    ASSERT_EQ(infos[0].edges.size(), 1u);
    EXPECT_EQ(infos[0].edges[0], key(0x802));
    ASSERT_EQ(infos[0].preferred_edges.size(), 1u);
    ASSERT_EQ(infos[0].doors.size(), 1u);
    EXPECT_EQ(infos[0].doors[0].crc, 0xABCDEF01u);
    EXPECT_EQ(infos[0].doors[0].door, key(0x812));
  });

  EXPECT_EQ(remove_navi_link(plugin, key(0x801), 0, NaviLinkKind::edge, 0), ScalarMutationStatus::updated);
  EXPECT_EQ(remove_navi_link(plugin, key(0x801), 0, NaviLinkKind::edge, 0), ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(add_navi_link(plugin, key(0x801), 3, NaviLinkKind::edge, key(0x802)),
            ScalarMutationStatus::subrecord_missing);
  expect_infos(plugin, [](const std::vector<env::NavmeshInfo>& infos) {
    ASSERT_EQ(infos.size(), 1u);
    EXPECT_TRUE(infos[0].edges.empty());
    EXPECT_EQ(infos[0].preferred_edges.size(), 1u);
    EXPECT_EQ(infos[0].doors.size(), 1u);
  });
}

TEST(NaviStructure, RemoveInfoDropsEntriesAndReportsMissing) {
  auto plugin = navi_plugin();
  env::NavmeshInfo info;
  info.navmesh = key(0x802);
  info.parent.cell = key(0x810);
  EXPECT_EQ(add_navi_info(plugin, key(0x801), info), ScalarMutationStatus::updated);
  EXPECT_EQ(remove_navi_info(plugin, key(0x801), 1), ScalarMutationStatus::updated);
  EXPECT_EQ(remove_navi_info(plugin, key(0x801), 1), ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(remove_navi_info(plugin, key(0x801), 0), ScalarMutationStatus::updated);
  expect_infos(plugin, [](const std::vector<env::NavmeshInfo>& infos) { EXPECT_TRUE(infos.empty()); });
}

TEST(NaviStructure, DeletedListCreatesAndDrainsNvsi) {
  auto plugin = navi_plugin();
  EXPECT_FALSE(subrecord_bytes(plugin, key(0x801), "NVSI").has_value());
  EXPECT_EQ(add_navi_deleted(plugin, key(0x801), key(0x800)), ScalarMutationStatus::updated);
  EXPECT_EQ(add_navi_deleted(plugin, key(0x801), key(0x802)), ScalarMutationStatus::updated);
  const auto list = subrecord_bytes(plugin, key(0x801), "NVSI");
  ASSERT_TRUE(list.has_value());
  ASSERT_EQ(list->size(), 8u);
  EXPECT_EQ(read_u32(*list, 4), 0x802u);
  EXPECT_EQ(remove_navi_deleted(plugin, key(0x801), 0), ScalarMutationStatus::updated);
  EXPECT_EQ(remove_navi_deleted(plugin, key(0x801), 1), ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(remove_navi_deleted(plugin, key(0x801), 0), ScalarMutationStatus::updated);
  EXPECT_FALSE(subrecord_bytes(plugin, key(0x801), "NVSI").has_value());
  EXPECT_EQ(remove_navi_deleted(plugin, key(0x801), 0), ScalarMutationStatus::subrecord_missing);
}

TEST(NaviStructure, CreateNavmeshPlacesInteriorTemporaryChildren) {
  auto plugin = navi_plugin();
  const auto created = create_navmesh(plugin, key(0x810));
  bool placed = false;
  for (const auto& group : plugin.groups) placed = placed || temporary_contains(group, created, false);
  EXPECT_TRUE(placed);
  expect_reparsed(plugin, [&](const env::Environment& loaded) {
    const auto geometry = env::decode_navmesh(loaded, *loaded.winner(created));
    ASSERT_TRUE(geometry.has_value());
    EXPECT_EQ(geometry->version, 15u);
    EXPECT_EQ(geometry->parent.cell, key(0x810));
    EXPECT_TRUE(geometry->vertices.empty());
  });
}

TEST(NaviStructure, CreateNavmeshPlacesExteriorTemporaryChildren) {
  auto plugin = navi_plugin_with_exterior();
  const auto created = create_navmesh(plugin, key(0x830));
  bool placed = false;
  for (const auto& group : plugin.groups) placed = placed || temporary_contains(group, created, false);
  EXPECT_TRUE(placed);
  expect_reparsed(plugin, [&](const env::Environment& loaded) {
    const auto geometry = env::decode_navmesh(loaded, *loaded.winner(created));
    ASSERT_TRUE(geometry.has_value());
    EXPECT_EQ(geometry->version, 15u);
    EXPECT_EQ(geometry->parent.world, key(0x820));
    EXPECT_EQ(geometry->parent.grid_x, 7);
    EXPECT_EQ(geometry->parent.grid_y, 3);
  });
}

// Detects: the owner gate refused an override of a master CELL, so a navmesh must be creatable there.
TEST(NaviStructure, CreateNavmeshAcceptsAnOverriddenMasterCell) {
  auto plugin = navi_plugin_with_override_cell();
  const FormKey master_cell{PluginKey::from_filename("Fallout4.esm"), LocalFormId{0x810}};
  const auto created = create_navmesh(plugin, master_cell);
  bool placed = false;
  for (const auto& group : plugin.groups) placed = placed || temporary_contains(group, created, false);
  EXPECT_TRUE(placed);
  expect_reparsed(plugin, [&](const env::Environment& loaded) {
    const auto geometry = env::decode_navmesh(loaded, *loaded.winner(created));
    ASSERT_TRUE(geometry.has_value());
    EXPECT_EQ(geometry->version, 15u);
    EXPECT_EQ(geometry->parent.cell, master_cell);
  });
}

TEST(NaviStructure, CreateNavmeshRefusesExteriorAndNonCellParents) {
  auto plugin = navi_plugin();
  const auto before = write_plugin(plugin);
  EXPECT_THROW(create_navmesh(plugin, key(0x811)), schema::ScalarError);
  EXPECT_THROW(create_navmesh(plugin, key(0x812)), schema::ScalarError);
  EXPECT_THROW(create_navmesh(plugin, key(0x800)), schema::ScalarError);
  EXPECT_EQ(write_plugin(plugin), before);
}

TEST(NaviStructure, CreateNavmeshRefusesFormVersionBelow128) {
  auto plugin = navi_plugin();
  plugin.header.form_version = 127;
  const auto before = write_plugin(plugin);
  EXPECT_THROW(create_navmesh(plugin, key(0x810)), schema::ScalarError);
  EXPECT_EQ(write_plugin(plugin), before);
}

TEST(NaviStructure, CAbiExportsValidateAndRoundTrip) {
  const auto source = write_plugin(navi_plugin());
  const auto root = std::filesystem::temp_directory_path() /
      ("navi_structure_c_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  const auto path = root / "Navigation.esm";
  {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(source.data()), static_cast<std::streamsize>(source.size()));
  }
  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(path.string().c_str(), &plugin), RADICLE_STATUS_OK);

  const radicle_form_key navi{"Navigation.esm", 0x801};
  const radicle_form_key navm_802{"Navigation.esm", 0x802};
  const radicle_form_key cell{"Navigation.esm", 0x810};
  const radicle_form_key refr{"Navigation.esm", 0x812};

  radicle_navmesh_info info{};
  info.navmesh = navm_802;
  info.location = {1.0f, 2.0f, 3.0f};
  info.preferred = 4.5f;
  info.parent.cell = cell;
  EXPECT_EQ(radicle_plugin_add_navi_info(plugin, navi, &info), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_add_navi_info(plugin, navi, &info), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_navmesh_info anchored = info;
  anchored.flags = 0x40;
  EXPECT_EQ(radicle_plugin_add_navi_info(plugin, navi, &anchored), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_navmesh_info no_cell = info;
  no_cell.parent.cell = {nullptr, 0};
  EXPECT_EQ(radicle_plugin_add_navi_info(plugin, navi, &no_cell), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_navmesh_info null_edges = info;
  null_edges.edge_count = 1;
  null_edges.edges = nullptr;
  EXPECT_EQ(radicle_plugin_add_navi_info(plugin, navi, &null_edges), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_navmesh_info null_preferred = info;
  null_preferred.preferred_edge_count = 1;
  null_preferred.preferred_edges = nullptr;
  EXPECT_EQ(radicle_plugin_add_navi_info(plugin, navi, &null_preferred), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_navmesh_info null_doors = info;
  null_doors.door_count = 1;
  null_doors.doors = nullptr;
  EXPECT_EQ(radicle_plugin_add_navi_info(plugin, navi, &null_doors), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_navi_info(nullptr, navi, &info), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_navi_info(plugin, navi, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_navi_info(plugin, {"Navigation.esm", 0x899}, &info), RADICLE_STATUS_NOT_FOUND);

  EXPECT_EQ(radicle_plugin_add_navi_link(plugin, navi, 0, RADICLE_NAVI_EDGE, navm_802), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_add_navi_link(plugin, navi, 0, RADICLE_NAVI_DOOR, navm_802), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_navi_link(plugin, navi, 5, RADICLE_NAVI_EDGE, navm_802), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_remove_navi_link(plugin, navi, 0, RADICLE_NAVI_EDGE, 0), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_remove_navi_link(plugin, navi, 0, RADICLE_NAVI_EDGE, 0), RADICLE_STATUS_NOT_FOUND);

  const radicle_navi_door_link door{0x1234, refr};
  const radicle_navi_door_link bad_door{0x1234, navm_802};
  EXPECT_EQ(radicle_plugin_add_navi_door(plugin, navi, 1, &door), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_add_navi_door(plugin, navi, 1, &bad_door), RADICLE_STATUS_INVALID_ARGUMENT);

  EXPECT_EQ(radicle_plugin_add_navi_deleted(plugin, navi, navm_802), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_add_navi_deleted(plugin, navi, refr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_remove_navi_deleted(plugin, navi, 0), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_remove_navi_deleted(plugin, navi, 0), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_remove_navi_info(plugin, navi, 1), RADICLE_STATUS_OK);

  const radicle_form_key path_links[2] = {navm_802, {"Navigation.esm", 0x800}};
  EXPECT_EQ(radicle_plugin_add_navi_path(plugin, navi, path_links, 2), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_add_navi_path(plugin, navi, nullptr, 1), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_navi_path(plugin, navi, path_links, 0), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_add_navi_path(plugin, navi, &path_links[0], 1), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_remove_navi_path(plugin, navi, 2), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_remove_navi_path(plugin, navi, 1), RADICLE_STATUS_OK);

  radicle_form_key made{nullptr, 0};
  EXPECT_EQ(radicle_plugin_create_navmesh(plugin, cell, &made), RADICLE_STATUS_OK);
  ASSERT_NE(made.plugin, nullptr);
  const auto created = FormKey{PluginKey::from_filename("Navigation.esm"), LocalFormId{made.local_id}};
  radicle_form_key refused{nullptr, 0};
  EXPECT_EQ(radicle_plugin_create_navmesh(plugin, {"Navigation.esm", 0x811}, &refused), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_navmesh(plugin, refr, &refused), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_navmesh(plugin, cell, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_create_navmesh(nullptr, cell, &refused), RADICLE_STATUS_INVALID_ARGUMENT);

  std::uint8_t* bytes = nullptr;
  std::size_t size = 0;
  ASSERT_EQ(radicle_plugin_write_bytes(plugin, &bytes, &size), RADICLE_STATUS_OK);
  const auto parsed = read_plugin(std::as_bytes(std::span{bytes, size}), "Navigation.esm");
  EXPECT_NE(find_record(parsed, created), nullptr);
  radicle_free_bytes(bytes);
  radicle_plugin_destroy(plugin);
  std::filesystem::remove_all(root);
}

TEST(NaviStructure, FixtureAnchorInfoRemovalIsRefused) {
  auto plugin = fo4::test::navmesh_writer_fixture();
  EXPECT_THROW(remove_navi_info(plugin, fo4::test::navmesh_key(0x801), 0), schema::ScalarError);
  EXPECT_EQ(remove_navi_deleted(plugin, fo4::test::navmesh_key(0x801), 0), ScalarMutationStatus::subrecord_missing);
}

}
