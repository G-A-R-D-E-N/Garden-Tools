#include "f4lod/btr_probe.hpp"
#include "f4lod/btr_writer.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void u8(std::vector<unsigned char>& out, std::uint8_t value) { out.push_back(value); }
void u16(std::vector<unsigned char>& out, std::uint16_t value) {
    u8(out, static_cast<std::uint8_t>(value & 0xFFU));
    u8(out, static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}
void u32(std::vector<unsigned char>& out, std::uint32_t value) {
    u8(out, static_cast<std::uint8_t>(value & 0xFFU));
    u8(out, static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    u8(out, static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    u8(out, static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
}
void u64(std::vector<unsigned char>& out, std::uint64_t value) {
    u32(out, static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));
    u32(out, static_cast<std::uint32_t>(value >> 32U));
}
void signature(std::vector<unsigned char>& out, std::string_view value) {
    assert(value.size() == 4U);
    out.insert(out.end(), value.begin(), value.end());
}

std::vector<unsigned char> read_all(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    input.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(input.tellg());
    input.seekg(0, std::ios::beg);
    std::vector<unsigned char> data(size);
    input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return data;
}

void write_general_ba2(
    const std::filesystem::path& path,
    const std::string& game_path,
    const std::vector<unsigned char>& payload) {
    constexpr std::uint64_t header_size = 24U;
    constexpr std::uint64_t record_size = 36U;
    const std::uint64_t name_offset = header_size + record_size;
    const std::uint64_t data_offset = name_offset + 2U + game_path.size();

    std::vector<unsigned char> bytes;
    signature(bytes, "BTDX");
    u32(bytes, 1U);
    signature(bytes, "GNRL");
    u32(bytes, 1U);
    u64(bytes, name_offset);

    u32(bytes, 0U);
    u32(bytes, 0U);
    u32(bytes, 0U);
    u32(bytes, 0xBAADF00DU);
    u64(bytes, data_offset);
    u32(bytes, 0U);
    u32(bytes, static_cast<std::uint32_t>(payload.size()));
    u32(bytes, 0U);

    u16(bytes, static_cast<std::uint16_t>(game_path.size()));
    bytes.insert(bytes.end(), game_path.begin(), game_path.end());
    bytes.insert(bytes.end(), payload.begin(), payload.end());

    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "f4lodgen-btr-probe-smoke";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    f4lod::TerrainSimplifiedMesh mesh;
    mesh.valid = true;
    mesh.vertices = {
        {0.0F, 0.0F, 0.0F},
        {128.0F, 0.0F, 8.0F},
        {128.0F, 128.0F, 16.0F},
        {0.0F, 128.0F, 4.0F},
    };
    mesh.indices = {0, 1, 2, 0, 2, 3};

    f4lod::TerrainBtrBuildOptions options;
    options.uv_span_x = 128.0F;
    options.uv_span_y = 128.0F;

    const auto loose_btr = root / "Generated.btr";
    const auto write = f4lod::write_fallout4_terrain_btr(loose_btr, mesh, options);
    assert(write.valid);
    const auto btr_bytes = read_all(loose_btr);
    assert(!btr_bytes.empty());

    const std::string game_path = "meshes\\terrain\\Commonwealth\\Commonwealth.4.0.0.btr";
    const auto archive_path = root / "Fixture - Meshes.ba2";
    write_general_ba2(archive_path, game_path, btr_bytes);

    const auto archive = f4lod::inspect_ba2_index(archive_path);
    assert(archive.valid);
    assert(archive.type == f4lod::Ba2ArchiveType::general);

    const auto probe = f4lod::probe_btr_from_general_ba2(
        archive,
        "MESHES/TERRAIN/COMMONWEALTH/COMMONWEALTH.4.0.0.BTR");
    assert(probe.valid);
    assert(probe.btr.valid);
    assert(probe.geometry.valid);
    assert(probe.geometry.vertex_count == 4U);
    assert(probe.geometry.triangle_count == 2U);
    assert(!probe.material_present[0]);
    assert(!probe.material_present[1]);

    const auto missing = f4lod::probe_btr_from_general_ba2(
        archive, "meshes/terrain/Commonwealth/missing.btr");
    assert(!missing.valid);
    assert(!missing.error.empty());

    std::filesystem::remove_all(root);
    return 0;
}
