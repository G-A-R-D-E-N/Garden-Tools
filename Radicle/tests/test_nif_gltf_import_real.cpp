#include "fo4/archive/assets.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/gltf_export.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_import.hpp"
#include "fo4/nif/shader.hpp"
#include "gltf_glb_reader.hpp"
#include "gltf_import_builder.hpp"
#include "real_data_plugins.hpp"

#include <nlohmann/json.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t k_per_folder = 10;

struct Mode {
  const char* name;
  fo4::nif::GltfTextureMode textures;
  bool strip_extras;
};

constexpr std::array<Mode, 3> k_modes{{
    {"embed", fo4::nif::GltfTextureMode::embed, false},
    {"uri", fo4::nif::GltfTextureMode::uri, false},
    {"embed-no-extras", fo4::nif::GltfTextureMode::embed, true},
}};

struct Totals {
  double position{0.0};
  double normal{0.0};
  double uv{0.0};
  double weight{0.0};
  double bind{0.0};
  double colour{0.0};
  double tangent{0.0};
  std::size_t nifs{0};
  std::size_t shapeless{0};
  std::size_t shape_checks{0};
  std::size_t skinned{0};
  std::size_t bones{0};
  std::size_t textures{0};
  std::size_t bgsm_resolved_textures{0};
  std::size_t unresolved_textures{0};
  std::size_t case_only_textures{0};
  std::size_t bgsm{0};
  std::size_t colours{0};
  std::size_t tangents{0};
  std::size_t exporter_warnings{0};
  std::size_t skinned_shaders{0};
  std::size_t skinned_with_flag{0};
  std::size_t degenerate_vertices{0};
};

void strip_extras(nlohmann::json& document) {
  for (const char* list : {"images", "materials"}) {
    if (!document.contains(list)) continue;
    for (auto& entry : document.at(list)) entry.erase("extras");
  }
}

std::map<std::string, float> weights_by_bone(const fo4::nif::Shape& shape, const std::vector<fo4::nif::Bone>& bones,
                                             std::size_t vertex) {
  std::map<std::string, float> out;
  for (std::size_t k = 0; k < 4; ++k) {
    const auto weight = shape.weights[vertex * 4 + k];
    if (weight <= 0.0f) continue;
    out[bones.at(shape.bone_indices[vertex * 4 + k]).name] += weight;
  }
  return out;
}

void compare_shape(const fo4::nif::Shape& original, const fo4::nif::Shape& imported, const std::string& where,
                   const Mode& mode, const std::vector<std::string>& exported_warnings, Totals& totals) {
  ASSERT_EQ(imported.verts.size(), original.verts.size()) << where;
  for (std::size_t i = 0; i < original.verts.size(); ++i) {
    totals.position = std::max(totals.position, static_cast<double>(std::fabs(imported.verts[i] - original.verts[i])));
  }
  EXPECT_EQ(imported.tris, original.tris) << where;
  if (!original.normals.empty()) {
    ASSERT_EQ(imported.normals.size(), original.normals.size()) << where;
    for (std::size_t i = 0; i < original.normals.size(); ++i) {
      totals.normal = std::max(totals.normal, static_cast<double>(std::fabs(imported.normals[i] - original.normals[i])));
    }
  }
  if (!original.uvs.empty()) {
    ASSERT_EQ(imported.uvs.size(), original.uvs.size()) << where;
    for (std::size_t i = 0; i < original.uvs.size(); ++i) {
      totals.uv = std::max(totals.uv, static_cast<double>(std::fabs(imported.uvs[i] - original.uvs[i])));
    }
  }
  if (!original.colors.empty()) {
    ASSERT_EQ(imported.colors.size(), original.colors.size()) << where;
    for (std::size_t i = 0; i < original.colors.size(); ++i) {
      totals.colour = std::max(totals.colour, static_cast<double>(std::fabs(imported.colors[i] - original.colors[i])));
    }
    ++totals.colours;
  }
  if (!original.tangents.empty()) {
    ASSERT_EQ(imported.tangents.size(), original.tangents.size()) << where;
    for (std::size_t i = 0; i < original.tangents.size(); ++i) {
      totals.tangent = std::max(totals.tangent, static_cast<double>(std::fabs(imported.tangents[i] - original.tangents[i])));
    }
    ++totals.tangents;
  }
  for (std::size_t slot = 0; slot < 2 && slot < original.textures.size(); ++slot) {
    const auto imported_slot = slot < imported.textures.size() ? imported.textures[slot] : std::string{};
    const auto& written = original.textures[slot];
    const auto lowered = [](std::string text) {
      std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return ch == '/' ? '\\' : static_cast<char>(std::tolower(ch));
      });
      return text;
    };
    const bool has_prefix = lowered(written).starts_with("textures\\");
    const std::string bgsm_resolved = has_prefix ? written.substr(9) : written;
    const auto file_name = lowered(written.substr(written.find_last_of("/\\") + 1));
    bool warned = false;
    for (const auto& warning : exported_warnings) {
      if (lowered(warning).find(file_name) != std::string::npos) warned = true;
    }
    if (imported_slot == written) {
      ++totals.textures;
    } else if (has_prefix && lowered(imported_slot) == lowered(bgsm_resolved)) {
      ++totals.bgsm_resolved_textures;
    } else if (lowered(imported_slot) == lowered(written)) {
      ++totals.case_only_textures;
    } else if (imported_slot.empty() && warned) {
      ++totals.unresolved_textures;
    } else {
      ADD_FAILURE() << where << " texture slot " << slot << " (" << mode.name << "): imported '" << imported_slot
                    << "' but the NIF stores '" << written << "'";
    }
  }
  if (!mode.strip_extras) {
    EXPECT_EQ(imported.material, original.material) << where << " (" << mode.name << ")";
    if (!original.material.empty()) ++totals.bgsm;
  }
  if (original.bones.empty() || original.weights.empty()) return;
  ASSERT_EQ(imported.bones.size(), original.bones.size()) << where;
  for (std::size_t b = 0; b < original.bones.size(); ++b) {
    EXPECT_EQ(imported.bones[b].name, original.bones[b].name) << where << " bone " << b;
    for (std::size_t k = 0; k < 3; ++k) {
      totals.bind = std::max(totals.bind, static_cast<double>(std::fabs(imported.bones[b].translation[k] - original.bones[b].translation[k])));
    }
    totals.bind = std::max(totals.bind, static_cast<double>(std::fabs(imported.bones[b].scale - original.bones[b].scale)));
    for (std::size_t k = 0; k < 9; ++k) {
      totals.bind = std::max(totals.bind, static_cast<double>(std::fabs(imported.bones[b].rotation[k] - original.bones[b].rotation[k])));
    }
  }
  const std::size_t vertices = original.verts.size() / 3;
  for (std::size_t v = 0; v < vertices; ++v) {
    const auto expected = weights_by_bone(original, original.bones, v);
    if (expected.empty()) {
      ++totals.degenerate_vertices;
      continue;
    }
    const auto actual = weights_by_bone(imported, imported.bones, v);
    EXPECT_EQ(actual.size(), expected.size()) << where << " vertex " << v;
    for (const auto& [name, weight] : expected) {
      const auto found = actual.find(name);
      if (found == actual.end()) {
        ADD_FAILURE() << where << " vertex " << v << " lost bone " << name;
        continue;
      }
      totals.weight = std::max(totals.weight, static_cast<double>(std::fabs(found->second - weight)));
    }
  }
  ++totals.skinned;
  totals.bones += original.bones.size();
}

}

// Detects: a vanilla NIF exported by the real exporter and re-imported must keep its positions, normals, UVs, tangents, colours, triangles, skin weights, bones and texture paths, or the edit round trip loses data.
TEST(NifGltfImportReal, ExporterOutputRoundTripsThroughTheImporter) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  const auto assets = fo4::archive::AssetResolver::open({}, archives);

  Totals totals;
  for (const std::string_view folder : {"meshes/actors", "meshes/armor", "meshes/weapons"}) {
    const auto listing = assets.list(folder, ".nif", false);
    EXPECT_FALSE(listing.empty()) << folder;
    const auto step = std::max<std::size_t>(1, listing.size() / k_per_folder);
    for (std::size_t i = 0, picked = 0; i < listing.size() && picked < k_per_folder; i += step, ++picked) {
      const auto& entry = listing[i];
      ++totals.nifs;
      try {
        const auto bytes = assets.read(entry.location);
        const auto nif = fo4::nif::read_nif(bytes);
        const auto original = fo4::nif::read_shapes(nif);
        std::vector<fo4::nif::Shape> comparable;
        for (const auto& shape : original) {
          if (!shape.verts.empty() && !shape.tris.empty()) comparable.push_back(shape);
        }
        if (comparable.empty()) {
          ++totals.shapeless;
          continue;
        }
        if (nif.blocks().size() > 0) {
          for (const auto& shape : comparable) {
            if (!shape.bones.empty() && shape.shader_ref >= 0 &&
                nif.blocks().at(static_cast<std::size_t>(shape.shader_ref)).type == "BSLightingShaderProperty") {
              const auto lighting = fo4::nif::read_lighting_shader(nif, static_cast<std::uint32_t>(shape.shader_ref));
              const auto names = fo4::nif::shader_flag_names(lighting.flags);
              ++totals.skinned_shaders;
              if (std::find(names.begin(), names.end(), std::string("Skinned")) != names.end()) ++totals.skinned_with_flag;
            }
          }
        }

        for (const auto& mode : k_modes) {
          fo4::nif::GltfOptions options;
          options.textures = mode.textures;
          options.assets = &assets;
          const auto exported = fo4::nif::export_glb(nif, options);
          totals.exporter_warnings += exported.warnings.size();
          auto glb = gltf_test::parse_glb(exported.glb);
          if (mode.strip_extras) strip_extras(glb.json);
          gltf_import_test::Gltf document{glb.json, glb.bin};

          fo4::nif::ImportOptions import;
          import.skeleton = bytes;
          const auto imported = fo4::nif::read_shapes(fo4::nif::read_nif(
              fo4::nif::import_gltf(gltf_import_test::to_glb(document), import)));

          EXPECT_EQ(imported.size(), comparable.size()) << entry.path << " (" << mode.name << ")";
          std::vector<bool> used(imported.size(), false);
          for (const auto& original_shape : comparable) {
            std::size_t match = imported.size();
            for (std::size_t k = 0; k < imported.size() && match == imported.size(); ++k) {
              if (!used[k] && imported[k].name == original_shape.name && imported[k].verts.size() == original_shape.verts.size()) {
                match = k;
              }
            }
            if (match == imported.size()) {
              ADD_FAILURE() << entry.path << " has no imported shape for " << original_shape.name << " (" << mode.name << ")";
              continue;
            }
            used[match] = true;
            compare_shape(original_shape, imported[match], entry.path + " " + original_shape.name, mode, exported.warnings,
                          totals);
            ++totals.shape_checks;
          }
        }
      } catch (const std::exception& error) {
        ADD_FAILURE() << entry.path << ": " << error.what();
      }
    }
  }

  RecordProperty("nifs_round_tripped", std::to_string(totals.nifs - totals.shapeless));
  RecordProperty("nifs_without_geometry_skipped", std::to_string(totals.shapeless));
  RecordProperty("shape_checks", std::to_string(totals.shape_checks));
  RecordProperty("texture_slots_compared", std::to_string(totals.textures));
  RecordProperty("bgsm_resolved_texture_slots", std::to_string(totals.bgsm_resolved_textures));
  RecordProperty("unresolved_texture_slots_warned_by_exporter", std::to_string(totals.unresolved_textures));
  RecordProperty("bgsm_compared", std::to_string(totals.bgsm));
  RecordProperty("colour_shapes_compared", std::to_string(totals.colours));
  RecordProperty("tangent_shapes_compared", std::to_string(totals.tangents));
  RecordProperty("skinned_shapes_compared", std::to_string(totals.skinned));
  RecordProperty("bones_compared", std::to_string(totals.bones));
  RecordProperty("exporter_warnings", std::to_string(totals.exporter_warnings));
  RecordProperty("skinned_shaders", std::to_string(totals.skinned_shaders));
  RecordProperty("skinned_shaders_with_skinned_flag", std::to_string(totals.skinned_with_flag));
  std::cout << "gltf real round trip: nifs=" << totals.nifs - totals.shapeless << " without_geometry=" << totals.shapeless
            << " modes=" << k_modes.size() << " shape_checks=" << totals.shape_checks << " textures=" << totals.textures
            << " bgsm=" << totals.bgsm << " colour_shapes=" << totals.colours << " tangent_shapes=" << totals.tangents
            << " skinned=" << totals.skinned << " bones=" << totals.bones << " exporter_warnings=" << totals.exporter_warnings
            << " max_position=" << totals.position << " max_normal=" << totals.normal << " max_uv=" << totals.uv
            << " max_weight=" << totals.weight << " max_bind=" << totals.bind << " max_colour=" << totals.colour
            << " max_tangent=" << totals.tangent << " degenerate_vertices=" << totals.degenerate_vertices << '\n';
  std::cout << "bgsm_resolved_texture_slots=" << totals.bgsm_resolved_textures
            << " case_only_texture_slots=" << totals.case_only_textures
            << " unresolved_warned_slots=" << totals.unresolved_textures << '\n';
  RecordProperty("case_only_texture_slots", std::to_string(totals.case_only_textures));
  std::cout << "skinned shaders=" << totals.skinned_shaders << " with Skinned flag=" << totals.skinned_with_flag << '\n';
  EXPECT_GT(totals.shape_checks, 0U);
  EXPECT_EQ(totals.skinned_with_flag, totals.skinned_shaders);
  EXPECT_LE(totals.position, 1e-3);
  EXPECT_LE(totals.normal, 0.02);
  EXPECT_LE(totals.uv, 1e-4);
  EXPECT_LE(totals.weight, 2e-3);
  EXPECT_LE(totals.bind, 1e-3);
  EXPECT_LE(totals.colour, 0.005);
  EXPECT_LE(totals.tangent, 0.02);
}
