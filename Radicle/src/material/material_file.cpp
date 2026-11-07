#include "fo4/material/material_file.hpp"

#include "fo4/binary/cursor.hpp"
#include "fo4/error.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <stdexcept>

namespace fo4::material {

namespace {

constexpr std::uint32_t k_bgsm_magic = 0x4D534742u;
constexpr std::uint32_t k_bgem_magic = 0x4D454742u;

bool equal_ignore_case(std::string_view left, std::string_view right) {
  return std::equal(left.begin(), left.end(), right.begin(), right.end(), [](char a, char b) {
    return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
  });
}

void put_u8(std::vector<std::byte>& out, std::uint8_t value) { out.push_back(std::byte{value}); }

void put_u32(std::vector<std::byte>& out, std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    put_u8(out, static_cast<std::uint8_t>(value >> shift));
  }
}

void put_f32(std::vector<std::byte>& out, float value) {
  std::uint32_t bits{};
  std::memcpy(&bits, &value, sizeof(bits));
  put_u32(out, bits);
}

template <typename T>
T decode(binary::Cursor& in) {
  if constexpr (std::is_same_v<T, RawBool>) {
    return RawBool{in.read_u8()};
  } else if constexpr (std::is_same_v<T, std::uint8_t>) {
    return in.read_u8();
  } else if constexpr (std::is_same_v<T, std::uint32_t>) {
    return in.read_u32();
  } else if constexpr (std::is_same_v<T, float>) {
    return in.read_f32();
  } else if constexpr (std::is_same_v<T, Color3>) {
    const auto r = in.read_f32();
    const auto g = in.read_f32();
    const auto b = in.read_f32();
    return Color3{r, g, b};
  } else {
    static_assert(std::is_same_v<T, std::string>);
    const auto length = in.read_u32();
    const auto bytes = in.read_bytes(length);
    std::string value;
    value.reserve(bytes.size());
    for (const auto element : bytes) {
      value.push_back(static_cast<char>(element));
    }
    return value;
  }
}

template <typename T>
void encode(std::vector<std::byte>& out, const T& value) {
  if constexpr (std::is_same_v<T, RawBool>) {
    put_u8(out, value.raw);
  } else if constexpr (std::is_same_v<T, std::uint8_t>) {
    put_u8(out, value);
  } else if constexpr (std::is_same_v<T, std::uint32_t>) {
    put_u32(out, value);
  } else if constexpr (std::is_same_v<T, float>) {
    put_f32(out, value);
  } else if constexpr (std::is_same_v<T, Color3>) {
    put_f32(out, value.r);
    put_f32(out, value.g);
    put_f32(out, value.b);
  } else {
    static_assert(std::is_same_v<T, std::string>);
    put_u32(out, static_cast<std::uint32_t>(value.size()));
    for (const char ch : value) {
      put_u8(out, static_cast<std::uint8_t>(ch));
    }
  }
}

class Reader {
public:
  Reader(binary::Cursor& in, std::vector<MaterialField>& fields) : in_(in), fields_(fields) {}

  template <typename T>
  T field(std::string_view name) {
    T value = decode<T>(in_);
    fields_.push_back(MaterialField{std::string(name), FieldValue{value}});
    return value;
  }

private:
  binary::Cursor& in_;
  std::vector<MaterialField>& fields_;
};

class Writer {
public:
  Writer(const MaterialFile& file, std::vector<std::byte>& out) : file_(file), out_(out) {}

  template <typename T>
  T field(std::string_view name) {
    const T value = lookup<T>(name);
    encode(out_, value);
    return value;
  }

private:
  template <typename T>
  T lookup(std::string_view name) const {
    const auto* found = find_field(file_, name);
    if (found == nullptr) {
      return T{};
    }
    if (const auto* value = std::get_if<T>(&found->value)) {
      return *value;
    }
    throw std::invalid_argument("material field " + std::string(name) + " has a different type");
  }

  const MaterialFile& file_;
  std::vector<std::byte>& out_;
};

template <typename Access>
class Fields {
public:
  explicit Fields(Access& access) : access_(access) {}

  bool flag(std::string_view name) { return access_.template field<RawBool>(name).raw != 0; }
  std::uint8_t u8(std::string_view name) { return access_.template field<std::uint8_t>(name); }
  std::uint32_t u32(std::string_view name) { return access_.template field<std::uint32_t>(name); }
  float f32(std::string_view name) { return access_.template field<float>(name); }
  Color3 color(std::string_view name) { return access_.template field<Color3>(name); }
  std::string str(std::string_view name) { return access_.template field<std::string>(name); }

private:
  Access& access_;
};

template <typename Access>
void visit_bgsm(Fields<Access>& f, std::uint32_t version) {
  if (version > 2) {
    f.str("DiffuseTexture");
    f.str("NormalTexture");
    f.str("SmoothSpecTexture");
    f.str("GreyscaleTexture");
    f.str("GlowTexture");
    f.str("WrinklesTexture");
    f.str("SpecularTexture");
    f.str("LightingTexture");
    f.str("FlowTexture");
    if (version >= 17) {
      f.str("DistanceFieldAlphaTexture");
    }
  } else {
    f.str("DiffuseTexture");
    f.str("NormalTexture");
    f.str("SmoothSpecTexture");
    f.str("GreyscaleTexture");
    f.str("EnvmapTexture");
    f.str("GlowTexture");
    f.str("InnerLayerTexture");
    f.str("WrinklesTexture");
    f.str("DisplacementTexture");
  }
  f.flag("EnableEditorAlphaRef");
  if (version >= 8) {
    f.flag("Translucency");
    f.flag("TranslucencyThickObject");
    f.flag("TranslucencyMixAlbedoWithSubsurfaceColor");
    f.color("TranslucencySubsurfaceColor");
    f.f32("TranslucencyTransmissiveScale");
    f.f32("TranslucencyTurbulence");
  } else {
    f.flag("RimLighting");
    f.f32("RimPower");
    f.f32("BackLightPower");
    f.flag("SubsurfaceLighting");
    f.f32("SubsurfaceLightingRolloff");
  }
  f.flag("SpecularEnabled");
  f.color("SpecularColor");
  f.f32("SpecularMult");
  f.f32("Smoothness");
  f.f32("FresnelPower");
  f.f32("WetnessControlSpecScale");
  f.f32("WetnessControlSpecPowerScale");
  f.f32("WetnessControlSpecMinvar");
  if (version < 10) {
    f.f32("WetnessControlEnvMapScale");
  }
  f.f32("WetnessControlFresnelPower");
  f.f32("WetnessControlMetalness");
  if (version > 2) {
    f.flag("Pbr");
    if (version >= 9) {
      f.flag("CustomPorosity");
      f.f32("PorosityValue");
    }
  }
  f.str("RootMaterialPath");
  f.flag("AnisoLighting");
  if (f.flag("EmitEnabled")) {
    f.color("EmittanceColor");
  }
  f.f32("EmittanceMult");
  f.flag("ModelSpaceNormals");
  f.flag("ExternalEmittance");
  if (version >= 12) {
    f.f32("LumEmittance");
  }
  if (version >= 13) {
    f.flag("UseAdaptativeEmissive");
    f.f32("AdaptativeEmissiveExposureOffset");
    f.f32("AdaptativeEmissiveFinalExposureMin");
    f.f32("AdaptativeEmissiveFinalExposureMax");
  }
  if (version < 8) {
    f.flag("BackLighting");
  }
  f.flag("ReceiveShadows");
  f.flag("HideSecret");
  f.flag("CastShadows");
  f.flag("DissolveFade");
  f.flag("AssumeShadowmask");
  f.flag("Glowmap");
  if (version < 7) {
    f.flag("EnvironmentMappingWindow");
    f.flag("EnvironmentMappingEye");
  }
  f.flag("Hair");
  f.color("HairTintColor");
  f.flag("Tree");
  f.flag("Facegen");
  f.flag("SkinTint");
  f.flag("Tessellate");
  if (version < 3) {
    f.f32("DisplacementTextureBias");
    f.f32("DisplacementTextureScale");
    f.f32("TessellationPnScale");
    f.f32("TessellationBaseFactor");
    f.f32("TessellationFadeDistance");
  }
  f.f32("GrayscaleToPaletteScale");
  if (version >= 1) {
    f.flag("SkewSpecularAlpha");
  }
  if (version >= 3 && f.flag("Terrain")) {
    if (version == 3) {
      f.u32("UnkInt1");
    }
    f.f32("TerrainThresholdFalloff");
    f.f32("TerrainTilingDistance");
    f.f32("TerrainRotationAngle");
  }
}

template <typename Access>
void visit_bgem(Fields<Access>& f, std::uint32_t version) {
  f.str("BaseTexture");
  f.str("GrayscaleTexture");
  f.str("EnvmapTexture");
  f.str("NormalTexture");
  f.str("EnvmapMaskTexture");
  if (version >= 11) {
    f.str("SpecularTexture");
    f.str("LightingTexture");
    f.str("GlowTexture");
  }
  if (version >= 21) {
    f.str("GlassRoughnessScratch");
    f.str("GlassDirtOverlay");
    if (f.flag("GlassEnabled")) {
      f.color("GlassFresnelColor");
      f.f32("GlassBlurScaleBase");
      if (version >= 22) {
        f.f32("GlassBlurScaleFactor");
      }
      f.f32("GlassRefractionScaleBase");
    }
  }
  if (version >= 10) {
    f.flag("EffectEnvironmentMapping");
    f.f32("EffectEnvironmentMappingMaskScale");
  }
  f.flag("BloodEnabled");
  f.flag("EffectLightingEnabled");
  f.flag("FalloffEnabled");
  f.flag("FalloffColorEnabled");
  f.flag("GrayscaleToPaletteAlpha");
  f.flag("SoftEnabled");
  f.color("BaseColor");
  f.f32("BaseColorScale");
  f.f32("FalloffStartAngle");
  f.f32("FalloffStopAngle");
  f.f32("FalloffStartOpacity");
  f.f32("FalloffStopOpacity");
  f.f32("LightingInfluence");
  f.u8("EnvmapMinLOD");
  f.f32("SoftDepth");
  if (version >= 11) {
    f.color("EmittanceColor");
  }
  if (version >= 15) {
    f.f32("AdaptativeEmissiveExposureOffset");
    f.f32("AdaptativeEmissiveFinalExposureMin");
    f.f32("AdaptativeEmissiveFinalExposureMax");
  }
  if (version >= 16) {
    f.flag("Glowmap");
  }
  if (version >= 20) {
    f.flag("EffectPbrSpecular");
  }
}

template <typename Access>
void visit_layout(Access& access, MaterialFormat format) {
  Fields<Access> f(access);
  f.u32("Signature");
  const auto version = f.u32("Version");
  f.u32("TileFlags");
  f.f32("UOffset");
  f.f32("VOffset");
  f.f32("UScale");
  f.f32("VScale");
  f.f32("Alpha");
  f.u8("AlphaBlendMode0");
  f.u32("AlphaBlendMode1");
  f.u32("AlphaBlendMode2");
  f.u8("AlphaTestRef");
  f.flag("AlphaTest");
  f.flag("ZBufferWrite");
  f.flag("ZBufferTest");
  f.flag("ScreenSpaceReflections");
  f.flag("WetnessControlScreenSpaceReflections");
  f.flag("Decal");
  f.flag("TwoSided");
  f.flag("DecalNoFade");
  f.flag("NonOccluder");
  f.flag("Refraction");
  f.flag("RefractionFalloff");
  f.f32("RefractionPower");
  if (version < 10) {
    f.flag("EnvironmentMapping");
    f.f32("EnvironmentMappingMaskScale");
  } else {
    f.flag("DepthBias");
  }
  f.flag("GrayscaleToPaletteColor");
  if (version >= 6) {
    f.u8("MaskWrites");
  }
  if (format == MaterialFormat::bgem) {
    visit_bgem(f, version);
  } else {
    visit_bgsm(f, version);
  }
}

}  // namespace

MaterialFile read_material_file(std::span<const std::byte> bytes) {
  MaterialFile file;
  switch (binary::Cursor(bytes).read_u32()) {
    case k_bgsm_magic:
      file.format = MaterialFormat::bgsm;
      break;
    case k_bgem_magic:
      file.format = MaterialFormat::bgem;
      break;
    default:
      throw fo4::parse_error(0, "not a BGSM or BGEM file: bad magic");
  }
  binary::Cursor cursor(bytes);
  Reader reader(cursor, file.fields);
  visit_layout(reader, file.format);
  const auto rest = cursor.read_bytes(cursor.remaining());
  file.trailing.assign(rest.begin(), rest.end());
  return file;
}

std::vector<std::byte> write_material_file(const MaterialFile& file) {
  std::vector<std::byte> out;
  Writer writer(file, out);
  visit_layout(writer, file.format);
  out.insert(out.end(), file.trailing.begin(), file.trailing.end());
  return out;
}

std::uint32_t MaterialFile::version() const {
  return get_field<std::uint32_t>(*this, "Version").value_or(std::uint32_t{0});
}

const MaterialField* find_field(const MaterialFile& file, std::string_view name) noexcept {
  for (const auto& field : file.fields) {
    if (equal_ignore_case(field.name, name)) {
      return &field;
    }
  }
  return nullptr;
}

MaterialField* find_field(MaterialFile& file, std::string_view name) noexcept {
  return const_cast<MaterialField*>(find_field(std::as_const(file), name));
}

std::string_view text(std::string_view raw) noexcept {
  if (!raw.empty() && raw.back() == '\0') {
    raw.remove_suffix(1);
  }
  return raw;
}

bool set_text(MaterialFile& file, std::string_view name, std::string_view value) {
  std::string stored(value);
  stored.push_back('\0');
  return set_field(file, name, std::move(stored));
}

void check_settable(std::string_view name) {
  if (equal_ignore_case(name, "Version") || equal_ignore_case(name, "Signature")) {
    throw std::invalid_argument("material field " + std::string(name) + " is read-only");
  }
}

}  // namespace fo4::material
