#include "f4lod/btr_material.hpp"
#include "f4lod/btr_material_writer.hpp"
#include "f4lod/btr_writer.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string_view>
#include <vector>

namespace {

void u8(std::vector<unsigned char>& out, std::uint8_t v) { out.push_back(v); }
void u32(std::vector<unsigned char>& out, std::uint32_t v) {
    u8(out, static_cast<std::uint8_t>(v & 0xFFU));
    u8(out, static_cast<std::uint8_t>((v >> 8U) & 0xFFU));
    u8(out, static_cast<std::uint8_t>((v >> 16U) & 0xFFU));
    u8(out, static_cast<std::uint8_t>((v >> 24U) & 0xFFU));
}
void i32(std::vector<unsigned char>& out, std::int32_t v) {
    u32(out, static_cast<std::uint32_t>(v));
}
void f32(std::vector<unsigned char>& out, float v) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    u32(out, bits);
}
void sized_string(std::vector<unsigned char>& out, std::string_view value) {
    u32(out, static_cast<std::uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

std::vector<unsigned char> lighting_shader() {
    std::vector<unsigned char> out;
    u32(out, 0U);
    i32(out, -1);
    u32(out, 0U);
    i32(out, -1);
    u32(out, 0x81200000U);
    u32(out, 0x00008021U);
    f32(out, 0.0F); f32(out, 0.0F);
    f32(out, 1.0F); f32(out, 1.0F);
    i32(out, 5);
    f32(out, 0.0F); f32(out, 0.0F); f32(out, 0.0F); f32(out, 1.0F);
    i32(out, -1);
    u32(out, 3U);
    f32(out, 1.0F);
    f32(out, 0.0F);
    f32(out, 80.0F);
    f32(out, 1.0F); f32(out, 1.0F); f32(out, 1.0F);
    f32(out, 1.0F);
    f32(out, 0.0F);
    f32(out, 0.0F);
    f32(out, 2.5F);
    f32(out, 1.0F);
    f32(out, 5.0F);
    f32(out, -1.0F);
    f32(out, -1.0F);
    f32(out, -1.0F);
    f32(out, -1.0F);
    f32(out, -1.0F);
    f32(out, -1.0F);
    return out;
}

std::vector<unsigned char> texture_set() {
    std::vector<unsigned char> out;
    i32(out, 2);
    sized_string(out, "textures\\terrain\\commonwealth\\diffuse.dds");
    sized_string(out, "textures\\terrain\\commonwealth\\normal.dds");
    return out;
}

f4lod::TerrainSimplifiedMesh make_mesh() {
    f4lod::TerrainSimplifiedMesh mesh;
    mesh.valid = true;
    mesh.vertices = {
        {0.0F, 0.0F, 0.0F},
        {128.0F, 0.0F, 0.0F},
        {128.0F, 128.0F, 0.0F},
        {0.0F, 128.0F, 0.0F},
    };
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "f4lodgen-btr-material-smoke";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const auto mesh = make_mesh();

    f4lod::TerrainBtrBuildOptions options;
    options.uv_span_x = 128.0F;
    options.uv_span_y = 128.0F;
    options.shader_property_0 = 4;
    options.additional_blocks.push_back({"BSLightingShaderProperty", lighting_shader()});
    options.additional_blocks.push_back({"BSShaderTextureSet", texture_set()});

    const auto path = root / "Material.btr";
    const auto written = f4lod::write_fallout4_terrain_btr(path, mesh, options);
    assert(written.valid);
    assert(written.renderable);

    const auto material = f4lod::inspect_btr_material(path, 4);
    assert(material.valid);
    assert(material.lighting.valid);
    assert(material.lighting.shader_type == 0U);
    assert(material.lighting.texture_set == 5);
    assert(material.lighting.texture_clamp_mode == 3U);
    assert(material.lighting.alpha == 1.0F);
    assert(material.lighting.refraction_strength == 0.0F);
    assert(material.lighting.glossiness == 80.0F);
    assert(material.lighting.backlight_power == 2.5F);
    assert(material.lighting.grayscale_to_palette_scale == 1.0F);
    assert(material.lighting.fresnel_power == 5.0F);
    assert(material.lighting.type_specific_tail.empty());
    assert(material.texture_set.valid);
    assert(material.texture_set.textures.size() == 2);
    assert(material.texture_set.textures[0] == "textures\\terrain\\commonwealth\\diffuse.dds");
    assert(material.texture_set.textures[1] == "textures\\terrain\\commonwealth\\normal.dds");

    f4lod::TerrainBtrBuildOptions cloned_options;
    cloned_options.uv_span_x = 128.0F;
    cloned_options.uv_span_y = 128.0F;
    const std::vector<std::string> replacement_textures{
        "textures\\terrain\\commonwealth\\generated_d.dds",
        "textures\\terrain\\commonwealth\\generated_n.dds",
    };
    const auto attached = f4lod::attach_calibrated_lighting_material(
        cloned_options, material, 0, replacement_textures);
    assert(attached.valid);
    assert(attached.property_block == 4);
    assert(attached.texture_set_block == 5);
    assert(cloned_options.shader_property_0 == 4);
    assert(cloned_options.additional_blocks.size() == 2);

    const auto clone_path = root / "ClonedMaterial.btr";
    const auto clone_write = f4lod::write_fallout4_terrain_btr(
        clone_path, mesh, cloned_options);
    assert(clone_write.valid);
    assert(clone_write.renderable);

    const auto cloned = f4lod::inspect_btr_material(clone_path, 4);
    assert(cloned.valid);
    assert(cloned.lighting.shader_type == material.lighting.shader_type);
    assert(cloned.lighting.shader_flags_1 == material.lighting.shader_flags_1);
    assert(cloned.lighting.shader_flags_2 == material.lighting.shader_flags_2);
    assert(cloned.lighting.backlight_power == material.lighting.backlight_power);
    assert(cloned.lighting.wetness_metalness == material.lighting.wetness_metalness);
    assert(cloned.lighting.type_specific_tail == material.lighting.type_specific_tail);
    assert(cloned.texture_set.textures == replacement_textures);

    const auto wrong = f4lod::inspect_btr_material(path, 5);
    assert(!wrong.valid);
    assert(!f4lod::attach_calibrated_lighting_material(cloned_options, material, 2).valid);

    std::filesystem::remove_all(root);
    return 0;
}
