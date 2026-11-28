#include "fo4/c_api_lod.h"

#include "env_node_fixture.hpp"
#include "fo4/archive/assets.hpp"
#include "fo4/environment/environment.hpp"
#include "fo4/lod/generate_support.hpp"
#include "fo4/lod/object_atlas.hpp"
#include "fo4/lod/object_candidates.hpp"
#include "fo4/lod/lod.hpp"
#include "fo4/lod/lod_surface.hpp"
#include "fo4/lod/object_tile.hpp"
#include "fo4/lod/terrain_texture.hpp"
#include "fo4/material/material_file.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_import.hpp"
#include "fo4/texture/dds.hpp"
#include "lod_object_generate_fixture.hpp"
#include "material_file_builder.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

using namespace fo4::test::envnode;
using namespace fo4::test::envnode::capi_lod;

namespace {

std::vector<std::filesystem::path> object_lod_tiles(const std::filesystem::path& root, std::string_view extension) {
  std::vector<std::filesystem::path> found;
  std::error_code status;
  for (std::filesystem::recursive_directory_iterator it(root, status), end; !status && it != end;
       it.increment(status)) {
    // Invariant: the tile extension is written upper case, so a case-sensitive scan must fold it.
    auto candidate = it->path().extension().string();
    for (auto& ch : candidate) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (candidate != extension) continue;
    found.push_back(it->path());
  }
  return found;
}

// Invariant: a BTO names its atlas with a lowercase data prefix, so the local file drops it and folds the separators.
std::filesystem::path object_lod_texture_file(const std::filesystem::path& root, std::string_view data_path) {
  std::string_view rest = data_path;
  if (rest.size() > 5 && (rest.substr(0, 5) == "Data\\" || rest.substr(0, 5) == "data\\")) {
    rest.remove_prefix(5);
  }
  std::filesystem::path relative;
  std::size_t start = 0;
  while (start < rest.size()) {
    const auto bar = rest.find('\\', start);
    const auto part = bar == std::string_view::npos ? rest.substr(start) : rest.substr(start, bar - start);
    if (!part.empty()) relative /= std::string{part};
    if (bar == std::string_view::npos) break;
    start = bar + 1;
  }
  return root / relative;
}

// Invariant: a plan row names its kind and level, so a reopened tile is identified without a second plan parser.
std::string plan_field(const std::string& row, std::string_view name) {
  const auto at = row.find("\"" + std::string{name} + "\":\"");
  if (at == std::string::npos) return {};
  const auto start = at + name.size() + 4;
  const auto end = row.find('"', start);
  return row.substr(start, end == std::string::npos ? 0 : end - start);
}

std::string plan_number(const std::string& row, std::string_view name) {
  const auto at = row.find("\"" + std::string{name} + "\":");
  if (at == std::string::npos) return {};
  const auto start = at + name.size() + 3;
  const auto end = row.find_first_of(",}", start);
  return row.substr(start, end == std::string::npos ? 0 : end - start);
}

// Invariant: only a row the planner marks for a rebake is a reopened tile, and plan order is the report order.
std::vector<std::string> reopened_tiles(const std::string& plan) {
  std::vector<std::string> tiles;
  std::size_t at = 0;
  while ((at = plan.find("{\"world\":", at)) != std::string::npos) {
    const auto end = plan.find('}', at);
    if (end == std::string::npos) break;
    const auto row = plan.substr(at, end - at + 1);
    at = end + 1;
    if (row.find("\"rebuild\":true") == std::string::npos) continue;
    tiles.push_back(plan_field(row, "kind") + "|" + plan_number(row, "level"));
  }
  return tiles;
}

}

// Detects (invariant): generate must write a .bto beside the .btr tiles, or object LOD never reaches disk.
TEST(CApiLod, WritesObjectTilesAlongsideTheTerrainTiles) {
  const Fixture fixture;
  object_lod_prepare(fixture);

  radicle_lod* lod = nullptr;
  ASSERT_EQ(radicle_lod_open(fixture.plugin_paths.c_str(), fixture.data_dir.c_str(), "", &lod), RADICLE_STATUS_OK);
  uint8_t* report = nullptr;
  size_t report_size = 0;
  uint8_t* ledger = nullptr;
  size_t ledger_size = 0;
  ASSERT_EQ(radicle_lod_generate_json(lod, "", 1, &report, &report_size, &ledger, &ledger_size), RADICLE_STATUS_OK);
  const auto text = text_of(report, report_size);
  const auto ledger_text = text_of(ledger, ledger_size);
  release(report);
  release(ledger);
  EXPECT_NE(text.find("\"kind\":\"object\""), std::string::npos);
  EXPECT_NE(text.find("\"kind\":\"terrain\""), std::string::npos);
  // Detects: a clean object run serialising zero counters instead of a short summary.
  expect_absent_count(text, "failed");
  expect_absent_count(text, "empty");
  EXPECT_NE(text.find("\"texturesWritten\":3"), std::string::npos);
  expect_absent_count(text, "texturesSkipped");
  EXPECT_NE(text.find("\"terrainTexturesWritten\":8"), std::string::npos);
  expect_absent_count(text, "terrainTexturesSkipped");
  EXPECT_NE(ledger_text.find("|texture|"), std::string::npos);

  const auto objects = object_lod_tiles(fixture.root, ".bto");
  const auto terrain = object_lod_tiles(fixture.root, ".btr");
  ASSERT_EQ(objects.size(), 4u);
  ASSERT_EQ(terrain.size(), 4u);
  for (const auto& path : objects) {
    EXPECT_EQ(path.parent_path().filename().string(), "Objects");
    EXPECT_EQ(path.extension().string(), ".BTO");
    const auto bytes = read_bytes(path);
    ASSERT_FALSE(bytes.empty());
    const auto nif = fo4::nif::read_nif(bytes);
    EXPECT_EQ(fo4::lod::check_lod_tile_structure(nif, fo4::lod::LodTileKind::object),
              fo4::lod::LodTileStructure::ok);
    const auto shapes = fo4::nif::read_shapes(nif);
    ASSERT_FALSE(shapes.empty());
    EXPECT_EQ(shapes.front().tris.size(), 3u);
    ASSERT_FALSE(shapes.front().textures.empty());
    EXPECT_EQ(shapes.front().textures.front(),
              "data\\Textures\\Terrain\\TestWorld\\Objects\\TestWorld.Objects.DDS");
    // Detects (invariant): every texture the BTO names must exist as a readable DDS in the same data folder.
    std::vector<std::string> named;
    for (const auto& texture : shapes.front().textures) {
      if (!texture.empty()) named.push_back(texture);
    }
    ASSERT_EQ(named.size(), 3u);
    for (const auto& texture : named) {
      const auto file = object_lod_texture_file(fixture.root, texture);
      ASSERT_TRUE(std::filesystem::exists(file)) << texture;
      const auto payload = read_bytes(file);
      ASSERT_GT(payload.size(), 128u) << texture;
      std::vector<std::uint8_t> raw(payload.size());
      std::memcpy(raw.data(), payload.data(), payload.size());
      const auto info = fo4::texture::read_dds(raw);
      EXPECT_EQ(info.width, info.height) << texture;
      EXPECT_EQ(info.dxgi_format(), 77) << texture;
      EXPECT_GE(info.width, 64) << texture;
      EXPECT_LE(info.width, 512) << texture;
      int expected_mips = 1;
      for (int side = info.width; side > 1; side >>= 1) ++expected_mips;
      EXPECT_EQ(info.mip_count, expected_mips) << texture;
      EXPECT_GT(info.payload_size(), 0) << texture;
      EXPECT_EQ(static_cast<std::size_t>(info.data_offset) + static_cast<std::size_t>(info.payload_size()),
                raw.size())
          << texture;
    }
  }

  // Detects (invariant): each .btr names two terrain atlases, so both must exist as readable DDS files.
  for (const auto& path : terrain) {
    const auto bytes = read_bytes(path);
    ASSERT_FALSE(bytes.empty()) << path.string();
    const auto nif = fo4::nif::read_nif(bytes);
    const auto shapes = fo4::nif::read_shapes(nif);
    ASSERT_FALSE(shapes.empty()) << path.string();
    std::vector<std::string> named;
    for (const auto& texture : shapes.front().textures) {
      if (!texture.empty()) named.push_back(texture);
    }
    ASSERT_EQ(named.size(), 2u) << path.string();
    for (const auto& texture : named) {
      const auto file = object_lod_texture_file(fixture.root, texture);
      ASSERT_TRUE(std::filesystem::exists(file)) << texture;
      const auto payload = read_bytes(file);
      ASSERT_GT(payload.size(), 128u) << texture;
      std::vector<std::uint8_t> raw(payload.size());
      std::memcpy(raw.data(), payload.data(), payload.size());
      const auto info = fo4::texture::read_dds(raw);
      EXPECT_EQ(info.width, 512) << texture;
      EXPECT_EQ(info.height, 512) << texture;
      EXPECT_EQ(info.dxgi_format(), 77) << texture;
      int expected_mips = 1;
      for (int side = info.width; side > 1; side >>= 1) ++expected_mips;
      EXPECT_EQ(info.mip_count, expected_mips) << texture;
      EXPECT_EQ(static_cast<std::size_t>(info.data_offset) + static_cast<std::size_t>(info.payload_size()),
                raw.size())
          << texture;
    }
  }

  // Invariant: the ledger must cover object tiles, so verify calls them current and a re-run writes nothing.
  uint8_t* verify_bytes = nullptr;
  size_t verify_size = 0;
  ASSERT_EQ(radicle_lod_verify_json(lod, ledger_text.c_str(), &verify_bytes, &verify_size), RADICLE_STATUS_OK);
  const auto verify_text = text_of(verify_bytes, verify_size);
  release(verify_bytes);
  EXPECT_NE(verify_text.find("\"ok\":8"), std::string::npos);
  EXPECT_NE(verify_text.find("\"stale\":0"), std::string::npos);
  EXPECT_NE(verify_text.find("\"missing\":0"), std::string::npos);

  report = nullptr;
  ledger = nullptr;
  ASSERT_EQ(radicle_lod_generate_json(lod, ledger_text.c_str(), 1, &report, &report_size, &ledger, &ledger_size),
            RADICLE_STATUS_OK);
  const auto repeat = text_of(report, report_size);
  release(report);
  release(ledger);
  // Detects (invariant): a second identical run must skip the atlas once the ledger and the files agree, and must
  // not report the tile, atlas and terrain counts it did not write.
  expect_absent_count(repeat, "written");
  expect_absent_count(repeat, "texturesWritten");
  EXPECT_NE(repeat.find("\"texturesSkipped\":3"), std::string::npos);
  expect_absent_count(repeat, "terrainTexturesWritten");
  EXPECT_NE(repeat.find("\"terrainTexturesSkipped\":8"), std::string::npos);
  radicle_lod_destroy(lod);
}

// Detects: a preview that always builds the terrain tile (so an object tile the planner lists draws terrain),
// and a preview whose bytes differ from the BTO the writer bakes for the same tile.
TEST(CApiLod, PreviewsTheObjectTileTheWriterBakes) {
  const Fixture fixture;
  object_lod_prepare(fixture);

  radicle_lod* lod = nullptr;
  ASSERT_EQ(radicle_lod_open(fixture.plugin_paths.c_str(), fixture.data_dir.c_str(), "", &lod), RADICLE_STATUS_OK);
  uint8_t* report = nullptr;
  size_t report_size = 0;
  uint8_t* ledger = nullptr;
  size_t ledger_size = 0;
  ASSERT_EQ(radicle_lod_generate_json(lod, "", 1, &report, &report_size, &ledger, &ledger_size), RADICLE_STATUS_OK);
  release(report);
  release(ledger);

  // Invariant: the written tile is the oracle, so the preview is compared with what generate produced.
  std::filesystem::path baked;
  for (const auto& path : object_lod_tiles(fixture.root, ".bto")) {
    const auto tile = fo4::lod::parse_lod_tile_path(path.filename().string());
    ASSERT_TRUE(tile.has_value()) << path.string();
    if (tile->level == 4) baked = path;
  }
  ASSERT_FALSE(baked.empty());
  const auto written = read_bytes(baked);
  ASSERT_FALSE(written.empty());

  uint8_t* bytes = nullptr;
  size_t size = 0;
  ASSERT_EQ(radicle_lod_preview_object_nif(lod, "testworld", 4, 0, 0, &bytes, &size), RADICLE_STATUS_OK)
      << radicle_last_error();
  ASSERT_NE(bytes, nullptr);
  EXPECT_EQ(size, written.size());
  // Detects (invariant): an unpacked preview would carry source UVs, so the atlas must be baked first.
  EXPECT_EQ(std::memcmp(bytes, written.data(), written.size()), 0);
  const std::vector<std::byte> preview(reinterpret_cast<const std::byte*>(bytes),
                                       reinterpret_cast<const std::byte*>(bytes) + size);
  release(bytes);
  const auto nif = fo4::nif::read_nif(preview);
  EXPECT_EQ(fo4::lod::check_lod_tile_structure(nif, fo4::lod::LodTileKind::object),
            fo4::lod::LodTileStructure::ok);

  // Detects: an empty tile or an unknown world answering with empty bytes, which would render nothing silently.
  bytes = nullptr;
  size = 123;
  EXPECT_EQ(radicle_lod_preview_object_nif(lod, "testworld", 4, 16, 16, &bytes, &size), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(bytes, nullptr);
  EXPECT_EQ(size, 0u);
  EXPECT_EQ(radicle_lod_preview_object_nif(lod, "MissingWorld", 4, 0, 0, &bytes, &size), RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(bytes, nullptr);
  radicle_lod_destroy(lod);
}

// Detects: verify passing a world whose atlas DDS was deleted or truncated, so the game loads missing LOD textures.
TEST(CApiLod, VerifyReportsAtlasTexturesAndFailsWhenTheyBreak) {
  const Fixture fixture;
  object_lod_prepare(fixture);
  radicle_lod* lod = nullptr;
  ASSERT_EQ(radicle_lod_open(fixture.plugin_paths.c_str(), fixture.data_dir.c_str(), "", &lod), RADICLE_STATUS_OK);
  uint8_t* report = nullptr;
  size_t report_size = 0;
  uint8_t* ledger = nullptr;
  size_t ledger_size = 0;
  ASSERT_EQ(radicle_lod_generate_json(lod, "", 1, &report, &report_size, &ledger, &ledger_size), RADICLE_STATUS_OK);
  const auto ledger_text = text_of(ledger, ledger_size);
  release(report);
  release(ledger);

  uint8_t* verify_bytes = nullptr;
  size_t verify_size = 0;
  ASSERT_EQ(radicle_lod_verify_json(lod, ledger_text.c_str(), &verify_bytes, &verify_size), RADICLE_STATUS_OK);
  const auto clean = text_of(verify_bytes, verify_size);
  release(verify_bytes);
  // Invariant: the report counts the three object atlas maps and the two atlases of each of the four terrain tiles.
  EXPECT_NE(clean.find("\"texturesOk\":11"), std::string::npos);
  EXPECT_NE(clean.find("\"texturesMissing\":0"), std::string::npos);
  EXPECT_NE(clean.find("\"texturesStale\":0"), std::string::npos);
  EXPECT_NE(clean.find("\"kind\":\"texture\""), std::string::npos);

  // Invariant: the atlas paths come from the produced tile bytes, not from a second naming rule in the test.
  const auto objects = object_lod_tiles(fixture.root, ".bto");
  ASSERT_EQ(objects.size(), 4u);
  const auto tile_bytes = read_bytes(objects.front());
  ASSERT_FALSE(tile_bytes.empty());
  const auto nif = fo4::nif::read_nif(tile_bytes);
  const auto shapes = fo4::nif::read_shapes(nif);
  ASSERT_FALSE(shapes.empty());
  std::vector<std::filesystem::path> atlases;
  for (const auto& texture : shapes.front().textures) {
    if (!texture.empty()) atlases.push_back(object_lod_texture_file(fixture.root, texture));
  }
  ASSERT_EQ(atlases.size(), 3u);

  // Invariant: a deleted atlas must fail verification and the error must name the world and map.
  std::filesystem::remove(atlases[0]);
  verify_bytes = nullptr;
  verify_size = 0;
  EXPECT_EQ(radicle_lod_verify_json(lod, ledger_text.c_str(), &verify_bytes, &verify_size),
            RADICLE_STATUS_NOT_FOUND);
  // Constraint: radicle_free_bytes clears the last error, so read it before releasing the report.
  const char* raw_error = radicle_last_error();
  const std::string missing_error = raw_error == nullptr ? std::string{} : std::string{raw_error};
  const auto missing = text_of(verify_bytes, verify_size);
  release(verify_bytes);
  EXPECT_NE(missing.find("\"texturesMissing\":1"), std::string::npos);
  EXPECT_NE(missing_error.find("TestWorld"), std::string::npos) << missing_error;
  EXPECT_NE(missing_error.find("diffuse"), std::string::npos) << missing_error;

  // Invariant: regenerate restores the deleted atlas, so the corruption case starts from a complete set.
  report = nullptr;
  report_size = 0;
  ledger = nullptr;
  ledger_size = 0;
  ASSERT_EQ(radicle_lod_generate_json(lod, ledger_text.c_str(), 1, &report, &report_size, &ledger, &ledger_size),
            RADICLE_STATUS_OK);
  release(report);
  release(ledger);
  ASSERT_TRUE(std::filesystem::exists(atlases[0]));

  // Invariant: a truncated or hand-edited atlas is stale, and verify must fail for it too.
  {
    std::ofstream out(atlases[1], std::ios::binary | std::ios::trunc);
    out << "not a dds";
  }
  verify_bytes = nullptr;
  verify_size = 0;
  EXPECT_EQ(radicle_lod_verify_json(lod, ledger_text.c_str(), &verify_bytes, &verify_size),
            RADICLE_STATUS_PARSE_ERROR);
  const auto stale = text_of(verify_bytes, verify_size);
  release(verify_bytes);
  EXPECT_NE(stale.find("\"texturesStale\":1"), std::string::npos);
  EXPECT_NE(stale.find("\"texturesMissing\":0"), std::string::npos);
  radicle_lod_destroy(lod);
}

namespace {

using Rgba = std::array<std::uint8_t, 4>;

constexpr Rgba k_red{255, 0, 0, 255};
constexpr Rgba k_green{0, 255, 0, 255};
constexpr Rgba k_blue{0, 0, 255, 255};
constexpr Rgba k_white{255, 255, 255, 255};
constexpr Rgba k_yellow{255, 255, 0, 255};

struct AtlasPixels {
  std::uint32_t width{0};
  std::uint32_t height{0};
  std::vector<std::uint8_t> rgba;
  Rgba at(float u, float v) const {
    const auto column = std::min(width - 1, static_cast<std::uint32_t>(u * static_cast<float>(width)));
    const auto row = std::min(height - 1, static_cast<std::uint32_t>(v * static_cast<float>(height)));
    const auto index = (static_cast<std::size_t>(row) * width + column) * 4;
    return {rgba[index], rgba[index + 1], rgba[index + 2], rgba[index + 3]};
  }
};

// Invariant: a 16x16 source in four 8x8 quadrants, so every BC3 block is one colour and a sample is exact.
std::vector<std::byte> object_lod_quadrant_dds(const Rgba& top_left, const Rgba& top_right, const Rgba& bottom_left,
                                               const Rgba& bottom_right) {
  fo4::lod::TerrainImage image;
  image.width = 16;
  image.height = 16;
  image.rgba.resize(16 * 16 * 4);
  for (std::size_t row = 0; row < 16; ++row) {
    for (std::size_t column = 0; column < 16; ++column) {
      const auto& colour = row < 8 ? (column < 8 ? top_left : top_right) : (column < 8 ? bottom_left : bottom_right);
      for (std::size_t part = 0; part < 4; ++part) image.rgba[(row * 16 + column) * 4 + part] = colour[part];
    }
  }
  return fo4::lod::write_terrain_dds(image, fo4::lod::TerrainDdsFormat::bgra8, fo4::lod::TerrainDdsAlpha::keep);
}

std::vector<std::byte> object_lod_textured_mesh(const std::string& material, const std::string& diffuse) {
  const std::string obj =
      "v 0 0 0\nv 128 0 0\nv 0 128 0\nvt 0.1 0.1\nvt 0.9 0.1\nvt 0.1 0.9\nvn 0 0 1\nf 1/1/1 2/2/1 3/3/1\n";
  fo4::nif::ImportOptions options;
  options.material = material;
  options.tex_diffuse = diffuse;
  return fo4::nif::import_obj(obj, options);
}

AtlasPixels object_lod_decode(const std::filesystem::path& path) {
  const auto payload = read_bytes(path);
  std::vector<std::uint8_t> raw(payload.size());
  if (!payload.empty()) std::memcpy(raw.data(), payload.data(), payload.size());
  AtlasPixels pixels;
  pixels.rgba = fo4::texture::decode_dds_rgba(raw, 0, false, pixels.width, pixels.height);
  return pixels;
}

void expect_colour(const Rgba& actual, const Rgba& expected, const std::string& what) {
  for (std::size_t part = 0; part < 4; ++part) {
    EXPECT_NEAR(static_cast<int>(actual[part]), static_cast<int>(expected[part]), 12) << what << " channel " << part;
  }
}

Rgba quadrant_colour(float u, float v) {
  return v < 0.5f ? (u < 0.5f ? k_red : k_green) : (u < 0.5f ? k_blue : k_white);
}

struct GeneratedTile {
  std::filesystem::path path;
  fo4::nif::Shape shape;
};

GeneratedTile object_lod_tile_at_level(const std::filesystem::path& root, int level) {
  const auto prefix = "TestWorld." + std::to_string(level) + ".";
  for (const auto& path : object_lod_tiles(root, ".bto")) {
    if (path.filename().string().rfind(prefix, 0) != 0) continue;
    const auto nif = fo4::nif::read_nif(read_bytes(path));
    auto shapes = fo4::nif::read_shapes(nif);
    if (shapes.empty()) continue;
    return GeneratedTile{path, std::move(shapes.front())};
  }
  return {};
}

}  // namespace

// Detects (invariant): an atlas of stand-in colours, or tile UVs left in source space, so the game would draw every
// LOD object with the wrong texels. Each tile vertex must sample, in the written atlas, the colour its source mesh
// sampled in its own texture, for a mesh texture set, for a material file and for a texture that is missing.
TEST(CApiLod, PacksSourceTexturesIntoTheObjectAtlasAndRemapsTileUvs) {
  const Fixture fixture;
  object_lod_prepare(fixture);
  write_bytes(fixture.root / "Textures" / "LOD" / "Quad.dds",
              object_lod_quadrant_dds(k_red, k_green, k_blue, k_white));
  write_bytes(fixture.root / "Textures" / "LOD" / "Yellow.dds",
              object_lod_quadrant_dds(k_yellow, k_yellow, k_yellow, k_yellow));
  // Invariant: the material file names its texture below Textures and switches the alpha test on.
  auto material = fo4::material::read_material_file(fo4::test::material_bgsm({.version = 2}));
  ASSERT_TRUE(fo4::material::set_text(material, "DiffuseTexture", "LOD/Yellow.dds"));
  ASSERT_TRUE(fo4::material::set_text(material, "NormalTexture", ""));
  ASSERT_TRUE(fo4::material::set_text(material, "SmoothSpecTexture", ""));
  ASSERT_TRUE(fo4::material::set_field(material, "AlphaTest", true));
  write_bytes(fixture.root / "Materials" / "LOD" / "Rock.BGSM", fo4::material::write_material_file(material));

  const auto quad_mesh = object_lod_textured_mesh("", "textures\\LOD\\Quad.dds");
  write_bytes(fixture.root / "Meshes" / "lod4.nif", quad_mesh);
  // Invariant: this mesh names the quadrant texture itself, so only its material file can make it sample yellow.
  write_bytes(fixture.root / "Meshes" / "lod8.nif",
              object_lod_textured_mesh("Materials\\LOD\\Rock.BGSM", "textures\\LOD\\Quad.dds"));
  write_bytes(fixture.root / "Meshes" / "lod16.nif", object_lod_textured_mesh("", "textures\\LOD\\Gone.dds"));
  write_bytes(fixture.root / "Meshes" / "lod32.nif", quad_mesh);

  radicle_lod* lod = nullptr;
  ASSERT_EQ(radicle_lod_open(fixture.plugin_paths.c_str(), fixture.data_dir.c_str(), "", &lod), RADICLE_STATUS_OK);
  uint8_t* report = nullptr;
  size_t report_size = 0;
  uint8_t* ledger = nullptr;
  size_t ledger_size = 0;
  ASSERT_EQ(radicle_lod_generate_json(lod, "", 1, &report, &report_size, &ledger, &ledger_size), RADICLE_STATUS_OK);
  const auto text = text_of(report, report_size);
  const auto ledger_text = text_of(ledger, ledger_size);
  release(report);
  release(ledger);
  expect_absent_count(text, "failed");
  // Invariant: levels 4 and 32 share the quadrant entry; the material and the missing texture get one each.
  EXPECT_NE(text.find("\"atlasEntries\":3"), std::string::npos) << text;
  EXPECT_NE(text.find("\"atlasMissingTextures\":1"), std::string::npos) << text;
  expect_absent_count(text, "atlasClampedShapes");
  expect_absent_count(text, "objectMissingMeshes");

  const auto source = fo4::nif::read_shapes(fo4::nif::read_nif(quad_mesh));
  ASSERT_EQ(source.size(), 1u);
  ASSERT_EQ(source[0].tris.size(), 3u);

  const auto atlas_path = fixture.root / "Textures" / "Terrain" / "TestWorld" / "Objects" / "TestWorld.Objects.DDS";
  ASSERT_TRUE(std::filesystem::exists(atlas_path));
  const auto atlas = object_lod_decode(atlas_path);
  ASSERT_GE(atlas.width, 64u);
  ASSERT_EQ(atlas.width, atlas.height);

  for (const int level : {4, 32}) {
    const auto tile = object_lod_tile_at_level(fixture.root, level);
    ASSERT_FALSE(tile.path.empty()) << level;
    ASSERT_EQ(tile.shape.tris.size(), 3u) << level;
    EXPECT_EQ(tile.shape.name, "obj") << level;
    for (std::size_t corner = 0; corner < 3; ++corner) {
      // Invariant: the builder emits tile vertices in source triangle order, so corner k is source index tris[k].
      const auto from = static_cast<std::size_t>(source[0].tris[corner]);
      const auto to = static_cast<std::size_t>(tile.shape.tris[corner]);
      const auto expected = quadrant_colour(source[0].uvs[from * 2], source[0].uvs[from * 2 + 1]);
      const auto actual = atlas.at(tile.shape.uvs[to * 2], tile.shape.uvs[to * 2 + 1]);
      expect_colour(actual, expected, "level " + std::to_string(level) + " corner " + std::to_string(corner));
    }
  }
  // Invariant: the three corners must hit three different quadrants, or the check above could pass on a flat entry.
  {
    const auto tile = object_lod_tile_at_level(fixture.root, 4);
    const auto first = atlas.at(tile.shape.uvs[0], tile.shape.uvs[1]);
    const auto second = atlas.at(tile.shape.uvs[2], tile.shape.uvs[3]);
    const auto third = atlas.at(tile.shape.uvs[4], tile.shape.uvs[5]);
    EXPECT_NE(first, second);
    EXPECT_NE(first, third);
    EXPECT_NE(second, third);
  }

  // Invariant: a material file wins over the mesh texture set and its alpha test moves the shape to the alpha lane.
  {
    const auto tile = object_lod_tile_at_level(fixture.root, 8);
    ASSERT_FALSE(tile.path.empty());
    EXPECT_EQ(tile.shape.name, "obj-at");
    for (std::size_t corner = 0; corner < 3; ++corner) {
      const auto to = static_cast<std::size_t>(tile.shape.tris[corner]);
      expect_colour(atlas.at(tile.shape.uvs[to * 2], tile.shape.uvs[to * 2 + 1]), k_yellow,
                    "material corner " + std::to_string(corner));
    }
  }

  // Invariant: a missing source texture still yields a written tile whose UVs sit on an opaque stand-in entry.
  {
    const auto tile = object_lod_tile_at_level(fixture.root, 16);
    ASSERT_FALSE(tile.path.empty());
    const auto first = atlas.at(tile.shape.uvs[0], tile.shape.uvs[1]);
    EXPECT_EQ(first[3], 255);
    for (std::size_t corner = 1; corner < 3; ++corner) {
      expect_colour(atlas.at(tile.shape.uvs[corner * 2], tile.shape.uvs[corner * 2 + 1]), first, "stand-in");
    }
  }

  // Detects (invariant): a tile deleted behind a current ledger must be rewritten, and with the same UVs, because
  // the atlas it samples is skipped as current.
  const auto before = object_lod_tile_at_level(fixture.root, 4);
  std::filesystem::remove(before.path);
  report = nullptr;
  ledger = nullptr;
  ASSERT_EQ(radicle_lod_generate_json(lod, ledger_text.c_str(), 1, &report, &report_size, &ledger, &ledger_size),
            RADICLE_STATUS_OK);
  const auto repeat = text_of(report, report_size);
  release(report);
  release(ledger);
  EXPECT_NE(repeat.find("\"written\":1"), std::string::npos) << repeat;
  // Detects: a one-tile rewrite reporting an atlas it skipped as "texturesWritten":0.
  expect_absent_count(repeat, "texturesWritten");
  const auto after = object_lod_tile_at_level(fixture.root, 4);
  ASSERT_FALSE(after.path.empty());
  EXPECT_EQ(after.shape.uvs, before.shape.uvs);

  // Detects (invariant): a source texture changed behind an unchanged plugin leaving old tiles on a new atlas.
  // Restoring one deleted map bakes a different atlas, so every tile and all three maps must be rewritten together.
  write_bytes(fixture.root / "Textures" / "LOD" / "Quad.dds",
                   object_lod_quadrant_dds(k_white, k_blue, k_green, k_red));
  std::filesystem::remove(atlas_path);
  report = nullptr;
  ledger = nullptr;
  ASSERT_EQ(radicle_lod_generate_json(lod, ledger_text.c_str(), 1, &report, &report_size, &ledger, &ledger_size),
            RADICLE_STATUS_OK);
  const auto relaid = text_of(report, report_size);
  release(report);
  release(ledger);
  EXPECT_NE(relaid.find("\"written\":4,"), std::string::npos) << relaid;
  EXPECT_NE(relaid.find("\"texturesWritten\":3,"), std::string::npos) << relaid;
  expect_absent_count(relaid, "failed");
  radicle_lod_destroy(lod);
}

// Detects (invariant): verify failing a world whose object atlas was simply never generated, which would make the
// first verify of any untouched data folder an error instead of a list of missing files.
TEST(CApiLod, VerifyListsANeverGeneratedAtlasAsMissingWithoutFailing) {
  const Fixture fixture;
  object_lod_prepare(fixture);
  radicle_lod* lod = nullptr;
  ASSERT_EQ(radicle_lod_open(fixture.plugin_paths.c_str(), fixture.data_dir.c_str(), "", &lod), RADICLE_STATUS_OK);
  uint8_t* verify_bytes = nullptr;
  size_t verify_size = 0;
  EXPECT_EQ(radicle_lod_verify_json(lod, "", &verify_bytes, &verify_size), RADICLE_STATUS_OK);
  const auto text = text_of(verify_bytes, verify_size);
  release(verify_bytes);
  EXPECT_NE(text.find("\"texturesMissing\":11"), std::string::npos) << text;
  radicle_lod_destroy(lod);
}

// Detects (invariant): a loose mesh or texture edit must reopen the tiles that read it, not leave them current.
TEST(CApiLod, PlanReopensTheTilesALooseMeshOrTextureEditTouches) {
  const Fixture fixture;
  object_lod_prepare(fixture);
  // Invariant: only the level-8 mesh names a texture, so only its tile reads that .DDS.
  const auto textured = object_lod_textured_mesh("", "textures\\LOD\\Quad.dds");
  write_bytes(fixture.root / "Meshes" / "lod8.nif", textured);
  write_bytes(fixture.root / "Textures" / "LOD" / "Quad.dds",
              object_lod_quadrant_dds(k_red, k_green, k_blue, k_white));

  radicle_lod* lod = nullptr;
  ASSERT_EQ(radicle_lod_open(fixture.plugin_paths.c_str(), fixture.data_dir.c_str(), "", &lod), RADICLE_STATUS_OK);
  const auto plan_with = [](radicle_lod* session, const std::string& ledger) {
    uint8_t* plan = nullptr;
    size_t plan_size = 0;
    uint8_t* kept = nullptr;
    size_t kept_size = 0;
    uint8_t* commit = nullptr;
    size_t commit_size = 0;
    EXPECT_EQ(radicle_lod_plan_json(session, ledger.c_str(), &plan, &plan_size, &kept, &kept_size, &commit,
                                    &commit_size),
              RADICLE_STATUS_OK);
    const auto text = text_of(plan, plan_size);
    release(plan);
    release(kept);
    release(commit);
    return text;
  };
  const auto generate_with = [](radicle_lod* session, const std::string& ledger) {
    uint8_t* report = nullptr;
    size_t report_size = 0;
    uint8_t* kept = nullptr;
    size_t kept_size = 0;
    EXPECT_EQ(radicle_lod_generate_json(session, ledger.c_str(), 1, &report, &report_size, &kept, &kept_size),
              RADICLE_STATUS_OK);
    const auto text = text_of(kept, kept_size);
    release(report);
    release(kept);
    return text;
  };

  const auto ledger = generate_with(lod, "");
  ASSERT_FALSE(ledger.empty());
  // Detects (invariant): a tile the writer just baked that must stay current on a replan.
  EXPECT_TRUE(reopened_tiles(plan_with(lod, ledger)).empty()) << plan_with(lod, ledger);

  // Detects (invariant): a replaced loose mesh that must reopen only the tile that reads it.
  write_bytes(fixture.root / "Meshes" / "lod8.nif", object_lod_mesh());
  EXPECT_EQ(reopened_tiles(plan_with(lod, ledger)), std::vector<std::string>({"object|8"}));

  // Invariant: restoring the textured mesh puts the .DDS back among the level-8 tile's inputs.
  write_bytes(fixture.root / "Meshes" / "lod8.nif", textured);
  const auto restored = generate_with(lod, ledger);
  EXPECT_TRUE(reopened_tiles(plan_with(lod, restored)).empty()) << plan_with(lod, restored);
  // Detects (invariant): a replaced loose texture that must reopen the tiles whose atlas packs it.
  write_bytes(fixture.root / "Textures" / "LOD" / "Quad.dds",
              object_lod_quadrant_dds(k_yellow, k_white, k_red, k_blue));
  EXPECT_EQ(reopened_tiles(plan_with(lod, restored)), std::vector<std::string>({"object|8"}));
  radicle_lod_destroy(lod);
}

// Detects (invariant): a tile with nothing to draw being rebuilt on every run and listed as missing by verify
// forever, or a tile that only lost its meshes being recorded as done and never retried.
TEST(CApiLod, RecordsAShapelessObjectTileAsEmptyButRetriesOneThatLostItsMeshes) {
  const auto generate = [](radicle_lod* lod, const std::string& ledger_in, std::string& ledger_out) {
    uint8_t* report = nullptr;
    size_t report_size = 0;
    uint8_t* ledger = nullptr;
    size_t ledger_size = 0;
    EXPECT_EQ(radicle_lod_generate_json(lod, ledger_in.c_str(), 1, &report, &report_size, &ledger, &ledger_size),
              RADICLE_STATUS_OK);
    auto text = text_of(report, report_size);
    ledger_out = text_of(ledger, ledger_size);
    release(report);
    release(ledger);
    return text;
  };
  const auto verify = [](radicle_lod* lod, const std::string& ledger_in) {
    uint8_t* bytes = nullptr;
    size_t size = 0;
    EXPECT_EQ(radicle_lod_verify_json(lod, ledger_in.c_str(), &bytes, &size), RADICLE_STATUS_OK);
    auto text = text_of(bytes, size);
    release(bytes);
    return text;
  };
  {
    // Invariant: a readable mesh without any shape is a tile with nothing to draw.
    const Fixture fixture;
    object_lod_prepare(fixture);
    fo4::lod::ObjectTile bare;
    bare.level = 4;
    const auto shapeless = fo4::lod::write_object_bto(bare, "TestWorld");
    for (const auto* name : {"lod4.nif", "lod8.nif", "lod16.nif", "lod32.nif"}) {
      write_bytes(fixture.root / "Meshes" / name, shapeless);
    }
    radicle_lod* lod = nullptr;
    ASSERT_EQ(radicle_lod_open(fixture.plugin_paths.c_str(), fixture.data_dir.c_str(), "", &lod), RADICLE_STATUS_OK);
    std::string ledger_text;
    const auto text = generate(lod, "", ledger_text);
    EXPECT_NE(text.find("\"empty\":4,"), std::string::npos) << text;
    expect_absent_count(text, "objectMissingMeshes");
    expect_absent_count(text, "failed");
    EXPECT_TRUE(object_lod_tiles(fixture.root, ".bto").empty());
    const auto verify_text = verify(lod, ledger_text);
    EXPECT_NE(verify_text.find("\"empty\":4,"), std::string::npos) << verify_text;
    EXPECT_NE(verify_text.find("\"missing\":0,"), std::string::npos) << verify_text;
    // Invariant: a world with only empty object tiles has no atlas, so verify lists none as missing.
    EXPECT_NE(verify_text.find("\"texturesMissing\":0,"), std::string::npos) << verify_text;
    std::string second_ledger;
    const auto repeat = generate(lod, ledger_text, second_ledger);
    // Detects: a repeat run whose four object tiles are all current reporting "empty":0 instead of no count.
    expect_absent_count(repeat, "empty");
    radicle_lod_destroy(lod);
  }
  {
    const Fixture fixture;
    object_lod_prepare(fixture);
    for (const auto* name : {"lod4.nif", "lod8.nif", "lod16.nif", "lod32.nif"}) {
      std::filesystem::remove(fixture.root / "Meshes" / name);
    }
    radicle_lod* lod = nullptr;
    ASSERT_EQ(radicle_lod_open(fixture.plugin_paths.c_str(), fixture.data_dir.c_str(), "", &lod), RADICLE_STATUS_OK);
    std::string ledger_text;
    const auto text = generate(lod, "", ledger_text);
    EXPECT_NE(text.find("\"empty\":4,"), std::string::npos) << text;
    EXPECT_NE(text.find("\"objectMissingMeshes\":4,"), std::string::npos) << text;
    const auto verify_text = verify(lod, ledger_text);
    EXPECT_NE(verify_text.find("\"missing\":4,"), std::string::npos) << verify_text;
    // Invariant: once the meshes arrive the same ledger must let the tiles be written.
    const auto mesh = object_lod_mesh();
    for (const auto* name : {"lod4.nif", "lod8.nif", "lod16.nif", "lod32.nif"}) {
      write_bytes(fixture.root / "Meshes" / name, mesh);
    }
    std::string second_ledger;
    const auto repeat = generate(lod, ledger_text, second_ledger);
    EXPECT_NE(repeat.find("\"written\":4,"), std::string::npos) << repeat;
    EXPECT_EQ(object_lod_tiles(fixture.root, ".bto").size(), 4u);
    radicle_lod_destroy(lod);
  }
}

// Detects (invariant): a dry run that hides which object tiles it replaces and which it deletes as shapeless.
TEST(CApiLod, DryRunListsTheObjectTilesItWouldOverwriteAndDelete) {
  const Fixture fixture;
  object_lod_prepare(fixture);
  const auto dry_report = [&]() {
    radicle_lod* lod = nullptr;
    EXPECT_EQ(radicle_lod_open(fixture.plugin_paths.c_str(), fixture.data_dir.c_str(), "", &lod), RADICLE_STATUS_OK);
    uint8_t* report = nullptr;
    size_t report_size = 0;
    uint8_t* ledger = nullptr;
    size_t ledger_size = 0;
    EXPECT_EQ(radicle_lod_generate_json(lod, "", 0, &report, &report_size, &ledger, &ledger_size), RADICLE_STATUS_OK);
    auto text = text_of(report, report_size);
    release(report);
    release(ledger);
    radicle_lod_destroy(lod);
    return text;
  };
  {
    radicle_lod* lod = nullptr;
    ASSERT_EQ(radicle_lod_open(fixture.plugin_paths.c_str(), fixture.data_dir.c_str(), "", &lod), RADICLE_STATUS_OK);
    uint8_t* report = nullptr;
    size_t report_size = 0;
    uint8_t* ledger = nullptr;
    size_t ledger_size = 0;
    ASSERT_EQ(radicle_lod_generate_json(lod, "", 1, &report, &report_size, &ledger, &ledger_size), RADICLE_STATUS_OK);
    release(report);
    release(ledger);
    radicle_lod_destroy(lod);
  }
  const auto relative = [&](const std::filesystem::path& path) {
    return path.lexically_relative(fixture.root).generic_string();
  };
  const auto objects = object_lod_tiles(fixture.root, ".bto");
  const auto terrain = object_lod_tiles(fixture.root, ".btr");
  ASSERT_EQ(objects.size(), 4u);
  ASSERT_EQ(terrain.size(), 4u);

  const auto overwriting = dry_report();
  for (const auto& path : objects) {
    EXPECT_NE(overwriting.find("\"" + relative(path) + "\""), std::string::npos) << overwriting;
  }
  for (const auto& path : terrain) {
    EXPECT_NE(overwriting.find("\"" + relative(path) + "\""), std::string::npos) << overwriting;
  }
  EXPECT_EQ(overwriting.find("wouldDelete"), std::string::npos) << overwriting;
  EXPECT_EQ(overwriting.find("Truncated"), std::string::npos) << overwriting;

  fo4::lod::ObjectTile bare;
  bare.level = 4;
  const auto shapeless = fo4::lod::write_object_bto(bare, "TestWorld");
  for (const auto* name : {"lod4.nif", "lod8.nif", "lod16.nif", "lod32.nif"}) {
    write_bytes(fixture.root / "Meshes" / name, shapeless);
  }
  const auto deleting = dry_report();
  const auto deleted_at = deleting.find("\"wouldDelete\":[");
  ASSERT_NE(deleted_at, std::string::npos) << deleting;
  const auto overwritten_at = deleting.find("\"wouldOverwrite\":[");
  ASSERT_NE(overwritten_at, std::string::npos) << deleting;
  const auto deleted_list = deleting.substr(deleted_at, deleting.find(']', deleted_at) - deleted_at);
  const auto overwritten_list = deleting.substr(overwritten_at, deleting.find(']', overwritten_at) - overwritten_at);
  for (const auto& path : objects) {
    EXPECT_NE(deleted_list.find("\"" + relative(path) + "\""), std::string::npos) << deleting;
    EXPECT_EQ(overwritten_list.find("\"" + relative(path) + "\""), std::string::npos) << deleting;
  }
  for (const auto& path : terrain) {
    EXPECT_EQ(deleted_list.find("\"" + relative(path) + "\""), std::string::npos) << deleting;
    EXPECT_NE(overwritten_list.find("\"" + relative(path) + "\""), std::string::npos) << deleting;
  }
  EXPECT_EQ(object_lod_tiles(fixture.root, ".bto").size(), 4u);
}

namespace {

// Invariant: a ledger line names its lane, so a test can count the rows of one kind without parsing a hash.
std::vector<std::string> ledger_rows_with(const std::string& ledger, std::string_view marker) {
  std::vector<std::string> rows;
  std::size_t start = 0;
  while (start < ledger.size()) {
    const auto end = ledger.find('\n', start);
    const auto line = ledger.substr(start, end == std::string::npos ? std::string::npos : end - start);
    start = end == std::string::npos ? ledger.size() : end + 1;
    if (line.find(marker) != std::string::npos) rows.push_back(line);
  }
  return rows;
}

// Invariant: the object rows are the ledger lines that name an object tile.
std::vector<std::string> object_ledger_rows(const std::string& ledger) {
  return ledger_rows_with(ledger, "|object|");
}

// Invariant: a ledger row names its tile by level, x and y, so both lanes of one tile share that triple.
std::string ledger_tile_coords(const std::string& row) {
  // Constraint: a tile key is "<world>|terrain|l|x|y" and an atlas key is "<world>|texture|terrain.l.x.y.<map>".
  const std::string key = row.substr(0, row.rfind(' '));
  const std::string tile_marker = "|terrain|";
  const std::string atlas_marker = "|texture|terrain.";
  if (const auto at = key.find(tile_marker); at != std::string::npos) {
    return key.substr(at + tile_marker.size());
  }
  const auto at = key.find(atlas_marker);
  if (at == std::string::npos) return {};
  std::string coords = key.substr(at + atlas_marker.size());
  const auto dot = coords.rfind('.');
  if (dot == std::string::npos) return {};
  coords.resize(dot);
  std::replace(coords.begin(), coords.end(), '.', '|');
  return coords;
}

std::size_t ledger_row_count(const std::string& ledger) {
  std::size_t rows = 0;
  std::size_t start = 0;
  while (start < ledger.size()) {
    const auto end = ledger.find('\n', start);
    const auto line = ledger.substr(start, end == std::string::npos ? std::string::npos : end - start);
    start = end == std::string::npos ? ledger.size() : end + 1;
    if (line.find_first_not_of(" \r") != std::string::npos) ++rows;
  }
  return rows;
}

struct CancelOnFirstReport {
  std::int32_t flag{0};
  std::size_t reports{0};
};

void cancel_on_first_report(void* user, const char*, const char*, std::int32_t, std::int32_t, std::int32_t,
                            std::size_t, std::size_t) {
  auto* state = static_cast<CancelOnFirstReport*>(user);
  if (++state->reports == 1) state->flag = 1;
}

}  // namespace

// Detects (invariant): a level scope repacking the per-world atlas, so the other levels' UVs stop resolving.
TEST(CApiLod, ALevelScopeLeavesTheObjectAtlasAndEveryObjectTileUntouched) {
  const Fixture fixture;
  object_lod_prepare(fixture);
  radicle_lod* lod = nullptr;
  ASSERT_EQ(radicle_lod_open(fixture.plugin_paths.c_str(), fixture.data_dir.c_str(), "", &lod), RADICLE_STATUS_OK);

  // Invariant: the whole world is baked first, so the atlas and every level's BTO exist to be left alone.
  uint8_t* report = nullptr;
  size_t report_size = 0;
  uint8_t* ledger = nullptr;
  size_t ledger_size = 0;
  ASSERT_EQ(radicle_lod_generate_json(lod, "", 1, &report, &report_size, &ledger, &ledger_size), RADICLE_STATUS_OK);
  const auto ledger_before = text_of(ledger, ledger_size);
  release(report);
  release(ledger);
  const auto atlas = object_lod_texture_file(
      fixture.root, "data\\Textures\\Terrain\\TestWorld\\Objects\\TestWorld.Objects.DDS");
  ASSERT_TRUE(std::filesystem::exists(atlas));
  const auto atlas_before = read_bytes(atlas);
  std::vector<std::pair<std::filesystem::path, std::vector<std::byte>>> tiles_before;
  for (const auto& path : object_lod_tiles(fixture.root, ".bto")) tiles_before.emplace_back(path, read_bytes(path));
  ASSERT_EQ(tiles_before.size(), 4u);

  radicle_lod_scope scope{};
  scope.level_min = 8;
  scope.level_max = 8;
  report = nullptr;
  ledger = nullptr;
  ASSERT_EQ(radicle_lod_generate_scoped_json(lod, ledger_before.c_str(), 1, "TestWorld", &scope, &report,
                                             &report_size, &ledger, &ledger_size),
            RADICLE_STATUS_OK);
  const auto text = text_of(report, report_size);
  const auto ledger_after = text_of(ledger, ledger_size);
  release(report);
  release(ledger);

  EXPECT_NE(text.find("\"objectsSkipped\":\"level scope\""), std::string::npos) << text;
  EXPECT_NE(text.find("\"levelMin\":8,\"levelMax\":8"), std::string::npos) << text;
  // Detects (invariant): an atlas repacked from one level's tiles, so the other levels' UVs no longer resolve.
  EXPECT_EQ(read_bytes(atlas), atlas_before);
  for (const auto& entry : tiles_before) EXPECT_EQ(read_bytes(entry.first), entry.second) << entry.first.string();
  EXPECT_EQ(object_ledger_rows(ledger_after), object_ledger_rows(ledger_before)) << ledger_after;
  // Invariant: terrain still bakes the scoped level, so its row is still in the ledger.
  EXPECT_NE(ledger_after.find("testworld|terrain|8|"), std::string::npos) << ledger_after;
}

// Detects (invariant): a grid starting below level 4 read as a partial scope, so an unscoped bake loses its objects.
TEST(CApiLod, AWorldGridBelowTheLodLevelFloorStillBakesItsObjects) {
  const Fixture fixture;
  object_lod_prepare(fixture);
  // Invariant: only a level the object stage would bake can make a scope partial, so 2..32 bakes the same objects.
  write_bytes(fixture.root / "LODSettings" / "TestWorld.LOD",
              fo4::lod::serialize_lod_settings(fo4::lod::LodSettings{0, 0, 64, 2, 32}));
  radicle_lod* lod = nullptr;
  ASSERT_EQ(radicle_lod_open(fixture.plugin_paths.c_str(), fixture.data_dir.c_str(), "", &lod), RADICLE_STATUS_OK);
  uint8_t* report = nullptr;
  size_t report_size = 0;
  uint8_t* ledger = nullptr;
  size_t ledger_size = 0;
  ASSERT_EQ(radicle_lod_generate_json(lod, "", 1, &report, &report_size, &ledger, &ledger_size), RADICLE_STATUS_OK);
  const auto text = text_of(report, report_size);
  const auto kept = text_of(ledger, ledger_size);
  release(report);
  release(ledger);

  EXPECT_EQ(text.find("objectsSkipped"), std::string::npos) << text;
  EXPECT_EQ(object_lod_tiles(fixture.root, ".bto").size(), 4u);
  EXPECT_TRUE(std::filesystem::exists(
      object_lod_texture_file(fixture.root, "data\\Textures\\Terrain\\TestWorld\\Objects\\TestWorld.Objects.DDS")));
  EXPECT_EQ(object_ledger_rows(kept).size(), 4u) << kept;
  radicle_lod_destroy(lod);
}

// Detects (invariant): a cancel reported before the tile's own rows are merged, so a finished tile loses its atlases.
TEST(CApiLod, ACancelSeenMidRunRecordsOnlyTheFinishedTileAndNoObjects) {
  const Fixture fixture;
  object_lod_prepare(fixture);
  radicle_lod* lod = nullptr;
  ASSERT_EQ(radicle_lod_open(fixture.plugin_paths.c_str(), fixture.data_dir.c_str(), "", &lod), RADICLE_STATUS_OK);
  CancelOnFirstReport state;
  radicle_lod_scope scope{};
  scope.progress = &cancel_on_first_report;
  scope.user = &state;
  scope.cancel = &state.flag;
  uint8_t* report = nullptr;
  size_t report_size = 0;
  uint8_t* ledger = nullptr;
  size_t ledger_size = 0;
  ASSERT_EQ(radicle_lod_generate_scoped_json(lod, "", 1, "TestWorld", &scope, &report, &report_size, &ledger,
                                             &ledger_size),
            RADICLE_STATUS_OK);
  const auto text = text_of(report, report_size);
  const auto kept = text_of(ledger, ledger_size);
  release(report);
  release(ledger);

  EXPECT_EQ(state.reports, 1u);
  EXPECT_NE(text.find("\"cancelled\":true"), std::string::npos) << text;
  // Invariant: a reported tile is fully merged, so the ledger holds its terrain row and both of its texture rows.
  const auto terrain_rows = ledger_rows_with(kept, "|terrain|");
  const auto texture_rows = ledger_rows_with(kept, "|texture|");
  ASSERT_EQ(terrain_rows.size(), 1u) << kept;
  ASSERT_EQ(texture_rows.size(), 2u) << kept;
  EXPECT_EQ(ledger_row_count(kept), 3u) << kept;
  EXPECT_TRUE(object_ledger_rows(kept).empty()) << kept;
  for (const auto& row : texture_rows) {
    EXPECT_EQ(ledger_tile_coords(row), ledger_tile_coords(terrain_rows.front())) << row << kept;
  }
  EXPECT_TRUE(object_lod_tiles(fixture.root, ".bto").empty()) << kept;
  EXPECT_FALSE(std::filesystem::exists(
      object_lod_texture_file(fixture.root, "data\\Textures\\Terrain\\TestWorld\\Objects\\TestWorld.Objects.DDS")));
  radicle_lod_destroy(lod);
}

// Detects (invariant): the two-pass object stage writing one tile of a multi-tile level with another tile's bytes.
TEST(CApiLod, TwoTilesPerLevelWriteTheSameBytesAsTheAllAtOnceReference) {
  const Fixture fixture;
  object_lod_prepare(fixture, ObjectLodRefs::two_tiles);
  radicle_lod* lod = nullptr;
  ASSERT_EQ(radicle_lod_open(fixture.plugin_paths.c_str(), fixture.data_dir.c_str(), "", &lod), RADICLE_STATUS_OK);
  const auto generate = [&](const std::string& ledger_in, std::string& ledger_out) {
    uint8_t* report = nullptr;
    size_t report_size = 0;
    uint8_t* ledger = nullptr;
    size_t ledger_size = 0;
    EXPECT_EQ(radicle_lod_generate_json(lod, ledger_in.c_str(), 1, &report, &report_size, &ledger, &ledger_size),
              RADICLE_STATUS_OK);
    auto text = text_of(report, report_size);
    ledger_out = text_of(ledger, ledger_size);
    release(report);
    release(ledger);
    return text;
  };
  std::string first_ledger;
  const auto first = generate("", first_ledger);
  expect_absent_count(first, "failed");
  expect_absent_count(first, "empty");

  const std::vector<std::filesystem::path> plugins{fixture.root / "Test.esp"};
  const auto environment = fo4::env::Environment::load(plugins);
  const std::vector<std::filesystem::path> roots{fixture.root};
  const auto assets = fo4::archive::AssetResolver::open_roots(roots, std::vector<std::filesystem::path>{});
  const auto worlds = fo4::lod::list_lod_worlds(environment, assets);
  ASSERT_EQ(worlds.size(), 1u);
  const auto& world = worlds.front();
  std::vector<fo4::lod::LodTile> origins_all;
  std::vector<fo4::lod::ObjectTile> all;
  std::vector<std::size_t> second_tile_of_level;
  fo4::lod::for_each_object_level(world.settings, [&](std::int32_t level) {
    const auto candidates = fo4::lod::collect_object_lod_candidates(environment, world.key, level);
    const auto origins = fo4::lod::object_tiles(world, level, candidates);
    auto built = fo4::lod::build_object_tiles(assets, world.settings, level, origins, candidates);
    EXPECT_EQ(origins.size(), 2u) << level;
    ASSERT_EQ(built.size(), origins.size()) << level;
    second_tile_of_level.push_back(origins_all.size() + 1);
    for (std::size_t index = 0; index < origins.size(); ++index) {
      origins_all.push_back(origins[index]);
      all.push_back(std::move(built[index]));
    }
  });
  ASSERT_EQ(all.size(), 8u);
  const auto atlas = fo4::lod::bake_object_atlas(assets, all);
  EXPECT_EQ(object_lod_tiles(fixture.root, ".bto").size(), 8u);

  const auto expect_reference = [&](std::string_view label) {
    for (std::size_t index = 0; index < all.size(); ++index) {
      const auto written = read_bytes(fo4::lod::lod_tile_local_path(fixture.root, origins_all[index]));
      EXPECT_EQ(written, fo4::lod::write_object_bto(all[index], world.name))
          << label << " level " << origins_all[index].level << " x " << origins_all[index].x;
    }
    for (std::size_t map = 0; map < fo4::lod::k_object_atlas_maps; ++map) {
      const auto kind = static_cast<fo4::lod::ObjectAtlasMap>(map);
      EXPECT_EQ(read_bytes(fo4::lod::object_atlas_local_path(fixture.root, world.name, kind)),
                fo4::lod::write_object_atlas_dds(atlas, kind))
          << label << " map " << map;
    }
  };
  expect_reference("first run");
  const auto ledger = fo4::lod::parse_lod_ledger(first_ledger);
  const auto content = ledger.hashes.find(fo4::lod::object_atlas_content_ledger_key(world.name));
  ASSERT_NE(content, ledger.hashes.end()) << first_ledger;
  EXPECT_EQ(content->second, atlas.content_hash);
  EXPECT_EQ(object_ledger_rows(first_ledger).size(), 8u) << first_ledger;

  // Detects (invariant): a rewrite of one tile of a multi-tile level using another tile's plan row or geometry.
  for (const auto index : second_tile_of_level) {
    std::filesystem::remove(fo4::lod::lod_tile_local_path(fixture.root, origins_all[index]));
    std::string ledger_after;
    const auto repeat = generate(first_ledger, ledger_after);
    EXPECT_NE(repeat.find("\"written\":1"), std::string::npos) << repeat;
    expect_absent_count(repeat, "texturesWritten");
    expect_absent_count(repeat, "failed");
    expect_reference("rewrite of level " + std::to_string(origins_all[index].level));
  }

  // Detects (invariant): a non-first origin previewing another tile's bytes.
  const auto preview_of = [&](std::int32_t level, std::int32_t x, std::int32_t y, std::vector<std::byte>& out) {
    uint8_t* bytes = nullptr;
    size_t size = 0;
    const auto status = radicle_lod_preview_object_nif(lod, "testworld", level, x, y, &bytes, &size);
    if (status == RADICLE_STATUS_OK) {
      out.assign(reinterpret_cast<const std::byte*>(bytes), reinterpret_cast<const std::byte*>(bytes) + size);
    }
    release(bytes);
    return status;
  };
  for (const auto& [level, x] : {std::pair<std::int32_t, std::int32_t>{4, 40}, {32, 32}}) {
    std::vector<std::byte> preview;
    ASSERT_EQ(preview_of(level, x, 0, preview), RADICLE_STATUS_OK) << level << " x " << x << radicle_last_error();
    bool matched = false;
    for (const auto& origin : origins_all) {
      if (origin.level != level || origin.x != x || origin.y != 0) continue;
      matched = true;
      EXPECT_EQ(preview, read_bytes(fo4::lod::lod_tile_local_path(fixture.root, origin))) << level << " x " << x;
    }
    EXPECT_TRUE(matched) << level << " x " << x;
  }
  std::vector<std::byte> none;
  EXPECT_EQ(preview_of(4, 20, 0, none), RADICLE_STATUS_NOT_FOUND);
  radicle_lod_destroy(lod);
}
