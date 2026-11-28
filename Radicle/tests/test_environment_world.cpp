#include "fo4/c_api_env.h"
#include "fo4/c_api_records.h"
#include "fo4/c_api_cell.h"
#include "fo4/c_api_pet_write.h"
#include "fo4/environment/world.hpp"
#include "fo4/forms/world_edit.hpp"
#include "fo4/plugin/writer.hpp"
#include "env_node_fixture.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"
#include "xcll_fixture.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using namespace fo4::test::envnode;
using fo4::test::xcll_bytes;

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

class EnvironmentBorrow : public WorldEnvironmentTest {};

std::vector<fo4::Plugin> read_stamped(const std::vector<std::filesystem::path>& paths) {
  std::vector<fo4::Plugin> plugins;
  for (const auto& path : paths) plugins.push_back(fo4::read_plugin_file(path));
  for (std::size_t i = 1; i < plugins.size(); ++i) {
    fo4::stamp_master_kinds(plugins[i], std::span<const fo4::Plugin>(plugins).first(i));
  }
  return plugins;
}

Node parented_world(std::uint32_t id, std::uint32_t parent, std::uint8_t flags,
                    std::vector<Sub> subrecords = {}) {
  std::vector<Sub> all{{"WNAM", bytes_of({parent})}, {"PNAM", {std::byte{flags}, std::byte{0}}}};
  all.insert(all.end(), subrecords.begin(), subrecords.end());
  return rec("WRLD", id, std::move(all));
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

TEST_F(WorldEnvironmentTest, ResolvesWaterThroughIndependentInheritanceAndWinningRecords) {
  const auto wrld = [](std::uint32_t id, std::uint32_t parent, std::uint16_t flags,
                       std::uint32_t water, float height) {
    return rec("WRLD", id, {{"WNAM", bytes_of({parent})},
      {"PNAM", {std::byte{static_cast<std::uint8_t>(flags)}, std::byte{0}}},
      {"NAM2", bytes_of({water})}, {"DNAM", floats({-2048, height})}});
  };
  paths_ = {write("Fallout4.esm", make_plugin({}, {
    top("WATR", {rec("WATR", 0x18), rec("WATR", 0x840), rec("WATR", 0x841)}),
    top("WRLD", {wrld(0x800, 0, 0, 0x840, 450), wrld(0x801, 0x800, 8, 0x841, 10),
      wrld(0x802, 0x801, 9, 0x841, 20), wrld(0x803, 0x800, 1, 0x841, 30),
      wrld(0x804, 0x999, 9, 0x841, 40), wrld(0x805, 0, 0, 0x999, 50)})})),
    write("Override.esp", make_plugin({"Fallout4.esm"}, {top("WATR", {rec("WATR", 0x840)})}))};
  const auto env = fo4::env::Environment::load(paths_);
  const auto select = [&](std::uint32_t id) {
    return fo4::env::resolve_water(env, *env.winner(fo4::FormKey{fo4::PluginKey::from_filename("Fallout4.esm"), fo4::LocalFormId{id}}));
  };
  const auto inherited = select(0x802);
  EXPECT_EQ(inherited.water, key("000840:Fallout4.esm"));
  ASSERT_NE(inherited.record, nullptr);
  EXPECT_EQ(inherited.record->plugin, 1u);
  EXPECT_EQ(inherited.height, 10.0f);
  EXPECT_FALSE(inherited.uses_default_water);
  EXPECT_EQ(select(0x803).water, key("000841:Fallout4.esm"));
  EXPECT_EQ(select(0x803).height, 450.0f);
  EXPECT_EQ(select(0x804).height, 40.0f);
  EXPECT_EQ(select(0x804).water, key("000841:Fallout4.esm"));
  EXPECT_TRUE(select(0x805).uses_default_water);
  EXPECT_EQ(select(0x805).water, key("000018:Fallout4.esm"));

  radicle_env* handle = nullptr;
  const auto first = paths_[0].string(), second = paths_[1].string();
  const char* inputs[] = {first.c_str(), second.c_str()};
  ASSERT_EQ(radicle_env_load(inputs, 2, &handle), RADICLE_STATUS_OK);
  const radicle_record* world = nullptr;
  ASSERT_EQ(radicle_env_winner(handle, {"Fallout4.esm", 0x802}, &world), RADICLE_STATUS_OK);
  radicle_water_selection selection{};
  ASSERT_EQ(radicle_water_resolve(handle, world, &selection), RADICLE_STATUS_OK);
  EXPECT_EQ(selection.water.local_id, 0x840u);
  EXPECT_NE(selection.record, nullptr);
  EXPECT_EQ(selection.has_height, 1);
  EXPECT_EQ(selection.height, 10.0f);
  radicle_env_destroy(handle);
}

TEST_F(WorldEnvironmentTest, WaterHeightBoundariesPreserveRawValuesAndRejectActiveCycles) {
  const std::vector<float> raw = {-2147483648.0f, 2147483648.0f,
    std::numeric_limits<float>::max(), 2147483520.0f, 2.5f, -2.5f,
    -3e10f, -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()};
  const std::vector<float> expected = {-2147483648.0f, 450, 450, 2147483520.0f,
    3, -2, -2147483648.0f, -2147483648.0f, -2147483648.0f};
  std::vector<Node> cells;
  for (std::size_t i = 0; i < raw.size(); ++i) {
    cells.push_back(rec("CELL", 0x900u + static_cast<std::uint32_t>(i),
      {{"DATA", {std::byte{2}}}, {"XCLW", floats({raw[i]})}}));
  }
  cells.push_back(rec("CELL", 0x920, {{"DATA", {std::byte{0}}}, {"XCWT", bytes_of({0x840})}}));
  cells.push_back(rec("CELL", 0x921, {{"DATA", {std::byte{3}}}, {"XCWT", bytes_of({0})}}));
  cells.push_back(rec("CELL", 0x922, {{"DATA", {std::byte{2}}}, {"XCWT", bytes_of({0x999})}}));
  cells.push_back(rec("CELL", 0x923, {{"DATA", {std::byte{2}}}, {"XCWT", bytes_of({0x800})}}));
  cells.push_back(rec("CELL", 0x924, {{"DATA", {std::byte{2}}}, {"XCLW", floats({0.5f})}}));
  cells.push_back(rec("CELL", 0x925, {{"DATA", {std::byte{2}}}, {"XCLW", floats({-0.7f})}}));
  paths_ = {write("Fallout4.esm", make_plugin({}, {
    top("WATR", {rec("WATR", 0x840)}),
    top("WRLD", {rec("WRLD", 0x800, {{"NAM2", bytes_of({0x840})}, {"DNAM", floats({-2048, 450})}}),
      grp(0x800, 1, {grp(0, 4, {grp(0, 5, cells)})}),
      rec("WRLD", 0x801, {{"WNAM", bytes_of({0x801})}, {"PNAM", {std::byte{8}, std::byte{0}}}}),
      rec("WRLD", 0x802, {{"PNAM", {std::byte{0}, std::byte{0}}}, {"WNAM", bytes_of({0x801})}}),
      rec("WRLD", 0x803, {{"WNAM", bytes_of({0x803})}, {"PNAM", {std::byte{0}, std::byte{0}}}})})}))};
  const auto env = fo4::env::Environment::load(paths_);
  const auto selected = [&](std::uint32_t id) {
    return env.winner(fo4::FormKey{fo4::PluginKey::from_filename("Fallout4.esm"), fo4::LocalFormId{id}});
  };
  for (std::size_t i = 0; i < raw.size(); ++i) {
    SCOPED_TRACE(i);
    const auto* cell = selected(0x900u + static_cast<std::uint32_t>(i));
    EXPECT_EQ(fo4::env::resolve_water(env, *cell).height, expected[i]);
    if (i + 1 != raw.size()) {
      EXPECT_EQ(fo4::env::decode_cell(env, *cell).water_height, raw[i]);
    }
  }
  EXPECT_FALSE(fo4::env::resolve_water(env, *selected(0x920)).height.has_value());
  EXPECT_EQ(fo4::env::resolve_water(env, *selected(0x920)).water, key("000840:Fallout4.esm"));
  const auto interior = fo4::env::resolve_water(env, *selected(0x921));
  EXPECT_TRUE(interior.uses_default_water);
  EXPECT_EQ(interior.record, nullptr);
  EXPECT_FALSE(interior.height.has_value());
  const auto missing_target = fo4::env::resolve_water(env, *selected(0x922));
  EXPECT_EQ(missing_target.water, key("000840:Fallout4.esm"));
  EXPECT_NE(missing_target.record, nullptr);
  EXPECT_FALSE(missing_target.uses_default_water);
  EXPECT_EQ(missing_target.height, 450.0f);
  const auto non_water_target = fo4::env::resolve_water(env, *selected(0x923));
  EXPECT_EQ(non_water_target.water, key("000840:Fallout4.esm"));
  EXPECT_NE(non_water_target.record, nullptr);
  EXPECT_FALSE(non_water_target.uses_default_water);
  EXPECT_EQ(non_water_target.height, 450.0f);
  EXPECT_THROW(fo4::env::resolve_water(env, *selected(0x801)), fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::resolve_water(env, *selected(0x802)), fo4::env::WorldDecodeError);
  EXPECT_TRUE(fo4::env::resolve_water(env, *selected(0x803)).uses_default_water);
  EXPECT_EQ(fo4::env::resolve_water(env, *selected(0x924)).height, 1.0f);
  EXPECT_EQ(fo4::env::resolve_water(env, *selected(0x925)).height, 0.0f);
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

TEST_F(WorldEnvironmentTest, DecodesCellLightingTailAndExtraSubrecords) {
  fo4::test::ByteWriter lod;
  lod.u32(0xC00);
  lod.f32(1.5f);
  lod.f32(-2.5f);
  lod.f32(3.5f);
  const auto cell = rec("CELL", 0x800,
                        {{"EDID", zstring("Extras")},
                         {"DATA", {std::byte{0x02}, std::byte{0}}},
                         {"VISI", {std::byte{0x34}, std::byte{0x12}}},
                         {"PCMB", {std::byte{0x78}, std::byte{0x56}}},
                         {"XCLR", bytes_of({0x900, 0x901})},
                         {"XPRI", bytes_of({0x910, 0x911})},
                         {"XOWN", bytes_of({0xA00, 0, 0})},
                         {"XRNK", bytes_of({3})},
                         {"XILL", bytes_of({0xB00})},
                         {"XILW", lod.bytes},
                         {"XWCN", bytes_of({2})},
                         {"XWCU", floats({1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f})},
                         {"XWEM", zstring("water/current.dds")},
                         {"XCCM", bytes_of({0xD00})},
                         {"XCAS", bytes_of({0xE00})},
                         {"XCMO", bytes_of({0xF00})},
                         {"XGDR", bytes_of({0x1000})},
                         {"XCLL", xcll_bytes(136)}});
  const auto truncated = rec("CELL", 0x801, {{"XCLL", xcll_bytes(135)}});
  const auto bad_region = rec("CELL", 0x802, {{"XCLR", {std::byte{0}, std::byte{0}}}});
  const auto bad_current = rec("CELL", 0x803, {{"XWCU", floats({1.0f, 2.0f, 3.0f, 4.0f, 5.0f})}});
  const auto bad_stride = rec("CELL", 0x804, {{"XPRI", std::vector<std::byte>(6)}});
  paths_ = {write("Extras.esp", make_plugin({}, {top("CELL", {grp(0, 2, {grp(0, 3,
      {cell, truncated, bad_region, bad_current, bad_stride})})})}))};
  const auto env = fo4::env::Environment::load(paths_);

  const auto data = fo4::env::decode_cell(env, *env.winner(key("000800:Extras.esp")));
  EXPECT_EQ(data.previs_timestamp, std::optional<std::uint16_t>{0x1234});
  EXPECT_EQ(data.precombined_timestamp, std::optional<std::uint16_t>{0x5678});
  ASSERT_EQ(data.regions.size(), 2u);
  EXPECT_EQ(data.regions[0], key("000900:Extras.esp"));
  EXPECT_EQ(data.regions[1], key("000901:Extras.esp"));
  ASSERT_EQ(data.previs_references.size(), 2u);
  EXPECT_EQ(data.previs_references[0], key("000910:Extras.esp"));
  EXPECT_EQ(data.previs_references[1], key("000911:Extras.esp"));
  EXPECT_EQ(data.owner, key("000A00:Extras.esp"));
  EXPECT_EQ(data.owner_rank, std::optional<std::int32_t>{3});
  EXPECT_EQ(data.lock_list, key("000B00:Extras.esp"));
  ASSERT_TRUE(data.exterior_lod.has_value());
  EXPECT_EQ(data.exterior_lod->worldspace, key("000C00:Extras.esp"));
  EXPECT_EQ(data.exterior_lod->offset[0], 1.5f);
  EXPECT_EQ(data.exterior_lod->offset[1], -2.5f);
  EXPECT_EQ(data.exterior_lod->offset[2], 3.5f);
  EXPECT_EQ(data.water_current_count, std::optional<std::uint32_t>{2});
  ASSERT_EQ(data.water_currents.size(), 2u);
  EXPECT_EQ(data.water_currents[0].velocity[2], 3.0f);
  EXPECT_EQ(data.water_currents[0].unnamed, 4.0f);
  EXPECT_EQ(data.water_currents[1].velocity[0], 5.0f);
  EXPECT_EQ(data.water_currents[1].unnamed, 8.0f);
  EXPECT_EQ(data.water_environment_map, std::optional<std::string>{"water/current.dds"});
  EXPECT_EQ(data.sky_weather_region, key("000D00:Extras.esp"));
  EXPECT_EQ(data.acoustic_space, key("000E00:Extras.esp"));
  EXPECT_EQ(data.music_type, key("000F00:Extras.esp"));
  EXPECT_EQ(data.god_rays, key("00001000:Extras.esp"));

  ASSERT_TRUE(data.lighting.has_value());
  const auto& lighting = *data.lighting;
  EXPECT_EQ(lighting.near_height_mid, std::optional<float>{300.0f});
  EXPECT_EQ(lighting.near_height_range, std::optional<float>{400.0f});
  ASSERT_TRUE(lighting.fog_color_high_near.has_value());
  EXPECT_EQ(lighting.fog_color_high_near->red, 0x10);
  EXPECT_EQ(lighting.fog_color_high_near->green, 0x11);
  EXPECT_EQ(lighting.fog_color_high_near->blue, 0x12);
  ASSERT_TRUE(lighting.fog_color_high_far.has_value());
  EXPECT_EQ(lighting.fog_color_high_far->blue, 0x22);
  EXPECT_EQ(lighting.high_density_scale, std::optional<float>{2.0f});
  EXPECT_EQ(lighting.fog_near_scale, std::optional<float>{3.0f});
  EXPECT_EQ(lighting.fog_far_scale, std::optional<float>{4.0f});
  EXPECT_EQ(lighting.fog_high_near_scale, std::optional<float>{5.0f});
  EXPECT_EQ(lighting.fog_high_far_scale, std::optional<float>{6.0f});
  EXPECT_EQ(lighting.far_height_mid, std::optional<float>{500.0f});
  EXPECT_EQ(lighting.far_height_range, std::optional<float>{600.0f});

  const auto short_lighting = fo4::env::decode_cell(env, *env.winner(key("000801:Extras.esp")));
  ASSERT_TRUE(short_lighting.lighting.has_value());
  EXPECT_EQ(short_lighting.lighting->far_height_mid, std::optional<float>{500.0f});
  EXPECT_FALSE(short_lighting.lighting->far_height_range.has_value());
  EXPECT_THROW(fo4::env::decode_cell(env, *env.winner(key("000802:Extras.esp"))),
               fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::decode_cell(env, *env.winner(key("000803:Extras.esp"))),
               fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::decode_cell(env, *env.winner(key("000804:Extras.esp"))),
               fo4::env::WorldDecodeError);
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

TEST_F(WorldEnvironmentTest, StaleWorldspaceHandleResolvesLikeTheWinner) {
  paths_ = {write("Fallout4.esm", make_plugin({}, {
                  top("WATR", {rec("WATR", 0x18)}),
                  top("WRLD", {parented_world(0x800, 0x801, 8), parented_world(0x801, 0x800, 8)})})),
            write("Override.esp", make_plugin({"Fallout4.esm"}, {top("WRLD", {rec("WRLD", 0x800)})}))};
  const auto env = fo4::env::Environment::load(paths_);
  const auto world = key("000800:Fallout4.esm");
  const auto versions = env.versions(world);
  ASSERT_EQ(versions.size(), 2u);
  const auto& stale = versions[0];
  ASSERT_NE(&stale, env.winner(world));

  fo4::env::WaterSelection from_stale;
  EXPECT_NO_THROW(from_stale = fo4::env::resolve_water(env, stale));
  const auto from_winner = fo4::env::resolve_water(env, *env.winner(world));
  EXPECT_EQ(from_stale.water, from_winner.water);
  EXPECT_EQ(from_stale.record, from_winner.record);
  EXPECT_EQ(from_stale.uses_default_water, from_winner.uses_default_water);
  EXPECT_EQ(from_stale.height, from_winner.height);
}

TEST_F(WorldEnvironmentTest, StaleCellHandleResolvesLikeTheWinner) {
  const auto cell_record = [](std::uint32_t water, float height) {
    return rec("CELL", 0x810, {{"DATA", {std::byte{0x02}}}, {"XCWT", bytes_of({water})}, {"XCLW", floats({height})}});
  };
  paths_ = {write("Fallout4.esm", make_plugin({}, {
                  top("WATR", {rec("WATR", 0x850), rec("WATR", 0x851), rec("WATR", 0x852)}),
                  top("WRLD", {rec("WRLD", 0x800, {{"NAM2", bytes_of({0x850})}, {"DNAM", floats({-2048, 450})}}),
                               grp(0x800, 1, {grp(0, 4, {grp(0, 5, {cell_record(0x851, 100.0f)})})})})})),
            write("Override.esp", make_plugin({"Fallout4.esm"}, {
                  top("WRLD", {grp(0x800, 1, {grp(0, 4, {grp(0, 5, {cell_record(0x852, 250.0f)})})})})}))};
  const auto env = fo4::env::Environment::load(paths_);
  const auto cell = key("000810:Fallout4.esm");
  const auto versions = env.versions(cell);
  ASSERT_EQ(versions.size(), 2u);
  const auto& older = versions[0];
  ASSERT_NE(&older, env.winner(cell));

  const auto winning = fo4::env::resolve_water(env, *env.winner(cell));
  EXPECT_EQ(winning.water, key("000852:Fallout4.esm"));
  EXPECT_EQ(winning.height, 250.0f);
  EXPECT_FALSE(winning.uses_default_water);

  const auto stale = fo4::env::resolve_water(env, older);
  EXPECT_EQ(stale.water, winning.water);
  EXPECT_EQ(stale.record, winning.record);
  EXPECT_EQ(stale.uses_default_water, winning.uses_default_water);
  EXPECT_EQ(stale.height, winning.height);
}

TEST_F(WorldEnvironmentTest, XcwtToDeletedWinningWaterFallsBackToWorldspaceWater) {
  paths_ = {write("Fallout4.esm", make_plugin({}, {top("WATR", {rec("WATR", 0x850), rec("WATR", 0x851)}),
                  top("WRLD", {rec("WRLD", 0x800, {{"NAM2", bytes_of({0x850})}, {"DNAM", floats({-2048, 450})}}),
                               grp(0x800, 1, {grp(0, 4, {grp(0, 5, {rec("CELL", 0x810, {{"DATA", {std::byte{0x02}}}, {"XCWT", bytes_of({0x851})}})})})})})})),
            write("Override.esp", make_plugin({"Fallout4.esm"}, {top("WATR", {rec("WATR", 0x851, {}, 0x20)})}))};
  const auto env = fo4::env::Environment::load(paths_);
  const auto selection = fo4::env::resolve_water(env, *env.winner(key("000810:Fallout4.esm")));
  EXPECT_EQ(selection.water, key("000850:Fallout4.esm"));
  ASSERT_NE(selection.record, nullptr);
  EXPECT_EQ(selection.record->record->form_key, key("000850:Fallout4.esm"));
  EXPECT_FALSE(selection.uses_default_water);
}

TEST_F(WorldEnvironmentTest, XcwtToNonWaterRecordFallsBackToWorldspaceWater) {
  paths_ = {write("Fallout4.esm", make_plugin({}, {top("WATR", {rec("WATR", 0x850)}), top("STAT", {rec("STAT", 0x860)}),
                  top("WRLD", {rec("WRLD", 0x800, {{"NAM2", bytes_of({0x850})}, {"DNAM", floats({-2048, 450})}}),
                               grp(0x800, 1, {grp(0, 4, {grp(0, 5, {rec("CELL", 0x810, {{"DATA", {std::byte{0x02}}}, {"XCWT", bytes_of({0x860})}})})})})})}))};
  const auto env = fo4::env::Environment::load(paths_);
  const auto selection = fo4::env::resolve_water(env, *env.winner(key("000810:Fallout4.esm")));
  EXPECT_EQ(selection.water, key("000850:Fallout4.esm"));
  ASSERT_NE(selection.record, nullptr);
  EXPECT_EQ(selection.record->record->form_key, key("000850:Fallout4.esm"));
  EXPECT_FALSE(selection.uses_default_water);
}

TEST_F(WorldEnvironmentTest, XcwtIntoUnloadedMasterFallsBackToWorldspaceWater) {
  paths_ = {write("Fallout4.esm", make_plugin({}, {top("WATR", {rec("WATR", 0x850), rec("WATR", 0x851)}),
                  top("WRLD", {rec("WRLD", 0x800, {{"NAM2", bytes_of({0x850})}, {"DNAM", floats({-2048, 450})}})})})),
            write("Dangling.esp", make_plugin({"Fallout4.esm", "Missing.esm"}, {top("WATR", {rec("WATR", 0x02000851)}), top("WRLD", {grp(0x800, 1, {grp(0, 4, {grp(0, 5, {rec("CELL", 0x02000810, {{"DATA", {std::byte{0x02}}}, {"XCWT", bytes_of({0x01000851})}})})})})})}))};
  const auto env = fo4::env::Environment::load(paths_);
  const auto selection = fo4::env::resolve_water(env, *env.winner(key("000810:Dangling.esp")));
  EXPECT_EQ(selection.water, key("000850:Fallout4.esm"));
  ASSERT_NE(selection.record, nullptr);
  EXPECT_EQ(selection.record->record->form_key, key("000850:Fallout4.esm"));
  EXPECT_FALSE(selection.uses_default_water);
}

TEST_F(WorldEnvironmentTest, CApiInvalidXcwtResolvesToWorldspaceWater) {
  paths_ = {write("Fallout4.esm", make_plugin({}, {top("WATR", {rec("WATR", 0x850)}),
                  top("WRLD", {rec("WRLD", 0x800, {{"NAM2", bytes_of({0x850})}, {"DNAM", floats({-2048, 450})}}),
                               grp(0x800, 1, {grp(0, 4, {grp(0, 5, {rec("CELL", 0x810, {{"DATA", {std::byte{0x02}}}, {"XCWT", bytes_of({0x999})}})})})})})}))};
  radicle_env* handle = nullptr;
  const auto first = paths_[0].string();
  const char* inputs[] = {first.c_str()};
  ASSERT_EQ(radicle_env_load(inputs, 1, &handle), RADICLE_STATUS_OK);
  const radicle_record* cell = nullptr;
  ASSERT_EQ(radicle_env_winner(handle, {"Fallout4.esm", 0x810}, &cell), RADICLE_STATUS_OK);
  radicle_water_selection selection{};
  ASSERT_EQ(radicle_water_resolve(handle, cell, &selection), RADICLE_STATUS_OK);
  EXPECT_STREQ(selection.water.plugin, "Fallout4.esm");
  EXPECT_EQ(selection.water.local_id, 0x850u);
  EXPECT_NE(selection.record, nullptr);
  EXPECT_EQ(selection.uses_default_water, 0);
  radicle_env_destroy(handle);
}

TEST_F(WorldEnvironmentTest, WorldspaceWaterCycleThrowsWhenBothWorldsUseWaterData) {
  paths_ = {write("Fallout4.esm", make_plugin({}, {top("WRLD", {parented_world(0x800, 0x801, 8),
                                                                parented_world(0x801, 0x800, 8)})}))};
  const auto env = fo4::env::Environment::load(paths_);
  EXPECT_THROW(fo4::env::resolve_water(env, *env.winner(key("000800:Fallout4.esm"))),
               fo4::env::WorldDecodeError);
  EXPECT_THROW(fo4::env::resolve_water(env, *env.winner(key("000801:Fallout4.esm"))),
               fo4::env::WorldDecodeError);

  radicle_env* handle = nullptr;
  const auto first = paths_[0].string();
  const char* inputs[] = {first.c_str()};
  ASSERT_EQ(radicle_env_load(inputs, 1, &handle), RADICLE_STATUS_OK);
  const radicle_record* world = nullptr;
  ASSERT_EQ(radicle_env_winner(handle, {"Fallout4.esm", 0x800}, &world), RADICLE_STATUS_OK);
  radicle_water_selection selection{};
  EXPECT_EQ(radicle_water_resolve(handle, world, &selection), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_STREQ(radicle_last_error(), "cyclic worldspace parent inheritance");
  radicle_env_destroy(handle);
}

TEST_F(WorldEnvironmentTest, DeletedWorldspaceParentEndsWaterInheritance) {
  paths_ = {write("Fallout4.esm", make_plugin({}, {
                  top("WATR", {rec("WATR", 0x18), rec("WATR", 0x840), rec("WATR", 0x841)}),
                  top("WRLD", {rec("WRLD", 0x800, {{"NAM2", bytes_of({0x841})}, {"DNAM", floats({-2048, 99})}}),
                               parented_world(0x801, 0x800, 8,
                                              {{"NAM2", bytes_of({0x840})}, {"DNAM", floats({-2048, 450})}})})})),
            write("Override.esp", make_plugin({"Fallout4.esm"}, {top("WRLD", {rec("WRLD", 0x800, {}, 0x20)})}))};
  const auto env = fo4::env::Environment::load(paths_);
  EXPECT_TRUE(env.metadata(*env.winner(key("000800:Fallout4.esm"))).deleted);
  const auto selection = fo4::env::resolve_water(env, *env.winner(key("000801:Fallout4.esm")));
  EXPECT_EQ(selection.water, key("000840:Fallout4.esm"));
  ASSERT_NE(selection.record, nullptr);
  EXPECT_EQ(selection.record->record->form_key, key("000840:Fallout4.esm"));
  EXPECT_FALSE(selection.uses_default_water);
  EXPECT_EQ(selection.height, 450.0f);
}

TEST_F(WorldEnvironmentTest, WnamWithoutPnamInheritsWaterAndHeight) {
  paths_ = {write("Fallout4.esm", make_plugin({}, {
                  top("WATR", {rec("WATR", 0x840)}),
                  top("WRLD", {rec("WRLD", 0x800, {{"NAM2", bytes_of({0x840})}, {"DNAM", floats({-2048, 450})}}),
                               rec("WRLD", 0x801, {{"WNAM", bytes_of({0x800})}})})}))};
  const auto env = fo4::env::Environment::load(paths_);
  const auto selection = fo4::env::resolve_water(env, *env.winner(key("000801:Fallout4.esm")));
  EXPECT_EQ(selection.water, key("000840:Fallout4.esm"));
  ASSERT_NE(selection.record, nullptr);
  EXPECT_FALSE(selection.uses_default_water);
  EXPECT_EQ(selection.height, 450.0f);
}

TEST_F(WorldEnvironmentTest, WorldspaceWithoutDnamResolvesZeroWaterHeight) {
  paths_ = {write("Fallout4.esm", make_plugin({}, {top("WRLD", {rec("WRLD", 0x800)})}))};
  const auto env = fo4::env::Environment::load(paths_);
  const auto selection = fo4::env::resolve_water(env, *env.winner(key("000800:Fallout4.esm")));
  ASSERT_TRUE(selection.height.has_value());
  EXPECT_EQ(*selection.height, 0.0f);

  radicle_env* handle = nullptr;
  const auto first = paths_[0].string();
  const char* inputs[] = {first.c_str()};
  ASSERT_EQ(radicle_env_load(inputs, 1, &handle), RADICLE_STATUS_OK);
  const radicle_record* world = nullptr;
  ASSERT_EQ(radicle_env_winner(handle, {"Fallout4.esm", 0x800}, &world), RADICLE_STATUS_OK);
  radicle_water_selection c_selection{};
  ASSERT_EQ(radicle_water_resolve(handle, world, &c_selection), RADICLE_STATUS_OK);
  EXPECT_EQ(c_selection.has_height, 1);
  EXPECT_EQ(c_selection.height, 0.0f);
  radicle_env_destroy(handle);
}

TEST_F(EnvironmentBorrow, WinnersAndVersionsMatchFromPlugins) {
  const auto owned = fo4::env::Environment::load(paths_);
  const auto plugins = read_stamped(paths_);
  const std::vector<const fo4::Plugin*> ordered{&plugins[0], &plugins[1]};
  const auto borrowed = fo4::env::Environment::borrow(ordered);

  ASSERT_EQ(borrowed.plugins().size(), owned.plugins().size());
  for (std::size_t i = 0; i < owned.plugins().size(); ++i) {
    EXPECT_EQ(borrowed.plugins()[i].key.filename(), owned.plugins()[i].key.filename());
  }
  for (const char* id : {"000802:Base.esm", "000811:Base.esm", "000812:Base.esm", "000813:Base.esm", "000900:Patch.esp"}) {
    const auto form_key = key(id);
    const auto* owned_win = owned.winner(form_key);
    const auto* borrowed_win = borrowed.winner(form_key);
    ASSERT_NE(owned_win, nullptr);
    ASSERT_NE(borrowed_win, nullptr);
    EXPECT_EQ(borrowed_win->plugin, owned_win->plugin);
    EXPECT_EQ(borrowed_win->record->form_key, owned_win->record->form_key);
    EXPECT_EQ(borrowed.versions(form_key).size(), owned.versions(form_key).size());
    EXPECT_EQ(borrowed.metadata(*borrowed_win).editor_id, owned.metadata(*owned_win).editor_id);
  }

  const auto cell = key("000802:Base.esm");
  const auto owned_children = owned.cell_children(cell);
  const auto borrowed_children = borrowed.cell_children(cell);
  ASSERT_EQ(borrowed_children.size(), 3u);
  ASSERT_EQ(borrowed_children.size(), owned_children.size());
  for (std::size_t i = 0; i < owned_children.size(); ++i) {
    EXPECT_EQ(borrowed_children[i].ref->record->form_key, owned_children[i].ref->record->form_key);
    EXPECT_EQ(borrowed_children[i].ref->plugin, owned_children[i].ref->plugin);
    EXPECT_EQ(borrowed_children[i].persistent, owned_children[i].persistent);
  }

  const auto owned_cells = owned.winners_of(fo4::Signature{'C', 'E', 'L', 'L'});
  const auto borrowed_cells = borrowed.winners_of(fo4::Signature{'C', 'E', 'L', 'L'});
  ASSERT_EQ(borrowed_cells.size(), owned_cells.size());
  for (std::size_t i = 0; i < owned_cells.size(); ++i) {
    EXPECT_EQ(borrowed_cells[i]->record->form_key, owned_cells[i]->record->form_key);
  }

  const auto* owned_edid = owned.find_by_editor_id("exterior12", fo4::Signature{'C', 'E', 'L', 'L'});
  const auto* borrowed_edid = borrowed.find_by_editor_id("exterior12", fo4::Signature{'C', 'E', 'L', 'L'});
  ASSERT_NE(owned_edid, nullptr);
  ASSERT_NE(borrowed_edid, nullptr);
  EXPECT_EQ(borrowed_edid->record->form_key, owned_edid->record->form_key);
}

TEST_F(EnvironmentBorrow, DoesNotCopyPlugins) {
  const auto plugins = read_stamped(paths_);
  const std::vector<const fo4::Plugin*> ordered{&plugins[0], &plugins[1]};
  const auto borrowed = fo4::env::Environment::borrow(ordered);

  EXPECT_EQ(&borrowed.plugin(0), &plugins[0]);
  EXPECT_EQ(&borrowed.plugin(1), &plugins[1]);

  const auto base_only = key("000810:Base.esm");
  const auto* base_win = borrowed.winner(base_only);
  ASSERT_NE(base_win, nullptr);
  EXPECT_EQ(base_win->record, fo4::find_record(plugins[0], base_only));

  const auto overridden = key("000812:Base.esm");
  const auto* patch_win = borrowed.winner(overridden);
  ASSERT_NE(patch_win, nullptr);
  EXPECT_EQ(patch_win->record, fo4::find_record(plugins[1], overridden));
}

TEST_F(EnvironmentBorrow, RejectsNullAndDuplicatePlugins) {
  const auto plugins = read_stamped(paths_);
  const auto reread_base = fo4::read_plugin_file(paths_[0]);
  const fo4::Plugin* with_null[] = {&plugins[0], nullptr};
  const fo4::Plugin* repeated[] = {&plugins[0], &plugins[0]};
  const fo4::Plugin* same_key[] = {&plugins[0], &reread_base};
  EXPECT_THROW(fo4::env::Environment::borrow(with_null), fo4::env::EnvironmentError);
  EXPECT_THROW(fo4::env::Environment::borrow(repeated), fo4::env::EnvironmentError);
  EXPECT_THROW(fo4::env::Environment::borrow(same_key), fo4::env::EnvironmentError);

  const fo4::Plugin* ordered[] = {&plugins[0], &plugins[1]};
  EXPECT_NO_THROW(fo4::env::Environment::borrow(ordered));
}

namespace {

std::vector<std::byte> world_edit_bytes(std::initializer_list<float> values) {
  std::vector<std::byte> out;
  for (const auto value : values) {
    const auto* bytes = reinterpret_cast<const std::byte*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(value));
  }
  return out;
}

std::vector<std::byte> world_edit_id(std::uint32_t value) {
  std::vector<std::byte> out(4);
  std::memcpy(out.data(), &value, sizeof(value));
  return out;
}

std::vector<std::byte> world_edit_base() {
  const auto world = rec("WRLD", 0x800, {{"EDID", zstring("TestWorld")},
                                         {"CNAM", world_edit_id(0x900)},
                                         {"DNAM", world_edit_bytes({10.0f, 20.0f})},
                                         {"DATA", {std::byte{0x01}}}});
  const auto second = rec("WRLD", 0x902, {{"EDID", zstring("OtherWorld")}});
  const auto climate = rec("CLMT", 0x900, {{"EDID", zstring("TestClimate")}});
  const auto water = rec("WATR", 0x901, {{"EDID", zstring("TestWater")}});
  return make_plugin({}, {top("WRLD", {world, second}), top("CLMT", {climate}), top("WATR", {water})});
}

std::vector<std::byte> world_edit_patch() { return make_plugin({"Base.esm"}, {}); }

const fo4::Subrecord* world_subrecord(const fo4::Record& record, const char* signature) {
  const auto wanted = fo4::Signature::from_string(signature);
  for (const auto& sub : record.subrecords) {
    if (sub.signature == wanted) return &sub;
  }
  return nullptr;
}

}  // namespace

TEST(WorldEdit, AnEditedMasterWorldspaceIsCopiedIntoThePatchWithTheNewValues) {
  const auto base = fo4::read_plugin(world_edit_base(), "Base.esm");
  auto patch = fo4::read_plugin(world_edit_patch(), "Patch.esp");
  const fo4::Plugin* order[] = {&base};
  const auto env = fo4::env::Environment::borrow(order);
  const auto world = fo4::FormKey::parse("000800:Base.esm");

  fo4::forms::WorldEdit edit;
  edit.flags = 0x03;
  edit.default_land_height = 5.0f;
  edit.default_water_height = 6.0f;
  edit.water = std::optional<fo4::FormKey>(fo4::FormKey::parse("000901:Base.esm"));
  const auto result = fo4::forms::edit_world(patch, env, world, edit);

  EXPECT_EQ(result.status, fo4::forms::WorldEditStatus::edited);
  EXPECT_EQ(result.copied_from, "Base.esm");
  const auto* record = fo4::find_record(patch, world);
  ASSERT_NE(record, nullptr);
  ASSERT_EQ(record->header.signature, fo4::Signature::from_string("WRLD"));
  const auto* flags = world_subrecord(*record, "DATA");
  ASSERT_NE(flags, nullptr);
  ASSERT_EQ(flags->data.size(), 1u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(flags->data[0]), 0x03);
  const auto* heights = world_subrecord(*record, "DNAM");
  ASSERT_NE(heights, nullptr);
  ASSERT_EQ(heights->data.size(), 8u);
  float land = 0.0f;
  float water_height = 0.0f;
  std::memcpy(&land, heights->data.data(), sizeof(land));
  std::memcpy(&water_height, heights->data.data() + 4, sizeof(water_height));
  EXPECT_FLOAT_EQ(land, 5.0f);
  EXPECT_FLOAT_EQ(water_height, 6.0f);
  const auto* water = world_subrecord(*record, "NAM2");
  ASSERT_NE(water, nullptr);
  ASSERT_EQ(water->data.size(), 4u);
  EXPECT_EQ(world_subrecord(*record, "WNAM"), nullptr);
}

TEST(WorldEdit, ARefusedWorldspaceEditLeavesThePatchBytesUnchanged) {
  const auto base = fo4::read_plugin(world_edit_base(), "Base.esm");
  const auto original = fo4::read_plugin(world_edit_patch(), "Patch.esp");
  auto patch = original;
  const fo4::Plugin* order[] = {&base};
  const auto env = fo4::env::Environment::borrow(order);

  fo4::forms::WorldEdit edit;
  edit.flags = 0x02;
  EXPECT_EQ(fo4::forms::edit_world(patch, env, fo4::FormKey::parse("000999:Base.esm"), edit).status,
            fo4::forms::WorldEditStatus::world_missing);
  EXPECT_EQ(fo4::forms::edit_world(patch, env, fo4::FormKey::parse("000900:Base.esm"), edit).status,
            fo4::forms::WorldEditStatus::not_a_world);

  fo4::forms::WorldEdit missing_target;
  missing_target.climate = std::optional<fo4::FormKey>(fo4::FormKey::parse("000999:Base.esm"));
  EXPECT_EQ(fo4::forms::edit_world(patch, env, fo4::FormKey::parse("000800:Base.esm"), missing_target).status,
            fo4::forms::WorldEditStatus::target_missing);

  fo4::forms::WorldEdit wrong_type;
  wrong_type.water = std::optional<fo4::FormKey>(fo4::FormKey::parse("000900:Base.esm"));
  EXPECT_EQ(fo4::forms::edit_world(patch, env, fo4::FormKey::parse("000800:Base.esm"), wrong_type).status,
            fo4::forms::WorldEditStatus::target_type_not_allowed);

  fo4::forms::WorldEdit not_master;
  not_master.climate = std::optional<fo4::FormKey>(fo4::FormKey::parse("000900:Other.esm"));
  EXPECT_EQ(fo4::forms::edit_world(patch, env, fo4::FormKey::parse("000800:Base.esm"), not_master).status,
            fo4::forms::WorldEditStatus::target_not_master);

  fo4::forms::WorldEdit not_finite;
  not_finite.default_land_height = std::numeric_limits<float>::infinity();
  EXPECT_EQ(fo4::forms::edit_world(patch, env, fo4::FormKey::parse("000800:Base.esm"), not_finite).status,
            fo4::forms::WorldEditStatus::invalid_value);

  EXPECT_EQ(fo4::find_record(patch, fo4::FormKey::parse("000800:Base.esm")), nullptr);
  EXPECT_EQ(fo4::write_plugin(patch), fo4::write_plugin(original));
}

// Detects: parent flags edited alone, which must keep the WNAM link instead of erasing it and refusing.
TEST(WorldEdit, ParentFlagsAloneKeepTheParentLink) {
  const auto child = rec("WRLD", 0x800, {{"EDID", zstring("Child")},
                                         {"WNAM", world_edit_id(0x902)},
                                         {"PNAM", {std::byte{0x01}, std::byte{0}}}});
  const auto base_bytes = make_plugin({}, {top("WRLD", {child, rec("WRLD", 0x902, {{"EDID", zstring("Parent")}})})});
  const auto base = fo4::read_plugin(base_bytes, "Base.esm");
  const fo4::Plugin* order[] = {&base};
  const auto env = fo4::env::Environment::borrow(order);

  auto patch = fo4::read_plugin(world_edit_patch(), "Patch.esp");
  fo4::forms::WorldEdit edit;
  edit.parent_flags = 0x0F;
  ASSERT_EQ(fo4::forms::edit_world(patch, env, fo4::FormKey::parse("000800:Base.esm"), edit).status,
            fo4::forms::WorldEditStatus::edited);
  const auto* record = fo4::find_record(patch, fo4::FormKey::parse("000800:Base.esm"));
  ASSERT_NE(record, nullptr);
  const auto* parent = world_subrecord(*record, "WNAM");
  ASSERT_NE(parent, nullptr);
  EXPECT_EQ(parent->data, world_edit_id(0x902));
  const auto* flags = world_subrecord(*record, "PNAM");
  ASSERT_NE(flags, nullptr);
  ASSERT_EQ(flags->data.size(), 2u);
  EXPECT_EQ(std::to_integer<std::uint8_t>(flags->data[0]), 0x0F);

  auto without_parent = fo4::read_plugin(world_edit_patch(), "Patch.esp");
  const auto before = fo4::write_plugin(without_parent);
  EXPECT_EQ(fo4::forms::edit_world(without_parent, env, fo4::FormKey::parse("000902:Base.esm"), edit).status,
            fo4::forms::WorldEditStatus::invalid_value);
  EXPECT_EQ(fo4::write_plugin(without_parent), before);
}

// Detects: NaN or infinite bounds, which must be refused like every other scalar instead of reaching NAM0 and NAM9.
TEST(WorldEdit, NonFiniteBoundsAreRefused) {
  const auto base = fo4::read_plugin(world_edit_base(), "Base.esm");
  const fo4::Plugin* order[] = {&base};
  const auto env = fo4::env::Environment::borrow(order);
  const auto world = fo4::FormKey::parse("000800:Base.esm");
  const auto nan = std::numeric_limits<float>::quiet_NaN();
  const auto inf = std::numeric_limits<float>::infinity();

  auto patch = fo4::read_plugin(world_edit_patch(), "Patch.esp");
  const auto before = fo4::write_plugin(patch);
  fo4::forms::WorldEdit min_edit;
  min_edit.bounds_min = std::array<float, 2>{-4096.0f, nan};
  EXPECT_EQ(fo4::forms::edit_world(patch, env, world, min_edit).status, fo4::forms::WorldEditStatus::invalid_value);
  fo4::forms::WorldEdit max_edit;
  max_edit.bounds_max = std::array<float, 2>{inf, 4096.0f};
  EXPECT_EQ(fo4::forms::edit_world(patch, env, world, max_edit).status, fo4::forms::WorldEditStatus::invalid_value);
  EXPECT_EQ(fo4::write_plugin(patch), before);

  fo4::forms::WorldEdit finite;
  finite.bounds_min = std::array<float, 2>{-4096.0f, -8192.0f};
  EXPECT_EQ(fo4::forms::edit_world(patch, env, world, finite).status, fo4::forms::WorldEditStatus::edited);
}

// Detects: a WRLD already in the patch that is deleted or undecodable, which must be refused like the copy path does.
TEST(WorldEdit, AWorldAlreadyInThePatchIsCheckedLikeACopiedOne) {
  const auto base = fo4::read_plugin(world_edit_base(), "Base.esm");
  const fo4::Plugin* order[] = {&base};
  const auto env = fo4::env::Environment::borrow(order);
  const auto world = fo4::FormKey::parse("000800:Base.esm");
  fo4::forms::WorldEdit edit;
  edit.flags = 0x02;

  const auto deleted_bytes = make_plugin({"Base.esm"}, {top("WRLD", {rec("WRLD", 0x800, {}, 0x20)})});
  auto deleted = fo4::read_plugin(deleted_bytes, "Patch.esp");
  const auto deleted_before = fo4::write_plugin(deleted);
  EXPECT_EQ(fo4::forms::edit_world(deleted, env, world, edit).status, fo4::forms::WorldEditStatus::world_deleted);
  EXPECT_EQ(fo4::write_plugin(deleted), deleted_before);

  const auto live_bytes = make_plugin({"Base.esm"}, {top("WRLD", {rec("WRLD", 0x800, {{"EDID", zstring("TestWorld")}})})});
  auto undecodable = fo4::read_plugin(live_bytes, "Patch.esp");
  std::get<fo4::Record>(undecodable.groups.front().children.front()).undecodable = true;
  EXPECT_EQ(fo4::forms::edit_world(undecodable, env, world, edit).status, fo4::forms::WorldEditStatus::malformed);
}

namespace {

std::vector<std::byte> world_order_base(bool with_unranked) {
  std::vector<Sub> subs{{"EDID", zstring("OrderWorld")}, {"RNAM", bytes_of({0, 0})}, {"RNAM", bytes_of({0, 0})},
                        {"MHDT", world_edit_id(3)},      {"FULL", world_edit_id(4)}, {"WCTR", world_edit_id(5)},
                        {"XLCN", world_edit_id(6)},      {"MNAM", world_edit_id(7)}, {"ONAM", world_edit_id(8)},
                        {"NAMA", world_edit_id(9)},      {"ZNAM", world_edit_id(10)}, {"XWEM", world_edit_id(11)},
                        {"WLEV", world_edit_id(12)},     {"WLEV", world_edit_id(13)}, {"OFST", world_edit_id(14)},
                        {"CLSZ", world_edit_id(15)}};
  if (with_unranked) subs.insert(subs.begin() + 7, Sub{"QQQQ", world_edit_id(16)});
  return make_plugin({}, {top("WRLD", {rec("WRLD", 0x800, std::move(subs)),
                                       rec("WRLD", 0x902, {{"EDID", zstring("Parent")}})}),
                          top("CLMT", {rec("CLMT", 0x900, {})}), top("WATR", {rec("WATR", 0x901, {})}),
                          top("LGTM", {rec("LGTM", 0x903, {})})});
}

std::vector<std::string> world_signatures(const fo4::Plugin& patch) {
  std::vector<std::string> out;
  const auto* record = fo4::find_record(patch, fo4::FormKey::parse("000800:Base.esm"));
  if (record == nullptr) return out;
  for (const auto& sub : record->subrecords) out.push_back(sub.signature.to_string());
  return out;
}

fo4::forms::WorldEdit every_world_property() {
  fo4::forms::WorldEdit edit;
  edit.parent = std::optional<fo4::FormKey>(fo4::FormKey::parse("000902:Base.esm"));
  edit.parent_flags = 0x01;
  edit.climate = std::optional<fo4::FormKey>(fo4::FormKey::parse("000900:Base.esm"));
  edit.water = std::optional<fo4::FormKey>(fo4::FormKey::parse("000901:Base.esm"));
  edit.lighting_template = std::optional<fo4::FormKey>(fo4::FormKey::parse("000903:Base.esm"));
  edit.default_land_height = 1.0f;
  edit.bounds_min = std::array<float, 2>{-1.0f, -2.0f};
  edit.bounds_max = std::array<float, 2>{1.0f, 2.0f};
  edit.flags = 0x01;
  edit.lod_water = std::optional<fo4::FormKey>(fo4::FormKey::parse("000901:Base.esm"));
  edit.lod_water_height = 3.0f;
  return edit;
}

}  // namespace

// Detects: a guessed order table that sends a new subrecord ahead of fields the CK writes before it.
TEST(WorldEdit, EveryEditablePropertyLandsInTheVanillaWorldspaceOrder) {
  const auto base = fo4::read_plugin(world_order_base(false), "Base.esm");
  const fo4::Plugin* order[] = {&base};
  const auto env = fo4::env::Environment::borrow(order);
  const auto world = fo4::FormKey::parse("000800:Base.esm");
  auto patch = fo4::read_plugin(world_edit_patch(), "Patch.esp");

  ASSERT_EQ(fo4::forms::edit_world(patch, env, world, every_world_property()).status,
            fo4::forms::WorldEditStatus::edited);
  const std::vector<std::string> expected{
      "EDID", "RNAM", "RNAM", "MHDT", "FULL", "WCTR", "LTMP", "XLCN", "WNAM", "PNAM", "CNAM",
      "NAM2", "NAM3", "NAM4", "DNAM", "MNAM", "ONAM", "NAMA", "DATA", "NAM0", "NAM9", "ZNAM",
      "XWEM", "WLEV", "WLEV", "OFST", "CLSZ"};
  EXPECT_EQ(world_signatures(patch), expected);
}

// Detects: an insertion rule that only works when every other property is edited in the same call.
TEST(WorldEdit, ASinglePropertyAddedToAVanillaWorldspaceLandsAfterItsLowerRankFields) {
  const auto base = fo4::read_plugin(world_order_base(false), "Base.esm");
  const fo4::Plugin* order[] = {&base};
  const auto env = fo4::env::Environment::borrow(order);
  const auto world = fo4::FormKey::parse("000800:Base.esm");
  const auto full = every_world_property();

  struct Case {
    const char* name;
    fo4::forms::WorldEdit edit;
    std::vector<std::string> added;
  };
  std::vector<Case> cases(11);
  cases[0] = {"DATA", {}, {"DATA"}};
  cases[0].edit.flags = full.flags;
  cases[1] = {"CNAM", {}, {"CNAM"}};
  cases[1].edit.climate = full.climate;
  cases[2] = {"NAM2", {}, {"NAM2"}};
  cases[2].edit.water = full.water;
  cases[3] = {"LTMP", {}, {"LTMP"}};
  cases[3].edit.lighting_template = full.lighting_template;
  cases[4] = {"DNAM", {}, {"DNAM"}};
  cases[4].edit.default_land_height = full.default_land_height;
  cases[5] = {"NAM0", {}, {"NAM0"}};
  cases[5].edit.bounds_min = full.bounds_min;
  cases[6] = {"NAM9", {}, {"NAM9"}};
  cases[6].edit.bounds_max = full.bounds_max;
  cases[7] = {"NAM3", {}, {"NAM3"}};
  cases[7].edit.lod_water = full.lod_water;
  cases[8] = {"NAM4", {}, {"NAM4"}};
  cases[8].edit.lod_water_height = full.lod_water_height;
  cases[9] = {"WNAM", {}, {"WNAM"}};
  cases[9].edit.parent = full.parent;
  cases[10] = {"WNAM+PNAM", {}, {"WNAM", "PNAM"}};
  cases[10].edit.parent = full.parent;
  cases[10].edit.parent_flags = full.parent_flags;

  const std::vector<std::string> vanilla{"EDID", "RNAM", "RNAM", "MHDT", "FULL", "WCTR", "XLCN", "MNAM",
                                         "ONAM", "NAMA", "ZNAM", "XWEM", "WLEV", "WLEV", "OFST", "CLSZ"};
  const std::vector<std::string> ranked{
      "EDID", "RNAM", "MHDT", "FULL", "WCTR", "LTMP", "XEZN", "XLCN", "WNAM", "PNAM", "CNAM",
      "NAM2", "NAM3", "NAM4", "DNAM", "ICON", "MNAM", "ONAM", "NAMA", "DATA", "NAM0", "NAM9",
      "ZNAM", "XWEM", "WLEV", "OFST", "CLSZ"};
  const auto rank = [&](const std::string& signature) {
    return std::find(ranked.begin(), ranked.end(), signature) - ranked.begin();
  };
  for (const auto& item : cases) {
    auto patch = fo4::read_plugin(world_edit_patch(), "Patch.esp");
    ASSERT_EQ(fo4::forms::edit_world(patch, env, world, item.edit).status, fo4::forms::WorldEditStatus::edited)
        << item.name;
    auto expected = vanilla;
    expected.insert(expected.end(), item.added.begin(), item.added.end());
    std::stable_sort(expected.begin(), expected.end(),
                     [&](const std::string& a, const std::string& b) { return rank(a) < rank(b); });
    EXPECT_EQ(world_signatures(patch), expected) << item.name;
  }
}

// Detects: a subrecord outside the order table that drags a new field to the front of the record.
TEST(WorldEdit, AnUnrankedSubrecordDoesNotMoveANewProperty) {
  const auto base = fo4::read_plugin(world_order_base(true), "Base.esm");
  const fo4::Plugin* order[] = {&base};
  const auto env = fo4::env::Environment::borrow(order);
  auto patch = fo4::read_plugin(world_edit_patch(), "Patch.esp");

  fo4::forms::WorldEdit edit;
  edit.default_land_height = 1.0f;
  ASSERT_EQ(fo4::forms::edit_world(patch, env, fo4::FormKey::parse("000800:Base.esm"), edit).status,
            fo4::forms::WorldEditStatus::edited);
  const std::vector<std::string> expected{"EDID", "RNAM", "RNAM", "MHDT", "FULL", "WCTR", "XLCN", "DNAM",
                                          "QQQQ", "MNAM", "ONAM", "NAMA", "ZNAM", "XWEM", "WLEV", "WLEV",
                                          "OFST", "CLSZ"};
  EXPECT_EQ(world_signatures(patch), expected);
}

TEST_F(WorldEnvironmentTest, ResolvesClimateThroughUseClimateDataAndWinningRecords) {
  const auto wrld = [](std::uint32_t id, std::uint32_t parent, std::uint8_t flags, std::uint32_t climate) {
    std::vector<Sub> subs;
    if (parent != 0) {
      subs.push_back({"WNAM", bytes_of({parent})});
      subs.push_back({"PNAM", {std::byte{flags}, std::byte{0}}});
    }
    if (climate != 0) subs.push_back({"CNAM", bytes_of({climate})});
    return rec("WRLD", id, std::move(subs));
  };
  paths_ = {write("Fallout4.esm", make_plugin({}, {
    top("CLMT", {rec("CLMT", 0x840), rec("CLMT", 0x841), rec("CLMT", 0x842), rec("CLMT", 0x843)}),
    top("WTHR", {rec("WTHR", 0x850)}),
    top("WRLD", {wrld(0x800, 0, 0, 0x840), wrld(0x801, 0x800, 0x10, 0x841), wrld(0x802, 0x800, 0x08, 0x841),
      wrld(0x803, 0x801, 0x10, 0), wrld(0x804, 0, 0, 0), wrld(0x805, 0, 0, 0x850), wrld(0x806, 0, 0, 0x999),
      wrld(0x807, 0x999, 0x10, 0x841), wrld(0x808, 0x80B, 0x10, 0x841), wrld(0x809, 0, 0, 0x843),
      rec("WRLD", 0x80B, {}, 0x20)})})),
    write("Override.esp", make_plugin({"Fallout4.esm"}, {
      top("CLMT", {rec("CLMT", 0x840), rec("CLMT", 0x843, {}, 0x20)}),
      top("WRLD", {wrld(0x804, 0, 0, 0x842)})}))};
  const auto env = fo4::env::Environment::load(paths_);
  const auto climate_of = [&](std::uint32_t id) {
    return fo4::env::resolve_climate(
        env, env.versions(fo4::FormKey{fo4::PluginKey::from_filename("Fallout4.esm"), fo4::LocalFormId{id}})[0]);
  };
  const auto* own = climate_of(0x800);
  ASSERT_NE(own, nullptr);
  EXPECT_EQ(own->record->form_key, key("000840:Fallout4.esm"));
  EXPECT_EQ(own->plugin, 1u);
  EXPECT_EQ(climate_of(0x801)->record->form_key, key("000840:Fallout4.esm"));
  EXPECT_EQ(climate_of(0x802)->record->form_key, key("000841:Fallout4.esm"));
  EXPECT_EQ(climate_of(0x803)->record->form_key, key("000840:Fallout4.esm"));
  ASSERT_NE(climate_of(0x804), nullptr);
  EXPECT_EQ(climate_of(0x804)->record->form_key, key("000842:Fallout4.esm"));
  EXPECT_EQ(climate_of(0x805), nullptr);
  EXPECT_EQ(climate_of(0x806), nullptr);
  EXPECT_EQ(climate_of(0x807)->record->form_key, key("000841:Fallout4.esm"));
  EXPECT_EQ(climate_of(0x808)->record->form_key, key("000841:Fallout4.esm"));
  EXPECT_EQ(climate_of(0x809), nullptr);
  EXPECT_EQ(climate_of(0x80B), nullptr);
  EXPECT_THROW(fo4::env::resolve_climate(env, *env.winner(key("000840:Fallout4.esm"))), fo4::env::WorldDecodeError);

  radicle_env* handle = nullptr;
  const auto first = paths_[0].string(), second = paths_[1].string();
  const char* inputs[] = {first.c_str(), second.c_str()};
  ASSERT_EQ(radicle_env_load(inputs, 2, &handle), RADICLE_STATUS_OK);
  const auto winner = [&](std::uint32_t id) {
    const radicle_record* record = nullptr;
    EXPECT_EQ(radicle_env_winner(handle, {"Fallout4.esm", id}, &record), RADICLE_STATUS_OK);
    return record;
  };
  const radicle_record* resolved = nullptr;
  ASSERT_EQ(radicle_worldspace_resolve_climate(handle, winner(0x803), &resolved), RADICLE_STATUS_OK);
  ASSERT_NE(resolved, nullptr);
  radicle_record_info info{};
  ASSERT_EQ(radicle_record_get_info(handle, resolved, &info), RADICLE_STATUS_OK);
  EXPECT_STREQ(info.signature, "CLMT");
  EXPECT_EQ(info.local_id, 0x840u);
  EXPECT_EQ(info.plugin_index, 1u);
  resolved = winner(0x800);
  ASSERT_EQ(radicle_worldspace_resolve_climate(handle, winner(0x805), &resolved), RADICLE_STATUS_OK);
  EXPECT_EQ(resolved, nullptr);
  EXPECT_EQ(radicle_worldspace_resolve_climate(handle, winner(0x840), &resolved), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(radicle_worldspace_resolve_climate(handle, nullptr, &resolved), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_worldspace_resolve_climate(handle, winner(0x800), nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_env_destroy(handle);
}

TEST_F(WorldEnvironmentTest, ClimateCycleThrowsWhenBothWorldsUseClimateData) {
  paths_ = {write("Fallout4.esm", make_plugin({}, {top("WRLD", {parented_world(0x800, 0x801, 0x10),
                                                                parented_world(0x801, 0x800, 0x10)})}))};
  const auto env = fo4::env::Environment::load(paths_);
  EXPECT_THROW(fo4::env::resolve_climate(env, *env.winner(key("000800:Fallout4.esm"))),
               fo4::env::WorldDecodeError);
}

static fo4::test::envnode::Node map_marker(std::uint32_t id, std::string_view name) {
  return rec("REFR", id,
             {{"NAME", bytes_of({0x820})}, {"XMRK", {}}, {"FNAM", {std::byte{0x03}}}, {"FULL", zstring(name)},
              {"TNAM", {std::byte{12}, std::byte{0}}}});
}

static std::vector<std::string> marker_keys(const radicle_env* env, radicle_form_key world, radicle_status expected = RADICLE_STATUS_OK) {
  const radicle_record** records = nullptr;
  size_t count = 99;
  EXPECT_EQ(radicle_env_worldspace_map_markers(env, world, &records, &count), expected);
  std::vector<std::string> keys;
  for (size_t i = 0; i < count; ++i) {
    radicle_record_info info{};
    EXPECT_EQ(radicle_record_get_info(env, records[i], &info), RADICLE_STATUS_OK);
    EXPECT_STREQ(info.signature, "REFR");
    keys.push_back(std::string(info.owner_plugin) + ":" + std::to_string(info.local_id));
  }
  radicle_free_record_array(records);
  return keys;
}

// Constraint: detects a worldspace marker scan that reads only the persistent cell, keeps a deleted or XMRK-less override, mixes worldspaces, or lists plain references.
TEST_F(WorldEnvironmentTest, WorldspaceMapMarkersCoverPersistentAndTemporaryCellsAndOverrides) {
  const auto cell_children = [](std::vector<fo4::test::envnode::Node> persistent) {
    return grp(0x801, 6, {grp(0x801, 8, std::move(persistent))});
  };
  const auto base = write(
      "Map.esm",
      make_plugin(
          {},
          {top("WRLD",
               {rec("WRLD", 0x800),
                grp(0x800, 1,
                    {rec("CELL", 0x801, {{"DATA", {std::byte{0x02}, std::byte{0}}}}, 0x400),
                     cell_children({map_marker(0x810, "Persistent"), rec("REFR", 0x815, {{"NAME", bytes_of({0x820})}})}),
                     grp(0, 4,
                         {grp(0, 5,
                              {rec("CELL", 0x802, {{"DATA", {std::byte{0x02}}}, {"XCLC", bytes_of({1, 2})}}),
                               grp(0x802, 6,
                                   {grp(0x802, 9,
                                        {map_marker(0x811, "Deleted"), map_marker(0x812, "Dropped"),
                                         map_marker(0x814, "Temporary"),
                                         rec("REFR", 0x813, {{"NAME", bytes_of({0x820})}})})})})})}),
                rec("WRLD", 0x850),
                grp(0x850, 1,
                    {rec("CELL", 0x851, {{"DATA", {std::byte{0x02}, std::byte{0}}}}, 0x400),
                     grp(0x851, 6, {grp(0x851, 8, {map_marker(0x852, "Other")})})})})}));
  const auto patch = write(
      "MapPatch.esp",
      make_plugin({"Map.esm"},
                  {top("WRLD",
                       {rec("WRLD", 0x800),
                        grp(0x800, 1,
                            {grp(0, 4,
                                 {grp(0, 5,
                                      {rec("CELL", 0x802, {{"DATA", {std::byte{0x02}}}, {"XCLC", bytes_of({1, 2})}}),
                                       grp(0x802, 6,
                                           {grp(0x802, 9,
                                                {rec("REFR", 0x811, {}, 0x20),
                                                 rec("REFR", 0x812, {{"NAME", bytes_of({0x820})}})})})})})})})}));
  const std::vector<std::string> utf8{base.string(), patch.string()};
  const char* raw[] = {utf8[0].c_str(), utf8[1].c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(raw, 2, &env), RADICLE_STATUS_OK) << radicle_last_error();

  EXPECT_EQ(marker_keys(env, {"Map.esm", 0x800}), (std::vector<std::string>{"Map.esm:" + std::to_string(0x810), "Map.esm:" + std::to_string(0x814)}));
  EXPECT_EQ(marker_keys(env, {"Map.esm", 0x850}), (std::vector<std::string>{"Map.esm:" + std::to_string(0x852)}));
  EXPECT_TRUE(marker_keys(env, {"Map.esm", 0x999}).empty());

  const radicle_record** records = nullptr;
  size_t count = 0;
  EXPECT_EQ(radicle_env_worldspace_map_markers(nullptr, {"Map.esm", 0x800}, &records, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_env_worldspace_map_markers(env, {"Map.esm", 0x800}, nullptr, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_env_worldspace_map_markers(env, {"Map.esm", 0x800}, &records, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_env_worldspace_map_markers(env, {nullptr, 0}, &records, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_env_destroy(env);
}

// Constraint: detects a WRLD map-data row that drifts from xEdit wbWorldMapData and wbWorldMapOffset, which would give the Pip-Boy a wrong map frame.
TEST_F(WorldEnvironmentTest, WorldMapDataReadsThroughTheGenericFieldApi) {
  fo4::test::ByteWriter mnam;
  mnam.i32(393216);
  mnam.i32(327680);
  for (const std::int16_t cell : {std::int16_t{-30}, std::int16_t{25}, std::int16_t{30}, std::int16_t{-25}}) mnam.u16(static_cast<std::uint16_t>(cell));
  const auto plugin = write(
      "World.esm", make_plugin({}, {top("WRLD", {rec("WRLD", 0x800, {{"EDID", zstring("MapWorld")},
                                                                      {"ICON", zstring("Textures\\Maps\\World.dds")},
                                                                      {"MNAM", mnam.bytes},
                                                                      {"ONAM", floats({0.5f, 1.25f, -2.5f, 3.0f})}}),
                                                  rec("WRLD", 0x801)})}));
  const std::string utf8 = plugin.string();
  const char* raw[] = {utf8.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(raw, 1, &env), RADICLE_STATUS_OK) << radicle_last_error();
  const auto read = [&](std::uint32_t id, const char* path, std::string& text) {
    const radicle_record* record = nullptr;
    EXPECT_EQ(radicle_env_winner(env, {"World.esm", id}, &record), RADICLE_STATUS_OK);
    char* out = nullptr;
    const auto status = radicle_record_get_field(env, record, path, 0, &out);
    text = out == nullptr ? std::string{} : std::string(out);
    radicle_record_scalar_free(out);
    return status;
  };
  std::string text;
  const std::pair<const char*, double> numbers[] = {
      {"WRLD\\MapUsableX", 393216}, {"WRLD\\MapUsableY", 327680}, {"WRLD\\MapNwCellX", -30},
      {"WRLD\\MapNwCellY", 25},     {"WRLD\\MapSeCellX", 30},     {"WRLD\\MapSeCellY", -25},
      {"WRLD\\MapScale", 0.5},      {"WRLD\\MapCellOffsetX", 1.25}, {"WRLD\\MapCellOffsetY", -2.5},
      {"WRLD\\MapCellOffsetZ", 3.0}};
  for (const auto& [path, expected] : numbers) {
    ASSERT_EQ(read(0x800, path, text), RADICLE_STATUS_OK) << path;
    EXPECT_DOUBLE_EQ(std::stod(text), expected) << path;
  }
  ASSERT_EQ(read(0x800, "WRLD\\MapImage", text), RADICLE_STATUS_OK);
  EXPECT_EQ(text, "Textures\\Maps\\World.dds");
  EXPECT_EQ(read(0x801, "WRLD\\MapUsableX", text), RADICLE_STATUS_NOT_FOUND);
  radicle_env_destroy(env);
}
