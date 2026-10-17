#include "fo4/archive/assets.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/nif.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}

bool has_extension(const std::string& path, std::string_view extension) {
  return lower(std::filesystem::path(path).extension().string()) == extension;
}

bool is_handled_geometry(std::string_view type) {
  return type == "BSTriShape" || type == "BSSubIndexTriShape" || type == "BSMeshLODTriShape" ||
         type == "BSDynamicTriShape";
}

bool is_geometry_name(std::string_view type) {
  return type.ends_with("TriShape") || type.ends_with("TriStrips");
}

struct CorpusCounts {
  std::size_t nifs{0};
  std::size_t shapes{0};
  std::size_t skinned_shapes{0};
  std::size_t shapes_with_bones{0};
  std::size_t shapes_with_material{0};
  std::size_t shapes_with_textures{0};
  std::size_t bones{0};
  std::size_t texture_slots{0};
  std::size_t supplied_textures{0};
  std::size_t failures{0};
  std::size_t weight_count_mismatches{0};
  std::size_t skinned_shapes_without_bones{0};
  std::size_t bone_index_overflows{0};
  std::size_t non_file_materials{0};
  std::size_t dds_textures{0};
};

}  // namespace

TEST(NifCorpus, ReadsEveryVanillaNifAndHandlesGeometryTypes) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  record_game_build(detect_game_build(dir));
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";

  const auto assets = fo4::archive::AssetResolver::open({}, archives);
  std::map<std::string, std::size_t> histogram;
  std::map<std::string, std::string> unhandled;
  std::map<std::uint32_t, std::size_t> bs_versions;
  std::map<std::string, std::size_t> material_extensions;
  std::map<std::string, std::size_t> texture_extensions;
  std::map<std::string, std::size_t> non_file_material_names;
  CorpusCounts counts;
  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    const auto archive_start = counts.nifs;
    for (std::size_t i = 0; i < reader.entries().size(); ++i) {
      const auto& path = reader.entries()[i].path;
      if (!has_extension(path, ".nif")) continue;
      ++counts.nifs;
      const auto bytes = reader.extract(i);
      try {
        const auto nif = fo4::nif::read_nif(bytes);
        ++bs_versions[nif.bs_version()];
        for (const auto& block : nif.blocks()) {
          if (!is_geometry_name(block.type)) continue;
          ++histogram[block.type];
          if (!is_handled_geometry(block.type)) unhandled.emplace(block.type, path);
        }
        for (const auto& shape : fo4::nif::read_shapes(nif)) {
          const auto vertex_count = shape.verts.size() / 3;
          EXPECT_TRUE(std::all_of(shape.tris.begin(), shape.tris.end(),
                                  [vertex_count](std::uint16_t index) { return index < vertex_count; }))
              << path;
          fo4::nif::shape_to_global(nif, shape.block);
          ++counts.shapes;
          if (!shape.material.empty()) {
            ++counts.shapes_with_material;
            const auto extension = lower(std::filesystem::path(shape.material).extension().string());
            ++material_extensions[extension];
            if (extension != ".bgsm" && extension != ".bgem") {
              ++counts.non_file_materials;
              ++non_file_material_names[lower(shape.material)];
            }
          }
          if (!shape.textures.empty()) {
            ++counts.shapes_with_textures;
            counts.texture_slots += shape.textures.size();
            for (const auto& texture : shape.textures) {
              if (texture.empty()) continue;
              ++counts.supplied_textures;
              const auto extension = lower(std::filesystem::path(texture).extension().string());
              ++texture_extensions[extension];
              if (extension == ".dds") ++counts.dds_textures;
            }
          }
          if (shape.weights.empty()) continue;
          ++counts.skinned_shapes;
          if (shape.weights.size() != vertex_count * 4 || shape.bone_indices.size() != vertex_count * 4) {
            ++counts.weight_count_mismatches;
          }
          if (shape.bones.empty()) {
            ++counts.skinned_shapes_without_bones;
          } else {
            ++counts.shapes_with_bones;
            counts.bones += shape.bones.size();
            for (const auto index : shape.bone_indices) {
              if (index >= shape.bones.size()) ++counts.bone_index_overflows;
            }
          }
        }
      } catch (const fo4::parse_error& error) {
        ++counts.failures;
        ADD_FAILURE() << path << ": " << error.what();
      }
    }
    RecordProperty(reader.path().filename().string() + " nif_count", std::to_string(counts.nifs - archive_start));
  }

  EXPECT_GT(counts.nifs, 0U);
  for (const auto& [type, example] : unhandled) {
    ADD_FAILURE() << "unhandled geometry block type " << type << " (first seen in " << example << ")";
  }
  for (const auto& [version, count] : bs_versions) {
    RecordProperty("bs_version_" + std::to_string(version) + "_nif_count", std::to_string(count));
  }
  for (const auto& [type, count] : histogram) std::cout << type << ": " << count << '\n';

  std::cout << "nifs " << counts.nifs << ", shapes " << counts.shapes << ", skinned shapes "
            << counts.skinned_shapes << ", shapes with bones " << counts.shapes_with_bones << ", bones "
            << counts.bones << ", shapes with material " << counts.shapes_with_material << ", shapes with textures "
            << counts.shapes_with_textures << ", texture slots " << counts.texture_slots << ", supplied textures "
            << counts.supplied_textures << ", failures " << counts.failures << '\n';
  for (const auto& [extension, count] : material_extensions) std::cout << "material extension " << extension << ": " << count << '\n';
  for (const auto& [extension, count] : texture_extensions) std::cout << "texture extension " << extension << ": " << count << '\n';
  for (const auto& [name, count] : non_file_material_names) std::cout << "non-file material " << name << ": " << count << '\n';

  RecordProperty("nif_count", std::to_string(counts.nifs));
  RecordProperty("shape_count", std::to_string(counts.shapes));
  RecordProperty("skinned_shape_count", std::to_string(counts.skinned_shapes));
  RecordProperty("bone_count", std::to_string(counts.bones));
  RecordProperty("shapes_with_material_count", std::to_string(counts.shapes_with_material));
  RecordProperty("shapes_with_textures_count", std::to_string(counts.shapes_with_textures));
  RecordProperty("supplied_texture_count", std::to_string(counts.supplied_textures));
  RecordProperty("parse_failure_count", std::to_string(counts.failures));

  EXPECT_EQ(counts.failures, 0U);
  EXPECT_EQ(counts.weight_count_mismatches, 0U);
  EXPECT_EQ(counts.bone_index_overflows, 0U);
  EXPECT_EQ(counts.non_file_materials, 0U);
  EXPECT_GT(counts.dds_textures * 1000U, counts.supplied_textures * 999U);
  EXPECT_LT(counts.skinned_shapes_without_bones * 100U, counts.skinned_shapes);
  EXPECT_GT(counts.shapes, 0U);
  EXPECT_GT(counts.skinned_shapes, 0U);
  EXPECT_GT(counts.shapes_with_material, 0U);
  EXPECT_GT(counts.shapes_with_textures, 0U);
  EXPECT_GT(counts.bones, 0U);
}
