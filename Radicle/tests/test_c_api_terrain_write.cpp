#include "fo4/c_api.h"
#include "fo4/c_api_terrain.h"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/visit.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

constexpr std::size_t k_side = 33;
constexpr std::size_t k_vertices = k_side * k_side;
constexpr std::size_t k_alpha_side = 17;
constexpr std::size_t k_alpha_count = k_alpha_side * k_alpha_side;
constexpr const char* k_plugin_name = "Land.esp";

radicle_form_key fk(std::uint32_t local) { return radicle_form_key{k_plugin_name, local}; }

Bytes texture_payload(std::uint32_t texture, std::uint8_t quadrant, std::uint16_t layer) {
  ByteWriter out;
  out.u32(texture);
  out.u8(quadrant);
  out.u8(0);
  out.u16(layer);
  return out.bytes;
}

Bytes zstring(std::string_view text) {
  Bytes out;
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  out.push_back(std::byte{0});
  return out;
}

Bytes fixture_bytes() {
  ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto land_group = fo4::test::begin_group(out, "LAND", 0);
  const auto land = fo4::test::begin_record(out, "LAND", 0, 0x800);
  fo4::test::write_subrecord(out, "DATA", Bytes(4));
  fo4::test::write_subrecord(out, "VNML", Bytes(k_vertices * 3));
  fo4::test::write_subrecord(out, "VHGT", Bytes(4 + k_vertices + 3));
  fo4::test::write_subrecord(out, "VCLR", Bytes(k_vertices * 3));
  fo4::test::write_subrecord(out, "BTXT", texture_payload(0x810, 0, 0));
  fo4::test::end_record(out, land);
  fo4::test::end_group(out, land_group);

  const auto ltex_group = fo4::test::begin_group(out, "LTEX", 0);
  const auto ltex = fo4::test::begin_record(out, "LTEX", 0, 0x810);
  fo4::test::write_subrecord(out, "EDID", zstring("Dirt"));
  fo4::test::end_record(out, ltex);
  fo4::test::end_group(out, ltex_group);
  return out.bytes;
}

std::filesystem::path make_root() {
  const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() / ("radicle_c_terrain_write_" + std::to_string(id));
}

struct Fixture {
  std::filesystem::path root{make_root()};
  radicle_plugin* handle{nullptr};

  Fixture() {
    std::filesystem::create_directories(root);
    const auto bytes = fixture_bytes();
    std::ofstream out(root / k_plugin_name, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.close();
    EXPECT_EQ(radicle_plugin_read_file((root / k_plugin_name).string().c_str(), &handle), RADICLE_STATUS_OK);
  }
  ~Fixture() {
    radicle_plugin_destroy(handle);
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }
  Fixture(const Fixture&) = delete;
  Fixture& operator=(const Fixture&) = delete;

  Bytes image() const {
    std::uint8_t* bytes = nullptr;
    std::size_t size = 0;
    EXPECT_EQ(radicle_plugin_write_bytes(handle, &bytes, &size), RADICLE_STATUS_OK);
    Bytes copy(reinterpret_cast<std::byte*>(bytes), reinterpret_cast<std::byte*>(bytes) + size);
    radicle_free_bytes(bytes);
    return copy;
  }
};

std::vector<std::string> signatures(const Fixture& fx, std::uint32_t id) {
  std::vector<std::string> out;
  const auto plugin = fo4::read_plugin(fx.image(), k_plugin_name);
  const auto* record =
      fo4::find_record(plugin, fo4::FormKey{fo4::PluginKey::from_filename(k_plugin_name), fo4::LocalFormId{id}});
  if (record == nullptr) return out;
  for (const auto& sub : record->subrecords) out.push_back(std::string{sub.signature.to_string()});
  return out;
}

std::optional<Bytes> payload(const Fixture& fx, std::uint32_t id, std::string_view signature) {
  const auto plugin = fo4::read_plugin(fx.image(), k_plugin_name);
  const auto* record =
      fo4::find_record(plugin, fo4::FormKey{fo4::PluginKey::from_filename(k_plugin_name), fo4::LocalFormId{id}});
  if (record == nullptr) return std::nullopt;
  for (const auto& sub : record->subrecords) {
    if (std::string{sub.signature.to_string()} == signature) return sub.data;
  }
  return std::nullopt;
}

}  // namespace

TEST(CApiTerrainWrite, SetHeightsReportsStatusAndRefusesBadInput) {
  Fixture fx;
  std::vector<float> heights(k_vertices, 0.0f);
  heights[5] = 16.0f;
  radicle_land_mutation_status status{};
  ASSERT_EQ(radicle_plugin_set_land_heights(fx.handle, fk(0x800), heights.data(), heights.size(), &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_LAND_MUTATION_UPDATED);
  const auto after = fx.image();
  ASSERT_EQ(radicle_plugin_set_land_heights(fx.handle, fk(0x800), heights.data(), heights.size(), &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_LAND_MUTATION_UNCHANGED);
  EXPECT_EQ(fx.image(), after);

  ASSERT_EQ(radicle_plugin_set_land_heights(fx.handle, fk(0x800), heights.data(), heights.size() - 1, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_LAND_MUTATION_SIZE_MISMATCH);
  ASSERT_EQ(radicle_plugin_set_land_heights(fx.handle, fk(0x999), heights.data(), heights.size(), &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_LAND_MUTATION_RECORD_MISSING);
  EXPECT_EQ(radicle_plugin_set_land_heights(fx.handle, fk(0x800), nullptr, 3, &status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_plugin_set_land_heights(nullptr, fk(0x800), heights.data(), heights.size(), &status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(fx.image(), after);
}

TEST(CApiTerrainWrite, SetNormalsAndVertexColorsWriteVectorPayloads) {
  Fixture fx;
  const std::vector<std::uint8_t> normals(k_vertices * 3, 7);
  const std::vector<std::uint8_t> colors(k_vertices * 3, 9);
  radicle_land_mutation_status status{};
  ASSERT_EQ(radicle_plugin_set_land_normals(fx.handle, fk(0x800), normals.data(), normals.size(), &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_LAND_MUTATION_UPDATED);
  ASSERT_EQ(radicle_plugin_set_land_vertex_colors(fx.handle, fk(0x800), colors.data(), colors.size(), &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_LAND_MUTATION_UPDATED);

  const auto vnml = payload(fx, 0x800, "VNML");
  ASSERT_TRUE(vnml.has_value());
  EXPECT_EQ(vnml->size(), normals.size());
  EXPECT_EQ(vnml->front(), std::byte{7});
  const auto vclr = payload(fx, 0x800, "VCLR");
  ASSERT_TRUE(vclr.has_value());
  EXPECT_EQ(vclr->back(), std::byte{9});

  const auto before = fx.image();
  ASSERT_EQ(radicle_plugin_set_land_normals(fx.handle, fk(0x800), normals.data(), normals.size() - 1, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_LAND_MUTATION_SIZE_MISMATCH);
  ASSERT_EQ(radicle_plugin_set_land_vertex_colors(fx.handle, fk(0x811), colors.data(), colors.size(), &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_LAND_MUTATION_RECORD_MISSING);
  EXPECT_EQ(fx.image(), before);
}

TEST(CApiTerrainWrite, SetBaseTextureReportsStatusAndOrdersLayers) {
  Fixture fx;
  radicle_land_mutation_status status{};
  ASSERT_EQ(radicle_plugin_set_land_base_texture(fx.handle, fk(0x800), 2, fk(0x810), 4, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_LAND_MUTATION_UPDATED);
  EXPECT_EQ(signatures(fx, 0x800), (std::vector<std::string>{"DATA", "VNML", "VHGT", "VCLR", "BTXT", "BTXT"}));

  const auto before = fx.image();
  ASSERT_EQ(radicle_plugin_set_land_base_texture(fx.handle, fk(0x800), 2, radicle_form_key{nullptr, 0}, 0, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_LAND_MUTATION_TEXTURE_MISSING);
  ASSERT_EQ(radicle_plugin_set_land_base_texture(fx.handle, fk(0x800), 2, fk(0x800), 0, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_LAND_MUTATION_TEXTURE_MISMATCH);
  ASSERT_EQ(radicle_plugin_set_land_base_texture(fx.handle, fk(0x800), 4, fk(0x810), 0, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_LAND_MUTATION_QUADRANT_INVALID);
  EXPECT_EQ(radicle_plugin_set_land_base_texture(fx.handle, fk(0x800), 0, radicle_form_key{k_plugin_name, 0x1000000},
                                                 0, &status),
            RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(fx.image(), before);
}

TEST(CApiTerrainWrite, AlphaLayersAddReportIndexAndRemoveRefuseOutOfRange) {
  Fixture fx;
  std::vector<float> alpha(k_alpha_count, 0.0f);
  alpha[0] = 0.5f;
  alpha[k_alpha_side * 2 + 5] = 1.0f;
  std::size_t index = 99;
  radicle_land_mutation_status status{};
  ASSERT_EQ(radicle_plugin_add_land_alpha_layer(fx.handle, fk(0x800), 1, fk(0x810), 3, alpha.data(), alpha.size(),
                                                &index, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_LAND_MUTATION_UPDATED);
  EXPECT_EQ(index, 0u);
  ASSERT_EQ(radicle_plugin_add_land_alpha_layer(fx.handle, fk(0x800), 0, fk(0x810), 9, alpha.data(), alpha.size(),
                                                nullptr, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_LAND_MUTATION_UPDATED);
  EXPECT_EQ(signatures(fx, 0x800),
            (std::vector<std::string>{"DATA", "VNML", "VHGT", "VCLR", "BTXT", "ATXT", "VTXT", "ATXT", "VTXT"}));
  EXPECT_EQ(payload(fx, 0x800, "ATXT"), texture_payload(0x810, 1, 3));

  const auto before = fx.image();
  index = 99;
  ASSERT_EQ(radicle_plugin_add_land_alpha_layer(fx.handle, fk(0x800), 1, fk(0x810), 3, alpha.data(), alpha.size() - 1,
                                                &index, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_LAND_MUTATION_SIZE_MISMATCH);
  EXPECT_EQ(index, 99u);
  ASSERT_EQ(radicle_plugin_add_land_alpha_layer(fx.handle, fk(0x800), 1, radicle_form_key{nullptr, 0}, 3,
                                                alpha.data(), alpha.size(), &index, &status),
            RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_LAND_MUTATION_TEXTURE_MISSING);
  EXPECT_EQ(fx.image(), before);

  EXPECT_EQ(radicle_plugin_remove_land_alpha_layer(fx.handle, fk(0x800), 5, &status), RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_LAND_MUTATION_INDEX_OUT_OF_RANGE);
  EXPECT_EQ(radicle_plugin_remove_land_alpha_layer(fx.handle, fk(0x800), 0, &status), RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_LAND_MUTATION_UPDATED);
  EXPECT_EQ(signatures(fx, 0x800), (std::vector<std::string>{"DATA", "VNML", "VHGT", "VCLR", "BTXT", "ATXT", "VTXT"}));
  EXPECT_EQ(radicle_plugin_remove_land_alpha_layer(fx.handle, fk(0x800), 0, &status), RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_LAND_MUTATION_UPDATED);
  EXPECT_EQ(radicle_plugin_remove_land_alpha_layer(fx.handle, fk(0x800), 0, &status), RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_LAND_MUTATION_SUBRECORD_MISSING);
  EXPECT_EQ(radicle_plugin_remove_land_alpha_layer(nullptr, fk(0x800), 0, &status), RADICLE_STATUS_INVALID_ARGUMENT);
}
