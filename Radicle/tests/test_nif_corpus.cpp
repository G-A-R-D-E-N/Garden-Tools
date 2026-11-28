#include "fo4/archive/assets.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/describe.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_write.hpp"
#include "fo4/nif/shader.hpp"
#include "fo4/nif/sidecars.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"
#include "real_data.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <set>
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
  std::size_t effect_shaders{0};
  std::size_t alpha_properties{0};
  std::size_t bsx_flags{0};
  std::size_t connect_points{0};
  std::size_t connect_children{0};
  std::size_t segment_tables{0};
  std::size_t segments{0};
  std::size_t extra_block_failures{0};
  std::size_t value_nodes{0};
  std::size_t value_node_mismatches{0};
};

void read_extra_blocks(const fo4::nif::NifFile& nif, const std::string& path, CorpusCounts& counts) {
  for (std::size_t i = 0; i < nif.blocks().size(); ++i) {
    const auto block = static_cast<std::uint32_t>(i);
    const auto& type = nif.blocks()[i].type;
    try {
      if (type == "BSEffectShaderProperty") {
        fo4::nif::read_effect_shader(nif, block);
        ++counts.effect_shaders;
      } else if (type == "NiAlphaProperty") {
        fo4::nif::read_alpha(nif, block);
        ++counts.alpha_properties;
      } else if (type == "BSXFlags") {
        fo4::nif::read_bsx_flags(nif, block);
        ++counts.bsx_flags;
      } else if (type == "BSSubIndexTriShape") {
        const auto table = fo4::nif::read_segments(nif, block);
        if (!table.segments.empty()) ++counts.segment_tables;
        counts.segments += table.segments.size();
      }
    } catch (const fo4::parse_error& error) {
      ++counts.extra_block_failures;
      ADD_FAILURE() << path << ": " << error.what();
    }
  }
  try {
    counts.connect_points += fo4::nif::read_connect_parents(nif).size();
    counts.connect_children += fo4::nif::read_connect_children(nif).size();
  } catch (const fo4::parse_error& error) {
    ++counts.extra_block_failures;
    ADD_FAILURE() << path << ": " << error.what();
  }
}

}  // namespace

TEST(NifCorpus, ReadsEveryVanillaNifAndHandlesGeometryTypes) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives");
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
        read_extra_blocks(nif, path, counts);
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
  RecordProperty("extra_block_failure_count", std::to_string(counts.extra_block_failures));
  std::cout << "effect shaders " << counts.effect_shaders << ", alpha properties " << counts.alpha_properties
            << ", BSXFlags values " << counts.bsx_flags << ", connect points " << counts.connect_points
            << ", connect children " << counts.connect_children << ", segment tables " << counts.segment_tables
            << " (" << counts.segments << " segments), extra block failures " << counts.extra_block_failures << '\n';
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
  EXPECT_EQ(counts.extra_block_failures, 0U);
  EXPECT_GT(counts.effect_shaders, 0U);
  EXPECT_GT(counts.alpha_properties, 0U);
  EXPECT_GT(counts.connect_points, 0U);
  EXPECT_GT(counts.segment_tables, 0U);
}

// regression: a vanilla NIF that the writer re-encodes differently must fail here, so zero-edit saves stay byte-identical
TEST(NifCorpus, WriteRoundTripsEveryVanillaNifByteForByte) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives");
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

TEST(NifCorpus, VaultGearDoorShapesCarryDistinctBakedBounds) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  record_game_build(detect_game_build(dir));
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";

  const auto assets = fo4::archive::AssetResolver::open({}, archives);
  const auto location = assets.locate_in_archives("Meshes\\Interiors\\Vault\\Doors\\VltGearDoor\\VltGearDoor01.nif");
  ASSERT_TRUE(location.has_value());
  const auto nif = fo4::nif::read_nif(assets.read(*location));

  using Vec3 = std::array<float, 3>;
  constexpr float inf = std::numeric_limits<float>::infinity();
  struct Bounds {
    std::string name;
    Vec3 lo;
    Vec3 hi;
  };
  std::vector<Bounds> boxes;
  Vec3 union_lo{inf, inf, inf};
  Vec3 union_hi{-inf, -inf, -inf};
  std::set<std::array<long long, 3>> centres;
  for (const auto& shape : fo4::nif::read_shapes(nif)) {
    if (shape.skin_ref >= 0 || shape.verts.empty()) continue;
    const auto world = fo4::nif::shape_to_global(nif, shape.block);
    Vec3 lo{inf, inf, inf};
    Vec3 hi{-inf, -inf, -inf};
    for (std::size_t v = 0; v + 2 < shape.verts.size(); v += 3) {
      for (std::size_t i = 0; i < 3; ++i) {
        float p = world.t[i];
        for (std::size_t k = 0; k < 3; ++k) p += world.s * world.r[i * 3 + k] * shape.verts[v + k];
        lo[i] = std::min(lo[i], p);
        hi[i] = std::max(hi[i], p);
      }
    }
    const auto& r = world.r;
    const double rotation_det = r[0] * (r[4] * r[8] - r[5] * r[7]) - r[1] * (r[3] * r[8] - r[5] * r[6]) +
                                r[2] * (r[3] * r[7] - r[4] * r[6]);
    const double determinant = static_cast<double>(world.s) * world.s * world.s * rotation_det;
    std::cout << "door shape '" << shape.name << "' block " << shape.block << " translation (" << world.t[0] << ", "
              << world.t[1] << ", " << world.t[2] << ") scale " << world.s << " determinant " << determinant
              << " box centre (" << (lo[0] + hi[0]) / 2 << ", " << (lo[1] + hi[1]) / 2 << ", "
              << (lo[2] + hi[2]) / 2 << ")\n";
    for (std::size_t i = 0; i < 3; ++i) {
      union_lo[i] = std::min(union_lo[i], lo[i]);
      union_hi[i] = std::max(union_hi[i], hi[i]);
    }
    centres.insert({std::llround((lo[0] + hi[0]) * 500.0), std::llround((lo[1] + hi[1]) * 500.0),
                    std::llround((lo[2] + hi[2]) * 500.0)});
    boxes.push_back({shape.name, lo, hi});
  }

  ASSERT_GE(boxes.size(), 2U);
  for (const auto& box : boxes) {
    for (std::size_t i = 0; i < 3; ++i) {
      EXPECT_GE(box.lo[i], union_lo[i] - 1e-3F) << box.name;
      EXPECT_LE(box.hi[i], union_hi[i] + 1e-3F) << box.name;
    }
  }
  EXPECT_GE(centres.size(), 2U) << "every baked shape box shares one centre, so the transforms were not applied";
}

// regression: geometry_nif must bake each rigid shape through shape_to_global, so a door that loses its node transforms collapses to the origin
TEST(NifCorpus, VaultGearDoorGeometryNifBakesTheComposedWorldTransform) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  record_game_build(detect_game_build(dir));
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";

  const auto assets = fo4::archive::AssetResolver::open({}, archives);
  const auto location = assets.locate_in_archives("Meshes\\Interiors\\Vault\\Doors\\VltGearDoor\\VltGearDoor01.nif");
  ASSERT_TRUE(location.has_value());
  const auto nif = fo4::nif::read_nif(assets.read(*location));

  using Vec3 = std::array<float, 3>;
  constexpr float inf = std::numeric_limits<float>::infinity();
  struct Box {
    Vec3 lo{inf, inf, inf};
    Vec3 hi{-inf, -inf, -inf};
    void include(const Vec3& p) {
      for (std::size_t i = 0; i < 3; ++i) {
        lo[i] = std::min(lo[i], p[i]);
        hi[i] = std::max(hi[i], p[i]);
      }
    }
  };

  std::map<std::uint32_t, Box> expected;
  std::map<std::uint32_t, Box> local;
  Box expected_union;
  for (const auto& shape : fo4::nif::read_shapes(nif)) {
    if (shape.skin_ref >= 0 || shape.verts.empty() || fo4::nif::billboard_mode(nif, shape.block) >= 0) continue;
    const auto world = fo4::nif::shape_to_global(nif, shape.block);
    Box box;
    Box raw;
    for (std::size_t v = 0; v + 2 < shape.verts.size(); v += 3) {
      Vec3 p{};
      for (std::size_t i = 0; i < 3; ++i) {
        p[i] = world.t[i];
        for (std::size_t k = 0; k < 3; ++k) p[i] += world.s * world.r[i * 3 + k] * shape.verts[v + k];
      }
      box.include(p);
      expected_union.include(p);
      raw.include({shape.verts[v], shape.verts[v + 1], shape.verts[v + 2]});
    }
    expected[shape.block] = box;
    local[shape.block] = raw;
  }
  ASSERT_GE(expected.size(), 2U);

  const auto report = fo4::nif::geometry_nif(nif);
  Box baked_union;
  std::size_t checked = 0;
  bool any_moved = false;
  for (const auto& shape : report.shapes) {
    const auto found = expected.find(shape.block);
    if (found == expected.end()) continue;
    ASSERT_FALSE(shape.verts.empty()) << shape.name;
    Box baked;
    for (std::size_t v = 0; v + 2 < shape.verts.size(); v += 3) {
      const Vec3 p{shape.verts[v], shape.verts[v + 1], shape.verts[v + 2]};
      baked.include(p);
      baked_union.include(p);
    }
    constexpr float tolerance = 1e-3F;
    for (std::size_t i = 0; i < 3; ++i) {
      EXPECT_NEAR(baked.lo[i], found->second.lo[i], tolerance) << shape.name << " axis " << i;
      EXPECT_NEAR(baked.hi[i], found->second.hi[i], tolerance) << shape.name << " axis " << i;
      EXPECT_GE(baked.lo[i], expected_union.lo[i] - tolerance) << shape.name;
      EXPECT_LE(baked.hi[i], expected_union.hi[i] + tolerance) << shape.name;
      const auto& raw = local.at(shape.block);
      if (std::abs(baked.lo[i] - raw.lo[i]) > 0.01F || std::abs(baked.hi[i] - raw.hi[i]) > 0.01F) any_moved = true;
    }
    ++checked;
  }
  EXPECT_EQ(checked, expected.size());
  for (std::size_t i = 0; i < 3; ++i) {
    EXPECT_NEAR(baked_union.lo[i], expected_union.lo[i], 1e-3F);
    EXPECT_NEAR(baked_union.hi[i], expected_union.hi[i], 1e-3F);
  }
  EXPECT_TRUE(any_moved) << "no shape's baked bounds differ from its local bounds, so no node transform was applied";
}

// regression: a field read at the wrong width leaves bytes unconsumed, so every vanilla shader block must end at its size
TEST(NifCorpus, ReadsEveryVanillaShaderBlockToItsDeclaredSize) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives");
  record_game_build(detect_game_build(dir));
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";

  const auto assets = fo4::archive::AssetResolver::open({}, archives);
  std::map<std::uint32_t, std::size_t> material_types;
  std::size_t lighting = 0;
  std::size_t effect = 0;
  std::size_t alpha = 0;
  std::size_t water = 0;
  std::size_t sky = 0;
  std::size_t unconsumed = 0;
  std::size_t failures = 0;
  std::size_t texture_sets = 0;
  std::size_t texture_sets_without_ten = 0;
  std::size_t effect_slots_without_five = 0;
  std::size_t first_effect_textured = 0;
  std::string first_unconsumed;
  std::string first_failure;

  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size(); ++i) {
      const auto& path = reader.entries()[i].path;
      if (!has_extension(path, ".nif")) continue;
      try {
        const auto nif = fo4::nif::read_nif(reader.extract(i));
        for (std::size_t block = 0; block < nif.blocks().size(); ++block) {
          const auto index = static_cast<std::uint32_t>(block);
          const auto& info = nif.blocks()[block];
          if (info.type == "BSLightingShaderProperty") {
            ++lighting;
            const auto shader = fo4::nif::read_lighting_shader(nif, index);
            ++material_types[shader.shader_type];
            if (shader.offsets.end != info.size) {
              ++unconsumed;
              if (first_unconsumed.empty()) {
                first_unconsumed = path + " block " + std::to_string(block) + " (type " +
                                   std::to_string(shader.shader_type) + ") ends at " +
                                   std::to_string(shader.offsets.end) + " of " + std::to_string(info.size);
              }
            }
            if (shader.texture_set_ref >= 0) {
              ++texture_sets;
              if (shader.textures.size() != 10U) ++texture_sets_without_ten;
            }
          } else if (info.type == "BSEffectShaderProperty") {
            ++effect;
            const auto shader = fo4::nif::read_effect_shader(nif, index);
            if (shader.offsets.end != info.size) {
              ++unconsumed;
              if (first_unconsumed.empty()) first_unconsumed = path + " block " + std::to_string(block) + " (effect)";
            }
            if (shader.textures.size() != 5U) ++effect_slots_without_five;
            if (!shader.textures[0].empty() && !shader.textures[2].empty()) ++first_effect_textured;
          } else if (info.type == "NiAlphaProperty") {
            ++alpha;
            const auto property = fo4::nif::read_alpha(nif, index);
            if (property.end != info.size) {
              ++unconsumed;
              if (first_unconsumed.empty()) first_unconsumed = path + " block " + std::to_string(block) + " (alpha)";
            }
          } else if (info.type == "BSWaterShaderProperty") {
            ++water;
            const auto shader = fo4::nif::read_water_shader(nif, index);
            if (shader.offsets.end != info.size) {
              ++unconsumed;
              if (first_unconsumed.empty()) first_unconsumed = path + " block " + std::to_string(block) + " (water)";
            }
          } else if (info.type == "BSSkyShaderProperty") {
            ++sky;
            const auto shader = fo4::nif::read_sky_shader(nif, index);
            if (shader.offsets.end != info.size) {
              ++unconsumed;
              if (first_unconsumed.empty()) first_unconsumed = path + " block " + std::to_string(block) + " (sky)";
            }
          }
        }
      } catch (const fo4::parse_error& error) {
        ++failures;
        if (first_failure.empty()) first_failure = path + ": " + error.parse_message();
      }
    }
  }

  for (const auto& [type, count] : material_types) {
    std::cout << "lighting material type " << type << ": " << count << '\n';
    RecordProperty("lighting_material_type_" + std::to_string(type) + "_count", std::to_string(count));
  }
  std::cout << "lighting shaders " << lighting << ", effect shaders " << effect << ", alpha properties " << alpha
            << ", water shaders " << water << ", sky shaders " << sky
            << ", texture sets " << texture_sets << " of which " << texture_sets_without_ten
            << " hold other than ten slots, effect shaders with a source and an env map " << first_effect_textured
            << ", blocks not consumed exactly " << unconsumed << ", parse failures " << failures << '\n';
  if (!first_unconsumed.empty()) std::cout << "first block not consumed exactly: " << first_unconsumed << '\n';
  RecordProperty("lighting_shader_count", std::to_string(lighting));
  RecordProperty("effect_shader_count", std::to_string(effect));
  RecordProperty("alpha_property_count", std::to_string(alpha));
  RecordProperty("water_shader_count", std::to_string(water));
  RecordProperty("sky_shader_count", std::to_string(sky));
  RecordProperty("unconsumed_shader_block_count", std::to_string(unconsumed));

  EXPECT_EQ(failures, 0U) << first_failure;
  EXPECT_EQ(unconsumed, 0U) << first_unconsumed;
  EXPECT_EQ(texture_sets_without_ten, 0U);
  EXPECT_EQ(effect_slots_without_five, 0U);
  EXPECT_GT(lighting, 0U);
  EXPECT_GT(effect, 0U);
  EXPECT_GT(alpha, 0U);
  EXPECT_GT(water, 0U);
  EXPECT_GT(sky, 0U);
  EXPECT_GT(material_types.size(), 0U);
}

// Detects: a vanilla .ssf that fails to parse or loses a field on rewrite; every file must parse and its whole document must come back unchanged
TEST(NifCorpus, SsfSidecarsRoundTripAcrossGeneralArchives) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives");
  record_game_build(detect_game_build(dir));
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";

  const auto assets = fo4::archive::AssetResolver::open({}, archives);
  std::size_t ssf_count = 0;
  std::size_t ssf_with_deltas = 0;
  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size(); ++i) {
      const auto& path = reader.entries()[i].path;
      if (!has_extension(path, ".ssf")) continue;
      ++ssf_count;
      const auto bytes = reader.extract(i);
      const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
      try {
        const auto shapes = fo4::nif::read_ssf(text);
        for (const auto& shape : shapes) {
          if (!shape.deltas.empty()) {
            ++ssf_with_deltas;
            break;
          }
        }
        EXPECT_EQ(nlohmann::json::parse(fo4::nif::write_ssf(shapes)), nlohmann::json::parse(text)) << path;
      } catch (const fo4::parse_error& error) {
        ADD_FAILURE() << path << ": " << error.what();
      }
    }
  }
  EXPECT_GT(ssf_count, 0U);
  EXPECT_GT(ssf_with_deltas, 0U);
}

// Detects: a bone ID hash that drifted from the engine's; most set segment bone IDs must match a string in their own NIF, so matched * 2 must exceed the set count
TEST(NifCorpus, SegmentBoneIdsMatchNamesInTheirOwnNif) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives");
  record_game_build(detect_game_build(dir));
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";

  const auto assets = fo4::archive::AssetResolver::open({}, archives);
  std::size_t matched = 0;
  std::size_t set = 0;
  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size(); ++i) {
      const auto& path = reader.entries()[i].path;
      if (!has_extension(path, ".nif")) continue;
      try {
        const auto bytes = reader.extract(i);
        const auto nif = fo4::nif::read_nif(bytes);
        std::vector<std::uint32_t> name_hashes;
        for (const auto& name : nif.strings()) name_hashes.push_back(fo4::nif::bone_id_hash(name));
        for (std::uint32_t block = 0; block < nif.blocks().size(); ++block) {
          if (nif.blocks()[block].type != "BSSubIndexTriShape") continue;
          for (const auto& user : fo4::nif::read_segments(nif, block).user_data) {
            if (user.bone_id == 0xFFFFFFFFU) continue;
            ++set;
            if (std::find(name_hashes.begin(), name_hashes.end(), user.bone_id) != name_hashes.end()) ++matched;
          }
        }
      } catch (const fo4::parse_error& error) {
        ADD_FAILURE() << path << ": " << error.what();
      }
    }
  }
  EXPECT_GT(set, 0U);
  EXPECT_GT(matched * 2, set);
}

// Detects: a vanilla .sclp that fails to parse or does not survive a write and read cycle; every file must parse and round-trip exactly
TEST(NifCorpus, SclpSidecarsRoundTripAcrossGeneralArchives) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives");
  record_game_build(detect_game_build(dir));
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";

  const auto assets = fo4::archive::AssetResolver::open({}, archives);
  std::size_t sclp_count = 0;
  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size(); ++i) {
      const auto& path = reader.entries()[i].path;
      if (!has_extension(path, ".sclp")) continue;
      ++sclp_count;
      const auto bytes = reader.extract(i);
      const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
      try {
        const auto bones = fo4::nif::read_sclp(text);
        EXPECT_EQ(fo4::nif::read_sclp(fo4::nif::write_sclp(bones)), bones) << path;
      } catch (const fo4::parse_error& error) {
        ADD_FAILURE() << path << ": " << error.what();
      }
    }
  }
  EXPECT_GT(sclp_count, 0U);
}
