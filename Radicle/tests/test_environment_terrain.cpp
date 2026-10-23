#include "fo4/c_api_env.h"
#include "fo4/c_api_terrain.h"
#include "fo4/environment/terrain.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct Sub {
  std::string signature;
  std::vector<std::byte> data;
};

struct Rec {
  std::string signature;
  std::uint32_t form_id{0};
  std::vector<Sub> subrecords;
};

std::vector<std::byte> words(std::initializer_list<std::uint32_t> values) {
  fo4::test::ByteWriter out;
  for (const auto value : values) out.u32(value);
  return out.bytes;
}

std::vector<std::byte> zstring(std::string_view text) {
  std::vector<std::byte> out;
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  out.push_back(std::byte{0});
  return out;
}

std::vector<std::byte> layer(std::uint32_t texture, std::uint8_t quadrant, std::uint16_t number) {
  fo4::test::ByteWriter out;
  out.u32(texture);
  out.u8(quadrant);
  out.u8(0);
  out.u16(number);
  return out.bytes;
}

std::vector<std::byte> heights() {
  fo4::test::ByteWriter out;
  out.f32(-1024.0f);
  for (std::size_t i = 0; i < fo4::env::k_land_vertices; ++i) out.u8(static_cast<std::uint8_t>(i));
  out.u8(0);
  out.u8(0);
  out.u8(0);
  return out.bytes;
}

std::vector<std::byte> grid3(std::uint8_t seed) {
  std::vector<std::byte> out(fo4::env::k_land_vertices * 3);
  for (std::size_t i = 0; i < out.size(); ++i) out[i] = std::byte{static_cast<std::uint8_t>(seed + i)};
  return out;
}

std::vector<std::byte> alpha_entries() {
  fo4::test::ByteWriter out;
  out.u16(5);
  out.u16(0);
  out.f32(0.5f);
  out.u16(288);
  out.u16(0);
  out.f32(1.0f);
  return out.bytes;
}

std::vector<std::byte> decal() {
  fo4::test::ByteWriter out;
  for (const float value : {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f}) out.f32(value);
  out.u8(4);
  out.u8(0);
  out.u16(0);
  out.u32(0xFFFFFFFF);
  return out.bytes;
}

std::vector<std::byte> make_plugin(const std::vector<Rec>& records) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  for (const auto& record : records) {
    const auto group = fo4::test::begin_group(out, record.signature, 0);
    const auto header = fo4::test::begin_record(out, record.signature, 0, record.form_id);
    for (const auto& sub : record.subrecords) fo4::test::write_subrecord(out, sub.signature, sub.data);
    fo4::test::end_record(out, header);
    fo4::test::end_group(out, group);
  }
  return out.bytes;
}

std::vector<Rec> fixture() {
  return {
      {"LAND", 0x800,
       {{"DATA", words({0x7})},
        {"VNML", grid3(1)},
        {"VHGT", heights()},
        {"VCLR", grid3(9)},
        {"BTXT", layer(0x810, 0, 0xFFFF)},
        {"ATXT", layer(0x811, 2, 1)},
        {"VTXT", alpha_entries()},
        {"ATXT", layer(0x812, 3, 2)}}},
      {"LAND", 0x801, {{"VTXT", alpha_entries()}}},
      {"LAND", 0x802, {{"VHGT", words({0, 0})}}},
      {"LAND", 0x803, {{"BTXT", layer(0x810, 4, 0)}}},
      {"LAND", 0x804, {{"BTXT", layer(0x810, 0, 0)}, {"VTXT", alpha_entries()}}},
      {"LTEX", 0x810, {{"EDID", zstring("Dirt")}, {"TNAM", words({0x820})}}},
      {"LTEX", 0x811, {{"EDID", zstring("NoSet")}}},
      {"LTEX", 0x812, {{"TNAM", words({0x821})}}},
      {"TXST", 0x820,
       {{"TX00", zstring("Landscape\\Dirt01_d.dds")},
        {"TX01", zstring("Landscape\\Dirt01_n.dds")},
        {"DODT", decal()},
        {"DNAM", {std::byte{1}, std::byte{0}}},
        {"MNAM", zstring("Landscape\\Dirt01.BGSM")}}},
      {"TXST", 0x821, {{"TX01", zstring("OnlyNormal_n.dds")}}},
  };
}

class TerrainEnvironmentTest : public ::testing::Test {
protected:
  void SetUp() override {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() / ("radicle_terrain_" + std::to_string(id));
    std::filesystem::create_directories(root_);
    path_ = root_ / "Terrain.esp";
    const auto data = make_plugin(fixture());
    std::ofstream stream(path_, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }

  std::filesystem::path root_;
  std::filesystem::path path_;
};

fo4::FormKey key(std::uint32_t local) {
  return fo4::FormKey{fo4::PluginKey::from_filename("Terrain.esp"), fo4::LocalFormId{local}};
}

using Alpha = std::pair<std::uint16_t, float>;

std::vector<std::byte> vtxt_entries(const std::vector<Alpha>& entries) {
  fo4::test::ByteWriter out;
  for (const auto& [position, opacity] : entries) {
    out.u16(position);
    out.u16(0);
    out.f32(opacity);
  }
  return out.bytes;
}

std::vector<Alpha> full_grid(float opacity) {
  std::vector<Alpha> out;
  for (std::uint16_t position = 0; position < 289; ++position) out.emplace_back(position, opacity);
  return out;
}

fo4::env::LandLayer land_layer(bool base, std::uint32_t texture, std::uint8_t quadrant,
                               const std::vector<std::byte>& alpha) {
  return fo4::env::LandLayer{base, key(texture), quadrant, 0, alpha};
}

fo4::env::LandscapeData landscape(std::vector<fo4::env::LandLayer> layers) {
  fo4::env::LandscapeData land;
  land.layers = std::move(layers);
  return land;
}

float weight_of(const fo4::env::LandVertexWeights& blend, std::uint32_t texture, std::size_t x, std::size_t y) {
  for (const auto& [owner, weights] : blend.layers) {
    if (owner == key(texture)) return weights[y * fo4::env::k_land_grid + x];
  }
  return 0.0f;
}

}  // namespace

TEST(TerrainLand, IntegratesDeltasWithRowStartCarry) {
  std::vector<std::byte> deltas(fo4::env::k_land_vertices, std::byte{0});
  deltas[0] = std::byte{0xFF};
  deltas[1] = std::byte{1};
  deltas[33] = std::byte{3};
  deltas[34] = std::byte{0x80};
  deltas[66] = std::byte{0x7F};
  fo4::env::LandscapeData land;
  land.height_offset = 2.0f;
  land.heights = deltas;
  const auto grid = fo4::env::decode_land_heights(land);
  ASSERT_TRUE(grid.has_value());
  EXPECT_EQ(grid->base, 2.0f);
  EXPECT_EQ(grid->heights[0], 8.0f);
  EXPECT_EQ(grid->heights[1], 16.0f);
  EXPECT_EQ(grid->heights[2], 16.0f);
  EXPECT_EQ(grid->heights[33], 32.0f);
  EXPECT_EQ(grid->heights[34], -992.0f);
  EXPECT_EQ(grid->heights[66], 1048.0f);
  EXPECT_FALSE(fo4::env::decode_land_heights(fo4::env::LandscapeData{}).has_value());
}

TEST(TerrainLand, MapsAlphaPositionsOntoTheSeventeenGrid) {
  const auto alpha = vtxt_entries({{20, 0.5f}, {288, 1.0f}, {5, 1.5f}, {6, -0.25f}, {17, 0.75f}, {7, std::nanf("")}});
  const auto layers = fo4::env::decode_layer_alpha(landscape({land_layer(false, 0x811, 2, alpha)}));
  ASSERT_EQ(layers.size(), 1u);
  EXPECT_EQ(layers[0].quadrant, 2);
  EXPECT_EQ(layers[0].texture, key(0x811));
  EXPECT_EQ(layers[0].alpha[5], 1.0f);
  EXPECT_EQ(layers[0].alpha[6], 0.0f);
  EXPECT_EQ(layers[0].alpha[7], 0.0f);
  EXPECT_EQ(layers[0].alpha[17], 0.75f);
  EXPECT_EQ(layers[0].alpha[20], 0.5f);
  EXPECT_EQ(layers[0].alpha[288], 1.0f);
}

TEST(TerrainLand, SkipsAlphaPositionsOutsideTheGrid) {
  const auto alpha = vtxt_entries({{289, 0.5f}, {0xFFFF, 0.5f}, {0, 0.25f}});
  const auto layers = fo4::env::decode_layer_alpha(landscape({land_layer(false, 0x811, 0, alpha)}));
  ASSERT_EQ(layers.size(), 1u);
  EXPECT_EQ(layers[0].alpha[0], 0.25f);
  EXPECT_EQ(layers[0].alpha[1], 0.0f);
  EXPECT_EQ(layers[0].alpha[288], 0.0f);
}

TEST(TerrainLand, BlendMergesQuadrantEdgesIntoSharedVertices) {
  const auto quarter = vtxt_entries(full_grid(0.25f));
  const auto half = vtxt_entries(full_grid(0.5f));
  const auto blend = fo4::env::blend_cell_weights(landscape({
      land_layer(true, 0x810, 0, {}),
      land_layer(false, 0x811, 0, quarter),
      land_layer(true, 0x810, 1, {}),
      land_layer(false, 0x811, 1, half),
  }));
  EXPECT_FLOAT_EQ(weight_of(blend, 0x811, 5, 5), 0.25f);
  EXPECT_FLOAT_EQ(weight_of(blend, 0x811, 16, 5), 0.375f);
  EXPECT_FLOAT_EQ(weight_of(blend, 0x811, 5, 16), 0.125f);
  EXPECT_FLOAT_EQ(weight_of(blend, 0x811, 16, 16), 0.1875f);
  EXPECT_FLOAT_EQ(weight_of(blend, 0x811, 5, 20), 0.0f);
}

TEST(TerrainLand, BlendGivesGroundTheRemainderAndNormalisesOverfullVertices) {
  const auto three_quarters = vtxt_entries(full_grid(0.75f));
  const auto quarter = vtxt_entries(full_grid(0.25f));
  const auto blend = fo4::env::blend_cell_weights(landscape({
      land_layer(true, 0x810, 0, {}),
      land_layer(false, 0x811, 0, three_quarters),
      land_layer(false, 0x812, 0, three_quarters),
      land_layer(true, 0x810, 2, {}),
      land_layer(false, 0x811, 2, quarter),
  }));
  EXPECT_FLOAT_EQ(weight_of(blend, 0x811, 3, 3), 0.5f);
  EXPECT_FLOAT_EQ(weight_of(blend, 0x812, 3, 3), 0.5f);
  EXPECT_FLOAT_EQ(weight_of(blend, 0x810, 3, 3), 0.0f);
  EXPECT_FLOAT_EQ(weight_of(blend, 0x810, 3, 19), 0.75f);
  EXPECT_FLOAT_EQ(weight_of(blend, 0x811, 3, 19), 0.25f);
  for (std::size_t vertex = 0; vertex < fo4::env::k_land_vertices; ++vertex) {
    float total = 0.0f;
    for (const auto& [texture, weights] : blend.layers) total += weights[vertex];
    EXPECT_LE(total, 1.0f + 1e-5f) << vertex;
  }
}

TEST(TerrainLand, BaseTextureIsTheFirstBaseLayerOfEachQuadrant) {
  const auto quarter = vtxt_entries(full_grid(0.25f));
  const auto half = vtxt_entries(full_grid(0.5f));
  const auto blend = fo4::env::blend_cell_weights(landscape({
      land_layer(true, 0x810, 0, {}),
      land_layer(true, 0x812, 0, {}),
      land_layer(false, 0x811, 0, quarter),
      land_layer(false, 0x811, 1, half),
      land_layer(false, 0x811, 3, quarter),
      land_layer(true, 0x812, 3, {}),
  }));
  ASSERT_TRUE(blend.base[0].has_value());
  EXPECT_EQ(*blend.base[0], key(0x810));
  EXPECT_FALSE(blend.base[1].has_value());
  ASSERT_TRUE(blend.base[3].has_value());
  EXPECT_EQ(*blend.base[3], key(0x812));
  EXPECT_FLOAT_EQ(weight_of(blend, 0x812, 3, 3), 0.0f);
  EXPECT_FLOAT_EQ(weight_of(blend, 0x812, 20, 20), 0.75f);
}

TEST_F(TerrainEnvironmentTest, DecodesLandscapeGridsAndLayers) {
  const std::vector<std::filesystem::path> paths{path_};
  const auto env = fo4::env::Environment::load(paths);
  const auto land = fo4::env::decode_landscape(env, *env.winner(key(0x800)));
  EXPECT_EQ(land.flags, std::optional<std::uint32_t>{7});
  EXPECT_EQ(land.height_offset, -1024.0f);
  ASSERT_EQ(land.heights.size(), fo4::env::k_land_vertices);
  EXPECT_EQ(land.heights[34], std::byte{34});
  ASSERT_EQ(land.normals.size(), fo4::env::k_land_vertices * 3);
  EXPECT_EQ(land.normals[0], std::byte{1});
  EXPECT_EQ(land.colors[2], std::byte{11});

  ASSERT_EQ(land.layers.size(), 3u);
  EXPECT_TRUE(land.layers[0].base);
  EXPECT_EQ(land.layers[0].layer, 0xFFFF);
  EXPECT_TRUE(land.layers[0].alpha.empty());
  EXPECT_FALSE(land.layers[1].base);
  EXPECT_EQ(land.layers[1].texture, key(0x811));
  EXPECT_EQ(land.layers[1].quadrant, 2);
  EXPECT_EQ(land.layers[1].alpha.size(), 16u);
  EXPECT_TRUE(land.layers[2].alpha.empty());
}

TEST_F(TerrainEnvironmentTest, RejectsMalformedLandscapes) {
  const std::vector<std::filesystem::path> paths{path_};
  const auto env = fo4::env::Environment::load(paths);
  for (const std::uint32_t local : {0x801u, 0x802u, 0x803u, 0x804u}) {
    EXPECT_THROW(fo4::env::decode_landscape(env, *env.winner(key(local))), fo4::env::WorldDecodeError)
        << std::hex << local;
  }
  EXPECT_THROW(fo4::env::decode_landscape(env, *env.winner(key(0x810))), fo4::env::WorldDecodeError);
}

TEST_F(TerrainEnvironmentTest, ResolvesLandscapeTextureChain) {
  const std::vector<std::filesystem::path> paths{path_};
  const auto env = fo4::env::Environment::load(paths);
  EXPECT_EQ(fo4::env::decode_landscape_texture_set(env, *env.winner(key(0x810))), key(0x820));
  EXPECT_FALSE(fo4::env::decode_landscape_texture_set(env, *env.winner(key(0x811))).has_value());

  const auto set = fo4::env::decode_texture_set(env, *env.winner(key(0x820)));
  EXPECT_EQ(set.textures[0], "Landscape\\Dirt01_d.dds");
  EXPECT_EQ(set.textures[1], "Landscape\\Dirt01_n.dds");
  EXPECT_FALSE(set.textures[2].has_value());
  EXPECT_EQ(set.material, "Landscape\\Dirt01.BGSM");
  EXPECT_EQ(set.flags, std::optional<std::uint16_t>{1});
  ASSERT_TRUE(set.decal.has_value());
  EXPECT_EQ(set.decal->min_width, 1.0f);
  EXPECT_EQ(set.decal->min_height, 3.0f);

  EXPECT_EQ(fo4::env::landscape_texture_diffuse(env, key(0x810)), "Landscape\\Dirt01_d.dds");
  EXPECT_FALSE(fo4::env::landscape_texture_diffuse(env, key(0x811)).has_value());
  EXPECT_FALSE(fo4::env::landscape_texture_diffuse(env, key(0x812)).has_value());
  EXPECT_FALSE(fo4::env::landscape_texture_diffuse(env, key(0x999)).has_value());
}

TEST_F(TerrainEnvironmentTest, CApiExposesTerrain) {
  const auto utf8 = path_.u8string();
  const std::string path(utf8.begin(), utf8.end());
  const char* raw[] = {path.c_str()};
  radicle_env* env = nullptr;
  ASSERT_EQ(radicle_env_load(raw, 1, &env), RADICLE_STATUS_OK) << radicle_last_error();

  const radicle_record* record = nullptr;
  ASSERT_EQ(radicle_env_winner(env, {"Terrain.esp", 0x800}, &record), RADICLE_STATUS_OK);
  radicle_landscape* land = nullptr;
  ASSERT_EQ(radicle_landscape_decode(env, record, &land), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(land->has_heights, 1);
  EXPECT_EQ(land->height_offset, -1024.0f);
  EXPECT_EQ(land->heights[1088], static_cast<std::uint8_t>(1088 & 0xFF));
  EXPECT_NE(land->normals, nullptr);
  ASSERT_EQ(land->layer_count, 3u);
  EXPECT_EQ(land->layers[0].is_base, 1);
  EXPECT_EQ(land->layers[0].alpha, nullptr);
  EXPECT_EQ(land->layers[1].alpha_size, 16u);
  EXPECT_EQ(land->layers[1].texture.local_id, 0x811u);
  radicle_landscape_free(land);

  radicle_land_heights heights{};
  ASSERT_EQ(radicle_landscape_heights(env, record, &heights), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(heights.has_heights, 1);
  EXPECT_EQ(heights.base, -1024.0f);
  EXPECT_EQ(heights.heights[0], -8192.0f);
  EXPECT_EQ(heights.heights[1], -8184.0f);

  radicle_land_blend* blend = nullptr;
  ASSERT_EQ(radicle_landscape_blend(env, record, &blend), RADICLE_STATUS_OK) << radicle_last_error();
  EXPECT_EQ(blend->base[0].local_id, 0x810u);
  ASSERT_EQ(blend->layer_count, 2u);
  EXPECT_EQ(blend->layers[0].texture.local_id, 0x810u);
  EXPECT_FLOAT_EQ(blend->layers[0].weights[0], 1.0f);
  EXPECT_EQ(blend->layers[1].texture.local_id, 0x811u);
  EXPECT_FLOAT_EQ(blend->layers[1].weights[16 * 33 + 5], 0.25f);
  radicle_landscape_blend_free(blend);
  radicle_landscape_blend_free(nullptr);

  ASSERT_EQ(radicle_env_winner(env, {"Terrain.esp", 0x801}, &record), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_landscape_decode(env, record, &land), RADICLE_STATUS_PARSE_ERROR);
  EXPECT_EQ(land, nullptr);

  ASSERT_EQ(radicle_env_winner(env, {"Terrain.esp", 0x810}, &record), RADICLE_STATUS_OK);
  radicle_form_key set{};
  ASSERT_EQ(radicle_landscape_texture_get_set(env, record, &set), RADICLE_STATUS_OK);
  EXPECT_EQ(set.local_id, 0x820u);

  ASSERT_EQ(radicle_env_winner(env, {"Terrain.esp", 0x820}, &record), RADICLE_STATUS_OK);
  radicle_texture_set_info info{};
  ASSERT_EQ(radicle_texture_set_get_info(env, record, &info), RADICLE_STATUS_OK);
  EXPECT_STREQ(info.textures[0], "Landscape\\Dirt01_d.dds");
  EXPECT_EQ(info.textures[3], nullptr);
  EXPECT_STREQ(info.material, "Landscape\\Dirt01.BGSM");
  EXPECT_EQ(info.decal_count, 6u);
  EXPECT_EQ(info.decal[5], 6.0f);

  const char* diffuse = nullptr;
  ASSERT_EQ(radicle_env_landscape_texture_diffuse(env, {"Terrain.esp", 0x810}, &diffuse),
            RADICLE_STATUS_OK);
  EXPECT_STREQ(diffuse, "Landscape\\Dirt01_d.dds");
  EXPECT_EQ(radicle_env_landscape_texture_diffuse(env, {"Terrain.esp", 0x811}, &diffuse),
            RADICLE_STATUS_NOT_FOUND);
  radicle_landscape_free(nullptr);
  radicle_env_destroy(env);
}
