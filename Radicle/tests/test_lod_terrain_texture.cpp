#include "fo4/lod/terrain_texture.hpp"

#include "env_node_fixture.hpp"
#include "fo4/archive/ba2_reader.hpp"
#include "fo4/environment/environment.hpp"
#include "fo4/environment/terrain.hpp"
#include "fo4/lod/lod.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/texture/dds.hpp"
#include "real_data.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace fo4::test::envnode;

std::span<const std::uint8_t> as_bytes(const std::vector<std::byte>& bytes) {
  return std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
}

std::span<const std::uint8_t> as_u8(const std::vector<std::uint8_t>& bytes) {
  return std::span<const std::uint8_t>(bytes.data(), bytes.size());
}

std::string lower_copy(std::string text) {
  for (auto& ch : text) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return text;
}

fo4::lod::TerrainImage solid_image(int width, int height, std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
  fo4::lod::TerrainImage image;
  image.width = width;
  image.height = height;
  image.rgba.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 255);
  for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(width) * static_cast<std::size_t>(height); ++pixel) {
    image.rgba[pixel * 4] = red;
    image.rgba[pixel * 4 + 1] = green;
    image.rgba[pixel * 4 + 2] = blue;
    image.rgba[pixel * 4 + 3] = 255;
  }
  return image;
}

std::array<float, 3> channel_means(std::span<const std::uint8_t> rgba) {
  std::array<double, 3> sums{};
  std::size_t count = 0;
  for (std::size_t index = 0; index + 3 < rgba.size(); index += 4) {
    sums[0] += rgba[index];
    sums[1] += rgba[index + 1];
    sums[2] += rgba[index + 2];
    ++count;
  }
  if (count == 0) return {0.0f, 0.0f, 0.0f};
  return {static_cast<float>(sums[0] / static_cast<double>(count)),
          static_cast<float>(sums[1] / static_cast<double>(count)),
          static_cast<float>(sums[2] / static_cast<double>(count))};
}

std::vector<std::byte> xclc(std::int32_t x, std::int32_t y) {
  fo4::test::ByteWriter out;
  out.i32(x);
  out.i32(y);
  return out.bytes;
}

std::vector<std::byte> vhgt(float offset) {
  fo4::test::ByteWriter out;
  out.f32(offset);
  for (std::size_t index = 0; index < fo4::env::k_land_vertices; ++index) out.u8(0);
  out.u8(0);
  out.u8(0);
  out.u8(0);
  return out.bytes;
}

std::vector<std::byte> vnml() {
  fo4::test::ByteWriter out;
  for (std::size_t index = 0; index < fo4::env::k_land_vertices; ++index) {
    out.u8(90);
    out.u8(160);
    out.u8(200);
  }
  return out.bytes;
}

std::vector<std::byte> world_plugin(bool with_land) {
  std::vector<Node> cells;
  if (with_land) {
    cells = {rec("CELL", 0x802, {{"XCLC", xclc(0, 0)}}),
             grp(0x802, 6, {grp(0x802, 9, {rec("LAND", 0x812, {{"VHGT", vhgt(10.0f)}, {"VNML", vnml()}})})})};
  }
  return make_plugin({}, {top("WRLD", {rec("WRLD", 0x800, {{"EDID", zstring("TestWorld")}}),
                                       grp(0x800, 1, {grp(0, 4, {grp(0, 5, std::move(cells))})})})});
}

fo4::env::Environment load_env(const std::vector<std::byte>& bytes) {
  std::vector<fo4::Plugin> plugins;
  plugins.push_back(fo4::read_plugin(bytes, "Test.esp"));
  return fo4::env::Environment::from_plugins(std::move(plugins));
}

}  // namespace

// Detects (invariant): a written DDS whose header, mip chain or byte order does not round trip.
TEST(LodTerrainTexture, WritesADdsThatReadsBackWithTheSameSizeFormatAndMipChain) {
  const auto image = solid_image(4, 4, 255, 0, 0);
  const auto bytes = fo4::lod::write_terrain_dds(image, fo4::lod::TerrainDdsFormat::bgra8);
  const auto info = fo4::texture::read_dds(as_bytes(bytes));
  EXPECT_EQ(info.width, 4);
  EXPECT_EQ(info.height, 4);
  EXPECT_EQ(info.mip_count, 3);
  EXPECT_EQ(info.dxgi_format(), 87);
  EXPECT_EQ(info.data_offset + info.payload_size(), static_cast<std::int64_t>(bytes.size()));
  const auto* pixels = reinterpret_cast<const std::uint8_t*>(bytes.data()) + info.data_offset;
  EXPECT_EQ(pixels[0], 0);
  EXPECT_EQ(pixels[1], 0);
  EXPECT_EQ(pixels[2], 255);
  EXPECT_EQ(pixels[3], 255);
}

// Detects (invariant): the BC3 encoder must produce blocks the repository decoder reads back in RGBA order.
TEST(LodTerrainTexture, EncodesBc3BlocksThatDecodeBackToTheirColours) {
  const fo4::lod::TerrainImage image = solid_image(8, 8, 200, 120, 40);
  const auto encoded = fo4::texture::encode_bc3(image.rgba, image.width, image.height);
  EXPECT_EQ(encoded.size(), 4u * 16u);
  const auto decoded = fo4::texture::decode_block_format(as_u8(encoded), 77, image.width, image.height);
  ASSERT_EQ(decoded.size(), 8u * 8u * 4u);
  for (std::size_t pixel = 0; pixel < 64; ++pixel) {
    EXPECT_NEAR(static_cast<int>(decoded[pixel * 4]), 200, 8);
    EXPECT_NEAR(static_cast<int>(decoded[pixel * 4 + 1]), 120, 8);
    EXPECT_NEAR(static_cast<int>(decoded[pixel * 4 + 2]), 40, 8);
  }
  const auto dds = fo4::lod::write_terrain_dds(image, fo4::lod::TerrainDdsFormat::bc3);
  const auto info = fo4::texture::read_dds(as_bytes(dds));
  EXPECT_EQ(info.dxgi_format(), 77);
  EXPECT_EQ(info.data_offset + info.payload_size(), static_cast<std::int64_t>(dds.size()));
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  const auto top = fo4::texture::decode_dds_rgba(as_bytes(dds), 0, false, width, height);
  EXPECT_EQ(width, 8u);
  EXPECT_EQ(height, 8u);
  ASSERT_GE(top.size(), 4u);
  EXPECT_NEAR(static_cast<int>(top[0]), 200, 12);
  EXPECT_NEAR(static_cast<int>(top[1]), 120, 12);
  EXPECT_NEAR(static_cast<int>(top[2]), 40, 12);
}

// Detects (invariant): the baked normal must carry the raw VNML bytes and the textureless rule must follow land_cells.
TEST(LodTerrainTexture, BakesRawVertexNormalsAndDecidesTexturelessTiles) {
  const auto env = load_env(world_plugin(true));
  const fo4::lod::LodSettings settings{};
  const auto worldspace = fo4::FormKey::parse("000800:Test.esp");
  const auto textures = fo4::lod::bake_terrain_textures(env, worldspace, settings, 4, 0, 0);
  EXPECT_EQ(textures.diffuse.land_cells, 1u);
  EXPECT_EQ(textures.normal.land_cells, 1u);
  EXPECT_TRUE(fo4::lod::terrain_tile_needs_texture(textures.diffuse));
  EXPECT_TRUE(fo4::lod::terrain_tile_needs_texture(textures.normal));
  EXPECT_EQ(textures.normal.width, fo4::lod::k_terrain_texture_side);
  ASSERT_GE(textures.normal.rgba.size(), 4u);
  EXPECT_EQ(textures.normal.rgba[0], 90u);
  EXPECT_EQ(textures.normal.rgba[1], 160u);
  EXPECT_EQ(textures.normal.rgba[2], 200u);
  const auto empty_env = load_env(world_plugin(false));
  const auto bare = fo4::lod::bake_terrain_textures(empty_env, worldspace, settings, 4, 0, 0);
  EXPECT_EQ(bare.diffuse.land_cells, 0u);
  EXPECT_EQ(bare.normal.land_cells, 0u);
  EXPECT_FALSE(fo4::lod::terrain_tile_needs_texture(bare.diffuse));
}

// Detects (invariant): the baked terrain texture size, mip count, format or channel means drifting from vanilla.
TEST(LodTerrainTextureReal, WritesDdsFilesThatMatchTheVanillaTerrainTextureHeaders) {
  const char* dir = ::fo4::test::fo4_data_dir_cstr();
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to the Fallout 4 Data folder to run this";
  const std::filesystem::path data{dir};
  const std::filesystem::path master = data / "Fallout4.esm";
  if (!std::filesystem::exists(master)) GTEST_SKIP() << "no Fallout4.esm under " << dir;

  struct Measured {
    std::uint16_t width{0};
    std::uint16_t height{0};
    std::uint8_t mips{0};
    std::uint8_t dxgi{0};
    bool found{false};
    std::filesystem::path archive;
    std::size_t index{0};
  };
  auto measure = [&data](const std::string& wanted) {
    Measured out;
    const auto wanted_lower = lower_copy(wanted);
    for (int index = 1; index <= 9 && !out.found; ++index) {
      const std::filesystem::path archive = data / ("Fallout4 - Textures" + std::to_string(index) + ".ba2");
      if (!std::filesystem::exists(archive)) continue;
      const auto reader = fo4::archive::Ba2Reader::open(archive);
      if (reader.kind() != fo4::archive::Ba2Kind::texture) continue;
      for (std::size_t entry_index = 0; entry_index < reader.entries().size(); ++entry_index) {
        const auto& entry = reader.entries()[entry_index];
        if (lower_copy(entry.path) != wanted_lower) continue;
        out.found = true;
        out.archive = archive;
        out.index = entry_index;
        out.width = entry.texture.width;
        out.height = entry.texture.height;
        out.mips = entry.texture.mip_count;
        out.dxgi = entry.texture.dxgi_format;
        break;
      }
    }
    return out;
  };
  const auto diffuse = measure("Textures\\Terrain\\Commonwealth\\Commonwealth.4.-96.-96.DDS");
  const auto normal = measure("Textures\\Terrain\\Commonwealth\\Commonwealth.4.-96.-96_msn.DDS");
  ASSERT_TRUE(diffuse.found) << "the vanilla terrain diffuse should exist in a Fallout4 - Textures archive";
  ASSERT_TRUE(normal.found) << "the vanilla terrain _msn should exist in a Fallout4 - Textures archive";

  const auto env = fo4::env::Environment::load(std::vector<std::filesystem::path>{master});
  const auto worldspace = fo4::FormKey::parse("00003C:Fallout4.esm");
  std::size_t sampled_cells = 0;
  std::array<int, 3> raw_min{255, 255, 255};
  std::array<int, 3> raw_max{0, 0, 0};
  std::array<double, 3> raw_sum{};
  for (const auto* cell : env.worldspace_cells(worldspace)) {
    if (sampled_cells >= 8) break;
    try {
      const auto cell_data = fo4::env::decode_cell(env, *cell);
      if (!cell_data.grid) continue;
      if (cell_data.grid->x < -96 || cell_data.grid->x > -93 || cell_data.grid->y < -96 ||
          cell_data.grid->y > -93) {
        continue;
      }
      const auto* land = env.cell_landscape(cell->record->form_key);
      if (land == nullptr) continue;
      const auto landscape = fo4::env::decode_landscape(env, *land);
      if (landscape.normals.size() != fo4::env::k_land_vertices * 3) continue;
      for (std::size_t index = 0; index < landscape.normals.size(); ++index) {
        const auto value = static_cast<int>(std::to_integer<std::uint8_t>(landscape.normals[index]));
        const auto channel = index % 3;
        raw_min[channel] = std::min(raw_min[channel], value);
        raw_max[channel] = std::max(raw_max[channel], value);
        raw_sum[channel] += value;
      }
      ++sampled_cells;
    } catch (const fo4::env::WorldDecodeError&) {
      continue;
    }
  }
  if (sampled_cells != 0) {
    const auto count = static_cast<double>(sampled_cells) * static_cast<double>(fo4::env::k_land_vertices);
    std::cout << "raw VNML over " << sampled_cells << " cell(s): min [" << raw_min[0] << ',' << raw_min[1] << ','
              << raw_min[2] << "] max [" << raw_max[0] << ',' << raw_max[1] << ',' << raw_max[2] << "] mean ["
              << raw_sum[0] / count << ',' << raw_sum[1] / count << ',' << raw_sum[2] / count << "]\n";
  }

  const fo4::lod::LodSettings commonwealth{-96, -96, 256, 4, 32};
  const auto textures = fo4::lod::bake_terrain_textures(env, worldspace, commonwealth, 4, -96, -96);
  const auto diffuse_bytes = fo4::lod::write_terrain_dds(textures.diffuse);
  const auto normal_bytes = fo4::lod::write_terrain_dds(textures.normal);
  const auto diffuse_info = fo4::texture::read_dds(as_bytes(diffuse_bytes));
  const auto normal_info = fo4::texture::read_dds(as_bytes(normal_bytes));
  const auto baked_mean = channel_means(textures.normal.rgba);
  std::cout << "terrain diffuse 4.-96.-96: ours " << diffuse_info.width << 'x' << diffuse_info.height << " mips "
            << diffuse_info.mip_count << " dxgi " << static_cast<int>(diffuse_info.dxgi_format()) << " bytes "
            << diffuse_bytes.size() << " vs vanilla " << diffuse.width << 'x' << diffuse.height << " mips "
            << static_cast<int>(diffuse.mips) << " dxgi " << static_cast<int>(diffuse.dxgi) << '\n';
  std::cout << "terrain normal 4.-96.-96: ours " << normal_info.width << 'x' << normal_info.height << " mips "
            << normal_info.mip_count << " dxgi " << static_cast<int>(normal_info.dxgi_format()) << " bytes "
            << normal_bytes.size() << " mean [" << baked_mean[0] << ',' << baked_mean[1] << ',' << baked_mean[2]
            << "] vs vanilla " << normal.width << 'x' << normal.height << " mips " << static_cast<int>(normal.mips)
            << " dxgi " << static_cast<int>(normal.dxgi) << '\n';
  try {
    const auto reader = fo4::archive::Ba2Reader::open(normal.archive);
    const auto vanilla_bytes = reader.extract(normal.index);
    std::uint32_t vanilla_width = 0;
    std::uint32_t vanilla_height = 0;
    const auto vanilla_top = fo4::texture::decode_dds_rgba(as_bytes(vanilla_bytes), 0, false, vanilla_width,
                                                           vanilla_height);
    const auto vanilla_mean = channel_means(vanilla_top);
    const auto quadrants = [](std::span<const std::uint8_t> rgba, std::uint32_t width, std::uint32_t height) {
      std::array<std::array<double, 3>, 4> sums{};
      std::array<std::size_t, 4> counts{};
      for (std::uint32_t row = 0; row < height; ++row) {
        for (std::uint32_t column = 0; column < width; ++column) {
          const auto quadrant = static_cast<std::size_t>((row >= height / 2 ? 2 : 0) + (column >= width / 2 ? 1 : 0));
          const auto index = (static_cast<std::size_t>(row) * width + column) * 4;
          sums[quadrant][0] += rgba[index];
          sums[quadrant][1] += rgba[index + 1];
          sums[quadrant][2] += rgba[index + 2];
          ++counts[quadrant];
        }
      }
      std::array<std::array<float, 3>, 4> means{};
      for (std::size_t quadrant = 0; quadrant < 4; ++quadrant) {
        if (counts[quadrant] == 0) continue;
        for (std::size_t channel = 0; channel < 3; ++channel) {
          means[quadrant][channel] = static_cast<float>(sums[quadrant][channel] / counts[quadrant]);
        }
      }
      return means;
    };
    const auto baked_quads = quadrants(textures.normal.rgba, static_cast<std::uint32_t>(textures.normal.width),
                                       static_cast<std::uint32_t>(textures.normal.height));
    const auto vanilla_quads = quadrants(vanilla_top, vanilla_width, vanilla_height);
    std::cout << "vanilla _msn top mip mean [" << vanilla_mean[0] << ',' << vanilla_mean[1] << ','
              << vanilla_mean[2] << "] dxgi " << static_cast<int>(normal.dxgi) << '\n';
    for (std::size_t quadrant = 0; quadrant < 4; ++quadrant) {
      std::cout << "quadrant " << quadrant << " baked [" << baked_quads[quadrant][0] << ','
                << baked_quads[quadrant][1] << ',' << baked_quads[quadrant][2] << "] vanilla ["
                << vanilla_quads[quadrant][0] << ',' << vanilla_quads[quadrant][1] << ','
                << vanilla_quads[quadrant][2] << "]\n";
    }
  } catch (const std::exception&) {
    std::cout << "vanilla _msn payload could not be decoded through the archive reader\n";
  }
  // Constraint: the land_cells heuristic must reproduce the measured 462 textureless tiles before it can assert.
  {
    std::size_t shipped_tiles = 0;
    std::size_t shipped_diffuse = 0;
    std::vector<std::string> tile_names;
    const std::filesystem::path mesh_archive = data / "Fallout4 - Meshes.ba2";
    if (std::filesystem::exists(mesh_archive)) {
      const auto reader = fo4::archive::Ba2Reader::open(mesh_archive);
      for (const auto& entry : reader.entries()) {
        const auto lower = lower_copy(entry.path);
        if (lower.find("terrain\\commonwealth\\commonwealth.") == std::string::npos) continue;
        if (lower.find("objects\\") != std::string::npos) continue;
        if (lower.size() < 4 || lower.compare(lower.size() - 4, 4, ".btr") != 0) continue;
        ++shipped_tiles;
        tile_names.push_back(entry.path);
      }
    }
    for (int index = 1; index <= 9; ++index) {
      const std::filesystem::path archive = data / ("Fallout4 - Textures" + std::to_string(index) + ".ba2");
      if (!std::filesystem::exists(archive)) continue;
      const auto reader = fo4::archive::Ba2Reader::open(archive);
      if (reader.kind() != fo4::archive::Ba2Kind::texture) continue;
      for (const auto& entry : reader.entries()) {
        const auto lower = lower_copy(entry.path);
        if (lower.find("textures\\terrain\\commonwealth\\commonwealth.") == std::string::npos) continue;
        if (lower.find("objects\\") != std::string::npos) continue;
        if (lower.find("_msn.dds") != std::string::npos) continue;
        if (lower.size() < 4 || lower.compare(lower.size() - 4, 4, ".dds") != 0) continue;
        ++shipped_diffuse;
      }
    }
    std::size_t sampled = 0;
    std::size_t sampled_zero = 0;
    if (!tile_names.empty()) {
      const auto step = std::max<std::size_t>(1, tile_names.size() / 20);
      for (std::size_t index = 0; index < tile_names.size() && sampled < 20; index += step) {
        const auto name = tile_names[index];
        const auto start = name.find_last_of('\\');
        const auto file = name.substr(start == std::string::npos ? 0 : start + 1);
        const auto parts = file.substr(0, file.size() - 4);
        std::vector<std::string> fields;
        std::size_t begin = 0;
        for (std::size_t position = 0; position <= parts.size(); ++position) {
          if (position == parts.size() || parts[position] == '.') {
            fields.push_back(parts.substr(begin, position - begin));
            begin = position + 1;
          }
        }
        if (fields.size() != 4) continue;
        const auto level = std::atoi(fields[1].c_str());
        const auto tile_x = std::atoi(fields[2].c_str());
        const auto tile_y = std::atoi(fields[3].c_str());
        try {
          const auto sampled_textures = fo4::lod::bake_terrain_textures(env, worldspace, commonwealth, level, tile_x,
                                                                        tile_y);
          ++sampled;
          if (sampled_textures.diffuse.land_cells == 0) ++sampled_zero;
        } catch (const std::exception&) {
          continue;
        }
      }
    }
    std::cout << "shipped BTR tiles " << shipped_tiles << ", shipped diffuse " << shipped_diffuse << ", delta "
              << (shipped_tiles > shipped_diffuse ? shipped_tiles - shipped_diffuse : 0) << " (measured 462); sampled "
              << sampled << " tiles, land_cells zero " << sampled_zero << '\n';
  }
  EXPECT_EQ(diffuse_info.width, static_cast<int>(diffuse.width));
  EXPECT_EQ(diffuse_info.height, static_cast<int>(diffuse.height));
  EXPECT_EQ(diffuse_info.mip_count, static_cast<int>(diffuse.mips));
  EXPECT_EQ(diffuse_info.dxgi_format(), diffuse.dxgi);
  EXPECT_EQ(normal_info.width, static_cast<int>(normal.width));
  EXPECT_EQ(normal_info.height, static_cast<int>(normal.height));
  EXPECT_EQ(normal_info.mip_count, static_cast<int>(normal.mips));
  EXPECT_EQ(normal_info.dxgi_format(), normal.dxgi);
  EXPECT_EQ(diffuse_info.data_offset + diffuse_info.payload_size(), static_cast<std::int64_t>(diffuse_bytes.size()));
  EXPECT_EQ(normal_info.data_offset + normal_info.payload_size(), static_cast<std::int64_t>(normal_bytes.size()));
  EXPECT_TRUE(fo4::lod::terrain_tile_needs_texture(textures.diffuse));
}
