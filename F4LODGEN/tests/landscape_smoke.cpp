#include "f4lod/landscape.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
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

void append_f32(std::vector<unsigned char>& out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    append_u32(out, bits);
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

void write_record_blob(const std::filesystem::path& path, const std::vector<unsigned char>& payload) {
    std::ofstream file(path, std::ios::binary);
    const std::vector<unsigned char> header(24, 0);
    file.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    file.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
}

f4lod::WinningRecord make_land(const std::filesystem::path& path, std::uint32_t data_size) {
    f4lod::WinningRecord record;
    record.key = {"Fixture.esp", 0x300U};
    record.type = "LAND";
    record.source_plugin = "Fixture.esp";
    record.source_path = path;
    record.data_size = data_size;
    record.file_offset = 0;
    return record;
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "f4lodgen-landscape-smoke";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    std::vector<unsigned char> vhgt;
    append_f32(vhgt, 10.0F);
    vhgt.resize(4 + f4lod::kLandscapeVertexCount, 0);

    auto* deltas = vhgt.data() + 4;
    deltas[0] = 1;
    deltas[1] = 2;
    deltas[f4lod::kLandscapeVertexSide] = 3;
    deltas[f4lod::kLandscapeVertexSide + 1] = 0xFF;

    std::vector<unsigned char> payload;
    append_subrecord(payload, "VHGT", vhgt);

    const auto path = root / "land.bin";
    write_record_blob(path, payload);
    const auto heightmap = f4lod::decode_landscape_heightmap(
        make_land(path, static_cast<std::uint32_t>(payload.size())));

    assert(heightmap.valid);
    assert(heightmap.height_offset == 80.0F);
    assert(heightmap.at(0, 0) == 88.0F);
    assert(heightmap.at(1, 0) == 104.0F);
    assert(heightmap.at(0, 1) == 112.0F);
    assert(heightmap.at(1, 1) == 104.0F);
    assert(heightmap.at(32, 32) == 112.0F);
    assert(heightmap.min_height == 88.0F);
    assert(heightmap.max_height == 112.0F);

    std::vector<unsigned char> missing_payload;
    append_subrecord(missing_payload, "DATA", {0, 0, 0, 0});
    const auto missing_path = root / "missing.bin";
    write_record_blob(missing_path, missing_payload);
    const auto missing = f4lod::decode_landscape_heightmap(
        make_land(missing_path, static_cast<std::uint32_t>(missing_payload.size())));
    assert(!missing.valid);

    std::filesystem::remove_all(root);
    return 0;
}
