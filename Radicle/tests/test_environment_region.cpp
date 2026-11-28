#include "fo4/c_api_cell.h"
#include "fo4/c_api_env.h"
#include "fo4/environment/region.hpp"
#include "env_node_fixture.hpp"
#include "plugin_bytes.hpp"
#include "temp_data_dir.hpp"
#include "byte_helpers.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;
using fo4::test::envnode::bytes_of;
using fo4::test::envnode::floats;
using fo4::test::envnode::rec;
using fo4::test::envnode::top;

using fo4::test::u32_bytes;

Bytes text_bytes(std::string_view text) {
  Bytes bytes;
  for (const char ch : text) bytes.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  bytes.push_back(std::byte{0});
  return bytes;
}

Bytes rdat_bytes(std::uint32_t type, std::uint8_t override_value, std::uint8_t priority) {
  ByteWriter out;
  out.u32(type);
  out.u8(override_value);
  out.u8(priority);
  out.u8(0);
  out.u8(0);
  return out.bytes;
}

void write_header(ByteWriter& out) {
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Fallout4.esm\0", 13});
  fo4::test::write_subrecord(out, "DATA", Bytes(8));
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Objects.esp\0", 12});
  fo4::test::write_subrecord(out, "DATA", Bytes(8));
  fo4::test::end_record(out, tes4);
}

void write_grouped_record(ByteWriter& out, std::string_view signature, std::uint32_t local_id,
                          std::initializer_list<std::pair<std::string_view, Bytes>> subs) {
  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, 0x02000000u | local_id);
  for (const auto& sub : subs) fo4::test::write_subrecord(out, sub.first, sub.second);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
}

Bytes point_bytes(std::initializer_list<float> values) {
  ByteWriter out;
  for (const auto value : values) out.f32(value);
  return out.bytes;
}

Bytes rdot_bytes() {
  ByteWriter out;
  out.u32(0x02000D40);
  out.u16(3);
  out.u16(0);
  out.f32(0.25f);
  out.u8(1);
  out.u8(2);
  out.u8(3);
  out.u8(0x81);
  out.u16(7);
  out.u16(9);
  out.f32(-10.0f);
  out.f32(20.0f);
  out.f32(0.5f);
  out.f32(0.75f);
  out.f32(1.25f);
  out.u16(11);
  out.u16(12);
  out.u16(13);
  out.u16(0);
  out.u32(0);
  return out.bytes;
}

Bytes rdwt_bytes() {
  ByteWriter out;
  out.u32(0x02000D50);
  out.u32(100);
  out.u32(0x02000D51);
  out.u32(0x02000D52);
  out.u32(0);
  out.u32(0);
  return out.bytes;
}

Bytes rdgs_bytes(std::uint32_t object) {
  ByteWriter out;
  out.u32(object);
  out.u32(0);
  return out.bytes;
}

Bytes rdsa_bytes() {
  ByteWriter out;
  out.u32(0x02000D70);
  out.u32(3);
  out.f32(0.5f);
  return out.bytes;
}

Bytes typed_region_plugin() {
  ByteWriter out;
  write_header(out);
  write_grouped_record(
      out, "REGN", 0x0D10,
      {{"EDID", text_bytes("TypedRegion")},
       {"RCLR", Bytes{std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0x00}}},
       {"WNAM", u32_bytes(0x00000D20)},
       {"RPLI", u32_bytes(80)},
       {"RPLD", point_bytes({1.5f, -2.5f, 3.5f, 4.5f})},
       {"RPLI", u32_bytes(120)},
       {"RPLD", point_bytes({6.5f, 7.5f})},
       {"RDAT", rdat_bytes(3, 1, 40)},
       {"RDWT", rdwt_bytes()},
       {"RDAT", rdat_bytes(2, 0, 50)},
       {"RDOT", rdot_bytes()},
       {"RDAT", rdat_bytes(6, 0, 60)},
       {"RDGS", rdgs_bytes(0x02000D60)},
       {"RDAT", rdat_bytes(7, 1, 70)},
       {"RDMO", u32_bytes(0x02000D21)},
       {"RDSA", rdsa_bytes()},
       {"RDAT", rdat_bytes(4, 0, 80)},
       {"RDMP", text_bytes("Map Name")},
       {"RDAT", rdat_bytes(5, 0, 90)}});
  return out.bytes;
}

TEST(RegionEnvironmentTest, DecodesTypedRegionAreasEntriesAndPayloads) {
  fo4::test::TempDataDir dir;
  dir.write("Regions.esp", typed_region_plugin(), 0);
  const std::vector<std::filesystem::path> ordered{dir.path() / "Regions.esp"};
  const auto env = fo4::env::Environment::load(ordered);
  const auto* ref = env.winner(fo4::FormKey::parse("000D10:Regions.esp"));
  ASSERT_NE(ref, nullptr);
  const auto region = fo4::env::decode_region(env, *ref);

  ASSERT_TRUE(region.map_color.has_value());
  EXPECT_EQ(region.map_color->red, 0x11);
  EXPECT_EQ(region.map_color->green, 0x22);
  EXPECT_EQ(region.map_color->blue, 0x33);
  EXPECT_EQ(region.worldspace, fo4::FormKey::parse("000D20:Fallout4.esm"));

  ASSERT_EQ(region.areas.size(), 2u);
  EXPECT_EQ(region.areas[0].edge_falloff, 80u);
  ASSERT_EQ(region.areas[0].points.size(), 2u);
  EXPECT_EQ(region.areas[0].points[0].x, 1.5f);
  EXPECT_EQ(region.areas[0].points[0].y, -2.5f);
  EXPECT_EQ(region.areas[0].points[1].x, 3.5f);
  EXPECT_EQ(region.areas[0].points[1].y, 4.5f);
  EXPECT_EQ(region.areas[1].edge_falloff, 120u);
  ASSERT_EQ(region.areas[1].points.size(), 1u);
  EXPECT_EQ(region.areas[1].points[0].x, 6.5f);

  ASSERT_EQ(region.entries.size(), 6u);
  EXPECT_EQ(region.entries[0].type, 3u);
  EXPECT_EQ(region.entries[0].override_value, 1);
  EXPECT_EQ(region.entries[0].priority, 40);
  ASSERT_EQ(region.entries[0].weathers.size(), 2u);
  EXPECT_EQ(region.entries[0].weathers[0].weather, fo4::FormKey::parse("000D50:Regions.esp"));
  EXPECT_EQ(region.entries[0].weathers[0].chance, 100u);
  EXPECT_EQ(region.entries[0].weathers[0].global, fo4::FormKey::parse("000D51:Regions.esp"));
  EXPECT_EQ(region.entries[0].weathers[1].weather, fo4::FormKey::parse("000D52:Regions.esp"));
  EXPECT_EQ(region.entries[0].weathers[1].chance, 0u);
  EXPECT_FALSE(region.entries[0].weathers[1].global.has_value());

  ASSERT_EQ(region.entries[1].objects.size(), 1u);
  const auto& object = region.entries[1].objects[0];
  EXPECT_EQ(object.object, fo4::FormKey::parse("000D40:Regions.esp"));
  EXPECT_EQ(object.parent_index, 3);
  EXPECT_EQ(object.density, 0.25f);
  EXPECT_EQ(object.clustering, 1);
  EXPECT_EQ(object.min_slope, 2);
  EXPECT_EQ(object.max_slope, 3);
  EXPECT_EQ(object.flags, 0x81);
  EXPECT_EQ(object.radius_with_parent, 7);
  EXPECT_EQ(object.radius, 9);
  EXPECT_EQ(object.min_height, -10.0f);
  EXPECT_EQ(object.max_height, 20.0f);
  EXPECT_EQ(object.sink, 0.5f);
  EXPECT_EQ(object.sink_variance, 0.75f);
  EXPECT_EQ(object.size_variance, 1.25f);
  EXPECT_EQ(object.angle_variance[0], 11);
  EXPECT_EQ(object.angle_variance[1], 12);
  EXPECT_EQ(object.angle_variance[2], 13);

  ASSERT_EQ(region.entries[2].grasses.size(), 1u);
  EXPECT_EQ(region.entries[2].grasses[0], fo4::FormKey::parse("000D60:Regions.esp"));

  ASSERT_EQ(region.entries[3].sounds.size(), 1u);
  EXPECT_EQ(region.entries[3].sounds[0].sound, fo4::FormKey::parse("000D70:Regions.esp"));
  EXPECT_EQ(region.entries[3].sounds[0].flags, 3u);
  EXPECT_EQ(region.entries[3].sounds[0].chance, 0.5f);
  EXPECT_EQ(region.entries[3].music, fo4::FormKey::parse("000D21:Regions.esp"));

  EXPECT_EQ(region.entries[4].type, 4u);
  EXPECT_EQ(region.entries[4].map_name.kind, fo4::env::NameKind::inline_text);
  EXPECT_EQ(region.entries[4].map_name.text, "Map Name");
  EXPECT_EQ(region.entries[5].type, 5u);
  EXPECT_TRUE(region.entries[5].objects.empty());
  EXPECT_TRUE(region.entries[5].grasses.empty());
  EXPECT_TRUE(region.entries[5].weathers.empty());
  EXPECT_TRUE(region.entries[5].sounds.empty());
}

TEST(RegionEnvironmentTest, CApiListsRegionWeathersAcrossEntriesWithPriorities) {
  const auto bytes = fo4::test::envnode::make_plugin(
      {}, {top("REGN",
               {rec("REGN", 0x0F01, {{"RDAT", bytes_of({3, 0})}, {"RDWT", bytes_of({0x0D50, 40, 0x0D51, 0x0D52, 0, 0})},
                                     {"RDAT", bytes_of({2, 0})},
                                     {"RDAT", bytes_of({3, 0x0A01})},
                                     {"RDWT", bytes_of({0x0D53, 7, 0})}}),
                rec("REGN", 0x0F02, {{"RDAT", bytes_of({2, 0})}}),
                rec("REGN", 0x0F03, {{"RDAT", bytes_of({3, 0})}, {"RDWT", Bytes(13)}}),
                rec("REGN", 0x0F04, {})}),
           top("CELL", {rec("CELL", 0x0F05, {})})});
  fo4::test::TempDataDir dir;
  dir.write("Weathers.esp", bytes, 0);
  const std::string path = (dir.path() / "Weathers.esp").string();
  const char* raw[] = {path.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(raw, 1, &env), RADICLE_STATUS_OK);
  const auto record = [&](std::uint32_t local) {
    const radicle_record* out = nullptr;
    EXPECT_EQ(radicle_env_winner(env, {"Weathers.esp", local}, &out), RADICLE_STATUS_OK);
    return out;
  };

  radicle_region_weather* rows = nullptr;
  size_t count = 99;
  ASSERT_EQ(radicle_region_get_weathers(env, record(0x0F01), &rows, &count), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(count, 3u);
  EXPECT_STREQ(rows[0].weather.plugin, "Weathers.esp");
  EXPECT_EQ(rows[0].weather.local_id, 0x0D50u);
  EXPECT_EQ(rows[0].chance, 40u);
  EXPECT_STREQ(rows[0].global.plugin, "Weathers.esp");
  EXPECT_EQ(rows[0].global.local_id, 0x0D51u);
  EXPECT_EQ(rows[0].override_value, 0);
  EXPECT_EQ(rows[0].priority, 0);
  EXPECT_EQ(rows[1].weather.local_id, 0x0D52u);
  EXPECT_EQ(rows[1].chance, 0u);
  EXPECT_EQ(rows[1].global.plugin, nullptr);
  EXPECT_EQ(rows[1].global.local_id, 0u);
  EXPECT_EQ(rows[2].weather.local_id, 0x0D53u);
  EXPECT_EQ(rows[2].chance, 7u);
  EXPECT_EQ(rows[2].override_value, 1);
  EXPECT_EQ(rows[2].priority, 0x0A);
  radicle_free_region_weathers(rows);

  ASSERT_EQ(radicle_region_get_weathers(env, record(0x0F02), &rows, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(rows, nullptr);
  EXPECT_EQ(count, 0u);
  ASSERT_EQ(radicle_region_get_weathers(env, record(0x0F04), &rows, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 0u);
  EXPECT_EQ(radicle_region_get_weathers(env, record(0x0F03), &rows, &count), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(rows, nullptr);
  EXPECT_EQ(radicle_region_get_weathers(env, record(0x0F05), &rows, &count), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(radicle_region_get_weathers(env, nullptr, &rows, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_region_get_weathers(env, record(0x0F01), nullptr, &count), RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_env_destroy(env);
}

using fo4::test::envnode::Node;
using fo4::test::envnode::Sub;

std::vector<Sub> square_area(float x0, float y0, float x1, float y1) {
  return {{"RPLI", bytes_of({0})}, {"RPLD", floats({x0, y0, x1, y0, x1, y1, x0, y1})}};
}

Node region_with(std::uint32_t local, std::uint32_t world, std::vector<std::vector<Sub>> areas,
                 std::optional<std::uint32_t> weather_priority) {
  std::vector<Sub> subs{{"WNAM", bytes_of({world})}};
  for (const auto& area : areas) subs.insert(subs.end(), area.begin(), area.end());
  if (weather_priority) {
    subs.push_back({"RDAT", bytes_of({3, *weather_priority << 8})});
    subs.push_back({"RDWT", bytes_of({0x0D50, 100, 0})});
  } else {
    subs.push_back({"RDAT", bytes_of({2, 0})});
  }
  return rec("REGN", local, std::move(subs));
}

std::vector<std::byte> position_plugin() {
  return fo4::test::envnode::make_plugin(
      {}, {top("REGN",
               {region_with(0x2001, 0x1000, {square_area(0, 0, 10, 10)}, 5),
                region_with(0x2002, 0x1000, {square_area(5, 5, 15, 15)}, 9),
                region_with(0x2003, 0x1001, {square_area(0, 0, 10, 10)}, 20),
                region_with(0x2004, 0x1000, {square_area(0, 0, 10, 10)}, std::nullopt),
                rec("REGN", 0x2005, {{"WNAM", bytes_of({0x1000})}, {"RPLI", bytes_of({0})},
                                     {"RPLD", floats({0, 0, 10, 10})}}),
                region_with(0x2006, 0x1000,
                            {square_area(100, 100, 110, 110), square_area(8, 8, 12, 12)}, 5)})});
}

TEST(RegionEnvironmentTest, RegionsAtFollowsAreaPolygonsAndWeatherPriority) {
  fo4::test::TempDataDir dir;
  dir.write("Regions.esp", position_plugin(), 0);
  const std::vector<std::filesystem::path> ordered{dir.path() / "Regions.esp"};
  const auto env = fo4::env::Environment::load(ordered);
  const auto r = [](const char* local) { return fo4::FormKey::parse(std::string(local) + ":Regions.esp"); };
  const auto world = r("001000");
  using Keys = std::vector<fo4::FormKey>;

  EXPECT_EQ(fo4::env::regions_at(env, world, 3, 3), (Keys{r("002001"), r("002004")}));
  EXPECT_EQ(fo4::env::regions_at(env, world, 7, 7), (Keys{r("002002"), r("002001"), r("002004")}));
  EXPECT_EQ(fo4::env::regions_at(env, world, 5, 5), (Keys{r("002002"), r("002001"), r("002004")}));
  EXPECT_EQ(fo4::env::regions_at(env, world, 15, 10), (Keys{r("002002")}));
  EXPECT_EQ(fo4::env::regions_at(env, world, 9, 9),
            (Keys{r("002002"), r("002001"), r("002006"), r("002004")}));
  EXPECT_EQ(fo4::env::regions_at(env, world, 11, 11), (Keys{r("002002"), r("002006")}));
  EXPECT_EQ(fo4::env::regions_at(env, world, 12, 12), (Keys{r("002002"), r("002006")}));
  EXPECT_EQ(fo4::env::regions_at(env, world, 105, 105), (Keys{r("002006")}));
  EXPECT_TRUE(fo4::env::regions_at(env, world, -1, 5).empty());
  EXPECT_TRUE(fo4::env::regions_at(env, world, 10.5f, 0).empty());
  EXPECT_EQ(fo4::env::regions_at(env, r("001001"), 3, 3), (Keys{r("002003")}));
  EXPECT_TRUE(fo4::env::regions_at(env, r("001FFF"), 3, 3).empty());
}

TEST(RegionEnvironmentTest, CApiRegionsAtReturnsPriorityOrderedKeys) {
  fo4::test::TempDataDir dir;
  dir.write("Regions.esp", position_plugin(), 0);
  const std::string path = (dir.path() / "Regions.esp").string();
  const char* raw[] = {path.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(raw, 1, &env), RADICLE_STATUS_OK);

  radicle_form_key* keys = nullptr;
  size_t count = 99;
  ASSERT_EQ(radicle_env_regions_at(env, {"Regions.esp", 0x1000}, 9.0f, 9.0f, &keys, &count), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_EQ(count, 4u);
  EXPECT_STREQ(keys[0].plugin, "Regions.esp");
  EXPECT_EQ(keys[0].local_id, 0x2002u);
  EXPECT_EQ(keys[1].local_id, 0x2001u);
  EXPECT_EQ(keys[2].local_id, 0x2006u);
  EXPECT_EQ(keys[3].local_id, 0x2004u);
  radicle_free_form_keys(keys);

  ASSERT_EQ(radicle_env_regions_at(env, {"Regions.esp", 0x1000}, -1.0f, 5.0f, &keys, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(keys, nullptr);
  EXPECT_EQ(count, 0u);
  ASSERT_EQ(radicle_env_regions_at(env, {"Regions.esp", 0x1FFF}, 3.0f, 3.0f, &keys, &count), RADICLE_STATUS_OK);
  EXPECT_EQ(count, 0u);
  EXPECT_EQ(radicle_env_regions_at(nullptr, {"Regions.esp", 0x1000}, 3.0f, 3.0f, &keys, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_env_regions_at(env, {"Regions.esp", 0x1000}, 3.0f, 3.0f, nullptr, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_env_destroy(env);
}

std::vector<Sub> polygon_area(std::initializer_list<float> xy) {
  return {{"RPLI", bytes_of({0})}, {"RPLD", floats(xy)}};
}

TEST(RegionEnvironmentTest, RegionsAtTiesFollowTheLaterPluginAndTheOverrideWins) {
  fo4::test::TempDataDir dir;
  dir.write("Tie.esp",
            fo4::test::envnode::make_plugin(
                {}, {top("REGN", {region_with(0x3001, 0x1000, {square_area(0, 0, 10, 10)}, 5),
                                  region_with(0x3002, 0x1000, {square_area(0, 0, 10, 10)}, 5),
                                  region_with(0x3003, 0x1000, {square_area(0, 0, 10, 10)}, 2)})}),
            0);
  dir.write("TieOverride.esp",
            fo4::test::envnode::make_plugin(
                {"Tie.esp"}, {top("REGN", {region_with(0x3003, 0x1000, {square_area(0, 0, 10, 10)}, 5),
                                           region_with(0x3002, 0x1000, {square_area(100, 100, 110, 110)}, 5)})}),
            1);
  const std::vector<std::filesystem::path> ordered{dir.path() / "Tie.esp", dir.path() / "TieOverride.esp"};
  const auto env = fo4::env::Environment::load(ordered);
  const auto k = [](const char* local) { return fo4::FormKey::parse(std::string(local) + ":Tie.esp"); };
  using Keys = std::vector<fo4::FormKey>;

  EXPECT_EQ(fo4::env::regions_at(env, k("001000"), 3, 3), (Keys{k("003003"), k("003001")}));
  EXPECT_EQ(fo4::env::regions_at(env, k("001000"), 105, 105), (Keys{k("003002")}));
}

TEST(RegionEnvironmentTest, RegionsAtHandlesSlantedEdgesAndConcavePolygons) {
  fo4::test::TempDataDir dir;
  dir.write("Shapes.esp",
            fo4::test::envnode::make_plugin(
                {}, {top("REGN", {region_with(0x4001, 0x1000, {polygon_area({0, 0, 10, 10, 0, 10})}, 5),
                                  region_with(0x4002, 0x1000,
                                              {polygon_area({0, 0, 10, 0, 10, 10, 5, 4, 0, 10})}, 5)})}),
            0);
  const std::vector<std::filesystem::path> ordered{dir.path() / "Shapes.esp"};
  const auto env = fo4::env::Environment::load(ordered);
  const auto world = fo4::FormKey::parse("001000:Shapes.esp");
  const auto triangle = fo4::FormKey::parse("004001:Shapes.esp");
  const auto concave = fo4::FormKey::parse("004002:Shapes.esp");
  const auto in = [&](const fo4::FormKey& region, float x, float y) {
    const auto keys = fo4::env::regions_at(env, world, x, y);
    return std::find(keys.begin(), keys.end(), region) != keys.end();
  };

  EXPECT_TRUE(in(triangle, 5, 5));
  EXPECT_TRUE(in(triangle, 4, 6));
  EXPECT_FALSE(in(triangle, 6, 4));
  EXPECT_TRUE(in(triangle, 5, 5.001f));
  EXPECT_FALSE(in(triangle, 5, 4.999f));
  EXPECT_TRUE(in(triangle, 0, 5));
  EXPECT_FALSE(in(triangle, -0.001f, 5));

  EXPECT_TRUE(in(concave, 5, 2));
  EXPECT_TRUE(in(concave, 2, 7));
  EXPECT_TRUE(in(concave, 8, 7));
  EXPECT_TRUE(in(concave, 5, 4));
  EXPECT_TRUE(in(concave, 2.5f, 7));
  EXPECT_FALSE(in(concave, 5, 6));
  EXPECT_FALSE(in(concave, 2, 8));
  EXPECT_FALSE(in(concave, 8, 8));
  EXPECT_FALSE(in(concave, 2.5f, 7.5f));
}

TEST(RegionEnvironmentTest, RegionsAtFailsOnAMalformedRegion) {
  const auto bytes = fo4::test::envnode::make_plugin(
      {}, {top("REGN", {region_with(0x2101, 0x1000, {square_area(0, 0, 10, 10)}, 5),
                        rec("REGN", 0x2102, {{"RPLD", floats({1.0f, 2.0f})}})})});
  fo4::test::TempDataDir dir;
  dir.write("BadAt.esp", bytes, 0);
  const std::string path = (dir.path() / "BadAt.esp").string();
  const char* raw[] = {path.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(raw, 1, &env), RADICLE_STATUS_OK);
  radicle_form_key* keys = nullptr;
  size_t count = 99;
  EXPECT_EQ(radicle_env_regions_at(env, {"BadAt.esp", 0x1000}, 3.0f, 3.0f, &keys, &count),
            RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(keys, nullptr);
  EXPECT_EQ(count, 0u);
  radicle_env_destroy(env);
}

TEST(RegionEnvironmentTest, RejectsMalformedTypedRegionPayloads) {
  const auto bytes = fo4::test::envnode::make_plugin(
      {}, {top("REGN",
               {rec("REGN", 0x0E01, {{"RPLD", floats({1.0f, 2.0f})}}),
                rec("REGN", 0x0E02, {{"RPLI", bytes_of({80})}, {"RPLD", floats({1.0f, 2.0f, 3.0f})}}),
                rec("REGN", 0x0E03, {{"RPLI", {std::byte{0x50}, std::byte{0}}}}),
                rec("REGN", 0x0E04, {{"RDAT", bytes_of({3})}}),
                rec("REGN", 0x0E05, {{"RDAT", bytes_of({2, 0})}, {"RDOT", Bytes(51)}}),
                rec("REGN", 0x0E06, {{"RDAT", bytes_of({3, 0})}, {"RDWT", Bytes(13)}}),
                rec("REGN", 0x0E07, {{"RDAT", bytes_of({6, 0})}, {"RDGS", Bytes(7)}}),
                rec("REGN", 0x0E08, {{"RDAT", bytes_of({7, 0})}, {"RDSA", Bytes(11)}}),
                rec("REGN", 0x0E09, {{"RDGS", Bytes(8)}}),
                rec("REGN", 0x0E0A, {{"RDAT", bytes_of({4, 0})}, {"RDMP", Bytes(2)}}),
                rec("REGN", 0x0E0B, {{"WNAM", Bytes(2)}})}),
           top("CELL", {rec("CELL", 0x0E0C, {})})});
  fo4::test::TempDataDir dir;
  dir.write("Bad.esp", bytes, 0);
  const std::vector<std::filesystem::path> ordered{dir.path() / "Bad.esp"};
  const auto env = fo4::env::Environment::load(ordered);

  for (const auto* local : {"000E01", "000E02", "000E03", "000E04", "000E05", "000E06",
                            "000E07", "000E08", "000E09", "000E0A", "000E0B", "000E0C"}) {
    const auto* ref = env.winner(fo4::FormKey::parse(std::string(local) + ":Bad.esp"));
    ASSERT_NE(ref, nullptr) << local;
    EXPECT_THROW(fo4::env::decode_region(env, *ref), fo4::env::WorldDecodeError) << local;
  }
}

}  // namespace