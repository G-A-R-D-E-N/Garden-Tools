#include "f4lod/record_data.hpp"

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

void append_signature(std::vector<unsigned char>& out, std::string_view signature) {
    assert(signature.size() == 4);
    out.insert(out.end(), signature.begin(), signature.end());
}

void append_subrecord(
    std::vector<unsigned char>& out,
    std::string_view type,
    const std::vector<unsigned char>& data) {
    append_signature(out, type);
    append_u16(out, static_cast<std::uint16_t>(data.size()));
    out.insert(out.end(), data.begin(), data.end());
}

void write_record_blob(
    const std::filesystem::path& path,
    const std::vector<unsigned char>& data) {
    std::ofstream file(path, std::ios::binary);
    const std::vector<unsigned char> header(24, 0);
    file.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

f4lod::WinningRecord make_record(
    const std::filesystem::path& path,
    std::uint32_t flags,
    std::uint32_t data_size) {
    f4lod::WinningRecord record;
    record.key = {"Fixture.esp", 0x100U};
    record.type = "WRLD";
    record.source_plugin = "Fixture.esp";
    record.source_path = path;
    record.flags = flags;
    record.data_size = data_size;
    record.file_offset = 0;
    return record;
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "f4lodgen-record-data-smoke";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    std::vector<unsigned char> xclc;
    append_u32(xclc, static_cast<std::uint32_t>(-12));
    append_u32(xclc, 34U);
    std::vector<unsigned char> uncompressed;
    append_subrecord(uncompressed, "XCLC", xclc);

    const auto uncompressed_path = root / "uncompressed.bin";
    write_record_blob(uncompressed_path, uncompressed);
    const auto uncompressed_payload = f4lod::read_record_payload(
        make_record(uncompressed_path, 0, static_cast<std::uint32_t>(uncompressed.size())));
    assert(uncompressed_payload.valid);
    const auto* xclc_record = f4lod::find_subrecord(uncompressed_payload, "XCLC");
    assert(xclc_record != nullptr);
    std::int32_t x = 0;
    std::int32_t y = 0;
    assert(f4lod::read_i32_pair(*xclc_record, x, y));
    assert(x == -12);
    assert(y == 34);

    // Decodes to: EDID, uint16 length 13, "Commonwealth\\0".
    const std::vector<unsigned char> compressed{
        0x13, 0x00, 0x00, 0x00,
        0x78, 0x9C, 0x73, 0x75, 0xF1, 0x74, 0xE1, 0x65,
        0x70, 0xCE, 0xCF, 0xCD, 0xCD, 0xCF, 0x2B, 0x4F,
        0x4D, 0xCC, 0x29, 0xC9, 0x60, 0x00, 0x00, 0x38,
        0x19, 0x06, 0x12,
    };
    const auto compressed_path = root / "compressed.bin";
    write_record_blob(compressed_path, compressed);
    const auto compressed_payload = f4lod::read_record_payload(
        make_record(compressed_path, 0x00040000U, static_cast<std::uint32_t>(compressed.size())));
    assert(compressed_payload.valid);
    const auto* edid = f4lod::find_subrecord(compressed_payload, "EDID");
    assert(edid != nullptr);
    assert(f4lod::read_zstring(*edid) == "Commonwealth");

    auto broken = compressed;
    broken.resize(12);
    const auto broken_path = root / "broken.bin";
    write_record_blob(broken_path, broken);
    const auto broken_payload = f4lod::read_record_payload(
        make_record(broken_path, 0x00040000U, static_cast<std::uint32_t>(broken.size())));
    assert(!broken_payload.valid);

    std::filesystem::remove_all(root);
    return 0;
}
