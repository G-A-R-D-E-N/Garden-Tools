#include "fo4/error.hpp"
#include "fo4/material/material_file.hpp"
#include "material_file_builder.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using fo4::material::Color3;
using fo4::material::MaterialFile;
using fo4::material::MaterialFormat;
using fo4::material::RawBool;

constexpr std::uint32_t kBgsmVersions[] = {0, 1, 2, 3, 6, 7, 8, 9, 10, 12, 13, 17, 20};
constexpr std::uint32_t kBgemVersions[] = {2, 10, 11, 15, 16, 20, 21, 22};

MaterialFile load(const std::vector<std::byte>& bytes) { return fo4::material::read_material_file(bytes); }

std::vector<std::byte> save(const MaterialFile& file) { return fo4::material::write_material_file(file); }

bool has(const MaterialFile& file, std::string_view name) {
  return fo4::material::find_field(file, name) != nullptr;
}

std::uint8_t raw_bool(const MaterialFile& file, std::string_view name) {
  const auto* field = fo4::material::find_field(file, name);
  const auto* raw = field ? std::get_if<RawBool>(&field->value) : nullptr;
  return raw ? raw->raw : 0xFF;
}

std::string text_of(const MaterialFile& file, std::string_view name) {
  const auto value = fo4::material::get_field<std::string>(file, name);
  return value ? std::string(fo4::material::text(*value)) : std::string("<missing>");
}

}  // namespace

TEST(MaterialFile, BgsmRoundTripsByteIdenticalAcrossVersionsAndBranches) {
  for (const auto version : kBgsmVersions) {
    for (const bool emit : {false, true}) {
      for (const bool terrain : {false, true}) {
        const auto bytes = fo4::test::material_bgsm({.version = version, .emit = emit, .terrain = terrain});
        const auto file = load(bytes);
        ASSERT_EQ(file.format, MaterialFormat::bgsm);
        ASSERT_EQ(file.version(), version);
        EXPECT_TRUE(save(file) == bytes) << "version " << version << " emit " << emit << " terrain " << terrain;

        EXPECT_EQ(has(file, "EmittanceColor"), emit);
        EXPECT_EQ(raw_bool(file, "EmitEnabled"), emit ? 1 : 0);
        if (emit) {
          EXPECT_EQ(fo4::material::get_field<Color3>(file, "EmittanceColor"), (Color3{0.25F, 0.5F, 0.75F}));
        }
        EXPECT_EQ(has(file, "Terrain"), version >= 3);
        EXPECT_EQ(has(file, "UnkInt1"), version == 3 && terrain);
        EXPECT_EQ(has(file, "TerrainRotationAngle"), version >= 3 && terrain);
        if (version >= 3) {
          EXPECT_EQ(raw_bool(file, "Terrain"), terrain ? 1 : 0);
        }

        EXPECT_EQ(has(file, "DiffuseTexture"), true);
        EXPECT_EQ(has(file, "FlowTexture"), version > 2);
        EXPECT_EQ(has(file, "InnerLayerTexture"), version <= 2);
        EXPECT_EQ(has(file, "DisplacementTexture"), version <= 2);
        EXPECT_EQ(has(file, "DistanceFieldAlphaTexture"), version >= 17);
        EXPECT_EQ(has(file, "Translucency"), version >= 8);
        EXPECT_EQ(has(file, "RimLighting"), version < 8);
        EXPECT_EQ(has(file, "WetnessControlEnvMapScale"), version < 10);
        EXPECT_EQ(has(file, "EnvironmentMapping"), version < 10);
        EXPECT_EQ(has(file, "DepthBias"), version >= 10);
        EXPECT_EQ(has(file, "MaskWrites"), version >= 6);
        EXPECT_EQ(has(file, "Pbr"), version > 2);
        EXPECT_EQ(has(file, "CustomPorosity"), version >= 9);
        EXPECT_EQ(has(file, "LumEmittance"), version >= 12);
        EXPECT_EQ(has(file, "UseAdaptativeEmissive"), version >= 13);
        EXPECT_EQ(has(file, "BackLighting"), version < 8);
        EXPECT_EQ(has(file, "EnvironmentMappingWindow"), version < 7);
        EXPECT_EQ(has(file, "DisplacementTextureBias"), version < 3);
        EXPECT_EQ(has(file, "SkewSpecularAlpha"), version >= 1);

        EXPECT_EQ(raw_bool(file, "AlphaTest"), 2);
        EXPECT_EQ(fo4::material::get_field<std::uint32_t>(file, "TileFlags"), 0x80000003u);
        EXPECT_EQ(text_of(file, "NormalTexture"), "NoNul");
        EXPECT_EQ(text_of(file, "RootMaterialPath"), "");
        EXPECT_EQ(file.trailing.size(), 4u);
      }
    }
  }
}

TEST(MaterialFile, BgemRoundTripsByteIdenticalAcrossVersionsAndBranches) {
  for (const auto version : kBgemVersions) {
    for (const bool glass : {false, true}) {
      const auto bytes = fo4::test::material_bgem({.version = version, .glass = glass});
      const auto file = load(bytes);
      ASSERT_EQ(file.format, MaterialFormat::bgem);
      ASSERT_EQ(file.version(), version);
      EXPECT_TRUE(save(file) == bytes) << "version " << version << " glass " << glass;

      EXPECT_EQ(has(file, "SpecularTexture"), version >= 11);
      EXPECT_EQ(has(file, "GlowTexture"), version >= 11);
      EXPECT_EQ(has(file, "GlassRoughnessScratch"), version >= 21);
      EXPECT_EQ(has(file, "GlassEnabled"), version >= 21);
      EXPECT_EQ(has(file, "GlassFresnelColor"), version >= 21 && glass);
      EXPECT_EQ(has(file, "GlassBlurScaleBase"), version >= 21 && glass);
      EXPECT_EQ(has(file, "GlassBlurScaleFactor"), version >= 22 && glass);
      EXPECT_EQ(has(file, "GlassRefractionScaleBase"), version >= 21 && glass);
      if (version >= 21) {
        EXPECT_EQ(raw_bool(file, "GlassEnabled"), glass ? 1 : 0);
      }
      EXPECT_EQ(has(file, "EnvironmentMapping"), version < 10);
      EXPECT_EQ(has(file, "DepthBias"), version >= 10);
      EXPECT_EQ(has(file, "EffectEnvironmentMapping"), version >= 10);
      EXPECT_EQ(has(file, "MaskWrites"), version >= 6);
      EXPECT_EQ(has(file, "EmittanceColor"), version >= 11);
      EXPECT_EQ(has(file, "AdaptativeEmissiveExposureOffset"), version >= 15);
      EXPECT_EQ(has(file, "Glowmap"), version >= 16);
      EXPECT_EQ(has(file, "EffectPbrSpecular"), version >= 20);
      EXPECT_TRUE(has(file, "EnvmapMinLOD"));

      EXPECT_EQ(raw_bool(file, "AlphaTest"), 2);
      EXPECT_EQ(fo4::material::get_field<std::uint32_t>(file, "TileFlags"), 0x80000003u);
      EXPECT_EQ(text_of(file, "NormalTexture"), "NoNul");
      EXPECT_EQ(text_of(file, "EnvmapMaskTexture"), "");
      EXPECT_EQ(file.trailing.size(), 4u);
    }
  }
}

TEST(MaterialFile, FormatComesFromTheMagicNotFromAnExtension) {
  const auto bgem = fo4::test::material_bgem({.version = 2});
  EXPECT_EQ(load(bgem).format, MaterialFormat::bgem);
  const auto bgsm = fo4::test::material_bgsm({.version = 2});
  EXPECT_EQ(load(bgsm).format, MaterialFormat::bgsm);
}

TEST(MaterialFile, BadMagicIsAParseError) {
  auto bytes = fo4::test::material_bgsm({.version = 2});
  bytes[0] = std::byte{0};
  EXPECT_THROW(load(bytes), fo4::parse_error);
}

TEST(MaterialFile, TruncatedFilesAreParseErrors) {
  const auto bgsm = fo4::test::material_bgsm({.version = 20, .emit = true, .trailing = false});
  const auto bgem = fo4::test::material_bgem({.version = 22, .glass = true, .trailing = false});
  for (const auto* full : {&bgsm, &bgem}) {
    const std::vector<std::size_t> cuts{0, 1, 3, 4, 8, 40, full->size() - 1};
    for (const auto cut : cuts) {
      const std::vector<std::byte> truncated(full->begin(), full->begin() + static_cast<std::ptrdiff_t>(cut));
      EXPECT_THROW(load(truncated), fo4::parse_error) << "cut at " << cut;
    }
  }
}

TEST(MaterialFile, FieldLookupIsCaseInsensitive) {
  const auto file = load(fo4::test::material_bgsm({.version = 2}));
  EXPECT_TRUE(has(file, "smoothness"));
  EXPECT_EQ(fo4::material::get_field<float>(file, "SMOOTHNESS"), 0.5F);
  EXPECT_FALSE(has(file, "NoSuchField"));
}

TEST(MaterialFile, VersionAndSignatureAreReadOnly) {
  auto file = load(fo4::test::material_bgsm({.version = 2}));
  EXPECT_THROW(fo4::material::set_field(file, "Version", std::uint32_t{3}), std::invalid_argument);
  EXPECT_THROW(fo4::material::set_field(file, "signature", std::uint32_t{0}), std::invalid_argument);
  EXPECT_EQ(file.version(), 2u);
}

TEST(MaterialFile, SetFieldRefusesAMismatchedType) {
  auto file = load(fo4::test::material_bgem({.version = 2}));
  EXPECT_FALSE(fo4::material::set_field(file, "AlphaTest", 1.0F));
  EXPECT_FALSE(fo4::material::set_field(file, "NoSuchField", true));
}

TEST(MaterialFile, SettersStoreBoolsAsZeroOrOneAndTextWithOneNul) {
  auto file = load(fo4::test::material_bgem({.version = 2}));
  ASSERT_TRUE(fo4::material::set_field(file, "AlphaTest", false));
  EXPECT_EQ(raw_bool(file, "AlphaTest"), 0);
  ASSERT_TRUE(fo4::material::set_field(file, "AlphaTest", true));
  EXPECT_EQ(raw_bool(file, "AlphaTest"), 1);

  ASSERT_TRUE(fo4::material::set_text(file, "BaseTexture", "new.dds"));
  EXPECT_EQ(text_of(file, "BaseTexture"), "new.dds");
  EXPECT_EQ(fo4::material::get_field<std::string>(file, "BaseTexture")->size(), 8u);

  ASSERT_TRUE(fo4::material::set_text(file, "BaseTexture", ""));
  EXPECT_EQ(fo4::material::get_field<std::string>(file, "BaseTexture")->size(), 1u);
}

TEST(MaterialFile, TextDropsExactlyOneTrailingNul) {
  EXPECT_EQ(fo4::material::text(std::string_view("a\0\0", 3)), std::string_view("a\0", 2));
  EXPECT_EQ(fo4::material::text(std::string_view("abc", 3)), std::string_view("abc"));
  EXPECT_EQ(fo4::material::text(std::string_view("", 0)), std::string_view(""));
}

TEST(MaterialFile, SetThenWriteChangesOnlyTheTargetField) {
  const auto bytes = fo4::test::material_bgsm({.version = 2});
  auto file = load(bytes);
  ASSERT_TRUE(fo4::material::set_field(file, "Smoothness", 0.75F));
  const auto written = save(file);
  ASSERT_EQ(written.size(), bytes.size());

  std::size_t first = bytes.size();
  std::size_t last = 0;
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    if (written[i] != bytes[i]) {
      first = std::min(first, i);
      last = i;
    }
  }
  EXPECT_LT(last - first, 4u);

  const auto reread = load(written);
  ASSERT_EQ(reread.fields.size(), file.fields.size());
  for (std::size_t i = 0; i < file.fields.size(); ++i) {
    EXPECT_EQ(reread.fields[i].name, file.fields[i].name);
    if (file.fields[i].name == "Smoothness") {
      EXPECT_EQ(fo4::material::get_field<float>(reread, "Smoothness"), 0.75F);
    } else {
      EXPECT_TRUE(reread.fields[i].value == file.fields[i].value) << file.fields[i].name;
    }
  }
}

TEST(MaterialFile, SettingEmitEnabledWritesAZeroColorWhenTheColorIsAbsent) {
  const auto bytes = fo4::test::material_bgsm({.version = 20});
  auto file = load(bytes);
  ASSERT_FALSE(has(file, "EmittanceColor"));
  ASSERT_TRUE(fo4::material::set_field(file, "EmitEnabled", true));

  const auto written = save(file);
  EXPECT_EQ(written.size(), bytes.size() + 12u);
  const auto reread = load(written);
  const auto color = fo4::material::get_field<Color3>(reread, "EmittanceColor");
  ASSERT_TRUE(color.has_value());
  EXPECT_EQ(*color, (Color3{0.0F, 0.0F, 0.0F}));
  EXPECT_EQ(fo4::material::get_field<bool>(reread, "EmitEnabled"), true);
}
