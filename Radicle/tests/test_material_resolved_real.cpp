#include "fo4/archive/assets.hpp"
#include "fo4/error.hpp"
#include "fo4/material/material_file.hpp"
#include "fo4/material/resolved.hpp"
#include "fo4/nif/nif.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"
#include "real_data.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <string_view>
#include <string>
#include <vector>

namespace {

using fo4::material::AlphaMode;
using fo4::material::ResolvedMaterial;

constexpr std::size_t k_entry_stride = 7;
constexpr std::size_t k_max_models = 3000;

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}

bool is_nif(const std::string& path) { return std::filesystem::path(lower(path)).extension() == ".nif"; }

using Visit = std::function<bool(const std::string&, const fo4::nif::NifFile&, const fo4::nif::Shape&, const ResolvedMaterial&)>;

std::size_t sample_shapes(const fo4::archive::AssetResolver& assets, const Visit& visit) {
  std::size_t models = 0;
  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size() && models < k_max_models; i += k_entry_stride) {
      const auto& path = reader.entries()[i].path;
      if (!is_nif(path)) continue;
      try {
        const auto file = fo4::nif::read_nif(reader.extract(i));
        ++models;
        for (const auto& shape : fo4::nif::read_shapes(file)) {
          if (!visit(path, file, shape, fo4::material::resolve_material(file, shape, assets))) return models;
        }
      } catch (const fo4::parse_error&) {
      }
    }
  }
  return models;
}

std::optional<fo4::archive::AssetResolver> open_vanilla(const char* dir) {
  record_game_build(detect_game_build(dir));
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) return std::nullopt;
  return fo4::archive::AssetResolver::open({}, archives);
}

}  // namespace

TEST(MaterialResolvedReal, VanillaShapesMustResolveWithSoundTexturesAndAlpha) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives");
  const auto assets = open_vanilla(dir);
  if (!assets) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";

  std::size_t shapes = 0;
  std::size_t with_material = 0;
  std::size_t bad = 0;
  std::string first_bad;
  sample_shapes(*assets, [&](const std::string& path, const fo4::nif::NifFile&, const fo4::nif::Shape&, const ResolvedMaterial& rm) {
    ++shapes;
    if (rm.source != ResolvedMaterial::Source::nif_only) ++with_material;
    bool sound = rm.alpha >= 0.0F && rm.alpha <= 1.0F;
    for (const auto& texture : rm.textures) sound = sound && (texture.empty() || texture.starts_with("textures/"));
    if (!sound && ++bad == 1) first_bad = path;
    return true;
  });

  std::cout << "resolved shapes=" << shapes << " with_material=" << with_material << " bad=" << bad << '\n';
  EXPECT_GT(shapes, 0U);
  EXPECT_GT(with_material, 0U);
  EXPECT_EQ(bad, 0U) << first_bad;
}

TEST(MaterialResolvedReal, VanillaAlphaTestedBgsmMustCarryTheMaterialReference) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives");
  const auto assets = open_vanilla(dir);
  if (!assets) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";

  std::size_t checked = 0;
  std::string mismatch;
  sample_shapes(*assets, [&](const std::string& path, const fo4::nif::NifFile&, const fo4::nif::Shape&, const ResolvedMaterial& rm) {
    if (rm.source != ResolvedMaterial::Source::bgsm || rm.alpha_mode != AlphaMode::test || rm.material_path != rm.nif_material_path) return true;
    const auto location = assets->locate(rm.material_path);
    if (!location) return true;
    const auto file = fo4::material::read_material_file(assets->read(*location));
    if (fo4::material::get_field<std::uint8_t>(file, "AlphaBlendMode0").value_or(0) != 0) return true;
    ++checked;
    if (fo4::material::get_field<std::uint8_t>(file, "AlphaTestRef") != rm.alpha_threshold && mismatch.empty()) mismatch = path;
    return checked < 50;
  });

  if (checked == 0) GTEST_SKIP() << "no alpha-tested BGSM without blend in the sampled vanilla NIFs";
  EXPECT_TRUE(mismatch.empty()) << mismatch;
}

TEST(MaterialResolvedReal, VanillaBgemEffectMustResolveFromTheEffectShader) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives");
  const auto assets = open_vanilla(dir);
  if (!assets) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";

  std::size_t effects = 0;
  std::size_t with_texture = 0;
  sample_shapes(*assets, [&](const std::string&, const fo4::nif::NifFile&, const fo4::nif::Shape&, const ResolvedMaterial& rm) {
    if (rm.source != ResolvedMaterial::Source::bgem) return true;
    ++effects;
    if (!rm.textures[fo4::material::k_slot_diffuse].empty()) ++with_texture;
    return effects < 50;
  });

  if (effects == 0) GTEST_SKIP() << "no BGEM effect shapes in the sampled vanilla NIFs";
  EXPECT_GT(with_texture, 0U);
}

TEST(MaterialResolvedReal, VanillaSwapMustReResolveFromAnotherVanillaBgsm) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives");
  const auto assets = open_vanilla(dir);
  if (!assets) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";

  const auto listing = assets->list("materials/", ".bgsm", false);
  if (listing.empty()) GTEST_SKIP() << "no BGSM files under materials/ in the archives";

  std::size_t applied = 0;
  std::string failure;
  sample_shapes(*assets, [&](const std::string& path, const fo4::nif::NifFile& file, const fo4::nif::Shape& shape, ResolvedMaterial rm) {
    if (rm.source != ResolvedMaterial::Source::bgsm || rm.material_path != rm.nif_material_path) return true;
    const auto replacement = std::ranges::find_if(listing, [&](const auto& entry) { return entry.path != rm.material_path; });
    if (replacement == listing.end()) return true;
    const std::string original = rm.nif_material_path;
    const fo4::env::MaterialSubstitution swap{original, std::string_view{replacement->path}, 0.5F};
    const bool ok = fo4::material::apply_material_swap(rm, file, shape, *assets, swap);
    if ((!ok || !rm.swapped || rm.material_path != replacement->path) && failure.empty()) failure = path;
    ++applied;
    return applied < 25;
  });

  if (applied == 0) GTEST_SKIP() << "no vanilla shape with a BGSM in the sampled NIFs";
  EXPECT_TRUE(failure.empty()) << failure;
}
