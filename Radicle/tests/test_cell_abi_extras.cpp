#include "fo4/c_api_cell.h"
#include "fo4/c_api_cell_edit.h"

#include "env_node_fixture.hpp"
#include "plugin_bytes.hpp"
#include "xcll_fixture.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace fo4::test::envnode;
using fo4::test::xcll_bytes;

std::vector<std::byte> make_extras() {
  fo4::test::ByteWriter lod;
  lod.u32(0xC00);
  lod.f32(1.5f);
  lod.f32(-2.5f);
  lod.f32(3.5f);
  const auto cell = rec("CELL", 0x800,
                        {{"EDID", zstring("Extras")},
                         {"DATA", {std::byte{0x01}, std::byte{0}}},
                         {"VISI", {std::byte{0x34}, std::byte{0x12}}},
                         {"PCMB", {std::byte{0x78}, std::byte{0x56}}},
                         {"XCLL", xcll_bytes(136)},
                         {"XCLR", bytes_of({0x900, 0x901})},
                         {"XPRI", bytes_of({0x910})},
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
                         {"XGDR", bytes_of({0x1000})}});
  const auto short_tail = rec("CELL", 0x801, {{"XCLL", xcll_bytes(135)}});
  const auto bad_regions = rec("CELL", 0x802, {{"XCLR", {std::byte{0}, std::byte{0}}}});
  const auto world = rec("WRLD", 0x803, {{"EDID", zstring("NotACell")}});
  const auto stat = rec("STAT", 0x2100,
                        {{"EDID", zstring("TableStat")}, {"MODL", zstring("Props\\Table.nif")}});
  const auto scol = rec("SCOL", 0x2200,
                        {{"EDID", zstring("TableCollection")},
                         {"ONAM", bytes_of({0x2100})},
                         {"DATA", floats({1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                          4.0f, 5.0f, 6.0f, 0.0f, 0.0f, 0.0f, 1.0f})}});
  const auto merged = rec("CELL", 0x804, {{"EDID", zstring("Merged")}, {"DATA", {std::byte{0x01}, std::byte{0}}}});
  const auto merged_refs = grp(0x804, 6,
                               {grp(0x804, 9,
                                    {rec("REFR", 0x2001,
                                         {{"NAME", bytes_of({0x2100})},
                                          {"DATA", floats({10.0f, 20.0f, 30.0f, 0.0f, 0.0f, 0.0f})}}),
                                     rec("REFR", 0x2002,
                                         {{"NAME", bytes_of({0x2200})},
                                          {"DATA", floats({40.0f, 50.0f, 60.0f, 0.0f, 0.0f, 0.0f})}})})});
  const auto marker = rec("STAT", 0x2300,
                          {{"EDID", zstring("TriggerMarker")}, {"MODL", zstring("Markers\\Trigger.nif")}});
  const auto decal = rec("TXST", 0x2400,
                         {{"EDID", zstring("ScorchDecal")},
                          {"TX00", zstring("Decals\\Scorch.dds")},
                          {"DODT", floats({64.0f, 0.0f, 32.0f})}});
  const auto field = rec("CELL", 0x810,
                         {{"EDID", zstring("Field")},
                          {"FULL", zstring("Field Name")},
                          {"XCLC", bytes_of({1, 2})}});
  const auto field_persistent = grp(0x810, 8,
                                    {rec("REFR", 0x2401,
                                         {{"NAME", bytes_of({0x2400})},
                                          {"DATA", floats({100.0f, 200.0f, 0.0f, 0.0f, 0.0f, 0.0f})}},
                                         0x800)});
  const auto field_temporary = grp(0x810, 9,
                                   {rec("REFR", 0x2402,
                                        {{"NAME", bytes_of({0x2300})},
                                         {"DATA", floats({300.0f, 400.0f, 0.0f, 0.0f, 0.0f, 0.0f})}}),
                                    rec("REFR", 0x2403,
                                        {{"DATA", floats({500.0f, 600.0f, 0.0f, 0.0f, 0.0f, 0.0f})}})});
  const auto field_children = grp(0x810, 6, {field_persistent, field_temporary});
  const auto world_children = grp(0x803, 1, {grp(0, 4, {grp(0, 5, {field, field_children})})});
  return make_plugin({}, {top("CELL", {grp(0, 2, {grp(0, 3, {cell, short_tail, bad_regions, merged, merged_refs})})}),
                          top("WRLD", {world, world_children}), top("STAT", {stat, marker}),
                          top("SCOL", {scol}), top("TXST", {decal})});
}

std::vector<std::byte> make_patch() {
  const auto merged = rec("CELL", 0x804, {{"EDID", zstring("Merged")}, {"DATA", {std::byte{0x01}, std::byte{0}}}});
  const auto refs = grp(0x804, 6,
                        {grp(0x804, 9,
                             {rec("REFR", 0x2002,
                                  {{"NAME", bytes_of({0x2200})},
                                   {"DATA", floats({70.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f})}}),
                              rec("REFR", 0x01000003,
                                  {{"NAME", bytes_of({0x2100})},
                                   {"DATA", floats({80.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f})}})})});
  const auto world = rec("WRLD", 0x803, {{"EDID", zstring("NotACell")}});
  const auto field = rec("CELL", 0x810,
                         {{"EDID", zstring("Field")},
                          {"FULL", zstring("Patch Name")},
                          {"XCLC", bytes_of({1, 2})}});
  return make_plugin({"Extras.esp"}, {top("CELL", {grp(0, 2, {grp(0, 3, {merged, refs})})}),
                                      top("WRLD", {world, grp(0x803, 1, {grp(0, 4, {grp(0, 5, {field})})})})});
}

void write_file(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
}

std::string utf8_text(const std::filesystem::path& path) {
  const auto text = path.u8string();
  return std::string(text.begin(), text.end());
}

using PluginHandle = std::unique_ptr<radicle_plugin, void (*)(radicle_plugin*)>;

std::vector<std::uint8_t> patch_bytes(const radicle_plugin* patch) {
  uint8_t* bytes = nullptr;
  size_t size = 0;
  EXPECT_EQ(radicle_plugin_write_bytes(patch, &bytes, &size), RADICLE_STATUS_OK) << radicle_last_error();
  std::vector<std::uint8_t> copy(bytes, bytes + size);
  radicle_free_bytes(bytes);
  return copy;
}

bool find_reference(const radicle_cell_view* view, std::string_view plugin, std::uint32_t local_id,
                    radicle_cell_reference& out) {
  const auto count = radicle_cell_view_reference_count(view);
  for (size_t index = 0; index < count; ++index) {
    radicle_cell_reference reference{};
    if (radicle_cell_view_reference(view, index, &reference) != RADICLE_STATUS_OK) return false;
    if (reference.key.local_id == local_id && plugin == reference.key.plugin) {
      out = reference;
      return true;
    }
  }
  return false;
}

class CellAbiTest : public ::testing::Test {
protected:
  void SetUp() override {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() / ("radicle_cell_abi_" + std::to_string(id));
    std::filesystem::create_directories(root_);
    write_file(root_ / "Extras.esp", make_extras());
    write_file(root_ / "Patch.esp", make_patch());
    load_env();
  }

  void TearDown() override {
    if (env_ != nullptr) radicle_env_destroy(env_);
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }

  void load_env() {
    const auto base = utf8_text(root_ / "Extras.esp");
    const auto patch = utf8_text(root_ / "Patch.esp");
    const char* raw[] = {base.c_str(), patch.c_str()};
    ASSERT_EQ(radicle_env_load(raw, 2, &env_), RADICLE_STATUS_OK) << radicle_last_error();
  }

  void open_patch(PluginHandle& handle) {
    radicle_plugin* patch = nullptr;
    ASSERT_EQ(radicle_plugin_read_file(utf8_text(root_ / "Patch.esp").c_str(), &patch), RADICLE_STATUS_OK)
        << radicle_last_error();
    handle.reset(patch);
  }

  void reload_env(radicle_plugin* patch) {
    radicle_env_destroy(env_);
    env_ = nullptr;
    ASSERT_EQ(radicle_plugin_write_file(patch, utf8_text(root_ / "Patch.esp").c_str()), RADICLE_STATUS_OK)
        << radicle_last_error();
    load_env();
  }

  const radicle_record* winner(std::uint32_t local_id) {
    const radicle_record* record = nullptr;
    EXPECT_EQ(radicle_env_winner(env_, {"Extras.esp", local_id}, &record), RADICLE_STATUS_OK)
        << radicle_last_error();
    return record;
  }

  std::filesystem::path root_;
  radicle_env* env_{nullptr};
};

TEST_F(CellAbiTest, ExposesCellMembersLightingTailAndPackedLists) {
  const radicle_record* record = winner(0x800);
  ASSERT_NE(record, nullptr);
  radicle_cell_info info{};
  ASSERT_EQ(radicle_cell_get_info(env_, record, &info), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(info.has_previs_timestamp, 1);
  EXPECT_EQ(info.previs_timestamp, 0x1234u);
  EXPECT_EQ(info.has_precombined_timestamp, 1);
  EXPECT_EQ(info.precombined_timestamp, 0x5678u);
  EXPECT_STREQ(info.acoustic_space.plugin, "Extras.esp");
  EXPECT_EQ(info.acoustic_space.local_id, 0xE00u);
  EXPECT_EQ(info.music_type.local_id, 0xF00u);
  EXPECT_EQ(info.sky_weather_region.local_id, 0xD00u);
  EXPECT_EQ(info.god_rays.local_id, 0x1000u);
  EXPECT_EQ(info.lock_list.local_id, 0xB00u);
  EXPECT_EQ(info.owner.local_id, 0xA00u);
  EXPECT_EQ(info.has_owner_rank, 1);
  EXPECT_EQ(info.owner_rank, 3);
  EXPECT_EQ(info.has_exterior_lod, 1);
  EXPECT_EQ(info.exterior_lod_worldspace.local_id, 0xC00u);
  EXPECT_EQ(info.exterior_lod_offset[0], 1.5f);
  EXPECT_EQ(info.exterior_lod_offset[1], -2.5f);
  EXPECT_EQ(info.exterior_lod_offset[2], 3.5f);
  EXPECT_EQ(info.has_water_current_count, 1);
  EXPECT_EQ(info.water_current_count, 2u);
  ASSERT_NE(info.water_environment_map, nullptr);
  EXPECT_STREQ(info.water_environment_map, "water/current.dds");

  EXPECT_EQ(info.has_lighting, 1);
  EXPECT_EQ(info.lighting.fog_far, 4096.0f);
  EXPECT_EQ(info.lighting_tail.has_near_height, 1);
  EXPECT_EQ(info.lighting_tail.near_height_mid, 300.0f);
  EXPECT_EQ(info.lighting_tail.near_height_range, 400.0f);
  EXPECT_EQ(info.lighting_tail.has_high_fog_colors, 1);
  EXPECT_EQ(info.lighting_tail.fog_color_high_near.red, 0x10);
  EXPECT_EQ(info.lighting_tail.fog_color_high_near.green, 0x11);
  EXPECT_EQ(info.lighting_tail.fog_color_high_near.blue, 0x12);
  EXPECT_EQ(info.lighting_tail.fog_color_high_far.blue, 0x22);
  EXPECT_EQ(info.lighting_tail.has_high_density_scale, 1);
  EXPECT_EQ(info.lighting_tail.high_density_scale, 2.0f);
  EXPECT_EQ(info.lighting_tail.has_fog_scales, 1);
  EXPECT_EQ(info.lighting_tail.fog_near_scale, 3.0f);
  EXPECT_EQ(info.lighting_tail.fog_far_scale, 4.0f);
  EXPECT_EQ(info.lighting_tail.fog_high_near_scale, 5.0f);
  EXPECT_EQ(info.lighting_tail.fog_high_far_scale, 6.0f);
  EXPECT_EQ(info.lighting_tail.has_far_height, 1);
  EXPECT_EQ(info.lighting_tail.far_height_mid, 500.0f);
  EXPECT_EQ(info.lighting_tail.far_height_range, 600.0f);

  radicle_form_key* keys = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_cell_get_regions(env_, record, &keys, &count), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(count, 2u);
  EXPECT_STREQ(keys[0].plugin, "Extras.esp");
  EXPECT_EQ(keys[0].local_id, 0x900u);
  EXPECT_EQ(keys[1].local_id, 0x901u);
  radicle_free_form_keys(keys);
  keys = nullptr;
  ASSERT_EQ(radicle_cell_get_previs_references(env_, record, &keys, &count), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(count, 1u);
  EXPECT_STREQ(keys[0].plugin, "Extras.esp");
  EXPECT_EQ(keys[0].local_id, 0x910u);
  radicle_free_form_keys(keys);

  radicle_water_current* currents = nullptr;
  ASSERT_EQ(radicle_cell_get_water_currents(env_, record, &currents, &count), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(count, 2u);
  EXPECT_EQ(currents[0].velocity[2], 3.0f);
  EXPECT_EQ(currents[0].unnamed, 4.0f);
  EXPECT_EQ(currents[1].velocity[0], 5.0f);
  EXPECT_EQ(currents[1].unnamed, 8.0f);
  radicle_free_water_currents(currents);
}

TEST_F(CellAbiTest, ReportsShortTailsMalformedSubrecordsAndNonCells) {
  const radicle_record* short_tail = winner(0x801);
  ASSERT_NE(short_tail, nullptr);
  radicle_cell_info info{};
  ASSERT_EQ(radicle_cell_get_info(env_, short_tail, &info), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(info.lighting_tail.has_near_height, 1);
  EXPECT_EQ(info.lighting_tail.has_far_height, 0);
  EXPECT_EQ(info.lighting_tail.far_height_mid, 0.0f);
  EXPECT_EQ(info.lighting_tail.has_fog_scales, 1);
  EXPECT_EQ(info.water_environment_map, nullptr);

  radicle_form_key* keys = nullptr;
  size_t count = 7;
  ASSERT_EQ(radicle_cell_get_regions(env_, short_tail, &keys, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(keys, nullptr);
  EXPECT_EQ(count, 0u);
  radicle_water_current* currents = nullptr;
  ASSERT_EQ(radicle_cell_get_water_currents(env_, short_tail, &currents, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(currents, nullptr);
  EXPECT_EQ(count, 0u);

  const radicle_record* bad = winner(0x802);
  ASSERT_NE(bad, nullptr);
  EXPECT_EQ(radicle_cell_get_info(env_, bad, &info), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(radicle_cell_get_regions(env_, bad, &keys, &count), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(keys, nullptr);
  EXPECT_EQ(count, 0u);

  const radicle_record* world = winner(0x803);
  ASSERT_NE(world, nullptr);
  EXPECT_EQ(radicle_cell_get_info(env_, world, &info), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(radicle_cell_get_previs_references(env_, world, &keys, &count),
            RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(radicle_cell_get_water_currents(env_, world, &currents, &count),
            RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(currents, nullptr);
  EXPECT_EQ(count, 0u);

  EXPECT_EQ(radicle_cell_get_info(env_, nullptr, &info), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_cell_get_regions(env_, nullptr, &keys, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_cell_get_water_currents(env_, nullptr, nullptr, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_cell_get_info(env_, short_tail, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST_F(CellAbiTest, ResolveCellByKeyAndEditorId) {
  radicle_form_key key{};
  ASSERT_EQ(radicle_env_resolve_cell(env_, "Extras", &key), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_STREQ(key.plugin, "Extras.esp");
  EXPECT_EQ(key.local_id, 0x800u);

  ASSERT_EQ(radicle_env_resolve_cell(env_, "merged", &key), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(key.local_id, 0x804u);

  ASSERT_EQ(radicle_env_resolve_cell(env_, "000800:Extras.esp", &key), RADICLE_STATUS_OK);
  EXPECT_EQ(key.local_id, 0x800u);

  EXPECT_EQ(radicle_env_resolve_cell(env_, "NotACell", &key), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(key.plugin, nullptr);

  ASSERT_EQ(radicle_env_resolve_worldspace(env_, "NotACell", &key), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_STREQ(key.plugin, "Extras.esp");
  EXPECT_EQ(key.local_id, 0x803u);
  EXPECT_EQ(radicle_env_resolve_worldspace(env_, "Merged", &key), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_env_resolve_cell(env_, nullptr, &key), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST_F(CellAbiTest, FindCellAtGridNotFound) {
  radicle_form_key found{};
  EXPECT_EQ(radicle_env_find_cell_at_grid(env_, {"Extras.esp", 0x803}, 0, 0, &found),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(found.plugin, nullptr);
  EXPECT_EQ(radicle_env_find_cell_at_grid(env_, {"Extras.esp", 0x803}, 200, 0, &found),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_env_find_cell_at_grid(env_, {nullptr, 0}, 0, 0, &found),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST_F(CellAbiTest, FindCellAtGridReturnsHighestPluginCellSummary) {
  radicle_form_key found{};
  ASSERT_EQ(radicle_env_find_cell_at_grid(env_, {"Extras.esp", 0x803}, 1, 2, &found),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_STREQ(found.plugin, "Extras.esp");
  EXPECT_EQ(found.local_id, 0x810u);

  radicle_cell_view* view = nullptr;
  ASSERT_EQ(radicle_env_cell_view(env_, found, &view), RADICLE_STATUS_OK) << radicle_last_error();
  radicle_cell_summary summary{};
  ASSERT_EQ(radicle_cell_view_summary(view, &summary), RADICLE_STATUS_OK);
  EXPECT_EQ(summary.interior, 0);
  EXPECT_EQ(summary.has_grid, 1);
  EXPECT_EQ(summary.grid_x, 1);
  EXPECT_EQ(summary.grid_y, 2);
  EXPECT_EQ(summary.has_worldspace, 1);
  EXPECT_STREQ(summary.worldspace.plugin, "Extras.esp");
  EXPECT_EQ(summary.worldspace.local_id, 0x803u);
  EXPECT_STREQ(summary.editor_id, "Field");
  EXPECT_EQ(summary.name_kind, RADICLE_NAME_INLINE);
  EXPECT_STREQ(summary.name_text, "Patch Name");
  EXPECT_EQ(summary.winning_plugin, 1u);
  radicle_cell_view_free(view);
}

TEST_F(CellAbiTest, CellViewReportsReferenceFlagsDecalAndMarkerBases) {
  radicle_cell_view* view = nullptr;
  ASSERT_EQ(radicle_env_cell_view(env_, {"Extras.esp", 0x810}, &view), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(radicle_cell_view_reference_count(view), 3u);

  radicle_cell_reference decal_ref{};
  ASSERT_EQ(radicle_cell_view_reference(view, 0, &decal_ref), RADICLE_STATUS_OK);
  EXPECT_EQ(decal_ref.key.local_id, 0x2401u);
  EXPECT_EQ(decal_ref.has_base, 1);
  EXPECT_EQ(decal_ref.persistent, 1);
  EXPECT_EQ(decal_ref.initially_disabled, 1);
  ASSERT_NE(decal_ref.base_info, nullptr);
  EXPECT_STREQ(decal_ref.base_info->type_name, "TextureSet");
  EXPECT_EQ(decal_ref.base_info->has_decal, 1);
  EXPECT_STREQ(decal_ref.base_info->decal_diffuse, "Decals\\Scorch.dds");
  EXPECT_EQ(decal_ref.base_info->has_decal_width, 1);
  EXPECT_EQ(decal_ref.base_info->decal_width, 64.0f);
  EXPECT_EQ(decal_ref.base_info->has_decal_height, 1);
  EXPECT_EQ(decal_ref.base_info->decal_height, 32.0f);
  EXPECT_EQ(decal_ref.base_info->editor_marker, 0);

  radicle_cell_reference marker_ref{};
  ASSERT_EQ(radicle_cell_view_reference(view, 1, &marker_ref), RADICLE_STATUS_OK);
  EXPECT_EQ(marker_ref.key.local_id, 0x2402u);
  EXPECT_EQ(marker_ref.persistent, 0);
  EXPECT_EQ(marker_ref.initially_disabled, 0);
  ASSERT_NE(marker_ref.base_info, nullptr);
  EXPECT_EQ(marker_ref.base_info->editor_marker, 1);
  EXPECT_EQ(marker_ref.base_info->has_decal, 0);

  radicle_cell_reference bare_ref{};
  ASSERT_EQ(radicle_cell_view_reference(view, 2, &bare_ref), RADICLE_STATUS_OK);
  EXPECT_EQ(bare_ref.key.local_id, 0x2403u);
  EXPECT_EQ(bare_ref.has_base, 0);
  EXPECT_EQ(bare_ref.base_info, nullptr);
  EXPECT_EQ(bare_ref.position[0], 500.0f);
  radicle_cell_view_free(view);

  radicle_cell_view* non_cell = nullptr;
  EXPECT_EQ(radicle_env_cell_view(env_, {"Extras.esp", 0x803}, &non_cell), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(non_cell, nullptr);
}

TEST_F(CellAbiTest, CellViewListsMergedReferencesAndBaseInfo) {
  radicle_cell_view* view = nullptr;
  ASSERT_EQ(radicle_env_cell_view(env_, {"Extras.esp", 0x804}, &view), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_NE(view, nullptr);

  radicle_cell_summary summary{};
  ASSERT_EQ(radicle_cell_view_summary(view, &summary), RADICLE_STATUS_OK);
  EXPECT_STREQ(summary.key.plugin, "Extras.esp");
  EXPECT_EQ(summary.key.local_id, 0x804u);
  EXPECT_STREQ(summary.editor_id, "Merged");
  EXPECT_EQ(summary.interior, 1);
  EXPECT_EQ(summary.winning_plugin, 1u);

  ASSERT_EQ(radicle_cell_view_reference_count(view), 3u);

  radicle_cell_reference first{};
  ASSERT_EQ(radicle_cell_view_reference(view, 0, &first), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_STREQ(first.key.plugin, "Extras.esp");
  EXPECT_EQ(first.key.local_id, 0x2001u);
  EXPECT_STREQ(first.record_type, "PlacedObject");
  EXPECT_EQ(first.winning_plugin, 0u);
  EXPECT_EQ(first.position[0], 10.0f);
  ASSERT_NE(first.base_info, nullptr);
  EXPECT_STREQ(first.base_info->type_name, "Static");
  EXPECT_STREQ(first.base_info->model_path, "Props\\Table.nif");

  radicle_cell_reference second{};
  ASSERT_EQ(radicle_cell_view_reference(view, 1, &second), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(second.key.local_id, 0x2002u);
  EXPECT_EQ(second.winning_plugin, 1u);
  EXPECT_EQ(second.position[0], 70.0f);
  ASSERT_NE(second.base_info, nullptr);
  EXPECT_STREQ(second.base_info->type_name, "StaticCollection");
  EXPECT_EQ(second.base_info->scol_part_count, 1u);

  radicle_scol_part part{};
  ASSERT_EQ(radicle_cell_view_scol_part(view, 1, 0, &part), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_STREQ(part.model_path, "Props\\Table.nif");
  ASSERT_EQ(part.placement_count, 2u);
  EXPECT_EQ(part.placements[1].position[0], 4.0f);
  EXPECT_EQ(part.placements[1].scale, 1.0f);

  radicle_cell_reference third{};
  ASSERT_EQ(radicle_cell_view_reference(view, 2, &third), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_STREQ(third.key.plugin, "Patch.esp");
  EXPECT_EQ(third.key.local_id, 3u);
  EXPECT_EQ(third.winning_plugin, 1u);
  EXPECT_EQ(third.position[0], 80.0f);
  ASSERT_NE(third.base_info, nullptr);
  EXPECT_STREQ(third.base_info->model_path, "Props\\Table.nif");

  EXPECT_EQ(radicle_cell_view_scol_part(view, 0, 0, &part), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_cell_view_free(view);

  radicle_base_object_info* info = nullptr;
  ASSERT_EQ(radicle_env_base_object_info(env_, {"Extras.esp", 0x2200}, &info), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_NE(info, nullptr);
  EXPECT_STREQ(info->signature, "SCOL");
  EXPECT_STREQ(info->type_name, "StaticCollection");
  ASSERT_EQ(info->scol_part_count, 1u);
  EXPECT_STREQ(info->scol_parts[0].model_path, "Props\\Table.nif");
  radicle_base_object_info_free(info);
}

TEST_F(CellAbiTest, CellViewFreeIsNullSafe) {
  radicle_cell_view_free(nullptr);
  radicle_base_object_info_free(nullptr);
  EXPECT_EQ(radicle_cell_view_reference_count(nullptr), 0u);

  radicle_cell_view* view = nullptr;
  EXPECT_EQ(radicle_env_cell_view(env_, {"Extras.esp", 0x9999}, &view), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(view, nullptr);
  radicle_base_object_info* info = nullptr;
  EXPECT_EQ(radicle_env_base_object_info(env_, {"Extras.esp", 0x9999}, &info),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(info, nullptr);

  ASSERT_EQ(radicle_env_cell_view(env_, {"Extras.esp", 0x804}, &view), RADICLE_STATUS_OK)
      << radicle_last_error();
  radicle_cell_reference reference{};
  EXPECT_EQ(radicle_cell_view_reference(view, 3, &reference), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_scol_part part{};
  EXPECT_EQ(radicle_cell_view_scol_part(view, 2, 0, &part), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_cell_view_free(view);
}

TEST_F(CellAbiTest, EditPlacedReferenceWritesOverrideAndReadsBack) {
  PluginHandle patch(nullptr, &radicle_plugin_destroy);
  open_patch(patch);
  const auto before = patch_bytes(patch.get());

  radicle_placed_edit edit{};
  edit.has_transform = 1;
  edit.position[0] = 11.0f;
  edit.position[1] = 21.0f;
  edit.position[2] = 31.0f;
  edit.has_editor_id = 1;
  edit.editor_id = "Edited";
  radicle_placed_edit_result result{};
  ASSERT_EQ(radicle_plugin_edit_placed_reference(patch.get(), env_, {"Extras.esp", 0x2001}, &edit, &result),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(result.status, RADICLE_PLACED_EDIT_EDITED);
  EXPECT_STREQ(result.copied_from, "Extras.esp");
  EXPECT_EQ(result.relocated, 0);
  EXPECT_NE(patch_bytes(patch.get()), before);

  reload_env(patch.get());
  radicle_cell_view* view = nullptr;
  ASSERT_EQ(radicle_env_cell_view(env_, {"Extras.esp", 0x804}, &view), RADICLE_STATUS_OK) << radicle_last_error();
  radicle_cell_reference reference{};
  ASSERT_TRUE(find_reference(view, "Extras.esp", 0x2001, reference));
  EXPECT_STREQ(reference.editor_id, "Edited");
  EXPECT_EQ(reference.position[0], 11.0f);
  EXPECT_EQ(reference.position[2], 31.0f);
  radicle_cell_view_free(view);
}

TEST_F(CellAbiTest, EditPlacedReferenceRefusalLeavesPatchUnchanged) {
  PluginHandle patch(nullptr, &radicle_plugin_destroy);
  open_patch(patch);
  const auto before = patch_bytes(patch.get());

  radicle_placed_edit edit{};
  edit.has_transform = 1;
  edit.position[0] = 500.0f;
  radicle_placed_edit_result result{};
  EXPECT_EQ(radicle_plugin_edit_placed_reference(patch.get(), env_, {"Extras.esp", 0x9999}, &edit, &result),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(result.status, RADICLE_PLACED_EDIT_REFERENCE_MISSING);
  EXPECT_EQ(patch_bytes(patch.get()), before);

  edit.has_primitive = 1;
  EXPECT_EQ(radicle_plugin_edit_placed_reference(patch.get(), env_, {"Extras.esp", 0x2001}, &edit, &result),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(result.status, RADICLE_PLACED_EDIT_FIELD_ABSENT);
  EXPECT_EQ(patch_bytes(patch.get()), before);
}

TEST_F(CellAbiTest, DuplicatePlacedReferenceReturnsPatchKeyInSourceCell) {
  PluginHandle patch(nullptr, &radicle_plugin_destroy);
  open_patch(patch);
  radicle_duplicate_options options{};
  options.offset_position[0] = 5.0f;
  radicle_duplicate_status status = RADICLE_DUPLICATE_REFERENCE_MISSING;
  radicle_form_key created{};
  ASSERT_EQ(radicle_plugin_duplicate_placed_reference(patch.get(), env_, {"Extras.esp", 0x2001}, &options,
                                                      &status, &created),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(status, RADICLE_DUPLICATE_DUPLICATED);
  ASSERT_NE(created.plugin, nullptr);
  EXPECT_STREQ(created.plugin, "Patch.esp");
  EXPECT_NE(created.local_id, 0x2001u);
  const std::string created_plugin = created.plugin;
  const auto created_id = created.local_id;

  reload_env(patch.get());
  radicle_cell_view* view = nullptr;
  ASSERT_EQ(radicle_env_cell_view(env_, {"Extras.esp", 0x804}, &view), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(radicle_cell_view_reference_count(view), 4u);
  radicle_cell_reference copy{};
  ASSERT_TRUE(find_reference(view, created_plugin, created_id, copy));
  EXPECT_EQ(copy.position[0], 15.0f);
  EXPECT_EQ(copy.position[1], 20.0f);
  radicle_cell_view_free(view);
}

TEST_F(CellAbiTest, DeleteReportsStatusAndDropsReferenceAfterReload) {
  PluginHandle patch(nullptr, &radicle_plugin_destroy);
  open_patch(patch);
  radicle_delete_status status = RADICLE_DELETE_REFERENCE_MISSING;
  ASSERT_EQ(radicle_plugin_delete_placed_reference(patch.get(), env_, {"Extras.esp", 0x2002}, &status),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(status, RADICLE_DELETE_MARKED_DELETED);
  const auto after_mark = patch_bytes(patch.get());

  EXPECT_EQ(radicle_plugin_delete_placed_reference(patch.get(), env_, {"Extras.esp", 0x2002}, &status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(status, RADICLE_DELETE_ALREADY_DELETED);
  EXPECT_EQ(radicle_plugin_delete_placed_reference(patch.get(), env_, {"Extras.esp", 0x9999}, &status),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(status, RADICLE_DELETE_REFERENCE_MISSING);
  EXPECT_EQ(patch_bytes(patch.get()), after_mark);

  ASSERT_EQ(radicle_plugin_delete_placed_reference(patch.get(), env_, {"Patch.esp", 3}, &status),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(status, RADICLE_DELETE_DELETED);

  reload_env(patch.get());
  radicle_cell_view* view = nullptr;
  ASSERT_EQ(radicle_env_cell_view(env_, {"Extras.esp", 0x804}, &view), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(radicle_cell_view_reference_count(view), 1u);
  radicle_cell_reference remaining{};
  ASSERT_EQ(radicle_cell_view_reference(view, 0, &remaining), RADICLE_STATUS_OK);
  EXPECT_EQ(remaining.key.local_id, 0x2001u);
  radicle_cell_view_free(view);
}

TEST_F(CellAbiTest, CopyCellOverrideReportsStatusAndRefusals) {
  PluginHandle patch(nullptr, &radicle_plugin_destroy);
  open_patch(patch);
  radicle_plugin* source_plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(utf8_text(root_ / "Extras.esp").c_str(), &source_plugin),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  PluginHandle source(source_plugin, &radicle_plugin_destroy);
  const auto before = patch_bytes(patch.get());

  radicle_cell_override_status status = RADICLE_CELL_OVERRIDE_INSERTED;
  EXPECT_EQ(radicle_plugin_copy_cell_override(patch.get(), source.get(), {"Extras.esp", 0x804}, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_CELL_OVERRIDE_ALREADY_EXISTS);
  EXPECT_EQ(radicle_plugin_copy_cell_override(patch.get(), source.get(), {"Extras.esp", 0x803}, &status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(status, RADICLE_CELL_OVERRIDE_NOT_A_CELL);
  EXPECT_EQ(radicle_plugin_copy_cell_override(patch.get(), source.get(), {"Extras.esp", 0x9999}, &status),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(status, RADICLE_CELL_OVERRIDE_SOURCE_MISSING);
  EXPECT_EQ(patch_bytes(patch.get()), before);

  ASSERT_EQ(radicle_plugin_copy_cell_override(patch.get(), source.get(), {"Extras.esp", 0x800}, &status),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(status, RADICLE_CELL_OVERRIDE_INSERTED);
  EXPECT_NE(patch_bytes(patch.get()), before);
}

TEST_F(CellAbiTest, EditCellWritesOverrideAndRefusesNonCells) {
  PluginHandle patch(nullptr, &radicle_plugin_destroy);
  open_patch(patch);
  radicle_cell_edit edit{};
  edit.has_water_height = 1;
  edit.water_height = 12.5f;
  radicle_cell_edit_status status = RADICLE_CELL_EDIT_CELL_MISSING;
  ASSERT_EQ(radicle_plugin_edit_cell(patch.get(), env_, {"Extras.esp", 0x800}, &edit, &status),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(status, RADICLE_CELL_EDIT_EDITED);
  const auto after = patch_bytes(patch.get());

  EXPECT_EQ(radicle_plugin_edit_cell(patch.get(), env_, {"Extras.esp", 0x803}, &edit, &status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(status, RADICLE_CELL_EDIT_NOT_A_CELL);
  EXPECT_EQ(radicle_plugin_edit_cell(patch.get(), env_, {"Extras.esp", 0x9999}, &edit, &status),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(status, RADICLE_CELL_EDIT_CELL_MISSING);
  EXPECT_EQ(patch_bytes(patch.get()), after);

  reload_env(patch.get());
  const radicle_record* record = nullptr;
  ASSERT_EQ(radicle_env_winner(env_, {"Extras.esp", 0x800}, &record), RADICLE_STATUS_OK) << radicle_last_error();
  radicle_cell_info info{};
  ASSERT_EQ(radicle_cell_get_info(env_, record, &info), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(info.has_water_height, 1);
  EXPECT_EQ(info.water_height, 12.5f);
  EXPECT_EQ(info.lighting.fog_far, 4096.0f);
}

TEST_F(CellAbiTest, EditCallsRejectNullArgumentsAndInvalidKeys) {
  PluginHandle patch(nullptr, &radicle_plugin_destroy);
  open_patch(patch);
  const auto before = patch_bytes(patch.get());

  radicle_placed_edit placed{};
  radicle_placed_edit_result placed_result{};
  EXPECT_EQ(radicle_plugin_edit_placed_reference(nullptr, env_, {"Extras.esp", 0x2001}, &placed, &placed_result),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_edit_placed_reference(patch.get(), env_, {"Extras.esp", 0x2001}, nullptr, &placed_result),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_edit_placed_reference(patch.get(), env_, {nullptr, 0}, &placed, &placed_result),
            RADICLE_STATUS_INVALID_ARGUMENT);
  placed.has_ownership = 1;
  placed.owner = {"Extras.esp", 0x1000000};
  EXPECT_EQ(radicle_plugin_edit_placed_reference(patch.get(), env_, {"Extras.esp", 0x2001}, &placed, &placed_result),
            RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_form_key created{};
  EXPECT_EQ(radicle_plugin_duplicate_placed_reference(patch.get(), env_, {"Extras.esp", 0x2001}, nullptr, nullptr,
                                                      &created),
            RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_delete_status delete_status = RADICLE_DELETE_DELETED;
  EXPECT_EQ(radicle_plugin_delete_placed_reference(patch.get(), env_, {nullptr, 0}, &delete_status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_cell_override_status override_status = RADICLE_CELL_OVERRIDE_INSERTED;
  EXPECT_EQ(radicle_plugin_copy_cell_override(patch.get(), patch.get(), {"Extras.esp", 0x800}, &override_status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_cell_edit_status cell_status = RADICLE_CELL_EDIT_EDITED;
  EXPECT_EQ(radicle_plugin_edit_cell(patch.get(), env_, {"Extras.esp", 0x800}, nullptr, &cell_status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(patch_bytes(patch.get()), before);
}

}  // namespace
