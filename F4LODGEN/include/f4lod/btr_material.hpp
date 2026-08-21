#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace f4lod {

struct BtrLightingShaderInspection {
    bool valid{};
    std::string error;
    std::int32_t block_index{-1};
    std::uint32_t shader_type{};
    std::int32_t name_index{-1};
    std::string name;
    std::uint32_t shader_flags_1{};
    std::uint32_t shader_flags_2{};
    float uv_offset[2]{};
    float uv_scale[2]{1.0F, 1.0F};
    std::int32_t texture_set{-1};
    float emissive_color[3]{};
    float emissive_multiple{1.0F};
    std::int32_t wet_material_index{-1};
    std::string wet_material_name;
    std::uint32_t texture_clamp_mode{};
    float alpha{};
    float refraction_strength{};
    float glossiness{};
    float specular_color[3]{};
    float specular_strength{};
    float subsurface_rolloff{};
    float unknown_float_1{};
    float backlight_power{};
    float grayscale_to_palette_scale{};
    float fresnel_power{};
    float wetness_spec_scale{};
    float wetness_spec_power{};
    float wetness_min_var{};
    float wetness_env_map_scale{};
    float wetness_fresnel_power{};
    float wetness_metalness{};
    std::vector<unsigned char> type_specific_tail;
};

struct BtrTextureSetInspection {
    bool valid{};
    std::string error;
    std::int32_t block_index{-1};
    std::vector<std::string> textures;
};

struct BtrMaterialInspection {
    bool valid{};
    std::string error;
    BtrLightingShaderInspection lighting;
    BtrTextureSetInspection texture_set;
};

[[nodiscard]] BtrLightingShaderInspection inspect_btr_lighting_shader(
    const std::filesystem::path& path,
    std::int32_t block_index);

[[nodiscard]] BtrTextureSetInspection inspect_btr_texture_set(
    const std::filesystem::path& path,
    std::int32_t block_index);

[[nodiscard]] BtrMaterialInspection inspect_btr_material(
    const std::filesystem::path& path,
    std::int32_t property_block_index);

} // namespace f4lod
