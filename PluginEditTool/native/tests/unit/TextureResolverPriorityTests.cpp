#include <catch2/catch_test_macros.hpp>
#include "../helpers/TempRoot.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fo4/archive/ba2.hpp>
#include <fo4/archive/ba2_texture.hpp>
#include <fo4ide/assets/TextureResolver.hpp>

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_texture_priority_" + id);
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void writeText(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(stream)),
                       std::istreambuf_iterator<char>());
}

std::vector<std::byte> bytesOf(std::string_view text) {
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const unsigned char ch : text) bytes.push_back(static_cast<std::byte>(ch));
    return bytes;
}

void writeArchive(const std::filesystem::path& path,
                  std::string_view innerPath,
                  std::string_view payload) {
    fo4::archive::Ba2GeneralArchive archive;
    fo4::archive::Ba2GeneralEntry entry;
    entry.path = std::string(innerPath);
    const auto bytes = bytesOf(payload);
    entry.chunks.push_back(fo4::archive::make_stored_ba2_chunk(bytes));
    archive.entries.push_back(std::move(entry));
    std::filesystem::create_directories(path.parent_path());
    fo4::archive::write_ba2_general_file(archive, path);
}

void writeRawTextureArchive(const std::filesystem::path& path) {
    fo4::archive::Ba2TextureArchive archive;
    archive.version = 8;
    fo4::archive::Ba2TextureEntry entry;
    entry.path = "Textures\\Raw.dds";
    entry.texture = {4, 4, 1, 28, 0, 8};
    std::vector<std::byte> rgba(4u * 4u * 4u);
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        rgba[i] = std::byte{0x20};
        rgba[i + 1] = std::byte{0x40};
        rgba[i + 2] = std::byte{0x80};
        rgba[i + 3] = std::byte{0xFF};
    }
    entry.chunks.push_back(fo4::archive::make_stored_ba2_texture_chunk(rgba));
    archive.entries.push_back(std::move(entry));
    std::filesystem::create_directories(path.parent_path());
    fo4::archive::write_ba2_texture_file(archive, path);
}

}  // namespace

TEST_CASE("Texture resolver uses the first configured root as the highest loose-file priority") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto low = root / "low";
    const auto high = root / "high";
    writeText(low / "Meshes" / "Shared.nif", "low");
    writeText(high / "Meshes" / "Shared.nif", "high");

    fo4ide::assets::TextureResolver resolver(root / "cache");
    resolver.setSessionRoots({high, low});

    const auto result = resolver.resolveNif("Meshes\\Shared.nif");
    REQUIRE(result.has_value());
    CHECK(readText(*result) == "high");
}

TEST_CASE("Texture resolver honors archive root priority and clears stale index entries") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto low = root / "low";
    const auto high = root / "high";
    writeArchive(low / "low.ba2", "Meshes\\Shared.nif", "low-archive");
    writeArchive(high / "high.ba2", "Meshes\\Shared.nif", "high-archive");

    fo4ide::assets::TextureResolver resolver(root / "cache");
    resolver.setSessionRoots({high, low});

    const auto highResult = resolver.resolveNif("Meshes\\Shared.nif");
    REQUIRE(highResult.has_value());
    CHECK(readText(*highResult) == "high-archive");

    resolver.setSessionRoots({low});
    const auto lowResult = resolver.resolveNif("Meshes\\Shared.nif");
    REQUIRE(lowResult.has_value());
    CHECK(readText(*lowResult) == "low-archive");
}

TEST_CASE("Texture resolver rebuilds a DDS header for raw DX10 BA2 mip payloads") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto data = root / "data";
    writeRawTextureArchive(data / "textures.ba2");

    fo4ide::assets::TextureResolver resolver(root / "cache");
    resolver.setSessionRoots({data});

    const auto url = resolver.getTexturePngDataUrl("", "Textures\\Raw.dds", "");
    CHECK(url.starts_with("data:image/png;base64,"));
}
