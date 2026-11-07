#include "fo4/error.hpp"
#include "fo4/nif/collision.hpp"
#include "fo4/nif/hknp_decode.hpp"
#include "fo4/nif/hknp_encode.hpp"
#include "fo4/nif/hknp_packfile.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_import.hpp"
#include "gltf_import_builder.hpp"
#include "nif_build.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using fo4::nif::ImportOptions;
using fo4::nif::NifFile;
using fo4::nif::Shape;
using fo4::nif::Transform;
using fo4::nif::import_gltf;
using fo4::nif::read_nif;
using fo4::nif::read_shapes;
using gltf_import_test::add_accessor;
using gltf_import_test::add_indices;
using gltf_import_test::add_mesh;
using gltf_import_test::add_node;
using gltf_import_test::add_positions;
using gltf_import_test::empty_gltf;
using gltf_import_test::Gltf;
using gltf_import_test::k_float;
using gltf_import_test::to_glb;

namespace {

constexpr float k_metres = 0.0142875f;
constexpr float k_tolerance = 1e-3f;

std::vector<std::byte> import_glb(Gltf& g, const ImportOptions& options = {}) {
  return import_gltf(to_glb(g), options);
}

const fo4::nif::Node& node_named(const NifFile& nif, std::string_view name) {
  for (const auto& node : nif.nodes()) {
    if (node.name == name) return node;
  }
  throw std::runtime_error("no NIF node named " + std::string(name));
}

std::int32_t node_index(const NifFile& nif, std::string_view name) {
  const auto& found = node_named(nif, name);
  return static_cast<std::int32_t>(&found - nif.nodes().data());
}

std::array<float, 3> apply_rotation(const Transform& xf, const std::array<float, 3>& v) {
  std::array<float, 3> out{};
  for (std::size_t row = 0; row < 3; ++row) {
    out[row] = xf.r[row * 3] * v[0] + xf.r[row * 3 + 1] * v[1] + xf.r[row * 3 + 2] * v[2];
  }
  return out;
}

Shape only_shape(const NifFile& nif) {
  const auto shapes = read_shapes(nif);
  EXPECT_EQ(shapes.size(), 1U);
  return shapes.empty() ? Shape{} : shapes.front();
}

}

// Detects: a glTF vertex in metres, Y-up, must land at (x, -z, y) divided by the metres-per-unit scale, or every imported shape is turned or scaled wrong.
TEST(GltfImport, YUpMetresBecomeZUpNifUnitsOnEveryVertex) {
  auto g = empty_gltf();
  const auto positions = add_positions(g, {1.0f, 2.0f, 3.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
  const auto mesh = add_mesh(g, "Body", {{{"attributes", {{"POSITION", positions}}}, {"indices", add_indices(g, {0, 1, 2})}}});
  add_node(g, {{"name", "Body"}, {"mesh", mesh}});

  const auto shape = only_shape(read_nif(import_glb(g)));
  ASSERT_EQ(shape.verts.size(), 9U);
  const std::vector<float> expected{1.0f / k_metres, -3.0f / k_metres, 2.0f / k_metres, 0.0f, 0.0f, 0.0f,
                                    0.0f, 0.0f, 1.0f / k_metres};
  for (std::size_t i = 0; i < expected.size(); ++i) EXPECT_NEAR(shape.verts[i], expected[i], k_tolerance) << i;
  EXPECT_EQ(shape.tris, (std::vector<std::uint16_t>{0, 1, 2}));
}

// Detects: a child node's transform must stay its local transform and its parent link must stay, or the NIF hierarchy no longer matches the glTF one.
TEST(GltfImport, ChildNodeKeepsItsParentAndItsLocalTransformWithTheScaleKept) {
  auto g = empty_gltf();
  const auto mesh = add_mesh(g, "Leaf", {{{"attributes", {{"POSITION", add_positions(g, {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f})}}}}});
  const auto child = add_node(g, {{"name", "Child"}, {"translation", {0.0, 1.0, 0.0}}, {"scale", {2.0, 2.0, 2.0}}, {"mesh", mesh}});
  add_node(g, {{"name", "Parent"}, {"translation", {1.0, 0.0, 0.0}}, {"children", {child}}});

  const auto nif = read_nif(import_glb(g));
  const auto& parent = node_named(nif, "Parent");
  const auto& leaf = node_named(nif, "Child");
  EXPECT_NEAR(parent.local.t[0], 1.0f / k_metres, k_tolerance);
  EXPECT_NEAR(parent.local.t[1], 0.0f, k_tolerance);
  EXPECT_NEAR(leaf.local.t[2], 1.0f / k_metres, k_tolerance);
  EXPECT_NEAR(leaf.local.s, 2.0f, k_tolerance);
  EXPECT_EQ(leaf.parent, node_index(nif, "Parent"));
  EXPECT_EQ(only_shape(nif).parent_node, node_index(nif, "Child"));
}

// Detects: a TRS node's rotation must turn the NIF axes with the frame, or a turned glTF node lands unturned in the NIF.
TEST(GltfImport, TrsRotationOfANodeTurnsItsNifAxesWithTheFrame) {
  auto g = empty_gltf();
  const auto mesh = add_mesh(g, "Turned", {{{"attributes", {{"POSITION", add_positions(g, {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f})}}}}});
  const double half = std::sqrt(0.5);
  add_node(g, {{"name", "Turned"}, {"rotation", {0.0, 0.0, half, half}}, {"mesh", mesh}});

  const auto nif = read_nif(import_glb(g));
  const auto& node = node_named(nif, "Turned");
  const auto x = apply_rotation(node.local, {1.0f, 0.0f, 0.0f});
  const auto y = apply_rotation(node.local, {0.0f, 1.0f, 0.0f});
  EXPECT_NEAR(x[2], 1.0f, 1e-4f);
  EXPECT_NEAR(y[0], 0.0f, 1e-4f);
  EXPECT_NEAR(y[1], 1.0f, 1e-4f);
}

// Detects: a multi-primitive mesh must become one shape per primitive with distinct names, or a glTF primitive is lost or unnamed.
TEST(GltfImport, EachPrimitiveBecomesItsOwnShapeUnderTheMeshNode) {
  auto g = empty_gltf();
  const auto positions = add_positions(g, {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
  const auto mesh = add_mesh(g, "Body",
                             {{{"attributes", {{"POSITION", positions}}}, {"indices", add_indices(g, {0, 1, 2})}},
                              {{"attributes", {{"POSITION", positions}}}, {"indices", add_indices(g, {2, 1, 0})}}});
  add_node(g, {{"name", "Holder"}, {"mesh", mesh}});

  const auto nif = read_nif(import_glb(g));
  const auto shapes = read_shapes(nif);
  ASSERT_EQ(shapes.size(), 2U);
  EXPECT_EQ(shapes[0].name, "Body");
  EXPECT_EQ(shapes[1].name, "Body_1");
  EXPECT_EQ(shapes[0].parent_node, node_index(nif, "Holder"));
  EXPECT_EQ(shapes[1].parent_node, node_index(nif, "Holder"));
  EXPECT_EQ(shapes[1].tris, (std::vector<std::uint16_t>{2, 1, 0}));
}

// Detects: normals and UVs must come from their own accessors and triangles must keep their order, or the imported surface differs from the glTF one.
TEST(GltfImport, NormalsUvsAndTrianglesKeepTheirValuesInTheNifFrame) {
  auto g = empty_gltf();
  const auto positions = add_positions(g, {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f});
  const auto normals = add_accessor(g, k_float, 4, "VEC3", gltf_import_test::float_bytes({0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f}));
  const auto uvs = add_accessor(g, k_float, 4, "VEC2", gltf_import_test::float_bytes({0.0f, 0.0f, 0.5f, 0.25f, 1.0f, 1.0f, 0.0f, 0.5f}));
  const auto mesh = add_mesh(g, "Quad", {{{"attributes", {{"POSITION", positions}, {"NORMAL", normals}, {"TEXCOORD_0", uvs}}},
                                           {"indices", add_indices(g, {0, 1, 2, 0, 2, 3})}}});
  add_node(g, {{"name", "Quad"}, {"mesh", mesh}});

  const auto shape = only_shape(read_nif(import_glb(g)));
  EXPECT_EQ(shape.tris, (std::vector<std::uint16_t>{0, 1, 2, 0, 2, 3}));
  EXPECT_EQ(shape.uvs, (std::vector<float>{0.0f, 0.0f, 0.5f, 0.25f, 1.0f, 1.0f, 0.0f, 0.5f}));
  ASSERT_EQ(shape.normals.size(), 12U);
  for (std::size_t v = 0; v < 4; ++v) {
    EXPECT_NEAR(shape.normals[3 * v + 2], 1.0f, 0.01f) << "vertex " << v;
    EXPECT_NEAR(shape.normals[3 * v + 1], 0.0f, 0.01f) << "vertex " << v;
  }
}

// Detects: a glTF without NORMAL must get normals computed from its faces, or the shape has no usable lighting.
TEST(GltfImport, MissingNormalsAreComputedFromTheFaces) {
  auto g = empty_gltf();
  const auto mesh = add_mesh(g, "Flat", {{{"attributes", {{"POSITION", add_positions(g, {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f})}}}}});
  add_node(g, {{"name", "Flat"}, {"mesh", mesh}});

  const auto shape = only_shape(read_nif(import_glb(g)));
  ASSERT_EQ(shape.normals.size(), 9U);
  for (std::size_t v = 0; v < 3; ++v) {
    EXPECT_NEAR(shape.normals[3 * v + 1], -1.0f, 0.01f) << "vertex " << v;
  }
}

// Detects: a file's TANGENT must be kept and its w sign must set the bitangent, or the normal map flips.
TEST(GltfImport, FileTangentsAreKeptAndTheirSignSetsTheBitangent) {
  auto g = empty_gltf();
  const auto positions = add_positions(g, {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
  const auto normals = add_accessor(g, k_float, 3, "VEC3", gltf_import_test::float_bytes({0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f}));
  const auto tangents = add_accessor(g, k_float, 3, "VEC4", gltf_import_test::float_bytes({1.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, -1.0f}));
  const auto mesh = add_mesh(g, "Tangent", {{{"attributes", {{"POSITION", positions}, {"NORMAL", normals}, {"TANGENT", tangents}}}, {"indices", add_indices(g, {0, 1, 2})}}});
  add_node(g, {{"name", "Tangent"}, {"mesh", mesh}});

  const auto shape = only_shape(read_nif(import_glb(g)));
  ASSERT_EQ(shape.tangents.size(), 9U);
  ASSERT_EQ(shape.bitangents.size(), 9U);
  EXPECT_NEAR(shape.tangents[0], 1.0f, 0.01f);
  EXPECT_NEAR(shape.tangents[1], 0.0f, 0.01f);
  EXPECT_NEAR(shape.bitangents[0], 0.0f, 0.01f);
  EXPECT_NEAR(shape.bitangents[1], -1.0f, 0.01f);
}

// Detects: vertex colours must be written per vertex and clamped to 0..1, or the shape's colours no longer match the file.
TEST(GltfImport, VertexColoursAreClampedAndWrittenPerVertex) {
  auto g = empty_gltf();
  const auto positions = add_positions(g, {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
  const auto colours = add_accessor(g, k_float, 3, "VEC4", gltf_import_test::float_bytes({1.0f, 0.5f, 0.0f, 1.0f, 2.0f, -1.0f, 0.25f, 1.0f, 0.0f, 0.0f, 0.0f, 0.5f}));
  const auto mesh = add_mesh(g, "Paint", {{{"attributes", {{"POSITION", positions}, {"COLOR_0", colours}}}, {"indices", add_indices(g, {0, 1, 2})}}});
  add_node(g, {{"name", "Paint"}, {"mesh", mesh}});

  const auto shape = only_shape(read_nif(import_glb(g)));
  ASSERT_EQ(shape.colors.size(), 12U);
  const std::vector<float> expected{1.0f, 0.5f, 0.0f, 1.0f, 1.0f, 0.0f, 0.25f, 1.0f, 0.0f, 0.0f, 0.0f, 0.5f};
  for (std::size_t i = 0; i < expected.size(); ++i) EXPECT_NEAR(shape.colors[i], expected[i], 0.005f) << i;
}

// Detects: a collision box must be sized from world positions, or a moved node is left outside its own box.
TEST(GltfImport, BoxCollisionCoversTheWorldPositionOfAMovedNode) {
  auto g = empty_gltf();
  const auto mesh = add_mesh(g, "Moved", {{{"attributes", {{"POSITION", add_positions(g, {0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f})}}}, {"indices", add_indices(g, {0, 1, 2})}}});
  add_node(g, {{"name", "Moved"}, {"translation", {1.0, 0.0, 0.0}}, {"mesh", mesh}});
  ImportOptions options;
  options.box_collision = true;

  const auto nif = read_nif(import_glb(g, options));
  const auto blobs = fo4::nif::read_collision_blobs(nif);
  ASSERT_EQ(blobs.size(), 1U);
  const auto system = fo4::nif::decode_hknp_system(fo4::nif::read_hknp_packfile(blobs[0].bytes));
  ASSERT_EQ(system.bodies.size(), 1U);
  const auto& shape = system.shapes[static_cast<std::size_t>(system.bodies[0].shape)];
  const auto scale = fo4::nif::k_hknp_game_units_per_metre;
  const std::array<float, 3> low{70.0f, -70.0f, 0.0f};
  const std::array<float, 3> high{70.0f, 0.0f, 70.0f};
  for (std::size_t k = 0; k < 3; ++k) {
    float vmin = shape.vertices[0][k];
    float vmax = shape.vertices[0][k];
    for (const auto& v : shape.vertices) {
      vmin = std::min(vmin, v[k]);
      vmax = std::max(vmax, v[k]);
    }
    if (k == 0) continue;
    EXPECT_NEAR((vmin - shape.convex_radius) * scale, low[k], 1.0f) << "axis " << k;
    EXPECT_NEAR((vmax + shape.convex_radius) * scale, high[k], 1.0f) << "axis " << k;
  }
}

// Detects: a 1079 export must be read raw under its NIF-units root, or the frame is applied a second time and every vertex scales by 70.
TEST(GltfImport, ExporterNifUnitsRootIsReadRawSoRoundTripPositionsAreUnchanged) {
  const float m = k_metres;
  auto g = empty_gltf();
  const auto mesh = add_mesh(g, "Raw", {{{"attributes", {{"POSITION", add_positions(g, {0.0f, 0.0f, 7.0f, 1.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f})}}}, {"indices", add_indices(g, {0, 1, 2})}}});
  const auto body = add_node(g, {{"name", "Body"}, {"mesh", mesh}});
  const auto scene = add_node(g, {{"name", "Scene Root"}, {"children", {body}}});
  add_node(g, {{"name", "nif_units_to_metres_z_up_to_y_up"},
               {"matrix", {m, 0.0, 0.0, 0.0, 0.0, 0.0, -m, 0.0, 0.0, m, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0}},
               {"children", {scene}}});

  const auto nif = read_nif(import_glb(g));
  EXPECT_EQ(only_shape(nif).verts, (std::vector<float>{0.0f, 0.0f, 7.0f, 1.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f}));
  EXPECT_EQ(node_named(nif, "Body").local.s, 1.0f);
}

// Detects: a node with non-uniform scale must be refused by name, or the stretch is silently dropped and the mesh lands in the wrong shape.
TEST(GltfImport, NonUniformScaleIsRefusedByName) {
  auto g = empty_gltf();
  const auto mesh = add_mesh(g, "Stretched", {{{"attributes", {{"POSITION", add_positions(g, {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f})}}}, {"indices", add_indices(g, {0, 1, 2})}}});
  add_node(g, {{"name", "Stretched"}, {"scale", {1.0, 2.0, 1.0}}, {"mesh", mesh}});
  try {
    static_cast<void>(import_glb(g));
    FAIL() << "expected a refusal for a non-uniform scale";
  } catch (const std::runtime_error& error) {
    EXPECT_NE(std::string(error.what()).find("non-uniform scale"), std::string::npos) << error.what();
  }
}

// Detects: a mirrored node must be refused by name, or its handedness flips silently and the mesh is inside out.
TEST(GltfImport, MirroredNodeIsRefusedByName) {
  auto g = empty_gltf();
  const auto mesh = add_mesh(g, "Mirror", {{{"attributes", {{"POSITION", add_positions(g, {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f})}}}, {"indices", add_indices(g, {0, 1, 2})}}});
  add_node(g, {{"name", "Mirror"}, {"scale", {-1.0, 1.0, 1.0}}, {"mesh", mesh}});
  try {
    static_cast<void>(import_glb(g));
    FAIL() << "expected a refusal for a mirrored node";
  } catch (const std::runtime_error& error) {
    EXPECT_NE(std::string(error.what()).find("mirrored"), std::string::npos) << error.what();
  }
}

// Detects: a glTF with no triangle mesh must be refused, or it imports as an empty NIF that reports success.
TEST(GltfImport, FileWithNoMeshesIsRefused) {
  auto g = empty_gltf();
  add_node(g, {{"name", "Empty"}});
  EXPECT_THROW(static_cast<void>(import_glb(g)), std::runtime_error);
}

// Detects: a .gltf data: URI buffer must be decoded, or a self-contained JSON file imports as nothing or fails.
TEST(GltfImport, GltfJsonWithADataUriBufferImportsLikeTheGlb) {
  auto g = empty_gltf();
  const auto mesh = add_mesh(g, "Json", {{{"attributes", {{"POSITION", add_positions(g, {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f})}}}, {"indices", add_indices(g, {0, 1, 2})}}});
  add_node(g, {{"name", "Json"}, {"mesh", mesh}});
  const auto text = gltf_import_test::to_gltf_text(g);

  const auto from_text = read_nif(import_gltf(gltf_import_test::bytes_of(text), {}));
  EXPECT_EQ(only_shape(from_text).tris, (std::vector<std::uint16_t>{0, 1, 2}));
}

// Detects: an external .bin must be refused by name when no loader is given, or the mesh comes out empty.
TEST(GltfImport, ExternalBufferWithoutALoaderIsRefusedByName) {
  auto g = empty_gltf();
  const auto mesh = add_mesh(g, "External", {{{"attributes", {{"POSITION", add_positions(g, {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f})}}}, {"indices", add_indices(g, {0, 1, 2})}}});
  add_node(g, {{"name", "External"}, {"mesh", mesh}});
  g.json["buffers"] = nlohmann::json::array({{{"byteLength", g.bin.size()}, {"uri", "scene.bin"}}});
  const auto text = g.json.dump();
  try {
    static_cast<void>(import_gltf(gltf_import_test::bytes_of(text), {}));
    FAIL() << "expected a refusal for an external buffer";
  } catch (const std::runtime_error& error) {
    EXPECT_NE(std::string(error.what()).find("external file"), std::string::npos) << error.what();
  }
}

// Detects: an external .bin the loader returns must reach the importer, or a .gltf with separate buffers never imports.
TEST(GltfImport, ExternalBufferIsReadThroughTheLoader) {
  auto g = empty_gltf();
  const auto mesh = add_mesh(g, "Loaded", {{{"attributes", {{"POSITION", add_positions(g, {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f})}}}, {"indices", add_indices(g, {0, 1, 2})}}});
  add_node(g, {{"name", "Loaded"}, {"mesh", mesh}});
  const auto bin = g.bin;
  g.json["buffers"] = nlohmann::json::array({{{"byteLength", bin.size()}, {"uri", "my%20scene.bin"}}});
  std::string requested;
  const fo4::nif::GltfUriLoader loader = [&](std::string_view uri) {
    requested = std::string(uri);
    return bin;
  };

  const auto nif = read_nif(import_gltf(gltf_import_test::bytes_of(g.json.dump()), {}, loader));
  EXPECT_EQ(requested, "my scene.bin");
  EXPECT_EQ(only_shape(nif).tris, (std::vector<std::uint16_t>{0, 1, 2}));
}

// Detects: an accessor whose count runs past its buffer must be refused before any read, or a corrupt file reads outside its bytes.
TEST(GltfImport, AccessorCountPastItsBufferIsRefusedBeforeReading) {
  auto g = empty_gltf();
  const auto positions = add_positions(g, {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
  g.json["accessors"][positions]["count"] = 1000000;
  const auto mesh = add_mesh(g, "Corrupt", {{{"attributes", {{"POSITION", positions}}}, {"indices", add_indices(g, {0, 1, 2})}}});
  add_node(g, {{"name", "Corrupt"}, {"mesh", mesh}});
  try {
    static_cast<void>(import_glb(g));
    FAIL() << "expected a refusal for an accessor past its buffer";
  } catch (const std::runtime_error& error) {
    EXPECT_NE(std::string(error.what()).find("runs past the end of its buffer"), std::string::npos) << error.what();
  }
}

// Detects: a second colour set must be refused by name, or the colours of the second set are silently dropped.
TEST(GltfImport, SecondVertexColourSetIsRefusedByName) {
  auto g = empty_gltf();
  const auto positions = add_positions(g, {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
  const auto mesh = add_mesh(g, "Paint", {{{"attributes", {{"POSITION", positions}, {"COLOR_1", positions}}}, {"indices", add_indices(g, {0, 1, 2})}}});
  add_node(g, {{"name", "Paint"}, {"mesh", mesh}});
  try {
    static_cast<void>(import_glb(g));
    FAIL() << "expected a refusal for a second colour set";
  } catch (const std::runtime_error& error) {
    EXPECT_NE(std::string(error.what()).find("second vertex colour set"), std::string::npos) << error.what();
  }
}
