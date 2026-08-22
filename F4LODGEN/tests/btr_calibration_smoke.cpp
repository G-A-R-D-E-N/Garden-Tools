#include "f4lod/btr_calibration.hpp"

#include <array>
#include <cassert>
#include <vector>

namespace {

f4lod::PackedBtrProbe make_probe() {
    f4lod::PackedBtrProbe probe;
    probe.valid = true;
    probe.btr.valid = true;
    probe.btr.nif.valid = true;
    probe.btr.nif.strings = {"obj"};

    auto& g = probe.geometry;
    g.valid = true;
    g.type = "BSTriShape";
    g.name_index = 0;
    g.av_flags = 14U;
    g.av_flags2 = 0U;
    g.scale = 1.0F;
    g.full_precision_positions = true;
    g.vertex_flags = static_cast<std::uint16_t>(16U | 32U | 128U | 16384U);
    g.vertex_stride = 24U;
    g.vertex_flag3 = 0U;
    g.vertex_flag4 = 0U;
    g.vertex_flag5 = 0U;
    g.vertex_flag8 = 0U;
    g.shader_properties[0] = 4;

    probe.material_present[0] = true;
    auto& material = probe.materials[0];
    material.valid = true;
    material.lighting.valid = true;
    material.lighting.texture_set = 5;
    material.lighting.alpha = 1.0F;
    material.lighting.uv_scale[0] = 1.0F;
    material.lighting.uv_scale[1] = 1.0F;
    material.texture_set.valid = true;
    material.texture_set.textures = {"textures\\terrain\\commonwealth\\source.dds"};
    return probe;
}

} // namespace

int main() {
    auto probe = make_probe();
    const auto calibration = f4lod::calibrate_terrain_btr_writer(probe);
    assert(calibration.valid);
    assert(calibration.writer_compatible);
    assert(calibration.position_precision == f4lod::TerrainBtrVertexPrecision::full);
    assert(calibration.av_flags == 14U);
    assert(calibration.vertex_flag3 == 0U);
    assert(calibration.shape_name == "obj");

    f4lod::TerrainBtrBuildOptions options;
    std::array<std::vector<std::string>, 2> replacements;
    replacements[0] = {"textures\\terrain\\commonwealth\\generated.dds"};
    std::string error;
    assert(f4lod::apply_terrain_btr_calibration(options, calibration, replacements, error));
    assert(error.empty());
    assert(options.position_precision == f4lod::TerrainBtrVertexPrecision::full);
    assert(options.shader_property_0 == 4);
    assert(options.vertex_flag3 == 0U);
    assert(options.additional_blocks.size() == 2U);

    probe.geometry.vertex_flags |= 256U;
    const auto unsupported = f4lod::calibrate_terrain_btr_writer(probe);
    assert(unsupported.valid);
    assert(!unsupported.writer_compatible);
    assert(!unsupported.issues.empty());

    probe = make_probe();
    probe.geometry.type = "BSSubIndexTriShape";
    probe.geometry.subindex_a = 1U;
    probe.geometry.subindex_b = 1U;
    probe.geometry.subindex_segments.push_back({0U, 1U, 0U, 0U});
    const auto segmented = f4lod::calibrate_terrain_btr_writer(probe);
    assert(segmented.valid);
    assert(!segmented.writer_compatible);

    probe = make_probe();
    probe.geometry.vertex_flag3 = 101U;
    const auto object_descriptor = f4lod::calibrate_terrain_btr_writer(probe);
    assert(object_descriptor.valid);
    assert(!object_descriptor.writer_compatible);
    return 0;
}
