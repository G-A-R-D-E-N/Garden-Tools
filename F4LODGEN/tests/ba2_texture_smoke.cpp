#include "f4lod/assets.hpp"
#include "f4lod/ba2.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void u16(std::vector<unsigned char>& out, std::uint16_t value) {
    out.push_back(static_cast<unsigned char>(value & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
}

void u32(std::vector<unsigned char>& out, std::uint32_t value) {
    out.push_back(static_cast<unsigned char>(value & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 24U) & 0xFFU));
}

void u64(std::vector<unsigned char>& out, std::uint64_t value) {
    u32(out, static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));
    u32(out, static_cast<std::uint32_t>(value >> 32U));
}

void sig(std::vector<unsigned char>& out, std::string_view value) {
    assert(value.size() == 4U);
    out.insert(out.end(), value.begin(), value.end());
}

std::uint32_t read_u32(const std::vector<unsigned char>& data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(data[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(data[offset + 3U]) << 24U);
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "f4lodgen-ba2-texture-smoke";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const std::string name = "textures\\landscape\\fixture_d.dds";
    const std::vector<unsigned char> bc1_payload{
        0x00U, 0xF8U, 0xE0U, 0x07U, 0x00U, 0x00U, 0x00U, 0x00U,
    };

    constexpr std::uint64_t header_size = 24U;
    constexpr std::uint64_t texture_record_size = 24U;
    constexpr std::uint64_t chunk_size = 24U;
    const std::uint64_t name_offset = header_size + texture_record_size + chunk_size;
    const std::uint64_t data_offset = name_offset + 2U + name.size();

    std::vector<unsigned char> bytes;
    sig(bytes, "BTDX");
    u32(bytes, 1U);
    sig(bytes, "DX10");
    u32(bytes, 1U);
    u64(bytes, name_offset);

    u32(bytes, 0U);
    u32(bytes, 0x00736464U);
    u32(bytes, 0U);
    bytes.push_back(0U);
    bytes.push_back(1U);
    u16(bytes, 24U);
    u16(bytes, 4U);
    u16(bytes, 4U);
    bytes.push_back(1U);
    bytes.push_back(71U);
    u16(bytes, 0U);

    u64(bytes, data_offset);
    u32(bytes, 0U);
    u32(bytes, static_cast<std::uint32_t>(bc1_payload.size()));
    u16(bytes, 0U);
    u16(bytes, 0U);
    u32(bytes, 0xBAADF00DU);

    u16(bytes, static_cast<std::uint16_t>(name.size()));
    bytes.insert(bytes.end(), name.begin(), name.end());
    bytes.insert(bytes.end(), bc1_payload.begin(), bc1_payload.end());

    const auto archive_path = root / "Fixture - Textures.ba2";
    {
        std::ofstream output(archive_path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    const auto archive = f4lod::inspect_ba2_index(archive_path);
    assert(archive.valid);
    assert(archive.type == f4lod::Ba2ArchiveType::texture);
    assert(archive.files.size() == 1U);
    assert(archive.texture_entries.size() == 1U);
    assert(archive.texture_entries[0].name == "textures/landscape/fixture_d.dds");
    assert(archive.texture_entries[0].width == 4U);
    assert(archive.texture_entries[0].height == 4U);
    assert(archive.texture_entries[0].mip_count == 1U);
    assert(archive.texture_entries[0].dxgi_format == 71U);
    assert(archive.texture_entries[0].chunks.size() == 1U);
    assert(archive.assets.textures == 1U);

    const auto extracted = f4lod::extract_ba2_texture_file(
        archive,
        "TEXTURES/LANDSCAPE/FIXTURE_D.DS");
    assert(extracted.valid);
    assert(extracted.data.size() == 148U + bc1_payload.size());
    assert(read_u32(extracted.data, 0U) == 0x20534444U);
    assert(read_u32(extracted.data, 4U) == 124U);
    assert(read_u32(extracted.data, 12U) == 4U);
    assert(read_u32(extracted.data, 16U) == 4U);
    assert(read_u32(extracted.data, 20U) == 8U);
    assert(read_u32(extracted.data, 76U) == 32U);
    assert(read_u32(extracted.data, 84U) == 0x30315844U);
    assert(read_u32(extracted.data, 128U) == 71U);
    assert(read_u32(extracted.data, 132U) == 3U);
    assert(read_u32(extracted.data, 140U) == 1U);
    for (std::size_t i = 0; i < bc1_payload.size(); ++i) {
        assert(extracted.data[148U + i] == bc1_payload[i]);
    }

    f4lod::AssetResolver resolver(root, {archive});
    assert(resolver.archives().size() == 1U);
    assert(resolver.archives()[0].type == f4lod::Ba2ArchiveType::texture);
    const auto resolved = resolver.read("textures/landscape/fixture_d.dds");
    assert(resolved.valid);
    assert(resolved.source.kind == f4lod::AssetSourceKind::texture_ba2);
    assert(resolved.source.container == archive_path);
    assert(resolved.data == extracted.data);
    assert(std::string(f4lod::asset_source_kind_name(f4lod::AssetSourceKind::texture_ba2)) == "texture-ba2");

    std::filesystem::create_directories(root / "textures" / "landscape");
    {
        std::ofstream loose(root / "textures" / "landscape" / "fixture_d.dds", std::ios::binary);
        loose << "loose-dds";
    }
    const auto loose = resolver.read("textures/landscape/fixture_d.dds");
    assert(loose.valid);
    assert(loose.source.kind == f4lod::AssetSourceKind::loose);
    assert(std::string(loose.data.begin(), loose.data.end()) == "loose-dds");

    const auto discovery = f4lod::discover_asset_sources(root, "textures/landscape/missing.dds");
    assert(discovery.valid);
    assert(!discovery.loose_exists);
    assert(discovery.matching_archives.empty());

    assert(!f4lod::extract_ba2_general_file(archive, name).valid);
    assert(!f4lod::extract_ba2_texture_file(archive, "missing.dds").valid);

    std::filesystem::remove_all(root);
    return 0;
}
