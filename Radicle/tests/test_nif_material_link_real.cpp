#include "fo4/archive/assets.hpp"
#include "fo4/error.hpp"
#include "fo4/material/material_file.hpp"
#include "fo4/nif/material_link.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_write.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using MaterialCache = std::unordered_map<std::string, std::optional<fo4::material::MaterialFile>>;

struct LinkCounts {
  std::size_t nifs{0};
  std::size_t nif_failures{0};
  std::size_t shapes{0};
  std::size_t shapes_with_material{0};
  std::size_t shapes_without_material_file{0};
  std::size_t shapes_compared{0};
  std::size_t shapes_with_mismatch{0};
  std::size_t texture_mismatches{0};
  std::size_t refused_shapes{0};
  std::size_t shapes_needing_fill{0};
  std::size_t shapes_unfillable{0};
  std::size_t fill_edits{0};
  std::size_t fill_errors{0};
  std::size_t fill_failures{0};
  std::map<std::string, std::size_t> field_mismatches;
  std::map<std::string, std::size_t> texture_nonempty;
  std::map<std::string, std::size_t> texture_disagree;
};

constexpr std::string_view k_lighting_texture_fields[] = {"DiffuseTexture", "NormalTexture", "GlowTexture",
                                                          "GreyscaleTexture", "EnvmapTexture", "SmoothSpecTexture"};

std::string lower_extension(const std::string& path) {
  std::string extension = std::filesystem::path(path).extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return extension;
}

bool is_texture_field(const std::string& field) { return field.ends_with("Texture"); }

struct AgreementFloor {
  std::string_view field;
  std::size_t percent;
};

// Constraint: the floors sit about two points under the full-corpus agreement measured for each slot.
constexpr AgreementFloor k_agreement_floors[] = {
    {"DiffuseTexture", 97}, {"NormalTexture", 97}, {"SmoothSpecTexture", 97},
    {"GlowTexture", 95},    {"GreyscaleTexture", 93}, {"EnvmapTexture", 92},
};

// Constraint: the sample is every 37th NIF in archive order, so one data set always gives the same counts.
constexpr std::size_t k_nif_sample_stride = 37;

const std::optional<fo4::material::MaterialFile>& material_for(const fo4::archive::AssetResolver& assets,
                                                               MaterialCache& cache, const std::string& stored) {
  const auto path = fo4::nif::data_relative_material_path(stored);
  std::string key = path;
  try {
    key = fo4::archive::normalize_asset_path(path);
  } catch (const fo4::archive::AssetPathError&) {
  }
  const auto found = cache.find(key);
  if (found != cache.end()) return found->second;
  std::optional<fo4::material::MaterialFile> file;
  try {
    if (const auto location = assets.locate(path)) file = fo4::material::read_material_file(assets.read(*location));
  } catch (const std::exception&) {
    file.reset();
  }
  return cache.emplace(std::move(key), std::move(file)).first->second;
}

}  // namespace

// regression: a vanilla shape whose texture fill leaves a mismatch, whose compare refuses a vanilla shader, or whose slot layout disagrees with vanilla fails here.
TEST(NifMaterialLinkReal, VanillaShapesCompareWithTheirMaterialsAndFillRoundTrips) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  record_game_build(detect_game_build(dir));
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";

  const auto assets = fo4::archive::AssetResolver::open({}, archives);
  MaterialCache materials;
  LinkCounts counts;
  std::size_t nif_index = 0;
  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size(); ++i) {
      if (lower_extension(reader.entries()[i].path) != ".nif") continue;
      if (nif_index++ % k_nif_sample_stride != 0) continue;
      ++counts.nifs;
      const auto bytes = reader.extract(i);
      try {
        const auto nif = fo4::nif::read_nif(bytes);
        for (const auto& shape : fo4::nif::read_shapes(nif)) {
          ++counts.shapes;
          if (shape.material.empty()) continue;
          ++counts.shapes_with_material;
          const auto& file = material_for(assets, materials, shape.material);
          if (!file) {
            ++counts.shapes_without_material_file;
            continue;
          }
          try {
            const auto mismatches = fo4::nif::compare_shape_material(nif, shape.block, *file);
            ++counts.shapes_compared;
            if (!mismatches.empty()) ++counts.shapes_with_mismatch;
            for (const auto& mismatch : mismatches) {
              ++counts.field_mismatches[mismatch.field];
              if (is_texture_field(mismatch.field)) ++counts.texture_mismatches;
            }
            if (file->format == fo4::material::MaterialFormat::bgsm && !shape.textures.empty()) {
              for (const auto field : k_lighting_texture_fields) {
                const auto value = fo4::material::get_field<std::string>(*file, field);
                if (value && !fo4::material::text(*value).empty()) ++counts.texture_nonempty[std::string(field)];
              }
              for (const auto& mismatch : mismatches) {
                if (!fo4::material::text(mismatch.material_value).empty()) ++counts.texture_disagree[mismatch.field];
              }
            }
            std::vector<fo4::nif::Edit> edits;
            try {
              edits = fo4::nif::shape_material_texture_edits(nif, shape.block, *file);
            } catch (const fo4::parse_error&) {
              ++counts.shapes_unfillable;
              continue;
            }
            if (edits.empty()) continue;
            ++counts.shapes_needing_fill;
            counts.fill_edits += edits.size();
            try {
              const auto filled = fo4::nif::read_nif(fo4::nif::apply_edits(nif, edits));
              for (const auto& mismatch : fo4::nif::compare_shape_material(filled, shape.block, *file)) {
                if (is_texture_field(mismatch.field)) ++counts.fill_failures;
              }
            } catch (const fo4::parse_error&) {
              ++counts.fill_errors;
            }
          } catch (const fo4::parse_error&) {
            ++counts.refused_shapes;
          }
        }
      } catch (const fo4::parse_error&) {
        ++counts.nif_failures;
      }
    }
  }

  std::size_t missing_materials = 0;
  for (const auto& [key, file] : materials) {
    if (!file) ++missing_materials;
  }
  std::cout << "nifs " << counts.nifs << ", nif failures " << counts.nif_failures << ", shapes " << counts.shapes
            << ", shapes with material " << counts.shapes_with_material << ", material files unresolved "
            << missing_materials << " (shapes " << counts.shapes_without_material_file << ")\n";
  std::cout << "compared " << counts.shapes_compared << ", with mismatch " << counts.shapes_with_mismatch
            << ", texture mismatches " << counts.texture_mismatches << ", refused " << counts.refused_shapes << "\n";
  std::cout << "fill: shapes " << counts.shapes_needing_fill << ", edits " << counts.fill_edits << ", unfillable "
            << counts.shapes_unfillable << ", errors " << counts.fill_errors << ", texture mismatches after fill "
            << counts.fill_failures << '\n';
  for (const auto& [field, count] : counts.field_mismatches) std::cout << "mismatch " << field << ": " << count << '\n';
  for (const auto& floor : k_agreement_floors) {
    const auto total = counts.texture_nonempty[std::string(floor.field)];
    const auto agree = total - counts.texture_disagree[std::string(floor.field)];
    std::cout << "agreement " << floor.field << ": " << agree << "/" << total << '\n';
    RecordProperty("agreement_" + std::string(floor.field), std::to_string(agree) + "/" + std::to_string(total));
    if (total > 0) {
      EXPECT_GE(agree * 100, total * floor.percent) << floor.field << " agrees with the NIF slot on too few vanilla shapes";
    }
  }

  RecordProperty("shapes_with_material", std::to_string(counts.shapes_with_material));
  RecordProperty("shapes_compared", std::to_string(counts.shapes_compared));
  RecordProperty("shapes_with_mismatch", std::to_string(counts.shapes_with_mismatch));
  RecordProperty("texture_mismatches", std::to_string(counts.texture_mismatches));
  RecordProperty("shapes_without_material_file", std::to_string(counts.shapes_without_material_file));
  RecordProperty("material_files_unresolved", std::to_string(missing_materials));
  RecordProperty("shapes_needing_fill", std::to_string(counts.shapes_needing_fill));
  RecordProperty("shapes_unfillable", std::to_string(counts.shapes_unfillable));
  RecordProperty("texture_mismatches_after_fill", std::to_string(counts.fill_failures));
  for (const auto& [field, count] : counts.field_mismatches) {
    RecordProperty("mismatch_" + field, std::to_string(count));
  }

  EXPECT_EQ(counts.nif_failures, 0U);
  EXPECT_EQ(counts.refused_shapes, 0U);
  EXPECT_EQ(counts.fill_errors, 0U);
  EXPECT_EQ(counts.fill_failures, 0U);
  EXPECT_GT(counts.shapes_compared, 0U);
}
