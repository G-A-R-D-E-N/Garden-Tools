#include "fo4/nif/connect_points.hpp"
#include "fo4/nif/nif.hpp"
#include "nif_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using fo4::nif::apply_connect_point_edits_json;
using fo4::nif::ConnectPoint;
using fo4::nif::connect_point_world_position;
using fo4::nif::NifFile;
using fo4::nif::read_connect_children;
using fo4::nif::read_connect_parents;
using fo4::nif::read_nif;
using nif_build::Block;
using nif_build::expect_parse_error;
using nif_build::Writer;

struct PointSpec {
  std::string parent;
  std::string name;
  float rotation[4];
  float translation[3];
  float scale;
};

Block named_node(std::uint32_t name, const std::vector<std::int32_t>& children,
                 const fo4::nif::Transform& transform = {}) {
  Writer w;
  w.i32(static_cast<std::int32_t>(name));
  w.u32(0);
  w.i32(-1);
  w.u32(0);
  w.transform(transform);
  w.i32(-1);
  w.u32(static_cast<std::uint32_t>(children.size()));
  for (const auto child : children) w.i32(child);
  return Block{"NiNode", w.bytes()};
}

Block parents_block(std::int32_t name, const std::vector<PointSpec>& points,
                    const std::vector<std::byte>& trailing = {}) {
  Writer w;
  w.i32(name);
  w.u32(static_cast<std::uint32_t>(points.size()));
  for (const auto& point : points) {
    w.sized(point.parent);
    w.sized(point.name);
    for (const float value : point.rotation) w.f32(value);
    for (const float value : point.translation) w.f32(value);
    w.f32(point.scale);
  }
  w.append(trailing);
  return Block{"BSConnectPoint::Parents", w.bytes()};
}

Block children_block(std::int32_t name, bool skinned, const std::vector<std::string>& names) {
  Writer w;
  w.i32(name);
  w.u8(static_cast<std::uint8_t>(skinned ? 1 : 0));
  w.u32(static_cast<std::uint32_t>(names.size()));
  for (const auto& value : names) w.sized(value);
  return Block{"BSConnectPoint::Children", w.bytes()};
}

PointSpec muzzle() {
  return PointSpec{"Chest", "P-Muzzle", {1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 2.0f, 3.0f}, 1.0f};
}

PointSpec grip() {
  return PointSpec{"", "P-Grip", {0.5f, 0.5f, 0.5f, 0.5f}, {-4.0f, 0.25f, 8.0f}, 2.0f};
}

// Invariant: block 0 is the root, block 1 is the node named Chest, block 2 is the CPA block and block 3 is the CPT block.
std::vector<std::byte> sample_nif(std::vector<std::byte> trailing = {}) {
  const std::vector<std::string> strings{"Root", "Chest", "CPA", "CPT"};
  std::vector<Block> blocks{
      named_node(0, {1}),
      named_node(1, {}),
      parents_block(2, {muzzle(), grip()}, trailing),
      children_block(3, true, {"C-Barrel", "C-Grip"}),
  };
  return nif_build::build_nif(strings, blocks, {0});
}

std::span<const std::byte> block_span(const std::vector<std::byte>& file, const NifFile& nif, std::uint32_t block) {
  const auto& info = nif.blocks()[block];
  return std::span<const std::byte>(file).subspan(info.offset, info.size);
}

bool same(std::span<const std::byte> left, std::span<const std::byte> right) {
  return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin());
}

void expect_point(const ConnectPoint& point, const PointSpec& expected) {
  EXPECT_EQ(point.parent, expected.parent);
  EXPECT_EQ(point.name, expected.name);
  for (std::size_t i = 0; i < 4; ++i) EXPECT_FLOAT_EQ(point.rotation[i], expected.rotation[i]);
  for (std::size_t i = 0; i < 3; ++i) EXPECT_FLOAT_EQ(point.translation[i], expected.translation[i]);
  EXPECT_FLOAT_EQ(point.scale, expected.scale);
}

}  // namespace

TEST(NifConnectPoints, ReadsParentsBlockFieldsInNifXmlOrder) {
  const auto bytes = sample_nif();
  const auto nif = read_nif(bytes);
  const auto value = read_connect_parents(nif, 2);
  EXPECT_EQ(value.name, "CPA");
  ASSERT_EQ(value.points.size(), 2U);
  expect_point(value.points[0], muzzle());
  expect_point(value.points[1], grip());
}

TEST(NifConnectPoints, ReadsChildrenBlockFieldsInNifXmlOrder) {
  const auto bytes = sample_nif();
  const auto nif = read_nif(bytes);
  const auto value = read_connect_children(nif, 3);
  EXPECT_EQ(value.name, "CPT");
  EXPECT_TRUE(value.skinned);
  EXPECT_EQ(value.names, (std::vector<std::string>{"C-Barrel", "C-Grip"}));
}

TEST(NifConnectPoints, WorldPositionAddsTheNamedParentTranslationOrNothingForAnEmptyParent) {
  fo4::nif::Transform chest;
  chest.t[0] = 10.0f;
  const std::vector<std::string> strings{"Root", "Chest", "CPA"};
  const std::vector<Block> blocks{
      named_node(0, {1}),
      named_node(1, {}, chest),
      parents_block(2, {muzzle(), grip()}),
  };
  const auto bytes = nif_build::build_nif(strings, blocks, {0});
  const auto nif = read_nif(bytes);
  const auto points = read_connect_parents(nif, 2).points;
  ASSERT_EQ(points.size(), 2U);
  EXPECT_EQ(connect_point_world_position(nif, points[0]), (std::array<float, 3>{11.0f, 2.0f, 3.0f}));
  EXPECT_EQ(connect_point_world_position(nif, points[1]), (std::array<float, 3>{-4.0f, 0.25f, 8.0f}));
}

TEST(NifConnectPoints, ListsConnectPointBlocksInFileOrder) {
  const auto bytes = sample_nif();
  const auto nif = read_nif(bytes);
  EXPECT_EQ(fo4::nif::connect_point_blocks(nif), (std::vector<std::uint32_t>{2, 3}));
}

TEST(NifConnectPoints, ReadRefusesMissingAndWrongBlocks) {
  const auto bytes = sample_nif();
  const auto nif = read_nif(bytes);
  expect_parse_error([&] { read_connect_parents(nif, 99); }, "is not in the file");
  expect_parse_error([&] { read_connect_parents(nif, 3); }, "is BSConnectPoint::Children, not BSConnectPoint::Parents");
  expect_parse_error([&] { read_connect_children(nif, 2); }, "is BSConnectPoint::Parents, not BSConnectPoint::Children");
}

TEST(NifConnectPoints, EmptyOpsReturnTheFileUnchanged) {
  const auto bytes = sample_nif();
  const auto nif = read_nif(bytes);
  EXPECT_EQ(apply_connect_point_edits_json(nif, "[]"), bytes);
}

TEST(NifConnectPoints, RewritingCurrentValuesIsByteIdentical) {
  const auto bytes = sample_nif();
  const auto nif = read_nif(bytes);
  const std::string ops = R"([
    {"op":"set","block":2,"index":0,"field":"parent","value":"Chest"},
    {"op":"set","block":2,"index":0,"field":"name","value":"P-Muzzle"},
    {"op":"set","block":2,"index":1,"field":"rotation","value":[0.5,0.5,0.5,0.5]},
    {"op":"set","block":2,"index":1,"field":"translation","value":[-4,0.25,8]},
    {"op":"set","block":2,"index":1,"field":"scale","value":2},
    {"op":"set","block":3,"field":"skinned","value":true},
    {"op":"set","block":3,"index":0,"field":"name","value":"C-Barrel"}
  ])";
  EXPECT_EQ(apply_connect_point_edits_json(nif, ops), bytes);
}

TEST(NifConnectPoints, SetFieldsRewritesOnlyThatBlockAndKeepsTheRest) {
  const auto bytes = sample_nif();
  const auto nif = read_nif(bytes);
  const std::string ops = R"([
    {"op":"set","block":2,"index":0,"field":"parent","value":"Root"},
    {"op":"set","block":2,"index":0,"field":"name","value":"P-Renamed"},
    {"op":"set","block":2,"index":0,"field":"rotation","value":[0,1,0,0]},
    {"op":"set","block":2,"index":0,"field":"translation","value":[9,8,7]},
    {"op":"set","block":2,"index":0,"field":"scale","value":0.5}
  ])";
  const auto edited = apply_connect_point_edits_json(nif, ops);
  const auto after = read_nif(edited);
  const auto value = read_connect_parents(after, 2);
  ASSERT_EQ(value.points.size(), 2U);
  EXPECT_EQ(value.points[0].parent, "Root");
  EXPECT_EQ(value.points[0].name, "P-Renamed");
  EXPECT_FLOAT_EQ(value.points[0].rotation[1], 1.0f);
  EXPECT_FLOAT_EQ(value.points[0].translation[0], 9.0f);
  EXPECT_FLOAT_EQ(value.points[0].scale, 0.5f);
  expect_point(value.points[1], grip());
  EXPECT_EQ(read_connect_children(after, 3).names, (std::vector<std::string>{"C-Barrel", "C-Grip"}));
  EXPECT_TRUE(same(block_span(edited, after, 0), block_span(bytes, nif, 0)));
  EXPECT_TRUE(same(block_span(edited, after, 1), block_span(bytes, nif, 1)));
  EXPECT_TRUE(same(block_span(edited, after, 3), block_span(bytes, nif, 3)));
}

TEST(NifConnectPoints, AddAndRemovePointsChangeTheBlockSize) {
  const auto bytes = sample_nif();
  const auto nif = read_nif(bytes);
  const std::string ops = R"([
    {"op":"add","block":2,"index":0,"value":{"parent":"Chest","name":"P-New","scale":3}},
    {"op":"remove","block":2,"index":2}
  ])";
  const auto edited = apply_connect_point_edits_json(nif, ops);
  const auto after = read_nif(edited);
  const auto value = read_connect_parents(after, 2);
  ASSERT_EQ(value.points.size(), 2U);
  EXPECT_EQ(value.points[0].name, "P-New");
  EXPECT_EQ(value.points[0].parent, "Chest");
  EXPECT_FLOAT_EQ(value.points[0].scale, 3.0f);
  EXPECT_FLOAT_EQ(value.points[0].rotation[0], 1.0f);
  expect_point(value.points[1], muzzle());
}

TEST(NifConnectPoints, AppendsAPointWhenIndexIsOmitted) {
  const auto bytes = sample_nif();
  const auto nif = read_nif(bytes);
  const auto edited = apply_connect_point_edits_json(nif, R"([{"op":"add","block":2,"value":{"name":"P-Tail"}}])");
  const auto value = read_connect_parents(read_nif(edited), 2);
  ASSERT_EQ(value.points.size(), 3U);
  EXPECT_EQ(value.points[2].name, "P-Tail");
  EXPECT_EQ(value.points[2].parent, "");
  EXPECT_FLOAT_EQ(value.points[2].scale, 1.0f);
}

TEST(NifConnectPoints, EditsChildrenListAndSkinnedFlag) {
  const auto bytes = sample_nif();
  const auto nif = read_nif(bytes);
  const std::string ops = R"([
    {"op":"add","block":3,"index":1,"value":"C-Sight"},
    {"op":"remove","block":3,"index":0},
    {"op":"set","block":3,"index":1,"field":"name","value":"C-Scope"},
    {"op":"set","block":3,"field":"skinned","value":false}
  ])";
  const auto edited = apply_connect_point_edits_json(nif, ops);
  const auto value = read_connect_children(read_nif(edited), 3);
  EXPECT_FALSE(value.skinned);
  EXPECT_EQ(value.names, (std::vector<std::string>{"C-Sight", "C-Scope"}));
}

TEST(NifConnectPoints, TrailingBytesAfterTheListSurviveAnEdit) {
  const std::vector<std::byte> trailing{std::byte{0xAA}, std::byte{0xBB}};
  const auto bytes = sample_nif(trailing);
  const auto nif = read_nif(bytes);
  const auto edited = apply_connect_point_edits_json(nif, R"([{"op":"set","block":2,"index":0,"field":"name","value":"X"}])");
  const auto after = read_nif(edited);
  const auto block = block_span(edited, after, 2);
  ASSERT_GE(block.size(), 2U);
  EXPECT_EQ(block[block.size() - 2], std::byte{0xAA});
  EXPECT_EQ(block[block.size() - 1], std::byte{0xBB});
  EXPECT_EQ(read_connect_parents(after, 2).points[0].name, "X");
}

TEST(NifConnectPoints, RefusesAMissingBlock) {
  const auto bytes = sample_nif();
  const auto nif = read_nif(bytes);
  expect_parse_error(
      [&] { apply_connect_point_edits_json(nif, R"([{"op":"remove","block":40,"index":0}])"); },
      "targets block 40, which is not in the file");
}

TEST(NifConnectPoints, RefusesABlockOfAnotherType) {
  const auto bytes = sample_nif();
  const auto nif = read_nif(bytes);
  expect_parse_error(
      [&] { apply_connect_point_edits_json(nif, R"([{"op":"set","block":1,"index":0,"field":"name","value":"x"}])"); },
      "is NiNode, not a connect point block");
}

TEST(NifConnectPoints, RefusesABadIndex) {
  const auto bytes = sample_nif();
  const auto nif = read_nif(bytes);
  expect_parse_error(
      [&] { apply_connect_point_edits_json(nif, R"([{"op":"remove","block":2,"index":2}])"); },
      "connect point index 2 is out of range");
  expect_parse_error(
      [&] { apply_connect_point_edits_json(nif, R"([{"op":"add","block":2,"index":3,"value":{"name":"x"}}])"); },
      "connect point index 3 is out of range");
  expect_parse_error(
      [&] { apply_connect_point_edits_json(nif, R"([{"op":"set","block":2,"index":-1,"field":"name","value":"x"}])"); },
      "connect point index -1 is out of range");
  expect_parse_error(
      [&] { apply_connect_point_edits_json(nif, R"([{"op":"set","block":3,"index":2,"field":"name","value":"x"}])"); },
      "connect point index 2 is out of range");
}

TEST(NifConnectPoints, RefusesNonFiniteValues) {
  const auto bytes = sample_nif();
  const auto nif = read_nif(bytes);
  expect_parse_error(
      [&] { apply_connect_point_edits_json(nif, R"([{"op":"set","block":2,"index":0,"field":"scale","value":1e39}])"); },
      "must be a finite 32-bit float");
  expect_parse_error(
      [&] { apply_connect_point_edits_json(nif, R"([{"op":"set","block":2,"index":0,"field":"translation","value":[1e39,0,0]}])"); },
      "must be a finite 32-bit float");
  expect_parse_error(
      [&] { apply_connect_point_edits_json(nif, R"([{"op":"set","block":2,"index":0,"field":"scale","value":1e400}])"); },
      "connect point ops are not valid JSON");
}

TEST(NifConnectPoints, RefusesAParentThatIsNotANode) {
  const auto bytes = sample_nif();
  const auto nif = read_nif(bytes);
  expect_parse_error(
      [&] { apply_connect_point_edits_json(nif, R"([{"op":"set","block":2,"index":0,"field":"parent","value":"Ghost"}])"); },
      "connect point parent 'Ghost' is not a node in this NIF");
  expect_parse_error(
      [&] { apply_connect_point_edits_json(nif, R"([{"op":"add","block":2,"value":{"parent":"Ghost"}}])"); },
      "connect point parent 'Ghost' is not a node in this NIF");
}

TEST(NifConnectPoints, AcceptsAnEmptyParentAndTheWorkshopParentName) {
  const auto bytes = sample_nif();
  const auto nif = read_nif(bytes);
  const auto edited = apply_connect_point_edits_json(
      nif, R"([{"op":"set","block":2,"index":0,"field":"parent","value":"WorkshopConnectPoints"},)"
           R"({"op":"set","block":2,"index":1,"field":"parent","value":"Chest"}])");
  const auto value = read_connect_parents(read_nif(edited), 2);
  EXPECT_EQ(value.points[0].parent, "WorkshopConnectPoints");
  EXPECT_EQ(value.points[1].parent, "Chest");
  const auto cleared = apply_connect_point_edits_json(
      nif, R"([{"op":"set","block":2,"index":0,"field":"parent","value":""}])");
  EXPECT_EQ(read_connect_parents(read_nif(cleared), 2).points[0].parent, "");
}

TEST(NifConnectPoints, RefusesAnUnknownOpFieldOrKey) {
  const auto bytes = sample_nif();
  const auto nif = read_nif(bytes);
  expect_parse_error(
      [&] { apply_connect_point_edits_json(nif, R"([{"op":"rotate","block":2,"index":0}])"); },
      "connect point op 'rotate' is not add, remove or set");
  expect_parse_error(
      [&] { apply_connect_point_edits_json(nif, R"([{"op":"set","block":2,"index":0,"field":"flags","value":1}])"); },
      "connect point field 'flags' is not editable");
  expect_parse_error(
      [&] { apply_connect_point_edits_json(nif, R"([{"op":"add","block":2,"value":{"rotaton":[1,0,0,0]}}])"); },
      "connect point value has no field 'rotaton'");
  expect_parse_error(
      [&] { apply_connect_point_edits_json(nif, R"([{"op":"set","block":2,"index":0,"field":"rotation","value":[1,0,0]}])"); },
      "needs 4 numbers");
}

TEST(NifConnectPoints, RefusesMalformedOpText) {
  const auto bytes = sample_nif();
  const auto nif = read_nif(bytes);
  expect_parse_error([&] { apply_connect_point_edits_json(nif, "{}"); }, "must be a JSON array");
  expect_parse_error([&] { apply_connect_point_edits_json(nif, "[{"); }, "not valid JSON");
}

TEST(NifConnectPoints, SetParentToAParentAlreadyStoredInTheBlockSkipsTheNodeCheck) {
  const std::vector<std::string> strings{"Root", "Chest", "CPA"};
  const std::vector<Block> blocks{
      named_node(0, {1}),
      named_node(1, {}),
      parents_block(2, {muzzle(), PointSpec{"Ghost", "P-B", {1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 1.0f},
                        PointSpec{"Ghost", "P-C", {1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 1.0f}}),
  };
  const auto bytes = nif_build::build_nif(strings, blocks, {0});
  const auto nif = read_nif(bytes);
  const std::string ops = R"([
    {"op":"set","block":2,"index":1,"field":"parent","value":"Ghost"},
    {"op":"set","block":2,"index":1,"field":"name","value":"P-C"},
    {"op":"remove","block":2,"index":2}
  ])";
  const auto edited = apply_connect_point_edits_json(nif, ops);
  const auto value = read_connect_parents(read_nif(edited), 2);
  ASSERT_EQ(value.points.size(), 2U);
  EXPECT_EQ(value.points[1].parent, "Ghost");
  EXPECT_EQ(value.points[1].name, "P-C");
}

TEST(NifConnectPoints, ParentsCountLargerThanTheBlockThrows) {
  Writer w;
  w.i32(2);
  w.u32(0xFFFFFFFFU);
  const std::vector<std::string> strings{"Root", "Chest", "CPA"};
  const std::vector<Block> blocks{
      named_node(0, {1}),
      named_node(1, {}),
      Block{"BSConnectPoint::Parents", w.bytes()},
  };
  const auto bytes = nif_build::build_nif(strings, blocks, {0});
  const auto nif = read_nif(bytes);
  expect_parse_error([&] { read_connect_parents(nif, 2); }, "connect point count is larger than the block");
  expect_parse_error(
      [&] { apply_connect_point_edits_json(nif, R"([{"op":"set","block":2,"index":0,"field":"name","value":"x"}])"); },
      "connect point count is larger than the block");
}

TEST(NifConnectPoints, SkinnedByteOtherThanZeroOrOneIsWrittenBackAsOne) {
  Writer w;
  w.i32(3);
  w.u8(2);
  w.u32(1);
  w.sized("C-Barrel");
  const std::vector<std::string> strings{"Root", "Chest", "CPA", "CPT"};
  const std::vector<Block> blocks{
      named_node(0, {1}),
      named_node(1, {}),
      parents_block(2, {muzzle(), grip()}),
      Block{"BSConnectPoint::Children", w.bytes()},
  };
  const auto bytes = nif_build::build_nif(strings, blocks, {0});
  const auto nif = read_nif(bytes);
  EXPECT_TRUE(read_connect_children(nif, 3).skinned);
  const auto edited = apply_connect_point_edits_json(
      nif, R"([{"op":"set","block":3,"index":0,"field":"name","value":"C-Barrel"}])");
  const auto after = read_nif(edited);
  const auto block = block_span(edited, after, 3);
  ASSERT_GT(block.size(), 4U);
  EXPECT_EQ(block[4], std::byte{1});
  EXPECT_TRUE(read_connect_children(after, 3).skinned);
}
