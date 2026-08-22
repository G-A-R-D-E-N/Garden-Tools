#include "f4lod/btr_geometry.hpp"
#include "f4lod/nif.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

namespace {

void u8(std::vector<unsigned char>& out, std::uint8_t v) { out.push_back(v); }
void u16(std::vector<unsigned char>& out, std::uint16_t v) {
    out.push_back(static_cast<unsigned char>(v & 0xFFU));
    out.push_back(static_cast<unsigned char>((v >> 8U) & 0xFFU));
}
void u32(std::vector<unsigned char>& out, std::uint32_t v) {
    out.push_back(static_cast<unsigned char>(v & 0xFFU));
    out.push_back(static_cast<unsigned char>((v >> 8U) & 0xFFU));
    out.push_back(static_cast<unsigned char>((v >> 16U) & 0xFFU));
    out.push_back(static_cast<unsigned char>((v >> 24U) & 0xFFU));
}
void i32(std::vector<unsigned char>& out, std::int32_t v) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    u32(out, bits);
}
void f32(std::vector<unsigned char>& out, float v) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    u32(out, bits);
}
void short_header_string(std::vector<unsigned char>& out, std::string_view s) {
    u8(out, static_cast<std::uint8_t>(s.size() + 1U));
    out.insert(out.end(), s.begin(), s.end());
    u8(out, 0);
}
void sized_string(std::vector<unsigned char>& out, std::string_view s) {
    u32(out, static_cast<std::uint32_t>(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}

void avobject(std::vector<unsigned char>& out) {
    i32(out, 0);
    u32(out, 0);
    i32(out, -1);
    u16(out, 14);
    u16(out, 0);
    f32(out, 0); f32(out, 0); f32(out, 0);
    f32(out, 1); f32(out, 0); f32(out, 0);
    f32(out, 0); f32(out, 1); f32(out, 0);
    f32(out, 0); f32(out, 0); f32(out, 1);
    f32(out, 1);
    i32(out, -1);
}

std::vector<unsigned char> make_btr(bool bad_data_size) {
    std::vector<unsigned char> out;
    constexpr std::string_view header = "Gamebryo File Format, Version 20.2.0.7";
    out.insert(out.end(), header.begin(), header.end());
    u8(out, '\n');
    u32(out, 335675399U);
    u8(out, 1);
    u32(out, 12U);
    u32(out, 4U);
    u32(out, 130U);
    short_header_string(out, "F4LODGen geometry fixture");
    short_header_string(out, "");
    short_header_string(out, "");
    short_header_string(out, "");

    u16(out, 4U);
    sized_string(out, "BSMultiBoundNode");
    sized_string(out, "BSSubIndexTriShape");
    sized_string(out, "BSMultiBound");
    sized_string(out, "BSMultiBoundAABB");

    u16(out, 0U); u16(out, 1U); u16(out, 2U); u16(out, 3U);
    u32(out, 88U); u32(out, 208U); u32(out, 4U); u32(out, 24U);
    u32(out, 1U); u32(out, 3U); sized_string(out, "obj");
    u32(out, 0U);

    avobject(out);
    u32(out, 1U); i32(out, 1);
    i32(out, 2); u32(out, 0U);

    avobject(out);
    f32(out, 64.0F); f32(out, 64.0F); f32(out, 0.0F);
    f32(out, 128.0F);
    i32(out, -1); i32(out, -1); i32(out, -1);

    u8(out, 6U);
    u8(out, 4U);
    u8(out, 101U);
    u8(out, 0U);
    u8(out, 0U);
    u16(out, static_cast<std::uint16_t>(16U | 32U | 128U | 16384U));
    u8(out, 0U);
    u32(out, 1U);
    u16(out, 3U);
    u32(out, bad_data_size ? 77U : 78U);

    for (int vertex = 0; vertex < 3; ++vertex) {
        f32(out, vertex == 1 ? 128.0F : 0.0F);
        f32(out, vertex == 2 ? 128.0F : 0.0F);
        f32(out, 0.0F);
        f32(out, 0.0F);
        u16(out, 0U); u16(out, 0U);
        u8(out, 128U); u8(out, 128U); u8(out, 255U); u8(out, 128U);
    }
    u16(out, 0U); u16(out, 1U); u16(out, 2U);

    u32(out, 1U); u32(out, 0U); u32(out, 0U);

    i32(out, 3);
    f32(out, 8192); f32(out, 8192); f32(out, 0);
    f32(out, 8192); f32(out, 8192); f32(out, 512);

    u32(out, 1U);
    i32(out, 0);
    return out;
}

void write_file(const std::filesystem::path& path, const std::vector<unsigned char>& data) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "f4lodgen-btr-geometry-smoke";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const auto good = root / "Good.btr";
    write_file(good, make_btr(false));
    const auto btr = f4lod::inspect_fallout4_btr(good);
    assert(btr.valid);
    assert(btr.nif.roots.size() == 1U);
    assert(btr.nif.roots[0] == 0);
    const auto geometry = f4lod::inspect_fallout4_btr_geometry(good, btr.terrain_shape);
    assert(geometry.valid);
    assert(geometry.type == "BSSubIndexTriShape");
    assert(geometry.vertex_count == 3);
    assert(geometry.triangle_count == 1);
    assert(geometry.vertex_stride == 24);
    assert(geometry.data_size == 78);
    assert(geometry.expected_geometry_bytes == 78);
    assert(geometry.subindex_triangle_count == 1);
    assert(geometry.subindex_a == 0);

    const auto bad = root / "Bad.btr";
    write_file(bad, make_btr(true));
    const auto invalid = f4lod::inspect_fallout4_btr_geometry(bad, 1);
    assert(!invalid.valid);
    assert(!invalid.error.empty());

    std::filesystem::remove_all(root);
    return 0;
}
