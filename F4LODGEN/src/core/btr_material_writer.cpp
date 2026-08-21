#include "f4lod/btr_material_writer.hpp"

#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace f4lod {
namespace {

void u8(std::vector<unsigned char>& out, std::uint8_t value) {
    out.push_back(value);
}

void u32(std::vector<unsigned char>& out, std::uint32_t value) {
    u8(out, static_cast<std::uint8_t>(value & 0xFFU));
    u8(out, static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    u8(out, static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    u8(out, static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
}

void i32(std::vector<unsigned char>& out, std::int32_t value) {
    u32(out, static_cast<std::uint32_t>(value));
}

void f32(std::vector<unsigned char>& out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    u32(out, bits);
}

void vec2(std::vector<unsigned char>& out, const float (&values)[2]) {
    f32(out, values[0]);
    f32(out, values[1]);
}

void vec3(std::vector<unsigned char>& out, const float (&values)[3]) {
    f32(out, values[0]);
    f32(out, values[1]);
    f32(out, values[2]);
}

bool append_sized_string(std::vector<unsigned char>& out, const std::string& value) {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) return false;
    u32(out, static_cast<std::uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
    return true;
}

bool finite2(const float (&values)[2]) {
    return std::isfinite(values[0]) && std::isfinite(values[1]);
}

bool finite3(const float (&values)[3]) {
    return std::isfinite(values[0]) && std::isfinite(values[1]) && std::isfinite(values[2]);
}

bool finite_material(const BtrLightingShaderInspection& material) {
    return finite2(material.uv_offset) && finite2(material.uv_scale) &&
           finite3(material.emissive_color) && std::isfinite(material.emissive_multiple) &&
           std::isfinite(material.alpha) && std::isfinite(material.refraction_strength) &&
           std::isfinite(material.glossiness) && finite3(material.specular_color) &&
           std::isfinite(material.specular_strength) && std::isfinite(material.subsurface_rolloff) &&
           std::isfinite(material.unknown_float_1) && std::isfinite(material.backlight_power) &&
           std::isfinite(material.grayscale_to_palette_scale) && std::isfinite(material.fresnel_power) &&
           std::isfinite(material.wetness_spec_scale) && std::isfinite(material.wetness_spec_power) &&
           std::isfinite(material.wetness_min_var) && std::isfinite(material.wetness_env_map_scale) &&
           std::isfinite(material.wetness_fresnel_power) && std::isfinite(material.wetness_metalness);
}

std::int32_t append_optional_string(
    TerrainBtrBuildOptions& options,
    const std::string& value) {
    if (value.empty()) return -1;
    const auto index = 1U + options.additional_strings.size();
    if (index > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) return -2;
    options.additional_strings.push_back(value);
    return static_cast<std::int32_t>(index);
}

std::vector<unsigned char> make_texture_set(const std::vector<std::string>& textures, std::string& error) {
    std::vector<unsigned char> out;
    if (textures.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        error = "Texture set contains too many texture paths.";
        return {};
    }
    i32(out, static_cast<std::int32_t>(textures.size()));
    for (const auto& texture : textures) {
        if (!append_sized_string(out, texture)) {
            error = "Texture path is too long for BSShaderTextureSet.";
            return {};
        }
    }
    return out;
}

std::vector<unsigned char> make_lighting_shader(
    const BtrLightingShaderInspection& material,
    std::int32_t target_name_index,
    std::int32_t target_wet_material_index,
    std::int32_t texture_set_block) {
    std::vector<unsigned char> out;
    u32(out, material.shader_type);
    i32(out, target_name_index);
    u32(out, 0U);  // extra data count
    i32(out, -1);  // controller
    u32(out, material.shader_flags_1);
    u32(out, material.shader_flags_2);
    vec2(out, material.uv_offset);
    vec2(out, material.uv_scale);
    i32(out, texture_set_block);
    vec3(out, material.emissive_color);
    f32(out, material.emissive_multiple);
    i32(out, target_wet_material_index);
    u32(out, material.texture_clamp_mode);
    f32(out, material.alpha);
    f32(out, material.refraction_strength);
    f32(out, material.glossiness);
    vec3(out, material.specular_color);
    f32(out, material.specular_strength);
    f32(out, material.subsurface_rolloff);
    f32(out, material.unknown_float_1);
    f32(out, material.backlight_power);
    f32(out, material.grayscale_to_palette_scale);
    f32(out, material.fresnel_power);
    f32(out, material.wetness_spec_scale);
    f32(out, material.wetness_spec_power);
    f32(out, material.wetness_min_var);
    f32(out, material.wetness_env_map_scale);
    f32(out, material.wetness_fresnel_power);
    f32(out, material.wetness_metalness);
    out.insert(out.end(), material.type_specific_tail.begin(), material.type_specific_tail.end());
    return out;
}

} // namespace

BtrMaterialAttachResult attach_calibrated_lighting_material(
    TerrainBtrBuildOptions& options,
    const BtrMaterialInspection& calibration,
    std::size_t property_slot,
    const std::vector<std::string>& replacement_textures) {
    BtrMaterialAttachResult result;

    if (!calibration.valid || !calibration.lighting.valid) {
        result.error = "Calibration material is invalid.";
        return result;
    }
    if (property_slot > 1U) {
        result.error = "Terrain BTR has only shader property slots 0 and 1.";
        return result;
    }
    if (!finite_material(calibration.lighting)) {
        result.error = "Calibration lighting material contains non-finite numeric values.";
        return result;
    }

    const auto& textures = replacement_textures.empty()
        ? calibration.texture_set.textures
        : replacement_textures;
    if (calibration.lighting.texture_set >= 0 && !calibration.texture_set.valid) {
        result.error = "Calibration lighting material references an invalid texture set.";
        return result;
    }

    const auto property_index_64 = 4U + options.additional_blocks.size();
    const auto texture_index_64 = property_index_64 + 1U;
    if (texture_index_64 > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        result.error = "BTR material block index exceeds signed 32-bit NIF reference range.";
        return result;
    }
    const auto property_index = static_cast<std::int32_t>(property_index_64);
    const auto texture_index = static_cast<std::int32_t>(texture_index_64);

    const auto target_name_index = append_optional_string(options, calibration.lighting.name);
    if (target_name_index == -2) {
        result.error = "BTR string table cannot represent the calibrated shader name index.";
        return result;
    }
    const auto target_wet_index = append_optional_string(options, calibration.lighting.wet_material_name);
    if (target_wet_index == -2) {
        result.error = "BTR string table cannot represent the calibrated wet-material index.";
        return result;
    }

    std::string texture_error;
    auto texture_block = make_texture_set(textures, texture_error);
    if (!texture_error.empty()) {
        result.error = std::move(texture_error);
        return result;
    }

    const auto target_texture_ref = calibration.lighting.texture_set >= 0 ? texture_index : -1;
    auto shader_block = make_lighting_shader(
        calibration.lighting,
        target_name_index,
        target_wet_index,
        target_texture_ref);

    options.additional_blocks.push_back(NifWriteBlock{
        "BSLightingShaderProperty",
        std::move(shader_block),
    });
    options.additional_blocks.push_back(NifWriteBlock{
        "BSShaderTextureSet",
        std::move(texture_block),
    });

    if (property_slot == 0U) {
        options.shader_property_0 = property_index;
    } else {
        options.shader_property_1 = property_index;
    }

    result.property_block = property_index;
    result.texture_set_block = target_texture_ref;
    result.valid = true;
    return result;
}

} // namespace f4lod
