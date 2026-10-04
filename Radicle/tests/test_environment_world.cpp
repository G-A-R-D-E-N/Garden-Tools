#include "fo4/c_api_env.h"
#include "fo4/environment/world.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

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

Node rec(std::string signature, std::uint32_t form_id, std::vector<Sub> subrecords = {},
         std::uint32_t flags = 0) {
  Node node;
  node.signature = std::move(signature);
  node.form_id = form_id;
  node.flags = flags;
  node.subrecords = std::move(subrecords);
  return node;
}

Node grp(std::uint32_t label, std::int32_t type, std::vector<Node> children) {
  Node node;
  node.group = true;
  node.label = label;
  node.group_type = type;
  node.children = std::move(children);
  return node;
}

Node top(std::string_view signature, std::vector<Node> children) {
  std::uint32_t label = 0;
  std::memcpy(&label, signature.data(), 4);
  return grp(label, 0, std::move(children));
}

std::vector<std::byte> bytes_of(std::initializer_list<std::uint32_t> words) {
  fo4::test::ByteWriter out;
  for (const auto word : words) out.u32(word);
  return out.bytes;
}

std::vector<std::byte> floats(std::initializer_list<float> values) {
  fo4::test::ByteWriter out;
  for (const auto value : values) out.f32(value);
  return out.bytes;
}

std::vector<std::byte> zstring(std::string_view text) {
  std::vector<std::byte> bytes;
  for (const char ch : text) bytes.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  bytes.push_back(std::byte{0});
  return bytes;
}

void write_node(fo4::test::ByteWriter& out, const Node& node) {
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

std::vector<std::byte> make_plugin(const std::vector<std::string>& masters,
                                   const std::vector<Node>& groups) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  for (const auto& master : masters) {
    fo4::test::write_subrecord(out, "MAST", zstring(master));
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
  }
  fo4::test::end_record(out, tes4);
  for (const auto& group : groups) write_node(out, group);
  return out.bytes;
}

std::vector<std::byte> xcll_bytes(std::size_t size) {
  fo4::test::ByteWriter out;
  out.u32(0x00302010);  // ambient
  out.u32(0x00050403);  // directional
  out.u32(0x00090807);  // fog near color
  out.f32(64.0f);
  out.f32(4096.0f);
  out.i32(45);
  out.i32(-30);
  out.f32(1.0f);
  out.f32(8192.0f);
  out.f32(2.0f);
  for (int i = 0; i < 32; ++i) out.u8(0);  // directional ambient + specular + fresnel
  out.u32(0x00CCBBAA);                        // fog far color
  out.f32(0.75f);
  out.f32(100.0f);
  out.f32(200.0f);
  out.u32(0x14);  // inherits fog near + fog far
  out.bytes.resize(size);
  return out.bytes;
}

// Base.esm: WRLD 000800, top cell 000801, exterior cell 000802 at (1,2) with LAND 000813, interior 000803.
std::vector<std::byte> make_base() {
  const auto wrld = rec("WRLD", 0x800,
                        {{"EDID", zstring("TestWorld")},
                         {"WNAM", bytes_of({0x805})},
                         {"PNAM", {std::byte{0x10}, std::byte{0}}},
                         {"CNAM", bytes_of({0x806})},
                         {"DNAM", floats({-2048.0f, 0.0f})},
                         {"NAM0", floats({-4096.0f, -8192.0f})},
                         {"NAM9", floats({4096.0f, 8192.0f})},
                         {"DATA", {std::byte{0x01}}}});
  const auto top_cell = rec("CELL", 0x801, {{"DATA", {std::byte{0x02}, std::byte{0}}}}, 0x400);
  const auto exterior = rec("CELL", 0x802,
                            {{"EDID", zstring("Exterior12")},
                             {"DATA", {std::byte{0x02}}},
                             {"XCLC", bytes_of({1, 2, 0x3})},
                             {"LTMP", bytes_of({0x807})},
                             {"XCLW", floats({-100.0f})}});
  const auto interior = rec("CELL", 0x803,
                            {{"EDID", zstring("Vault")},
                             {"DATA", {std::byte{0x01}, std::byte{0}}},
                             {"XCLL", xcll_bytes(92)}});
  return make_plugin(
      {},
      {top("CELL", {grp(0, 2, {grp(0, 3, {interior})})}),
       top("WRLD",
           {wrld,
            grp(0x800, 1,
                {top_cell,
                 grp(0x801, 6, {grp(0x801, 8, {rec("REFR", 0x810, {{"NAME", bytes_of({0x820})}})})}),
                 grp(0x00000000, 4,
                     {grp(0x00000000, 5,
                          {exterior,
                           grp(0x802, 6,
                               {grp(0x802, 9,
                                    {rec("REFR", 0x811), rec("REFR", 0x812),
                                     rec("LAND", 0x813)})})})})})})});
}

// Patch.esp: overrides cell 000802, deletes ref 000811, overrides 000812, adds 000900 and a LAND.
std::vector<std::byte> make_patch() {
  const auto exterior = rec("CELL", 0x802, {{"DATA", {std::byte{0x02}}}, {"XCLC", bytes_of({1, 2})}});
  return make_plugin(
      {"Base.esm"},
      {top("WRLD",
           {rec("WRLD", 0x800, {{"EDID", zstring("TestWorld")}}),
            grp(0x800, 1,
                {grp(0, 4,
                     {grp(0, 5,
                          {exterior,
                           grp(0x802, 6,
                               {grp(0x802, 9,
                                    {rec("REFR", 0x811, {}, 0x20), rec("REFR", 0x812),
                                     rec("REFR", 0x01000900), rec("LAND", 0x813)})})})})})})});
}

class WorldEnvironmentTest : public ::testing::Test {
protected:
  void SetUp() override {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() / ("radicle_world_" + std::to_string(id));
    std::filesystem::create_directories(root_);
    paths_ = {write("Base.esm", make_base()), write("Patch.esp", make_patch())};
  }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }

  std::filesystem::path write(std::string_view name, const std::vector<std::byte>& bytes) {
    const auto path = root_ / std::string(name);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return path;
  }

  std::filesystem::path root_;
  std::vector<std::filesystem::path> paths_;
};

fo4::FormKey key(std::string_view text) {
  return fo4::FormKey::parse(text);
}

}  // namespace

TEST_F(WorldEnvironmentTest, DecodesWorldspaceFields) {
  const auto env = fo4::env::Environment::load(paths_);
  const auto* base_world = &env.versions(key("000800:Base.esm"))[0];
  const auto world = fo4::env::decode_worldspace(env, *base_world);
  EXPECT_EQ(world.parent, key("000805:Base.esm"));
  EXPECT_EQ(world.parent_flags, std::optional<std::uint16_t>{0x10});
  EXPECT_EQ(world.climate, key("000806:Base.esm"));
  EXPECT_FALSE(world.water.has_value());
  EXPECT_EQ(world.default_land_height, -2048.0f);
  EXPECT_EQ(world.default_water_height, 0.0f);
  ASSERT_TRUE(world.bounds_min.has_value());
  EXPECT_EQ((*world.bounds_min)[1], -8192.0f);
  EXPECT_EQ((*world.bounds_max)[0], 4096.0f);
  EXPECT_EQ(world.flags, std::optional<std::uint8_t>{1});

  const auto winner = fo4::env::decode_worldspace(env, *env.winner(key("000800:Base.esm")));
  EXPECT_FALSE(winner.parent.has_value());
  EXPECT_FALSE(winner.default_land_height.has_value());
}

TEST_F(WorldEnvironmentTest, WalksWorldspaceHierarchy) {
  const auto env = fo4::env::Environment::load(paths_);
  const auto world = key("000800:Base.esm");

  const auto* top_cell = env.worldspace_top_cell(world);
  ASSERT_NE(top_cell, nullptr);
  EXPECT_EQ(top_cell->record->form_key, key("000801:Base.esm"));
  EXPECT_EQ(top_cell->group, fo4::GroupType::world_children);

  const auto cells = env.worldspace_cells(world);
  ASSERT_EQ(cells.size(), 1u);
  EXPECT_EQ(cells[0]->record->form_key, key("000802:Base.esm"));
  EXPECT_EQ(cells[0]->plugin, 1u);

  const auto interiors = env.interior_cells();
  ASSERT_EQ(interiors.size(), 1u);
  EXPECT_EQ(interiors[0]->record->form_key, key("000803:Base.esm"));

  EXPECT_EQ(env.world_of(*env.winner(key("000802:Base.esm"))), world);
  EXPECT_FALSE(env.world_of(*env.winner(key("000803:Base.esm"))).has_value());
  EXPECT_EQ(env.cell_of(*env.winner(key("000810:Base.esm"))), key("000801:Base.esm"));
  EXPECT_EQ(env.worldspace_top_cell(key("000999:Base.esm")), nullptr);
  EXPECT_TRUE(env.worldspace_cells(key("000999:Base.esm")).empty());
}

TEST_F(WorldEnvironmentTest, MergesCellChildrenAcrossOverrides) {
  const auto env = fo4::env::Environment::load(paths_);
  const auto children = env.cell_children(key("000802:Base.esm"));
  std::vector<std::string> keys;
  for (const auto& child : children) {
    keys.push_back(child.ref->record->form_key.to_string() + "@" +
                   std::to_string(child.ref->plugin));
    EXPECT_FALSE(child.persistent);
  }
  const std::vector<std::string> expected{"000812:Base.esm@1", "000813:Base.esm@1",
                                          "000900:Patch.esp@1"};
  EXPECT_EQ(keys, expected);

  const auto top = env.cell_children(key("000801:Base.esm"));
  ASSERT_EQ(top.size(), 1u);
  EXPECT_TRUE(top[0].persistent);

  const auto* land = env.cell_landscape(key("000802:Base.esm"));
  ASSERT_NE(land, nullptr);
  EXPECT_EQ(land->plugin, 1u);
  EXPECT_EQ(env.cell_landscape(key("000803:Base.esm")), nullptr);
}

TEST_F(WorldEnvironmentTest, AReferenceMovedToAnotherCellLeavesItsOldCell) {
  const auto mover = write(
      "Mover.esp",
      make_plugin({"Base.esm"},
                  {top("WRLD", {rec("WRLD", 0x800),
                                grp(0x800, 1,
                                    {rec("CELL", 0x801, {{"DATA", {std::byte{0x02}, std::byte{0}}}}, 0x400),
                                     grp(0x801, 6, {grp(0x801, 8, {rec("REFR", 0x812)})})})})}));
  const std::vector<std::filesystem::path> order{paths_[0], paths_[1], mover};
  const auto env = fo4::env::Environment::load(order);

  const auto has = [&](const std::vector<fo4::env::CellChild>& children, std::uint32_t id) {
    return std::ranges::any_of(children, [&](const auto& c) { return c.ref->record->form_key.local().value() == id; });
  };
  const auto old_cell = env.cell_children(key("000802:Base.esm"));
  EXPECT_FALSE(has(old_cell, 0x812));
  EXPECT_TRUE(has(old_cell, 0x900));
  const auto new_cell = env.cell_children(key("000801:Base.esm"));
  ASSERT_TRUE(has(new_cell, 0x812));
  for (const auto& child : new_cell) {
    if (child.ref->record->form_key.local().value() == 0x812) {
      EXPECT_EQ(child.ref->plugin, 2u);
    }
  }
}

TEST_F(WorldEnvironmentTest, NoneSentinelLinksDecodeLikeNull) {
  const auto none = write(
      "None.esp",
      make_plugin({"Base.esm"}, {top("WRLD", {rec("WRLD", 0x800,
                                                  {{"WNAM", bytes_of({0})},
                                                   {"CNAM", bytes_of({0xFFFFFFFFu})}})})}));
  const std::vector<std::filesystem::path> order{paths_[0], none};
  const auto env = fo4::env::Environment::load(order);
  const auto world = fo4::env::decode_worldspace(env, *env.winner(key("000800:Base.esm")));
  EXPECT_EQ(world.parent, fo4::FormKey{});
  EXPECT_EQ(world.climate, fo4::FormKey{});
}

TEST_F(WorldEnvironmentTest, DecodesCellFields) {
  const auto env = fo4::env::Environment::load(paths_);
  const auto base = fo4::env::decode_cell(env, env.versions(key("000802:Base.esm"))[0]);
  EXPECT_EQ(base.flags, std::optional<std::uint16_t>{2});
  ASSERT_TRUE(base.grid.has_value());
  EXPECT_EQ(base.grid->x, 1);
  EXPECT_EQ(base.grid->y, 2);
  EXPECT_EQ(base.grid->land_flags, std::optional<std::uint8_t>{3});
  EXPECT_EQ(base.lighting_template, key("000807:Base.esm"));
  EXPECT_EQ(base.water_height, -100.0f);
  EXPECT_EQ(base.worldspace, key("000800:Base.esm"));
  EXPECT_FALSE(base.lighting.has_value());

  const auto patched = fo4::env::decode_cell(env, *env.winner(key("000802:Base.esm")));
  EXPECT_FALSE(patched.grid->land_flags.has_value());

  const auto interior = fo4::env::decode_cell(env, *env.winner(key("000803:Base.esm")));
  EXPECT_EQ(interior.flags, std::optional<std::uint16_t>{1});
  ASSERT_TRUE(interior.lighting.has_value());
  const auto& lighting = *interior.lighting;
  EXPECT_EQ(lighting.ambient.red, 0x10);
  EXPECT_EQ(lighting.ambient.blue, 0x30);
  EXPECT_EQ(lighting.fog_near_color.red, 7);
  EXPECT_EQ(lighting.fog_near, 64.0f);
  EXPECT_EQ(lighting.fog_far, 4096.0f);
  EXPECT_EQ(lighting.directional_rotation_z, -30);
  ASSERT_TRUE(lighting.fog_far_color.has_value());
  EXPECT_EQ(lighting.fog_far_color->red, 0xAA);
  EXPECT_EQ(lighting.fog_max, 0.75f);
  EXPECT_EQ(lighting.inherits, std::optional<std::uint32_t>{0x14});
  EXPECT_FALSE(interior.worldspace.has_value());
}

TEST_F(WorldEnvironmentTest, RejectsMalformedAndWrongRecords) {
  auto broken = make_plugin(
      {}, {top("CELL", {grp(0, 2, {grp(0, 3, {rec("CELL", 0x800, {{"XCLL", xcll_bytes(40)}}),
                                              rec("CELL", 0x801, {{"XCLC", bytes_of({1})}})})})})});
  const std::vector<std::filesystem::path> ordered{write("Broken.esp", broken)};
  const auto env = fo4::env::Environment::load(ordered);
  EXPECT_THROW(fo4::env::decode_cell(env, *env.winner(key("000800:Broken.esp"))),
               fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::decode_cell(env, *env.winner(key("000801:Broken.esp"))),
               fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::decode_worldspace(env, *env.winner(key("000800:Broken.esp"))),
               fo4::env::WorldDecodeError);
}

TEST_F(WorldEnvironmentTest, CApiExposesWorldAndCellQueries) {
  std::vector<std::string> utf8;
  for (const auto& path : paths_) {
    const auto text = path.u8string();
    utf8.emplace_back(text.begin(), text.end());
  }
  const char* raw[] = {utf8[0].c_str(), utf8[1].c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(raw, 2, &env), RADICLE_STATUS_OK) << radicle_last_error();

  const radicle_record* world = nullptr;
  ASSERT_EQ(radicle_env_winner(env, {"Base.esm", 0x800}, &world), RADICLE_STATUS_OK);
  const radicle_record** versions = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_env_versions(env, {"Base.esm", 0x800}, &versions, &count), RADICLE_STATUS_OK);
  radicle_worldspace_info world_info{};
  ASSERT_EQ(radicle_worldspace_get_info(env, versions[0], &world_info), RADICLE_STATUS_OK);
  radicle_free_record_array(versions);
  EXPECT_STREQ(world_info.parent.plugin, "Base.esm");
  EXPECT_EQ(world_info.parent.local_id, 0x805u);
  EXPECT_EQ(world_info.water.plugin, nullptr);
  EXPECT_EQ(world_info.has_land_defaults, 1);
  EXPECT_EQ(world_info.default_land_height, -2048.0f);
  EXPECT_EQ(world_info.has_bounds_max, 1);
  EXPECT_EQ(world_info.bounds_max[1], 8192.0f);

  const radicle_record* top_cell = nullptr;
  ASSERT_EQ(radicle_env_worldspace_top_cell(env, {"Base.esm", 0x800}, &top_cell), RADICLE_STATUS_OK);
  int32_t group_type = -1;
  radicle_form_key container_world{};
  radicle_form_key container_cell{};
  ASSERT_EQ(radicle_record_get_container(env, top_cell, &group_type, &container_world,
                                         &container_cell),
            RADICLE_STATUS_OK);
  EXPECT_EQ(group_type, 1);
  EXPECT_STREQ(container_world.plugin, "Base.esm");
  EXPECT_EQ(container_cell.plugin, nullptr);

  const radicle_record** cells = nullptr;
  ASSERT_EQ(radicle_env_worldspace_cells(env, {"Base.esm", 0x800}, &cells, &count), RADICLE_STATUS_OK);
  ASSERT_EQ(count, 1u);
  radicle_cell_info cell_info{};
  ASSERT_EQ(radicle_cell_get_info(env, cells[0], &cell_info), RADICLE_STATUS_OK);
  radicle_free_record_array(cells);
  EXPECT_EQ(cell_info.has_grid, 1);
  EXPECT_EQ(cell_info.grid_x, 1);
  EXPECT_EQ(cell_info.grid_y, 2);
  EXPECT_EQ(cell_info.has_land_flags, 0);
  EXPECT_STREQ(cell_info.worldspace.plugin, "Base.esm");
  EXPECT_EQ(cell_info.worldspace.local_id, 0x800u);

  ASSERT_EQ(radicle_env_interior_cells(env, &cells, &count), RADICLE_STATUS_OK);
  ASSERT_EQ(count, 1u);
  ASSERT_EQ(radicle_cell_get_info(env, cells[0], &cell_info), RADICLE_STATUS_OK);
  radicle_free_record_array(cells);
  EXPECT_EQ(cell_info.has_lighting, 1);
  EXPECT_EQ(cell_info.lighting.fog_far, 4096.0f);
  EXPECT_EQ(cell_info.lighting.has_inherits, 1);
  EXPECT_EQ(cell_info.lighting.inherits, 0x14u);
  EXPECT_EQ(cell_info.worldspace.plugin, nullptr);

  radicle_cell_child* children = nullptr;
  ASSERT_EQ(radicle_env_cell_children(env, {"Base.esm", 0x802}, &children, &count), RADICLE_STATUS_OK);
  ASSERT_EQ(count, 3u);
  radicle_record_info info{};
  ASSERT_EQ(radicle_record_get_info(env, children[2].record, &info), RADICLE_STATUS_OK);
  EXPECT_STREQ(info.owner_plugin, "Patch.esp");
  EXPECT_EQ(children[2].persistent, 0);
  radicle_free_cell_children(children);
  ASSERT_EQ(radicle_env_cell_children(env, {"Base.esm", 0x999}, &children, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(children, nullptr);
  EXPECT_EQ(count, 0u);

  const radicle_record* land = nullptr;
  ASSERT_EQ(radicle_env_cell_landscape(env, {"Base.esm", 0x802}, &land), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_record_get_info(env, land, &info), RADICLE_STATUS_OK);
  EXPECT_EQ(info.plugin_index, 1u);
  EXPECT_EQ(radicle_env_cell_landscape(env, {"Base.esm", 0x803}, &land), RADICLE_STATUS_NOT_FOUND);

  EXPECT_EQ(radicle_cell_get_info(env, world, &cell_info), RADICLE_STATUS_PARSE_ERROR);
  radicle_env_destroy(env);
}
