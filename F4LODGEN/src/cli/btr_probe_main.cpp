#include "f4lod/btr_probe.hpp"

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void print_material(const f4lod::BtrMaterialInspection& material, std::size_t slot) {
    const auto& shader = material.lighting;
    std::cout
        << "Material slot " << slot << '\n'
        << "  property block: " << shader.block_index << '\n'
        << "  shader type: " << shader.shader_type << '\n'
        << "  name: " << (shader.name.empty() ? "<none>" : shader.name) << '\n'
        << "  flags1: 0x" << std::hex << shader.shader_flags_1
        << " flags2: 0x" << shader.shader_flags_2 << std::dec << '\n'
        << "  UV offset: " << shader.uv_offset[0] << ", " << shader.uv_offset[1] << '\n'
        << "  UV scale: " << shader.uv_scale[0] << ", " << shader.uv_scale[1] << '\n'
        << "  texture set block: " << shader.texture_set << '\n'
        << "  clamp: " << shader.texture_clamp_mode << '\n'
        << "  alpha: " << shader.alpha << '\n'
        << "  glossiness: " << shader.glossiness << '\n'
        << "  specular strength: " << shader.specular_strength << '\n'
        << "  backlight: " << shader.backlight_power << '\n'
        << "  fresnel: " << shader.fresnel_power << '\n'
        << "  wetness spec scale: " << shader.wetness_spec_scale << '\n'
        << "  wetness spec power: " << shader.wetness_spec_power << '\n'
        << "  wetness min variance: " << shader.wetness_min_var << '\n'
        << "  wetness env scale: " << shader.wetness_env_map_scale << '\n'
        << "  wetness fresnel: " << shader.wetness_fresnel_power << '\n'
        << "  wetness metalness: " << shader.wetness_metalness << '\n'
        << "  shader tail bytes: " << shader.type_specific_tail.size() << '\n';

    if (!shader.wet_material_name.empty()) {
        std::cout << "  wet material: " << shader.wet_material_name << '\n';
    }
    if (material.texture_set.valid) {
        for (std::size_t i = 0; i < material.texture_set.textures.size(); ++i) {
            std::cout << "  texture[" << i << "]: "
                      << material.texture_set.textures[i] << '\n';
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr
            << "Usage: f4lod-btr-probe <General.ba2> <game/path/to/file.btr>\n";
        return 2;
    }

    const std::filesystem::path archive_path{argv[1]};
    const std::string game_path{argv[2]};
    const auto archive = f4lod::inspect_ba2_index(archive_path);
    if (!archive.valid) {
        std::cerr << "BA2 inspection failed: " << archive.error << '\n';
        return 1;
    }
    if (archive.type != f4lod::Ba2ArchiveType::general) {
        std::cerr << "BTR probe requires a General (GNRL) BA2 archive.\n";
        return 1;
    }

    const auto probe = f4lod::probe_btr_from_general_ba2(archive, game_path);
    if (!probe.valid) {
        std::cerr << "BTR probe failed: " << probe.error << '\n';
        return 1;
    }

    std::cout
        << "F4LODGen packed BTR calibration probe\n"
        << "Archive: " << probe.archive.string() << '\n'
        << "Asset: " << probe.game_path << '\n'
        << "NIF creator: " << probe.btr.nif.creator << '\n'
        << "Blocks: " << probe.btr.nif.blocks.size() << '\n'
        << "Root refs: " << probe.btr.nif.roots.size() << '\n'
        << "Terrain block: " << probe.btr.terrain_shape << '\n'
        << "Terrain type: " << probe.geometry.type << '\n'
        << "Vertices: " << probe.geometry.vertex_count << '\n'
        << "Triangles: " << probe.geometry.triangle_count << '\n'
        << "Vertex descriptor: sizeWords="
        << static_cast<unsigned int>(probe.geometry.vertex_size_words)
        << " floatWords=" << static_cast<unsigned int>(probe.geometry.float_size_words)
        << " VF3=" << static_cast<unsigned int>(probe.geometry.vertex_flag3)
        << " VF4=" << static_cast<unsigned int>(probe.geometry.vertex_flag4)
        << " VF5=" << static_cast<unsigned int>(probe.geometry.vertex_flag5)
        << " flags=0x" << std::hex << probe.geometry.vertex_flags << std::dec
        << " VF8=" << static_cast<unsigned int>(probe.geometry.vertex_flag8) << '\n'
        << "Vertex stride: " << probe.geometry.vertex_stride << '\n'
        << "Subindex triangles: " << probe.geometry.subindex_triangle_count << '\n'
        << "Subindex A/B: " << probe.geometry.subindex_a
        << '/' << probe.geometry.subindex_b << '\n'
        << "Shape shader refs: " << probe.geometry.shader_properties[0]
        << ", " << probe.geometry.shader_properties[1] << '\n'
        << "Bounds center: " << probe.btr.bounds_position[0] << ", "
        << probe.btr.bounds_position[1] << ", "
        << probe.btr.bounds_position[2] << '\n'
        << "Bounds extent: " << probe.btr.bounds_extent[0] << ", "
        << probe.btr.bounds_extent[1] << ", "
        << probe.btr.bounds_extent[2] << '\n';

    for (std::size_t slot = 0; slot < probe.materials.size(); ++slot) {
        if (probe.material_present[slot]) {
            print_material(probe.materials[slot], slot);
        } else {
            std::cout << "Material slot " << slot << ": <not lighting material>\n";
        }
    }

    if (!probe.geometry.subindex_segments.empty()) {
        std::cout << "Subindex entries:\n";
        for (std::size_t i = 0; i < probe.geometry.subindex_segments.size(); ++i) {
            const auto& segment = probe.geometry.subindex_segments[i];
            std::cout << "  [" << i << "] offset=" << segment.triangle_offset
                      << " count=" << segment.triangle_count
                      << " hash=" << segment.unknown_hash
                      << " segments=" << segment.num_segments << '\n';
        }
    }

    return 0;
}
