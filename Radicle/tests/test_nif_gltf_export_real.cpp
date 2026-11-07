#include "fo4/archive/assets.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/gltf_export.hpp"
#include "fo4/nif/nif.hpp"
#include "gltf_glb_reader.hpp"
#include "gltf_validate.hpp"
#include "real_data_plugins.hpp"

#include <nlohmann/json.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t k_per_folder = 10;

std::string texture_suffix(const std::string& path) {
  for (const std::string_view suffix : {"_d.dds", "_n.dds", "_s.dds"}) {
    if (path.ends_with(suffix)) return std::string(suffix);
  }
  return "other";
}

std::vector<double> position_extreme(const std::vector<float>& verts, bool lowest) {
  std::array<float, 3> value{};
  value.fill(lowest ? std::numeric_limits<float>::infinity() : -std::numeric_limits<float>::infinity());
  for (std::size_t i = 0; i < verts.size(); ++i) {
    value[i % 3] = lowest ? std::min(value[i % 3], verts[i]) : std::max(value[i % 3], verts[i]);
  }
  return std::vector<double>(value.begin(), value.end());
}

}  // namespace

TEST(NifGltfExportReal, SampledVanillaNifsKeepAccessorCountsBoundsAndSkinIndices) {
  // Regression: an exported vanilla mesh lost vertices or triangles, its POSITION bounds drifted, or a joint index pointed past its skin.
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  const auto assets = fo4::archive::AssetResolver::open({}, archives);

  std::size_t nifs = 0;
  std::size_t meshes = 0;
  std::size_t skinned = 0;
  std::size_t bones = 0;
  std::size_t warnings = 0;
  std::size_t files = 0;
  std::size_t png_fallbacks = 0;
  std::string first_png_failure;
  std::map<std::size_t, std::map<std::string, std::size_t>> slot_suffixes;
  for (const std::string_view folder : {"meshes/actors", "meshes/armor", "meshes/weapons"}) {
    const auto listing = assets.list(folder, ".nif", false);
    EXPECT_FALSE(listing.empty()) << folder;
    const auto step = std::max<std::size_t>(1, listing.size() / k_per_folder);
    for (std::size_t i = 0, picked = 0; i < listing.size() && picked < k_per_folder; i += step, ++picked) {
      const auto& entry = listing[i];
      ++nifs;
      try {
        const auto bytes = assets.read(entry.location);
        const auto nif = fo4::nif::read_nif(bytes);
        const auto shapes = fo4::nif::read_shapes(nif);
        fo4::nif::GltfOptions options;
        options.textures = fo4::nif::GltfTextureMode::copy;
        options.assets = &assets;
        const auto exported = fo4::nif::export_glb(nif, options);
        const auto glb = gltf_test::parse_glb(exported.glb);
        const auto& doc = glb.json;
        const auto faults = gltf_test::validate_glb(glb);
        EXPECT_TRUE(faults.empty()) << entry.path << ": " << (faults.empty() ? std::string{} : faults.front());
        EXPECT_EQ(exported.files.size(), doc.value("images", nlohmann::json::array()).size()) << entry.path;
        warnings += exported.warnings.size();        files += exported.files.size();
        options.textures = fo4::nif::GltfTextureMode::embed;
        const auto embedded = gltf_test::parse_glb(fo4::nif::export_glb(nif, options).glb);
        const auto embedded_faults = gltf_test::validate_glb(embedded);
        EXPECT_TRUE(embedded_faults.empty()) << entry.path << " (embed): "
                                             << (embedded_faults.empty() ? std::string{} : embedded_faults.front());
        options.textures = fo4::nif::GltfTextureMode::png;
        const auto converted = fo4::nif::export_glb(nif, options);
        for (const auto& file : converted.files) {
          if (file.path.ends_with(".dds")) ++png_fallbacks;
        }
        for (const auto& warning : converted.warnings) {
          if (warning.find("PNG conversion failed") != std::string::npos && first_png_failure.empty()) first_png_failure = warning;
        }
        const auto converted_faults = gltf_test::validate_glb(gltf_test::parse_glb(converted.glb));
        EXPECT_TRUE(converted_faults.empty()) << entry.path << " (png): "
                                              << (converted_faults.empty() ? std::string{} : converted_faults.front());
        const auto mesh_list = doc.value("meshes", nlohmann::json::array());
        const auto skin_list = doc.value("skins", nlohmann::json::array());

        std::size_t mesh = 0;
        std::size_t skin = 0;
        for (const auto& shape : shapes) {
          for (std::size_t slot = 0; slot < shape.textures.size(); ++slot) {
            if (!shape.textures[slot].empty()) ++slot_suffixes[slot][texture_suffix(shape.textures[slot])];
          }
          const std::size_t vertex_count = shape.verts.size() / 3;
          if (vertex_count == 0 || shape.tris.empty()) continue;
          ASSERT_LT(mesh, mesh_list.size()) << entry.path;
          const auto& primitive = mesh_list.at(mesh).at("primitives").at(0);
          ++mesh;
          const auto& attributes = primitive.at("attributes");

          const auto& position = doc.at("accessors").at(attributes.at("POSITION").get<std::size_t>());
          EXPECT_EQ(position.at("count").get<std::size_t>(), vertex_count) << entry.path << " " << shape.name;
          EXPECT_EQ(position.at("min").get<std::vector<double>>(), position_extreme(shape.verts, true)) << entry.path;
          EXPECT_EQ(position.at("max").get<std::vector<double>>(), position_extreme(shape.verts, false)) << entry.path;

          const auto index_accessor = primitive.at("indices").get<std::size_t>();
          EXPECT_EQ(doc.at("accessors").at(index_accessor).at("count").get<std::size_t>(), shape.tris.size());
          EXPECT_EQ(gltf_test::read_accessor(glb, index_accessor),
                    std::vector<double>(shape.tris.begin(), shape.tris.end())) << entry.path;

          if (attributes.contains("JOINTS_0")) {
            ASSERT_LT(skin, skin_list.size()) << entry.path;
            const auto& skin_json = skin_list.at(skin++);
            const auto joint_count = skin_json.at("joints").size();
            ++skinned;
            bones += shape.bones.size();
            EXPECT_EQ(joint_count, shape.bones.size()) << entry.path << " " << shape.name;
            const auto ibm = doc.at("accessors").at(skin_json.at("inverseBindMatrices").get<std::size_t>());
            EXPECT_EQ(ibm.at("count").get<std::size_t>(), shape.bones.size()) << entry.path;
            const auto joints = gltf_test::read_accessor(glb, attributes.at("JOINTS_0").get<std::size_t>());
            EXPECT_EQ(joints, std::vector<double>(shape.bone_indices.begin(), shape.bone_indices.end())) << entry.path;
            for (const double joint : joints) EXPECT_LT(joint, static_cast<double>(joint_count)) << entry.path;
          }
        }
        EXPECT_EQ(mesh, mesh_list.size()) << entry.path;
        EXPECT_EQ(skin, skin_list.size()) << entry.path;
        EXPECT_EQ(doc.value("materials", nlohmann::json::array()).size(), mesh) << entry.path;
        meshes += mesh;
      } catch (const fo4::parse_error& error) {
        ADD_FAILURE() << entry.path << ": " << error.what();
      }
    }
  }

  for (const auto& [slot, suffixes] : slot_suffixes) {
    for (const auto& [suffix, count] : suffixes) {
      std::cout << "texture slot " << slot << " " << suffix << " " << count << '\n';
    }
  }
  const auto share_of = [&slot_suffixes](std::size_t slot, const std::string& suffix) {
    const auto found = slot_suffixes.find(slot);
    if (found == slot_suffixes.end()) return 0.0;
    std::size_t total = 0;
    for (const auto& [name, count] : found->second) total += count;
    const auto hits = found->second.find(suffix);
    return total == 0 || hits == found->second.end() ? 0.0 : static_cast<double>(hits->second) / static_cast<double>(total);
  };
  // Detects: the texture slots the export reads as diffuse (0), normal (1) and smoothness/specular (7) that must hold their suffix on most vanilla shapes.
  EXPECT_GT(share_of(0, "_d.dds"), 0.8);
  EXPECT_GT(share_of(1, "_n.dds"), 0.8);
  EXPECT_GT(share_of(7, "_s.dds"), 0.8);
  std::cout << "gltf export materials: warnings=" << warnings << " files=" << files << " png_fallbacks=" << png_fallbacks << '\n';
  // Detects: a vanilla DDS that the in-process decoder cannot turn into PNG, which must fall back to DDS and show up here.
  EXPECT_EQ(png_fallbacks, 0U) << first_png_failure;
  RecordProperty("nifs_exported", std::to_string(nifs));
  RecordProperty("meshes_checked", std::to_string(meshes));
  RecordProperty("skinned_meshes_checked", std::to_string(skinned));
  RecordProperty("bones_checked", std::to_string(bones));
  std::cout << "gltf export sample: nifs=" << nifs << " skinned_meshes=" << skinned << " bones=" << bones << '\n';
  EXPECT_GT(skinned, 0U);
}
