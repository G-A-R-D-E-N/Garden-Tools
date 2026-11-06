#include "fo4/c_api_cell.h"

#include "env_node_fixture.hpp"
#include "plugin_bytes.hpp"
#include "xcll_fixture.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using namespace fo4::test::envnode;
using fo4::test::xcll_bytes;

// Extras.esp: CELL 000800 carries every decoded CELL member, 000801 a truncated XCLL, 000802 a malformed XCLR, 000803 a WRLD the CELL exports must reject.
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
  const auto merged_refs = grp(0x804, 9,
                               {rec("REFR", 0x2001,
                                    {{"NAME", bytes_of({0x2100})},
                                     {"DATA", floats({10.0f, 20.0f, 30.0f, 0.0f, 0.0f, 0.0f})}}),
                                rec("REFR", 0x2002,
                                    {{"NAME", bytes_of({0x2200})},
                                     {"DATA", floats({40.0f, 50.0f, 60.0f, 0.0f, 0.0f, 0.0f})}})});
  return make_plugin({}, {top("CELL", {grp(0, 2, {grp(0, 3, {cell, short_tail, bad_regions, merged, merged_refs})})}),
                          top("WRLD", {world}), top("STAT", {stat}), top("SCOL", {scol})});
}

std::vector<std::byte> make_patch() {
  const auto merged = rec("CELL", 0x804, {{"EDID", zstring("Merged")}, {"DATA", {std::byte{0x01}, std::byte{0}}}});
  const auto refs = grp(0x804, 9,
                        {rec("REFR", 0x2002,
                             {{"NAME", bytes_of({0x2200})},
                              {"DATA", floats({70.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f})}}),
                         rec("REFR", 0x01000003,
                             {{"NAME", bytes_of({0x2100})},
                              {"DATA", floats({80.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f})}})});
  return make_plugin({"Extras.esp"}, {top("CELL", {grp(0, 2, {grp(0, 3, {merged, refs})})})});
}

void write_file(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
}

class CellAbiTest : public ::testing::Test {
protected:
  void SetUp() override {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() / ("radicle_cell_abi_" + std::to_string(id));
    std::filesystem::create_directories(root_);
    write_file(root_ / "Extras.esp", make_extras());
    write_file(root_ / "Patch.esp", make_patch());
    const auto base = (root_ / "Extras.esp").u8string();
    const auto patch = (root_ / "Patch.esp").u8string();
    const std::string base_utf8(base.begin(), base.end());
    const std::string patch_utf8(patch.begin(), patch.end());
    const char* raw[] = {base_utf8.c_str(), patch_utf8.c_str()};
    ASSERT_EQ(radicle_env_load(raw, 2, &env_), RADICLE_STATUS_OK) << radicle_last_error();
  }

  void TearDown() override {
    if (env_ != nullptr) radicle_env_destroy(env_);
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
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

}  // namespace
