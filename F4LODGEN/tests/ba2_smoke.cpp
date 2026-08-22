#include "f4lod/ba2.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void append_u16(std::vector<unsigned char>& out, std::uint16_t value) {
    out.push_back(static_cast<unsigned char>(value & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
}

void append_u32(std::vector<unsigned char>& out, std::uint32_t value) {
    out.push_back(static_cast<unsigned char>(value & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<unsigned char>((value >> 24U) & 0xFFU));
}

void append_u64(std::vector<unsigned char>& out, std::uint64_t value) {
    append_u32(out, static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));
    append_u32(out, static_cast<std::uint32_t>(value >> 32U));
}

void append_signature(std::vector<unsigned char>& out, std::string_view signature) {
    assert(signature.size() == 4);
    out.insert(out.end(), signature.begin(), signature.end());
}

void write_general_ba2(
    const std::filesystem::path& path,
    const std::vector<std::string>& names) {
    constexpr std::uint64_t header_size = 24;
    constexpr std::uint64_t general_record_size = 36;
    const auto name_offset = header_size + general_record_size * names.size();

    std::vector<unsigned char> bytes;
    append_signature(bytes, "BTDX");
    append_u32(bytes, 1);
    append_signature(bytes, "GNRL");
    append_u32(bytes, static_cast<std::uint32_t>(names.size()));
    append_u64(bytes, name_offset);

    bytes.resize(static_cast<std::size_t>(name_offset), 0);
    for (const auto& name : names) {
        append_u16(bytes, static_cast<std::uint16_t>(name.size()));
        bytes.insert(bytes.end(), name.begin(), name.end());
    }

    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void append_general_record(
    std::vector<unsigned char>& out,
    std::uint64_t data_offset,
    std::uint32_t packed_size,
    std::uint32_t unpacked_size) {
    append_u32(out, 0);
    append_u32(out, 0);
    append_u32(out, 0);
    append_u32(out, 0xBAADF00DU);
    append_u64(out, data_offset);
    append_u32(out, packed_size);
    append_u32(out, unpacked_size);
    append_u32(out, 0);
}

void write_extractable_general_ba2(const std::filesystem::path& path) {
    const std::string lod_name = "lodsettings\\Commonwealth.lod";
    const std::string nif_name = "Meshes\\Architecture\\compressed.nif";
    const std::vector<unsigned char> lod_data{
        0xC0, 0xFF, 0xD0, 0xFF,
        0x80, 0x00, 0x00, 0x00,
        0x04, 0x00, 0x00, 0x00,
        0x20, 0x00, 0x00, 0x00,
    };
    const std::vector<unsigned char> compressed_nif{
        0x78, 0x9C, 0x4B, 0xCE, 0xCF, 0x2D, 0x28, 0x4A,
        0x2D, 0x2E, 0x4E, 0x4D, 0x51, 0x28, 0x48, 0xAC,
        0xCC, 0xC9, 0x4F, 0x4C, 0x01, 0x00, 0x45, 0xC5,
        0x07, 0x40,
    };
    const std::string nif_decoded = "compressed payload";

    constexpr std::uint64_t header_size = 24;
    constexpr std::uint64_t record_size = 36;
    constexpr std::uint32_t file_count = 2;
    const std::uint64_t name_offset = header_size + record_size * file_count;
    const std::uint64_t name_table_size =
        2 + lod_name.size() +
        2 + nif_name.size();
    const std::uint64_t lod_offset = name_offset + name_table_size;
    const std::uint64_t nif_offset = lod_offset + lod_data.size();

    std::vector<unsigned char> bytes;
    append_signature(bytes, "BTDX");
    append_u32(bytes, 1);
    append_signature(bytes, "GNRL");
    append_u32(bytes, file_count);
    append_u64(bytes, name_offset);

    append_general_record(
        bytes,
        lod_offset,
        0,
        static_cast<std::uint32_t>(lod_data.size()));
    append_general_record(
        bytes,
        nif_offset,
        static_cast<std::uint32_t>(compressed_nif.size()),
        static_cast<std::uint32_t>(nif_decoded.size()));

    append_u16(bytes, static_cast<std::uint16_t>(lod_name.size()));
    bytes.insert(bytes.end(), lod_name.begin(), lod_name.end());
    append_u16(bytes, static_cast<std::uint16_t>(nif_name.size()));
    bytes.insert(bytes.end(), nif_name.begin(), nif_name.end());
    bytes.insert(bytes.end(), lod_data.begin(), lod_data.end());
    bytes.insert(bytes.end(), compressed_nif.begin(), compressed_nif.end());

    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "f4lodgen-ba2-smoke";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const auto archive = root / "Fixture - Main.ba2";
    write_general_ba2(
        archive,
        {
            "Meshes\\Architecture\\fixture.nif",
            "Materials\\Architecture\\fixture.bgsm",
            "Textures\\Architecture\\fixture.dds",
            "Scripts\\fixture.pex",
        });

    const auto inspection = f4lod::inspect_ba2_index(archive);
    assert(inspection.valid);
    assert(inspection.error.empty());
    assert(inspection.version == 1);
    assert(inspection.type == f4lod::Ba2ArchiveType::general);
    assert(inspection.header_size == 24);
    assert(inspection.declared_file_count == 4);
    assert(inspection.files.size() == 4);
    assert(inspection.general_entries.size() == 4);
    assert(inspection.files[0] == "Meshes/Architecture/fixture.nif");
    assert(inspection.assets.meshes == 1);
    assert(inspection.assets.materials == 1);
    assert(inspection.assets.textures == 1);
    assert(inspection.assets.other == 1);

    const auto extractable_archive = root / "Fixture - Extract.ba2";
    write_extractable_general_ba2(extractable_archive);
    const auto extractable = f4lod::inspect_ba2_index(extractable_archive);
    assert(extractable.valid);
    assert(extractable.general_entries.size() == 2);

    const auto lod = f4lod::extract_ba2_general_file(
        extractable,
        "LODSETTINGS/Commonwealth.LOD");
    assert(lod.valid);
    assert(lod.data.size() == 16);
    assert(lod.data[0] == 0xC0);
    assert(lod.data[1] == 0xFF);
    assert(lod.data[12] == 0x20);

    const auto nif = f4lod::extract_ba2_general_file(
        extractable,
        "meshes/architecture/compressed.nif");
    assert(nif.valid);
    const std::string nif_text(nif.data.begin(), nif.data.end());
    assert(nif_text == "compressed payload");

    const auto missing = f4lod::extract_ba2_general_file(
        extractable,
        "meshes/missing.nif");
    assert(!missing.valid);

    const auto broken = root / "Broken.ba2";
    {
        std::ofstream file(broken, std::ios::binary);
        file << "not a ba2";
    }
    assert(!f4lod::inspect_ba2_index(broken).valid);

    std::filesystem::remove_all(root);
    return 0;
}
