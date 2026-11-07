#include "fo4/archive/assets.hpp"
#include "fo4/error.hpp"
#include "fo4/nif/describe.hpp"
#include "fo4/nif/lint.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_fix.hpp"
#include "nif_build.hpp"
#include "real_data_build.hpp"
#include "real_data_plugins.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <vector>

using fo4::nif::Transform;
using namespace nif_build;

namespace {

Block texture_paths_block(const std::vector<std::string>& paths) {
  Writer w;
  w.u32(static_cast<std::uint32_t>(paths.size()));
  for (const auto& path : paths) w.sized(path);
  return Block{"BSShaderTextureSet", w.bytes()};
}

// Root, a shape named Body with a lighting shader, and its texture set; the shape carries the given sphere.
std::vector<std::byte> body_nif(const std::array<float, 4>& sphere, const std::vector<std::string>& textures,
                                const std::vector<std::uint16_t>& tris = {0, 1, 2},
                                const std::vector<std::byte>& vertices = triangle_vertices(),
                                const std::vector<std::string>& extra_strings = {}) {
  LightingFields lighting;
  lighting.name = 2;
  lighting.texture_set = 3;
  const std::vector<Block> blocks{
      named_node_block("NiNode", 0, Transform{}, {1}),
      shape_with_refs("BSTriShape", 1, Transform{}, k_full, 16, -1, 2, -1, vertices, tris, sphere),
      full_lighting_shader_block(lighting),
      texture_paths_block(textures),
  };
  std::vector<std::string> strings{"Root", "Body", "shaderName"};
  strings.insert(strings.end(), extra_strings.begin(), extra_strings.end());
  return build_nif(strings, blocks, {0});
}

Block sub_index_block(std::uint32_t segments, std::uint32_t total, std::uint32_t subsegments, const std::string& ssf,
                      bool shared_data = true) {
  Block shape = shape_with_refs("BSSubIndexTriShape", 1, Transform{}, k_full, 16, -1, -1, -1, triangle_vertices(),
                                {0, 1, 2}, k_triangle_sphere);
  Writer w;
  w.u32(1);
  w.u32(segments);
  w.u32(total);
  for (std::uint32_t segment = 0; segment < segments; ++segment) {
    w.u32(0);
    w.u32(1);
    w.u32(0xFFFFFFFFU);
    w.u32(subsegments);
    for (std::uint32_t sub = 0; sub < subsegments; ++sub) {
      for (int i = 0; i < 4; ++i) w.u32(0);
    }
  }
  if (segments < total && shared_data) {
    w.u32(segments);
    w.u32(total);
    for (std::uint32_t segment = 0; segment < segments; ++segment) w.u32(0);
    for (std::uint32_t entry = 0; entry < total; ++entry) {
      w.u32(entry);
      w.u32(0);
      w.u32(0);
    }
    w.u16(static_cast<std::uint16_t>(ssf.size()));
    w.text(ssf);
  }
  shape.data.insert(shape.data.end(), w.bytes().begin(), w.bytes().end());
  return shape;
}

std::vector<std::byte> sub_index_nif(std::uint32_t segments, std::uint32_t total, std::uint32_t subsegments,
                                     const std::string& ssf, bool shared_data = true) {
  return build_nif({"Root", "Body"},
                   {named_node_block("NiNode", 0, Transform{}, {1}),
                    sub_index_block(segments, total, subsegments, ssf, shared_data)},
                   {0});
}

// A skinned shape with a zero sphere, as vanilla writes them, bound to one bone block.
std::vector<std::byte> skinned_zero_sphere_nif() {
  Writer vertices;
  for (const float x : {0.0f, 1.0f, 0.0f}) {
    vertices.f32(x);
    vertices.f32(0.0f);
    vertices.f32(0.0f);
    vertices.u32(0);
    for (const int weight : {0x3C00, 0, 0, 0}) vertices.u16(static_cast<std::uint16_t>(weight));
    for (int i = 0; i < 4; ++i) vertices.u8(0);
  }
  const std::vector<Block> blocks{
      named_node_block("NiNode", 0, Transform{}, {1, 3}),
      shape_with_refs("BSTriShape", 1, Transform{}, k_full | k_has_skin, 28, 2, -1, -1, vertices.bytes(), {0, 1, 2}),
      skin_instance_block(-1, {3}),
      named_node_block("NiNode", -1, Transform{}, {}),
  };
  return build_nif({"Root", "Body"}, blocks, {0});
}

std::vector<std::byte> positions_bytes(const std::vector<std::array<float, 3>>& positions) {
  Writer w;
  for (const auto& position : positions) {
    for (const float value : position) w.f32(value);
    w.u32(0);
  }
  return w.bytes();
}

// Constraint: the sphere sits 72 bytes into a shape block whose extra-data count is zero.
constexpr std::size_t k_sphere_offset = 72;

std::array<float, 4> bounding_sphere_of(const fo4::nif::NifFile& nif, std::uint32_t block) {
  std::array<float, 4> sphere{};
  std::memcpy(sphere.data(), nif.bytes().data() + nif.blocks()[block].offset + k_sphere_offset, sizeof sphere);
  return sphere;
}

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}

std::string sibling_path(const std::string& path, std::string_view leaf) {
  return path.substr(0, path.find_last_of("\\/") + 1) + std::string(leaf);
}

}  // namespace

TEST(NifLint, AbsoluteTexturePathWithATexturesFolderIsMadeRelative) {
  // regression: a drive-letter path copied from the author's machine survived the fix, so the mesh still failed verify
  const auto input = body_nif(k_triangle_sphere, {"C:\\Art\\Textures\\a_d.dds", "Textures\\a_n.dds"});
  const auto report = fo4::nif::verify_nif(fo4::nif::read_nif(input));
  ASSERT_EQ(report.checks.at(13).name, "absolute texture paths can be made relative");
  EXPECT_EQ(report.checks.at(13).failure,
            "shape 'Body' (block 1) texture 'C:\\Art\\Textures\\a_d.dds' can be made relative to 'Textures\\a_d.dds'");

  const auto fixed = fo4::nif::fix_nif(fo4::nif::read_nif(input));
  const auto shapes = fo4::nif::read_shapes(fo4::nif::read_nif(fixed));
  ASSERT_EQ(shapes.size(), 1U);
  ASSERT_EQ(shapes[0].textures.size(), 2U);
  EXPECT_EQ(shapes[0].textures[0], "Textures\\a_d.dds");
  EXPECT_EQ(shapes[0].textures[1], "Textures\\a_n.dds");
  EXPECT_EQ(fo4::nif::verify_nif(fo4::nif::read_nif(fixed)).checks.at(9).failure, "");
}

TEST(NifLint, AbsolutePathWithoutATexturesFolderIsLeftForTheRelativeCheck) {
  // regression: a path with no Textures folder was rewritten to an invented folder instead of being reported
  const auto input = body_nif(k_triangle_sphere, {"C:\\Art\\a_d.dds"});
  const auto report = fo4::nif::verify_nif(fo4::nif::read_nif(input));
  EXPECT_EQ(report.checks.at(13).failure, "");
  EXPECT_NE(report.checks.at(9).failure, "");

  const auto fixed = fo4::nif::fix_nif(fo4::nif::read_nif(input));
  const auto shapes = fo4::nif::read_shapes(fo4::nif::read_nif(fixed));
  ASSERT_EQ(shapes.size(), 1U);
  ASSERT_EQ(shapes[0].textures.size(), 1U);
  EXPECT_EQ(shapes[0].textures[0], "C:\\Art\\a_d.dds");
}

TEST(NifLint, ZeroRadiusSphereIsRefitAroundTheVertices) {
  // regression: a rigid shape with no bounding sphere culls wrongly when its origin is off screen, so the fix must refit it
  const auto input = body_nif({}, {"Textures\\a_d.dds"});
  EXPECT_EQ(fo4::nif::verify_nif(fo4::nif::read_nif(input)).checks.at(14).failure,
            "shape 'Body' (block 1) has no bounding sphere");

  const auto fixed = fo4::nif::fix_nif(fo4::nif::read_nif(input));
  EXPECT_EQ(fo4::nif::verify_nif(fo4::nif::read_nif(fixed)).checks.at(14).failure, "");
  const auto before = fo4::nif::read_shapes(fo4::nif::read_nif(input));
  const auto after = fo4::nif::read_shapes(fo4::nif::read_nif(fixed));
  ASSERT_EQ(after.size(), 1U);
  EXPECT_EQ(after[0].verts, before[0].verts);
}

TEST(NifLint, ApproximateSphereOnARigidShapeIsNotReported) {
  // regression: vanilla spheres are approximate, some miss their vertices by far, so a set sphere must pass
  const auto input = body_nif({0.1f, 0.1f, 0.0f, 0.1f}, {"Textures\\a_d.dds"});
  EXPECT_EQ(fo4::nif::verify_nif(fo4::nif::read_nif(input)).checks.at(14).failure, "");
}

TEST(NifLint, ZeroSphereOnASkinnedShapeIsNotReported) {
  // regression: vanilla skinned shapes carry a zero sphere, so judging them would fail every character mesh
  const auto nif = fo4::nif::read_nif(skinned_zero_sphere_nif());
  EXPECT_EQ(fo4::nif::verify_nif(nif).checks.at(14).failure, "");
  EXPECT_EQ(fo4::nif::read_shapes(nif).at(0).skin_ref, 2);
}

// Detects: an Inf position pulling the refit sphere to infinity, which the fix then writes into the file
TEST(NifLint, NonFinitePositionIsLeftOutOfTheSphereRefit) {
  constexpr float quiet_nan = std::numeric_limits<float>::quiet_NaN();
  constexpr float inf = std::numeric_limits<float>::infinity();
  const auto input = body_nif({}, {"Textures\\a_d.dds"}, {0, 1, 2},
                              positions_bytes({{0.0f, 0.0f, 0.0f},
                                               {1.0f, 0.0f, 0.0f},
                                               {0.0f, 1.0f, 0.0f},
                                               {quiet_nan, 0.0f, 0.0f},
                                               {0.0f, inf, 0.0f}}));
  const auto fixed = fo4::nif::read_nif(fo4::nif::fix_nif(fo4::nif::read_nif(input)));
  const auto sphere = bounding_sphere_of(fixed, 1);
  for (std::size_t k = 0; k < sphere.size(); ++k) EXPECT_FLOAT_EQ(sphere[k], k_triangle_sphere[k]) << "component " << k;
  EXPECT_EQ(fo4::nif::verify_nif(fixed).checks.at(14).failure, "");
}

// Detects: a shape with no finite position reported as missing its sphere, because a NaN never equals the zero sphere
TEST(NifLint, AllNonFinitePositionsAreNotJudgedForASphere) {
  constexpr float quiet_nan = std::numeric_limits<float>::quiet_NaN();
  const auto input = body_nif({}, {"Textures\\a_d.dds"}, {0, 1, 2},
                              positions_bytes({{quiet_nan, 0.0f, 0.0f},
                                               {0.0f, quiet_nan, 0.0f},
                                               {0.0f, 0.0f, quiet_nan}}));
  EXPECT_EQ(fo4::nif::verify_nif(fo4::nif::read_nif(input)).checks.at(14).failure, "");
  const auto fixed = fo4::nif::fix_nif(fo4::nif::read_nif(input));
  EXPECT_EQ(fo4::nif::verify_nif(fo4::nif::read_nif(fixed)).checks.at(14).failure, "");
}

TEST(NifLint, RepairsOfPathAndSphereAreIdempotent) {
  // regression: a second fix on a repaired file must change no bytes, or every re-save churns the file
  const auto once = fo4::nif::fix_nif(fo4::nif::read_nif(body_nif({}, {"C:\\Art\\Textures\\a_d.dds"})));
  const auto twice = fo4::nif::fix_nif(fo4::nif::read_nif(once));
  EXPECT_TRUE(twice == once);
}

// Detects: repairing one reported issue must leave other repairable findings intact.
TEST(NifLint, SelectedRepairLeavesOtherFindingsUntouched) {
  const auto original = fo4::nif::read_nif(body_nif({}, {"C:\\Art\\Textures\\a_d.dds"}, {0, 1, 2, 0, 0, 1}));
  const auto before = fo4::nif::verify_nif(original);
  ASSERT_FALSE(before.checks.at(13).failure.empty());
  ASSERT_FALSE(before.checks.at(14).failure.empty());
  ASSERT_FALSE(before.checks.at(15).failure.empty());
  const auto fixed = fo4::nif::read_nif(fo4::nif::fix_nif(original, "absolute texture paths can be made relative"));
  const auto after = fo4::nif::verify_nif(fixed);
  EXPECT_TRUE(after.checks.at(13).failure.empty());
  EXPECT_EQ(after.checks.at(14).failure, before.checks.at(14).failure);
  EXPECT_EQ(after.checks.at(15).failure, before.checks.at(15).failure);
  EXPECT_EQ(fo4::nif::read_shapes(fixed).at(0).tris, fo4::nif::read_shapes(original).at(0).tris);
  for (const auto selected : {14U, 15U}) {
    const auto single = fo4::nif::read_nif(fo4::nif::fix_nif(original, before.checks.at(selected).name));
    const auto report = fo4::nif::verify_nif(single);
    EXPECT_TRUE(report.checks.at(selected).failure.empty());
    EXPECT_EQ(report.checks.at(13).failure, before.checks.at(13).failure);
    const auto other = selected == 14U ? 15U : 14U;
    EXPECT_EQ(report.checks.at(other).failure, before.checks.at(other).failure);
  }
  EXPECT_THROW(fo4::nif::fix_nif(original, "unknown repair"), std::invalid_argument);
}

// Detects: a selected texture repair must accept a shape as the root.
TEST(NifLint, SelectedRepairDoesNotRequireANodeRoot) {
  LightingFields lighting;
  lighting.name = 1;
  lighting.texture_set = 2;
  const auto input = build_nif({"Body", "shaderName"},
      {shape_with_refs("BSTriShape", 0, Transform{}, k_full, 16, -1, 1, -1, triangle_vertices(), {0, 1, 2}, k_triangle_sphere),
       full_lighting_shader_block(lighting), texture_paths_block({"C:\\Art\\Textures\\a_d.dds"})}, {0});
  const auto original = fo4::nif::read_nif(input);
  const auto fixed = fo4::nif::read_nif(fo4::nif::fix_nif(original, "absolute texture paths can be made relative"));
  EXPECT_EQ(fo4::nif::read_shapes(fixed).at(0).textures.at(0), "Textures\\a_d.dds");
}

// Detects: a triangle naming one vertex twice left in a rigid mesh, and a fix that keeps the good triangle and the vertices
TEST(NifLint, RepeatedVertexTriangleIsDroppedAndTheFixIsIdempotent) {
  const auto input = body_nif(k_triangle_sphere, {"Textures\\a_d.dds"}, {0, 1, 2, 0, 0, 1});
  const auto check = fo4::nif::verify_nif(fo4::nif::read_nif(input)).checks.at(15);
  EXPECT_TRUE(check.fixable);
  EXPECT_EQ(check.failure,
            "shape 'Body' (block 1) has 1 triangle(s) that name a vertex twice");

  const auto once = fo4::nif::fix_nif(fo4::nif::read_nif(input));
  const auto fixed = fo4::nif::read_nif(once);
  EXPECT_EQ(fo4::nif::verify_nif(fixed).checks.at(15).failure, "");
  EXPECT_EQ(fo4::nif::verify_nif(fixed).failed(), 0U);
  const auto before = fo4::nif::read_shapes(fo4::nif::read_nif(input));
  const auto after = fo4::nif::read_shapes(fixed);
  ASSERT_EQ(after.size(), 1U);
  EXPECT_EQ(after[0].tris, (std::vector<std::uint16_t>{0, 1, 2}));
  EXPECT_EQ(after[0].verts, before[0].verts);
  EXPECT_TRUE(fo4::nif::fix_nif(fixed) == once);
}

// Detects: a shape whose only triangle repeats a vertex, which the fix must leave in place since an empty shape fails another check
TEST(NifLint, ShapeWithOnlyRepeatedVertexTrianglesIsReportedButNotEmptied) {
  const auto input = body_nif(k_triangle_sphere, {"Textures\\a_d.dds"}, {0, 0, 1});
  const auto check = fo4::nif::verify_nif(fo4::nif::read_nif(input)).checks.at(15);
  EXPECT_FALSE(check.fixable);
  EXPECT_EQ(check.failure,
            "shape 'Body' (block 1) has 1 triangle(s) that name a vertex twice");
  const auto fixed = fo4::nif::read_shapes(fo4::nif::read_nif(fo4::nif::fix_nif(fo4::nif::read_nif(input))));
  ASSERT_EQ(fixed.size(), 1U);
  EXPECT_EQ(fixed[0].tris, (std::vector<std::uint16_t>{0, 0, 1}));
}

// Detects: a collinear triangle with three distinct vertices reported, which vanilla ships in quantity and the check must leave alone
TEST(NifLint, CollinearTriangleWithDistinctVerticesIsNotReported) {
  const auto input = body_nif(k_triangle_sphere, {"Textures\\a_d.dds"}, {0, 1, 2},
                              positions_bytes({{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}}));
  EXPECT_EQ(fo4::nif::verify_nif(fo4::nif::read_nif(input)).checks.at(15).failure, "");
}

// Detects: a header string that no block names going unreported, so stale names pile up in the table unnoticed
TEST(NifLint, HeaderStringNoBlockNamesIsReported) {
  const auto input = build_nif({"Root", "Body", "orphan"},
                               {named_node_block("NiNode", 0, Transform{}, {1}),
                                named_node_block("NiNode", 1, Transform{}, {})},
                               {0});
  EXPECT_EQ(fo4::nif::verify_nif(fo4::nif::read_nif(input)).checks.at(16).failure,
            "string 2 'orphan' is used by no block");
}

// Detects: a header string that a block names being reported as unused
TEST(NifLint, NamedHeaderStringsAreNotReportedAsUnused) {
  EXPECT_EQ(fo4::nif::verify_nif(fo4::nif::read_nif(body_nif(k_triangle_sphere, {"Textures\\a_d.dds"})))
                .checks.at(16)
                .failure,
            "");
}

// Detects: a sidecar that exists beside the mesh but cannot be read, reported with the file name the shape asks for
TEST(NifLint, UnreadableSidecarNamedBySubIndexShapeIsReported) {
  const auto nif = fo4::nif::read_nif(sub_index_nif(1, 2, 0, "Body.ssf"));
  std::vector<std::string> asked;
  const auto report = fo4::nif::verify_nif(nif, [&asked](std::string_view leaf) {
    asked.emplace_back(leaf);
    return false;
  });
  EXPECT_EQ(report.checks.at(17).failure,
            "shape 'Body' (block 1) names sidecar 'Body.ssf', which cannot be read beside the mesh");
  EXPECT_EQ(asked, (std::vector<std::string>{"Body.ssf"}));
}

// Detects: a sidecar beside the mesh that exists passing the check, so a present file never fails verify
TEST(NifLint, PresentSidecarNamedBySubIndexShapePasses) {
  const auto nif = fo4::nif::read_nif(sub_index_nif(1, 2, 0, "Body.ssf"));
  EXPECT_EQ(fo4::nif::verify_nif(nif, [](std::string_view) { return true; }).checks.at(17).failure, "");
}

// Detects: a sidecar named with folders being looked up by its leaf name beside the mesh, not by the full reference
TEST(NifLint, SidecarIsLookedUpByItsLeafNameBesideTheMesh) {
  const auto nif = fo4::nif::read_nif(sub_index_nif(1, 2, 0, "Actors\\Male\\Body.ssf"));
  std::vector<std::string> asked;
  const auto report = fo4::nif::verify_nif(nif, [&asked](std::string_view leaf) {
    asked.emplace_back(leaf);
    return false;
  });
  EXPECT_EQ(asked, (std::vector<std::string>{"Body.ssf"}));
  EXPECT_EQ(report.checks.at(17).failure,
            "shape 'Body' (block 1) names sidecar 'Actors\\Male\\Body.ssf', which cannot be read beside the mesh");
}

// Detects: a sub-index shape with no segment data being looked up for a sidecar it never names
TEST(NifLint, SubIndexShapeWithoutSegmentDataIsNotJudgedForASidecar) {
  const auto nif = fo4::nif::read_nif(sub_index_nif(2, 2, 0, "Body.ssf"));
  std::vector<std::string> asked;
  const auto report = fo4::nif::verify_nif(nif, [&asked](std::string_view leaf) {
    asked.emplace_back(leaf);
    return false;
  });
  EXPECT_EQ(report.checks.at(17).failure, "");
  EXPECT_TRUE(asked.empty());
}

// Detects: a sidecar name read at the wrong offset when the segments carry sub-segments, which would ask for garbage
TEST(NifLint, SidecarIsReadPastTheSubSegmentsOfEachSegment) {
  const auto nif = fo4::nif::read_nif(sub_index_nif(1, 2, 2, "Body.ssf"));
  std::vector<std::string> asked;
  fo4::nif::verify_nif(nif, [&asked](std::string_view leaf) {
    asked.emplace_back(leaf);
    return false;
  });
  EXPECT_EQ(asked, (std::vector<std::string>{"Body.ssf"}));
}

// Detects: a file without a probe failing every sub-index shape, since nothing is read to judge the sidecar by
TEST(NifLint, WithoutAProbeTheSidecarCheckIsNotJudged) {
  const auto nif = fo4::nif::read_nif(sub_index_nif(1, 2, 0, "Body.ssf"));
  EXPECT_EQ(fo4::nif::verify_nif(nif).checks.at(17).failure, "");
}

// Detects: a segment table cut short being reported as not parsing, instead of reading past the end of its block
TEST(NifLint, TruncatedSegmentDataIsReportedAsNotParsing) {
  const auto nif = fo4::nif::read_nif(sub_index_nif(1, 2, 0, "Body.ssf", false));
  const auto failure = fo4::nif::verify_nif(nif, [](std::string_view) { return true; }).checks.at(17).failure;
  EXPECT_EQ(failure.rfind("shape 'Body' (block 1) segment data does not parse: ", 0), 0U) << failure;
}

// Detects: a check marked fixable that fix_nif does not repair, which would offer a button that leaves its finding in place
TEST(NifLint, OnlyTheChecksThatFixNifRepairsAreFixable) {
  const auto report = fo4::nif::verify_nif(fo4::nif::read_nif(body_nif({}, {"C:\\Art\\a_d.dds"})));
  const std::array<std::size_t, 4> repairable{2, 13, 14, 15};
  ASSERT_EQ(report.checks.size(), 18U);
  for (std::size_t i = 0; i < report.checks.size(); ++i) {
    const bool expected = std::find(repairable.begin(), repairable.end(), i) != repairable.end();
    EXPECT_EQ(report.checks[i].fixable, expected) << report.checks[i].name;
  }
}

// Detects: triangle repair on a dynamic shape must preserve its trailing bytes and remain idempotent.
TEST(NifLint, DynamicShapeKeepsTheBytesAfterItsTriangles) {
  Block shape = shape_with_refs("BSDynamicTriShape", 1, Transform{}, k_full, 16, -1, -1, -1, triangle_vertices(),
                                {0, 0, 1, 0, 1, 2}, k_triangle_sphere);
  Writer tail;
  tail.u32(1);
  for (const float value : {7.0f, 8.0f, 9.0f, 10.0f}) tail.f32(value);
  shape.data.insert(shape.data.end(), tail.bytes().begin(), tail.bytes().end());
  const auto input = build_nif({"Root", "Body", "shaderName"},
                               {named_node_block("NiNode", 0, Transform{}, {1}), shape}, {0});
  const auto original = fo4::nif::read_nif(input);
  ASSERT_EQ(fo4::nif::read_shapes(original).at(0).tris, (std::vector<std::uint16_t>{0, 0, 1, 0, 1, 2}));
  EXPECT_NE(fo4::nif::verify_nif(original).checks.at(15).failure, "");
  const auto once = fo4::nif::fix_nif(original);
  const auto fixed = fo4::nif::read_nif(once);
  const auto shapes = fo4::nif::read_shapes(fixed);
  ASSERT_EQ(shapes.size(), 1U);
  EXPECT_EQ(shapes[0].tris, (std::vector<std::uint16_t>{0, 1, 2}));
  const auto& block = fixed.blocks()[1];
  const auto end = fixed.bytes().begin() + static_cast<std::ptrdiff_t>(block.offset + block.size);
  EXPECT_TRUE(std::equal(tail.bytes().begin(), tail.bytes().end(), end - static_cast<std::ptrdiff_t>(tail.bytes().size())));
  EXPECT_EQ(fo4::nif::fix_nif(fixed), once);
}

// regression: a lint check that fires on a shipped NIF would flag files the game loads fine
TEST(NifLintReal, VanillaNifsGiveNoLintFindings) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  record_game_build(detect_game_build(dir));
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";

  const auto assets = fo4::archive::AssetResolver::open({}, archives);
  std::map<std::string, std::size_t> findings;
  std::size_t nifs = 0;
  std::size_t parse_failures = 0;
  std::size_t sidecar_lookups = 0;
  std::size_t sidecars_missing = 0;
  std::string first_finding;
  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    // Constraint: one in 37 keeps this test inside the shared real-data job.
    for (std::size_t i = 0; i < reader.entries().size(); i += 37) {
      const auto& path = reader.entries()[i].path;
      if (lower(std::filesystem::path(path).extension().string()) != ".nif") continue;
      ++nifs;
      try {
        const auto bytes = reader.extract(i);
        const auto nif = fo4::nif::read_nif(bytes);
        const auto sidecar = [&assets, &path, &sidecar_lookups, &sidecars_missing](std::string_view leaf) {
          ++sidecar_lookups;
          const auto found = assets.locate(sibling_path(path, leaf));
          if (!found) {
            ++sidecars_missing;
            return true;
          }
          try {
            assets.read(*found);
            return true;
          } catch (const std::exception&) {
            return false;
          }
        };
        for (const auto& check : fo4::nif::lint_checks(nif, fo4::nif::read_shapes(nif), sidecar)) {
          if (check.failure.empty()) continue;
          ++findings[check.name];
          if (first_finding.empty()) first_finding = path + ": " + check.failure;
        }
      } catch (const fo4::parse_error& error) {
        ++parse_failures;
        ADD_FAILURE() << path << ": " << error.what();
      }
    }
  }

  for (const auto& [name, count] : findings) std::cout << "lint check fires on vanilla: " << name << ": " << count << '\n';
  std::cout << "nifs " << nifs << ", parse failures " << parse_failures << ", sidecar lookups " << sidecar_lookups
            << ", sidecars not shipped " << sidecars_missing << '\n';
  RecordProperty("lint_check_nif_count", std::to_string(nifs));
  RecordProperty("lint_check_sidecar_lookups", std::to_string(sidecar_lookups));
  EXPECT_TRUE(findings.empty()) << first_finding;
}
