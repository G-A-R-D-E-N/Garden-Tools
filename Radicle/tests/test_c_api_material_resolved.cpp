#include "fo4/c_api.h"
#include "fo4/c_api_env.h"
#include "fo4/c_api_materials.h"
#include "fo4/c_api_nif.h"
#include "resolved_material_fixture.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using namespace resolved_fixture;

namespace {

constexpr const char* k_box = "Materials\\Props\\Box.bgsm";

struct Handles {
  radicle_nif* nif{nullptr};
  radicle_assets* assets{nullptr};
  radicle_resolved_material* material{nullptr};

  Handles(const std::vector<std::byte>& nif_bytes, const fo4::test::TempDataDir& dir) {
    EXPECT_EQ(radicle_nif_read(reinterpret_cast<const uint8_t*>(nif_bytes.data()), nif_bytes.size(), &nif),
              RADICLE_STATUS_OK)
        << radicle_last_error();
    const std::string root = dir.path().string();
    const char* roots[] = {root.c_str()};
    EXPECT_EQ(radicle_assets_open_roots(roots, 1, nullptr, 0, &assets), RADICLE_STATUS_OK) << radicle_last_error();
  }
  Handles(const Handles&) = delete;
  Handles& operator=(const Handles&) = delete;
  ~Handles() {
    radicle_resolved_material_free(material);
    radicle_assets_destroy(assets);
    radicle_nif_destroy(nif);
  }
};

Scene boxed() {
  Scene scene;
  scene.material = k_box;
  scene.slots = {"textures\\a_d.dds"};
  return scene;
}

}  // namespace

// Detects: an own BGSM that must cross the C boundary with its paths, ten texture slots, alpha mode and blend factors intact.
TEST(CApiMaterialResolved, ResolveMustCarryTheBgsmValuesAcrossTheAbi) {
  fo4::test::TempDataDir dir;
  put(dir, "materials/props/box.bgsm", bgsm());
  Handles h(lighting_bytes(boxed()), dir);

  ASSERT_EQ(radicle_material_resolve(h.nif, 0, h.assets, &h.material), RADICLE_STATUS_OK) << radicle_last_error();

  ASSERT_NE(h.material, nullptr);
  EXPECT_EQ(h.material->source, RADICLE_RESOLVED_MATERIAL_SOURCE_BGSM);
  EXPECT_STREQ(h.material->nif_material_path, "materials/props/box.bgsm");
  EXPECT_STREQ(h.material->material_path, "materials/props/box.bgsm");
  ASSERT_EQ(h.material->texture_count, 10U);
  EXPECT_STREQ(h.material->textures[0], "textures/d.dds");
  EXPECT_STREQ(h.material->textures[1], "textures/nonul");
  EXPECT_STREQ(h.material->textures[9], "");
  EXPECT_EQ(h.material->alpha_mode, RADICLE_RESOLVED_ALPHA_MODE_BLEND);
  EXPECT_EQ(h.material->blend_src, 6);
  EXPECT_EQ(h.material->blend_dst, 7);
  EXPECT_EQ(h.material->two_sided, 1);
  EXPECT_FLOAT_EQ(h.material->uv_scale[0], 1.5F);
  EXPECT_EQ(h.material->swapped, 0);
  EXPECT_EQ(h.material->has_color_remapping_index, 0);
  EXPECT_EQ(h.material->warning_count, 0U);
}

// Detects: a missing material file that must come back as a warning string on the handle, not as a failed status.
TEST(CApiMaterialResolved, MissingMaterialMustBeAWarningNotAFailure) {
  fo4::test::TempDataDir dir;
  Handles h(lighting_bytes(boxed()), dir);

  ASSERT_EQ(radicle_material_resolve(h.nif, 0, h.assets, &h.material), RADICLE_STATUS_OK) << radicle_last_error();

  EXPECT_EQ(h.material->source, RADICLE_RESOLVED_MATERIAL_SOURCE_NIF_ONLY);
  EXPECT_STREQ(h.material->textures[0], "textures/a_d.dds");
  ASSERT_GE(h.material->warning_count, 1U);
  EXPECT_NE(std::string(h.material->warnings[0]).find("material not found"), std::string::npos);
}

// Detects: a swap passed through the ABI that must re-resolve the handle in place and report out_applied, not leave stale pointers.
TEST(CApiMaterialResolved, ApplySwapMustReResolveTheHandleInPlace) {
  fo4::test::TempDataDir dir;
  put(dir, "materials/props/box.bgsm", bgsm());
  auto rust = bgsm();
  ASSERT_TRUE(fo4::material::set_text(rust, "DiffuseTexture", "Textures/rust_d.dds"));
  put(dir, "materials/props/rust.bgsm", rust);
  Handles h(lighting_bytes(boxed()), dir);
  ASSERT_EQ(radicle_material_resolve(h.nif, 0, h.assets, &h.material), RADICLE_STATUS_OK) << radicle_last_error();

  const radicle_material_substitution miss{"Materials\\Props\\Other.bgsm", "Materials\\Props\\Rust.bgsm", 0, 0.0F};
  const radicle_material_swap no_match{&miss, 1, nullptr};
  int32_t applied = -1;
  ASSERT_EQ(radicle_resolved_material_apply_swap(h.material, h.nif, 0, h.assets, &no_match, &applied),
            RADICLE_STATUS_OK);
  EXPECT_EQ(applied, 0);
  EXPECT_EQ(h.material->swapped, 0);
  EXPECT_STREQ(h.material->textures[0], "textures/d.dds");

  const radicle_material_substitution hit{k_box, "Materials\\Props\\Rust.bgsm", 1, 0.5F};
  const radicle_material_swap match{&hit, 1, nullptr};
  ASSERT_EQ(radicle_resolved_material_apply_swap(h.material, h.nif, 0, h.assets, &match, &applied), RADICLE_STATUS_OK)
      << radicle_last_error();

  EXPECT_EQ(applied, 1);
  EXPECT_EQ(h.material->swapped, 1);
  EXPECT_STREQ(h.material->material_path, "materials/props/rust.bgsm");
  EXPECT_STREQ(h.material->textures[0], "textures/rust_d.dds");
  EXPECT_EQ(h.material->has_color_remapping_index, 1);
  EXPECT_FLOAT_EQ(h.material->color_remapping_index, 0.5F);
}

// Detects: null and out-of-range arguments that must answer INVALID_ARGUMENT and leave the output null, not crash.
TEST(CApiMaterialResolved, BadArgumentsMustAnswerInvalidArgument) {
  fo4::test::TempDataDir dir;
  Handles h(lighting_bytes(boxed()), dir);
  radicle_resolved_material* out = nullptr;

  EXPECT_EQ(radicle_material_resolve(nullptr, 0, h.assets, &out), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_material_resolve(h.nif, 0, nullptr, &out), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_material_resolve(h.nif, 0, h.assets, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_material_resolve(h.nif, 9, h.assets, &out), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(out, nullptr);

  int32_t applied = 0;
  const radicle_material_swap empty{nullptr, 0, nullptr};
  EXPECT_EQ(radicle_resolved_material_apply_swap(nullptr, h.nif, 0, h.assets, &empty, &applied),
            RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_resolved_material_free(nullptr);
}
