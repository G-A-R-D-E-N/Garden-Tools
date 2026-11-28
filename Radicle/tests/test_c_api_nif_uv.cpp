#include "fo4/c_api_nif_mesh.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

struct Floats {
  float* data{nullptr};
  size_t count{0};
  Floats() = default;
  Floats(const Floats&) = delete;
  Floats& operator=(const Floats&) = delete;
  ~Floats() { radicle_free_floats(data); }
  std::vector<float> values() const { return std::vector<float>(data, data + count); }
};

const std::vector<float> k_quad_verts{0, 0, 0, 2, 0, 0, 2, 2, 0, 0, 2, 0};
const std::vector<uint32_t> k_quad_tris{0, 1, 2, 0, 2, 3};
const std::vector<uint32_t> k_quad_ids{0, 0, 0, 0};
const std::vector<uint32_t> k_all{0, 1, 2, 3};

}  // namespace

TEST(CApiNifUv, IslandsNumbersTwoDisjointTrianglesAsTwoIslands) {
  const std::vector<uint32_t> tris{0, 1, 2, 3, 4, 5};
  uint32_t* ids = nullptr;
  size_t count = 0;
  ASSERT_EQ(radicle_uv_islands(6, tris.data(), tris.size(), &ids, &count), RADICLE_STATUS_OK) << radicle_last_error();
  ASSERT_EQ(count, 6u);
  EXPECT_EQ(std::vector<uint32_t>(ids, ids + count), (std::vector<uint32_t>{0, 0, 0, 1, 1, 1}));
  radicle_free_node_refs(ids);
}

TEST(CApiNifUv, ResolvePivotAnswersTheModeAndRefusesAnUnknownOne) {
  const std::vector<float> uvs{0.25f, 0.5f, 0.75f, 0.5f, 0.5f, 0.25f};
  const std::vector<uint32_t> sel{0, 2};
  float u = -1, v = -1;
  ASSERT_EQ(radicle_uv_resolve_pivot(uvs.data(), 3, sel.data(), sel.size(), 0, &u, &v), RADICLE_STATUS_OK);
  EXPECT_FLOAT_EQ(u, 0.375f);
  EXPECT_FLOAT_EQ(v, 0.375f);
  ASSERT_EQ(radicle_uv_resolve_pivot(uvs.data(), 3, sel.data(), sel.size(), 2, &u, &v), RADICLE_STATUS_OK);
  EXPECT_FLOAT_EQ(u, 0.5f);
  EXPECT_EQ(radicle_uv_resolve_pivot(uvs.data(), 3, sel.data(), sel.size(), 3, &u, &v), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiNifUv, TransformScalesAboutThePivotAndLeavesUnselectedVerticesAlone) {
  const std::vector<float> uvs{0.25f, 0.5f, 0.75f, 0.5f, 0.5f, 0.25f};
  const std::vector<uint32_t> sel{0, 1};
  Floats out;
  ASSERT_EQ(radicle_uv_transform(uvs.data(), 3, sel.data(), sel.size(), 0, 0, 2, 1, 0, 0.5, 0.5, &out.data, &out.count), RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(out.values(), (std::vector<float>{0.0f, 0.5f, 1.0f, 0.5f, 0.5f, 0.25f}));
}

TEST(CApiNifUv, PackReportsFitsAndReturnsTheInputUnchangedWhenItDoesNot) {
  const std::vector<float> uvs{0, 0, 0.5f, 0, 0.5f, 0.5f, 0, 0.5f, 0.6f, 0.6f, 0.9f, 0.6f, 0.9f, 0.9f, 0.6f, 0.9f};
  const std::vector<uint32_t> ids{0, 0, 0, 0, 1, 1, 1, 1};
  const std::vector<uint32_t> sel{0, 4};
  Floats fit;
  uint8_t fits = 0;
  ASSERT_EQ(radicle_uv_pack(uvs.data(), 8, ids.data(), sel.data(), sel.size(), 0.1, &fit.data, &fit.count, &fits), RADICLE_STATUS_OK);
  EXPECT_EQ(fits, 1);
  ASSERT_EQ(fit.count, 16u);
  EXPECT_NEAR(fit.data[0], 0.1f, 5e-6f);
  EXPECT_NEAR(fit.data[2], 0.5375f, 5e-6f);
  std::vector<float> nine;
  std::vector<uint32_t> nine_ids, nine_sel;
  for (uint32_t k = 0; k < 9; ++k) {
    const float a = 0.1f * static_cast<float>(k), b = a + 0.05f;
    for (const float x : {a, a, b, a, b, b, a, b}) nine.push_back(x);
    for (uint32_t c = 0; c < 4; ++c) {
      nine_ids.push_back(k);
      nine_sel.push_back(4 * k + c);
    }
  }
  Floats tight;
  ASSERT_EQ(radicle_uv_pack(nine.data(), 36, nine_ids.data(), nine_sel.data(), nine_sel.size(), 0.25, &tight.data, &tight.count, &fits),
            RADICLE_STATUS_OK);
  EXPECT_EQ(fits, 0);
  EXPECT_EQ(tight.values(), nine);
}

TEST(CApiNifUv, AverageScaleScalesTwoIslandsToTheAreaWeightedMean) {
  const std::vector<float> verts{0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 2, 2, 0, 2, 2, 2, 2, 0, 2, 2};
  const std::vector<float> uvs{0, 0, 0.5f, 0, 0.5f, 0.5f, 0, 0.5f, 0.5f, 0.5f, 1, 0.5f, 1, 1, 0.5f, 1};
  const std::vector<uint32_t> tris{0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
  const std::vector<uint32_t> ids{0, 0, 0, 0, 1, 1, 1, 1};
  const std::vector<uint32_t> sel{0, 4};
  Floats out;
  ASSERT_EQ(radicle_uv_average_scale(uvs.data(), verts.data(), 8, tris.data(), tris.size(), ids.data(), sel.data(), sel.size(), &out.data, &out.count),
            RADICLE_STATUS_OK);
  ASSERT_EQ(out.count, 16u);
  EXPECT_NEAR(out.data[2], 0.4f, 5e-6f);
  EXPECT_NEAR(out.data[8], 0.45f, 5e-6f);
}

TEST(CApiNifUv, TriStretchGivesOneValuePerWholeTriangle) {
  const std::vector<float> uvs{0, 0, 2, 0, 2, 2, 0, 2};
  const std::vector<float> verts{0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0};
  Floats out;
  ASSERT_EQ(radicle_uv_tri_stretch(uvs.data(), verts.data(), 4, k_quad_tris.data(), k_quad_tris.size(), &out.data, &out.count), RADICLE_STATUS_OK);
  EXPECT_EQ(out.values(), (std::vector<float>{0.0f, 0.0f}));
}

TEST(CApiNifUv, SmartProjectFillsTheUnitTileAndReportsTheCounts) {
  const std::vector<float> uvs(8, 0.0f);
  Floats out;
  uint32_t projected = 0, skipped = 9;
  uint8_t fits = 0;
  ASSERT_EQ(radicle_uv_smart_project(uvs.data(), k_quad_verts.data(), 4, k_quad_tris.data(), k_quad_tris.size(), k_quad_ids.data(), k_all.data(), k_all.size(),
                                     0.01, &out.data, &out.count, &projected, &skipped, &fits),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(projected, 4u);
  EXPECT_EQ(skipped, 0u);
  EXPECT_EQ(fits, 1);
  ASSERT_EQ(out.count, 8u);
  float lo = 2, hi = -1;
  for (size_t i = 0; i < out.count; ++i) {
    lo = out.data[i] < lo ? out.data[i] : lo;
    hi = out.data[i] > hi ? out.data[i] : hi;
  }
  EXPECT_NEAR(lo, 0.01f, 5e-6f);
  EXPECT_NEAR(hi, 0.99f, 5e-6f);
}

TEST(CApiNifUv, LscmUnwrapKeepsThePinsExactAndCountsTheIslands) {
  const std::vector<float> uvs(8, 0.0f);
  const std::vector<uint32_t> pin_vertices{0, 1};
  const std::vector<float> pin_uvs{0, 0, 2, 0};
  Floats out;
  uint32_t unwrapped = 0, skipped = 9;
  ASSERT_EQ(radicle_uv_lscm_unwrap(uvs.data(), k_quad_verts.data(), 4, k_quad_tris.data(), k_quad_tris.size(), k_quad_ids.data(), k_all.data(), k_all.size(),
                                   pin_vertices.data(), pin_uvs.data(), pin_vertices.size(), &out.data, &out.count, &unwrapped, &skipped),
            RADICLE_STATUS_OK)
      << radicle_last_error();
  EXPECT_EQ(unwrapped, 1u);
  EXPECT_EQ(skipped, 0u);
  ASSERT_EQ(out.count, 8u);
  EXPECT_NEAR(out.data[2], 2.0f, 5e-6f);
  EXPECT_NEAR(out.data[3], 0.0f, 5e-6f);
  EXPECT_NEAR(out.data[4], 2.0f, 5e-5f);
  EXPECT_NEAR(out.data[5], 2.0f, 5e-5f);
}

TEST(CApiNifUv, MissingArraysAndOutputsAreInvalidArgumentsThatZeroTheOutputs) {
  Floats out;
  out.count = 7;
  uint8_t fits = 1;
  EXPECT_EQ(radicle_uv_pack(nullptr, 4, k_quad_ids.data(), k_all.data(), k_all.size(), 0.1, &out.data, &out.count, &fits), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(out.count, 0u);
  EXPECT_EQ(fits, 0);
  const std::vector<float> uvs(8, 0.0f);
  EXPECT_EQ(radicle_uv_pack(uvs.data(), 4, k_quad_ids.data(), k_all.data(), k_all.size(), 0.1, &out.data, &out.count, nullptr), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_uv_tri_stretch(uvs.data(), k_quad_verts.data(), 4, nullptr, 6, &out.data, &out.count), RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(CApiNifUv, AnEmptyMeshAnswersAnEmptyResult) {
  Floats out;
  uint8_t fits = 0;
  ASSERT_EQ(radicle_uv_pack(nullptr, 0, nullptr, nullptr, 0, 0.1, &out.data, &out.count, &fits), RADICLE_STATUS_OK);
  EXPECT_EQ(out.count, 0u);
  EXPECT_EQ(out.data, nullptr);
  EXPECT_EQ(fits, 1);
}
