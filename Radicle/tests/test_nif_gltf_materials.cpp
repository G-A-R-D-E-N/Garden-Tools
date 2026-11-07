#include "fo4/archive/assets.hpp"
#include "fo4/material/material_file.hpp"
#include "fo4/nif/gltf_export.hpp"
#include "fo4/nif/nif.hpp"
#include "fo4/texture/dds.hpp"
#include "gltf_glb_reader.hpp"
#include "gltf_validate.hpp"
#include "material_file_builder.hpp"
#include "nif_build.hpp"
#include "temp_data_dir.hpp"

#include <nlohmann/json.hpp>

#include <gtest/gtest.h>
#include <zlib.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using gltf_test::parse_glb;
using gltf_test::validate_glb;
using namespace nif_build;

namespace {

struct MaterialSpec {
  bool shader{true};
  std::string shader_name;
  std::vector<std::string> slots;
  std::uint32_t flags2{0};
  bool alpha{false};
  std::uint16_t alpha_flags{0};
  std::uint8_t threshold{0};
};

fo4::nif::NifFile material_nif(const MaterialSpec& spec) {
  const std::uint32_t flags = k_full | k_has_uv | k_has_normal;
  const Vert corners[3] = {{{0.0f, 0.0f, 0.0f}, {}}, {{1.0f, 0.0f, 0.0f}, {}}, {{0.0f, 2.0f, 0.0f}, {}}};
  std::vector<std::byte> vertices;
  for (const auto& corner : corners) {
    const auto one = encode_vertex(true, flags, corner);
    vertices.insert(vertices.end(), one.begin(), one.end());
  }
  const auto stride = encode_vertex(true, flags, corners[0]).size();

  LightingFields fields;
  fields.name = 2;
  fields.texture_set = 4;
  fields.flags2 = spec.flags2;
  return fo4::nif::read_nif(build_nif(
      {"Root", "Shape", spec.shader_name},
      {named_node_block("BSMultiBoundNode", 0, fo4::nif::Transform{}, {1}, root_trailer()),
       shape_with_refs("BSTriShape", 1, fo4::nif::Transform{}, flags, stride, -1, spec.shader ? 2 : -1,
                       spec.alpha ? 3 : -1, vertices, {0, 1, 2}),
       full_lighting_shader_block(fields),
       alpha_block(spec.alpha_flags, spec.threshold),
       texture_set_record(spec.slots)},
      {0}));
}

std::vector<std::byte> to_bytes(const std::vector<std::uint8_t>& octets) {
  std::vector<std::byte> out;
  out.reserve(octets.size());
  for (const auto octet : octets) out.push_back(static_cast<std::byte>(octet));
  return out;
}

std::vector<std::uint8_t> to_octets(const std::vector<std::byte>& bytes) {
  std::vector<std::uint8_t> out;
  out.reserve(bytes.size());
  for (const auto byte : bytes) out.push_back(std::to_integer<std::uint8_t>(byte));
  return out;
}

std::vector<std::uint8_t> bc1_dds() {
  const auto format = fo4::texture::lookup_format(71);
  const fo4::texture::DdsInfo info{4, 4, 1, false, 1, 1, 148, *format};
  auto bytes = fo4::texture::build_dds_header(info);
  const std::vector<std::uint8_t> surface{0x00, 0xF8, 0x1F, 0x00, 0xE4, 0xE4, 0xE4, 0xE4};
  bytes.insert(bytes.end(), surface.begin(), surface.end());
  return bytes;
}

std::string texture_uri(const nlohmann::json& doc, std::size_t texture) {
  const auto source = doc.at("textures").at(texture).at("source").get<std::size_t>();
  return doc.at("images").at(source).at("uri").get<std::string>();
}

const nlohmann::json& material_of(const nlohmann::json& doc, std::size_t index = 0) {
  return doc.at("materials").at(index);
}

std::uint32_t be32(const std::vector<std::uint8_t>& bytes, std::size_t at) {
  return (static_cast<std::uint32_t>(bytes.at(at)) << 24U) | (static_cast<std::uint32_t>(bytes.at(at + 1)) << 16U) |
         (static_cast<std::uint32_t>(bytes.at(at + 2)) << 8U) | static_cast<std::uint32_t>(bytes.at(at + 3));
}

struct PngImage {
  std::uint32_t width{0};
  std::uint32_t height{0};
  std::vector<std::uint8_t> scanlines;
};

PngImage read_png(const std::vector<std::uint8_t>& png) {
  PngImage image;
  std::vector<std::uint8_t> packed;
  std::size_t at = 8;
  while (at + 8 <= png.size()) {
    const auto length = be32(png, at);
    const std::string type(png.begin() + static_cast<std::ptrdiff_t>(at + 4),
                           png.begin() + static_cast<std::ptrdiff_t>(at + 8));
    const auto data = at + 8;
    if (type == "IHDR") {
      image.width = be32(png, data);
      image.height = be32(png, data + 4);
    } else if (type == "IDAT") {
      packed.insert(packed.end(), png.begin() + static_cast<std::ptrdiff_t>(data),
                    png.begin() + static_cast<std::ptrdiff_t>(data + length));
    }
    at = data + length + 4;
  }
  uLongf size = static_cast<uLongf>(image.height) * (1U + 4U * image.width);
  image.scanlines.resize(size);
  if (uncompress(image.scanlines.data(), &size, packed.data(), static_cast<uLong>(packed.size())) != Z_OK) {
    image.scanlines.clear();
  }
  return image;
}

std::string join(const std::vector<std::string>& lines) {
  std::string out;
  for (const auto& line : lines) out += line + "\n";
  return out;
}

}  // namespace

// Detects: a shape whose diffuse and normal texture slots must reach baseColorTexture and normalTexture, or reach the wrong image.
TEST(NifGltfMaterials, DiffuseAndNormalSlotsMustBecomeBaseColorAndNormalTextures) {
  MaterialSpec spec;
  spec.slots = {"textures\\armor\\a_d.dds", "textures\\armor\\a_n.dds"};
  const auto glb = parse_glb(fo4::nif::export_glb(material_nif(spec)).glb);
  const auto& doc = glb.json;

  const auto& material = material_of(doc);
  ASSERT_TRUE(material.at("pbrMetallicRoughness").contains("baseColorTexture"));
  EXPECT_EQ(texture_uri(doc, material.at("pbrMetallicRoughness").at("baseColorTexture").at("index").get<std::size_t>()),
            "textures/armor/a_d.dds");
  ASSERT_TRUE(material.contains("normalTexture"));
  EXPECT_EQ(texture_uri(doc, material.at("normalTexture").at("index").get<std::size_t>()), "textures/armor/a_n.dds");
  EXPECT_EQ(doc.at("meshes").at(0).at("primitives").at(0).at("material").get<std::size_t>(), 0U);
  EXPECT_TRUE(validate_glb(glb).empty()) << join(validate_glb(glb));
}

// Detects: each texture slot must record its game path as the image name and fo4Path, and the material must record its diffuse and normal paths, so an import can recover the DDS path when an image is embedded, renamed or stripped of extras.
TEST(NifGltfTextures, ImageAndMaterialMustRecordTheGameTexturePathOfEachSlot) {
  MaterialSpec spec;
  spec.slots = {"actors/a_d.dds", "actors/a_n.dds"};
  const auto glb = parse_glb(fo4::nif::export_glb(material_nif(spec)).glb);
  const auto& material = material_of(glb.json);
  const auto& pbr = material.at("pbrMetallicRoughness");
  const auto diffuse_image = glb.json.at("textures").at(pbr.at("baseColorTexture").at("index").get<std::size_t>()).at("source");
  const auto& image = glb.json.at("images").at(diffuse_image.get<std::size_t>());
  EXPECT_EQ(image.at("name"), "actors/a_d.dds");
  EXPECT_EQ(image.at("extras").at("fo4Path"), "actors/a_d.dds");
  EXPECT_EQ(material.at("extras").at("fo4DiffuseTexture"), "actors/a_d.dds");
  EXPECT_EQ(material.at("extras").at("fo4NormalTexture"), "actors/a_n.dds");
}

// Detects: a NiAlphaProperty blend or test flag that must map to alphaMode BLEND or MASK, with the threshold over 255 as the cutoff, and no alphaMode when neither is set.
TEST(NifGltfMaterials, AlphaBlendAndTestMustMapToBlendAndMaskWithThresholdCutoff) {
  MaterialSpec blend;
  blend.alpha = true;
  blend.alpha_flags = 0x0001;
  const auto blended = parse_glb(fo4::nif::export_glb(material_nif(blend)).glb);
  EXPECT_EQ(material_of(blended.json).at("alphaMode").get<std::string>(), "BLEND");

  MaterialSpec masked;
  masked.alpha = true;
  masked.alpha_flags = 0x0200;
  masked.threshold = 128;
  const auto cut = parse_glb(fo4::nif::export_glb(material_nif(masked)).glb);
  EXPECT_EQ(material_of(cut.json).at("alphaMode").get<std::string>(), "MASK");
  EXPECT_NEAR(material_of(cut.json).at("alphaCutoff").get<double>(), 128.0 / 255.0, 1e-6);

  MaterialSpec opaque;
  opaque.alpha = true;
  const auto flat = parse_glb(fo4::nif::export_glb(material_nif(opaque)).glb);
  EXPECT_FALSE(material_of(flat.json).contains("alphaMode"));
}

// Detects: the Double_Sided shader flag (flag word 2, bit 4) that must set doubleSided, and a single-sided shader that must not.
TEST(NifGltfMaterials, DoubleSidedShaderFlagMustSetDoubleSided) {
  MaterialSpec two_sided;
  two_sided.flags2 = 0x10;
  EXPECT_EQ(material_of(parse_glb(fo4::nif::export_glb(material_nif(two_sided)).glb).json).at("doubleSided"), true);

  MaterialSpec one_sided;
  EXPECT_FALSE(material_of(parse_glb(fo4::nif::export_glb(material_nif(one_sided)).glb).json).contains("doubleSided"));
}

// Detects: a shape naming a BGSM that must take its textures and TwoSided from the material file read through the resolver, not from the NIF texture set.
TEST(NifGltfMaterials, BgsmNamedByTheShaderMustSupplyTexturesAndTwoSidedThroughTheResolver) {
  fo4::test::TempDataDir dir;
  auto file = fo4::material::read_material_file(fo4::test::material_bgsm(fo4::test::MaterialShape{}));
  ASSERT_TRUE(fo4::material::set_text(file, "DiffuseTexture", "Textures/armor/bgsm_d.dds"));
  ASSERT_TRUE(fo4::material::set_text(file, "NormalTexture", "Textures/armor/bgsm_n.dds"));
  ASSERT_TRUE(fo4::material::set_field<bool>(file, "TwoSided", true));
  dir.write("materials/armor/x.bgsm", fo4::material::write_material_file(file), 0);

  const auto roots = std::vector<std::filesystem::path>{dir.path()};
  const auto assets = fo4::archive::AssetResolver::open_roots(roots, {});
  MaterialSpec spec;
  spec.shader_name = "Materials\\Armor\\X.BGSM";
  spec.slots = {"textures\\armor\\nif_d.dds"};
  fo4::nif::GltfOptions options;
  options.assets = &assets;
  const auto exported = fo4::nif::export_glb(material_nif(spec), options);
  const auto glb = parse_glb(exported.glb);

  const auto& material = material_of(glb.json);
  EXPECT_EQ(texture_uri(glb.json, material.at("pbrMetallicRoughness").at("baseColorTexture").at("index").get<std::size_t>()),
            "textures/armor/bgsm_d.dds");
  EXPECT_EQ(texture_uri(glb.json, material.at("normalTexture").at("index").get<std::size_t>()), "textures/armor/bgsm_n.dds");
  EXPECT_EQ(material.at("doubleSided"), true);
  EXPECT_TRUE(exported.warnings.empty()) << join(exported.warnings);
}

// Detects: a BGSM named by a shape but absent from the resolver that must keep the NIF texture set and report a warning, not drop its textures silently.
TEST(NifGltfMaterials, MissingBgsmMustFallBackToTheNifTextureSetWithAWarning) {
  fo4::test::TempDataDir dir;
  const auto roots = std::vector<std::filesystem::path>{dir.path()};
  const auto assets = fo4::archive::AssetResolver::open_roots(roots, {});
  MaterialSpec spec;
  spec.shader_name = "Materials\\Armor\\gone.bgsm";
  spec.slots = {"textures\\armor\\nif_d.dds"};
  fo4::nif::GltfOptions options;
  options.assets = &assets;
  const auto exported = fo4::nif::export_glb(material_nif(spec), options);
  const auto glb = parse_glb(exported.glb);

  EXPECT_EQ(texture_uri(glb.json, material_of(glb.json).at("pbrMetallicRoughness").at("baseColorTexture").at("index").get<std::size_t>()),
            "textures/armor/nif_d.dds");
  ASSERT_EQ(exported.warnings.size(), 1U);
  EXPECT_NE(exported.warnings.front().find("material not found"), std::string::npos);
}

// Detects: a texture path with .. that must be skipped with a warning, not kept as a URI that reaches outside the texture root.
TEST(NifGltfMaterials, TexturePathThatEscapesTheRootMustBeSkippedWithAWarning) {
  MaterialSpec spec;
  spec.slots = {"textures\\..\\..\\secret_d.dds", "textures\\armor\\a_n.dds"};
  const auto exported = fo4::nif::export_glb(material_nif(spec));
  const auto glb = parse_glb(exported.glb);

  EXPECT_FALSE(material_of(glb.json).at("pbrMetallicRoughness").contains("baseColorTexture"));
  EXPECT_TRUE(material_of(glb.json).contains("normalTexture"));
  ASSERT_EQ(exported.warnings.size(), 1U);
  EXPECT_NE(exported.warnings.front().find("secret_d.dds"), std::string::npos);
}

// Detects: a texture path without the textures prefix that must resolve under textures/ in the resolver, not at the Data root where it is never found.
TEST(NifGltfTextures, TexturePathWithoutTheTexturesPrefixMustResolveUnderTheTexturesFolder) {
  fo4::test::TempDataDir dir;
  const auto dds = bc1_dds();
  dir.write("textures/effects/a_d.dds", to_bytes(dds), 0);
  const auto roots = std::vector<std::filesystem::path>{dir.path()};
  const auto assets = fo4::archive::AssetResolver::open_roots(roots, {});
  MaterialSpec spec;
  spec.slots = {"effects\\a_d.dds"};
  fo4::nif::GltfOptions options;
  options.textures = fo4::nif::GltfTextureMode::copy;
  options.assets = &assets;
  const auto exported = fo4::nif::export_glb(material_nif(spec), options);

  ASSERT_EQ(exported.files.size(), 1U);
  EXPECT_EQ(exported.files.front().path, "textures/effects/a_d.dds");
  EXPECT_EQ(exported.files.front().bytes, to_bytes(dds));
  EXPECT_TRUE(exported.warnings.empty()) << join(exported.warnings);
}

// Detects: materials=false that must leave the primitives without a material and the document without materials, textures or images.
TEST(NifGltfMaterials, MaterialsOffMustLeaveNoMaterialOrTextureData) {
  MaterialSpec spec;
  spec.slots = {"textures\\armor\\a_d.dds"};
  spec.alpha = true;
  spec.alpha_flags = 0x0001;
  fo4::nif::GltfOptions options;
  options.materials = false;
  const auto glb = parse_glb(fo4::nif::export_glb(material_nif(spec), options).glb);

  EXPECT_FALSE(glb.json.contains("materials"));
  EXPECT_FALSE(glb.json.contains("textures"));
  EXPECT_FALSE(glb.json.contains("images"));
  EXPECT_FALSE(glb.json.at("meshes").at(0).at("primitives").at(0).contains("material"));
}

// Detects: copy mode that must return the resolved DDS bytes as a file named by the image uri, or leave the glb pointing at a file that was never written.
TEST(NifGltfTextures, CopyModeMustReturnTheResolvedDdsBytesAsTheFileNamedByTheImageUri) {
  fo4::test::TempDataDir dir;
  const auto dds = bc1_dds();
  dir.write("textures/armor/a_d.dds", to_bytes(dds), 0);
  const auto roots = std::vector<std::filesystem::path>{dir.path()};
  const auto assets = fo4::archive::AssetResolver::open_roots(roots, {});
  MaterialSpec spec;
  spec.slots = {"textures\\armor\\a_d.dds"};
  fo4::nif::GltfOptions options;
  options.textures = fo4::nif::GltfTextureMode::copy;
  options.assets = &assets;
  const auto exported = fo4::nif::export_glb(material_nif(spec), options);
  const auto glb = parse_glb(exported.glb);

  ASSERT_EQ(exported.files.size(), 1U);
  EXPECT_EQ(exported.files.front().path, "textures/armor/a_d.dds");
  EXPECT_EQ(exported.files.front().bytes, to_bytes(dds));
  EXPECT_EQ(texture_uri(glb.json, material_of(glb.json).at("pbrMetallicRoughness").at("baseColorTexture").at("index").get<std::size_t>()),
            "textures/armor/a_d.dds");
  EXPECT_TRUE(validate_glb(glb).empty()) << join(validate_glb(glb));
}

// Detects: embed mode that must put the DDS bytes in the BIN chunk behind a bufferView with image/vnd-ms.dds, or leave an image with no bytes.
TEST(NifGltfTextures, EmbedModeMustPutTheDdsBytesInTheBinChunkWithAMimeType) {
  fo4::test::TempDataDir dir;
  const auto dds = bc1_dds();
  dir.write("textures/armor/a_d.dds", to_bytes(dds), 0);
  const auto roots = std::vector<std::filesystem::path>{dir.path()};
  const auto assets = fo4::archive::AssetResolver::open_roots(roots, {});
  MaterialSpec spec;
  spec.slots = {"textures\\armor\\a_d.dds"};
  fo4::nif::GltfOptions options;
  options.textures = fo4::nif::GltfTextureMode::embed;
  options.assets = &assets;
  const auto exported = fo4::nif::export_glb(material_nif(spec), options);
  const auto glb = parse_glb(exported.glb);

  EXPECT_TRUE(exported.files.empty());
  const auto& image = glb.json.at("images").at(0);
  EXPECT_EQ(image.at("mimeType").get<std::string>(), "image/vnd-ms.dds");
  const auto& view = glb.json.at("bufferViews").at(image.at("bufferView").get<std::size_t>());
  const auto offset = view.at("byteOffset").get<std::size_t>();
  const auto length = view.at("byteLength").get<std::size_t>();
  ASSERT_EQ(length, dds.size());
  EXPECT_EQ(std::vector<std::uint8_t>(reinterpret_cast<const std::uint8_t*>(glb.bin.data() + offset),
                                      reinterpret_cast<const std::uint8_t*>(glb.bin.data() + offset + length)),
            dds);
  EXPECT_TRUE(validate_glb(glb).empty()) << join(validate_glb(glb));
}

// Detects: png mode that must write a .png whose pixels equal the DDS decode, or write a PNG that the decoder cannot read back.
TEST(NifGltfTextures, PngModeMustWriteAPngWhoseScanlinesEqualTheDdsDecode) {
  fo4::test::TempDataDir dir;
  const auto dds = bc1_dds();
  dir.write("textures/armor/a_d.dds", to_bytes(dds), 0);
  const auto roots = std::vector<std::filesystem::path>{dir.path()};
  const auto assets = fo4::archive::AssetResolver::open_roots(roots, {});
  MaterialSpec spec;
  spec.slots = {"textures\\armor\\a_d.dds"};
  fo4::nif::GltfOptions options;
  options.textures = fo4::nif::GltfTextureMode::png;
  options.assets = &assets;
  const auto exported = fo4::nif::export_glb(material_nif(spec), options);
  const auto glb = parse_glb(exported.glb);

  ASSERT_EQ(exported.files.size(), 1U);
  EXPECT_EQ(exported.files.front().path, "textures/armor/a_d.png");
  EXPECT_EQ(texture_uri(glb.json, material_of(glb.json).at("pbrMetallicRoughness").at("baseColorTexture").at("index").get<std::size_t>()),
            "textures/armor/a_d.png");

  std::uint32_t width = 0;
  std::uint32_t height = 0;
  const auto rgba = fo4::texture::decode_dds_rgba(dds, 0, false, width, height);
  const auto png = read_png(to_octets(exported.files.front().bytes));
  EXPECT_EQ(png.width, width);
  EXPECT_EQ(png.height, height);
  ASSERT_EQ(png.scanlines.size(), static_cast<std::size_t>(height) * (1U + 4U * width));
  for (std::uint32_t y = 0; y < height; ++y) {
    EXPECT_EQ(png.scanlines[y * (1U + 4U * width)], 0) << "row " << y << " must use filter 0";
    for (std::uint32_t x = 0; x < 4U * width; ++x) {
      EXPECT_EQ(png.scanlines[y * (1U + 4U * width) + 1U + x], rgba[y * 4U * width + x]) << "row " << y << " byte " << x;
    }
  }
  EXPECT_EQ(png.scanlines[1], 255);
  EXPECT_EQ(png.scanlines[2], 0);
  EXPECT_TRUE(validate_glb(glb).empty()) << join(validate_glb(glb));
}

// Detects: a BC5 normal map in png mode whose blue channel must be rebuilt from red and green, or Blender reads the normal map as flat.
TEST(NifGltfTextures, PngNormalMapMustRebuildTheBlueChannelFromRedAndGreen) {
  fo4::test::TempDataDir dir;
  const auto format = fo4::texture::lookup_format(83);
  const fo4::texture::DdsInfo info{4, 4, 1, false, 1, 1, 148, *format};
  auto dds = fo4::texture::build_dds_header(info);
  std::vector<std::uint8_t> surface(16, 0);
  surface[0] = 255;
  dds.insert(dds.end(), surface.begin(), surface.end());
  dir.write("textures/armor/a_n.dds", to_bytes(dds), 0);
  const auto roots = std::vector<std::filesystem::path>{dir.path()};
  const auto assets = fo4::archive::AssetResolver::open_roots(roots, {});
  MaterialSpec spec;
  spec.slots = {"", "textures\\armor\\a_n.dds"};
  fo4::nif::GltfOptions options;
  options.textures = fo4::nif::GltfTextureMode::png;
  options.assets = &assets;
  const auto exported = fo4::nif::export_glb(material_nif(spec), options);

  ASSERT_EQ(exported.files.size(), 1U);
  const auto png = read_png(to_octets(exported.files.front().bytes));
  ASSERT_EQ(png.scanlines.size(), 4U * (1U + 4U * 4U));
  EXPECT_EQ(png.scanlines[1], 255);
  EXPECT_EQ(png.scanlines[2], 0);
  EXPECT_EQ(png.scanlines[3], 128);
}

// Detects: a PNG output that must not take the name of a raw texture beside it, so each file keeps its own bytes and its own uri.
TEST(NifGltfTextures, PngOutputNameMustNotCollideWithARawTextureOfTheSameName) {
  fo4::test::TempDataDir dir;
  dir.write("textures/armor/x.dds", to_bytes(bc1_dds()), 0);
  const std::string raw_text = "RAW PNG";
  std::vector<std::byte> raw;
  for (const char ch : raw_text) raw.push_back(static_cast<std::byte>(ch));
  dir.write("textures/armor/x.png", raw, 0);
  const auto roots = std::vector<std::filesystem::path>{dir.path()};
  const auto assets = fo4::archive::AssetResolver::open_roots(roots, {});
  MaterialSpec spec;
  spec.slots = {"textures\\armor\\x.dds", "textures\\armor\\x.png"};
  fo4::nif::GltfOptions options;
  options.textures = fo4::nif::GltfTextureMode::png;
  options.assets = &assets;
  const auto exported = fo4::nif::export_glb(material_nif(spec), options);
  const auto glb = parse_glb(exported.glb);

  ASSERT_EQ(exported.files.size(), 2U);
  EXPECT_EQ(exported.files[0].path, "textures/armor/x.png");
  EXPECT_EQ(exported.files[0].bytes.front(), std::byte{137});
  EXPECT_EQ(exported.files[1].path, "textures/armor/x_2.png");
  EXPECT_EQ(exported.files[1].bytes, raw);
  EXPECT_EQ(texture_uri(glb.json, material_of(glb.json).at("pbrMetallicRoughness").at("baseColorTexture").at("index").get<std::size_t>()),
            "textures/armor/x.png");
  EXPECT_EQ(texture_uri(glb.json, material_of(glb.json).at("normalTexture").at("index").get<std::size_t>()), "textures/armor/x_2.png");
  EXPECT_TRUE(validate_glb(glb).empty()) << join(validate_glb(glb));
}

// Detects: a texture the resolver cannot find that must be left out of the material with a warning, not written as an image uri with no file behind it.
TEST(NifGltfTextures, UnresolvedTextureMustBeLeftOutWithAWarningInCopyMode) {
  fo4::test::TempDataDir dir;
  const auto roots = std::vector<std::filesystem::path>{dir.path()};
  const auto assets = fo4::archive::AssetResolver::open_roots(roots, {});
  MaterialSpec spec;
  spec.slots = {"textures\\armor\\missing_d.dds"};
  fo4::nif::GltfOptions options;
  options.textures = fo4::nif::GltfTextureMode::copy;
  options.assets = &assets;
  const auto exported = fo4::nif::export_glb(material_nif(spec), options);
  const auto glb = parse_glb(exported.glb);

  EXPECT_TRUE(exported.files.empty());
  EXPECT_FALSE(glb.json.contains("images"));
  EXPECT_FALSE(material_of(glb.json).at("pbrMetallicRoughness").contains("baseColorTexture"));
  ASSERT_EQ(exported.warnings.size(), 1U);
  EXPECT_NE(exported.warnings.front().find("texture not found"), std::string::npos);
}

// Detects: an export with materials, textures and copied files together that must pass every structural check, or a reference that points past the list it indexes.
TEST(NifGltfTextures, EveryMaterialAndTextureReferenceMustPassTheStructuralValidator) {
  fo4::test::TempDataDir dir;
  dir.write("textures/armor/a_d.dds", to_bytes(bc1_dds()), 0);
  const auto roots = std::vector<std::filesystem::path>{dir.path()};
  const auto assets = fo4::archive::AssetResolver::open_roots(roots, {});
  MaterialSpec spec;
  spec.slots = {"textures\\armor\\a_d.dds", "textures\\armor\\a_n.dds"};
  spec.alpha = true;
  spec.alpha_flags = 0x0200;
  spec.threshold = 64;
  spec.flags2 = 0x10;
  fo4::nif::GltfOptions options;
  options.textures = fo4::nif::GltfTextureMode::copy;
  options.assets = &assets;
  const auto exported = fo4::nif::export_glb(material_nif(spec), options);
  const auto glb = parse_glb(exported.glb);
  EXPECT_TRUE(validate_glb(glb).empty()) << join(validate_glb(glb));
}

// Detects: the structural validator passing a glb whose accessor runs past its bufferView, so a bad real-data export would look valid.
TEST(NifGltfTextures, StructuralValidatorMustFlagAnAccessorThatRunsPastItsBufferView) {
  auto glb = parse_glb(fo4::nif::export_glb(material_nif(MaterialSpec{})).glb);
  ASSERT_TRUE(validate_glb(glb).empty());
  glb.json["accessors"][0]["count"] = 1000;
  const auto faults = validate_glb(glb);
  ASSERT_FALSE(faults.empty());
  EXPECT_NE(faults.front().find("runs past its bufferView"), std::string::npos);
}
