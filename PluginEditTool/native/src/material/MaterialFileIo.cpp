#include "MaterialFileIo.hpp"

#include <fo4ide/material/MaterialService.hpp>

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace fo4ide::material {

std::string lower(std::string_view value) {
    std::string result(value);
    std::ranges::transform(result, result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
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
        if (!ec) return {};
        std::error_code cleanupEc;
        std::filesystem::remove(temp, cleanupEc);
        return std::unexpected(Error{"MaterialWriteFailed", ec.message()});
    } catch (const std::exception& ex) {
        return std::unexpected(Error{"MaterialWriteFailed", ex.what()});
    }
}

namespace {

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

const Field* findField(const Document& document, std::string_view name) {
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
