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

std::vector<unsigned char> make_btr(bool bad_multibound = false) {
    std::vector<unsigned char> out;
    constexpr std::string_view header = "Gamebryo File Format, Version 20.2.0.7";
    out.insert(out.end(), header.begin(), header.end());
    u8(out, '\n');
    u32(out, 335675399U);
    u8(out, 1);
    u32(out, 12U);
    u32(out, 4U);
    u32(out, 130U);
    short_header_string(out, "F4LODGen fixture");
    short_header_string(out, "");
    short_header_string(out, "");
    short_header_string(out, "");

    u16(out, 4U);
    sized_string(out, "BSMultiBoundNode");
    sized_string(out, "BSTriShape");
    sized_string(out, "BSMultiBound");
    sized_string(out, "BSMultiBoundAABB");

    u16(out, 0U);
    u16(out, 1U);
    u16(out, 2U);
    u16(out, 3U);

    u32(out, 88U);
    u32(out, 0U);
    u32(out, 4U);
    u32(out, 24U);

    u32(out, 1U);
    u32(out, 3U);
    sized_string(out, "obj");
    u32(out, 0U);

    i32(out, 0);
    u32(out, 0U);
    i32(out, -1);
    u16(out, 14U);
    u16(out, 0U);
    f32(out, 0.0F); f32(out, 0.0F); f32(out, 0.0F);
    f32(out, 1.0F); f32(out, 0.0F); f32(out, 0.0F);
    f32(out, 0.0F); f32(out, 1.0F); f32(out, 0.0F);
    f32(out, 0.0F); f32(out, 0.0F); f32(out, 1.0F);
    f32(out, 1.0F);
    i32(out, -1);
    u32(out, 1U);
    i32(out, 1);
    i32(out, bad_multibound ? 99 : 2);
    u32(out, 0U);

    i32(out, 3);

    f32(out, 8192.0F); f32(out, 8192.0F); f32(out, 512.0F);
    f32(out, 8192.0F); f32(out, 8192.0F); f32(out, 1024.0F);

    // NiFooter: one root, block 0.
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
    const auto root = std::filesystem::temp_directory_path() / "f4lodgen-nif-smoke";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const auto valid_path = root / "Commonwealth.4.0.0.btr";
    write_file(valid_path, make_btr());

    const auto nif = f4lod::inspect_nif(valid_path);
    assert(nif.valid);
    assert(nif.is_fallout4());
    assert(nif.blocks.size() == 4);
    assert(nif.blocks[0].type == "BSMultiBoundNode");
    assert(nif.blocks[0].size == 88);
    assert(nif.strings.size() == 1);
    assert(nif.strings[0] == "obj");
    assert(nif.roots.size() == 1);
    assert(nif.roots[0] == 0);
    assert(nif.footer_offset > nif.data_offset);

    const auto btr = f4lod::inspect_fallout4_btr(valid_path);
    assert(btr.valid);
    assert(btr.root_children.size() == 1);
    assert(btr.terrain_shape == 1);
    assert(btr.root_multibound == 2);
    assert(btr.bounds_data == 3);
    assert(btr.bounds_position[0] == 8192.0F);
    assert(btr.bounds_extent[2] == 1024.0F);

    const auto broken_path = root / "Broken.btr";
    write_file(broken_path, make_btr(true));
    const auto broken = f4lod::inspect_fallout4_btr(broken_path);
    assert(!broken.valid);
    assert(!broken.error.empty());

    auto missing_footer = make_btr();
    missing_footer.resize(missing_footer.size() - 8U);
    const auto missing_footer_path = root / "MissingFooter.btr";
    write_file(missing_footer_path, missing_footer);
    assert(!f4lod::inspect_nif(missing_footer_path).valid);

    auto bad_root = make_btr();
    bad_root[bad_root.size() - 4U] = 99U;
    const auto bad_root_path = root / "BadRoot.btr";
    write_file(bad_root_path, bad_root);
    assert(!f4lod::inspect_nif(bad_root_path).valid);

    auto trailing = make_btr();
    trailing.push_back(0xAAU);
    const auto trailing_path = root / "Trailing.btr";
    write_file(trailing_path, trailing);
    assert(!f4lod::inspect_nif(trailing_path).valid);

    std::filesystem::remove_all(root);
    return 0;
}
