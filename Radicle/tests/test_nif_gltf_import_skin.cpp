#include "fo4/nif/nif.hpp"
#include "fo4/nif/nif_import.hpp"
#include "gltf_import_builder.hpp"
#include "nif_build.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using fo4::nif::ImportOptions;
using fo4::nif::NifFile;
using fo4::nif::Shape;
using fo4::nif::Transform;
using fo4::nif::import_gltf;
using fo4::nif::read_bones;
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
using gltf_import_test::k_u8;
using gltf_import_test::to_glb;

namespace {

constexpr float k_metres = 0.0142875f;

const Transform k_bone_a{{0.0f, 5.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}, 1.0f};
const Transform k_bone_b{{0.0f, 0.0f, 3.0f}, {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}, 1.0f};

std::vector<std::byte> skeleton_nif() {
  return nif_build::build_nif({"Root", "Bone_A", "Bone_B"},
                              {nif_build::named_node_block("NiNode", 0, Transform{}, {1, 2}),
                               nif_build::named_node_block("NiNode", 1, k_bone_a, {}),
                               nif_build::named_node_block("NiNode", 2, k_bone_b, {})},
                              {0});
}

ImportOptions with_skeleton() {
  ImportOptions options;
  options.skeleton = skeleton_nif();
  return options;
}

ImportOptions with_five_bones() {
  ImportOptions options;
  options.skeleton = nif_build::build_nif({"Root", "Bone_A", "Bone_B", "Bone_C", "Bone_D", "Bone_E"},
                                          {nif_build::named_node_block("NiNode", 0, Transform{}, {1, 2, 3, 4, 5}),
                                           nif_build::named_node_block("NiNode", 1, k_bone_a, {}),
                                           nif_build::named_node_block("NiNode", 2, k_bone_b, {}),
                                           nif_build::named_node_block("NiNode", 3, k_bone_a, {}),
                                           nif_build::named_node_block("NiNode", 4, k_bone_b, {}),
                                           nif_build::named_node_block("NiNode", 5, k_bone_a, {})},
                                          {0});
  return options;
}

Shape only_shape(const NifFile& nif) {
  const auto shapes = read_shapes(nif);
  EXPECT_EQ(shapes.size(), 1U);
  return shapes.empty() ? Shape{} : shapes.front();
}

Gltf triangle(const std::string& name = "Body") {
  auto g = empty_gltf();
  const auto mesh = add_mesh(g, name, {{{"attributes", {{"POSITION", add_positions(g, {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f})}}}, {"indices", add_indices(g, {0, 1, 2})}}});
  add_node(g, {{"name", name}, {"mesh", mesh}});
  return g;
}

// Requires a skin of two joints named by joint_a and joint_b, with an inverse bind per joint when ibm is non-empty.
Gltf skinned_triangle(const std::string& joint_a, const std::string& joint_b, const std::vector<float>& ibm) {
  auto g = empty_gltf();
  const auto positions = add_positions(g, {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
  const auto joints0 = add_accessor(g, k_u8, 3, "VEC4", gltf_import_test::u8_bytes({0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0}));
  const auto weights0 = add_accessor(g, k_float, 3, "VEC4", gltf_import_test::float_bytes({0.5f, 0.25f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.2f, 0.2f, 0.0f, 0.0f}));
  const auto joints1 = add_accessor(g, k_u8, 3, "VEC4", gltf_import_test::u8_bytes({0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0}));
  const auto weights1 = add_accessor(g, k_float, 3, "VEC4", gltf_import_test::float_bytes({0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}));
  const auto mesh = add_mesh(g, "Body", {{{"attributes", {{"POSITION", positions}, {"JOINTS_0", joints0}, {"WEIGHTS_0", weights0}, {"JOINTS_1", joints1}, {"WEIGHTS_1", weights1}}}, {"indices", add_indices(g, {0, 1, 2})}}});
  const auto a = add_node(g, {{"name", joint_a}});
  const auto b = add_node(g, {{"name", joint_b}});
  add_node(g, {{"name", "Body"}, {"mesh", mesh}, {"skin", 0}});
  nlohmann::json skin{{"joints", {a, b}}};
  if (!ibm.empty()) skin["inverseBindMatrices"] = add_accessor(g, k_float, 2, "MAT4", gltf_import_test::float_bytes(ibm));
  g.json["skins"] = nlohmann::json::array({skin});
  return g;
}

std::vector<float> identity_with_translation(float x, float y, float z) {
  return {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, x, y, z, 1.0f};
}

void expect_refusal(Gltf& g, const std::string& fragment, const ImportOptions& options = {}) {
  try {
    static_cast<void>(import_gltf(to_glb(g), options));
    FAIL() << "expected a refusal containing \"" << fragment << "\"";
  } catch (const std::runtime_error& error) {
    EXPECT_NE(std::string(error.what()).find(fragment), std::string::npos) << error.what();
  }
}

}

// Detects: a skinned vertex must keep its positive influences normalised to one with the right bone indices, or the game deforms the mesh wrongly.
TEST(GltfImportSkin, SkinnedVertexKeepsItsPositiveInfluencesNormalisedToOne) {
  auto g = skinned_triangle("Bone_A", "Bone_B", {});
  const auto nif = read_nif(import_gltf(to_glb(g), with_skeleton()));
  const auto shape = only_shape(nif);
  ASSERT_NE(shape.skin_ref, -1);
  const auto bones = read_bones(nif, static_cast<std::uint32_t>(shape.skin_ref));
  ASSERT_EQ(bones.size(), 2U);
  EXPECT_EQ(bones[0].name, "Bone_A");
  EXPECT_EQ(bones[1].name, "Bone_B");

  ASSERT_EQ(shape.bone_indices.size(), 12U);
  EXPECT_EQ(std::vector<std::uint8_t>(shape.bone_indices.begin(), shape.bone_indices.begin() + 4),
            (std::vector<std::uint8_t>{0, 1, 0, 0}));
  EXPECT_EQ(std::vector<std::uint8_t>(shape.bone_indices.begin() + 4, shape.bone_indices.begin() + 8),
            (std::vector<std::uint8_t>{1, 0, 0, 0}));
  EXPECT_EQ(std::vector<std::uint8_t>(shape.bone_indices.begin() + 8, shape.bone_indices.end()),
            (std::vector<std::uint8_t>{0, 1, 0, 0}));
  EXPECT_NEAR(shape.weights[0], 2.0f / 3.0f, 0.002f);
  EXPECT_NEAR(shape.weights[1], 1.0f / 3.0f, 0.002f);
  EXPECT_NEAR(shape.weights[2], 0.0f, 0.002f);
  EXPECT_NEAR(shape.weights[4], 1.0f, 0.002f);
  EXPECT_NEAR(shape.weights[8], 0.5f, 0.002f);
  EXPECT_NEAR(shape.weights[9], 0.5f, 0.002f);
  EXPECT_NEAR(shape.weights[10], 0.0f, 0.002f);
  EXPECT_NEAR(shape.weights[11], 0.0f, 0.002f);
  for (std::size_t v = 0; v < 3; ++v) {
    float total = 0.0f;
    for (std::size_t k = 0; k < 4; ++k) total += shape.weights[v * 4 + k];
    EXPECT_NEAR(total, 1.0f, 0.002f) << "vertex " << v;
  }
}

// Detects: inverse bind matrices must become bone bind transforms in NIF units, or every skinned vertex lands in the wrong place.
TEST(GltfImportSkin, InverseBindMatricesBecomeTheBoneDataTransformsInNifUnits) {
  auto g = skinned_triangle("Bone_A", "Bone_B", [] {
    auto ibm = identity_with_translation(1.0f, 0.0f, 0.0f);
    const auto second = identity_with_translation(0.0f, 2.0f, 0.0f);
    ibm.insert(ibm.end(), second.begin(), second.end());
    return ibm;
  }());
  const auto nif = read_nif(import_gltf(to_glb(g), with_skeleton()));
  const auto bones = read_bones(nif, static_cast<std::uint32_t>(only_shape(nif).skin_ref));
  ASSERT_EQ(bones.size(), 2U);
  EXPECT_NEAR(bones[0].translation[0], 1.0f / k_metres, 0.01f);
  EXPECT_NEAR(bones[0].translation[1], 0.0f, 0.01f);
  EXPECT_NEAR(bones[0].translation[2], 0.0f, 0.01f);
  EXPECT_NEAR(bones[1].translation[2], 2.0f / k_metres, 0.01f);
  EXPECT_NEAR(bones[1].translation[0], 0.0f, 0.01f);
  EXPECT_NEAR(bones[0].scale, 1.0f, 1e-4f);
  EXPECT_NEAR(bones[0].rotation[0], 1.0f, 1e-4f);
  EXPECT_NEAR(bones[0].rotation[4], 1.0f, 1e-4f);
}

// Detects: a skinned mesh with no chosen skeleton must be refused by name, or it silently loses its deformation.
TEST(GltfImportSkin, SkinnedMeshWithoutAChosenSkeletonIsRefusedByName) {
  auto g = skinned_triangle("Bone_A", "Bone_B", {});
  expect_refusal(g, "no skeleton NIF was chosen");
}

// Detects: a joint missing from the chosen skeleton must be refused by its name, or the skin points at a bone the game does not have.
TEST(GltfImportSkin, JointNotInTheChosenSkeletonIsRefusedByItsName) {
  auto g = skinned_triangle("Bone_A", "Missing", {});
  expect_refusal(g, "joint 'Missing'", with_skeleton());
  auto again = skinned_triangle("Bone_A", "Missing", {});
  expect_refusal(again, "is not a bone in the chosen skeleton", with_skeleton());
}

// Detects: more than eight bone influence sets must be refused by name, or the extra weights vanish without a word.
TEST(GltfImportSkin, MoreThanEightInfluencesIsRefusedByName) {
  auto g = skinned_triangle("Bone_A", "Bone_B", {});
  const auto joints2 = add_accessor(g, k_u8, 3, "VEC4", gltf_import_test::u8_bytes(std::vector<std::uint8_t>(12, 0)));
  g.json["meshes"][0]["primitives"][0]["attributes"]["JOINTS_2"] = joints2;
  expect_refusal(g, "past the eight bone influences", with_skeleton());
}

// Detects: a sparse accessor must be refused by name, or its values are silently replaced with defaults.
TEST(GltfImportSkin, SparseAccessorIsRefusedByName) {
  auto g = triangle();
  const auto index = add_accessor(g, k_float, 3, "VEC3", gltf_import_test::float_bytes({0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f}));
  g.json["accessors"][index]["sparse"] = {{"count", 1}};
  g.json["meshes"][0]["primitives"][0]["attributes"]["NORMAL"] = index;
  expect_refusal(g, "sparse storage, which is not supported");
}

// Detects: morph targets must be refused by name, or the authored shape changes are dropped without a word.
TEST(GltfImportSkin, MorphTargetsAreRefusedByName) {
  auto g = triangle();
  g.json["meshes"][0]["primitives"][0]["targets"] = nlohmann::json::array({{{"POSITION", 0}}});
  expect_refusal(g, "has morph targets, which are not supported");
}

// Detects: a second UV set must be refused by name, or a lightmap or detail map never reaches the NIF.
TEST(GltfImportSkin, SecondUvSetIsRefusedByName) {
  auto g = triangle();
  g.json["meshes"][0]["primitives"][0]["attributes"]["TEXCOORD_1"] = 0;
  expect_refusal(g, "second UV set");
}

// Detects: a non-triangle primitive must be refused by name, or the mesh gains faces the author never made.
TEST(GltfImportSkin, NonTrianglePrimitiveIsRefusedByName) {
  auto g = triangle();
  g.json["meshes"][0]["primitives"][0]["mode"] = 1;
  expect_refusal(g, "mode other than TRIANGLES");
}

// Detects: an absolute texture path outside a textures folder must be refused by name, or the game looks for a file under no Data path.
TEST(GltfImportSkin, AbsoluteTexturePathOutsideTexturesIsRefusedByName) {
  auto g = triangle();
  g.json["meshes"][0]["primitives"][0]["material"] = 0;
  g.json["materials"] = nlohmann::json::array({{{"name", "Skin"}, {"pbrMetallicRoughness", {{"baseColorTexture", {{"index", 0}}}}}}});
  g.json["textures"] = nlohmann::json::array({{{"source", 0}}});
  g.json["images"] = nlohmann::json::array({{{"uri", "C:\\mod\\body_d.dds"}}});
  expect_refusal(g, "absolute path outside a textures folder");
}

// Detects: an absolute texture path under a textures folder must come back as a textures path, or the texture set cannot resolve it.
TEST(GltfImportSkin, TexturePathsBecomeDataRelativeTexturesPaths) {
  auto g = triangle();
  g.json["meshes"][0]["primitives"][0]["material"] = 0;
  g.json["materials"] = nlohmann::json::array({{{"name", "Skin"},
                                                 {"pbrMetallicRoughness", {{"baseColorTexture", {{"index", 0}}}}},
                                                 {"normalTexture", {{"index", 1}}}}});
  g.json["textures"] = nlohmann::json::array({{{"source", 0}}, {{"source", 1}}});
  g.json["images"] = nlohmann::json::array({{{"uri", "textures/Body/body_d.dds"}}, {{"uri", "C:\\mod\\Data\\textures\\Body\\body_n.dds"}}});
  const auto shape = only_shape(read_nif(import_gltf(to_glb(g), {})));
  ASSERT_GE(shape.textures.size(), 2U);
  EXPECT_EQ(shape.textures[0], "textures\\Body\\body_d.dds");
  EXPECT_EQ(shape.textures[1], "textures\\Body\\body_n.dds");
}

TEST(GltfImportSkin, AbsoluteTextureUriThatClimbsOutIsRefusedLikeTheRelativeForm) {
  for (const std::string uri : {"C:/m/textures/../../x.dds", "/m/textures/../x.dds", "textures/../../x.dds"}) {
    auto g = triangle();
    g.json["meshes"][0]["primitives"][0]["material"] = 0;
    g.json["materials"] = nlohmann::json::array({{{"name", "Skin"}, {"pbrMetallicRoughness", {{"baseColorTexture", {{"index", 0}}}}}}});
    g.json["textures"] = nlohmann::json::array({{{"source", 0}}});
    g.json["images"] = nlohmann::json::array({{{"uri", uri}}});
    expect_refusal(g, "climbs out of the Data folder");
  }
}

// Detects: a relative texture name rewritten under an invented folder must not happen, because vanilla NIFs store such paths as written.
TEST(GltfImportSkin, RelativeTextureNameIsKeptAsItIsWritten) {
  auto g = triangle();
  g.json["meshes"][0]["primitives"][0]["material"] = 0;
  g.json["materials"] = nlohmann::json::array({{{"name", "Skin"}, {"pbrMetallicRoughness", {{"baseColorTexture", {{"index", 0}}}}}}});
  g.json["textures"] = nlohmann::json::array({{{"source", 0}}});
  g.json["images"] = nlohmann::json::array({{{"uri", "actors/body_d.dds"}}});
  const auto shape = only_shape(read_nif(import_gltf(to_glb(g), {})));
  EXPECT_EQ(shape.textures[0], "actors\\body_d.dds");
}

// Detects: a BGSM named by the material name or its extras must be linked on the shader, or the game ignores the chosen material.
TEST(GltfImportSkin, BgsmNamedByTheMaterialIsLinkedOnTheShader) {
  auto g = triangle();
  g.json["meshes"][0]["primitives"][0]["material"] = 0;
  g.json["materials"] = nlohmann::json::array({{{"name", "Materials/Body.bgsm"}}});
  const auto by_name = only_shape(read_nif(import_gltf(to_glb(g), {})));
  EXPECT_EQ(by_name.material, "Materials\\Body.bgsm");

  g.json["materials"] = nlohmann::json::array({{{"name", "Skin"}, {"extras", {{"bgsm", "thing.bgsm"}}}}});
  const auto by_extras = only_shape(read_nif(import_gltf(to_glb(g), {})));
  EXPECT_EQ(by_extras.material, "thing.bgsm");
}

// Detects: an embedded image must be refused unless a fallback is given, or it becomes an empty texture path with no word.
TEST(GltfImportSkin, EmbeddedImageNeedsAFallbackOrIsRefusedByName) {
  auto g = triangle();
  g.json["meshes"][0]["primitives"][0]["material"] = 0;
  g.json["materials"] = nlohmann::json::array({{{"name", "Skin"}, {"pbrMetallicRoughness", {{"baseColorTexture", {{"index", 0}}}}}}});
  g.json["textures"] = nlohmann::json::array({{{"source", 0}}});
  g.json["bufferViews"].push_back({{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 4}});
  g.json["images"] = nlohmann::json::array({{{"bufferView", g.json["bufferViews"].size() - 1}, {"mimeType", "image/png"}}});
  expect_refusal(g, "has no game texture path");
  expect_refusal(g, "pass tex_diffuse", {});

  ImportOptions options;
  options.tex_diffuse = "textures\\fallback_d.dds";
  const auto shape = only_shape(read_nif(import_gltf(to_glb(g), options)));
  EXPECT_EQ(shape.textures[0], "textures\\fallback_d.dds");
}

namespace {

// Requires image to name its embedded DDS; the material gets a baseColorTexture that references the image.
Gltf embedded_triangle(nlohmann::json image, nlohmann::json material = {{"name", "Skin"}}) {
  auto g = triangle();
  g.json["meshes"][0]["primitives"][0]["material"] = 0;
  g.json["bufferViews"].push_back({{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 4}});
  image["bufferView"] = g.json["bufferViews"].size() - 1;
  image["mimeType"] = "image/vnd-ms.dds";
  g.json["images"] = nlohmann::json::array({image});
  g.json["textures"] = nlohmann::json::array({{{"source", 0}}});
  material["pbrMetallicRoughness"] = {{"baseColorTexture", {{"index", 0}}}};
  g.json["materials"] = nlohmann::json::array({material});
  return g;
}

}

// Detects: an embedded image with a recorded fo4Path must map to that DDS path, or Blender's default embedded .glb loses its textures.
TEST(GltfImportSkin, EmbeddedImageRecoversItsRecordedGamePath) {
  auto g = embedded_triangle({{"name", "Image0"}, {"extras", {{"fo4Path", "actors/character/x_d.dds"}}}});
  const auto shape = only_shape(read_nif(import_gltf(to_glb(g), {})));
  EXPECT_EQ(shape.textures[0], "actors/character/x_d.dds");
}

// Detects: a PNG uri must never be written as the texture path; the recorded DDS path must be used instead.
TEST(GltfImportSkin, PngUriUsesTheRecordedDdsPathNotThePng) {
  auto g = embedded_triangle({{"name", "x_d.png"}, {"extras", {{"fo4Path", "textures/x_d.dds"}}}});
  g.json["images"][0].erase("bufferView");
  g.json["images"][0].erase("mimeType");
  g.json["images"][0]["uri"] = "textures/x_d.png";
  const auto shape = only_shape(read_nif(import_gltf(to_glb(g), {})));
  EXPECT_EQ(shape.textures[0], "textures/x_d.dds");
}

// Detects: a renamed copy whose name carries a .NNN suffix must still map to its DDS game path, or a re-exported Blender file loses its textures.
TEST(GltfImportSkin, RenamedImageRecoversTheDdsPathFromItsNameWithoutExtras) {
  auto g = embedded_triangle({{"name", "textures/body_d.dds.001"}});
  const auto shape = only_shape(read_nif(import_gltf(to_glb(g), {})));
  EXPECT_EQ(shape.textures[0], "textures/body_d.dds");
}

// Detects: a material's recorded diffuse path must map the slot when its image names none, or the texture is lost when Blender keeps only the material.
TEST(GltfImportSkin, MaterialExtrasRecoverTheTexturePathWhenTheImageNamesNone) {
  auto g = embedded_triangle({{"name", "Image0"}}, {{"name", "Skin"}, {"extras", {{"fo4DiffuseTexture", "textures/x_d.dds"}}}});
  const auto shape = only_shape(read_nif(import_gltf(to_glb(g), {})));
  EXPECT_EQ(shape.textures[0], "textures/x_d.dds");
}

// Detects: an embedded image with no recorded path must be refused naming the option for its slot, or the texture is silently dropped.
TEST(GltfImportSkin, EmbeddedNormalWithNoRecordedPathIsRefusedNamingTexNormal) {
  auto g = embedded_triangle({{"name", "Image0"}});
  g.json["materials"][0].erase("pbrMetallicRoughness");
  g.json["materials"][0]["normalTexture"] = {{"index", 0}};
  expect_refusal(g, "pass tex_normal");
}

// Detects: a fallback option must apply only when no recorded path maps the slot, or the author's option overrides the file.
TEST(GltfImportSkin, FallbackAppliesOnlyWhenNothingElseMapsTheSlot) {
  ImportOptions options;
  options.tex_diffuse = "textures\\fallback_d.dds";
  auto recorded = embedded_triangle({{"name", "Image0"}, {"extras", {{"fo4Path", "textures/x_d.dds"}}}});
  EXPECT_EQ(only_shape(read_nif(import_gltf(to_glb(recorded), options))).textures[0], "textures/x_d.dds");
  auto nothing = embedded_triangle({{"name", "Image0"}});
  EXPECT_EQ(only_shape(read_nif(import_gltf(to_glb(nothing), options))).textures[0], "textures\\fallback_d.dds");
}

// Detects: a BGSM the exporter records as fo4Material must link on the shader, or the material the author chose is lost.
TEST(GltfImportSkin, BgsmRecordedAsFo4MaterialIsLinkedOnTheShader) {
  auto g = triangle();
  g.json["meshes"][0]["primitives"][0]["material"] = 0;
  g.json["materials"] = nlohmann::json::array({{{"name", "Body"}, {"extras", {{"fo4Material", "Materials/Body.bgsm"}}}}});
  EXPECT_EQ(only_shape(read_nif(import_gltf(to_glb(g), {}))).material, "Materials\\Body.bgsm");
}

// Detects: a skinned vertex with five distinct influences must keep the four largest normalised to one, or the smallest influence survives and the others are skewed.
TEST(GltfImportSkin, SkinnedVertexKeepsItsFourLargestOfFiveDistinctInfluences) {
  auto g = empty_gltf();
  const auto positions = add_positions(g, {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
  const auto joints0 = add_accessor(g, k_u8, 3, "VEC4", gltf_import_test::u8_bytes({0, 1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0}));
  const auto weights0 = add_accessor(g, k_float, 3, "VEC4", gltf_import_test::float_bytes({0.4f, 0.3f, 0.2f, 0.05f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f}));
  const auto joints1 = add_accessor(g, k_u8, 3, "VEC4", gltf_import_test::u8_bytes({4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}));
  const auto weights1 = add_accessor(g, k_float, 3, "VEC4", gltf_import_test::float_bytes({0.25f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}));
  const auto mesh = add_mesh(g, "Body", {{{"attributes", {{"POSITION", positions}, {"JOINTS_0", joints0}, {"WEIGHTS_0", weights0}, {"JOINTS_1", joints1}, {"WEIGHTS_1", weights1}}}, {"indices", add_indices(g, {0, 1, 2})}}});
  std::vector<std::size_t> joints;
  for (const char* name : {"Bone_A", "Bone_B", "Bone_C", "Bone_D", "Bone_E"}) joints.push_back(add_node(g, {{"name", name}}));
  add_node(g, {{"name", "Body"}, {"mesh", mesh}, {"skin", 0}});
  g.json["skins"] = nlohmann::json::array({{{"joints", joints}}});

  const auto shape = only_shape(read_nif(import_gltf(to_glb(g), with_five_bones())));
  ASSERT_EQ(shape.bone_indices.size(), 12U);
  EXPECT_EQ(std::vector<std::uint8_t>(shape.bone_indices.begin(), shape.bone_indices.begin() + 4), (std::vector<std::uint8_t>{0, 1, 4, 2}));
  const std::vector<float> expected{0.4f, 0.3f, 0.25f, 0.2f};
  for (std::size_t k = 0; k < 4; ++k) EXPECT_NEAR(shape.weights[k], expected[k] / 1.15f, 0.002f) << k;
}

// Detects: a joint named twice in one skin must be refused by name, or the two entries bind to the same bone without a word.
TEST(GltfImportSkin, DuplicateGltfJointNameIsRefused) {
  auto g = skinned_triangle("Bone_A", "Bone_A", {});
  expect_refusal(g, "lists joint 'Bone_A' more than once", with_skeleton());
}

// Detects: a skeleton carrying one joint name twice must refuse a glTF joint bound to it, or the skin silently binds to the first node.
TEST(GltfImportSkin, JointNameThatTheSkeletonCarriesTwiceIsRefused) {
  ImportOptions options;
  options.skeleton = nif_build::build_nif({"Root", "Bone_A", "Bone_A", "Bone_B"},
                                          {nif_build::named_node_block("NiNode", 0, Transform{}, {1, 2, 3}),
                                           nif_build::named_node_block("NiNode", 1, k_bone_a, {}),
                                           nif_build::named_node_block("NiNode", 2, k_bone_b, {}),
                                           nif_build::named_node_block("NiNode", 3, k_bone_b, {})},
                                          {0});
  auto g = skinned_triangle("Bone_A", "Bone_B", {});
  expect_refusal(g, "names more than one skeleton bone", options);
}

// Detects: JOINTS_0 must be unsigned byte or short integers and in range, or a fractional or NaN joint index is truncated or converted undefined.
TEST(GltfImportSkin, JointIndicesMustBeUnsignedIntegersAndInRange) {
  constexpr float nan = std::numeric_limits<float>::quiet_NaN();
  for (const float bad : {1.5f, nan}) {
    auto g = skinned_triangle("Bone_A", "Bone_B", {});
    const auto joints = add_accessor(g, k_float, 3, "VEC4", gltf_import_test::float_bytes({bad, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f}));
    g.json["meshes"][0]["primitives"][0]["attributes"]["JOINTS_0"] = joints;
    expect_refusal(g, "must be UNSIGNED_BYTE or UNSIGNED_SHORT", with_skeleton());
  }
  auto out_of_range = skinned_triangle("Bone_A", "Bone_B", {});
  const auto joints = add_accessor(out_of_range, k_u8, 3, "VEC4", gltf_import_test::u8_bytes({2, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0}));
  out_of_range.json["meshes"][0]["primitives"][0]["attributes"]["JOINTS_0"] = joints;
  expect_refusal(out_of_range, "past the skin's 2 joints", with_skeleton());
}

TEST(GltfImportSkin, NegativeNanAndInfiniteSkinWeightsAreRefused) {
  constexpr float nan = std::numeric_limits<float>::quiet_NaN();
  constexpr float inf = std::numeric_limits<float>::infinity();
  const std::vector<std::pair<std::vector<float>, std::string>> cases{
      {{-0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.2f, 0.2f, 0.0f, 0.0f}, "has a negative weight at vertex 0"},
      {{nan, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.2f, 0.2f, 0.0f, 0.0f}, "non-finite"},
      {{inf, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.2f, 0.2f, 0.0f, 0.0f}, "non-finite"},
  };
  for (const auto& [values, fragment] : cases) {
    auto g = skinned_triangle("Bone_A", "Bone_B", {});
    const auto weights = add_accessor(g, k_float, 3, "VEC4", gltf_import_test::float_bytes(values));
    g.json["meshes"][0]["primitives"][0]["attributes"]["WEIGHTS_0"] = weights;
    expect_refusal(g, fragment, with_skeleton());
  }
}

TEST(GltfImportSkin, AllZeroSkinWeightsAreStoredAsVanillaStoresThem) {
  auto g = skinned_triangle("Bone_A", "Bone_B", {});
  const auto weights = add_accessor(g, k_float, 3, "VEC4",
                                    gltf_import_test::float_bytes({0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
                                                                   0.2f, 0.2f, 0.0f, 0.0f}));
  g.json["meshes"][0]["primitives"][0]["attributes"]["WEIGHTS_0"] = weights;
  const auto nif = read_nif(import_gltf(to_glb(g), with_skeleton()));
  const auto shape = only_shape(nif);
  ASSERT_EQ(shape.weights.size(), 12U);
  ASSERT_EQ(shape.bone_indices.size(), 12U);
  EXPECT_EQ(std::vector<float>(shape.weights.begin(), shape.weights.begin() + 4), std::vector<float>(4, 0.0f));
  EXPECT_EQ(std::vector<std::uint8_t>(shape.bone_indices.begin(), shape.bone_indices.begin() + 4),
            std::vector<std::uint8_t>(4, 0));
  EXPECT_NEAR(shape.weights[4], 1.0f, 0.002f);
}

// Detects: one joint with two positive influences on a vertex, within a set or across the two sets, must be refused, or only part of its weight survives the four-slot cut.
TEST(GltfImportSkin, RepeatedPositiveInfluenceForOneJointIsRefused) {
  auto within = skinned_triangle("Bone_A", "Bone_B", {});
  const auto weights0 = add_accessor(within, k_float, 3, "VEC4", gltf_import_test::float_bytes({0.5f, 0.0f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f}));
  within.json["meshes"][0]["primitives"][0]["attributes"]["WEIGHTS_0"] = weights0;
  expect_refusal(within, "repeats joint 0", with_skeleton());

  auto across = skinned_triangle("Bone_A", "Bone_B", {});
  const auto weights1 = add_accessor(across, k_float, 3, "VEC4", gltf_import_test::float_bytes({0.1f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}));
  across.json["meshes"][0]["primitives"][0]["attributes"]["WEIGHTS_1"] = weights1;
  expect_refusal(across, "repeats joint 0", with_skeleton());
}

// Detects: a KHR_texture_transform on a texture must be refused by name, or its UV offset, rotation and scale are silently ignored.
TEST(GltfImportSkin, TextureTransformIsRefusedByName) {
  auto g = triangle();
  g.json["meshes"][0]["primitives"][0]["material"] = 0;
  g.json["materials"] = nlohmann::json::array({{{"name", "Skin"},
                                                 {"pbrMetallicRoughness", {{"baseColorTexture", {{"index", 0}, {"extensions", {{"KHR_texture_transform", {{"offset", {0.5, 0.0}}}}}}}}}}}});
  g.json["textures"] = nlohmann::json::array({{{"source", 0}}});
  g.json["images"] = nlohmann::json::array({{{"uri", "textures/Body/body_d.dds"}}});
  expect_refusal(g, "uses KHR_texture_transform");
}

// Detects: an extension listed in extensionsRequired must be refused by name, or a file that needs it imports with its data silently misread.
TEST(GltfImportSkin, RequiredExtensionIsRefusedByName) {
  auto g = triangle();
  g.json["extensionsRequired"] = nlohmann::json::array({"KHR_draco_mesh_compression"});
  expect_refusal(g, "requires extension KHR_draco_mesh_compression");
}
