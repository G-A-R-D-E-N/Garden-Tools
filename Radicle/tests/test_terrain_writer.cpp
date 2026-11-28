#include "fo4/environment/environment.hpp"
#include "fo4/environment/terrain.hpp"
#include "fo4/environment/terrain_writer.hpp"
#include "fo4/land/heightmap.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
constexpr const char* k_plugin_name = "Land.esp";
constexpr std::size_t k_side = fo4::env::k_land_grid;
constexpr std::size_t k_vertices = fo4::env::k_land_vertices;

struct Sub {
  std::string signature;
  Bytes data;
};

struct Rec {
  std::string signature;
  std::uint32_t form_id;
  std::vector<Sub> subrecords;
};

fo4::FormKey key(std::uint32_t local) {
  return fo4::FormKey{fo4::PluginKey::from_filename(k_plugin_name), fo4::LocalFormId{local}};
}

Bytes zstring(std::string_view text) {
  Bytes out;
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  out.push_back(std::byte{0});
  return out;
}

Bytes words(std::initializer_list<std::uint32_t> values) {
  fo4::test::ByteWriter out;
  for (const auto value : values) out.u32(value);
  return out.bytes;
}

Bytes vhgt(std::uint8_t trailing = 0) {
  fo4::test::ByteWriter out;
  out.f32(0.0f);
  for (std::size_t i = 0; i < k_vertices; ++i) out.u8(0);
  out.u8(0xAA);
  out.u8(0xBB);
  out.u8(trailing);
  return out.bytes;
}

Bytes btxt(std::uint32_t texture, std::uint8_t quadrant) {
  fo4::test::ByteWriter out;
  out.u32(texture);
  out.u8(quadrant);
  out.u8(0);
  out.u16(0);
  return out.bytes;
}

Bytes grid3(std::uint8_t seed) {
  Bytes out(k_vertices * 3);
  for (std::size_t i = 0; i < out.size(); ++i) out[i] = std::byte{static_cast<std::uint8_t>(seed + i)};
  return out;
}

Bytes make_plugin(const std::vector<Rec>& records) {
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
       {{"DATA", words({0})},
        {"VNML", grid3(1)},
        {"VHGT", vhgt(0x33)},
        {"VCLR", grid3(9)},
        {"BTXT", btxt(0x810, 0)}}},
      {"LAND", 0x801, {{"DATA", words({0})}, {"VNML", grid3(2)}}},
      {"LTEX", 0x810, {{"EDID", zstring("Dirt")}}},
      {"LTEX", 0x811, {{"EDID", zstring("Rock")}}},
  };
}

fo4::Plugin read_fixture() {
  auto plugin = fo4::read_plugin(make_plugin(fixture()), k_plugin_name);
  fo4::assign_record_form_keys(plugin);
  return plugin;
}

std::filesystem::path temp_root() {
  const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() / ("radicle_terrain_writer_" + std::to_string(id));
}

struct Saved {
  std::filesystem::path root;
  fo4::env::Environment env;
};

Saved save_and_load(const fo4::Plugin& plugin) {
  const auto root = temp_root();
  std::filesystem::create_directories(root);
  fo4::write_plugin_file(plugin, root / k_plugin_name);
  const std::filesystem::path paths[] = {root / k_plugin_name};
  return Saved{root, fo4::env::Environment::load(paths)};
}

struct RemoveOnExit {
  std::filesystem::path root;
  ~RemoveOnExit() {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
};

std::optional<fo4::env::LandHeightGrid> decoded_heights(const fo4::Plugin& plugin, std::uint32_t id) {
  auto saved = save_and_load(plugin);
  RemoveOnExit cleanup{saved.root};
  const auto* ref = saved.env.winner(key(id));
  if (ref == nullptr) return std::nullopt;
  return fo4::env::decode_land_heights(fo4::env::decode_landscape(saved.env, *ref));
}

Bytes plugin_image(const fo4::Plugin& plugin) { return fo4::write_plugin(plugin); }

std::vector<float> stepped_grid(float base) {
  std::vector<float> out(k_vertices);
  for (std::size_t r = 0; r < k_side; ++r) {
    for (std::size_t c = 0; c < k_side; ++c) {
      const auto steps = static_cast<int>((r * 7 + c * 11) % 41) - 20 + static_cast<int>(r) - static_cast<int>(c) * 2;
      out[r * k_side + c] = base + 8.0f * static_cast<float>(steps);
    }
  }
  return out;
}

}  // namespace

TEST(TerrainWriterHeights, RoundTripsNegativeAndPositiveHeightsExactly) {
  auto plugin = read_fixture();
  const auto heights = stepped_grid(-2048.0f);
  EXPECT_EQ(fo4::set_land_heights(plugin, key(0x800), heights), fo4::LandMutationStatus::updated);
  const auto decoded = decoded_heights(plugin, 0x800);
  ASSERT_TRUE(decoded.has_value());
  for (std::size_t i = 0; i < k_vertices; ++i) EXPECT_EQ(decoded->heights[i], heights[i]) << i;

  const auto positive = stepped_grid(1600.0f);
  EXPECT_EQ(fo4::set_land_heights(plugin, key(0x800), positive), fo4::LandMutationStatus::updated);
  const auto second = decoded_heights(plugin, 0x800);
  ASSERT_TRUE(second.has_value());
  for (std::size_t i = 0; i < k_vertices; ++i) EXPECT_EQ(second->heights[i], positive[i]) << i;
}

TEST(TerrainWriterHeights, SameHeightsReportUnchanged) {
  auto plugin = read_fixture();
  const auto heights = stepped_grid(-512.0f);
  ASSERT_EQ(fo4::set_land_heights(plugin, key(0x800), heights), fo4::LandMutationStatus::updated);
  const auto before = plugin_image(plugin);
  EXPECT_EQ(fo4::set_land_heights(plugin, key(0x800), heights), fo4::LandMutationStatus::unchanged);
  EXPECT_EQ(plugin_image(plugin), before);
}

// Detects: the writer always derived a fresh offset, so a record whose VHGT offset is fractional or
// whose first delta is not zero refused or rewrote the heights it already had.
TEST(TerrainWriterHeights, HeightsARecordAlreadyHasWriteBackUnchanged) {
  auto plugin = read_fixture();
  auto* record = const_cast<fo4::Record*>(fo4::find_record(plugin, key(0x800)));
  ASSERT_NE(record, nullptr);
  fo4::land::LandHeightmap stored;
  stored.offset = 100.5f;
  stored.deltas[0] = 3;
  stored.deltas[1] = -2;
  stored.deltas[k_side] = 5;
  const auto bytes = fo4::land::encode_land_heightmap(stored);
  bool replaced = false;
  for (auto& sub : record->subrecords) {
    if (sub.signature != fo4::Signature::from_string("VHGT")) continue;
    sub.data.assign(bytes.begin(), bytes.end());
    replaced = true;
  }
  ASSERT_TRUE(replaced);
  const auto own = fo4::land::land_heights(stored);
  const auto before = plugin_image(plugin);
  EXPECT_EQ(fo4::set_land_heights(plugin, key(0x800), std::vector<float>(own.begin(), own.end())),
            fo4::LandMutationStatus::unchanged);
  EXPECT_EQ(plugin_image(plugin), before);
}

TEST(TerrainWriterHeights, SteepEdgeAcceptsFullDeltaRangeAndRefusesBeyond) {
  auto plugin = read_fixture();
  std::vector<float> heights(k_vertices, 0.0f);
  heights[1] = 8.0f * 127.0f;
  EXPECT_EQ(fo4::set_land_heights(plugin, key(0x800), heights), fo4::LandMutationStatus::updated);
  const auto decoded = decoded_heights(plugin, 0x800);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->heights[1], 8.0f * 127.0f);

  const auto accepted = plugin_image(plugin);
  heights[1] = 8.0f * 128.0f;
  EXPECT_EQ(fo4::set_land_heights(plugin, key(0x800), heights), fo4::LandMutationStatus::height_out_of_range);
  EXPECT_EQ(plugin_image(plugin), accepted);

  for (std::size_t column = 1; column < k_side; ++column) heights[column] = -8.0f * 128.0f;
  heights[k_side] = 8.0f * -1.0f;
  EXPECT_EQ(fo4::set_land_heights(plugin, key(0x800), heights), fo4::LandMutationStatus::updated);
  const auto settled = plugin_image(plugin);
  heights[2] = 0.0f;
  EXPECT_EQ(fo4::set_land_heights(plugin, key(0x800), heights), fo4::LandMutationStatus::height_out_of_range);
  EXPECT_EQ(plugin_image(plugin), settled);
}

TEST(TerrainWriterHeights, PreservesTrailingBytesOfExistingVhgt) {
  auto plugin = read_fixture();
  ASSERT_EQ(fo4::set_land_heights(plugin, key(0x800), stepped_grid(-64.0f)), fo4::LandMutationStatus::updated);
  const auto reloaded = fo4::read_plugin(plugin_image(plugin), k_plugin_name);
  const auto* record = fo4::find_record(reloaded, key(0x800));
  ASSERT_NE(record, nullptr);
  const auto it = std::find_if(record->subrecords.begin(), record->subrecords.end(),
                               [](const fo4::Subrecord& sub) { return sub.signature == fo4::Signature::from_string("VHGT"); });
  ASSERT_NE(it, record->subrecords.end());
  ASSERT_EQ(it->data.size(), fo4::land::k_vhgt_size);
  EXPECT_EQ(it->data[4 + k_vertices], std::byte{0xAA});
  EXPECT_EQ(it->data[4 + k_vertices + 1], std::byte{0xBB});
  EXPECT_EQ(it->data[4 + k_vertices + 2], std::byte{0x33});
}

TEST(TerrainWriterHeights, RefusalsLeaveBytesUnchanged) {
  auto plugin = read_fixture();
  const auto before = plugin_image(plugin);
  auto heights = stepped_grid(-256.0f);

  EXPECT_EQ(fo4::set_land_heights(plugin, key(0x999), heights), fo4::LandMutationStatus::record_missing);
  EXPECT_EQ(fo4::set_land_heights(plugin, key(0x810), heights), fo4::LandMutationStatus::record_mismatch);
  EXPECT_EQ(fo4::set_land_heights(plugin, key(0x801), heights), fo4::LandMutationStatus::subrecord_missing);

  const std::vector<float> short_grid(k_vertices - 1, 0.0f);
  EXPECT_EQ(fo4::set_land_heights(plugin, key(0x800), short_grid), fo4::LandMutationStatus::size_mismatch);

  heights[3] = std::numeric_limits<float>::quiet_NaN();
  EXPECT_EQ(fo4::set_land_heights(plugin, key(0x800), heights), fo4::LandMutationStatus::value_invalid);

  heights = stepped_grid(-256.0f);
  heights[5] += 3.0f;
  EXPECT_EQ(fo4::set_land_heights(plugin, key(0x800), heights), fo4::LandMutationStatus::height_unrepresentable);

  EXPECT_EQ(plugin_image(plugin), before);
}

namespace {

std::optional<Bytes> subrecord_payload(const fo4::Plugin& plugin, std::uint32_t id, std::string_view signature) {
  const auto reloaded = fo4::read_plugin(plugin_image(plugin), k_plugin_name);
  const auto* record = fo4::find_record(reloaded, key(id));
  if (record == nullptr) return std::nullopt;
  for (const auto& sub : record->subrecords) {
    if (sub.signature == fo4::Signature::from_string(signature)) return sub.data;
  }
  return std::nullopt;
}

}  // namespace

TEST(TerrainWriterVectors, NormalsAndColorsRoundTripAndRefuseBadSizes) {
  auto plugin = read_fixture();
  const auto normals = grid3(5);
  const auto colors = grid3(77);
  EXPECT_EQ(fo4::set_land_normals(plugin, key(0x800), normals), fo4::LandMutationStatus::updated);
  EXPECT_EQ(fo4::set_land_vertex_colors(plugin, key(0x800), colors), fo4::LandMutationStatus::updated);
  EXPECT_EQ(subrecord_payload(plugin, 0x800, "VNML"), normals);
  EXPECT_EQ(subrecord_payload(plugin, 0x800, "VCLR"), colors);
  EXPECT_EQ(fo4::set_land_normals(plugin, key(0x800), normals), fo4::LandMutationStatus::unchanged);
  EXPECT_EQ(fo4::set_land_vertex_colors(plugin, key(0x800), colors), fo4::LandMutationStatus::unchanged);

  const auto before = plugin_image(plugin);
  const Bytes short_grid(k_vertices * 3 - 1, std::byte{1});
  const Bytes long_grid(k_vertices * 3 + 1, std::byte{1});
  EXPECT_EQ(fo4::set_land_normals(plugin, key(0x800), short_grid), fo4::LandMutationStatus::size_mismatch);
  EXPECT_EQ(fo4::set_land_vertex_colors(plugin, key(0x800), long_grid), fo4::LandMutationStatus::size_mismatch);
  EXPECT_EQ(fo4::set_land_vertex_colors(plugin, key(0x801), colors), fo4::LandMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::set_land_normals(plugin, key(0x999), normals), fo4::LandMutationStatus::record_missing);
  EXPECT_EQ(fo4::set_land_normals(plugin, key(0x811), normals), fo4::LandMutationStatus::record_mismatch);
  EXPECT_EQ(plugin_image(plugin), before);
}

namespace {

constexpr std::size_t k_alpha_count = fo4::env::k_land_alpha_grid * fo4::env::k_land_alpha_grid;

std::vector<std::string> signatures_of(const fo4::Plugin& plugin, std::uint32_t id) {
  std::vector<std::string> out;
  const auto reloaded = fo4::read_plugin(plugin_image(plugin), k_plugin_name);
  const auto* record = fo4::find_record(reloaded, key(id));
  if (record == nullptr) return out;
  for (const auto& sub : record->subrecords) out.push_back(std::string{sub.signature.to_string()});
  return out;
}

Bytes texture_payload(std::uint32_t texture, std::uint8_t quadrant, std::uint16_t layer) {
  fo4::test::ByteWriter out;
  out.u32(texture);
  out.u8(quadrant);
  out.u8(0);
  out.u16(layer);
  return out.bytes;
}

Bytes alpha_cell(std::uint16_t cell, float opacity) {
  fo4::test::ByteWriter out;
  out.u16(cell);
  out.u16(0);
  out.f32(opacity);
  return out.bytes;
}

Bytes joined(Bytes first, const Bytes& second) {
  first.insert(first.end(), second.begin(), second.end());
  return first;
}

std::vector<float> alpha_grid(std::initializer_list<std::pair<std::size_t, float>> cells) {
  std::vector<float> out(k_alpha_count, 0.0f);
  for (const auto& [cell, value] : cells) out[cell] = value;
  return out;
}

struct BaseRow {
  std::uint8_t quadrant;
  std::uint16_t layer;
  fo4::FormKey texture;
};

struct AlphaRow {
  std::uint8_t quadrant;
  std::uint16_t layer;
  std::optional<fo4::FormKey> texture;
  std::vector<float> alpha;
};

std::vector<BaseRow> decoded_base_layers(const fo4::Plugin& plugin, std::uint32_t id) {
  std::vector<BaseRow> out;
  auto saved = save_and_load(plugin);
  RemoveOnExit cleanup{saved.root};
  const auto* ref = saved.env.winner(key(id));
  if (ref == nullptr) return out;
  const auto land = fo4::env::decode_landscape(saved.env, *ref);
  for (const auto& layer : land.layers) {
    if (layer.base) out.push_back(BaseRow{layer.quadrant, layer.layer, layer.texture});
  }
  return out;
}

std::vector<AlphaRow> decoded_alpha_layers(const fo4::Plugin& plugin, std::uint32_t id) {
  std::vector<AlphaRow> out;
  auto saved = save_and_load(plugin);
  RemoveOnExit cleanup{saved.root};
  const auto* ref = saved.env.winner(key(id));
  if (ref == nullptr) return out;
  const auto land = fo4::env::decode_landscape(saved.env, *ref);
  for (const auto& layer : fo4::env::decode_layer_alpha(land)) {
    out.push_back(AlphaRow{layer.quadrant, layer.layer, layer.texture,
                           std::vector<float>(layer.alpha.begin(), layer.alpha.end())});
  }
  return out;
}

const std::vector<std::string> k_base_only{"DATA", "VNML", "VHGT", "VCLR", "BTXT"};

}  // namespace

TEST(TerrainWriterBaseTexture, RoundTripsAndReplacesPerQuadrant) {
  auto plugin = read_fixture();
  EXPECT_EQ(fo4::set_land_base_texture(plugin, key(0x800), 0, key(0x811), 0), fo4::LandMutationStatus::updated);
  auto base = decoded_base_layers(plugin, 0x800);
  ASSERT_EQ(base.size(), 1u);
  EXPECT_EQ(base[0].quadrant, 0);
  EXPECT_EQ(base[0].texture, key(0x811));
  EXPECT_EQ(subrecord_payload(plugin, 0x800, "BTXT"), texture_payload(0x811, 0, 0));

  EXPECT_EQ(fo4::set_land_base_texture(plugin, key(0x800), 3, key(0x810), 7), fo4::LandMutationStatus::updated);
  base = decoded_base_layers(plugin, 0x800);
  ASSERT_EQ(base.size(), 2u);
  EXPECT_EQ(base[1].quadrant, 3);
  EXPECT_EQ(base[1].layer, 7);
  EXPECT_EQ(base[1].texture, key(0x810));
  EXPECT_EQ(signatures_of(plugin, 0x800),
            (std::vector<std::string>{"DATA", "VNML", "VHGT", "VCLR", "BTXT", "BTXT"}));
}

TEST(TerrainWriterBaseTexture, InsertsBeforeAlphaLayersAndReportsUnchanged) {
  auto plugin = read_fixture();
  std::size_t index = 0;
  ASSERT_EQ(fo4::add_land_alpha_layer(plugin, key(0x800), 1, key(0x811), 3, alpha_grid({{0, 0.5f}}), &index),
            fo4::LandMutationStatus::updated);
  EXPECT_EQ(fo4::set_land_base_texture(plugin, key(0x800), 1, key(0x810), 0), fo4::LandMutationStatus::updated);
  EXPECT_EQ(signatures_of(plugin, 0x800),
            (std::vector<std::string>{"DATA", "VNML", "VHGT", "VCLR", "BTXT", "BTXT", "ATXT", "VTXT"}));
  const auto before = plugin_image(plugin);
  EXPECT_EQ(fo4::set_land_base_texture(plugin, key(0x800), 1, key(0x810), 0), fo4::LandMutationStatus::unchanged);
  EXPECT_EQ(plugin_image(plugin), before);
}

TEST(TerrainWriterBaseTexture, RefusalsLeaveBytesUnchanged) {
  auto plugin = read_fixture();
  const auto before = plugin_image(plugin);
  EXPECT_EQ(fo4::set_land_base_texture(plugin, key(0x999), 0, key(0x810), 0), fo4::LandMutationStatus::record_missing);
  EXPECT_EQ(fo4::set_land_base_texture(plugin, key(0x811), 0, key(0x810), 0), fo4::LandMutationStatus::record_mismatch);
  EXPECT_EQ(fo4::set_land_base_texture(plugin, key(0x800), 4, key(0x810), 0), fo4::LandMutationStatus::quadrant_invalid);
  EXPECT_EQ(fo4::set_land_base_texture(plugin, key(0x800), 0, fo4::FormKey{}, 0), fo4::LandMutationStatus::texture_missing);
  EXPECT_EQ(fo4::set_land_base_texture(plugin, key(0x800), 0, key(0x800), 0), fo4::LandMutationStatus::texture_mismatch);
  EXPECT_EQ(fo4::set_land_base_texture(plugin, key(0x800), 0, key(0x999), 0), fo4::LandMutationStatus::texture_mismatch);
  EXPECT_EQ(plugin_image(plugin), before);
}

TEST(TerrainWriterAlphaLayers, RoundTripsLayersInOrderWithIndexes) {
  auto plugin = read_fixture();
  const auto first = alpha_grid({{0, 0.5f}, {17 * 2 + 5, 1.0f}});
  std::size_t index = 99;
  ASSERT_EQ(fo4::add_land_alpha_layer(plugin, key(0x800), 1, key(0x811), 3, first, &index),
            fo4::LandMutationStatus::updated);
  EXPECT_EQ(index, 0u);
  const auto second = alpha_grid({{16, 0.25f}});
  ASSERT_EQ(fo4::add_land_alpha_layer(plugin, key(0x800), 0, key(0x810), 9, second, &index),
            fo4::LandMutationStatus::updated);
  EXPECT_EQ(index, 1u);

  EXPECT_EQ(signatures_of(plugin, 0x800),
            (std::vector<std::string>{"DATA", "VNML", "VHGT", "VCLR", "BTXT", "ATXT", "VTXT", "ATXT", "VTXT"}));
  EXPECT_EQ(subrecord_payload(plugin, 0x800, "ATXT"), texture_payload(0x811, 1, 3));
  EXPECT_EQ(subrecord_payload(plugin, 0x800, "VTXT"), joined(alpha_cell(0, 0.5f), alpha_cell(39, 1.0f)));

  const auto layers = decoded_alpha_layers(plugin, 0x800);
  ASSERT_EQ(layers.size(), 2u);
  EXPECT_EQ(layers[0].quadrant, 1);
  EXPECT_EQ(layers[0].layer, 3);
  ASSERT_TRUE(layers[0].texture.has_value());
  EXPECT_EQ(*layers[0].texture, key(0x811));
  EXPECT_EQ(layers[0].alpha, first);
  EXPECT_EQ(layers[1].quadrant, 0);
  EXPECT_EQ(layers[1].layer, 9);
  EXPECT_EQ(layers[1].alpha, second);
}

TEST(TerrainWriterAlphaLayers, RemoveDropsOneLayerWithItsAlphaAndKeepsBaseLayers) {
  auto plugin = read_fixture();
  const auto first = alpha_grid({{0, 0.5f}, {17 * 2 + 5, 1.0f}});
  const auto second = alpha_grid({{16, 0.25f}});
  ASSERT_EQ(fo4::add_land_alpha_layer(plugin, key(0x800), 1, key(0x811), 3, first), fo4::LandMutationStatus::updated);
  ASSERT_EQ(fo4::add_land_alpha_layer(plugin, key(0x800), 0, key(0x810), 9, second), fo4::LandMutationStatus::updated);

  EXPECT_EQ(fo4::remove_land_alpha_layer(plugin, key(0x800), 0), fo4::LandMutationStatus::updated);
  const auto layers = decoded_alpha_layers(plugin, 0x800);
  ASSERT_EQ(layers.size(), 1u);
  EXPECT_EQ(layers[0].layer, 9);
  EXPECT_EQ(layers[0].alpha, second);
  EXPECT_EQ(signatures_of(plugin, 0x800),
            (std::vector<std::string>{"DATA", "VNML", "VHGT", "VCLR", "BTXT", "ATXT", "VTXT"}));

  EXPECT_EQ(fo4::remove_land_alpha_layer(plugin, key(0x800), 1), fo4::LandMutationStatus::index_out_of_range);
  EXPECT_EQ(fo4::remove_land_alpha_layer(plugin, key(0x800), 0), fo4::LandMutationStatus::updated);
  EXPECT_EQ(signatures_of(plugin, 0x800), k_base_only);
  EXPECT_EQ(decoded_base_layers(plugin, 0x800).size(), 1u);

  EXPECT_EQ(fo4::remove_land_alpha_layer(plugin, key(0x800), 0), fo4::LandMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::remove_land_alpha_layer(plugin, key(0x801), 0), fo4::LandMutationStatus::subrecord_missing);
}

TEST(TerrainWriterAlphaLayers, RefusalsLeaveBytesAndIndexUnchanged) {
  auto plugin = read_fixture();
  const auto before = plugin_image(plugin);
  const auto valid = alpha_grid({{0, 0.5f}});
  std::size_t index = 99;
  EXPECT_EQ(fo4::add_land_alpha_layer(plugin, key(0x999), 0, key(0x810), 0, valid, &index),
            fo4::LandMutationStatus::record_missing);
  EXPECT_EQ(fo4::add_land_alpha_layer(plugin, key(0x811), 0, key(0x810), 0, valid, &index),
            fo4::LandMutationStatus::record_mismatch);
  EXPECT_EQ(fo4::add_land_alpha_layer(plugin, key(0x800), 4, key(0x810), 0, valid, &index),
            fo4::LandMutationStatus::quadrant_invalid);

  const std::vector<float> short_alpha(k_alpha_count - 1, 0.0f);
  EXPECT_EQ(fo4::add_land_alpha_layer(plugin, key(0x800), 0, key(0x810), 0, short_alpha, &index),
            fo4::LandMutationStatus::size_mismatch);

  auto not_a_number = valid;
  not_a_number[3] = std::numeric_limits<float>::quiet_NaN();
  EXPECT_EQ(fo4::add_land_alpha_layer(plugin, key(0x800), 0, key(0x810), 0, not_a_number, &index),
            fo4::LandMutationStatus::value_invalid);
  auto negative = valid;
  negative[3] = -0.1f;
  EXPECT_EQ(fo4::add_land_alpha_layer(plugin, key(0x800), 0, key(0x810), 0, negative, &index),
            fo4::LandMutationStatus::value_invalid);
  auto over = valid;
  over[3] = 1.5f;
  EXPECT_EQ(fo4::add_land_alpha_layer(plugin, key(0x800), 0, key(0x810), 0, over, &index),
            fo4::LandMutationStatus::value_invalid);

  // Detects: a layer covering no cell was written as an empty VTXT.
  const std::vector<float> covers_nothing(k_alpha_count, 0.0f);
  EXPECT_EQ(fo4::add_land_alpha_layer(plugin, key(0x800), 0, key(0x810), 0, covers_nothing, &index),
            fo4::LandMutationStatus::value_invalid);

  EXPECT_EQ(fo4::add_land_alpha_layer(plugin, key(0x800), 0, fo4::FormKey{}, 0, valid, &index),
            fo4::LandMutationStatus::texture_missing);
  EXPECT_EQ(fo4::add_land_alpha_layer(plugin, key(0x800), 0, key(0x800), 0, valid, &index),
            fo4::LandMutationStatus::texture_mismatch);
  EXPECT_EQ(index, 0u);
  EXPECT_EQ(plugin_image(plugin), before);

  EXPECT_EQ(fo4::remove_land_alpha_layer(plugin, key(0x999), 0), fo4::LandMutationStatus::record_missing);
  EXPECT_EQ(fo4::remove_land_alpha_layer(plugin, key(0x800), 0), fo4::LandMutationStatus::subrecord_missing);
  EXPECT_EQ(plugin_image(plugin), before);
}

TEST(TerrainWriterAlphaLayers, NewLayersInsertBeforeVtexWhenNoLayersExist) {
  auto plugin = fo4::read_plugin(make_plugin({
                                     {"LAND", 0x802, {{"DATA", words({0})}, {"VTEX", words({0x810})}}},
                                     {"LTEX", 0x810, {{"EDID", zstring("Dirt")}}},
                                 }),
                                 k_plugin_name);
  fo4::assign_record_form_keys(plugin);
  EXPECT_EQ(fo4::set_land_base_texture(plugin, key(0x802), 0, key(0x810), 0), fo4::LandMutationStatus::updated);
  EXPECT_EQ(signatures_of(plugin, 0x802), (std::vector<std::string>{"DATA", "BTXT", "VTEX"}));

  std::size_t index = 99;
  EXPECT_EQ(fo4::add_land_alpha_layer(plugin, key(0x802), 1, key(0x810), 3, alpha_grid({{0, 0.5f}}), &index),
            fo4::LandMutationStatus::updated);
  EXPECT_EQ(index, 0u);
  EXPECT_EQ(signatures_of(plugin, 0x802), (std::vector<std::string>{"DATA", "BTXT", "ATXT", "VTXT", "VTEX"}));
}
