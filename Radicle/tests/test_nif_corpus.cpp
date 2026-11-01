#include "fo4/archive/assets.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_write.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
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

bool names_a_node(std::string_view type) { return type.find("Node") != std::string_view::npos; }

bool name_numbers_value(std::string_view name, std::int32_t value) {
  const auto start = name.find_first_of("0123456789");
  if (start == std::string_view::npos) return false;
  const auto end = std::min(name.find_first_not_of("0123456789", start), name.size());
  std::int64_t number{};
  const auto parsed = std::from_chars(name.data() + start, name.data() + end, number);
  return parsed.ec == std::errc{} && number == value;
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
  std::size_t nodes{0};
  std::size_t nifs_with_nodes{0};
  std::size_t nifs_without_nodes{0};
  std::size_t shapes_with_node_parent{0};
  std::size_t shapes_without_node_parent{0};
  std::size_t parent_disagreements{0};
  std::size_t non_node_named_blocks{0};
  std::size_t shapes_with_warnings{0};
  std::size_t value_nodes{0};
  std::size_t value_node_mismatches{0};
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
  std::map<std::string, std::size_t> node_types;
  std::map<std::string, std::string> non_node_named_types;
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
          if (is_geometry_name(block.type)) {
            ++histogram[block.type];
            if (!is_handled_geometry(block.type)) unhandled.emplace(block.type, path);
          }
          if (names_a_node(block.type) && nif.node_for_block(static_cast<std::uint32_t>(&block - nif.blocks().data())) < 0) {
            ++counts.non_node_named_blocks;
            non_node_named_types.emplace(block.type, path);
          }
        }
        counts.nodes += nif.nodes().size();
        if (nif.nodes().empty()) {
          ++counts.nifs_without_nodes;
        } else {
          ++counts.nifs_with_nodes;
        }
        for (const auto& node : nif.nodes()) {
          ++node_types[node.type];
          EXPECT_EQ(nif.node_for_block(node.block), static_cast<std::int32_t>(&node - nif.nodes().data())) << path;
          if (node.parent < 0) {
            EXPECT_EQ(nif.parent(node.block), -1) << path;
          } else {
            const auto& parent = nif.nodes()[static_cast<std::size_t>(node.parent)];
            EXPECT_NE(std::find(parent.children.begin(), parent.children.end(), node.block), parent.children.end())
                << path;
          }
          for (const auto child : node.children) {
            EXPECT_EQ(nif.parent(child), static_cast<std::int32_t>(node.block)) << path;
          }
          if (node.type == "BSValueNode") {
            ++counts.value_nodes;
            if (!node.value || !name_numbers_value(node.name, *node.value)) {
              ++counts.value_node_mismatches;
              ADD_FAILURE() << path << ": BSValueNode '" << node.name << "' does not hold its name's number as a value";
            }
          }
        }
        for (const auto& shape : fo4::nif::read_shapes(nif)) {
          if (!shape.warning.empty()) ++counts.shapes_with_warnings;
          const auto vertex_count = shape.verts.size() / 3;
          EXPECT_TRUE(std::all_of(shape.tris.begin(), shape.tris.end(),
                                  [vertex_count](std::uint16_t index) { return index < vertex_count; }))
              << path;
          fo4::nif::shape_to_global(nif, shape.block);
          const auto shape_parent = nif.parent(shape.block);
          if (shape_parent < 0) {
            ++counts.shapes_without_node_parent;
            EXPECT_EQ(shape.parent_node, -1) << path;
          } else {
            ++counts.shapes_with_node_parent;
            if (shape.parent_node != nif.node_for_block(static_cast<std::uint32_t>(shape_parent))) {
              ++counts.parent_disagreements;
              ADD_FAILURE() << path << ": shape parent node disagrees with the block parent";
            }
          }
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
  for (const auto& [type, count] : node_types) std::cout << "node " << type << ": " << count << '\n';
  for (const auto& [type, example] : non_node_named_types) {
    std::cout << "not a node despite the name " << type << " (first seen in " << example << ")\n";
  }

  std::cout << "nifs " << counts.nifs << ", shapes " << counts.shapes << ", skinned shapes "
            << counts.skinned_shapes << ", shapes with bones " << counts.shapes_with_bones << ", bones "
            << counts.bones << ", shapes with material " << counts.shapes_with_material << ", shapes with textures "
            << counts.shapes_with_textures << ", texture slots " << counts.texture_slots << ", supplied textures "
            << counts.supplied_textures << ", failures " << counts.failures << '\n';
  std::cout << "nodes " << counts.nodes << " in " << counts.nifs_with_nodes << " NIFs; "
            << counts.nifs_without_nodes << " NIFs hold no node; " << counts.shapes_with_node_parent
            << " shapes have a node parent, " << counts.shapes_without_node_parent << " do not; "
            << counts.shapes_with_warnings << " shapes carry a warning\n";
  for (const auto& [extension, count] : material_extensions) std::cout << "material extension " << extension << ": " << count << '\n';
  for (const auto& [extension, count] : texture_extensions) std::cout << "texture extension " << extension << ": " << count << '\n';
  for (const auto& [name, count] : non_file_material_names) std::cout << "non-file material " << name << ": " << count << '\n';

  RecordProperty("nif_count", std::to_string(counts.nifs));
  RecordProperty("shape_count", std::to_string(counts.shapes));
  RecordProperty("skinned_shape_count", std::to_string(counts.skinned_shapes));
  RecordProperty("bone_count", std::to_string(counts.bones));
  RecordProperty("node_count", std::to_string(counts.nodes));
  RecordProperty("nifs_with_nodes_count", std::to_string(counts.nifs_with_nodes));
  RecordProperty("shape_node_parent_count", std::to_string(counts.shapes_with_node_parent));
  RecordProperty("shape_without_node_parent_count", std::to_string(counts.shapes_without_node_parent));
  RecordProperty("shape_warning_count", std::to_string(counts.shapes_with_warnings));
  RecordProperty("shapes_with_material_count", std::to_string(counts.shapes_with_material));
  RecordProperty("shapes_with_textures_count", std::to_string(counts.shapes_with_textures));
  RecordProperty("supplied_texture_count", std::to_string(counts.supplied_textures));
  RecordProperty("parse_failure_count", std::to_string(counts.failures));
  RecordProperty("non_node_named_block_count", std::to_string(counts.non_node_named_blocks));
  RecordProperty("value_node_count", std::to_string(counts.value_nodes));

  EXPECT_EQ(counts.failures, 0U);
  EXPECT_EQ(counts.value_node_mismatches, 0U);
  EXPECT_EQ(counts.weight_count_mismatches, 0U);
  EXPECT_EQ(counts.bone_index_overflows, 0U);
  EXPECT_EQ(counts.parent_disagreements, 0U);
  EXPECT_EQ(counts.non_file_materials, 0U);
  EXPECT_GT(counts.dds_textures * 1000U, counts.supplied_textures * 999U);
  EXPECT_LT(counts.skinned_shapes_without_bones * 100U, counts.skinned_shapes);
  EXPECT_GT(counts.shapes, 0U);
  EXPECT_GT(counts.skinned_shapes, 0U);
  EXPECT_GT(counts.shapes_with_material, 0U);
  EXPECT_GT(counts.shapes_with_textures, 0U);
  EXPECT_GT(counts.bones, 0U);
  EXPECT_GT(counts.nodes, 0U);
  EXPECT_GT(counts.value_nodes, 0U);
  EXPECT_GT(counts.shapes_with_node_parent, 0U);
  EXPECT_EQ(counts.shapes_with_warnings, 0U);
}

// regression: a vanilla NIF that the writer re-encodes differently must fail here, so zero-edit saves stay byte-identical
TEST(NifCorpus, WriteRoundTripsEveryVanillaNifByteForByte) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  record_game_build(detect_game_build(dir));
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";

  const auto assets = fo4::archive::AssetResolver::open({}, archives);
  std::size_t nif_count = 0;
  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size(); ++i) {
      const auto& path = reader.entries()[i].path;
      if (!has_extension(path, ".nif")) continue;
      ++nif_count;
      const auto bytes = reader.extract(i);
      try {
        EXPECT_EQ(fo4::nif::write_nif(fo4::nif::read_nif(bytes)), bytes) << path;
      } catch (const fo4::parse_error& error) {
        ADD_FAILURE() << path << ": " << error.what();
      }
    }
  }
  EXPECT_GT(nif_count, 0U);
}
