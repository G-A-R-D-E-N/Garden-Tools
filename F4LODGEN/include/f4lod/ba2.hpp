#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace f4lod {

enum class Ba2ArchiveType {
    general,
    texture,
    unknown,
};

struct Ba2AssetCounts {
    std::size_t meshes{};
    std::size_t materials{};
    std::size_t textures{};
    std::size_t other{};
};

struct Ba2GeneralEntry {
    std::string name;
    std::uint64_t data_offset{};
    std::uint32_t packed_size{};
    std::uint32_t unpacked_size{};
};

struct Ba2TextureChunk {
    std::uint64_t data_offset{};
    std::uint32_t packed_size{};
    std::uint32_t unpacked_size{};
    std::uint16_t start_mip{};
    std::uint16_t end_mip{};
};

struct Ba2TextureEntry {
    std::string name;
    std::uint8_t unknown{};
    std::uint16_t height{};
    std::uint16_t width{};
    std::uint8_t mip_count{};
    std::uint8_t dxgi_format{};
    std::uint16_t flags{};
    std::vector<Ba2TextureChunk> chunks;
};

struct Ba2Inspection {
    std::filesystem::path path;
    bool valid{};
    std::string error;
    std::uint32_t version{};
    Ba2ArchiveType type{Ba2ArchiveType::unknown};
    std::uint32_t header_size{};
    std::uint32_t declared_file_count{};
    std::uint64_t name_table_offset{};
    std::vector<std::string> files;
    std::vector<Ba2GeneralEntry> general_entries;
    std::vector<Ba2TextureEntry> texture_entries;
    Ba2AssetCounts assets;
};

struct Ba2Extraction {
    bool valid{};
    std::string error;
    std::string name;
    std::vector<unsigned char> data;
};

[[nodiscard]] Ba2Inspection inspect_ba2_index(const std::filesystem::path& path);
[[nodiscard]] Ba2Extraction extract_ba2_general_file(
    const Ba2Inspection& archive,
    const std::string& asset_path);
// Reconstructs a normal DDS file from a DX10 BA2 texture record by emitting
// the 148-byte DDS/DX10 header followed by the full decompressed mip payload.
// Fallout 4 zlib texture archives are supported. The version-3/LZ4 variant is
// detected and rejected until a pinned LZ4 dependency is intentionally added.
[[nodiscard]] Ba2Extraction extract_ba2_texture_file(
    const Ba2Inspection& archive,
    const std::string& asset_path);
[[nodiscard]] const char* ba2_archive_type_name(Ba2ArchiveType type) noexcept;

} // namespace f4lod
