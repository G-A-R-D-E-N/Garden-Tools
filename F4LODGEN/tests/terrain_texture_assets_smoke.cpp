#include "f4lod/terrain_texture_assets.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

void set_u32(std::vector<unsigned char>& out, std::size_t offset, std::uint32_t value) {
    out[offset] = static_cast<unsigned char>(value & 0xFFU);
    out[offset + 1U] = static_cast<unsigned char>((value >> 8U) & 0xFFU);
    out[offset + 2U] = static_cast<unsigned char>((value >> 16U) & 0xFFU);
    out[offset + 3U] = static_cast<unsigned char>((value >> 24U) & 0xFFU);
}

std::vector<unsigned char> make_dx10_bc1() {
    std::vector<unsigned char> bytes(156U, 0U);
    set_u32(bytes, 0U, 0x20534444U);
    set_u32(bytes, 4U, 124U);
    set_u32(bytes, 8U, 0x00081007U);
    set_u32(bytes, 12U, 4U);
    set_u32(bytes, 16U, 4U);
    set_u32(bytes, 20U, 8U);
    set_u32(bytes, 28U, 1U);
    set_u32(bytes, 76U, 32U);
    set_u32(bytes, 80U, 0x00000004U);
    set_u32(bytes, 84U, 0x30315844U);
    set_u32(bytes, 108U, 0x00001000U);
    set_u32(bytes, 128U, 71U);
    set_u32(bytes, 132U, 3U);
    set_u32(bytes, 140U, 1U);
    return bytes;
}

void write_file(const std::filesystem::path& path, const std::vector<unsigned char>& data) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "f4lodgen-terrain-texture-assets-smoke";
    std::filesystem::remove_all(root);

    const auto dds = make_dx10_bc1();
    write_file(root / "textures" / "landscape" / "dirt_d.dds", dds);
    write_file(root / "textures" / "landscape" / "dirt_n.dds", dds);

    f4lod::TerrainTextureGraph graph;
    graph.valid = true;
    f4lod::TextureSetDependency first;
    first.key = {"Fixture.esp", 0x100U};
    first.texture_paths[0] = "textures/landscape/dirt_d.dds";
    first.texture_paths[1] = "textures\\landscape\\dirt_n.dds";
    graph.texture_sets.push_back(first);

    f4lod::TextureSetDependency second;
    second.key = {"Fixture.esp", 0x101U};
    second.texture_paths[0] = "TEXTURES/LANDSCAPE/DIRT_D.DDS";
    graph.texture_sets.push_back(second);

    f4lod::AssetResolver assets(root, {});
    const auto report = f4lod::resolve_terrain_texture_assets(graph, assets);
    assert(report.valid);
    assert(report.referenced_slots == 3U);
    assert(report.unique_paths == 2U);
    assert(report.textures.size() == 2U);
    assert(report.diagnostics.empty());
    for (const auto& texture : report.textures) {
        assert(texture.source.kind == f4lod::AssetSourceKind::loose);
        assert(texture.dds.valid);
        assert(texture.dds.width == 4U);
        assert(texture.dds.height == 4U);
        assert(texture.dds.compression == f4lod::DdsCompression::bc1);
    }

    std::filesystem::remove(root / "textures" / "landscape" / "dirt_n.dds");
    const auto missing = f4lod::resolve_terrain_texture_assets(graph, assets);
    assert(!missing.valid);
    assert(missing.unique_paths == 2U);
    assert(missing.textures.size() == 1U);
    assert(missing.diagnostics.size() == 1U);
    assert(missing.diagnostics[0].code == "terrain-dds-unresolved");

    auto invalid_graph = graph;
    invalid_graph.valid = false;
    const auto blocked = f4lod::resolve_terrain_texture_assets(invalid_graph, assets);
    assert(!blocked.valid);
    assert(!blocked.diagnostics.empty());

    std::filesystem::remove_all(root);
    return 0;
}
