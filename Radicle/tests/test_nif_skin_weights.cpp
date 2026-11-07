#include "fo4/archive/assets.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/shape_ops.hpp"
#include "nif_fixture.hpp"
#include "real_data_plugins.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using fo4::nif::Shape;
using fo4::nif::ShapeOp;
using fo4::nif::ShapeOpReport;
using fo4::nif::ShapeOpRequest;
using fo4::nif::Transform;
using fo4::nif::apply_shape_op;
using fo4::nif::parse_shape_op;
using fo4::nif::read_nif;
using fo4::nif::read_shape;
using fo4::nif::read_shapes;
using fo4::nif::shape_op_name;
using nif_fixture::Block;
using nif_fixture::Vertex;

using Weights = std::array<std::uint16_t, 4>;
using Bones = std::array<std::uint8_t, 4>;

constexpr std::uint32_t k_has_uv2 = 1U << 2;
constexpr std::uint32_t k_body_flags = nif_fixture::k_full | nif_fixture::k_has_uv | nif_fixture::k_has_normal |
                                       nif_fixture::k_has_tangent | nif_fixture::k_has_colour |
                                       nif_fixture::k_has_skin;
constexpr std::size_t k_body_stride = 44;
constexpr std::uint16_t k_one = 0x3C00;
constexpr std::uint16_t k_half = 0x3800;
constexpr std::uint16_t k_quarter = 0x3400;
constexpr std::uint16_t k_eighth = 0x3000;
constexpr float k_tolerance = 2e-3f;
constexpr std::size_t k_real_shape_limit = 200;
const std::vector<std::uint16_t> k_quad{0, 1, 2, 0, 2, 3};

Vertex skinned(float x, float y, float z, Weights weights, Bones bones) {
  auto vertex = nif_fixture::skinned_vertex(x, y, z, 0);
  for (std::size_t i = 0; i < 4; ++i) {
    vertex.weights[i] = weights[i];
    vertex.bones[i] = bones[i];
  }
  return vertex;
}

std::vector<Vertex> body_vertices() {
  return {skinned(0, 0, 0, {k_half, k_quarter, 0, 0}, {0, 1, 0, 0}),
          skinned(1, 0, 0, {k_one, 0, 0, 0}, {0, 0, 0, 0}),
          skinned(1, 1, 0, {k_eighth, k_half, k_quarter, k_eighth}, {0, 1, 0, 1}),
          skinned(0, 1, 0, {k_half, k_half, 0, 0}, {0, 1, 0, 0})};
}

std::vector<Vertex> armor_vertices(Weights first_weights, Bones first_bones) {
  return {skinned(-9, 0, 0, first_weights, first_bones), skinned(-8, 0, 0, {k_one, 0, 0, 0}, {2, 0, 0, 0}),
          skinned(-8, 1, 0, {k_one, 0, 0, 0}, {2, 0, 0, 0}), skinned(-9, 1, 0, {k_one, 0, 0, 0}, {2, 0, 0, 0})};
}

std::vector<std::byte> encode(const std::vector<Vertex>& vertices) {
  std::vector<std::byte> bytes;
  for (const auto& vertex : vertices) {
    const auto encoded = nif_fixture::encode_vertex(vertex);
    bytes.insert(bytes.end(), encoded.begin(), encoded.end());
  }
  return bytes;
}

Transform armor_transform() {
  Transform xf;
  xf.t[0] = 10.0f;
  return xf;
}

std::vector<std::byte> skin_file(const std::vector<Vertex>& body, const std::vector<Vertex>& armor,
                                 std::int32_t body_skin = 2,
                                 const std::vector<std::int32_t>& body_bones = {3, 4}) {
  const std::vector<Block> blocks{
      nif_fixture::node_block("NiNode", 0, Transform{}, {1, 5}),
      nif_fixture::shape_block("BSTriShape", 1, Transform{}, k_body_flags, k_body_stride, body_skin, -1, -1,
                               encode(body), k_quad),
      nif_fixture::skin_instance_block(0, -1, body_bones),
      nif_fixture::node_block("NiNode", 3, Transform{}, {}),
      nif_fixture::node_block("NiNode", 4, Transform{}, {}),
      nif_fixture::shape_block("BSTriShape", 2, armor_transform(), k_body_flags, k_body_stride, 6, -1, -1,
                               encode(armor), k_quad),
      nif_fixture::skin_instance_block(0, -1, {7, 8, 9}),
      nif_fixture::node_block("NiNode", 5, Transform{}, {}),
      nif_fixture::node_block("NiNode", 3, Transform{}, {}),
      nif_fixture::node_block("NiNode", 4, Transform{}, {}),
  };
  return nif_fixture::build_nif({"root", "body", "armor", "Spine", "Arm", "Head"}, blocks, {0});
}

void append_skin_only(nif_fixture::Writer& writer, float x, float y, float z, Weights weights, Bones bones) {
  writer.f32(x);
  writer.f32(y);
  writer.f32(z);
  writer.f32(0.0f);
  for (const auto weight : weights) writer.u16(weight);
  for (const auto bone : bones) writer.u8(bone);
}

std::vector<std::byte> skin_only_file() {
  nif_fixture::Writer writer;
  append_skin_only(writer, 0, 0, 0, {k_half, k_quarter, 0, 0}, {0, 1, 0, 0});
  append_skin_only(writer, 1, 0, 0, {k_one, 0, 0, 0}, {0, 0, 0, 0});
  append_skin_only(writer, 0, 1, 0, {k_half, k_half, 0, 0}, {0, 0, 0, 0});
  const std::vector<Block> blocks{
      nif_fixture::node_block("NiNode", 0, Transform{}, {1}),
      nif_fixture::shape_block("BSTriShape", 1, Transform{}, nif_fixture::k_full | nif_fixture::k_has_skin, 28, 2,
                               -1, -1, writer.bytes(), {0, 1, 2}),
      nif_fixture::skin_instance_block(0, -1, {3, 4}),
      nif_fixture::node_block("NiNode", 2, Transform{}, {}),
      nif_fixture::node_block("NiNode", 3, Transform{}, {}),
  };
  return nif_fixture::build_nif({"root", "body", "Spine", "Arm"}, blocks, {0});
}

void append_colour_skin(nif_fixture::Writer& writer, bool full, Weights weights, Bones bones) {
  for (int i = 0; i < 4; ++i) {
    if (full) {
      writer.f32(0.0f);
    } else {
      writer.u16(0);
    }
  }
  writer.u16(k_half);
  writer.u16(k_quarter);
  if (full) writer.u32(0);
  writer.u32(0);
  writer.u32(0);
  writer.u32(0xFF336699U);
  for (const auto weight : weights) writer.u16(weight);
  for (const auto bone : bones) writer.u8(bone);
}

std::vector<std::byte> colour_skin_file(bool full) {
  nif_fixture::Writer writer;
  append_colour_skin(writer, full, {k_half, k_quarter, 0, 0}, {0, 1, 0, 0});
  append_colour_skin(writer, full, {k_one, 0, 0, 0}, {1, 0, 0, 0});
  const std::uint32_t precision = full ? nif_fixture::k_full | k_has_uv2 : 0U;
  const std::uint32_t flags = precision | nif_fixture::k_has_uv | nif_fixture::k_has_normal |
                              nif_fixture::k_has_tangent | nif_fixture::k_has_colour | nif_fixture::k_has_skin;
  const std::size_t stride = full ? 48 : 36;
  const std::vector<Block> blocks{
      nif_fixture::node_block("NiNode", 0, Transform{}, {1}),
      nif_fixture::shape_block("BSTriShape", 1, Transform{}, flags, stride, 2, -1, -1, writer.bytes(), {0, 1, 0}),
      nif_fixture::skin_instance_block(0, -1, {3, 4}),
      nif_fixture::node_block("NiNode", 2, Transform{}, {}),
      nif_fixture::node_block("NiNode", 3, Transform{}, {}),
  };
  return nif_fixture::build_nif({"root", "body", "Spine", "Arm"}, blocks, {0});
}

struct Edited {
  std::vector<std::byte> bytes;
  ShapeOpReport report;
};

Edited edit(const std::vector<std::byte>& bytes, const ShapeOpRequest& request) {
  Edited edited;
  edited.bytes = apply_shape_op(read_nif(bytes), request, &edited.report);
  return edited;
}

ShapeOpRequest request_for(ShapeOp op, std::vector<std::uint32_t> blocks = {}) {
  ShapeOpRequest request;
  request.op = op;
  request.blocks = std::move(blocks);
  return request;
}

Shape shape_of(const std::vector<std::byte>& bytes, std::uint32_t block) {
  return read_shape(read_nif(bytes), block);
}

bool is_changed(const ShapeOpReport& report, std::uint32_t block) {
  return std::find(report.changed.begin(), report.changed.end(), block) != report.changed.end();
}

void expect_weights(const Shape& shape, std::size_t vertex, const std::array<float, 4>& expected) {
  for (std::size_t i = 0; i < 4; ++i) {
    EXPECT_NEAR(shape.weights[vertex * 4 + i], expected[i], k_tolerance) << "vertex " << vertex << " slot " << i;
  }
}

void expect_bones(const Shape& shape, std::size_t vertex, const std::array<std::uint8_t, 4>& expected) {
  for (std::size_t i = 0; i < 4; ++i) {
    EXPECT_EQ(shape.bone_indices[vertex * 4 + i], expected[i]) << "vertex " << vertex << " slot " << i;
  }
}

void expect_sums_to_one(const Shape& shape) {
  for (std::size_t v = 0; v + 4 <= shape.weights.size(); v += 4) {
    const float sum = shape.weights[v] + shape.weights[v + 1] + shape.weights[v + 2] + shape.weights[v + 3];
    if (sum == 0.0f) continue;
    EXPECT_NEAR(sum, 1.0f, k_tolerance) << "vertex " << v / 4;
  }
}

bool is_nif(const std::string& path) {
  auto extension = std::filesystem::path(path).extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return extension == ".nif";
}

}  // namespace

TEST(SkinWeights, ParsesTheFourSkinWeightOperations) {
  for (const char* name : {"normalize_weights", "limit_influences", "prune_weights", "copy_weights"}) {
    const auto op = parse_shape_op(name);
    ASSERT_TRUE(op.has_value()) << name;
    EXPECT_EQ(std::string(shape_op_name(*op)), name);
  }
  EXPECT_FALSE(parse_shape_op("skin_weights").has_value());
}

TEST(SkinWeights, NormalizeScalesEachVertexToOneAndLeavesTheOtherShapeAlone) {
  const auto file = skin_file(body_vertices(), armor_vertices({k_one, 0, 0, 0}, {0, 0, 0, 0}));
  const auto edited = edit(file, request_for(ShapeOp::normalize_weights, {1}));

  EXPECT_EQ(edited.report.changed, std::vector<std::uint32_t>{1});
  EXPECT_TRUE(edited.report.skipped.empty());
  EXPECT_EQ(edited.report.vertices_changed, 1U);
  EXPECT_NEAR(edited.report.max_weight_change, 1.0f / 6.0f, k_tolerance);

  const auto body = shape_of(edited.bytes, 1);
  expect_weights(body, 0, {2.0f / 3.0f, 1.0f / 3.0f, 0.0f, 0.0f});
  expect_bones(body, 0, {0, 1, 0, 0});
  expect_sums_to_one(body);
  EXPECT_EQ(shape_of(edited.bytes, 5).weights, shape_of(file, 5).weights);
}

TEST(SkinWeights, NormalizeLeavesAnAlreadyEmptyVertexByteIdentical) {
  auto vertices = body_vertices();
  vertices[1] = skinned(1, 0, 0, {0, 0, 0, 0}, {1, 0, 1, 0});
  const auto file = skin_file(vertices, armor_vertices({k_one, 0, 0, 0}, {0, 0, 0, 0}));
  const auto edited = edit(file, request_for(ShapeOp::normalize_weights, {1}));

  EXPECT_EQ(edited.report.changed, std::vector<std::uint32_t>{1});
  EXPECT_TRUE(edited.report.skipped.empty());
  EXPECT_EQ(edited.report.vertices_changed, 1U);
  const auto body = shape_of(edited.bytes, 1);
  expect_weights(body, 0, {2.0f / 3.0f, 1.0f / 3.0f, 0.0f, 0.0f});
  expect_weights(body, 1, {0.0f, 0.0f, 0.0f, 0.0f});
  expect_bones(body, 1, {1, 0, 1, 0});
}

TEST(SkinWeights, LimitInfluencesKeepsTheLargestAndRenormalizes) {
  const auto file = skin_file(body_vertices(), armor_vertices({k_one, 0, 0, 0}, {0, 0, 0, 0}));
  auto request = request_for(ShapeOp::limit_influences, {1});
  request.max_influences = 2;
  const auto edited = edit(file, request);

  EXPECT_EQ(edited.report.changed, std::vector<std::uint32_t>{1});
  EXPECT_EQ(edited.report.vertices_changed, 1U);
  const auto body = shape_of(edited.bytes, 1);
  expect_weights(body, 0, {0.5f, 0.25f, 0.0f, 0.0f});
  expect_weights(body, 2, {0.0f, 2.0f / 3.0f, 1.0f / 3.0f, 0.0f});
  EXPECT_EQ(body.bone_indices[9], 1);
  EXPECT_EQ(body.bone_indices[10], 0);
}

TEST(SkinWeights, LimitRejectsCountsOutsideOneToFour) {
  const auto file = skin_file(body_vertices(), armor_vertices({k_one, 0, 0, 0}, {0, 0, 0, 0}));
  auto request = request_for(ShapeOp::limit_influences, {1});
  request.max_influences = 0;
  EXPECT_THROW(edit(file, request), std::invalid_argument);
  request.max_influences = 5;
  EXPECT_THROW(edit(file, request), std::invalid_argument);
}

TEST(SkinWeights, PruneDropsWeightsBelowThresholdAndRenormalizes) {
  const auto file = skin_file(body_vertices(), armor_vertices({k_one, 0, 0, 0}, {0, 0, 0, 0}));
  auto request = request_for(ShapeOp::prune_weights, {1});
  request.prune_threshold = 0.3f;
  const auto edited = edit(file, request);

  EXPECT_EQ(edited.report.changed, std::vector<std::uint32_t>{1});
  EXPECT_EQ(edited.report.vertices_changed, 2U);
  const auto body = shape_of(edited.bytes, 1);
  expect_weights(body, 0, {1.0f, 0.0f, 0.0f, 0.0f});
  expect_weights(body, 2, {0.0f, 1.0f, 0.0f, 0.0f});
  expect_weights(body, 3, {0.5f, 0.5f, 0.0f, 0.0f});
}

TEST(SkinWeights, PruneRefusesWhenAVertexWouldBeEmpty) {
  const auto file = skin_file(body_vertices(), armor_vertices({k_one, 0, 0, 0}, {0, 0, 0, 0}));
  auto request = request_for(ShapeOp::prune_weights, {1});
  request.prune_threshold = 0.9f;
  const auto edited = edit(file, request);

  EXPECT_TRUE(edited.report.changed.empty());
  ASSERT_EQ(edited.report.skipped.size(), 1U);
  EXPECT_EQ(edited.report.skipped[0], "block 1: vertex 0 would end with no positive bone weight");
}

TEST(SkinWeights, PruneRejectsAThresholdOfOneOrMore) {
  const auto file = skin_file(body_vertices(), armor_vertices({k_one, 0, 0, 0}, {0, 0, 0, 0}));
  auto request = request_for(ShapeOp::prune_weights, {1});
  request.prune_threshold = 1.0f;
  EXPECT_THROW(edit(file, request), std::invalid_argument);
}

TEST(SkinWeights, ShapeWithoutSkinIsRefused) {
  const auto file = skin_file(body_vertices(), armor_vertices({k_one, 0, 0, 0}, {0, 0, 0, 0}), -1);
  const auto edited = edit(file, request_for(ShapeOp::normalize_weights, {1}));

  EXPECT_TRUE(edited.report.changed.empty());
  ASSERT_EQ(edited.report.skipped.size(), 1U);
  EXPECT_EQ(edited.report.skipped[0], "block 1: no skin");
}

TEST(SkinWeights, SkinWithoutBonesIsRefused) {
  const auto file = skin_file(body_vertices(), armor_vertices({k_one, 0, 0, 0}, {0, 0, 0, 0}), 2, {});
  const auto edited = edit(file, request_for(ShapeOp::normalize_weights, {1}));

  EXPECT_TRUE(edited.report.changed.empty());
  ASSERT_EQ(edited.report.skipped.size(), 1U);
  EXPECT_EQ(edited.report.skipped[0], "block 1: the skin has no bones");
}

TEST(SkinWeights, CopyTakesTheNearestGlobalVertexAndRemapsBoneNames) {
  const auto file = skin_file(body_vertices(), armor_vertices({k_one, 0, 0, 0}, {0, 0, 0, 0}));
  auto request = request_for(ShapeOp::copy_weights, {5});
  request.source_block = 1;
  const auto edited = edit(file, request);

  EXPECT_EQ(edited.report.changed, std::vector<std::uint32_t>{5});
  EXPECT_TRUE(edited.report.skipped.empty());
  const auto armor = shape_of(edited.bytes, 5);
  expect_weights(armor, 0, {1.0f, 0.0f, 0.0f, 0.0f});
  expect_bones(armor, 0, {1, 0, 0, 0});
  expect_weights(armor, 1, {1.0f, 0.0f, 0.0f, 0.0f});
  expect_bones(armor, 1, {1, 0, 0, 0});
  expect_weights(armor, 2, {0.125f, 0.5f, 0.25f, 0.125f});
  expect_bones(armor, 2, {1, 2, 1, 2});
  expect_bones(armor, 3, {1, 2, 1, 2});
  EXPECT_EQ(shape_of(edited.bytes, 1).weights, shape_of(file, 1).weights);
}

TEST(SkinWeights, CopyRefusesNamingTheBonesTheTargetLacks) {
  const auto file = skin_file(body_vertices(), armor_vertices({k_one, 0, 0, 0}, {0, 0, 0, 0}));
  auto request = request_for(ShapeOp::copy_weights, {1});
  request.source_block = 5;
  const auto edited = edit(file, request);

  EXPECT_TRUE(edited.report.changed.empty());
  ASSERT_EQ(edited.report.skipped.size(), 1U);
  EXPECT_EQ(edited.report.skipped[0], "block 1: the target skin lacks bone(s) Head");
}

TEST(SkinWeights, CopyIgnoresAMissingBoneThatCarriesNoWeight) {
  const auto file = skin_file(body_vertices(), armor_vertices({0, k_one, 0, 0}, {0, 2, 0, 0}));
  auto request = request_for(ShapeOp::copy_weights, {1});
  request.source_block = 5;
  const auto edited = edit(file, request);

  EXPECT_EQ(edited.report.changed, std::vector<std::uint32_t>{1});
  EXPECT_TRUE(edited.report.skipped.empty());
  const auto body = shape_of(edited.bytes, 1);
  expect_weights(body, 0, {0.0f, 1.0f, 0.0f, 0.0f});
  expect_bones(body, 0, {0, 1, 0, 0});
}

TEST(SkinWeights, CopyNeedsASourceBlockAndRefusesItself) {
  const auto file = skin_file(body_vertices(), armor_vertices({k_one, 0, 0, 0}, {0, 0, 0, 0}));
  EXPECT_THROW(edit(file, request_for(ShapeOp::copy_weights, {1})), std::invalid_argument);

  auto request = request_for(ShapeOp::copy_weights, {1});
  request.source_block = 1;
  const auto edited = edit(file, request);
  EXPECT_TRUE(edited.report.changed.empty());
  ASSERT_EQ(edited.report.skipped.size(), 1U);
  EXPECT_EQ(edited.report.skipped[0], "block 1: the copy source is this shape");
}

TEST(SkinWeights, SkinOnlyVerticesReadAndWriteAtTheirOwnOffset) {
  const auto file = skin_only_file();
  const auto edited = edit(file, request_for(ShapeOp::normalize_weights, {1}));

  EXPECT_EQ(edited.report.changed, std::vector<std::uint32_t>{1});
  EXPECT_EQ(edited.report.vertices_changed, 1U);
  const auto body = shape_of(edited.bytes, 1);
  expect_weights(body, 0, {2.0f / 3.0f, 1.0f / 3.0f, 0.0f, 0.0f});
  expect_bones(body, 0, {0, 1, 0, 0});
  expect_weights(body, 1, {1.0f, 0.0f, 0.0f, 0.0f});
  expect_sums_to_one(body);
}

TEST(SkinWeights, SkinAtFollowsColourAndUv2AtFullPrecision) {
  const auto file = colour_skin_file(true);
  const auto edited = edit(file, request_for(ShapeOp::normalize_weights, {1}));

  EXPECT_EQ(edited.report.changed, std::vector<std::uint32_t>{1});
  EXPECT_EQ(edited.report.vertices_changed, 1U);
  const auto body = shape_of(edited.bytes, 1);
  expect_weights(body, 0, {2.0f / 3.0f, 1.0f / 3.0f, 0.0f, 0.0f});
  expect_bones(body, 0, {0, 1, 0, 0});
  expect_weights(body, 1, {1.0f, 0.0f, 0.0f, 0.0f});
  expect_bones(body, 1, {1, 0, 0, 0});
  EXPECT_EQ(body.colors, shape_of(file, 1).colors);
}

TEST(SkinWeights, SkinAtFollowsColourAtHalfPrecision) {
  const auto file = colour_skin_file(false);
  const auto edited = edit(file, request_for(ShapeOp::normalize_weights, {1}));

  EXPECT_EQ(edited.report.changed, std::vector<std::uint32_t>{1});
  EXPECT_EQ(edited.report.vertices_changed, 1U);
  const auto body = shape_of(edited.bytes, 1);
  expect_weights(body, 0, {2.0f / 3.0f, 1.0f / 3.0f, 0.0f, 0.0f});
  expect_bones(body, 0, {0, 1, 0, 0});
  expect_weights(body, 1, {1.0f, 0.0f, 0.0f, 0.0f});
  expect_bones(body, 1, {1, 0, 0, 0});
  EXPECT_EQ(body.colors, shape_of(file, 1).colors);
}

TEST(NifSkinWeightsRealData, NormalizedSkinnedShapesSumToOneAfterReread) {
  const char* dir = std::getenv("FO4_DATA_DIR");
  if (dir == nullptr) GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  const auto archives = fo4::archive::archive_load_order(dir, {}, installed_master_and_cc_plugins(dir));
  if (archives.empty()) {
    GTEST_SKIP() << "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives";
  }

  const auto assets = fo4::archive::AssetResolver::open({}, archives);
  std::size_t checked = 0;
  std::size_t vertices = 0;
  for (const auto& reader : assets.archives()) {
    if (reader.kind() != fo4::archive::Ba2Kind::general) continue;
    for (std::size_t i = 0; i < reader.entries().size() && checked < k_real_shape_limit; ++i) {
      if (!is_nif(reader.entries()[i].path)) continue;
      const auto bytes = reader.extract(i);
      std::vector<std::uint32_t> blocks;
      for (const auto& shape : read_shapes(read_nif(bytes))) {
        if (shape.skin_ref >= 0 && !shape.bones.empty() && !shape.weights.empty()) blocks.push_back(shape.block);
      }
      if (blocks.empty()) continue;
      const auto edited = edit(bytes, request_for(ShapeOp::normalize_weights, blocks));
      for (const auto& shape : read_shapes(read_nif(edited.bytes))) {
        if (!is_changed(edited.report, shape.block)) continue;
        ++checked;
        vertices += shape.weights.size() / 4;
        expect_sums_to_one(shape);
      }
    }
  }
  std::cout << "skin weights corpus: shapes " << checked << ", vertices " << vertices << '\n';
  EXPECT_GT(checked, 0U);
}
