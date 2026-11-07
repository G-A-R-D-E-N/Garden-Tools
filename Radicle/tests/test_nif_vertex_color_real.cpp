#include "fo4/archive/assets.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_write.hpp"
#include "nif_fixture.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

using fo4::nif::apply_vertex_color;
using fo4::nif::VertexColorKind;
using fo4::nif::VertexColorOp;

bool has_extension(const std::string& path, std::string_view extension) {
  return std::filesystem::path(path).extension() == extension;
}

std::uint64_t colour_offset_units(std::uint64_t descriptor) {
  const auto flags = static_cast<std::uint32_t>(descriptor >> 44U);
  std::uint64_t bytes = (flags & nif_fixture::k_full) != 0 ? 16 : 8;
  for (const std::uint32_t bit :
       {nif_fixture::k_has_uv, nif_fixture::k_has_uv2, nif_fixture::k_has_normal, nif_fixture::k_has_tangent}) {
    if ((flags & bit) != 0) bytes += 4;
  }
  return bytes / 4;
}

bool corpus_open(const char*& reason) {
  if (std::getenv("FO4_DATA_DIR") == nullptr) {
    reason = "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
    return false;
  }
  reason = nullptr;
  return true;
}

}  // namespace

TEST(NifVertexColorRealData, FillOnVanillaColouredMeshKeepsBlockSizeAndGeometry) {
  const char* reason = nullptr;
  if (!corpus_open(reason)) GTEST_SKIP() << reason;
  const char* dir = std::getenv("FO4_DATA_DIR");
  record_game_build(detect_game_build(dir));
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  const auto assets = fo4::archive::AssetResolver::open({}, archives);

  std::size_t coloured = 0;
  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size(); ++i) {
      const auto& path = reader.entries()[i].path;
      if (!has_extension(path, ".nif")) continue;
      const auto file = reader.extract(i);
      fo4::nif::NifFile nif;
      try {
        nif = fo4::nif::read_nif(file);
      } catch (const fo4::parse_error&) {
        continue;
      }
      for (const auto& shape : fo4::nif::read_shapes(nif)) {
        const auto& type = nif.blocks()[shape.block].type;
        if (type != "BSTriShape" && type != "BSSubIndexTriShape") continue;
        if (shape.colors.size() < 4) continue;
        ++coloured;
        VertexColorOp op;
        op.kind = VertexColorKind::fill;
        op.block = shape.block;
        op.color = {shape.colors[0], shape.colors[1], shape.colors[2], shape.colors[3]};
        std::vector<std::byte> out;
        try {
          out = apply_vertex_color(nif, op);
        } catch (const fo4::parse_error& error) {
          ADD_FAILURE() << path << ": colour fill failed: " << error.what();
          continue;
        }
        const auto out_nif = fo4::nif::read_nif(out);
        const auto after = fo4::nif::read_shapes(out_nif);
        const auto match = std::find_if(after.begin(), after.end(),
                                        [&shape](const fo4::nif::Shape& item) { return item.block == shape.block; });
        ASSERT_NE(match, after.end()) << path;
        EXPECT_EQ(out_nif.blocks()[shape.block].size, nif.blocks()[shape.block].size)
            << path << ": colour fill changed the block size, so segment or partition data moved";
        EXPECT_EQ(out.size(), file.size()) << path;
        EXPECT_TRUE(nif_fixture::same_floats(match->verts, shape.verts)) << path;
        EXPECT_TRUE(nif_fixture::same_floats(match->uvs, shape.uvs)) << path;
        EXPECT_EQ(match->tris, shape.tris) << path;
        ASSERT_EQ(match->colors.size(), shape.colors.size()) << path;
        for (std::size_t v = 0; v < match->colors.size(); ++v) {
          EXPECT_EQ(match->colors[v], op.color[v % 4]) << path << ": colour value " << v;
        }
      }
    }
  }
  if (coloured == 0) GTEST_SKIP() << "the sample corpus contains no coloured shapes";
}

TEST(NifVertexColorRealData, EnableOnVanillaMeshWithoutColoursAddsWhiteAndKeepsGeometry) {
  const char* reason = nullptr;
  if (!corpus_open(reason)) GTEST_SKIP() << reason;
  const char* dir = std::getenv("FO4_DATA_DIR");
  record_game_build(detect_game_build(dir));
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  const auto assets = fo4::archive::AssetResolver::open({}, archives);

  std::size_t plain = 0;
  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size(); ++i) {
      const auto& path = reader.entries()[i].path;
      if (!has_extension(path, ".nif")) continue;
      const auto file = reader.extract(i);
      fo4::nif::NifFile nif;
      try {
        nif = fo4::nif::read_nif(file);
      } catch (const fo4::parse_error&) {
        continue;
      }
      for (const auto& shape : fo4::nif::read_shapes(nif)) {
        const auto& type = nif.blocks()[shape.block].type;
        if (type != "BSTriShape" && type != "BSSubIndexTriShape") continue;
        if (!shape.colors.empty() || shape.verts.empty()) continue;
        ++plain;
        VertexColorOp op;
        op.kind = VertexColorKind::enable;
        op.block = shape.block;
        const auto before_descriptor = nif_fixture::descriptor_of(file, nif, shape.block);
        std::vector<std::byte> out;
        try {
          out = apply_vertex_color(nif, op);
        } catch (const fo4::parse_error& error) {
          ADD_FAILURE() << path << ": colour enable failed: " << error.what();
          continue;
        }
        const auto out_nif = fo4::nif::read_nif(out);
        const auto descriptor = nif_fixture::descriptor_of(out, out_nif, shape.block);
        EXPECT_EQ((descriptor >> 24U) & 0xFU, colour_offset_units(descriptor)) << path;
        for (std::size_t index = 1; index <= 9; ++index) {
          if (index == 6) continue;
          const auto before_nibble = (before_descriptor >> (4U * index)) & 0xFU;
          const auto moved = index >= 7 && before_nibble >= colour_offset_units(before_descriptor);
          EXPECT_EQ((descriptor >> (4U * index)) & 0xFU, moved ? before_nibble + 1 : before_nibble)
              << path << " nibble " << index;
        }
        const auto after = fo4::nif::read_shapes(out_nif);
        const auto match = std::find_if(after.begin(), after.end(),
                                        [&shape](const fo4::nif::Shape& item) { return item.block == shape.block; });
        ASSERT_NE(match, after.end()) << path;
        const auto vertex_count = shape.verts.size() / 3;
        ASSERT_EQ(match->colors.size(), 4 * vertex_count) << path;
        for (const float value : match->colors) EXPECT_EQ(value, 1.0f) << path;
        EXPECT_EQ(out_nif.blocks()[shape.block].size, nif.blocks()[shape.block].size + 4 * vertex_count) << path;
        EXPECT_TRUE(nif_fixture::same_floats(match->verts, shape.verts)) << path;
        EXPECT_TRUE(nif_fixture::same_floats(match->uvs, shape.uvs)) << path;
        EXPECT_TRUE(nif_fixture::same_floats(match->uvs2, shape.uvs2)) << path;
        EXPECT_EQ(match->tris, shape.tris) << path;
        EXPECT_TRUE(nif_fixture::same_floats(match->weights, shape.weights)) << path;
        EXPECT_EQ(match->bone_indices, shape.bone_indices) << path;
      }
    }
  }
  if (plain == 0) GTEST_SKIP() << "the sample corpus contains no shapes without vertex colours";
}
