#include "f4lod/ba2.hpp"

#include <miniz.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <string_view>
#include <vector>

namespace f4lod {
namespace {

constexpr std::size_t kBaseHeaderSize = 24;
constexpr std::size_t kGeneralRecordSize = 36;
constexpr std::size_t kTextureRecordSize = 24;
constexpr std::size_t kTextureChunkSize = 24;
constexpr std::size_t kDdsDx10HeaderSize = 148;
constexpr std::size_t kMaxNameLength = 4096;
constexpr std::uint32_t kMaxExtractedFileSize = 1024U * 1024U * 1024U;
constexpr std::uint32_t kBaadFood = 0xBAADF00DU;

std::uint16_t read_u16(const unsigned char* data) noexcept {
    return static_cast<std::uint16_t>(data[0]) |
           (static_cast<std::uint16_t>(data[1]) << 8U);
}

std::uint32_t read_u32(const unsigned char* data) noexcept {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

std::uint64_t read_u64(const unsigned char* data) noexcept {
    return static_cast<std::uint64_t>(read_u32(data)) |
           (static_cast<std::uint64_t>(read_u32(data + 4)) << 32U);
}

void write_u32(std::vector<unsigned char>& out, std::size_t offset, std::uint32_t value) {
    out[offset] = static_cast<unsigned char>(value & 0xFFU);
    out[offset + 1U] = static_cast<unsigned char>((value >> 8U) & 0xFFU);
    out[offset + 2U] = static_cast<unsigned char>((value >> 16U) & 0xFFU);
    out[offset + 3U] = static_cast<unsigned char>((value >> 24U) & 0xFFU);
}

bool read_exact(std::ifstream& input, unsigned char* data, std::size_t size) {
    input.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(size));
    return input.good() || input.gcount() == static_cast<std::streamsize>(size);
}

bool supported_version(std::uint32_t version) {
    return version == 1U || version == 2U || version == 3U || version == 7U || version == 8U;
}

std::uint32_t archive_header_size(std::uint32_t version, Ba2ArchiveType type) noexcept {
    if (type == Ba2ArchiveType::general) {
        return version == 2U || version == 3U ? 32U : 24U;
    }
    if (type == Ba2ArchiveType::texture) {
        if (version == 2U) return 32U;
        if (version == 3U) return 36U;
        return 24U;
    }
    return 24U;
}

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string normalize_archive_path(std::string value) {
    for (char& c : value) {
        if (c == '\\') c = '/';
    }
    while (!value.empty() && value.front() == '/') value.erase(value.begin());
    return value;
}

std::string lookup_key(std::string value) {
    return lower_ascii(normalize_archive_path(std::move(value)));
}

void classify_asset(Ba2AssetCounts& assets, std::string_view name) {
    const auto dot = name.find_last_of('.');
    const auto extension = dot == std::string_view::npos
        ? std::string{}
        : lower_ascii(std::string(name.substr(dot)));

    if (extension == ".nif") ++assets.meshes;
    else if (extension == ".bgsm" || extension == ".bgem") ++assets.materials;
    else if (extension == ".dds") ++assets.textures;
    else ++assets.other;
}

bool read_general_metadata(
    std::ifstream& input,
    std::uint64_t file_size,
    Ba2Inspection& result) {
    const auto table_size = static_cast<std::uint64_t>(result.declared_file_count) * kGeneralRecordSize;
    if (result.header_size > result.name_table_offset ||
        table_size > result.name_table_offset - result.header_size) {
        result.error = "General BA2 record table overlaps the name table.";
        return false;
    }

    input.seekg(static_cast<std::streamoff>(result.header_size), std::ios::beg);
    if (!input) {
        result.error = "Could not seek to General BA2 record table.";
        return false;
    }

    result.general_entries.resize(result.declared_file_count);
    std::array<unsigned char, kGeneralRecordSize> record{};
    for (std::uint32_t i = 0; i < result.declared_file_count; ++i) {
        if (!read_exact(input, record.data(), record.size())) {
            result.error = "General BA2 record table is truncated.";
            return false;
        }

        auto& entry = result.general_entries[i];
        entry.data_offset = read_u64(record.data() + 16U);
        entry.packed_size = read_u32(record.data() + 24U);
        entry.unpacked_size = read_u32(record.data() + 28U);

        const auto stored_size = static_cast<std::uint64_t>(
            entry.packed_size != 0U ? entry.packed_size : entry.unpacked_size);
        if (stored_size != 0U &&
            (entry.data_offset > file_size || stored_size > file_size - entry.data_offset)) {
            result.error = "General BA2 entry points outside the archive.";
            return false;
        }
        if (entry.unpacked_size > kMaxExtractedFileSize) {
            result.error = "General BA2 entry exceeds the supported extracted-file size limit.";
            return false;
        }
    }
    return true;
}

bool read_texture_metadata(
    std::ifstream& input,
    std::uint64_t file_size,
    Ba2Inspection& result) {
    input.seekg(static_cast<std::streamoff>(result.header_size), std::ios::beg);
    if (!input) {
        result.error = "Could not seek to DX10 BA2 texture record table.";
        return false;
    }

    result.texture_entries.resize(result.declared_file_count);
    std::uint64_t table_position = result.header_size;
    for (std::uint32_t i = 0; i < result.declared_file_count; ++i) {
        if (table_position > result.name_table_offset ||
            kTextureRecordSize > result.name_table_offset - table_position) {
            result.error = "DX10 BA2 texture record table overlaps the name table.";
            return false;
        }

        std::array<unsigned char, kTextureRecordSize> record{};
        if (!read_exact(input, record.data(), record.size())) {
            result.error = "DX10 BA2 texture record table is truncated.";
            return false;
        }
        table_position += kTextureRecordSize;

        const auto chunk_count = static_cast<std::size_t>(record[13]);
        const auto chunk_header_size = read_u16(record.data() + 14U);
        if (chunk_count == 0U) {
            result.error = "DX10 BA2 texture record declares zero chunks.";
            return false;
        }
        if (chunk_header_size != kTextureChunkSize) {
            result.error = "DX10 BA2 texture record uses an unsupported chunk-header size.";
            return false;
        }
        const auto chunk_table_size = static_cast<std::uint64_t>(chunk_count) * kTextureChunkSize;
        if (table_position > result.name_table_offset ||
            chunk_table_size > result.name_table_offset - table_position) {
            result.error = "DX10 BA2 texture chunk table overlaps the name table.";
            return false;
        }

        auto& entry = result.texture_entries[i];
        entry.unknown = record[12];
        entry.height = read_u16(record.data() + 16U);
        entry.width = read_u16(record.data() + 18U);
        entry.mip_count = record[20];
        entry.dxgi_format = record[21];
        entry.flags = read_u16(record.data() + 22U);
        if (entry.width == 0U || entry.height == 0U || entry.mip_count == 0U) {
            result.error = "DX10 BA2 texture record declares invalid dimensions or mip count.";
            return false;
        }

        entry.chunks.reserve(chunk_count);
        for (std::size_t chunk_index = 0; chunk_index < chunk_count; ++chunk_index) {
            std::array<unsigned char, kTextureChunkSize> chunk_bytes{};
            if (!read_exact(input, chunk_bytes.data(), chunk_bytes.size())) {
                result.error = "DX10 BA2 texture chunk table is truncated.";
                return false;
            }
            table_position += kTextureChunkSize;

            Ba2TextureChunk chunk;
            chunk.data_offset = read_u64(chunk_bytes.data());
            chunk.packed_size = read_u32(chunk_bytes.data() + 8U);
            chunk.unpacked_size = read_u32(chunk_bytes.data() + 12U);
            chunk.start_mip = read_u16(chunk_bytes.data() + 16U);
            chunk.end_mip = read_u16(chunk_bytes.data() + 18U);
            const auto sentinel = read_u32(chunk_bytes.data() + 20U);
            if (sentinel != kBaadFood) {
                result.error = "DX10 BA2 texture chunk is missing the BAADF00D sentinel.";
                return false;
            }
            if (chunk.end_mip < chunk.start_mip) {
                result.error = "DX10 BA2 texture chunk has an invalid mip range.";
                return false;
            }

            const auto stored_size = static_cast<std::uint64_t>(
                chunk.packed_size != 0U ? chunk.packed_size : chunk.unpacked_size);
            if (chunk.unpacked_size > kMaxExtractedFileSize ||
                stored_size > kMaxExtractedFileSize) {
                result.error = "DX10 BA2 texture chunk exceeds the supported size limit.";
                return false;
            }
            if (stored_size != 0U &&
                (chunk.data_offset > file_size || stored_size > file_size - chunk.data_offset)) {
                result.error = "DX10 BA2 texture chunk points outside the archive.";
                return false;
            }
            entry.chunks.push_back(chunk);
        }
    }
    return true;
}

struct DxgiSurfaceInfo {
    bool supported{};
    bool block_compressed{};
    std::uint32_t unit_bytes{};
    std::uint32_t bits_per_pixel{};
};

DxgiSurfaceInfo dxgi_surface_info(std::uint32_t format) noexcept {
    switch (format) {
    case 70U: case 71U: case 72U:       // BC1
    case 79U: case 80U: case 81U:       // BC4
        return {true, true, 8U, 0U};
    case 73U: case 74U: case 75U:       // BC2
    case 76U: case 77U: case 78U:       // BC3
    case 82U: case 83U: case 84U:       // BC5
    case 94U: case 95U: case 96U:       // BC6H
    case 97U: case 98U: case 99U:       // BC7
        return {true, true, 16U, 0U};
    case 1U: case 2U: case 3U: case 4U: // R32G32B32A32
        return {true, false, 0U, 128U};
    case 10U: case 11U: case 12U: case 13U: case 14U: // R16G16B16A16
        return {true, false, 0U, 64U};
    case 27U: case 28U: case 29U: case 30U: case 31U: case 32U: // R8G8B8A8
    case 34U: case 35U: case 36U: case 37U: case 38U:           // R16G16
    case 87U: case 88U: case 90U: case 91U: case 92U: case 93U: // BGRA/BGRX
        return {true, false, 0U, 32U};
    case 49U: case 50U: case 51U: case 52U: // R8G8
        return {true, false, 0U, 16U};
    case 60U: case 61U: case 62U: case 63U: case 64U: case 65U: // R8/A8
        return {true, false, 0U, 8U};
    default:
        return {};
    }
}

bool build_dds_dx10_header(
    const Ba2TextureEntry& entry,
    std::vector<unsigned char>& output,
    std::string& error) {
    const auto surface = dxgi_surface_info(entry.dxgi_format);
    if (!surface.supported) {
        error = "DX10 BA2 texture uses unsupported DXGI format " +
                std::to_string(static_cast<unsigned int>(entry.dxgi_format)) + '.';
        return false;
    }

    output.assign(kDdsDx10HeaderSize, 0U);
    write_u32(output, 0U, 0x20534444U);       // "DDS "
    write_u32(output, 4U, 124U);              // DDS_HEADER size

    std::uint32_t dds_flags = 0x00001007U;    // CAPS | HEIGHT | WIDTH | PIXELFORMAT
    std::uint32_t pitch_or_linear = 0U;
    if (surface.block_compressed) {
        dds_flags |= 0x00080000U;             // DDSD_LINEARSIZE
        const auto blocks_wide = std::max<std::uint32_t>(1U, (entry.width + 3U) / 4U);
        const auto blocks_high = std::max<std::uint32_t>(1U, (entry.height + 3U) / 4U);
        const auto size64 = static_cast<std::uint64_t>(blocks_wide) * blocks_high * surface.unit_bytes;
        if (size64 > std::numeric_limits<std::uint32_t>::max()) {
            error = "DX10 BA2 texture top mip exceeds DDS 32-bit linear-size field.";
            return false;
        }
        pitch_or_linear = static_cast<std::uint32_t>(size64);
    } else {
        dds_flags |= 0x00000008U;             // DDSD_PITCH
        const auto pitch64 = (static_cast<std::uint64_t>(entry.width) * surface.bits_per_pixel + 7ULL) / 8ULL;
        if (pitch64 > std::numeric_limits<std::uint32_t>::max()) {
            error = "DX10 BA2 texture row pitch exceeds DDS 32-bit field.";
            return false;
        }
        pitch_or_linear = static_cast<std::uint32_t>(pitch64);
    }
    if (entry.mip_count > 1U) dds_flags |= 0x00020000U; // DDSD_MIPMAPCOUNT

    write_u32(output, 8U, dds_flags);
    write_u32(output, 12U, entry.height);
    write_u32(output, 16U, entry.width);
    write_u32(output, 20U, pitch_or_linear);
    write_u32(output, 28U, entry.mip_count);

    write_u32(output, 76U, 32U);              // DDS_PIXELFORMAT size
    write_u32(output, 80U, 0x00000004U);      // DDPF_FOURCC
    write_u32(output, 84U, 0x30315844U);      // "DX10"

    std::uint32_t caps = 0x00001000U;         // DDSCAPS_TEXTURE
    if (entry.mip_count > 1U) caps |= 0x00400008U; // COMPLEX | MIPMAP
    write_u32(output, 108U, caps);
    const bool cube = (entry.flags & 0x0001U) != 0U;
    if (cube) write_u32(output, 112U, 0x0000FE00U); // cube + all six faces

    write_u32(output, 128U, entry.dxgi_format);
    write_u32(output, 132U, 3U);              // D3D10_RESOURCE_DIMENSION_TEXTURE2D
    write_u32(output, 136U, cube ? 0x00000004U : 0U); // TEXTURECUBE
    write_u32(output, 140U, 1U);              // array size / cube count
    write_u32(output, 144U, 0U);
    return true;
}

const Ba2GeneralEntry* find_general_entry(
    const Ba2Inspection& archive,
    const std::string& asset_path) noexcept {
    const auto requested = lookup_key(asset_path);
    for (const auto& candidate : archive.general_entries) {
        if (lookup_key(candidate.name) == requested) return &candidate;
    }
    return nullptr;
}

const Ba2TextureEntry* find_texture_entry(
    const Ba2Inspection& archive,
    const std::string& asset_path) noexcept {
    const auto requested = lookup_key(asset_path);
    for (const auto& candidate : archive.texture_entries) {
        if (lookup_key(candidate.name) == requested) return &candidate;
    }
    return nullptr;
}

bool read_archive_bytes(
    std::ifstream& input,
    std::uint64_t offset,
    std::size_t size,
    std::vector<unsigned char>& data,
    std::string& error) {
    data.resize(size);
    if (size == 0U) return true;
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input) {
        error = "Could not seek to BA2 asset data.";
        return false;
    }
    input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
    if (input.gcount() != static_cast<std::streamsize>(size)) {
        error = "BA2 asset payload is truncated.";
        return false;
    }
    return true;
}

bool decode_zlib_block(
    const std::vector<unsigned char>& stored,
    std::size_t unpacked_size,
    std::vector<unsigned char>& decoded,
    std::string& error) {
    decoded.resize(unpacked_size);
    mz_ulong output_size = static_cast<mz_ulong>(decoded.size());
    const auto input_size = static_cast<mz_ulong>(stored.size());
    const int status = mz_uncompress(
        decoded.data(),
        &output_size,
        stored.data(),
        input_size);
    if (status != MZ_OK) {
        decoded.clear();
        error = "BA2 zlib decompression failed with status " + std::to_string(status) + '.';
        return false;
    }
    if (output_size != unpacked_size) {
        decoded.clear();
        error = "BA2 asset decoded to an unexpected size.";
        return false;
    }
    return true;
}

} // namespace

const char* ba2_archive_type_name(Ba2ArchiveType type) noexcept {
    switch (type) {
    case Ba2ArchiveType::general: return "general";
    case Ba2ArchiveType::texture: return "texture";
    case Ba2ArchiveType::unknown: return "unknown";
    }
    return "unknown";
}

Ba2Inspection inspect_ba2_index(const std::filesystem::path& path) {
    Ba2Inspection result;
    result.path = path;

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        result.error = "Could not open BA2 archive.";
        return result;
    }

    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0) {
        result.error = "Could not determine BA2 archive size.";
        return result;
    }
    const auto file_size = static_cast<std::uint64_t>(end);
    input.seekg(0, std::ios::beg);

    if (file_size < kBaseHeaderSize) {
        result.error = "Archive is smaller than a BA2 header.";
        return result;
    }

    std::array<unsigned char, kBaseHeaderSize> header{};
    if (!read_exact(input, header.data(), header.size())) {
        result.error = "Failed while reading BA2 header.";
        return result;
    }
    if (std::string_view(reinterpret_cast<const char*>(header.data()), 4) != "BTDX") {
        result.error = "Archive does not have the BTDX signature.";
        return result;
    }

    result.version = read_u32(header.data() + 4U);
    if (!supported_version(result.version)) {
        result.error = "Unsupported BA2 version " + std::to_string(result.version) + '.';
        return result;
    }

    const std::string_view type(reinterpret_cast<const char*>(header.data() + 8U), 4);
    if (type == "GNRL") result.type = Ba2ArchiveType::general;
    else if (type == "DX10") result.type = Ba2ArchiveType::texture;
    else {
        result.error = "Unsupported BA2 archive type.";
        return result;
    }

    result.header_size = archive_header_size(result.version, result.type);
    if (result.header_size > file_size) {
        result.error = "BA2 version-specific header extends past the archive.";
        return result;
    }

    result.declared_file_count = read_u32(header.data() + 12U);
    result.name_table_offset = read_u64(header.data() + 16U);
    if (result.name_table_offset < result.header_size || result.name_table_offset > file_size) {
        result.error = "BA2 name table offset is outside the archive.";
        return result;
    }

    if (result.type == Ba2ArchiveType::general) {
        if (!read_general_metadata(input, file_size, result)) return result;
    } else {
        if (!read_texture_metadata(input, file_size, result)) return result;
    }

    const auto remaining = file_size - result.name_table_offset;
    if (static_cast<std::uint64_t>(result.declared_file_count) * 2ULL > remaining) {
        result.error = "BA2 file count cannot fit in the name table.";
        return result;
    }

    input.seekg(static_cast<std::streamoff>(result.name_table_offset), std::ios::beg);
    if (!input) {
        result.error = "Could not seek to BA2 name table.";
        return result;
    }

    result.files.reserve(result.declared_file_count);
    for (std::uint32_t i = 0; i < result.declared_file_count; ++i) {
        std::array<unsigned char, 2> size_bytes{};
        if (!read_exact(input, size_bytes.data(), size_bytes.size())) {
            result.error = "BA2 name table ended before all file names were read.";
            return result;
        }

        const auto name_length = static_cast<std::size_t>(read_u16(size_bytes.data()));
        if (name_length == 0U || name_length > kMaxNameLength) {
            result.error = "BA2 contains an invalid file-name length.";
            return result;
        }

        std::string name(name_length, '\0');
        input.read(name.data(), static_cast<std::streamsize>(name_length));
        if (input.gcount() != static_cast<std::streamsize>(name_length)) {
            result.error = "BA2 name table contains a truncated file name.";
            return result;
        }

        name = normalize_archive_path(std::move(name));
        classify_asset(result.assets, name);
        result.files.push_back(name);
        if (result.type == Ba2ArchiveType::general) result.general_entries[i].name = name;
        else result.texture_entries[i].name = name;
    }

    result.valid = true;
    return result;
}

Ba2Extraction extract_ba2_general_file(
    const Ba2Inspection& archive,
    const std::string& asset_path) {
    Ba2Extraction result;
    result.name = normalize_archive_path(asset_path);

    if (!archive.valid) {
        result.error = "BA2 index is not valid.";
        return result;
    }
    if (archive.type != Ba2ArchiveType::general) {
        result.error = "Requested archive is not a General BA2.";
        return result;
    }

    const auto* entry = find_general_entry(archive, asset_path);
    if (!entry) {
        result.error = "Asset was not found in General BA2 archive.";
        return result;
    }

    std::ifstream input(archive.path, std::ios::binary);
    if (!input) {
        result.error = "Could not reopen BA2 archive for extraction.";
        return result;
    }

    const auto stored_size = static_cast<std::size_t>(
        entry->packed_size != 0U ? entry->packed_size : entry->unpacked_size);
    std::vector<unsigned char> stored;
    if (!read_archive_bytes(input, entry->data_offset, stored_size, stored, result.error)) return result;

    if (entry->packed_size == 0U) {
        result.data = std::move(stored);
        result.valid = true;
        return result;
    }
    if (entry->unpacked_size == 0U) {
        result.error = "Compressed BA2 entry declares zero unpacked size.";
        return result;
    }
    if (!decode_zlib_block(stored, entry->unpacked_size, result.data, result.error)) return result;

    result.valid = true;
    return result;
}

Ba2Extraction extract_ba2_texture_file(
    const Ba2Inspection& archive,
    const std::string& asset_path) {
    Ba2Extraction result;
    result.name = normalize_archive_path(asset_path);

    if (!archive.valid) {
        result.error = "BA2 index is not valid.";
        return result;
    }
    if (archive.type != Ba2ArchiveType::texture) {
        result.error = "Requested archive is not a DX10 texture BA2.";
        return result;
    }

    const auto* entry = find_texture_entry(archive, asset_path);
    if (!entry) {
        result.error = "Asset was not found in DX10 texture BA2 archive.";
        return result;
    }
    if (!build_dds_dx10_header(*entry, result.data, result.error)) return result;

    std::uint64_t payload_size = 0U;
    for (const auto& chunk : entry->chunks) {
        payload_size += chunk.unpacked_size;
        if (payload_size > kMaxExtractedFileSize) {
            result.error = "Reconstructed DDS payload exceeds the supported extracted-file size limit.";
            result.data.clear();
            return result;
        }
    }
    result.data.reserve(kDdsDx10HeaderSize + static_cast<std::size_t>(payload_size));

    std::ifstream input(archive.path, std::ios::binary);
    if (!input) {
        result.error = "Could not reopen DX10 BA2 archive for extraction.";
        result.data.clear();
        return result;
    }

    for (const auto& chunk : entry->chunks) {
        const auto stored_size = static_cast<std::size_t>(
            chunk.packed_size != 0U ? chunk.packed_size : chunk.unpacked_size);
        std::vector<unsigned char> stored;
        if (!read_archive_bytes(input, chunk.data_offset, stored_size, stored, result.error)) {
            result.data.clear();
            return result;
        }

        if (chunk.packed_size == 0U) {
            result.data.insert(result.data.end(), stored.begin(), stored.end());
            continue;
        }
        if (archive.header_size == 36U) {
            result.error = "DX10 BA2 version-3 compressed chunks use the LZ4 variant, which is not enabled in v0.1.";
            result.data.clear();
            return result;
        }

        std::vector<unsigned char> decoded;
        if (!decode_zlib_block(stored, chunk.unpacked_size, decoded, result.error)) {
            result.data.clear();
            return result;
        }
        result.data.insert(result.data.end(), decoded.begin(), decoded.end());
    }

    if (result.data.size() != kDdsDx10HeaderSize + static_cast<std::size_t>(payload_size)) {
        result.error = "Reconstructed DDS size does not match DX10 BA2 chunk metadata.";
        result.data.clear();
        return result;
    }

    result.valid = true;
    return result;
}

} // namespace f4lod
