#include "f4lod/btr_geometry.hpp"
#include "f4lod/btr_writer.hpp"
#include "f4lod/nif.hpp"

#include <cassert>
#include <filesystem>

int main() {
    const auto root = std::filesystem::temp_directory_path() / "f4lodgen-btr-writer-smoke";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    f4lod::TerrainSimplifiedMesh mesh;
    mesh.valid = true;
    mesh.vertices = {
        {1000.0F, 2000.0F, 64.0F},
        {1128.0F, 2000.0F, 72.0F},
        {1128.0F, 2128.0F, 80.0F},
        {1000.0F, 2128.0F, 68.0F},
    };
    mesh.indices = {0, 1, 2, 0, 2, 3};

    f4lod::TerrainBtrBuildOptions options;
    options.shape_transform.translation_x = 1000.0F;
    options.shape_transform.translation_y = 2000.0F;
    options.shape_transform.translation_z = 0.0F;
    options.shape_transform.scale = 1.0F;
    options.uv_origin_x = 1000.0F;
    options.uv_origin_y = 2000.0F;
    options.uv_span_x = 128.0F;
    options.uv_span_y = 128.0F;

    const auto path = root / "GeometryOnly.btr";
    const auto write = f4lod::write_fallout4_terrain_btr(path, mesh, options);
    assert(write.valid);
    assert(!write.renderable);
    assert(write.bytes_written > 0);

    const auto btr = f4lod::inspect_fallout4_btr(path);
    assert(btr.valid);
    assert(btr.nif.blocks.size() == 4);
    assert(btr.terrain_shape == 1);
    assert(btr.nif.blocks[1].type == "BSTriShape");
    assert(btr.bounds_position[0] == 1064.0F);
    assert(btr.bounds_position[1] == 2064.0F);

    const auto geometry = f4lod::inspect_fallout4_btr_geometry(path, btr.terrain_shape);
    assert(geometry.valid);
    assert(geometry.type == "BSTriShape");
    assert(geometry.vertex_count == 4);
    assert(geometry.triangle_count == 2);
    assert(geometry.full_precision_positions);
    assert(geometry.vertex_stride == 24);
    assert(geometry.data_size == 108);
    assert(geometry.translation[0] == 1000.0F);
    assert(geometry.translation[1] == 2000.0F);
    assert(geometry.scale == 1.0F);
    assert(geometry.position_min[0] == 0.0F);
    assert(geometry.position_max[0] == 128.0F);
    assert(geometry.position_min[1] == 0.0F);
    assert(geometry.position_max[1] == 128.0F);
    assert(geometry.vertex_flag3 == 0U);
    assert(geometry.subindex_triangle_count == 0U);
    assert(geometry.subindex_a == 0U);
    assert(geometry.shader_properties[0] == -1);

    auto half_options = options;
    half_options.position_precision = f4lod::TerrainBtrVertexPrecision::half;
    const auto half_path = root / "Half.btr";
    const auto half_write = f4lod::write_fallout4_terrain_btr(half_path, mesh, half_options);
    assert(half_write.valid);
    const auto half_btr = f4lod::inspect_fallout4_btr(half_path);
    assert(half_btr.valid);
    const auto half_geometry = f4lod::inspect_fallout4_btr_geometry(half_path, half_btr.terrain_shape);
    assert(half_geometry.valid);
    assert(!half_geometry.full_precision_positions);
    assert(half_geometry.vertex_stride == 16U);
    assert(half_geometry.vertex_size_words == 4U);
    assert(half_geometry.float_size_words == 2U);

    auto material_options = options;
    material_options.additional_blocks.push_back({"BSLightingShaderProperty", {0x00}});
    material_options.shader_property_0 = 4;
    const auto material_document = f4lod::build_fallout4_terrain_btr_document(mesh, material_options);
    assert(material_document.valid);
    assert(material_document.renderable);

    auto bad_ref = options;
    bad_ref.shader_property_0 = 99;
    assert(!f4lod::build_fallout4_terrain_btr_document(mesh, bad_ref).valid);

    std::filesystem::remove_all(root);
    return 0;
}
