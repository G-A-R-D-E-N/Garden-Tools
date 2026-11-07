#include "fo4/nif/gltf_export.hpp"
#include "fo4/nif/nif.hpp"
#include "gltf_glb_reader.hpp"
#include "gltf_validate.hpp"
#include "nif_build.hpp"

#include <nlohmann/json.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using fo4::nif::Bone;
using fo4::nif::Transform;
using gltf_test::parse_glb;
using gltf_test::read_accessor;
using namespace nif_build;

namespace {

using Matrix = std::array<double, 16>;

Matrix column_major(const float* rotation, const float* translation, float scale) {
  Matrix m{};
  for (std::size_t col = 0; col < 3; ++col) {
    for (std::size_t row = 0; row < 3; ++row) m[col * 4 + row] = scale * rotation[row * 3 + col];
    m[12 + col] = translation[col];
  }
  m[15] = 1.0;
  return m;
}

Matrix multiply(const Matrix& a, const Matrix& b) {
  Matrix out{};
  for (std::size_t col = 0; col < 4; ++col) {
    for (std::size_t row = 0; row < 4; ++row) {
      for (std::size_t k = 0; k < 4; ++k) out[col * 4 + row] += a[k * 4 + row] * b[col * 4 + k];
    }
  }
  return out;
}

constexpr std::size_t k_no_parent = static_cast<std::size_t>(-1);

Matrix local_matrix(const nlohmann::json& doc, std::size_t node) {
  Matrix m{1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0};
  if (!doc.at("nodes").at(node).contains("matrix")) return m;
  const auto values = doc.at("nodes").at(node).at("matrix").get<std::vector<double>>();
  std::copy(values.begin(), values.end(), m.begin());
  return m;
}

std::size_t parent_of(const nlohmann::json& doc, std::size_t node) {
  for (std::size_t i = 0; i < doc.at("nodes").size(); ++i) {
    for (const auto& child : doc.at("nodes").at(i).value("children", nlohmann::json::array())) {
      if (child.get<std::size_t>() == node) return i;
    }
  }
  return k_no_parent;
}

Matrix world_matrix(const nlohmann::json& doc, std::size_t node) {
  Matrix world = local_matrix(doc, node);
  for (auto parent = parent_of(doc, node); parent != k_no_parent; parent = parent_of(doc, parent)) {
    world = multiply(local_matrix(doc, parent), world);
  }
  return world;
}

std::array<double, 3> apply_point(const Matrix& m, const std::array<double, 3>& p) {
  std::array<double, 3> out{};
  for (std::size_t row = 0; row < 3; ++row) out[row] = m[row] * p[0] + m[4 + row] * p[1] + m[8 + row] * p[2] + m[12 + row];
  return out;
}

Block name_block(std::int32_t name) {
  Writer w;
  w.i32(name);
  return Block{"NiStringExtraData", w.bytes()};
}

Block bone_data(const std::vector<Bone>& bones) {
  Writer w;
  w.u32(static_cast<std::uint32_t>(bones.size()));
  for (const auto& bone : bones) {
    for (const float value : bone.center) w.f32(value);
    w.f32(bone.radius);
    for (const float value : bone.rotation) w.f32(value);
    for (const float value : bone.translation) w.f32(value);
    w.f32(bone.scale);
  }
  return Block{"BSSkin::BoneData", w.bytes()};
}

Bone make_bone(const std::array<float, 3>& translation, const std::array<float, 9>& rotation, float scale) {
  Bone bone;
  bone.center[0] = 0.1f;
  bone.center[1] = 0.2f;
  bone.center[2] = 0.3f;
  bone.radius = 1.5f;
  std::ranges::copy(translation, bone.translation);
  std::ranges::copy(rotation, bone.rotation);
  bone.scale = scale;
  return bone;
}

constexpr std::array<float, 9> k_identity{1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};

std::vector<Bone> skinned_bones() {
  return {make_bone({-2.5f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}, 0.5f),
          make_bone({1.0f, 2.0f, 3.0f}, k_identity, 2.0f),
          make_bone({0.0f, 0.0f, 0.0f}, k_identity, 1.0f),
          make_bone({7.0f, 8.0f, 9.0f}, k_identity, 0.5f),
          make_bone({3.0f, 3.0f, 3.0f}, k_identity, 1.0f)};
}

fo4::nif::NifFile skinned_fixture(const std::vector<Bone>& bones) {
  const Transform bone_xf{{0.0f, 5.0f, 0.0f}, {0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}, 2.0f};
  const std::uint32_t flags = k_full | k_has_uv | k_has_normal | k_has_skin;
  const Vert corners[3] = {{{0.0f, 0.0f, 0.0f}, {}}, {{1.0f, 0.0f, 0.0f}, {}}, {{0.0f, 2.0f, 0.0f}, {}}};
  std::vector<std::byte> vertices;
  for (const auto& corner : corners) {
    const auto one = encode_vertex(true, flags, corner);
    vertices.insert(vertices.end(), one.begin(), one.end());
  }
  const auto stride = encode_vertex(true, flags, corners[0]).size();
  return fo4::nif::read_nif(build_nif(
      {"Root", "Bone", "Body", "Hip", "Tail", "Spine"},
      {named_node_block("BSMultiBoundNode", 0, Transform{}, {1, 2}, root_trailer()),
       named_node_block("NiNode", 1, bone_xf, {}),
       shape_with_refs("BSTriShape", 2, Transform{}, flags, stride, 3, -1, -1, vertices, {0, 1, 2}),
       skin_instance_block(4, {1, 5, 0, 6, 7}),
       bone_data(bones),
       name_block(3),
       name_block(4),
       name_block(5)},
      {0}));
}

}  // namespace

TEST(NifGltfExport, SkinnedShapeKeepsVertexCountsNodeNamesAndInverseBinds) {
  // Regression: skin joints or inverse bind matrices came out wrong, or a bone missing from the node tree lost its joint.
  const double metres = 0.0142875;
  const Transform root_xf{{10.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}, 1.0f};
  const Transform bone_xf{{0.0f, 5.0f, 0.0f}, {0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}, 2.0f};
  const Transform shape_xf{{0.0f, 0.0f, 7.0f}, {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}, 1.0f};
  const std::vector<Bone> bones{
      make_bone({1.0f, 2.0f, 3.0f}, {0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}, 2.0f),
      make_bone({-4.0f, 0.0f, 5.0f}, k_identity, 1.0f),
      make_bone({7.0f, 8.0f, 9.0f}, k_identity, 0.5f),
      make_bone({0.5f, -1.5f, 2.25f}, {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f}, 1.25f),
      make_bone({3.0f, 3.0f, 3.0f}, k_identity, 1.0f)};

  const std::uint32_t flags = k_full | k_has_uv | k_has_normal | k_has_skin;
  const Vert corners[3] = {{{0.0f, 0.0f, 0.0f}, {}}, {{1.0f, 0.0f, 0.0f}, {}}, {{0.0f, 2.0f, 0.0f}, {}}};
  std::vector<std::byte> vertices;
  for (const auto& corner : corners) {
    const auto one = encode_vertex(true, flags, corner);
    vertices.insert(vertices.end(), one.begin(), one.end());
  }
  const auto stride = encode_vertex(true, flags, corners[0]).size();

  const auto nif = fo4::nif::read_nif(build_nif(
      {"Root", "Bone", "Body", "Hip", "Tail", "Spine"},
      {named_node_block("BSMultiBoundNode", 0, root_xf, {1, 2}, root_trailer()),
       named_node_block("NiNode", 1, bone_xf, {}),
       shape_with_refs("BSTriShape", 2, shape_xf, flags, stride, 3, -1, -1, vertices, {0, 1, 2}),
       skin_instance_block(4, {1, 5, 0, 6, 7}),
       bone_data(bones),
       name_block(3),
       name_block(4),
       name_block(5)},
      {0}));

  const auto shapes = fo4::nif::read_shapes(nif);
  ASSERT_EQ(shapes.size(), 1U);
  const auto& shape = shapes.front();
  const auto read_bones = fo4::nif::read_bones(nif, 3);
  ASSERT_EQ(read_bones.size(), bones.size());

  const auto glb = parse_glb(fo4::nif::export_glb(nif).glb);
  const auto& doc = glb.json;

  std::vector<std::string> names;
  for (const auto& node : doc.at("nodes")) names.push_back(node.at("name").get<std::string>());
  EXPECT_EQ(names, (std::vector<std::string>{"nif_units_to_metres_z_up_to_y_up", "Root", "Bone", "Body", "Hip", "Tail", "Spine"}));

  const auto root_matrix = doc.at("nodes").at(0).at("matrix").get<std::vector<double>>();
  const std::vector<double> expected_root{metres, 0.0, 0.0, 0.0, 0.0, 0.0, -metres, 0.0, 0.0, metres, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0};
  ASSERT_EQ(root_matrix.size(), 16U);
  for (std::size_t i = 0; i < 16; ++i) EXPECT_NEAR(root_matrix[i], expected_root[i], 1e-7) << "element " << i;
  const auto bone_matrix = column_major(bone_xf.r, bone_xf.t, bone_xf.s);
  EXPECT_EQ(doc.at("nodes").at(2).at("matrix").get<std::vector<double>>(),
            std::vector<double>(bone_matrix.begin(), bone_matrix.end()));
  EXPECT_EQ(doc.at("nodes").at(1).at("children"), nlohmann::json({2, 3}));
  EXPECT_FALSE(doc.at("nodes").at(3).contains("matrix"));

  ASSERT_EQ(doc.at("meshes").size(), 1U);
  ASSERT_EQ(doc.at("skins").size(), 1U);
  const auto& attributes = doc.at("meshes").at(0).at("primitives").at(0).at("attributes");
  const auto vertex_count = doc.at("accessors").at(attributes.at("POSITION").get<std::size_t>()).at("count");
  EXPECT_EQ(vertex_count.get<std::size_t>(), shape.verts.size() / 3);
  EXPECT_EQ(read_accessor(glb, attributes.at("POSITION").get<std::size_t>()),
            std::vector<double>(shape.verts.begin(), shape.verts.end()));
  EXPECT_EQ(read_accessor(glb, attributes.at("NORMAL").get<std::size_t>()),
            std::vector<double>(shape.normals.begin(), shape.normals.end()));
  EXPECT_EQ(read_accessor(glb, attributes.at("TEXCOORD_0").get<std::size_t>()),
            std::vector<double>(shape.uvs.begin(), shape.uvs.end()));
  EXPECT_EQ(read_accessor(glb, attributes.at("JOINTS_0").get<std::size_t>()),
            std::vector<double>(shape.bone_indices.begin(), shape.bone_indices.end()));
  EXPECT_EQ(read_accessor(glb, attributes.at("WEIGHTS_0").get<std::size_t>()),
            std::vector<double>(shape.weights.begin(), shape.weights.end()));
  const auto index_accessor = doc.at("meshes").at(0).at("primitives").at(0).at("indices").get<std::size_t>();
  EXPECT_EQ(read_accessor(glb, index_accessor), std::vector<double>(shape.tris.begin(), shape.tris.end()));

  const auto& skin = doc.at("skins").at(0);
  const auto& joints = skin.at("joints");
  ASSERT_EQ(joints.size(), bones.size());
  const auto inverse_binds = read_accessor(glb, skin.at("inverseBindMatrices").get<std::size_t>());
  ASSERT_EQ(inverse_binds.size(), 16 * bones.size());
  for (std::size_t k = 0; k < bones.size(); ++k) {
    const auto& joint = doc.at("nodes").at(joints.at(k).get<std::size_t>());
    EXPECT_EQ(joint.at("name").get<std::string>(), read_bones[k].name) << "joint " << k;
    const auto expected = column_major(read_bones[k].rotation, read_bones[k].translation, read_bones[k].scale);
    for (std::size_t i = 0; i < 16; ++i) {
      EXPECT_NEAR(inverse_binds[k * 16 + i], expected[i], 1e-5) << "bone " << k << " element " << i;
    }
  }

  for (const auto& [bone, node] : {std::pair<std::size_t, std::size_t>{1, 4}, {3, 5}, {4, 6}}) {
    EXPECT_EQ(joints.at(bone).get<std::size_t>(), node) << "bone " << bone;
    const auto placed = doc.at("nodes").at(node).at("matrix").get<std::vector<double>>();
    Matrix placed_matrix{};
    std::copy(placed.begin(), placed.end(), placed_matrix.begin());
    Matrix ibm{};
    std::copy(inverse_binds.begin() + static_cast<std::ptrdiff_t>(bone * 16),
              inverse_binds.begin() + static_cast<std::ptrdiff_t>(bone * 16 + 16), ibm.begin());
    const auto identity = multiply(placed_matrix, ibm);
    for (std::size_t i = 0; i < 16; ++i) {
      EXPECT_NEAR(identity[i], (i % 5 == 0) ? 1.0 : 0.0, 1e-5) << "bone " << bone << " element " << i;
    }
  }
}

TEST(NifGltfExport, ShapelessNifWritesOnlyTheNodeTreeWithoutABinChunk) {
  // Regression: a NIF with no geometry wrote an empty BIN chunk, which glTF readers reject.
  const auto nif = fo4::nif::read_nif(build_nif({"Root"}, {named_node_block("NiNode", 0, Transform{}, {})}, {0}));
  const auto glb = parse_glb(fo4::nif::export_glb(nif).glb);
  EXPECT_TRUE(glb.bin.empty());
  EXPECT_FALSE(glb.json.contains("accessors"));
  EXPECT_EQ(glb.json.at("nodes").size(), 2U);
}

TEST(NifGltfExport, VertexWorldPositionIsStoredTimesMetresWithTheAxisSwap) {
  // Regression: exported vertices lost the 0.0142875 metres scale or the Z-up to Y-up turn, or scaled the NIF node chain too.
  const double metres = 0.0142875;
  const Transform identity{};
  const Transform group_xf{{2.0f, -3.0f, 4.0f}, {0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}, 2.0f};
  const Transform nested_xf{{0.5f, 0.25f, -1.0f}, {1.0f, 0.0f, 0.0f, 0.0f, 0.6f, -0.8f, 0.0f, 0.8f, 0.6f}, 1.5f};
  const std::uint32_t flags = k_full;
  const Vert corners[3] = {{{0.0f, 0.0f, 0.0f}, {}}, {{1.0f, 0.0f, 0.0f}, {}}, {{0.0f, 2.0f, 1.5f}, {}}};
  std::vector<std::byte> vertices;
  for (const auto& corner : corners) {
    const auto one = encode_vertex(true, flags, corner);
    vertices.insert(vertices.end(), one.begin(), one.end());
  }
  const auto stride = encode_vertex(true, flags, corners[0]).size();

  const auto nif = fo4::nif::read_nif(build_nif(
      {"Root", "Group", "Direct", "Nested"},
      {named_node_block("BSMultiBoundNode", 0, identity, {1, 2}, root_trailer()),
       named_node_block("NiNode", 1, group_xf, {3}),
       shape_with_refs("BSTriShape", 2, identity, flags, stride, -1, -1, -1, vertices, {0, 1, 2}),
       shape_with_refs("BSTriShape", 3, nested_xf, flags, stride, -1, -1, -1, vertices, {0, 1, 2})},
      {0}));
  const auto shapes = fo4::nif::read_shapes(nif);
  ASSERT_EQ(shapes.size(), 2U);

  const auto glb = parse_glb(fo4::nif::export_glb(nif).glb);
  const auto& doc = glb.json;
  ASSERT_EQ(doc.at("nodes").size(), 5U);
  for (std::size_t k = 0; k < shapes.size(); ++k) {
    const std::size_t node = 3 + k;
    ASSERT_EQ(doc.at("nodes").at(node).at("name").get<std::string>(), shapes[k].name);
    const auto transform = fo4::nif::shape_to_global(nif, shapes[k].block);
    const auto& attributes = doc.at("meshes").at(k).at("primitives").at(0).at("attributes");
    const auto positions = read_accessor(glb, attributes.at("POSITION").get<std::size_t>());
    ASSERT_EQ(positions.size(), 9U);
    const auto world = world_matrix(doc, node);
    for (std::size_t v = 0; v < 3; ++v) {
      const std::array<double, 3> stored{positions[v * 3], positions[v * 3 + 1], positions[v * 3 + 2]};
      std::array<double, 3> nif_world{};
      for (std::size_t i = 0; i < 3; ++i) {
        nif_world[i] = transform.s * (transform.r[i * 3] * stored[0] + transform.r[i * 3 + 1] * stored[1] +
                                      transform.r[i * 3 + 2] * stored[2]) + transform.t[i];
      }
      const std::array<double, 3> expected{metres * nif_world[0], metres * nif_world[2], -metres * nif_world[1]};
      const auto got = apply_point(world, stored);
      for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_NEAR(got[i], expected[i], 1e-5) << shapes[k].name << " vertex " << v << " axis " << i;
      }
    }
  }
}

// Detects: skin inverse binds written in metres, or a joint placed outside the scaled root, so skinned vertices scale twice or not at all.
TEST(NifGltfExport, SkinnedJointChainTimesInverseBindIsTheMetresRootMatrix) {
  const double metres = 0.0142875;
  const Matrix expected_root{metres, 0.0, 0.0, 0.0, 0.0, 0.0, -metres, 0.0, 0.0, metres, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0};
  const auto glb = parse_glb(fo4::nif::export_glb(skinned_fixture(skinned_bones())).glb);
  const auto& doc = glb.json;

  const auto& skin = doc.at("skins").at(0);
  const auto& joints = skin.at("joints");
  const auto inverse_binds = read_accessor(glb, skin.at("inverseBindMatrices").get<std::size_t>());
  ASSERT_EQ(joints.size(), 5U);
  ASSERT_EQ(inverse_binds.size(), 16U * 5U);
  for (std::size_t k = 0; k < joints.size(); ++k) {
    Matrix ibm{};
    std::copy(inverse_binds.begin() + static_cast<std::ptrdiff_t>(k * 16),
              inverse_binds.begin() + static_cast<std::ptrdiff_t>(k * 16 + 16), ibm.begin());
    const auto skin_matrix = multiply(world_matrix(doc, joints.at(k).get<std::size_t>()), ibm);
    for (std::size_t i = 0; i < 16; ++i) {
      EXPECT_NEAR(skin_matrix[i], expected_root[i], 1e-5) << "joint " << k << " element " << i;
    }
  }
}

// Detects: a NaN node translation written as JSON null in the node matrix, so the export reported OK with an invalid .glb.
TEST(NifGltfExport, NonFiniteNodeTransformIsRefusedInsteadOfWrittenAsNull) {
  Transform bad{};
  bad.t[0] = std::numeric_limits<float>::quiet_NaN();
  const auto nif = fo4::nif::read_nif(build_nif({"Root", "Node"},
                                                {named_node_block("NiNode", 0, Transform{}, {1}),
                                                 named_node_block("NiNode", 1, bad, {})},
                                                {0}));
  expect_parse_error([&] { static_cast<void>(fo4::nif::export_glb(nif)); }, "transform is not finite");
}

// Detects: a NaN bone translation written as JSON null in inverseBindMatrices, so a skinned export reported OK with an invalid .glb.
TEST(NifGltfExport, NonFiniteBoneTransformIsRefusedInsteadOfWrittenAsNull) {
  auto bones = skinned_bones();
  bones[1].translation[0] = std::numeric_limits<float>::quiet_NaN();
  const auto nif = skinned_fixture(bones);
  expect_parse_error([&] { static_cast<void>(fo4::nif::export_glb(nif)); }, "transform is not finite");
}

// Detects: a skinned export whose joint, inverse bind or accessor counts must stay consistent, so a bad skin would load as an empty or misbound mesh.
TEST(NifGltfExport, SkinnedExportMustPassTheStructuralValidator) {
  const auto glb = parse_glb(fo4::nif::export_glb(skinned_fixture(skinned_bones())).glb);
  EXPECT_TRUE(gltf_test::validate_glb(glb).empty()) << gltf_test::validate_glb(glb).front();
}

namespace {

std::vector<std::byte> framed_vertex(float x, std::uint8_t bitangent_y) {
  Writer w;
  w.f32(x);
  w.f32(0.0f);
  w.f32(0.0f);
  w.f32(0.0f);
  w.u8(128);
  w.u8(128);
  w.u8(255);
  w.u8(bitangent_y);
  w.u8(255);
  w.u8(128);
  w.u8(128);
  w.u8(128);
  return w.bytes();
}

}  // namespace

// Detects: a TANGENT w that must carry the handedness of the stored bitangent, and tangents=false that must leave TANGENT out.
TEST(NifGltfExport, TangentWMustCarryTheHandednessOfTheStoredBitangent) {
  const std::uint32_t flags = k_full | k_has_normal | k_has_tangent;
  std::vector<std::byte> vertices;
  for (const auto& one : {framed_vertex(0.0f, 255), framed_vertex(1.0f, 0), framed_vertex(2.0f, 255)}) {
    vertices.insert(vertices.end(), one.begin(), one.end());
  }
  const auto nif = fo4::nif::read_nif(build_nif({"Root", "Shape"},
                                                {named_node_block("BSMultiBoundNode", 0, Transform{}, {1}, root_trailer()),
                                                 shape_with_refs("BSTriShape", 1, Transform{}, flags, 24, -1, -1, -1, vertices,
                                                                 {0, 1, 2})},
                                                {0}));

  const auto glb = parse_glb(fo4::nif::export_glb(nif).glb);
  const auto& attributes = glb.json.at("meshes").at(0).at("primitives").at(0).at("attributes");
  const auto tangents = read_accessor(glb, attributes.at("TANGENT").get<std::size_t>());
  ASSERT_EQ(tangents.size(), 12U);
  for (std::size_t v = 0; v < 3; ++v) {
    EXPECT_NEAR(tangents[v * 4], 1.0, 1e-2) << "vertex " << v << " tangent x";
    EXPECT_NEAR(tangents[v * 4 + 1], 0.0, 1e-2) << "vertex " << v << " tangent y";
  }
  EXPECT_EQ(tangents[3], 1.0);
  EXPECT_EQ(tangents[7], -1.0);
  EXPECT_EQ(tangents[11], 1.0);

  fo4::nif::GltfOptions off;
  off.tangents = false;
  const auto plain = parse_glb(fo4::nif::export_glb(nif, off).glb);
  EXPECT_FALSE(plain.json.at("meshes").at(0).at("primitives").at(0).at("attributes").contains("TANGENT"));
}

// Detects: a vertex colour that must export as COLOR_0 with the stored RGBA values, and vertex_colours=false that must leave COLOR_0 out.
TEST(NifGltfExport, VertexColoursMustExportAsColor0AndTheOptionMustSkipIt) {
  const std::uint32_t flags = k_full | k_has_colour;
  const Vert corners[3] = {{{0.0f, 0.0f, 0.0f}, {}}, {{1.0f, 0.0f, 0.0f}, {}}, {{0.0f, 2.0f, 0.0f}, {}}};
  std::vector<std::byte> vertices;
  for (const auto& corner : corners) {
    const auto one = encode_vertex(true, flags, corner);
    vertices.insert(vertices.end(), one.begin(), one.end());
  }
  const auto stride = encode_vertex(true, flags, corners[0]).size();
  const auto nif = fo4::nif::read_nif(build_nif({"Root", "Shape"},
                                                {named_node_block("BSMultiBoundNode", 0, Transform{}, {1}, root_trailer()),
                                                 shape_with_refs("BSTriShape", 1, Transform{}, flags, stride, -1, -1, -1,
                                                                 vertices, {0, 1, 2})},
                                                {0}));
  const auto shapes = fo4::nif::read_shapes(nif);
  ASSERT_EQ(shapes.size(), 1U);
  ASSERT_EQ(shapes.front().colors.size(), 12U);

  const auto glb = parse_glb(fo4::nif::export_glb(nif).glb);
  const auto& attributes = glb.json.at("meshes").at(0).at("primitives").at(0).at("attributes");
  EXPECT_EQ(read_accessor(glb, attributes.at("COLOR_0").get<std::size_t>()),
            std::vector<double>(shapes.front().colors.begin(), shapes.front().colors.end()));
  EXPECT_NEAR(read_accessor(glb, attributes.at("COLOR_0").get<std::size_t>())[2], 51.0 / 255.0, 1e-6);

  fo4::nif::GltfOptions off;
  off.vertex_colours = false;
  const auto plain = parse_glb(fo4::nif::export_glb(nif, off).glb);
  EXPECT_FALSE(plain.json.at("meshes").at(0).at("primitives").at(0).at("attributes").contains("COLOR_0"));
}
