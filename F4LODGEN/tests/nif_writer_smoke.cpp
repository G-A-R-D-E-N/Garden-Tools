#include "f4lod/nif.hpp"
#include "f4lod/nif_writer.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

std::uint32_t read_u32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8U) |
           (static_cast<std::uint32_t>(p[2]) << 16U) |
           (static_cast<std::uint32_t>(p[3]) << 24U);
}

std::vector<unsigned char> read_all(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    file.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(file.tellg());
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> data(size);
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return data;
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "f4lodgen-nif-writer-smoke";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    f4lod::NifWriteDocument document;
    document.creator = "F4LODGen test";
    document.strings = {"obj", "materials\\terrain\\test.bgsm"};
    document.blocks = {
        {"NiNode", {0x01, 0x02, 0x03, 0x04}},
        {"BSMultiBound", {0x05, 0x06, 0x07, 0x08}},
        {"NiNode", {0x09}},
    };
    document.roots = {0};

    const auto path = root / "Container.nif";
    const auto written = f4lod::write_fallout4_nif(path, document);
    assert(written.valid);
    assert(written.bytes_written > 0);

    const auto inspection = f4lod::inspect_nif(path);
    assert(inspection.valid);
    assert(inspection.is_fallout4());
    assert(inspection.creator == "F4LODGen test");
    assert(inspection.blocks.size() == 3);
    assert(inspection.blocks[0].type == "NiNode");
    assert(inspection.blocks[0].size == 4);
    assert(inspection.blocks[1].type == "BSMultiBound");
    assert(inspection.blocks[2].type == "NiNode");
    assert(inspection.strings.size() == 2);
    assert(inspection.strings[0] == "obj");

    const auto bytes = read_all(path);
    assert(bytes.size() == written.bytes_written);
    assert(bytes.size() >= 8);
    assert(read_u32(bytes.data() + bytes.size() - 8) == 1U);
    assert(read_u32(bytes.data() + bytes.size() - 4) == 0U);

    f4lod::NifWriteDocument bad = document;
    bad.roots = {99};
    assert(!f4lod::write_fallout4_nif(root / "Bad.nif", bad).valid);

    std::filesystem::remove_all(root);
    return 0;
}
