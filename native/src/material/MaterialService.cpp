#include <fo4ide/material/MaterialService.hpp>

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <type_traits>

namespace fo4ide::material {
namespace {

constexpr std::uint32_t BgsmSignature = 0x4D534742U;
constexpr std::uint32_t BgemSignature = 0x4D454742U;

std::string lower(std::string_view value) {
    std::string result(value);
    std::ranges::transform(result, result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

class Reader {
public:
    explicit Reader(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {}

    std::uint8_t u8() {
        require(1);
        return bytes_[pos_++];
    }

    bool boolean() { return u8() != 0; }

    std::uint32_t u32() {
        require(4);
        const auto value = static_cast<std::uint32_t>(bytes_[pos_]) |
            (static_cast<std::uint32_t>(bytes_[pos_ + 1]) << 8U) |
            (static_cast<std::uint32_t>(bytes_[pos_ + 2]) << 16U) |
            (static_cast<std::uint32_t>(bytes_[pos_ + 3]) << 24U);
        pos_ += 4;
        return value;
    }

    float f32() { return std::bit_cast<float>(u32()); }

    std::string string() {
        const auto length = u32();
        if (length == 0) return {};
        if (length > static_cast<std::uint32_t>(std::numeric_limits<std::size_t>::max())) {
            throw std::runtime_error("material string length is too large");
        }
        require(static_cast<std::size_t>(length));
        const auto* begin = reinterpret_cast<const char*>(bytes_.data() + pos_);
        std::string value(begin, begin + length);
        pos_ += length;
        return value;
    }

    Color3 color3() { return {f32(), f32(), f32()}; }

    std::vector<std::uint8_t> remaining() const {
        return {bytes_.begin() + static_cast<std::ptrdiff_t>(pos_), bytes_.end()};
    }

private:
    void require(std::size_t count) const {
        if (count > bytes_.size() - pos_) throw std::runtime_error("unexpected EOF in material file");
    }

    std::vector<std::uint8_t> bytes_;
    std::size_t pos_{0};
};

class Writer {
public:
    void u8(std::uint8_t value) { bytes_.push_back(value); }
    void boolean(bool value) { u8(value ? 1U : 0U); }

    void u32(std::uint32_t value) {
        bytes_.push_back(static_cast<std::uint8_t>(value & 0xFFU));
        bytes_.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
        bytes_.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
        bytes_.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
    }

    void f32(float value) { u32(std::bit_cast<std::uint32_t>(value)); }

    void string(std::string_view value) {
        u32(static_cast<std::uint32_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    void materialString(std::string value) {
        if (value.empty()) value.push_back('\0');
        else if (value.back() != '\0') value.push_back('\0');
        string(value);
    }

    void color3(const Color3& value) {
        f32(value[0]);
        f32(value[1]);
        f32(value[2]);
    }

    void append(const std::vector<std::uint8_t>& bytes) {
        bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
    }

    std::vector<std::uint8_t> finish() { return std::move(bytes_); }

private:
    std::vector<std::uint8_t> bytes_;
};

void add(Document& document, std::string name, FieldValue value) {
    document.fields.push_back({std::move(name), std::move(value)});
}

const Field* findField(const Document& document, std::string_view name) {
    const auto wanted = lower(name);
    const auto it = std::ranges::find_if(document.fields, [&](const Field& field) {
        return lower(field.name) == wanted;
    });
    return it == document.fields.end() ? nullptr : &*it;
}

Field* findField(Document& document, std::string_view name) {
    const auto wanted = lower(name);
    const auto it = std::ranges::find_if(document.fields, [&](const Field& field) {
        return lower(field.name) == wanted;
    });
    return it == document.fields.end() ? nullptr : &*it;
}

template <class T>
T get(const Document& document, std::string_view name, T fallback = {}) {
    const auto* field = findField(document, name);
    if (!field) return fallback;
    if (const auto* value = std::get_if<T>(&field->value)) return *value;
    return fallback;
}

Header readHeader(Reader& reader, std::uint32_t expected, std::string_view format) {
    Header header;
    header.signature = reader.u32();
    if (header.signature != expected) {
        std::ostringstream message;
        message << "Invalid " << format << " signature: 0x" << std::hex << std::uppercase << header.signature;
        throw std::runtime_error(message.str());
    }
    header.version = reader.u32();
    const auto tileFlags = reader.u32();
    header.tileU = (tileFlags & 2U) != 0;
    header.tileV = (tileFlags & 1U) != 0;
    header.uOffset = reader.f32();
    header.vOffset = reader.f32();
    header.uScale = reader.f32();
    header.vScale = reader.f32();
    header.alpha = reader.f32();
    header.alphaBlendMode0 = reader.u8();
    header.alphaBlendMode1 = reader.u32();
    header.alphaBlendMode2 = reader.u32();
    header.alphaTestRef = reader.u8();
    header.alphaTest = reader.boolean();
    header.zBufferWrite = reader.boolean();
    header.zBufferTest = reader.boolean();
    header.ssr = reader.boolean();
    header.wetSsr = reader.boolean();
    header.decal = reader.boolean();
    header.twoSided = reader.boolean();
    header.decalNoFade = reader.boolean();
    header.nonOccluder = reader.boolean();
    header.refraction = reader.boolean();
    header.refractionFalloff = reader.boolean();
    header.refractionPower = reader.f32();
    if (header.version < 10) {
        header.envMapping = reader.boolean();
        header.envMappingMaskScale = reader.f32();
    } else {
        header.depthBias = reader.boolean();
    }
    header.grayscaleToPaletteColor = reader.boolean();
    if (header.version >= 6) header.maskWrites = reader.u8();
    return header;
}

void writeHeader(Writer& writer, const Header& header) {
    writer.u32(header.signature);
    writer.u32(header.version);
    writer.u32((header.tileU ? 2U : 0U) | (header.tileV ? 1U : 0U));
    writer.f32(header.uOffset);
    writer.f32(header.vOffset);
    writer.f32(header.uScale);
    writer.f32(header.vScale);
    writer.f32(header.alpha);
    writer.u8(header.alphaBlendMode0);
    writer.u32(header.alphaBlendMode1);
    writer.u32(header.alphaBlendMode2);
    writer.u8(header.alphaTestRef);
    writer.boolean(header.alphaTest);
    writer.boolean(header.zBufferWrite);
    writer.boolean(header.zBufferTest);
    writer.boolean(header.ssr);
    writer.boolean(header.wetSsr);
    writer.boolean(header.decal);
    writer.boolean(header.twoSided);
    writer.boolean(header.decalNoFade);
    writer.boolean(header.nonOccluder);
    writer.boolean(header.refraction);
    writer.boolean(header.refractionFalloff);
    writer.f32(header.refractionPower);
    if (header.version < 10) {
        writer.boolean(header.envMapping);
        writer.f32(header.envMappingMaskScale);
    } else {
        writer.boolean(header.depthBias);
    }
    writer.boolean(header.grayscaleToPaletteColor);
    if (header.version >= 6) writer.u8(header.maskWrites);
}

void parseBgsm(Document& document, Reader& reader) {
    const auto version = document.header.version;
    add(document, "DiffuseTexture", reader.string());
    add(document, "NormalTexture", reader.string());
    add(document, "SmoothSpecTexture", reader.string());
    add(document, "GreyscaleTexture", reader.string());
    if (version > 2) {
        add(document, "GlowTexture", reader.string());
        add(document, "WrinklesTexture", reader.string());
        add(document, "SpecularTexture", reader.string());
        add(document, "LightingTexture", reader.string());
        add(document, "FlowTexture", reader.string());
        if (version >= 17) add(document, "DistanceFieldAlphaTexture", reader.string());
    } else {
        add(document, "EnvmapTexture", reader.string());
        add(document, "GlowTexture", reader.string());
        add(document, "InnerLayerTexture", reader.string());
        add(document, "WrinklesTexture", reader.string());
        add(document, "DisplacementTexture", reader.string());
    }

    add(document, "EnableEditorAlphaRef", reader.boolean());
    if (version >= 8) {
        add(document, "Translucency", reader.boolean());
        add(document, "TranslucencyThickObject", reader.boolean());
        add(document, "TranslucencyMixAlbedoWithSubsurfaceColor", reader.boolean());
        add(document, "TranslucencySubsurfaceColor", reader.color3());
        add(document, "TranslucencyTransmissiveScale", reader.f32());
        add(document, "TranslucencyTurbulence", reader.f32());
    } else {
        add(document, "RimLighting", reader.boolean());
        add(document, "RimPower", reader.f32());
        add(document, "BackLightPower", reader.f32());
        add(document, "SubsurfaceLighting", reader.boolean());
        add(document, "SubsurfaceLightingRolloff", reader.f32());
    }

    add(document, "SpecularEnabled", reader.boolean());
    add(document, "SpecularColor", reader.color3());
    add(document, "SpecularMult", reader.f32());
    add(document, "Smoothness", reader.f32());
    add(document, "FresnelPower", reader.f32());
    add(document, "WetnessControlSpecScale", reader.f32());
    add(document, "WetnessControlSpecPowerScale", reader.f32());
    add(document, "WetnessControlSpecMinvar", reader.f32());
    if (version < 10) add(document, "WetnessControlEnvMapScale", reader.f32());
    add(document, "WetnessControlFresnelPower", reader.f32());
    add(document, "WetnessControlMetalness", reader.f32());

    if (version > 2) {
        add(document, "Pbr", reader.boolean());
        if (version >= 9) {
            add(document, "CustomPorosity", reader.boolean());
            add(document, "PorosityValue", reader.f32());
        }
    }

    add(document, "RootMaterialPath", reader.string());
    add(document, "AnisoLighting", reader.boolean());
    const auto emitEnabled = reader.boolean();
    add(document, "EmitEnabled", emitEnabled);
    if (emitEnabled) add(document, "EmittanceColor", reader.color3());
    add(document, "EmittanceMult", reader.f32());
    add(document, "ModelSpaceNormals", reader.boolean());
    add(document, "ExternalEmittance", reader.boolean());
    if (version >= 12) add(document, "LumEmittance", reader.f32());
    if (version >= 13) {
        add(document, "UseAdaptativeEmissive", reader.boolean());
        add(document, "AdaptativeEmissiveExposureOffset", reader.f32());
        add(document, "AdaptativeEmissiveFinalExposureMin", reader.f32());
        add(document, "AdaptativeEmissiveFinalExposureMax", reader.f32());
    }

    if (version < 8) add(document, "BackLighting", reader.boolean());
    add(document, "ReceiveShadows", reader.boolean());
    add(document, "HideSecret", reader.boolean());
    add(document, "CastShadows", reader.boolean());
    add(document, "DissolveFade", reader.boolean());
    add(document, "AssumeShadowmask", reader.boolean());
    add(document, "Glowmap", reader.boolean());
    if (version < 7) {
        add(document, "EnvironmentMappingWindow", reader.boolean());
        add(document, "EnvironmentMappingEye", reader.boolean());
    }

    add(document, "Hair", reader.boolean());
    add(document, "HairTintColor", reader.color3());
    add(document, "Tree", reader.boolean());
    add(document, "Facegen", reader.boolean());
    add(document, "SkinTint", reader.boolean());
    add(document, "Tessellate", reader.boolean());
    if (version < 3) {
        add(document, "DisplacementTextureBias", reader.f32());
        add(document, "DisplacementTextureScale", reader.f32());
        add(document, "TessellationPnScale", reader.f32());
        add(document, "TessellationBaseFactor", reader.f32());
        add(document, "TessellationFadeDistance", reader.f32());
    }
    add(document, "GrayscaleToPaletteScale", reader.f32());
    if (version >= 1) add(document, "SkewSpecularAlpha", reader.boolean());
    if (version >= 3) {
        const auto terrain = reader.boolean();
        add(document, "Terrain", terrain);
        if (terrain) {
            if (version == 3) add(document, "UnkInt1", reader.u32());
            add(document, "TerrainThresholdFalloff", reader.f32());
            add(document, "TerrainTilingDistance", reader.f32());
            add(document, "TerrainRotationAngle", reader.f32());
        }
    }
}

void parseBgem(Document& document, Reader& reader) {
    const auto version = document.header.version;
    add(document, "BaseTexture", reader.string());
    add(document, "GrayscaleTexture", reader.string());
    add(document, "EnvmapTexture", reader.string());
    add(document, "NormalTexture", reader.string());
    add(document, "EnvmapMaskTexture", reader.string());
    if (version >= 11) {
        add(document, "SpecularTexture", reader.string());
        add(document, "LightingTexture", reader.string());
        add(document, "GlowTexture", reader.string());
    }
    if (version >= 21) {
        add(document, "GlassRoughnessScratch", reader.string());
        add(document, "GlassDirtOverlay", reader.string());
        const auto enabled = reader.boolean();
        add(document, "GlassEnabled", enabled);
        if (enabled) {
            add(document, "GlassFresnelColor", reader.color3());
            add(document, "GlassBlurScaleBase", reader.f32());
            if (version >= 22) add(document, "GlassBlurScaleFactor", reader.f32());
            add(document, "GlassRefractionScaleBase", reader.f32());
        }
    }
    if (version >= 10) {
        add(document, "EnvironmentMapping", reader.boolean());
        add(document, "EnvironmentMappingMaskScale", reader.f32());
    }
    add(document, "BloodEnabled", reader.boolean());
    add(document, "EffectLightingEnabled", reader.boolean());
    add(document, "FalloffEnabled", reader.boolean());
    add(document, "FalloffColorEnabled", reader.boolean());
    add(document, "GrayscaleToPaletteAlpha", reader.boolean());
    add(document, "SoftEnabled", reader.boolean());
    add(document, "BaseColor", reader.color3());
    add(document, "BaseColorScale", reader.f32());
    add(document, "FalloffStartAngle", reader.f32());
    add(document, "FalloffStopAngle", reader.f32());
    add(document, "FalloffStartOpacity", reader.f32());
    add(document, "FalloffStopOpacity", reader.f32());
    add(document, "LightingInfluence", reader.f32());
    add(document, "EnvmapMinLOD", reader.u8());
    add(document, "SoftDepth", reader.f32());
    if (version >= 11) add(document, "EmittanceColor", reader.color3());
    if (version >= 15) {
        add(document, "AdaptativeEmissiveExposureOffset", reader.f32());
        add(document, "AdaptativeEmissiveFinalExposureMin", reader.f32());
        add(document, "AdaptativeEmissiveFinalExposureMax", reader.f32());
    }
    if (version >= 16) add(document, "Glowmap", reader.boolean());
    if (version >= 20) add(document, "EffectPbrSpecular", reader.boolean());
}

void writeBgsm(const Document& document, Writer& writer) {
    const auto version = document.header.version;
    writer.materialString(get<std::string>(document, "DiffuseTexture"));
    writer.materialString(get<std::string>(document, "NormalTexture"));
    writer.materialString(get<std::string>(document, "SmoothSpecTexture"));
    writer.materialString(get<std::string>(document, "GreyscaleTexture"));
    if (version > 2) {
        writer.materialString(get<std::string>(document, "GlowTexture"));
        writer.materialString(get<std::string>(document, "WrinklesTexture"));
        writer.materialString(get<std::string>(document, "SpecularTexture"));
        writer.materialString(get<std::string>(document, "LightingTexture"));
        writer.materialString(get<std::string>(document, "FlowTexture"));
        if (version >= 17) writer.materialString(get<std::string>(document, "DistanceFieldAlphaTexture"));
    } else {
        writer.materialString(get<std::string>(document, "EnvmapTexture"));
        writer.materialString(get<std::string>(document, "GlowTexture"));
        writer.materialString(get<std::string>(document, "InnerLayerTexture"));
        writer.materialString(get<std::string>(document, "WrinklesTexture"));
        writer.materialString(get<std::string>(document, "DisplacementTexture"));
    }

    writer.boolean(get<bool>(document, "EnableEditorAlphaRef"));
    if (version >= 8) {
        writer.boolean(get<bool>(document, "Translucency"));
        writer.boolean(get<bool>(document, "TranslucencyThickObject"));
        writer.boolean(get<bool>(document, "TranslucencyMixAlbedoWithSubsurfaceColor"));
        writer.color3(get<Color3>(document, "TranslucencySubsurfaceColor", {1.0F, 1.0F, 1.0F}));
        writer.f32(get<float>(document, "TranslucencyTransmissiveScale"));
        writer.f32(get<float>(document, "TranslucencyTurbulence"));
    } else {
        writer.boolean(get<bool>(document, "RimLighting"));
        writer.f32(get<float>(document, "RimPower"));
        writer.f32(get<float>(document, "BackLightPower"));
        writer.boolean(get<bool>(document, "SubsurfaceLighting"));
        writer.f32(get<float>(document, "SubsurfaceLightingRolloff"));
    }

    writer.boolean(get<bool>(document, "SpecularEnabled"));
    writer.color3(get<Color3>(document, "SpecularColor"));
    writer.f32(get<float>(document, "SpecularMult"));
    writer.f32(get<float>(document, "Smoothness"));
    writer.f32(get<float>(document, "FresnelPower"));
    writer.f32(get<float>(document, "WetnessControlSpecScale"));
    writer.f32(get<float>(document, "WetnessControlSpecPowerScale"));
    writer.f32(get<float>(document, "WetnessControlSpecMinvar"));
    if (version < 10) writer.f32(get<float>(document, "WetnessControlEnvMapScale"));
    writer.f32(get<float>(document, "WetnessControlFresnelPower"));
    writer.f32(get<float>(document, "WetnessControlMetalness"));
    if (version > 2) {
        writer.boolean(get<bool>(document, "Pbr"));
        if (version >= 9) {
            writer.boolean(get<bool>(document, "CustomPorosity"));
            writer.f32(get<float>(document, "PorosityValue"));
        }
    }

    writer.materialString(get<std::string>(document, "RootMaterialPath"));
    writer.boolean(get<bool>(document, "AnisoLighting"));
    const auto emitEnabled = get<bool>(document, "EmitEnabled");
    writer.boolean(emitEnabled);
    if (emitEnabled) writer.color3(get<Color3>(document, "EmittanceColor", {1.0F, 1.0F, 1.0F}));
    writer.f32(get<float>(document, "EmittanceMult"));
    writer.boolean(get<bool>(document, "ModelSpaceNormals"));
    writer.boolean(get<bool>(document, "ExternalEmittance"));
    if (version >= 12) writer.f32(get<float>(document, "LumEmittance"));
    if (version >= 13) {
        writer.boolean(get<bool>(document, "UseAdaptativeEmissive"));
        writer.f32(get<float>(document, "AdaptativeEmissiveExposureOffset"));
        writer.f32(get<float>(document, "AdaptativeEmissiveFinalExposureMin"));
        writer.f32(get<float>(document, "AdaptativeEmissiveFinalExposureMax"));
    }

    if (version < 8) writer.boolean(get<bool>(document, "BackLighting"));
    writer.boolean(get<bool>(document, "ReceiveShadows"));
    writer.boolean(get<bool>(document, "HideSecret"));
    writer.boolean(get<bool>(document, "CastShadows"));
    writer.boolean(get<bool>(document, "DissolveFade"));
    writer.boolean(get<bool>(document, "AssumeShadowmask"));
    writer.boolean(get<bool>(document, "Glowmap"));
    if (version < 7) {
        writer.boolean(get<bool>(document, "EnvironmentMappingWindow"));
        writer.boolean(get<bool>(document, "EnvironmentMappingEye"));
    }

    writer.boolean(get<bool>(document, "Hair"));
    writer.color3(get<Color3>(document, "HairTintColor"));
    writer.boolean(get<bool>(document, "Tree"));
    writer.boolean(get<bool>(document, "Facegen"));
    writer.boolean(get<bool>(document, "SkinTint"));
    writer.boolean(get<bool>(document, "Tessellate"));
    if (version < 3) {
        writer.f32(get<float>(document, "DisplacementTextureBias"));
        writer.f32(get<float>(document, "DisplacementTextureScale"));
        writer.f32(get<float>(document, "TessellationPnScale"));
        writer.f32(get<float>(document, "TessellationBaseFactor"));
        writer.f32(get<float>(document, "TessellationFadeDistance"));
    }
    writer.f32(get<float>(document, "GrayscaleToPaletteScale"));
    if (version >= 1) writer.boolean(get<bool>(document, "SkewSpecularAlpha"));
    if (version >= 3) {
        const auto terrain = get<bool>(document, "Terrain");
        writer.boolean(terrain);
        if (terrain) {
            if (version == 3) writer.u32(get<std::uint32_t>(document, "UnkInt1"));
            writer.f32(get<float>(document, "TerrainThresholdFalloff"));
            writer.f32(get<float>(document, "TerrainTilingDistance"));
            writer.f32(get<float>(document, "TerrainRotationAngle"));
        }
    }
}

void writeBgem(const Document& document, Writer& writer) {
    const auto version = document.header.version;
    writer.materialString(get<std::string>(document, "BaseTexture"));
    writer.materialString(get<std::string>(document, "GrayscaleTexture"));
    writer.materialString(get<std::string>(document, "EnvmapTexture"));
    writer.materialString(get<std::string>(document, "NormalTexture"));
    writer.materialString(get<std::string>(document, "EnvmapMaskTexture"));
    if (version >= 11) {
        writer.materialString(get<std::string>(document, "SpecularTexture"));
        writer.materialString(get<std::string>(document, "LightingTexture"));
        writer.materialString(get<std::string>(document, "GlowTexture"));
    }
    if (version >= 21) {
        writer.materialString(get<std::string>(document, "GlassRoughnessScratch"));
        writer.materialString(get<std::string>(document, "GlassDirtOverlay"));
        const auto enabled = get<bool>(document, "GlassEnabled");
        writer.boolean(enabled);
        if (enabled) {
            writer.color3(get<Color3>(document, "GlassFresnelColor", {1.0F, 1.0F, 1.0F}));
            writer.f32(get<float>(document, "GlassBlurScaleBase"));
            if (version >= 22) writer.f32(get<float>(document, "GlassBlurScaleFactor"));
            writer.f32(get<float>(document, "GlassRefractionScaleBase"));
        }
    }
    if (version >= 10) {
        writer.boolean(get<bool>(document, "EnvironmentMapping"));
        writer.f32(get<float>(document, "EnvironmentMappingMaskScale"));
    }
    writer.boolean(get<bool>(document, "BloodEnabled"));
    writer.boolean(get<bool>(document, "EffectLightingEnabled"));
    writer.boolean(get<bool>(document, "FalloffEnabled"));
    writer.boolean(get<bool>(document, "FalloffColorEnabled"));
    writer.boolean(get<bool>(document, "GrayscaleToPaletteAlpha"));
    writer.boolean(get<bool>(document, "SoftEnabled"));
    writer.color3(get<Color3>(document, "BaseColor"));
    writer.f32(get<float>(document, "BaseColorScale"));
    writer.f32(get<float>(document, "FalloffStartAngle"));
    writer.f32(get<float>(document, "FalloffStopAngle"));
    writer.f32(get<float>(document, "FalloffStartOpacity"));
    writer.f32(get<float>(document, "FalloffStopOpacity"));
    writer.f32(get<float>(document, "LightingInfluence"));
    writer.u8(get<std::uint8_t>(document, "EnvmapMinLOD"));
    writer.f32(get<float>(document, "SoftDepth"));
    if (version >= 11) writer.color3(get<Color3>(document, "EmittanceColor", {1.0F, 1.0F, 1.0F}));
    if (version >= 15) {
        writer.f32(get<float>(document, "AdaptativeEmissiveExposureOffset"));
        writer.f32(get<float>(document, "AdaptativeEmissiveFinalExposureMin"));
        writer.f32(get<float>(document, "AdaptativeEmissiveFinalExposureMax"));
    }
    if (version >= 16) writer.boolean(get<bool>(document, "Glowmap"));
    if (version >= 20) writer.boolean(get<bool>(document, "EffectPbrSpecular"));
}

std::string formatFloat(float value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << value;
    auto text = stream.str();
    while (!text.empty() && text.back() == '0') text.pop_back();
    if (!text.empty() && text.back() == '.') text.pop_back();
    if (text == "-0") text = "0";
    return text;
}

std::string displayValue(const FieldValue& value) {
    return std::visit([](const auto& item) -> std::string {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, bool>) return item ? "true" : "false";
        else if constexpr (std::is_same_v<T, std::uint8_t> || std::is_same_v<T, std::uint32_t>) return std::to_string(item);
        else if constexpr (std::is_same_v<T, float>) return formatFloat(item);
        else if constexpr (std::is_same_v<T, std::string>) {
            auto text = item;
            while (!text.empty() && text.back() == '\0') text.pop_back();
            return text;
        } else {
            return formatFloat(item[0]) + ", " + formatFloat(item[1]) + ", " + formatFloat(item[2]);
        }
    }, value);
}

std::string fieldType(const FieldValue& value) {
    if (std::holds_alternative<bool>(value)) return "bool";
    if (std::holds_alternative<std::uint8_t>(value) || std::holds_alternative<std::uint32_t>(value)) return "int";
    if (std::holds_alternative<float>(value)) return "float";
    if (std::holds_alternative<Color3>(value)) return "color";
    return "string";
}

bool parseBool(std::string value) {
    value = lower(value);
    if (value == "true" || value == "1" || value == "yes" || value == "on") return true;
    if (value == "false" || value == "0" || value == "no" || value == "off") return false;
    throw std::runtime_error("expected true or false");
}

Color3 parseColor(std::string value) {
    std::ranges::replace(value, ',', ' ');
    std::istringstream stream(value);
    Color3 color{};
    if (!(stream >> color[0] >> color[1] >> color[2])) {
        throw std::runtime_error("expected 3 comma/space-separated numbers, e.g. '1.0, 0.5, 0.2'");
    }
    std::string extra;
    if (stream >> extra) throw std::runtime_error("expected exactly 3 color components");
    return color;
}

void assign(Field& field, const std::string& text) {
    field.value = std::visit([&](const auto& current) -> FieldValue {
        using T = std::decay_t<decltype(current)>;
        if constexpr (std::is_same_v<T, bool>) return parseBool(text);
        else if constexpr (std::is_same_v<T, std::uint8_t>) {
            const auto value = std::stoul(text);
            if (value > 255U) throw std::runtime_error("value is outside byte range");
            return static_cast<std::uint8_t>(value);
        } else if constexpr (std::is_same_v<T, std::uint32_t>) {
            const auto value = std::stoull(text);
            if (value > std::numeric_limits<std::uint32_t>::max()) throw std::runtime_error("value is outside uint32 range");
            return static_cast<std::uint32_t>(value);
        } else if constexpr (std::is_same_v<T, float>) return std::stof(text);
        else if constexpr (std::is_same_v<T, std::string>) return text;
        else return parseColor(text);
    }, field.value);
}

nlohmann::json materialFieldsJson(const Document& document) {
    nlohmann::json fields = nlohmann::json::array();
    for (const auto& field : document.fields) {
        fields.push_back({
            {"name", field.name},
            {"section", "material"},
            {"type", fieldType(field.value)},
            {"value", displayValue(field.value)}
        });
    }
    return fields;
}

void addHeaderField(nlohmann::json& fields, std::string name, FieldValue value) {
    fields.push_back({
        {"name", std::move(name)},
        {"section", "header"},
        {"type", fieldType(value)},
        {"value", displayValue(value)}
    });
}

void appendHeaderJson(nlohmann::json& fields, const Header& h) {
    addHeaderField(fields, "Signature", h.signature);
    addHeaderField(fields, "Version", h.version);
    addHeaderField(fields, "TileU", h.tileU);
    addHeaderField(fields, "TileV", h.tileV);
    addHeaderField(fields, "UOffset", h.uOffset);
    addHeaderField(fields, "VOffset", h.vOffset);
    addHeaderField(fields, "UScale", h.uScale);
    addHeaderField(fields, "VScale", h.vScale);
    addHeaderField(fields, "Alpha", h.alpha);
    addHeaderField(fields, "AlphaBlendMode0", h.alphaBlendMode0);
    addHeaderField(fields, "AlphaBlendMode1", h.alphaBlendMode1);
    addHeaderField(fields, "AlphaBlendMode2", h.alphaBlendMode2);
    addHeaderField(fields, "AlphaTestRef", h.alphaTestRef);
    addHeaderField(fields, "AlphaTest", h.alphaTest);
    addHeaderField(fields, "ZBufferWrite", h.zBufferWrite);
    addHeaderField(fields, "ZBufferTest", h.zBufferTest);
    addHeaderField(fields, "Ssr", h.ssr);
    addHeaderField(fields, "WetSsr", h.wetSsr);
    addHeaderField(fields, "Decal", h.decal);
    addHeaderField(fields, "TwoSided", h.twoSided);
    addHeaderField(fields, "DecalNoFade", h.decalNoFade);
    addHeaderField(fields, "NonOccluder", h.nonOccluder);
    addHeaderField(fields, "Refraction", h.refraction);
    addHeaderField(fields, "RefractionFalloff", h.refractionFalloff);
    addHeaderField(fields, "RefractionPower", h.refractionPower);
    if (h.version < 10) {
        addHeaderField(fields, "EnvMapping", h.envMapping);
        addHeaderField(fields, "EnvMappingMaskScale", h.envMappingMaskScale);
    } else {
        addHeaderField(fields, "DepthBias", h.depthBias);
    }
    addHeaderField(fields, "GrayscaleToPaletteColor", h.grayscaleToPaletteColor);
    if (h.version >= 6) addHeaderField(fields, "MaskWrites", h.maskWrites);
}

template <class T>
void setScalar(T& target, const std::string& value) {
    if constexpr (std::is_same_v<T, bool>) target = parseBool(value);
    else if constexpr (std::is_same_v<T, float>) target = std::stof(value);
    else if constexpr (std::is_same_v<T, std::uint8_t>) {
        const auto parsed = std::stoul(value);
        if (parsed > 255U) throw std::runtime_error("value is outside byte range");
        target = static_cast<std::uint8_t>(parsed);
    } else {
        const auto parsed = std::stoull(value);
        if (parsed > std::numeric_limits<std::uint32_t>::max()) throw std::runtime_error("value is outside uint32 range");
        target = static_cast<std::uint32_t>(parsed);
    }
}

bool setHeaderField(Header& h, std::string_view name, const std::string& value) {
    const auto key = lower(name);
    if (key == "signature") {
        const auto requested = static_cast<std::uint32_t>(std::stoul(value, nullptr, 0));
        if (requested != h.signature) throw std::runtime_error("changing material signature is not supported");
        return true;
    }
    if (key == "version") {
        const auto requested = static_cast<std::uint32_t>(std::stoul(value, nullptr, 0));
        if (requested != h.version) throw std::runtime_error("changing material version is not supported because it changes the binary layout");
        return true;
    }
#define FO4IDE_HEADER_FIELD(Name, Member) if (key == lower(Name)) { setScalar(h.Member, value); return true; }
    FO4IDE_HEADER_FIELD("TileU", tileU)
    FO4IDE_HEADER_FIELD("TileV", tileV)
    FO4IDE_HEADER_FIELD("UOffset", uOffset)
    FO4IDE_HEADER_FIELD("VOffset", vOffset)
    FO4IDE_HEADER_FIELD("UScale", uScale)
    FO4IDE_HEADER_FIELD("VScale", vScale)
    FO4IDE_HEADER_FIELD("Alpha", alpha)
    FO4IDE_HEADER_FIELD("AlphaBlendMode0", alphaBlendMode0)
    FO4IDE_HEADER_FIELD("AlphaBlendMode1", alphaBlendMode1)
    FO4IDE_HEADER_FIELD("AlphaBlendMode2", alphaBlendMode2)
    FO4IDE_HEADER_FIELD("AlphaTestRef", alphaTestRef)
    FO4IDE_HEADER_FIELD("AlphaTest", alphaTest)
    FO4IDE_HEADER_FIELD("ZBufferWrite", zBufferWrite)
    FO4IDE_HEADER_FIELD("ZBufferTest", zBufferTest)
    FO4IDE_HEADER_FIELD("Ssr", ssr)
    FO4IDE_HEADER_FIELD("WetSsr", wetSsr)
    FO4IDE_HEADER_FIELD("Decal", decal)
    FO4IDE_HEADER_FIELD("TwoSided", twoSided)
    FO4IDE_HEADER_FIELD("DecalNoFade", decalNoFade)
    FO4IDE_HEADER_FIELD("NonOccluder", nonOccluder)
    FO4IDE_HEADER_FIELD("Refraction", refraction)
    FO4IDE_HEADER_FIELD("RefractionFalloff", refractionFalloff)
    FO4IDE_HEADER_FIELD("RefractionPower", refractionPower)
    FO4IDE_HEADER_FIELD("EnvMapping", envMapping)
    FO4IDE_HEADER_FIELD("EnvMappingMaskScale", envMappingMaskScale)
    FO4IDE_HEADER_FIELD("DepthBias", depthBias)
    FO4IDE_HEADER_FIELD("GrayscaleToPaletteColor", grayscaleToPaletteColor)
    FO4IDE_HEADER_FIELD("MaskWrites", maskWrites)
#undef FO4IDE_HEADER_FIELD
    return false;
}

Result<std::vector<std::uint8_t>> readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return std::unexpected(Error{"MaterialReadFailed", "Could not open material file: " + path.string()});
    stream.seekg(0, std::ios::end);
    const auto length = stream.tellg();
    if (length < 0) return std::unexpected(Error{"MaterialReadFailed", "Could not determine material size"});
    stream.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    if (!bytes.empty()) stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream && !bytes.empty()) return std::unexpected(Error{"MaterialReadFailed", "Could not read complete material file"});
    return bytes;
}

Result<void> writeFileAtomic(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    try {
        const auto full = std::filesystem::absolute(path);
        if (!full.parent_path().empty()) std::filesystem::create_directories(full.parent_path());
        const auto temp = full.string() + ".tmp";
        {
            std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
            if (!stream) return std::unexpected(Error{"MaterialWriteFailed", "Could not create temporary material file"});
            if (!bytes.empty()) stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            stream.flush();
            if (!stream) return std::unexpected(Error{"MaterialWriteFailed", "Could not write temporary material file"});
        }
        std::error_code ec;
        std::filesystem::rename(temp, full, ec);
        if (ec) {
            std::filesystem::remove(full, ec);
            ec.clear();
            std::filesystem::rename(temp, full, ec);
        }
        if (ec) return std::unexpected(Error{"MaterialWriteFailed", ec.message()});
        return {};
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"MaterialWriteFailed", ex.what()});
    }
}

}

Result<Document> MaterialService::load(const std::filesystem::path& path) const {
    if (path.empty()) return std::unexpected(Error{"MaterialPathMissing", "Provide a .bgsm or .bgem file path"});
    auto bytes = readFile(path);
    if (!bytes) return std::unexpected(bytes.error());
    if (bytes->size() < 4) return std::unexpected(Error{"MaterialParseFailed", "Not a material file: fewer than 4 bytes"});

    try {
        Reader reader(std::move(*bytes));
        const auto signature = reader.u32();
        Reader replay(readFile(path).value());
        Document document;
        if (signature == BgsmSignature) {
            document.format = Document::Format::Bgsm;
            document.header = readHeader(replay, BgsmSignature, "BGSM");
            parseBgsm(document, replay);
        } else if (signature == BgemSignature) {
            document.format = Document::Format::Bgem;
            document.header = readHeader(replay, BgemSignature, "BGEM");
            parseBgem(document, replay);
        } else {
            std::ostringstream message;
            message << "Not a BGSM or BGEM material: magic 0x" << std::hex << std::uppercase << signature;
            return std::unexpected(Error{"MaterialParseFailed", message.str()});
        }
        document.trailingBytes = replay.remaining();
        return document;
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"MaterialParseFailed", ex.what()});
    }
}

Result<nlohmann::json> MaterialService::inspect(const std::filesystem::path& path) const {
    auto document = load(path);
    if (!document) return std::unexpected(document.error());
    auto fields = materialFieldsJson(*document);
    appendHeaderJson(fields, document->header);
    return nlohmann::json{
        {"fileName", path.filename().string()},
        {"format", document->format == Document::Format::Bgem ? "BGEM" : "BGSM"},
        {"version", document->header.version},
        {"fields", std::move(fields)},
        {"trailingBytes", document->trailingBytes.size()}
    };
}

Result<std::string> MaterialService::setFields(
    const std::filesystem::path& path,
    const std::map<std::string, std::string, std::less<>>& fields,
    const std::filesystem::path& output) const {
    if (fields.empty()) return std::unexpected(Error{"MaterialNoChanges", "No field changes to apply"});
    auto document = load(path);
    if (!document) return std::unexpected(document.error());

    std::vector<std::string> applied;
    try {
        for (const auto& [name, value] : fields) {
            if (auto* field = findField(*document, name)) {
                assign(*field, value);
                applied.push_back(field->name);
                continue;
            }
            if (setHeaderField(document->header, name, value)) {
                applied.push_back(name);
                continue;
            }
            return std::unexpected(Error{"MaterialUnknownField", "Unknown material field '" + name + "'"});
        }
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"MaterialFieldParseFailed", ex.what()});
    }

    auto bytes = encode(*document);
    if (!bytes) return std::unexpected(bytes.error());
    const auto destination = output.empty() ? path : output;
    auto written = writeFileAtomic(destination, *bytes);
    if (!written) return std::unexpected(written.error());

    std::ostringstream message;
    message << "Set " << applied.size() << " field(s) (";
    for (std::size_t i = 0; i < applied.size(); ++i) {
        if (i != 0) message << ", ";
        message << applied[i];
    }
    message << ") in '" << path.filename().string() << "', wrote to '" << destination.string() << "'.";
    return message.str();
}

Result<std::vector<std::uint8_t>> MaterialService::encode(const Document& document) const {
    try {
        Writer writer;
        writeHeader(writer, document.header);
        if (document.format == Document::Format::Bgsm) writeBgsm(document, writer);
        else writeBgem(document, writer);
        writer.append(document.trailingBytes);
        return writer.finish();
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"MaterialEncodeFailed", ex.what()});
    }
}

}
