#include "fo4/c_api_env.h"
#include "fo4/c_api_records.h"
#include "fo4/environment/world.hpp"
#include "env_node_fixture.hpp"
#include "plugin_bytes.hpp"
#include "xcll_fixture.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
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
  EXPECT_STREQ(radicle_last_error(), "cyclic worldspace water inheritance");
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
