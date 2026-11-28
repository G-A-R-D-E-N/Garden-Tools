#include "fo4/archive/assets.hpp"
#include "fo4/c_api_nif_skin_tools.h"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/outfit_studio.hpp"
#include "fo4/nif/shape_join.hpp"
#include "fo4/nif/shape_model_ops.hpp"
#include "fo4/nif/shape_ops.hpp"
#include "fo4/nif/skin.hpp"
#include "fo4/nif/skin_create.hpp"
#include "fo4/nif/vertex_desc.hpp"
#include "fo4/nif/customization_remap.hpp"
#include "fo4/nif/weight_brush.hpp"
#include "fo4/nif/weight_transfer.hpp"
#include "nif_fixture.hpp"
#include "real_data_plugins.hpp"
#include "real_data.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
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
constexpr std::uint16_t k_negative_quarter = 0xB400;
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

nif_fixture::Block identity_bone_data(std::size_t count) {
  const nif_fixture::BoneFixture identity{"", {0.0f, 0.0f, 0.0f}, 0.0f,
                                          {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f},
                                          {0.0f, 0.0f, 0.0f}, 1.0f};
  return nif_fixture::bone_data_block(std::vector<nif_fixture::BoneFixture>(count, identity));
}

std::vector<std::byte> skin_file(const std::vector<Vertex>& body, const std::vector<Vertex>& armor,
                                 std::int32_t body_skin = 2,
                                 const std::vector<std::int32_t>& body_bones = {3, 4},
                                 const std::vector<std::int32_t>& armor_bones = {7, 8, 9},
                                 std::int32_t arm_name = 4) {
  const std::vector<Block> blocks{
      nif_fixture::node_block("NiNode", 0, Transform{}, {1, 5}),
      nif_fixture::shape_block("BSTriShape", 1, Transform{}, k_body_flags, k_body_stride, body_skin, -1, -1,
                               encode(body), k_quad),
      nif_fixture::skin_instance_block(0, 10, body_bones),
      nif_fixture::node_block("NiNode", 3, Transform{}, {}),
      nif_fixture::node_block("NiNode", arm_name, Transform{}, {}),
      nif_fixture::shape_block("BSTriShape", 2, armor_transform(), k_body_flags, k_body_stride, 6, -1, -1,
                               encode(armor), k_quad),
      nif_fixture::skin_instance_block(0, 11, armor_bones),
      nif_fixture::node_block("NiNode", 5, Transform{}, {}),
      nif_fixture::node_block("NiNode", 3, Transform{}, {}),
      nif_fixture::node_block("NiNode", arm_name, Transform{}, {}),
      identity_bone_data(body_bones.size()),
      identity_bone_data(armor_bones.size()),
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
      nif_fixture::skin_instance_block(0, 5, {3, 4}),
      nif_fixture::node_block("NiNode", 2, Transform{}, {}),
      nif_fixture::node_block("NiNode", 3, Transform{}, {}),
      identity_bone_data(2),
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
      nif_fixture::skin_instance_block(0, 5, {3, 4}),
      nif_fixture::node_block("NiNode", 2, Transform{}, {}),
      nif_fixture::node_block("NiNode", 3, Transform{}, {}),
      identity_bone_data(2),
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

TEST(SkinWeights, NormalizeRefusesAVertexWithANegativeInfluence) {
  auto vertices = body_vertices();
  vertices[0] = skinned(0, 0, 0, {k_one, k_negative_quarter, 0, 0}, {0, 1, 0, 0});
  const auto file = skin_file(vertices, armor_vertices({k_one, 0, 0, 0}, {0, 0, 0, 0}));
  const auto edited = edit(file, request_for(ShapeOp::normalize_weights, {1}));

  EXPECT_TRUE(edited.report.changed.empty());
  ASSERT_EQ(edited.report.skipped.size(), 1U);
  EXPECT_EQ(edited.report.skipped[0], "block 1: vertex 0 would end with a negative weight");
  expect_weights(shape_of(edited.bytes, 1), 0, {1.0f, -0.25f, 0.0f, 0.0f});
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

TEST(SkinWeights, CopyChoosesTheNearestSourceWhenSquaredDistancesOverflowSinglePrecision) {
  auto body = body_vertices();
  body[0] = skinned(3.0e20f, 0, 0, {k_one, 0, 0, 0}, {0, 0, 0, 0});
  body[1] = skinned(2.0e20f, 0, 0, {k_one, 0, 0, 0}, {1, 0, 0, 0});
  body[2] = skinned(3.0e20f, 0, 0, {k_one, 0, 0, 0}, {0, 0, 0, 0});
  body[3] = skinned(-3.0e20f, 0, 0, {k_one, 0, 0, 0}, {0, 0, 0, 0});
  const auto file = skin_file(body, armor_vertices({k_one, 0, 0, 0}, {0, 0, 0, 0}));
  auto request = request_for(ShapeOp::copy_weights, {5});
  request.source_block = 1;
  const auto edited = edit(file, request);

  EXPECT_EQ(edited.report.changed, std::vector<std::uint32_t>{5});
  const auto armor = shape_of(edited.bytes, 5);
  expect_weights(armor, 0, {1.0f, 0.0f, 0.0f, 0.0f});
  expect_bones(armor, 0, {2, 0, 0, 0});
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

// Detects: a copy onto a target that names a used bone twice, which must refuse rather than map to the first match.
TEST(SkinWeights, CopyRefusesATargetThatNamesAUsedBoneTwice) {
  const auto file = skin_file(body_vertices(), armor_vertices({k_one, 0, 0, 0}, {0, 0, 0, 0}), 2, {3, 4}, {7, 8, 9, 3});
  auto request = request_for(ShapeOp::copy_weights, {5});
  request.source_block = 1;
  const auto edited = edit(file, request);

  EXPECT_TRUE(edited.report.changed.empty());
  ASSERT_EQ(edited.report.skipped.size(), 1U);
  EXPECT_EQ(edited.report.skipped[0], "block 5: the target skin names more than one bone \"Spine\"");
  EXPECT_EQ(shape_of(edited.bytes, 5).weights, shape_of(file, 5).weights);
  EXPECT_EQ(shape_of(edited.bytes, 5).bone_indices, shape_of(file, 5).bone_indices);
}

// Detects: a used unnamed source bone that must refuse instead of matching an unnamed target bone by the empty name.
TEST(SkinWeights, CopyRefusesAUsedSourceBoneWithNoName) {
  const auto file = skin_file(body_vertices(), armor_vertices({k_one, 0, 0, 0}, {0, 0, 0, 0}), 2, {3, 4}, {7, 8, 9}, -1);
  auto request = request_for(ShapeOp::copy_weights, {5});
  request.source_block = 1;
  const auto edited = edit(file, request);

  EXPECT_TRUE(edited.report.changed.empty());
  ASSERT_EQ(edited.report.skipped.size(), 1U);
  EXPECT_EQ(edited.report.skipped[0], "block 5: the copy source uses bone 1, which has no name");
  EXPECT_EQ(shape_of(edited.bytes, 5).weights, shape_of(file, 5).weights);
  EXPECT_EQ(shape_of(edited.bytes, 5).bone_indices, shape_of(file, 5).bone_indices);
}

TEST(SkinWeights, CopyRefusesASourceThatNamesAUsedBoneTwice) {
  const auto file = skin_file(body_vertices(), armor_vertices({k_one, 0, 0, 0}, {0, 0, 0, 0}), 2, {4, 8}, {7, 9, 8}, 3);
  auto request = request_for(ShapeOp::copy_weights, {5});
  request.source_block = 1;
  const auto edited = edit(file, request);

  EXPECT_TRUE(edited.report.changed.empty());
  ASSERT_EQ(edited.report.skipped.size(), 1U);
  EXPECT_EQ(edited.report.skipped[0], "block 5: the copy source names more than one used bone \"Spine\"");
  EXPECT_EQ(shape_of(edited.bytes, 5).weights, shape_of(file, 5).weights);
  EXPECT_EQ(shape_of(edited.bytes, 5).bone_indices, shape_of(file, 5).bone_indices);
}

// Detects: a copy between two 15000-vertex shapes that must refuse past the 200000000-pair limit instead of running the search.
TEST(SkinWeights, CopyRefusesAPairCountPastTheSearchLimitWithoutChangingTheTarget) {
  const std::vector<Vertex> big(15000, skinned(0, 0, 0, {k_one, 0, 0, 0}, {0, 0, 0, 0}));
  const auto file = skin_file(big, big);
  auto request = request_for(ShapeOp::copy_weights, {5});
  request.source_block = 1;
  const auto edited = edit(file, request);

  EXPECT_TRUE(edited.report.changed.empty());
  ASSERT_EQ(edited.report.skipped.size(), 1U);
  EXPECT_EQ(edited.report.skipped[0],
            "block 5: the copy would compare 225000000 vertex pairs, past the limit of 200000000");
  EXPECT_EQ(shape_of(edited.bytes, 5).weights, shape_of(file, 5).weights);
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

namespace {
struct SkinSlots {
  std::vector<std::uint8_t> bones;
  std::vector<float> weights;
};

using NifHandle = std::unique_ptr<radicle_nif, decltype(&radicle_nif_destroy)>;

NifHandle open_handle(const std::vector<std::byte>& bytes) {
  radicle_nif* handle = nullptr;
  EXPECT_EQ(radicle_nif_read(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), &handle),
            RADICLE_STATUS_OK);
  return NifHandle(handle, &radicle_nif_destroy);
}

SkinSlots skin_read(const NifHandle& nif, std::uint32_t block) {
  std::uint8_t* bones = nullptr;
  float* weights = nullptr;
  std::size_t count = 0;
  EXPECT_EQ(radicle_nif_skin_read(nif.get(), block, &bones, &weights, &count), RADICLE_STATUS_OK)
      << radicle_last_error();
  SkinSlots slots{std::vector<std::uint8_t>(bones, bones + 4 * count), std::vector<float>(weights, weights + 4 * count)};
  radicle_free_bytes(bones);
  radicle_free_floats(weights);
  return slots;
}

radicle_status skin_write(const NifHandle& nif, std::uint32_t block, const SkinSlots& slots,
                          std::vector<std::byte>& out) {
  std::uint8_t* bytes = nullptr;
  std::size_t size = 0;
  const auto status = radicle_nif_skin_write(nif.get(), block, slots.bones.data(), slots.weights.data(),
                                             slots.bones.size() / 4, &bytes, &size);
  EXPECT_EQ(bytes == nullptr, status != RADICLE_STATUS_OK);
  out.assign(reinterpret_cast<const std::byte*>(bytes), reinterpret_cast<const std::byte*>(bytes) + size);
  radicle_free_bytes(bytes);
  return status;
}

}  // namespace

TEST(SkinCApi, WriteThenReadRoundTripsHalfTiesAndMovesNothingOutsideTheNamedShape) {
  // Detects (invariant): a skin write that rounds a half-precision tie away from zero (the editor re-reads with .NET Half, which rounds to even), drops the new bone indices, or touches the vertex data or the other shape.
  const auto file = skin_file(body_vertices(), armor_vertices({k_one, 0, 0, 0}, {2, 0, 0, 0}));
  const auto nif = open_handle(file);
  auto slots = skin_read(nif, 1);
  ASSERT_EQ(slots.weights.size(), 16U);
  EXPECT_EQ(slots.bones[1], 1);
  EXPECT_FLOAT_EQ(slots.weights[1], 0.25f);

  const float tie = 0.5f + 1.0f / 4096.0f;
  slots.weights[0] = 0.25f;
  slots.weights[1] = 0.75f;
  slots.bones[0] = 1;
  slots.bones[1] = 0;
  slots.weights[4] = tie;
  slots.weights[5] = 0.5f;
  slots.bones[5] = 1;
  std::vector<std::byte> written;
  ASSERT_EQ(skin_write(nif, 1, slots, written), RADICLE_STATUS_OK) << radicle_last_error();

  const auto reread = skin_read(open_handle(written), 1);
  EXPECT_EQ(reread.bones, slots.bones);
  EXPECT_EQ(reread.weights[0], 0.25f);
  EXPECT_EQ(reread.weights[1], 0.75f);
  EXPECT_EQ(reread.weights[4], 0.5f);
  EXPECT_EQ(reread.weights[5], 0.5f);
  for (std::size_t i = 8; i < reread.weights.size(); ++i) EXPECT_EQ(reread.weights[i], slots.weights[i]);

  ASSERT_EQ(written.size(), file.size());
  const auto source = read_nif(file);
  const auto& block = source.blocks()[1];
  for (std::size_t at = 0; at < file.size(); ++at) {
    if (file[at] == written[at]) continue;
    EXPECT_TRUE(at >= block.offset && at < block.offset + block.size) << "byte " << at << " changed outside block 1";
  }
  const auto before = shape_of(file, 1);
  const auto after = shape_of(written, 1);
  EXPECT_EQ(after.verts, before.verts);
  EXPECT_EQ(after.tris, before.tris);
  EXPECT_EQ(after.colors, before.colors);
  EXPECT_EQ(shape_of(written, 5).weights, shape_of(file, 5).weights);
}

TEST(SkinCApi, WriteRefusesWhatTheRecordCannotHoldAndAnUnchangedWriteReturnsTheInput) {
  // Detects (invariant): a refused skin edit that reports INTERNAL_ERROR instead of INVALID_ARGUMENT, still returns bytes, or a same-value write that rewrites the file.
  const auto file = skin_file(body_vertices(), armor_vertices({k_one, 0, 0, 0}, {2, 0, 0, 0}));
  const auto nif = open_handle(file);
  const auto slots = skin_read(nif, 1);
  std::vector<std::byte> out;

  ASSERT_EQ(skin_write(nif, 1, slots, out), RADICLE_STATUS_OK);
  EXPECT_EQ(out, file);

  auto negative = slots;
  negative.weights[0] = -0.5f;
  EXPECT_EQ(skin_write(nif, 1, negative, out), RADICLE_STATUS_INVALID_ARGUMENT);
  auto nan = slots;
  nan.weights[0] = std::numeric_limits<float>::quiet_NaN();
  EXPECT_EQ(skin_write(nif, 1, nan, out), RADICLE_STATUS_INVALID_ARGUMENT);
  auto too_heavy = slots;
  too_heavy.weights[0] = 70000.0f;
  EXPECT_EQ(skin_write(nif, 1, too_heavy, out), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_TRUE(out.empty());
  auto huge = slots;
  huge.weights[0] = 1e6f;
  EXPECT_EQ(skin_write(nif, 1, huge, out), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_TRUE(out.empty());
  auto emptied = slots;
  for (std::size_t k = 4; k < 8; ++k) emptied.weights[k] = 0.0f;
  EXPECT_EQ(skin_write(nif, 1, emptied, out), RADICLE_STATUS_INVALID_ARGUMENT);
  auto short_list = slots;
  short_list.bones.resize(12);
  short_list.weights.resize(12);
  EXPECT_EQ(skin_write(nif, 1, short_list, out), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(skin_write(nif, 0, slots, out), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(radicle_nif_skin_write(nullptr, 1, slots.bones.data(), slots.weights.data(), 4, nullptr, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

TEST(NifSkinWeightsRealData, NormalizedSkinnedShapesSumToOneAfterReread) {
  FO4_REQUIRE_DATA_DIR(dir, "set FO4_DATA_DIR to a Fallout 4 Data folder with its Fallout4 - *.ba2 archives");
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

std::vector<std::byte> plain_tri_bytes() {
  nif_fixture::Writer writer;
  for (const float value : {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f}) {
    writer.f32(value);
  }
  return writer.bytes();
}

std::vector<std::byte> unskinned_file() {
  const std::vector<Block> blocks{
      nif_fixture::node_block("NiNode", 0, Transform{}, {1, 2, 3}),
      nif_fixture::shape_block("BSTriShape", 0, Transform{}, nif_fixture::k_full, 16, -1, -1, -1,
                               plain_tri_bytes(), {0, 1, 2}),
      nif_fixture::node_block("NiNode", 1, Transform{}, {}),
      nif_fixture::node_block("NiNode", 2, Transform{}, {}),
  };
  return nif_fixture::build_nif({"root", "A", "B"}, blocks, {0});
}

fo4::nif::Bone skin_bone(std::string name, std::int32_t block) {
  fo4::nif::Bone bone;
  bone.name = std::move(name);
  bone.block = block;
  return bone;
}

// Invariant: the instance points at the named nodes with vertices and names intact.
TEST(SkinCreate, CreatesAnInstanceBoundToTheNamedNodes) {
  const auto nif = read_nif(unskinned_file());
  const auto created =
      fo4::nif::create_skin(nif, 1, 0, {skin_bone("A", 2), skin_bone("B", 3)});
  const auto check = read_nif(created);
  const auto shape = read_shape(check, 1);
  ASSERT_GE(shape.skin_ref, 0);
  const auto bones = fo4::nif::read_bones(check, static_cast<std::uint32_t>(shape.skin_ref));
  ASSERT_EQ(bones.size(), 2U);
  EXPECT_EQ(bones[0].name, "A");
  EXPECT_EQ(bones[1].name, "B");
  EXPECT_EQ(shape.verts.size(), 9U);
  EXPECT_EQ(shape.tris, (std::vector<std::uint16_t>{0, 1, 2}));
  EXPECT_THROW(fo4::nif::create_skin(check, 1, 0, {skin_bone("A", 2)}), std::invalid_argument);
}

// Regression: create_skin wrote the skin offset into the colour offset field of the vertex descriptor, so a
// coloured shape lost its colour offset and every later edit refused the skinned shape as mis-packed.
TEST(SkinCreate, DescriptorKeepsTheColourOffsetAndStoresTheSkinOffsetInItsOwnField) {
  nif_fixture::Writer vertices;
  for (std::uint32_t v = 0; v < 3; ++v) {
    for (const float value : {static_cast<float>(v), 0.0f, 0.0f, 1.0f}) vertices.f32(value);
    for (const std::uint8_t value : {std::uint8_t{10}, std::uint8_t{20}, std::uint8_t{30}, std::uint8_t{255}}) {
      vertices.u8(static_cast<std::uint8_t>(value + v));
    }
  }
  const std::uint32_t flags = nif_fixture::k_full | (1U << 5);
  const std::vector<Block> blocks{
      nif_fixture::node_block("NiNode", 0, Transform{}, {1, 2}),
      nif_fixture::shape_block("BSTriShape", 0, Transform{}, flags, 20, -1, -1, -1, vertices.bytes(), {0, 1, 2}, 0,
                               std::uint64_t{4} << 24U),
      nif_fixture::node_block("NiNode", 1, Transform{}, {}),
  };
  const auto nif = read_nif(nif_fixture::build_nif({"root", "A"}, blocks, {0}));
  ASSERT_EQ(fo4::nif::vertex_layout_fault(fo4::nif::read_vertex_desc(nif, 1)), "");
  const auto colours = read_shape(nif, 1).colors;

  const auto check = read_nif(fo4::nif::create_skin(nif, 1, 0, {skin_bone("A", 2)}));
  const auto desc = fo4::nif::read_vertex_desc(check, 1);
  EXPECT_EQ(fo4::nif::vertex_layout_fault(desc), "");
  EXPECT_EQ(desc.attribute_offset(fo4::nif::VertexAttribute::Color), 16U);
  EXPECT_EQ(desc.attribute_offset(fo4::nif::VertexAttribute::Skinning), 20U);
  EXPECT_EQ(desc.size_bytes(), 32U);
  EXPECT_EQ(read_shape(check, 1).colors, colours);
}

// Invariant: the shape points at the new dismember block with bones and partitions intact.
TEST(SkinCreate, AttachPartitionsAuthorsADismemberInstance) {
  const auto nif = read_nif(unskinned_file());
  const auto created = fo4::nif::create_skin(nif, 1, 0, {skin_bone("A", 2), skin_bone("B", 3)});
  const auto attached =
      fo4::nif::attach_partitions(read_nif(created), 1, {{8, 32}});
  const auto check = read_nif(attached);
  const auto shape = read_shape(check, 1);
  ASSERT_GE(shape.skin_ref, 0);
  const auto skin =
      fo4::nif::read_dismember_skin(check, static_cast<std::uint32_t>(shape.skin_ref));
  ASSERT_EQ(skin.partitions.size(), 1U);
  EXPECT_EQ(skin.partitions[0].flags, 8);
  EXPECT_EQ(skin.partitions[0].body_part, 32U);
  EXPECT_EQ(skin.bone_names, (std::vector<std::string>{"A", "B"}));
}

// Constraint: reading a plain instance as dismember is a refusal.
TEST(SkinCreate, DismemberReadRefusesAPlainInstance) {
  const auto nif = read_nif(unskinned_file());
  const auto created = fo4::nif::create_skin(nif, 1, 0, {skin_bone("A", 2)});
  const auto check = read_nif(created);
  const auto instance = static_cast<std::uint32_t>(read_shape(check, 1).skin_ref);
  EXPECT_THROW(fo4::nif::read_dismember_skin(check, instance), std::invalid_argument);
}

// Invariant: the adopted bone joins once with its donor bind and the count is exact.
TEST(SkinCreate, AdoptDonorBonesExtendsTheTargetSkin) {
  const std::vector<Block> blocks{
      nif_fixture::node_block("NiNode", 0, Transform{}, {1, 2, 3, 4}),
      nif_fixture::shape_block("BSTriShape", 0, Transform{}, nif_fixture::k_full, 16, -1, -1, -1,
                               plain_tri_bytes(), {0, 1, 2}),
      nif_fixture::shape_block("BSTriShape", 0, Transform{}, nif_fixture::k_full, 16, -1, -1, -1,
                               plain_tri_bytes(), {0, 1, 2}),
      nif_fixture::node_block("NiNode", 1, Transform{}, {}),
      nif_fixture::node_block("NiNode", 2, Transform{}, {}),
  };
  const auto nif = read_nif(nif_fixture::build_nif({"root", "A", "B"}, blocks, {0}));
  const auto first = fo4::nif::create_skin(nif, 1, 0, {skin_bone("A", 3)});
  const auto second = fo4::nif::create_skin(read_nif(first), 2, 0, {skin_bone("A", 3), skin_bone("B", 4)});
  std::uint32_t adopted = 0;
  const auto merged = fo4::nif::adopt_donor_bones(read_nif(second), 1, 2, &adopted);
  EXPECT_EQ(adopted, 1U);
  const auto check = read_nif(merged);
  const auto instance = static_cast<std::uint32_t>(read_shape(check, 1).skin_ref);
  const auto bones = fo4::nif::read_bones(check, instance);
  ASSERT_EQ(bones.size(), 2U);
  EXPECT_EQ(bones[0].name, "A");
  EXPECT_EQ(bones[1].name, "B");
}

// Invariant: the dabbed bone reaches the target weight and every row still sums to one.
TEST(SkinCreate, PaintDabRaisesTheDabbedBoneTowardOne) {
  const auto nif = read_nif(unskinned_file());
  const auto created = fo4::nif::create_skin(nif, 1, 0, {skin_bone("A", 2), skin_bone("B", 3)});
  fo4::nif::PaintDab dab;
  dab.center = {0.0f, 0.0f, 0.0f};
  dab.radius = 100.0f;
  dab.bone = 1;
  dab.weight = 1.0f;
  dab.strength = 1.0f;
  std::uint32_t changed = 0;
  const auto painted = fo4::nif::paint_dab(read_nif(created), 1, dab, &changed);
  EXPECT_EQ(changed, 3U);
  const auto shape = read_shape(read_nif(painted), 1);
  ASSERT_EQ(shape.weights.size(), 12U);
  ASSERT_EQ(shape.bone_indices.size(), 12U);
  for (std::size_t v = 0; v < 3; ++v) {
    EXPECT_FLOAT_EQ(shape.weights[4 * v], 1.0f);
    EXPECT_EQ(shape.bone_indices[4 * v], 1);
    float total = 0.0f;
    for (std::size_t k = 0; k < 4; ++k) total += shape.weights[4 * v + k];
    EXPECT_FLOAT_EQ(total, 1.0f);
  }
}

namespace {

// Invariant: these four vertices mirror across X, so each left vertex has exactly one partner.
std::vector<std::byte> mirrored_skin_file() {
  const std::vector<Vertex> vertices{
      skinned(1, 0, 0, {k_one, 0, 0, 0}, {0, 0, 0, 0}),
      skinned(1, 1, 0, {k_one, 0, 0, 0}, {0, 0, 0, 0}),
      skinned(-1, 0, 0, {k_one, 0, 0, 0}, {1, 1, 1, 1}),
      skinned(-1, 1, 0, {k_one, 0, 0, 0}, {1, 1, 1, 1}),
  };
  const std::vector<Block> blocks{
      nif_fixture::node_block("NiNode", 0, Transform{}, {1}),
      nif_fixture::shape_block("BSTriShape", 1, Transform{}, k_body_flags, k_body_stride, 2, -1, -1,
                               encode(vertices), {0, 1, 2, 0, 2, 3}),
      nif_fixture::skin_instance_block(0, 6, {3, 4, 5}),
      nif_fixture::node_block("NiNode", 2, Transform{}, {}),
      nif_fixture::node_block("NiNode", 3, Transform{}, {}),
      nif_fixture::node_block("NiNode", 4, Transform{}, {}),
      identity_bone_data(3),
  };
  return nif_fixture::build_nif({"root", "body", "LArm", "RArm", "Spine"}, blocks, {0});
}

fo4::nif::SkinEditReport edit_skin(const std::vector<std::byte>& bytes, const fo4::nif::SkinEditRequest& request,
                                   std::vector<std::byte>& out) {
  fo4::nif::SkinEditReport report;
  out = fo4::nif::apply_skin_edit(fo4::nif::read_nif(bytes), request, &report);
  return report;
}

fo4::nif::SkinEditRequest skin_request(fo4::nif::SkinEditKind kind, std::uint32_t block = 1) {
  fo4::nif::SkinEditRequest request;
  request.kind = kind;
  request.block = block;
  return request;
}

}  // namespace

TEST(SkinEdit, InfluencesOfListsOnlyThePositiveSlotsHeaviestFirst) {
  const auto shape = shape_of(skin_only_file(), 1);
  const auto first = fo4::nif::influences_of(shape, 0);
  ASSERT_EQ(first.size(), 2U);
  EXPECT_EQ(first[0].bone, 0);
  EXPECT_NEAR(first[0].weight, 0.5f, k_tolerance);
  EXPECT_EQ(first[1].bone, 1);
  EXPECT_NEAR(first[1].weight, 0.25f, k_tolerance);
  EXPECT_TRUE(fo4::nif::influences_of(shape, 1).size() == 1);
  EXPECT_THROW(fo4::nif::influences_of(shape, 9), std::invalid_argument);
}

TEST(SkinEdit, SetInfluencesRewritesOneVertexAndLeavesTheOthersByteIdentical) {
  const auto file = skin_only_file();
  auto request = skin_request(fo4::nif::SkinEditKind::set_influences);
  request.vertex = 1;
  request.influences = {{1, 0.75f}, {0, 0.25f}};
  std::vector<std::byte> out;
  const auto report = edit_skin(file, request, out);

  const auto before = shape_of(file, 1);
  const auto after = shape_of(out, 1);
  EXPECT_EQ(report.vertices_changed, 1U);
  EXPECT_NEAR(report.max_weight_change, 0.25f, k_tolerance);
  expect_weights(after, 1, {0.75f, 0.25f, 0.0f, 0.0f});
  expect_bones(after, 1, {1, 0, 0, 0});
  for (std::size_t vertex : {0U, 2U}) {
    expect_weights(after, vertex, {before.weights[vertex * 4], before.weights[vertex * 4 + 1],
                                   before.weights[vertex * 4 + 2], before.weights[vertex * 4 + 3]});
    expect_bones(after, vertex, {before.bone_indices[vertex * 4], before.bone_indices[vertex * 4 + 1],
                                 before.bone_indices[vertex * 4 + 2], before.bone_indices[vertex * 4 + 3]});
  }
}

TEST(SkinEdit, SetInfluencesRefusesWhatTheSkinRecordCannotHold) {
  const auto file = skin_only_file();
  auto request = skin_request(fo4::nif::SkinEditKind::set_influences);
  request.vertex = 1;
  const std::vector<std::vector<fo4::nif::Influence>> refused{
      {{0, 0.2f}, {1, 0.2f}, {0, 0.2f}, {1, 0.2f}, {0, 0.2f}},
      {{0, 0.5f}, {0, 0.5f}},
      {{4, 0.5f}},
      {{0, -0.5f}},
      {{}}};
  for (const auto& influences : refused) {
    request.influences = influences;
    std::vector<std::byte> out;
    EXPECT_THROW(edit_skin(file, request, out), std::exception) << "influences " << influences.size();
  }
}

TEST(SkinEdit, InfluencedByListsTheVerticesOneBoneMovesWithTheirBox) {
  const auto shape = shape_of(skin_only_file(), 1);
  const auto spine = fo4::nif::influenced_by(shape, 0);
  EXPECT_EQ(spine.name, "Spine");
  ASSERT_EQ(spine.vertices.size(), 3U);
  EXPECT_EQ(spine.vertices[0], 0U);
  EXPECT_EQ(spine.vertices[1], 1U);
  EXPECT_EQ(spine.vertices[2], 2U);
  EXPECT_NEAR(spine.weight, 2.0f, k_tolerance);
  EXPECT_NEAR(spine.min[0], 0.0f, k_tolerance);
  EXPECT_NEAR(spine.min[1], 0.0f, k_tolerance);
  EXPECT_NEAR(spine.max[0], 1.0f, k_tolerance);
  EXPECT_NEAR(spine.max[1], 1.0f, k_tolerance);

  const auto arm = fo4::nif::influenced_by(shape, 1, 0.2f);
  ASSERT_EQ(arm.vertices.size(), 1U);
  EXPECT_EQ(arm.vertices[0], 0U);
  EXPECT_NEAR(arm.weight, 0.25f, k_tolerance);

  const auto none = fo4::nif::influenced_by(shape, 7);
  EXPECT_FALSE(none.any());
  EXPECT_EQ(none.name, "bone 7");
}

TEST(SkinEdit, CopyBoneMovesTheWeightOntoTheTargetAndKeepsTheSum) {
  const auto file = skin_only_file();
  auto request = skin_request(fo4::nif::SkinEditKind::copy_bone);
  request.from_bone = 0;
  request.to_bone = 1;
  std::vector<std::byte> out;
  const auto report = edit_skin(file, request, out);

  const auto after = shape_of(out, 1);
  EXPECT_EQ(report.vertices_changed, 3U);
  expect_weights(after, 0, {0.75f, 0.0f, 0.0f, 0.0f});
  expect_bones(after, 0, {1, 0, 0, 0});
  expect_weights(after, 1, {1.0f, 0.0f, 0.0f, 0.0f});
  expect_bones(after, 1, {1, 0, 0, 0});
  expect_weights(after, 2, {1.0f, 0.0f, 0.0f, 0.0f});
  expect_bones(after, 2, {1, 0, 0, 0});
  // Constraint: vertex 0 arrives with 0.75 of weight, and moving it onto bone 1 must keep that 0.75, not invent 1.
  float total = 0.0f;
  for (std::size_t slot = 0; slot < 4; ++slot) total += after.weights[slot];
  EXPECT_NEAR(total, 0.75f, k_tolerance);
}

TEST(SkinEdit, CopyBoneCanKeepTheSourceAndRefusesAnImpossiblePair) {
  const auto file = skin_only_file();
  auto request = skin_request(fo4::nif::SkinEditKind::copy_bone);
  request.from_bone = 0;
  request.to_bone = 1;
  request.keep_source = true;
  std::vector<std::byte> out;
  edit_skin(file, request, out);

  const auto after = shape_of(out, 1);
  ASSERT_EQ(after.weights.size(), 12U);
  EXPECT_NEAR(after.weights[0], 0.5f, k_tolerance);
  EXPECT_EQ(after.bone_indices[0], 0);
  EXPECT_NEAR(after.weights[1], 0.75f, k_tolerance);
  EXPECT_EQ(after.bone_indices[1], 1);

  request.keep_source = false;
  request.from_bone = 2;
  EXPECT_THROW(edit_skin(file, request, out), std::invalid_argument);
  request.from_bone = 1;
  request.to_bone = 1;
  EXPECT_THROW(edit_skin(file, request, out), std::invalid_argument);
}

TEST(SkinEdit, MirrorWeightsCopiesThePositiveSideOntoTheNegativeOne) {
  const auto file = mirrored_skin_file();
  auto seed = skin_request(fo4::nif::SkinEditKind::set_influences);
  seed.vertex = 2;
  seed.influences = {{0, 1.0f}};
  std::vector<std::byte> seeded;
  edit_skin(file, seed, seeded);

  auto request = skin_request(fo4::nif::SkinEditKind::mirror_weights);
  request.lane = 0;
  request.from_positive_side = true;
  std::vector<std::byte> out;
  const auto report = edit_skin(seeded, request, out);

  const auto after = shape_of(out, 1);
  EXPECT_EQ(report.mirrored, 2U);
  EXPECT_EQ(report.no_partner_vertex, 0U);
  EXPECT_EQ(report.no_partner_bone, 0U);
  EXPECT_NEAR(report.worst, 0.0f, k_tolerance);
  expect_weights(after, 2, {1.0f, 0.0f, 0.0f, 0.0f});
  expect_bones(after, 2, {1, 0, 0, 0});
  expect_weights(after, 3, {1.0f, 0.0f, 0.0f, 0.0f});
  expect_bones(after, 3, {1, 0, 0, 0});
}

TEST(SkinEdit, MirrorWeightsRefusesAWeightedBoneWithNoOppositeNumber) {
  const auto file = mirrored_skin_file();
  auto set = skin_request(fo4::nif::SkinEditKind::set_influences);
  set.vertex = 0;
  set.influences = {{2, 1.0f}};
  std::vector<std::byte> seeded;
  edit_skin(file, set, seeded);
  // Constraint: split vertex 3 across bones 1 and 2 so the mirror has one real change and no bone centroid crosses the plane.
  auto seed3 = set;
  seed3.vertex = 3;
  seed3.influences = {{1, 0.5f}, {2, 0.5f}};
  std::vector<std::byte> seededBoth;
  edit_skin(seeded, seed3, seededBoth);

  auto request = skin_request(fo4::nif::SkinEditKind::mirror_weights);
  request.lane = 0;
  std::vector<std::byte> out;
  const auto report = edit_skin(seededBoth, request, out);

  const auto after = shape_of(out, 1);
  EXPECT_EQ(report.mirrored, 1U);
  EXPECT_EQ(report.no_partner_bone, 1U);
  // Constraint: vertex 2's only partner carries a bone without an opposite, so it must keep its own weight.
  expect_bones(after, 2, {1, 1, 1, 1});
  expect_bones(after, 3, {1, 0, 0, 0});
  expect_weights(after, 3, {1.0f, 0.0f, 0.0f, 0.0f});
}

TEST(SkinEdit, SkeletonPairsNamesLeftAndRightAtAWordStart) {
  const std::vector<std::string> names{"LArm", "RArm", "Spine", "LHand", "RHand"};
  const auto pairing = fo4::nif::skeleton_pairs(names);
  ASSERT_EQ(pairing.partner.size(), names.size());
  EXPECT_EQ(pairing.partner[0], 1);
  EXPECT_EQ(pairing.partner[1], 0);
  EXPECT_EQ(pairing.partner[2], 2);
  EXPECT_EQ(pairing.partner[3], 4);
  EXPECT_EQ(pairing.pairs, 2U);
  EXPECT_TRUE(pairing.ambiguous.empty());

  const std::vector<std::string> sided{"L_RArm", "R_RArm", "L_LArm"};
  const auto ambiguous = fo4::nif::skeleton_pairs(sided);
  EXPECT_EQ(ambiguous.partner[0], -1);
  ASSERT_FALSE(ambiguous.ambiguous.empty());
  EXPECT_EQ(ambiguous.ambiguous[0], "L_RArm");
  EXPECT_EQ(ambiguous.pairs, 0U);
}

TEST(SkinEdit, SkinEditKindsRoundTripThroughTheirNames) {
  for (const auto kind : {fo4::nif::SkinEditKind::set_influences, fo4::nif::SkinEditKind::copy_bone,
                          fo4::nif::SkinEditKind::mirror_weights}) {
    const auto parsed = fo4::nif::parse_skin_edit_kind(fo4::nif::skin_edit_kind_name(kind));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, kind);
  }
  EXPECT_FALSE(fo4::nif::parse_skin_edit_kind("weld_vertices").has_value());
}

TEST(SkinEdit, AnEditOutsideTheShapeItNamesIsRefusedWithAReason) {
  auto request = skin_request(fo4::nif::SkinEditKind::copy_bone);
  request.from_bone = 0;
  request.to_bone = 1;
  std::vector<std::byte> out;
  // Invariant: full_nif is skinned to one bone, so copy_bone has no second bone to land on.
  EXPECT_THROW(edit_skin(nif_fixture::full_nif(), request, out), std::invalid_argument);
  request.block = 99;
  EXPECT_THROW(edit_skin(skin_only_file(), request, out), std::invalid_argument);
}

namespace {

using fo4::nif::BrushFalloff;
using fo4::nif::BrushMode;
using fo4::nif::Point;
using fo4::nif::VertexWeights;
using fo4::nif::WeightBrush;
using fo4::nif::WeightBrushReport;

VertexWeights vw(std::array<std::uint8_t, 4> bones, std::array<float, 4> weights) {
  VertexWeights record;
  record.bone = bones;
  record.weight = weights;
  return record;
}

float weight_for(const VertexWeights& record, std::uint8_t bone) {
  float total = 0.0f;
  for (std::size_t k = 0; k < 4; ++k) {
    if (record.weight[k] > 0.0f && record.bone[k] == bone) total += record.weight[k];
  }
  return total;
}

float weight_sum(const VertexWeights& record) {
  return record.weight[0] + record.weight[1] + record.weight[2] + record.weight[3];
}

WeightBrush brush_for(BrushMode mode, std::uint32_t bone, float strength,
                      BrushFalloff falloff = BrushFalloff::constant) {
  WeightBrush brush;
  brush.mode = mode;
  brush.bone = bone;
  brush.strength = strength;
  brush.falloff = falloff;
  return brush;
}

WeightBrushReport stroke(std::vector<VertexWeights>& vertices, const std::vector<Point>& positions,
                         const WeightBrush& brush, const std::vector<std::uint16_t>& triangles = {},
                         const std::vector<std::uint8_t>& locked = {}) {
  return fo4::nif::apply_weight_brush(positions, triangles, vertices, brush, locked);
}

const std::vector<Point> k_one_vertex_at_origin{Point{0.0f, 0.0f, 0.0f}};

}  // namespace

TEST(WeightBrush, AddLandsOnTheExactTargetAndScalesTheOtherBones) {
  std::vector<VertexWeights> vertices{vw({0, 1, 0, 0}, {0.5f, 0.5f, 0.0f, 0.0f})};
  const auto report = stroke(vertices, k_one_vertex_at_origin, brush_for(BrushMode::add, 1, 0.3f));
  EXPECT_EQ(report.in_reach, 1U);
  EXPECT_EQ(report.changed, 1U);
  EXPECT_NEAR(weight_for(vertices[0], 1), 0.8f, 1e-5f);
  EXPECT_NEAR(weight_for(vertices[0], 0), 0.2f, 1e-5f);
  EXPECT_NEAR(weight_sum(vertices[0]), 1.0f, 1e-5f);
}

TEST(WeightBrush, SubtractLowersTheBoneAndHandsTheWeightToTheOthers) {
  std::vector<VertexWeights> vertices{vw({0, 1, 0, 0}, {0.6f, 0.4f, 0.0f, 0.0f})};
  stroke(vertices, k_one_vertex_at_origin, brush_for(BrushMode::subtract, 1, 0.25f));
  EXPECT_NEAR(weight_for(vertices[0], 1), 0.15f, 1e-5f);
  EXPECT_NEAR(weight_for(vertices[0], 0), 0.85f, 1e-5f);
}

TEST(WeightBrush, SubtractOnTheSoleInfluenceSkipsTheVertexInsteadOfEmptyingIt) {
  std::vector<VertexWeights> vertices{vw({0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f})};
  const auto report = stroke(vertices, k_one_vertex_at_origin, brush_for(BrushMode::subtract, 0, 1.0f));
  EXPECT_EQ(report.skipped, 1U);
  EXPECT_EQ(report.changed, 0U);
  EXPECT_FLOAT_EQ(weight_for(vertices[0], 0), 1.0f);
}

TEST(WeightBrush, ReplaceMovesTheBoneTowardTheTargetByTheEffect) {
  std::vector<VertexWeights> vertices{vw({0, 1, 0, 0}, {0.2f, 0.8f, 0.0f, 0.0f})};
  stroke(vertices, k_one_vertex_at_origin, brush_for(BrushMode::replace, 0, 0.5f));
  EXPECT_NEAR(weight_for(vertices[0], 0), 0.6f, 1e-5f);
  EXPECT_NEAR(weight_for(vertices[0], 1), 0.4f, 1e-5f);
}

TEST(WeightBrush, OnlyVerticesStrictlyInsideTheRadiusChangeAndTheFalloffScalesThem) {
  const auto sole = vw({0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f});
  std::vector<VertexWeights> vertices{sole, sole, sole, sole};
  const std::vector<Point> positions{Point{0.0f, 0.0f, 0.0f}, Point{0.5f, 0.0f, 0.0f}, Point{1.0f, 0.0f, 0.0f},
                                     Point{2.0f, 0.0f, 0.0f}};
  const auto report = stroke(vertices, positions, brush_for(BrushMode::add, 1, 1.0f, BrushFalloff::smoothstep));
  EXPECT_EQ(report.in_reach, 2U);
  EXPECT_EQ(report.changed, 2U);
  EXPECT_NEAR(weight_for(vertices[0], 1), 1.0f, 1e-5f);
  EXPECT_NEAR(weight_for(vertices[1], 1), 0.5f, 1e-5f);
  EXPECT_NEAR(weight_for(vertices[1], 0), 0.5f, 1e-5f);
  EXPECT_FLOAT_EQ(weight_for(vertices[2], 0), 1.0f);
  EXPECT_FLOAT_EQ(weight_for(vertices[3], 0), 1.0f);
}

TEST(WeightBrush, TheThreeFalloffCurvesDifferAtAQuarterOfTheRadius) {
  const std::vector<Point> positions{Point{0.25f, 0.0f, 0.0f}};
  const std::array<std::pair<BrushFalloff, float>, 3> expected{{{BrushFalloff::smoothstep, 0.84375f},
                                                                 {BrushFalloff::linear, 0.75f},
                                                                 {BrushFalloff::constant, 1.0f}}};
  for (const auto& entry : expected) {
    std::vector<VertexWeights> vertices{vw({0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f})};
    stroke(vertices, positions, brush_for(BrushMode::add, 1, 1.0f, entry.first));
    EXPECT_NEAR(weight_for(vertices[0], 1), entry.second, 1e-5f) << static_cast<int>(entry.first);
  }
}

TEST(WeightBrush, LockedBonesKeepTheirExactWeightAndOnlyTheRestRescales) {
  std::vector<VertexWeights> vertices{vw({0, 1, 2, 0}, {0.5f, 0.3f, 0.2f, 0.0f})};
  stroke(vertices, k_one_vertex_at_origin, brush_for(BrushMode::add, 2, 0.1f), {}, {0});
  EXPECT_EQ(weight_for(vertices[0], 0), 0.5f);
  EXPECT_NEAR(weight_for(vertices[0], 2), 0.3f, 1e-5f);
  EXPECT_NEAR(weight_for(vertices[0], 1), 0.2f, 1e-5f);
  EXPECT_NEAR(weight_sum(vertices[0]), 1.0f, 1e-5f);
}

TEST(WeightBrush, PaintingALockedBoneChangesNothing) {
  std::vector<VertexWeights> vertices{vw({0, 1, 0, 0}, {0.5f, 0.5f, 0.0f, 0.0f})};
  const auto report = stroke(vertices, k_one_vertex_at_origin, brush_for(BrushMode::add, 1, 1.0f), {}, {1});
  EXPECT_EQ(report.skipped, 1U);
  EXPECT_EQ(report.changed, 0U);
  EXPECT_FLOAT_EQ(weight_for(vertices[0], 1), 0.5f);
}

TEST(WeightBrush, AFifthBoneEntersOnlyByDisplacingTheSmallestAndTheResultStaysNormalized) {
  const auto four = vw({0, 1, 2, 3}, {0.4f, 0.3f, 0.2f, 0.1f});
  std::vector<VertexWeights> faint{four};
  const auto faint_report = stroke(faint, k_one_vertex_at_origin, brush_for(BrushMode::add, 4, 0.05f));
  EXPECT_EQ(faint_report.in_reach, 1U);
  EXPECT_EQ(faint_report.changed, 0U);
  EXPECT_EQ(faint[0].bone, four.bone);
  EXPECT_EQ(faint[0].weight, four.weight);

  std::vector<VertexWeights> strong{four};
  const auto report = stroke(strong, k_one_vertex_at_origin, brush_for(BrushMode::add, 4, 0.15f));
  EXPECT_EQ(report.changed, 1U);
  EXPECT_FLOAT_EQ(weight_for(strong[0], 3), 0.0f);
  EXPECT_NEAR(weight_for(strong[0], 4), 0.15f / 0.915f, 1e-4f);
  EXPECT_NEAR(weight_for(strong[0], 0), 0.34f / 0.915f, 1e-4f);
  EXPECT_NEAR(weight_sum(strong[0]), 1.0f, 1e-5f);
}

TEST(WeightBrush, SmoothReadsASnapshotSoVertexOrderDoesNotMatter) {
  std::vector<VertexWeights> vertices{vw({0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
                                      vw({1, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
                                      vw({1, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f})};
  const std::vector<Point> positions{Point{0.0f, 0.0f, 0.0f}, Point{0.1f, 0.0f, 0.0f}, Point{0.0f, 0.1f, 0.0f}};
  const auto report = stroke(vertices, positions, brush_for(BrushMode::smooth, 0, 1.0f), {0, 1, 2});
  EXPECT_EQ(report.changed, 3U);
  EXPECT_NEAR(weight_for(vertices[0], 1), 1.0f, 1e-5f);
  EXPECT_NEAR(weight_for(vertices[0], 0), 0.0f, 1e-5f);
  EXPECT_NEAR(weight_for(vertices[1], 0), 0.5f, 1e-5f);
  EXPECT_NEAR(weight_for(vertices[1], 1), 0.5f, 1e-5f);
  EXPECT_NEAR(weight_for(vertices[2], 0), 0.5f, 1e-5f);
  EXPECT_NEAR(weight_for(vertices[2], 1), 0.5f, 1e-5f);
}

TEST(WeightBrush, SmoothKeepsALockedBoneExactAndBlendsTheRest) {
  std::vector<VertexWeights> vertices{vw({0, 1, 0, 0}, {0.4f, 0.6f, 0.0f, 0.0f}),
                                      vw({2, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
                                      vw({2, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f})};
  const std::vector<Point> positions{Point{0.0f, 0.0f, 0.0f}, Point{10.0f, 0.0f, 0.0f}, Point{0.0f, 10.0f, 0.0f}};
  const auto report = stroke(vertices, positions, brush_for(BrushMode::smooth, 0, 1.0f), {0, 1, 2}, {0});
  EXPECT_EQ(report.in_reach, 1U);
  EXPECT_EQ(weight_for(vertices[0], 0), 0.4f);
  EXPECT_NEAR(weight_for(vertices[0], 2), 0.6f, 1e-5f);
  EXPECT_FLOAT_EQ(weight_for(vertices[0], 1), 0.0f);
}

TEST(WeightBrush, SmoothSkipsAVertexWithNoNeighbour) {
  std::vector<VertexWeights> vertices{vw({0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
                                      vw({1, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f})};
  const std::vector<Point> positions{Point{0.0f, 0.0f, 0.0f}, Point{0.1f, 0.0f, 0.0f}};
  const auto report = stroke(vertices, positions, brush_for(BrushMode::smooth, 0, 1.0f));
  EXPECT_EQ(report.skipped, 2U);
  EXPECT_EQ(report.changed, 0U);
}

TEST(WeightBrush, PackKeepsLockedBonesFirstAndBreaksWeightTiesByLowestBone) {
  const std::vector<fo4::nif::Influence> wide{{5, 0.2f}, {3, 0.2f}, {9, 0.2f}, {7, 0.2f}, {1, 0.2f}};
  const auto packed = fo4::nif::pack_influences(wide, std::vector<std::uint8_t>{9});
  EXPECT_EQ(packed.bone, (std::array<std::uint8_t, 4>{1, 3, 5, 9}));
  EXPECT_NEAR(packed.weight[0], 0.8f / 3.0f, 1e-5f);
  EXPECT_NEAR(packed.weight[2], 0.8f / 3.0f, 1e-5f);
  EXPECT_NEAR(packed.weight[3], 0.2f, 1e-6f);
  EXPECT_NEAR(weight_sum(packed), 1.0f, 1e-5f);
  EXPECT_THROW(fo4::nif::pack_influences(std::vector<fo4::nif::Influence>{{256, 1.0f}}), std::invalid_argument);
  EXPECT_THROW(fo4::nif::pack_influences(std::vector<fo4::nif::Influence>{{1, -0.5f}}), std::invalid_argument);
}

TEST(WeightBrush, AMalformedStrokeIsRefusedBeforeAnyVertexChanges) {
  const auto sole = vw({0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f});
  std::vector<VertexWeights> vertices{sole};
  auto brush = brush_for(BrushMode::add, 1, 1.0f);
  brush.radius = 0.0f;
  EXPECT_THROW(stroke(vertices, k_one_vertex_at_origin, brush), std::invalid_argument);
  brush = brush_for(BrushMode::add, 1, -1.0f);
  EXPECT_THROW(stroke(vertices, k_one_vertex_at_origin, brush), std::invalid_argument);
  EXPECT_THROW(stroke(vertices, k_one_vertex_at_origin, brush_for(BrushMode::add, 256, 1.0f)),
               std::invalid_argument);
  brush = brush_for(BrushMode::replace, 1, 1.0f);
  brush.target = 2.0f;
  EXPECT_THROW(stroke(vertices, k_one_vertex_at_origin, brush), std::invalid_argument);
  EXPECT_THROW(stroke(vertices, std::vector<Point>{}, brush_for(BrushMode::add, 1, 1.0f)), std::invalid_argument);
  EXPECT_THROW(stroke(vertices, k_one_vertex_at_origin, brush_for(BrushMode::smooth, 0, 1.0f), {0, 1, 5}),
               std::invalid_argument);
  std::vector<VertexWeights> negative{vw({0, 0, 0, 0}, {-0.5f, 0.0f, 0.0f, 0.0f})};
  EXPECT_THROW(stroke(negative, k_one_vertex_at_origin, brush_for(BrushMode::add, 1, 1.0f)),
               std::invalid_argument);
  EXPECT_EQ(vertices[0].weight, sole.weight);
}

TEST(WeightBrush, TheShapeBrushComparesInGlobalSpaceAndLeavesEveryOtherVertexAlone) {
  const auto file = skin_file(body_vertices(), armor_vertices({k_one, 0, 0, 0}, {0, 0, 0, 0}));
  WeightBrush brush = brush_for(BrushMode::add, 1, 1.0f);
  brush.center = {1.0f, 0.0f, 0.0f};
  brush.radius = 0.5f;
  WeightBrushReport report;
  const auto edited = fo4::nif::brush_shape_weights(read_nif(file), 5, brush, {}, &report);

  EXPECT_EQ(report.changed, 1U);
  const auto armor = shape_of(edited, 5);
  expect_weights(armor, 0, {1.0f, 0.0f, 0.0f, 0.0f});
  expect_bones(armor, 0, {1, 0, 0, 0});
  for (std::size_t v = 1; v < 4; ++v) {
    expect_weights(armor, v, {1.0f, 0.0f, 0.0f, 0.0f});
    expect_bones(armor, v, {2, 0, 0, 0});
  }
  EXPECT_EQ(shape_of(edited, 1).weights, shape_of(file, 1).weights);
  EXPECT_EQ(shape_of(edited, 1).bone_indices, shape_of(file, 1).bone_indices);
}

TEST(WeightBrush, AStrokeThatChangesNothingReturnsTheInputBytes) {
  const auto file = skin_file(body_vertices(), armor_vertices({k_one, 0, 0, 0}, {0, 0, 0, 0}));
  WeightBrush distant = brush_for(BrushMode::add, 1, 1.0f);
  distant.center = {100.0f, 0.0f, 0.0f};
  EXPECT_EQ(fo4::nif::brush_shape_weights(read_nif(file), 5, distant), file);

  WeightBrush subtract = brush_for(BrushMode::subtract, 0, 1.0f);
  subtract.center = {1.0f, 0.0f, 0.0f};
  subtract.radius = 0.5f;
  WeightBrushReport report;
  EXPECT_EQ(fo4::nif::brush_shape_weights(read_nif(file), 5, subtract, {}, &report), file);
  EXPECT_EQ(report.skipped, 1U);
}

namespace {

using fo4::nif::ConformMode;
using fo4::nif::ConformOptions;
using fo4::nif::TransferMethod;
using fo4::nif::WeightTransferOptions;

const std::vector<Point> k_plane{Point{-10.0f, -10.0f, 0.0f}, Point{10.0f, -10.0f, 0.0f}, Point{0.0f, 10.0f, 0.0f}};
const std::vector<std::uint16_t> k_plane_triangle{0, 1, 2};
const std::vector<Point> k_corner{Point{0.0f, 0.0f, 0.0f}, Point{3.0f, 0.0f, 0.0f}, Point{0.0f, 3.0f, 0.0f}};

std::vector<VertexWeights> corner_weights() {
  return {vw({0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}), vw({1, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
          vw({2, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f})};
}

}  // namespace

TEST(WeightTransfer, ProjectionFindsTheClosestPointItsBarycentricsAndTheSignedDistance) {
  const std::vector<Point> triangle{Point{0.0f, 0.0f, 0.0f}, Point{2.0f, 0.0f, 0.0f}, Point{0.0f, 2.0f, 0.0f}};
  const std::vector<Point> targets{Point{0.5f, 0.5f, 1.0f}, Point{0.5f, 0.5f, -1.0f}, Point{3.0f, 0.0f, 0.0f},
                                   Point{1.0f, -1.0f, 0.0f}};
  const auto hits = fo4::nif::project_onto_surface(targets, triangle, std::vector<std::uint16_t>{0, 1, 2});
  ASSERT_EQ(hits.size(), 4U);
  EXPECT_EQ(hits[0].triangle, 0);
  EXPECT_NEAR(hits[0].bary[0], 0.5f, 1e-5f);
  EXPECT_NEAR(hits[0].bary[1], 0.25f, 1e-5f);
  EXPECT_NEAR(hits[0].bary[2], 0.25f, 1e-5f);
  EXPECT_NEAR(hits[0].distance, 1.0f, 1e-5f);
  EXPECT_NEAR(hits[0].normal[2], 1.0f, 1e-5f);
  EXPECT_NEAR(hits[0].signed_distance, 1.0f, 1e-5f);
  EXPECT_NEAR(hits[1].signed_distance, -1.0f, 1e-5f);
  EXPECT_NEAR(hits[2].bary[1], 1.0f, 1e-5f);
  EXPECT_NEAR(hits[2].distance, 1.0f, 1e-5f);
  EXPECT_NEAR(hits[3].bary[0], 0.5f, 1e-5f);
  EXPECT_NEAR(hits[3].bary[1], 0.5f, 1e-5f);
  EXPECT_NEAR(hits[3].point[0], 1.0f, 1e-5f);

  const auto limited =
      fo4::nif::project_onto_surface(targets, triangle, std::vector<std::uint16_t>{0, 1, 2}, 0.5f);
  EXPECT_EQ(limited[0].triangle, -1);
}

TEST(WeightTransfer, BarycentricInterpolatesInsideATriangleWhereNearestVertexTakesOneCorner) {
  const std::vector<Point> target{Point{1.0f, 0.5f, 0.0f}};
  const std::vector<std::int32_t> map{3, 4, 5};
  std::vector<VertexWeights> weights{vw({9, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f})};
  WeightTransferOptions options;
  auto report = fo4::nif::transfer_weights(target, weights, k_corner, std::vector<std::uint16_t>{0, 1, 2},
                                           corner_weights(), map, options);
  EXPECT_EQ(report.transferred, 1U);
  EXPECT_EQ(weights[0].bone, (std::array<std::uint8_t, 4>{3, 4, 5, 0}));
  EXPECT_NEAR(weights[0].weight[0], 0.5f, 1e-5f);
  EXPECT_NEAR(weights[0].weight[1], 1.0f / 3.0f, 1e-5f);
  EXPECT_NEAR(weights[0].weight[2], 1.0f / 6.0f, 1e-5f);

  weights = {vw({9, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f})};
  options.method = TransferMethod::nearest_vertex;
  report = fo4::nif::transfer_weights(target, weights, k_corner, {}, corner_weights(), map, options);
  EXPECT_EQ(report.transferred, 1U);
  EXPECT_EQ(weights[0].bone[0], 3);
  EXPECT_FLOAT_EQ(weights[0].weight[0], 1.0f);
  EXPECT_FLOAT_EQ(weights[0].weight[1], 0.0f);
}

TEST(WeightTransfer, UnmappedBonesAreDroppedAndTheRestRenormalized) {
  const std::vector<Point> target{Point{1.0f, 0.5f, 0.0f}};
  std::vector<VertexWeights> weights{vw({9, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f})};
  const std::vector<std::int32_t> map{0, -1, 2};
  auto report = fo4::nif::transfer_weights(target, weights, k_corner, std::vector<std::uint16_t>{0, 1, 2},
                                           corner_weights(), map, WeightTransferOptions{});
  EXPECT_EQ(report.transferred, 1U);
  EXPECT_NEAR(weight_for(weights[0], 0), 0.75f, 1e-5f);
  EXPECT_NEAR(weight_for(weights[0], 2), 0.25f, 1e-5f);

  weights = {vw({9, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f})};
  const std::vector<std::int32_t> none{-1, -1, -1};
  report = fo4::nif::transfer_weights(target, weights, k_corner, std::vector<std::uint16_t>{0, 1, 2},
                                      corner_weights(), none, WeightTransferOptions{});
  EXPECT_EQ(report.unmapped, 1U);
  EXPECT_EQ(report.transferred, 0U);
  EXPECT_FLOAT_EQ(weight_for(weights[0], 9), 1.0f);
}

TEST(WeightTransfer, AVertexBeyondTheMaximumDistanceKeepsItsOwnWeights) {
  const std::vector<Point> target{Point{1.0f, 0.5f, 1.0f}};
  const std::vector<std::int32_t> map{0, 1, 2};
  WeightTransferOptions options;
  options.max_distance = 0.5f;
  std::vector<VertexWeights> weights{vw({9, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f})};
  auto report = fo4::nif::transfer_weights(target, weights, k_corner, std::vector<std::uint16_t>{0, 1, 2},
                                           corner_weights(), map, options);
  EXPECT_EQ(report.out_of_reach, 1U);
  EXPECT_FLOAT_EQ(weight_for(weights[0], 9), 1.0f);
  options.method = TransferMethod::nearest_vertex;
  report = fo4::nif::transfer_weights(target, weights, k_corner, {}, corner_weights(), map, options);
  EXPECT_EQ(report.out_of_reach, 1U);
  EXPECT_FLOAT_EQ(weight_for(weights[0], 9), 1.0f);
}

TEST(WeightTransfer, ConformSnapPlacesEveryVertexAtTheOffsetAndPushOutOnlyLiftsThoseBelowIt) {
  const std::vector<Point> start{Point{0.0f, 0.0f, 2.0f}, Point{0.0f, 1.0f, -1.0f}};
  ConformOptions options;
  options.offset = 0.5f;

  auto pushed = start;
  options.mode = ConformMode::push_out;
  auto report = fo4::nif::conform_to_surface(pushed, k_plane, k_plane_triangle, options);
  EXPECT_EQ(report.moved, 1U);
  EXPECT_NEAR(report.max_move, 1.5f, 1e-5f);
  EXPECT_EQ(pushed[0], start[0]);
  EXPECT_NEAR(pushed[1][2], 0.5f, 1e-5f);
  EXPECT_NEAR(pushed[1][1], 1.0f, 1e-5f);

  auto snapped = start;
  options.mode = ConformMode::snap;
  report = fo4::nif::conform_to_surface(snapped, k_plane, k_plane_triangle, options);
  EXPECT_EQ(report.moved, 2U);
  EXPECT_NEAR(snapped[0][2], 0.5f, 1e-5f);
  EXPECT_NEAR(snapped[1][2], 0.5f, 1e-5f);

  auto limited = start;
  options.max_distance = 1.5f;
  report = fo4::nif::conform_to_surface(limited, k_plane, k_plane_triangle, options);
  EXPECT_EQ(report.out_of_reach, 1U);
  EXPECT_EQ(limited[0], start[0]);
}

TEST(WeightTransfer, MalformedProjectionInputIsRefused) {
  const std::vector<Point> target{Point{0.0f, 0.0f, 0.0f}};
  EXPECT_THROW(fo4::nif::project_onto_surface(target, k_plane, std::vector<std::uint16_t>{}), std::invalid_argument);
  EXPECT_THROW(fo4::nif::project_onto_surface(target, k_plane, std::vector<std::uint16_t>{0, 1}),
               std::invalid_argument);
  EXPECT_THROW(fo4::nif::project_onto_surface(target, k_plane, std::vector<std::uint16_t>{0, 1, 7}),
               std::invalid_argument);
  EXPECT_THROW(fo4::nif::project_onto_surface(target, k_plane, k_plane_triangle, -1.0f), std::invalid_argument);
  const std::vector<Point> nan_target{Point{std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f}};
  EXPECT_THROW(fo4::nif::project_onto_surface(nan_target, k_plane, k_plane_triangle), std::invalid_argument);
  std::vector<VertexWeights> none;
  EXPECT_THROW(fo4::nif::transfer_weights(target, none, k_corner, k_plane_triangle, corner_weights(),
                                          std::vector<std::int32_t>{0, 1, 2}, WeightTransferOptions{}),
               std::invalid_argument);
}

TEST(WeightTransfer, BonesMapByNameOnlyWhenTheTargetNamesThemExactlyOnce) {
  const std::vector<std::string> source{"A", "B", "C", ""};
  const std::vector<std::string> target{"B", "A", "B", "C"};
  EXPECT_EQ(fo4::nif::map_bones_by_name(source, target), (std::vector<std::int32_t>{1, -1, 3, -1}));
}

TEST(WeightTransfer, ShapeTransferProjectsTheBodySkinOntoTheArmorByBoneName) {
  const auto file = skin_file(body_vertices(), armor_vertices({k_one, 0, 0, 0}, {0, 0, 0, 0}));
  fo4::nif::WeightTransferReport report;
  const auto edited = fo4::nif::transfer_shape_weights(read_nif(file), 1, 5, WeightTransferOptions{}, &report);

  EXPECT_EQ(report.transferred, 4U);
  EXPECT_EQ(report.out_of_reach, 0U);
  const auto armor = shape_of(edited, 5);
  expect_weights(armor, 0, {1.0f, 0.0f, 0.0f, 0.0f});
  expect_bones(armor, 0, {1, 0, 0, 0});
  expect_weights(armor, 1, {1.0f, 0.0f, 0.0f, 0.0f});
  expect_bones(armor, 1, {1, 0, 0, 0});
  for (const std::size_t v : {std::size_t{2}, std::size_t{3}}) {
    expect_weights(armor, v, {0.625f, 0.375f, 0.0f, 0.0f});
    expect_bones(armor, v, {2, 1, 0, 0});
  }
  EXPECT_EQ(shape_of(edited, 1).weights, shape_of(file, 1).weights);
}

TEST(WeightTransfer, ShapeTransferRefusesASourceBoneTheTargetLacksAndASourceThatIsTheTarget) {
  const auto file = skin_file(body_vertices(), armor_vertices({k_one, 0, 0, 0}, {0, 0, 0, 0}));
  try {
    fo4::nif::transfer_shape_weights(read_nif(file), 5, 1, WeightTransferOptions{});
    FAIL() << "the body skin has no Head bone";
  } catch (const std::runtime_error& error) {
    EXPECT_NE(std::string(error.what()).find("Head"), std::string::npos);
  }
  EXPECT_THROW(fo4::nif::transfer_shape_weights(read_nif(file), 5, 5, WeightTransferOptions{}),
               std::invalid_argument);
}

namespace {

using fo4::nif::CustomizationRemap;
using fo4::nif::RemapNewBone;
using fo4::nif::RemapRecord;

void put_i32(std::vector<std::byte>& bytes, std::size_t at, std::int32_t value) {
  for (std::size_t k = 0; k < 4; ++k) {
    bytes[at + k] = static_cast<std::byte>((static_cast<std::uint32_t>(value) >> (8U * k)) & 0xFFU);
  }
}

Block shape_with_extras(std::int32_t name, const std::vector<std::int32_t>& refs) {
  auto block = nif_fixture::shape_block("BSTriShape", name, Transform{}, nif_fixture::k_full, 16, -1, -1, -1,
                                        plain_tri_bytes(), {0, 1, 2}, static_cast<std::uint32_t>(refs.size()));
  for (std::size_t i = 0; i < refs.size(); ++i) put_i32(block.data, 8 + 4 * i, refs[i]);
  return block;
}

Block binary_extra_block(std::int32_t name, const std::vector<std::byte>& payload) {
  nif_fixture::Writer writer;
  writer.i32(name);
  writer.u32(static_cast<std::uint32_t>(payload.size()));
  writer.append(payload);
  return Block{"NiBinaryExtraData", writer.bytes()};
}

std::vector<std::byte> remap_payload(std::uint16_t first_weight, std::uint8_t first_bone, std::size_t records = 3) {
  nif_fixture::Writer writer;
  for (std::size_t v = 0; v < records; ++v) {
    writer.u16(first_weight);
    writer.u16(first_weight == k_one ? std::uint16_t{0} : k_half);
    writer.u16(0);
    writer.u16(0);
    writer.u8(first_bone);
    writer.u8(static_cast<std::uint8_t>(first_bone + 1));
    writer.u8(0);
    writer.u8(0);
  }
  return writer.bytes();
}

std::vector<std::byte> new_bone_payload(std::size_t entries = 1) {
  nif_fixture::Writer writer;
  for (std::size_t e = 0; e < entries; ++e) {
    writer.text("Neck");
    for (std::size_t i = 4; i < 0x90; ++i) writer.u8(i < 16 ? std::uint8_t{0} : static_cast<std::uint8_t>(i * 7 + e));
    const float matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 8.5f, -2.0f, 3.0f, 1};
    for (const float value : matrix) writer.f32(value);
  }
  return writer.bytes();
}

std::vector<std::byte> remap_file(const std::vector<std::byte>& remap, const std::vector<std::byte>& new_bones) {
  const std::vector<Block> blocks{
      nif_fixture::node_block("NiNode", 0, Transform{}, {1}),
      shape_with_extras(0, {2, 3}),
      binary_extra_block(1, remap),
      binary_extra_block(2, new_bones),
  };
  return nif_fixture::build_nif({"root", "CustomizationRemapData", "CustomizationRemapNewBonesData"}, blocks, {0});
}

}  // namespace

TEST(CustomizationRemap, ReadsTheRecordsTheNewBonesAndTheirTypedViews) {
  const auto nif = read_nif(remap_file(remap_payload(k_half, 1), new_bone_payload(2)));
  const auto remap = fo4::nif::read_customization_remap(nif, 1);
  EXPECT_EQ(remap.remap_block, 2);
  EXPECT_EQ(remap.new_bones_block, 3);
  ASSERT_EQ(remap.records.size(), 3U);
  EXPECT_EQ(remap.records[0].weight, (std::array<float, 4>{0.5f, 0.5f, 0.0f, 0.0f}));
  EXPECT_EQ(remap.records[0].bone, (std::array<std::uint8_t, 4>{1, 2, 0, 0}));
  ASSERT_EQ(remap.new_bones.size(), 2U);
  EXPECT_EQ(fo4::nif::remap_new_bone_name(remap.new_bones[0]), "Neck");
  const auto matrix = fo4::nif::remap_new_bone_matrix(remap.new_bones[1]);
  EXPECT_EQ(matrix[0], 1.0f);
  EXPECT_EQ(matrix[12], 8.5f);
  EXPECT_EQ(matrix[13], -2.0f);
  EXPECT_EQ(matrix[15], 1.0f);
}

TEST(CustomizationRemap, AnUntouchedRoundTripIsByteExactAndAnEditChangesOnlyItsOwnBytes) {
  const auto file = remap_file(remap_payload(k_half, 1), new_bone_payload());
  const auto nif = read_nif(file);
  auto remap = fo4::nif::read_customization_remap(nif, 1);
  EXPECT_EQ(fo4::nif::write_customization_remap(nif, 1, remap), file);

  remap.records[2].weight = {0.25f, 0.75f, 0.0f, 0.0f};
  remap.records[2].bone = {7, 8, 0, 0};
  auto matrix = fo4::nif::remap_new_bone_matrix(remap.new_bones[0]);
  matrix[12] = 1.5f;
  fo4::nif::set_remap_new_bone_matrix(remap.new_bones[0], matrix);
  const auto edited = fo4::nif::write_customization_remap(nif, 1, remap);
  const auto again = fo4::nif::read_customization_remap(read_nif(edited), 1);
  EXPECT_EQ(again.records[2].weight, remap.records[2].weight);
  EXPECT_EQ(again.records[2].bone, remap.records[2].bone);
  EXPECT_EQ(again.records[0].weight, (std::array<float, 4>{0.5f, 0.5f, 0.0f, 0.0f}));
  EXPECT_EQ(again.new_bones[0].raw, remap.new_bones[0].raw);
  EXPECT_EQ(edited.size(), file.size());
}

TEST(CustomizationRemap, TheReaderRefusesStructuralFaultsAndTheValidatorReportsTheRest) {
  const auto good_bones = new_bone_payload();
  auto odd = remap_payload(k_half, 1);
  odd.push_back(std::byte{0});
  EXPECT_THROW(fo4::nif::read_customization_remap(read_nif(remap_file(odd, good_bones)), 1), fo4::parse_error);
  auto short_bone = new_bone_payload();
  short_bone.resize(100);
  EXPECT_THROW(fo4::nif::read_customization_remap(read_nif(remap_file(remap_payload(k_half, 1), short_bone)), 1),
               fo4::parse_error);

  const auto remap = fo4::nif::read_customization_remap(read_nif(remap_file(remap_payload(k_half, 5), good_bones)), 1);
  EXPECT_TRUE(fo4::nif::validate_customization_remap(remap, 7, 3).empty());
  const auto problems = fo4::nif::validate_customization_remap(remap, 2, 3);
  ASSERT_FALSE(problems.empty());
  EXPECT_NE(problems[0].find("past 3 bones"), std::string::npos);

  CustomizationRemap light;
  light.records.push_back(RemapRecord{{0.25f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0}});
  const auto sums = fo4::nif::validate_customization_remap(light, 1, 1);
  ASSERT_EQ(sums.size(), 1U);
  EXPECT_NE(sums[0].find("sum"), std::string::npos);
}

TEST(CustomizationRemap, EachShapeOwnsTheBlocksItsOwnExtraDataListNames) {
  const std::vector<Block> blocks{
      nif_fixture::node_block("NiNode", 0, Transform{}, {1, 2}),
      shape_with_extras(0, {4, 3}),
      shape_with_extras(0, {6, 5}),
      binary_extra_block(2, new_bone_payload()),
      binary_extra_block(1, remap_payload(k_one, 0)),
      binary_extra_block(2, new_bone_payload(2)),
      binary_extra_block(1, remap_payload(k_one, 1)),
  };
  const auto nif = read_nif(nif_fixture::build_nif(
      {"root", "CustomizationRemapData", "CustomizationRemapNewBonesData"}, blocks, {0}));
  const auto first = fo4::nif::read_customization_remap(nif, 1);
  EXPECT_EQ(first.remap_block, 4);
  EXPECT_EQ(first.new_bones_block, 3);
  EXPECT_EQ(first.new_bones.size(), 1U);
  EXPECT_EQ(first.records[0].bone[0], 0);
  const auto second = fo4::nif::read_customization_remap(nif, 2);
  EXPECT_EQ(second.remap_block, 6);
  EXPECT_EQ(second.new_bones_block, 5);
  EXPECT_EQ(second.new_bones.size(), 2U);
  EXPECT_EQ(second.records[0].bone[0], 1);
  EXPECT_EQ(fo4::nif::read_customization_remap(read_nif(unskinned_file()), 1).remap_block, -1);
}

TEST(CustomizationRemap, TheWriterRefusesWhatItCannotPlace) {
  const auto nif = read_nif(remap_file(remap_payload(k_half, 1), new_bone_payload()));
  auto remap = fo4::nif::read_customization_remap(nif, 1);
  auto short_records = remap;
  short_records.records.pop_back();
  EXPECT_THROW(fo4::nif::write_customization_remap(nif, 1, short_records), std::invalid_argument);
  auto bad_weight = remap;
  bad_weight.records[0].weight[0] = std::numeric_limits<float>::quiet_NaN();
  EXPECT_THROW(fo4::nif::write_customization_remap(nif, 1, bad_weight), std::invalid_argument);

  const auto bare = read_nif(unskinned_file());
  EXPECT_THROW(fo4::nif::write_customization_remap(bare, 1, remap), std::invalid_argument);

  const std::vector<Block> blocks{
      nif_fixture::node_block("NiNode", 0, Transform{}, {1}),
      shape_with_extras(0, {2}),
      binary_extra_block(1, remap_payload(k_half, 1)),
  };
  const auto without_bones = read_nif(nif_fixture::build_nif({"root", "CustomizationRemapData"}, blocks, {0}));
  auto with_new = fo4::nif::read_customization_remap(without_bones, 1);
  with_new.new_bones.emplace_back();
  EXPECT_THROW(fo4::nif::write_customization_remap(without_bones, 1, with_new), std::invalid_argument);
}

TEST(CustomizationRemap, RecordsFollowAVertexReorderRemovalAndSplitByTheirSource) {
  const std::vector<RemapRecord> records{RemapRecord{{1.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0}},
                                         RemapRecord{{1.0f, 0.0f, 0.0f, 0.0f}, {1, 0, 0, 0}},
                                         RemapRecord{{1.0f, 0.0f, 0.0f, 0.0f}, {2, 0, 0, 0}}};
  const auto moved = fo4::nif::reindex_remap_records(records, std::vector<std::int32_t>{2, 0, 0});
  ASSERT_EQ(moved.size(), 3U);
  EXPECT_EQ(moved[0].bone[0], 2);
  EXPECT_EQ(moved[1].bone[0], 0);
  EXPECT_EQ(moved[2].bone[0], 0);
  EXPECT_EQ(fo4::nif::reindex_remap_records(records, std::vector<std::int32_t>{1}).size(), 1U);
  EXPECT_THROW(fo4::nif::reindex_remap_records(records, std::vector<std::int32_t>{0, -1}), std::invalid_argument);
  EXPECT_THROW(fo4::nif::reindex_remap_records(records, std::vector<std::int32_t>{3}), std::invalid_argument);
}

TEST(WeightBrush, SmoothLeavesAVertexWhoseOnlyUnlockedNeighbourWeightWouldVanishAtOne) {
  // regression: a locked variant {0:0.4 locked, 1:0.6} smoothed toward neighbours that carry only the locked bone must not leave the vertex summing to 0.4.
  std::vector<VertexWeights> vertices{vw({0, 1, 0, 0}, {0.4f, 0.6f, 0.0f, 0.0f}),
                                      vw({0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
                                      vw({0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f})};
  const std::vector<Point> positions{Point{0.0f, 0.0f, 0.0f}, Point{10.0f, 0.0f, 0.0f}, Point{0.0f, 10.0f, 0.0f}};
  const auto report = stroke(vertices, positions, brush_for(BrushMode::smooth, 0, 1.0f), {0, 1, 2}, {0});
  EXPECT_EQ(report.in_reach, 1U);
  EXPECT_EQ(report.skipped, 1U);
  EXPECT_EQ(report.changed, 0U);
  EXPECT_EQ(weight_for(vertices[0], 0), 0.4f);
  EXPECT_EQ(weight_for(vertices[0], 1), 0.6f);
  EXPECT_NEAR(weight_sum(vertices[0]), 1.0f, 1e-5f);
}

TEST(WeightBrush, ABrushPositionThatIsNotFiniteIsRefusedBeforeAnyVertexChanges) {
  // regression: a NaN position must be refused like project_onto_surface does, because NaN compares false and silently skips the vertex.
  const auto sole = vw({0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f});
  std::vector<VertexWeights> vertices{sole, sole};
  const std::vector<Point> positions{Point{0.0f, 0.0f, 0.0f},
                                     Point{std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f}};
  EXPECT_THROW(stroke(vertices, positions, brush_for(BrushMode::add, 1, 1.0f)), std::invalid_argument);
  EXPECT_EQ(vertices[0].weight, sole.weight);
}

TEST(WeightTransfer, ABoneMapEntryPastTheLastBoneIsRefusedBeforeTheTargetChanges) {
  // regression: a bone map entry above 255 must be refused up front, not after earlier vertices were already rewritten.
  const std::vector<Point> target{Point{1.0f, 0.5f, 0.0f}};
  std::vector<VertexWeights> weights{vw({9, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f})};
  EXPECT_THROW(fo4::nif::transfer_weights(target, weights, k_corner, std::vector<std::uint16_t>{0, 1, 2},
                                          corner_weights(), std::vector<std::int32_t>{0, 1, 256},
                                          WeightTransferOptions{}),
               std::invalid_argument);
  EXPECT_FLOAT_EQ(weight_for(weights[0], 9), 1.0f);
}

TEST(WeightTransfer, ShapeTransferTakesTheReferenceFromAnotherNifBuffer) {
  // regression: the reference must come from the source buffer, so a target file whose own body is differently weighted must not leak into the result.
  const auto file_a = skin_file(body_vertices(), armor_vertices({k_one, 0, 0, 0}, {0, 0, 0, 0}));
  const std::vector<Vertex> spine_only{skinned(0, 0, 0, {k_one, 0, 0, 0}, {0, 0, 0, 0}),
                                       skinned(1, 0, 0, {k_one, 0, 0, 0}, {0, 0, 0, 0}),
                                       skinned(1, 1, 0, {k_one, 0, 0, 0}, {0, 0, 0, 0}),
                                       skinned(0, 1, 0, {k_one, 0, 0, 0}, {0, 0, 0, 0})};
  const auto file_b = skin_file(spine_only, armor_vertices({k_one, 0, 0, 0}, {0, 0, 0, 0}));

  fo4::nif::WeightTransferReport report;
  const auto cross = fo4::nif::transfer_shape_weights(file_a, 1, file_b, 5, WeightTransferOptions{}, &report);
  const auto same_file = fo4::nif::transfer_shape_weights(read_nif(file_a), 1, 5, WeightTransferOptions{});
  const auto within_b = fo4::nif::transfer_shape_weights(read_nif(file_b), 1, 5, WeightTransferOptions{});

  EXPECT_EQ(report.transferred, 4U);
  EXPECT_EQ(shape_of(cross, 5).weights, shape_of(same_file, 5).weights);
  EXPECT_EQ(shape_of(cross, 5).bone_indices, shape_of(same_file, 5).bone_indices);
  EXPECT_NE(shape_of(cross, 5).weights, shape_of(within_b, 5).weights);
  EXPECT_EQ(shape_of(cross, 1).weights, shape_of(file_b, 1).weights);
  EXPECT_THROW(fo4::nif::transfer_shape_weights(file_a, 9, file_b, 5, WeightTransferOptions{}), std::exception);
}

namespace {

Transform conform_target_transform() {
  Transform xf;
  const float rotation[9] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f};
  std::copy(rotation, rotation + 9, xf.r);
  xf.t[2] = 1.0f;
  xf.s = 2.0f;
  return xf;
}

std::vector<std::byte> conform_plane_bytes() {
  nif_fixture::Writer writer;
  for (const auto& point : {std::array<float, 3>{-10.0f, -10.0f, 0.0f}, std::array<float, 3>{10.0f, -10.0f, 0.0f},
                            std::array<float, 3>{0.0f, 10.0f, 0.0f}}) {
    for (const float value : point) writer.f32(value);
    writer.f32(0.0f);
  }
  return writer.bytes();
}

std::vector<std::byte> conform_target_bytes(bool normals) {
  nif_fixture::Writer writer;
  for (const auto& point : {std::array<float, 3>{0.0f, 1.0f, 0.0f}, std::array<float, 3>{1.0f, -1.0f, 0.0f},
                            std::array<float, 3>{-1.0f, 0.0f, 0.5f}}) {
    for (const float value : point) writer.f32(value);
    writer.f32(0.0f);
    if (normals) {
      writer.u8(128);
      writer.u8(128);
      writer.u8(0);
      writer.u8(128);
    }
  }
  return writer.bytes();
}

Block conform_plane_block() {
  return nif_fixture::shape_block("BSTriShape", 0, Transform{}, nif_fixture::k_full, 16, -1, -1, -1,
                                  conform_plane_bytes(), {0, 1, 2});
}

Block conform_target_block(bool normals) {
  return nif_fixture::shape_block("BSTriShape", 0, conform_target_transform(),
                                  nif_fixture::k_full | (normals ? nif_fixture::k_has_normal : 0U),
                                  normals ? 20 : 16, -1, -1, -1, conform_target_bytes(normals), {0, 1, 2});
}

std::vector<std::byte> conform_file(bool normals) {
  const std::vector<Block> blocks{nif_fixture::node_block("NiNode", 0, Transform{}, {1, 2}), conform_plane_block(),
                                  conform_target_block(normals)};
  return nif_fixture::build_nif({"root"}, blocks, {0});
}

ConformOptions lift_to_half_a_unit() {
  ConformOptions options;
  options.mode = ConformMode::push_out;
  options.offset = 0.5f;
  return options;
}

}  // namespace

TEST(ConformShape, WritesOnlyTheMovedVertexThroughTheInverseOfItsPlacementAndKeepsTheCounts) {
  // regression: the write-back must invert s*R*p+t, so a rotated and scaled shape must not land at the global position as a local one.
  const auto file = conform_file(false);
  fo4::nif::ConformReport report;
  const auto edited = fo4::nif::conform_shape_to_surface(read_nif(file), 2, 1, lift_to_half_a_unit(), &report);

  EXPECT_EQ(report.moved, 1U);
  EXPECT_NEAR(report.max_move, 1.5f, 1e-4f);
  const auto before = shape_of(file, 2);
  const auto after = shape_of(edited, 2);
  ASSERT_EQ(after.verts.size(), before.verts.size());
  EXPECT_EQ(after.tris, before.tris);
  for (const std::size_t k : {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{6}, std::size_t{7},
                              std::size_t{8}}) {
    EXPECT_EQ(after.verts[k], before.verts[k]) << k;
  }
  EXPECT_NEAR(after.verts[3], 1.0f, 1e-4f);
  EXPECT_NEAR(after.verts[4], -0.25f, 1e-4f);
  EXPECT_NEAR(after.verts[5], 0.0f, 1e-4f);
  EXPECT_EQ(shape_of(edited, 1).verts, shape_of(file, 1).verts);
}

TEST(ConformShape, RecomputesTheStoredNormalsFromTheMovedGeometry) {
  // regression: a conformed shape must not keep the normals of its old surface, which would shade the pushed-out vertex wrongly.
  const auto file = conform_file(true);
  const auto edited = fo4::nif::conform_shape_to_surface(read_nif(file), 2, 1, lift_to_half_a_unit());
  const auto after = shape_of(edited, 2);
  ASSERT_EQ(after.normals.size(), 9U);
  const std::array<float, 3> expected{-0.2617f, -0.2094f, -0.9422f};
  for (std::size_t v = 0; v < 3; ++v) {
    const float along = after.normals[3 * v] * expected[0] + after.normals[3 * v + 1] * expected[1] +
                        after.normals[3 * v + 2] * expected[2];
    EXPECT_GT(along, 0.99f) << "vertex " << v;
  }
  EXPECT_LT(shape_of(file, 2).normals[2], -0.9f);
}

TEST(ConformShape, NothingToMoveReturnsTheInputBytesAndTheTwoBufferFormMatchesTheSingleFile) {
  // regression: a conform that moves nothing must return the input bytes, and the two-buffer form must give the single-file positions.
  const auto file = conform_file(false);
  ConformOptions far_below = lift_to_half_a_unit();
  far_below.offset = -50.0f;
  fo4::nif::ConformReport idle;
  EXPECT_EQ(fo4::nif::conform_shape_to_surface(read_nif(file), 2, 1, far_below, &idle), file);
  EXPECT_EQ(idle.moved, 0U);

  const std::vector<Block> reference_only{nif_fixture::node_block("NiNode", 0, Transform{}, {1}),
                                          conform_plane_block()};
  const std::vector<Block> target_only{nif_fixture::node_block("NiNode", 0, Transform{}, {1}),
                                       conform_target_block(false)};
  const auto reference_file = nif_fixture::build_nif({"root"}, reference_only, {0});
  const auto target_file = nif_fixture::build_nif({"root"}, target_only, {0});
  const auto cross = fo4::nif::conform_shape_to_surface(reference_file, 1, target_file, 1, lift_to_half_a_unit());
  const auto single = fo4::nif::conform_shape_to_surface(read_nif(file), 2, 1, lift_to_half_a_unit());
  EXPECT_EQ(shape_of(cross, 1).verts, shape_of(single, 2).verts);
}

TEST(ConformShape, RefusesHalfPrecisionAndTheSameShapeAsItsOwnReference) {
  // regression: a half-precision target must be refused because rewriting its positions as full floats would corrupt the vertex stride.
  nif_fixture::Writer half_vertices;
  for (int i = 0; i < 12; ++i) half_vertices.u16(0);
  const std::vector<Block> blocks{
      nif_fixture::node_block("NiNode", 0, Transform{}, {1, 2}), conform_plane_block(),
      nif_fixture::shape_block("BSTriShape", 0, Transform{}, 0U, 8, -1, -1, -1, half_vertices.bytes(), {0, 1, 2})};
  const auto half_file = nif_fixture::build_nif({"root"}, blocks, {0});
  EXPECT_THROW(fo4::nif::conform_shape_to_surface(read_nif(half_file), 2, 1, lift_to_half_a_unit()),
               std::invalid_argument);
  EXPECT_THROW(fo4::nif::conform_shape_to_surface(read_nif(conform_file(false)), 2, 2, lift_to_half_a_unit()),
               std::invalid_argument);
}

namespace {

fo4::nif::OsdDataSet osd_diff(std::vector<fo4::nif::OsdOffset> offsets) {
  fo4::nif::OsdDataSet diff;
  diff.name = "Slider";
  diff.offsets = std::move(offsets);
  return diff;
}

}  // namespace

TEST(OsdApplyShape, MovesOnlyTheListedVertexByDeltaTimesWeightInTheStoredFrame) {
  // regression: the offset must be added to the stored positions, not a global position (the placement rotates and scales), and only to its own vertex.
  const auto file = conform_file(false);
  std::size_t moved = 0;
  const auto edited =
      fo4::nif::apply_osd_to_shape(read_nif(file), 2, osd_diff({{1, {0.5f, 0.0f, 0.25f}}}), 0.5f, &moved);

  EXPECT_EQ(moved, 1U);
  const auto before = shape_of(file, 2);
  const auto after = shape_of(edited, 2);
  ASSERT_EQ(after.verts.size(), before.verts.size());
  EXPECT_EQ(after.tris, before.tris);
  for (const std::size_t k : {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{6}, std::size_t{7},
                              std::size_t{8}}) {
    EXPECT_EQ(after.verts[k], before.verts[k]) << k;
  }
  EXPECT_NEAR(after.verts[3], before.verts[3] + 0.25f, 1e-5f);
  EXPECT_NEAR(after.verts[4], before.verts[4], 1e-5f);
  EXPECT_NEAR(after.verts[5], before.verts[5] + 0.125f, 1e-5f);
  EXPECT_EQ(shape_of(edited, 1).verts, shape_of(file, 1).verts);
}

TEST(OsdApplyShape, RecomputesStoredNormals) {
  // regression: a slider write must not keep the stored normals of the old surface, which would shade the moved vertex wrongly.
  const auto file = conform_file(true);
  const auto edited = fo4::nif::apply_osd_to_shape(read_nif(file), 2, osd_diff({{0, {0.0f, 0.0f, 1.0f}}}), 1.0f);
  const auto before = shape_of(file, 2);
  const auto after = shape_of(edited, 2);
  ASSERT_EQ(after.normals.size(), 9U);
  EXPECT_NE(after.normals, before.normals);
  const std::array<float, 3> expected{0.0f, 0.4472f, -0.8944f};
  for (std::size_t v = 0; v < 3; ++v) {
    const float along = after.normals[3 * v] * expected[0] + after.normals[3 * v + 1] * expected[1] +
                        after.normals[3 * v + 2] * expected[2];
    EXPECT_GT(std::fabs(along), 0.99f) << "vertex " << v;
  }
}

TEST(OsdApplyShape, NothingToMoveReturnsTheInputBytes) {
  // regression: weight 0 or a diff with no offsets must return the input bytes instead of rewriting the file.
  const auto file = conform_file(true);
  std::size_t moved = 7;
  EXPECT_EQ(fo4::nif::apply_osd_to_shape(read_nif(file), 2, osd_diff({{1, {1.0f, 1.0f, 1.0f}}}), 0.0f, &moved), file);
  EXPECT_EQ(moved, 0U);
  EXPECT_EQ(fo4::nif::apply_osd_to_shape(read_nif(file), 2, osd_diff({}), 1.0f, &moved), file);
  EXPECT_EQ(moved, 0U);
}

TEST(OsdApplyShape, RefusesWhatItCannotWriteBackAndLeavesTheFileAlone) {
  // regression: a vertex past the shape, a non-finite weight, a half-precision shape or a non-shape block must be refused, never written.
  const auto file = conform_file(false);
  const auto nif = read_nif(file);
  EXPECT_THROW(fo4::nif::apply_osd_to_shape(nif, 2, osd_diff({{3, {1.0f, 0.0f, 0.0f}}}), 1.0f), std::invalid_argument);
  EXPECT_THROW(fo4::nif::apply_osd_to_shape(nif, 2, osd_diff({{1, {1.0f, 0.0f, 0.0f}}}),
                                            std::numeric_limits<float>::quiet_NaN()),
               std::invalid_argument);
  EXPECT_THROW(fo4::nif::apply_osd_to_shape(nif, 0, osd_diff({{1, {1.0f, 0.0f, 0.0f}}}), 1.0f), std::invalid_argument);
  EXPECT_THROW(fo4::nif::apply_osd_to_shape(nif, 99, osd_diff({{1, {1.0f, 0.0f, 0.0f}}}), 1.0f),
               std::invalid_argument);
  nif_fixture::Writer half_vertices;
  for (int i = 0; i < 12; ++i) half_vertices.u16(0);
  const std::vector<Block> blocks{
      nif_fixture::node_block("NiNode", 0, Transform{}, {1}),
      nif_fixture::shape_block("BSTriShape", 0, Transform{}, 0U, 8, -1, -1, -1, half_vertices.bytes(), {0, 1, 2})};
  const auto half_file = nif_fixture::build_nif({"root"}, blocks, {0});
  EXPECT_THROW(fo4::nif::apply_osd_to_shape(read_nif(half_file), 1, osd_diff({{1, {1.0f, 0.0f, 0.0f}}}), 1.0f),
               std::invalid_argument);
}

namespace {

using Points = std::vector<std::array<float, 3>>;

std::vector<std::byte> point_bytes(const Points& points) {
  nif_fixture::Writer writer;
  for (const auto& point : points) {
    for (const float value : point) writer.f32(value);
    writer.f32(0.0f);
  }
  return writer.bytes();
}

std::vector<std::byte> id_remap_payload(const std::vector<std::uint8_t>& ids) {
  nif_fixture::Writer writer;
  for (const auto id : ids) {
    writer.u16(k_one);
    writer.u16(0);
    writer.u16(0);
    writer.u16(0);
    writer.u8(id);
    writer.u8(0);
    writer.u8(0);
    writer.u8(0);
  }
  return writer.bytes();
}

std::vector<std::byte> remap_mesh_file(const Points& points, const std::vector<std::uint16_t>& tris,
                                       const std::vector<std::uint8_t>& ids) {
  auto shape = nif_fixture::shape_block("BSTriShape", 0, Transform{}, nif_fixture::k_full, 16, -1, -1, -1,
                                        point_bytes(points), tris, 1);
  put_i32(shape.data, 8, 2);
  const std::vector<Block> blocks{nif_fixture::node_block("NiNode", 0, Transform{}, {1}), shape,
                                  binary_extra_block(1, id_remap_payload(ids))};
  return nif_fixture::build_nif({"root", "CustomizationRemapData"}, blocks, {0});
}

std::vector<int> remap_ids(const std::vector<std::byte>& bytes, std::uint32_t shape) {
  std::vector<int> ids;
  for (const auto& record : fo4::nif::read_customization_remap(read_nif(bytes), shape).records) {
    ids.push_back(record.bone[0]);
  }
  return ids;
}

RemapNewBone one_new_bone() {
  RemapNewBone bone;
  const auto payload = new_bone_payload();
  std::copy_n(payload.begin(), bone.raw.size(), bone.raw.begin());
  return bone;
}

RemapRecord solo(std::uint8_t bone) { return RemapRecord{{1.0f, 0.0f, 0.0f, 0.0f}, {bone, 0, 0, 0}}; }

const Points k_welded_points{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 0, 0}, {1, 1, 0}};
const std::vector<std::uint16_t> k_welded_tris{0, 1, 2, 3, 4, 2};

}  // namespace

TEST(CustomizationRemap, WeldKeepsEachSurvivingVertexOwnRecordAndNeverMergesDifferentOnes) {
  // regression: a weld must carry each vertex remap record with it, so a record list left at the old length must not stay behind and shift every later record.
  const auto same = remap_mesh_file(k_welded_points, k_welded_tris, {0, 1, 2, 1, 4});
  const auto welded = edit(same, request_for(ShapeOp::weld_vertices, {1}));
  EXPECT_TRUE(is_changed(welded.report, 1));
  EXPECT_EQ(shape_of(welded.bytes, 1).verts.size() / 3, 4U);
  EXPECT_EQ(remap_ids(welded.bytes, 1), (std::vector<int>{0, 1, 2, 4}));
  const auto remap = fo4::nif::read_customization_remap(read_nif(welded.bytes), 1);
  EXPECT_TRUE(fo4::nif::validate_customization_remap(remap, 8, 4).empty());

  const auto different = remap_mesh_file(k_welded_points, k_welded_tris, {0, 1, 2, 3, 4});
  const auto kept = edit(different, request_for(ShapeOp::weld_vertices, {1}));
  EXPECT_FALSE(is_changed(kept.report, 1));
  EXPECT_EQ(shape_of(kept.bytes, 1).verts.size() / 3, 5U);
  EXPECT_EQ(remap_ids(kept.bytes, 1), (std::vector<int>{0, 1, 2, 3, 4}));
}

TEST(CustomizationRemap, RemovingUnusedVerticesDropsTheirRecordsToo) {
  // regression: dropping an unused vertex must drop its remap record, or every later vertex would read its neighbour record.
  const auto file = remap_mesh_file({{0, 0, 0}, {1, 0, 0}, {5, 5, 5}, {0, 1, 0}}, {0, 1, 3}, {0, 1, 2, 3});
  const auto edited = edit(file, request_for(ShapeOp::remove_unused_vertices, {1}));
  EXPECT_TRUE(is_changed(edited.report, 1));
  EXPECT_EQ(shape_of(edited.bytes, 1).verts.size() / 3, 3U);
  EXPECT_EQ(remap_ids(edited.bytes, 1), (std::vector<int>{0, 1, 3}));
}

TEST(CustomizationRemap, ACountChangingOpSkipsAShapeWhoseRecordCountDisagreesWithItsVertices) {
  // regression: a shape with a mismatched record count must be skipped with a reason, because aligning its records would invent or lose data.
  const auto file = remap_mesh_file({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 0, 0}}, {0, 1, 2, 3, 2, 1}, {0, 1, 2});
  const auto edited = edit(file, request_for(ShapeOp::weld_vertices, {1}));
  EXPECT_FALSE(is_changed(edited.report, 1));
  ASSERT_EQ(edited.report.skipped.size(), 1U);
  EXPECT_NE(edited.report.skipped[0].find("records for"), std::string::npos);
  EXPECT_EQ(shape_of(edited.bytes, 1).verts.size() / 3, 4U);
  EXPECT_EQ(remap_ids(edited.bytes, 1), (std::vector<int>{0, 1, 2}));
}

TEST(CustomizationRemap, SeparateGivesEachPartItsOwnRecordsInItsOwnBlock) {
  // regression: a separated part must read only its own vertices records from its own remap block, never the shared original with every record.
  const auto file = remap_mesh_file({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {5, 0, 0}, {6, 0, 0}, {5, 1, 0}},
                                    {0, 1, 2, 3, 4, 5}, {10, 11, 12, 13, 14, 15});
  fo4::nif::SeparateReport report;
  const auto edited = fo4::nif::separate_shape(read_nif(file), fo4::nif::SeparateRequest{1, {{0}, {1}}}, &report);
  ASSERT_EQ(report.blocks.size(), 2U);
  EXPECT_EQ(remap_ids(edited, report.blocks[0]), (std::vector<int>{10, 11, 12}));
  EXPECT_EQ(remap_ids(edited, report.blocks[1]), (std::vector<int>{13, 14, 15}));
  const auto after = read_nif(edited);
  EXPECT_NE(fo4::nif::read_customization_remap(after, report.blocks[0]).remap_block,
            fo4::nif::read_customization_remap(after, report.blocks[1]).remap_block);
  EXPECT_EQ(shape_of(edited, report.blocks[0]).verts.size() / 3, 3U);
  EXPECT_EQ(shape_of(edited, report.blocks[1]).verts.size() / 3, 3U);
}

TEST(CustomizationRemap, JoinAndTheModelOpsRefuseAShapeThatOwnsRemapData) {
  // regression: joining or extruding a remap shape must be refused, because the new vertices have no source record and the bone indices differ per palette.
  const auto file = remap_mesh_file({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, {0, 1, 2}, {0, 1, 2});
  try {
    fo4::nif::join_shapes(read_nif(file), fo4::nif::JoinRequest{{1, 1}});
    FAIL() << "a remap shape must not join";
  } catch (const std::invalid_argument& error) {
    EXPECT_NE(std::string(error.what()).find("remap"), std::string::npos);
  }
  fo4::nif::ModelOpRequest request;
  request.block = 1;
  request.tris = {0};
  request.distance = 1.0f;
  try {
    fo4::nif::apply_model_op(read_nif(file), request);
    FAIL() << "a remap shape must not take a model op";
  } catch (const std::invalid_argument& error) {
    EXPECT_NE(std::string(error.what()).find("CustomizationRemapData"), std::string::npos);
  }
}

TEST(CustomizationRemap, JoinAndTheModelOpsReportUnreadableRemapDataAsARefusalNotAParseError) {
  // regression: a shape whose remap payload is structurally bad must give the invalid_argument refusal of the op, not a parse_error from the remap probe.
  auto odd = remap_payload(k_half, 1);
  odd.push_back(std::byte{0});
  const auto file = remap_file(odd, new_bone_payload());
  EXPECT_THROW(fo4::nif::join_shapes(read_nif(file), fo4::nif::JoinRequest{{1, 1}}), std::invalid_argument);
  fo4::nif::ModelOpRequest request;
  request.block = 1;
  request.tris = {0};
  request.distance = 1.0f;
  EXPECT_THROW(fo4::nif::apply_model_op(read_nif(file), request), std::invalid_argument);
}

TEST(CustomizationRemap, CreateAddsTheTwoExtraBlocksToAShapeThatHasNoneWithTheirNamesInTheStringTable) {
  // regression: a created remap must own its extra-data refs and string names, or the reader would find no remap on the shape.
  const auto file = unskinned_file();
  const auto nif = read_nif(file);
  CustomizationRemap remap;
  remap.records = {solo(0), solo(1), solo(2)};
  remap.new_bones.push_back(one_new_bone());
  const auto created = fo4::nif::create_customization_remap(nif, 1, remap);

  const auto after = read_nif(created);
  EXPECT_EQ(after.blocks().size(), nif.blocks().size() + 2);
  EXPECT_EQ(after.strings().size(), nif.strings().size() + 2);
  const auto again = fo4::nif::read_customization_remap(after, 1);
  EXPECT_GE(again.remap_block, 0);
  EXPECT_GE(again.new_bones_block, 0);
  ASSERT_EQ(again.records.size(), 3U);
  EXPECT_EQ(again.records[2].bone[0], 2);
  ASSERT_EQ(again.new_bones.size(), 1U);
  EXPECT_EQ(again.new_bones[0].raw, remap.new_bones[0].raw);
  EXPECT_EQ(fo4::nif::remap_new_bone_name(again.new_bones[0]), "Neck");
  EXPECT_EQ(fo4::nif::write_customization_remap(after, 1, again), created);
  EXPECT_TRUE(fo4::nif::validate_customization_remap(again, 4, 3).empty());
  EXPECT_EQ(shape_of(created, 1).verts, shape_of(file, 1).verts);
  EXPECT_EQ(shape_of(created, 1).tris, shape_of(file, 1).tris);

  CustomizationRemap records_only;
  records_only.records = {solo(0), solo(1), solo(2)};
  const auto first = fo4::nif::create_customization_remap(nif, 1, records_only);
  EXPECT_EQ(read_nif(first).blocks().size(), nif.blocks().size() + 1);
  EXPECT_EQ(fo4::nif::read_customization_remap(read_nif(first), 1).new_bones_block, -1);
  CustomizationRemap bones_only;
  bones_only.new_bones.push_back(one_new_bone());
  const auto both = fo4::nif::create_customization_remap(read_nif(first), 1, bones_only);
  const auto joined = fo4::nif::read_customization_remap(read_nif(both), 1);
  EXPECT_EQ(joined.records.size(), 3U);
  EXPECT_EQ(joined.new_bones.size(), 1U);
}

TEST(CustomizationRemap, CreateRefusesWhatItWouldDuplicateOrCannotAlign) {
  // regression: creating on a shape that already owns a block must be refused, because a second same-name block makes the reader refuse the whole shape.
  const auto owned = read_nif(remap_file(remap_payload(k_half, 1), new_bone_payload()));
  CustomizationRemap again;
  again.records = {solo(0), solo(1), solo(2)};
  EXPECT_THROW(fo4::nif::create_customization_remap(owned, 1, again), std::invalid_argument);
  const auto bare = read_nif(unskinned_file());
  CustomizationRemap short_records;
  short_records.records = {solo(0), solo(1)};
  EXPECT_THROW(fo4::nif::create_customization_remap(bare, 1, short_records), std::invalid_argument);
  EXPECT_THROW(fo4::nif::create_customization_remap(bare, 1, CustomizationRemap{}), std::invalid_argument);
  CustomizationRemap heavy;
  heavy.records = {solo(0), solo(1), solo(2)};
  heavy.records[1].weight[0] = 70000.0f;
  EXPECT_THROW(fo4::nif::create_customization_remap(bare, 1, heavy), std::invalid_argument);
  EXPECT_THROW(fo4::nif::create_customization_remap(bare, 7, again), std::invalid_argument);
}

TEST(CustomizationRemap, TheWriterRefusesWeightsAboveOneNegativeOnesAndInfinity) {
  // regression: a weight above 1 must be refused, because half_bits overflows above 65504 and would store a wrong weight silently.
  const auto nif = read_nif(remap_file(remap_payload(k_half, 1), new_bone_payload()));
  const auto remap = fo4::nif::read_customization_remap(nif, 1);
  for (const float bad : {1.5f, -0.25f, std::numeric_limits<float>::infinity(), 70000.0f}) {
    auto edited = remap;
    edited.records[1].weight[0] = bad;
    EXPECT_THROW(fo4::nif::write_customization_remap(nif, 1, edited), std::invalid_argument) << bad;
  }
}

TEST(CustomizationRemap, AnEmptyNewBonesListWithNoBlockLeavesTheExistingNewBonesBlockUntouched) {
  // regression: a remap value that never read the new-bones block must not wipe the block the file still owns.
  const auto file = remap_file(remap_payload(k_half, 1), new_bone_payload());
  const auto nif = read_nif(file);
  auto remap = fo4::nif::read_customization_remap(nif, 1);
  remap.new_bones.clear();
  remap.new_bones_block = -1;
  const auto untouched = fo4::nif::write_customization_remap(nif, 1, remap);
  EXPECT_EQ(untouched, file);
  EXPECT_EQ(fo4::nif::read_customization_remap(read_nif(untouched), 1).new_bones.size(), 1U);

  remap.new_bones_block = 3;
  const auto emptied = fo4::nif::write_customization_remap(nif, 1, remap);
  EXPECT_TRUE(fo4::nif::read_customization_remap(read_nif(emptied), 1).new_bones.empty());
}

TEST(CustomizationRemap, TheReaderReturnsAMismatchedRecordCountAndTheValidatorReportsIt) {
  // regression: a record count that differs from the vertex count must be reported, not refused, so a damaged file can still be read and repaired.
  const auto nif = read_nif(remap_file(remap_payload(k_half, 1, 2), new_bone_payload()));
  const auto remap = fo4::nif::read_customization_remap(nif, 1);
  ASSERT_EQ(remap.records.size(), 2U);
  const auto problems = fo4::nif::validate_customization_remap(remap, 8, 3);
  ASSERT_EQ(problems.size(), 1U);
  EXPECT_NE(problems[0].find("2 records for 3 vertices"), std::string::npos);
  const auto repaired = fo4::nif::reindex_remap_records(remap.records, std::vector<std::int32_t>{0, 1, 1});
  EXPECT_EQ(repaired.size(), 3U);
}
