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

#include <fo4/archive/assets.hpp>
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

void writeArchive(const std::filesystem::path& path,
                  const std::vector<std::pair<std::string, std::string>>& files) {
    fo4::archive::Ba2GeneralArchive archive;
    for (const auto& [innerPath, payload] : files) {
        fo4::archive::Ba2GeneralEntry entry;
        entry.path = innerPath;
        entry.chunks.push_back(fo4::archive::make_stored_ba2_chunk(bytesOf(payload)));
        archive.entries.push_back(std::move(entry));
    }
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

TEST_CASE("Texture resolver and glTF export pick the same copy of a path shared by two archives in one root") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto data = root / "data";
    writeArchive(data / "a.ba2", "Materials\\Shared.bgsm", "from-a");
    writeArchive(data / "b.ba2", "Materials\\Shared.bgsm", "from-b");

    fo4ide::assets::TextureResolver resolver(root / "cache");
    resolver.setSessionRoots({data});

    const auto preview = resolver.resolveMaterialFile("", "Materials\\Shared.bgsm", "");
    REQUIRE(preview.has_value());
    CHECK(readText(*preview) == "from-b");

    const auto assets = resolver.assetResolver();
    const auto exported = assets->locate_in_archives("materials/shared.bgsm");
    REQUIRE(exported.has_value());
    const auto bytes = assets->read(*exported);
    CHECK(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()) == "from-b");
}

TEST_CASE("Texture resolver and glTF export give the higher root priority to a shared archive path") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto high = root / "high";
    const auto low = root / "low";
    writeArchive(high / "high.ba2", "Materials\\Shared.bgsm", "from-high");
    writeArchive(low / "low.ba2", "Materials\\Shared.bgsm", "from-low");

    fo4ide::assets::TextureResolver resolver(root / "cache");
    resolver.setSessionRoots({high, low});

    const auto preview = resolver.resolveMaterialFile("", "Materials\\Shared.bgsm", "");
    REQUIRE(preview.has_value());
    CHECK(readText(*preview) == "from-high");

    const auto assets = resolver.assetResolver();
    const auto exported = assets->locate_in_archives("materials/shared.bgsm");
    REQUIRE(exported.has_value());
    const auto bytes = assets->read(*exported);
    CHECK(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()) == "from-high");
}

TEST_CASE("Texture resolver returns the requested root's archive in either preview order") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto dataA = root / "A" / "Data";
    const auto dataB = root / "B" / "Data";
    writeArchive(dataA / "a.ba2", "Materials\\Shared.bgsm", "from-a");
    writeArchive(dataB / "b.ba2", "Materials\\Shared.bgsm", "from-b");
    const auto nifA = (dataA / "meshes" / "thing.nif").string();
    const auto nifB = (dataB / "meshes" / "thing.nif").string();

    fo4ide::assets::TextureResolver aFirst(root / "cache-a-first");
    const auto a1 = aFirst.resolveMaterialFile(nifA, "Materials\\Shared.bgsm", "");
    REQUIRE(a1.has_value());
    CHECK(readText(*a1) == "from-a");
    const auto b1 = aFirst.resolveMaterialFile(nifB, "Materials\\Shared.bgsm", "");
    REQUIRE(b1.has_value());
    CHECK(readText(*b1) == "from-b");

    fo4ide::assets::TextureResolver bFirst(root / "cache-b-first");
    const auto b2 = bFirst.resolveMaterialFile(nifB, "Materials\\Shared.bgsm", "");
    REQUIRE(b2.has_value());
    CHECK(readText(*b2) == "from-b");
    const auto a2 = bFirst.resolveMaterialFile(nifA, "Materials\\Shared.bgsm", "");
    REQUIRE(a2.has_value());
    CHECK(readText(*a2) == "from-a");
}

TEST_CASE("Texture resolver re-extracts an archived NIF rewritten with its old timestamp") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto archive = root / "data" / "meshes.ba2";
    writeArchive(archive, "Meshes\\Armor\\Swap.nif", "old-mesh-bytes");
    const auto stamp = std::filesystem::last_write_time(archive);

    fo4ide::assets::TextureResolver resolver(root / "cache");
    resolver.setSessionRoots({root / "data"});
    const auto before = resolver.resolveNif("meshes\\armor\\swap.nif");
    REQUIRE(before.has_value());
    CHECK(readText(*before) == "old-mesh-bytes");

    writeArchive(archive, "Meshes\\Armor\\Swap.nif", "new-mesh-bytes");
    std::filesystem::last_write_time(archive, stamp);

    const auto after = resolver.resolveNif("meshes\\armor\\swap.nif");
    REQUIRE(after.has_value());
    CHECK(readText(*after) == "new-mesh-bytes");
}

TEST_CASE("Texture resolver re-extracts an archived NIF rewritten with its old timestamp and a different size") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto archive = root / "data" / "meshes.ba2";
    writeArchive(archive, "Meshes\\Armor\\Swap.nif", "old-mesh");
    const auto stamp = std::filesystem::last_write_time(archive);

    fo4ide::assets::TextureResolver resolver(root / "cache");
    resolver.setSessionRoots({root / "data"});
    const auto before = resolver.resolveNif("meshes\\armor\\swap.nif");
    REQUIRE(before.has_value());
    CHECK(readText(*before) == "old-mesh");

    writeArchive(archive, "Meshes\\Armor\\Swap.nif", "much-longer-new-mesh-bytes");
    std::filesystem::last_write_time(archive, stamp);

    const auto after = resolver.resolveNif("meshes\\armor\\swap.nif");
    REQUIRE(after.has_value());
    CHECK(readText(*after) == "much-longer-new-mesh-bytes");
}

TEST_CASE("Texture resolver serves a sibling entry rewritten in place after another entry primed the reader") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto archive = root / "data" / "meshes.ba2";
    writeArchive(archive, {{"Meshes\\Armor\\Keep.nif", "keep-bytes"}, {"Meshes\\Armor\\Swap.nif", "old-mesh-bytes"}});
    const auto stamp = std::filesystem::last_write_time(archive);

    fo4ide::assets::TextureResolver resolver(root / "cache");
    resolver.setSessionRoots({root / "data"});
    const auto keep = resolver.resolveNif("meshes\\armor\\keep.nif");
    REQUIRE(keep.has_value());
    CHECK(readText(*keep) == "keep-bytes");

    writeArchive(archive, {{"Meshes\\Armor\\Keep.nif", "keep-bytes"}, {"Meshes\\Armor\\Swap.nif", "new-mesh-bytes"}});
    std::filesystem::last_write_time(archive, stamp);

    const auto swap = resolver.resolveNif("meshes\\armor\\swap.nif");
    REQUIRE(swap.has_value());
    CHECK(readText(*swap) == "new-mesh-bytes");
    const auto again = resolver.resolveNif("meshes\\armor\\keep.nif");
    REQUIRE(again.has_value());
    CHECK(readText(*again) == "keep-bytes");
}

TEST_CASE("Texture resolver and glTF export both find a material in an archive nested under a root") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto data = root / "data";
    writeArchive(data / "nested" / "deep.ba2", "Materials\\Nested.bgsm", "from-nested");

    fo4ide::assets::TextureResolver resolver(root / "cache");
    resolver.setSessionRoots({data});

    const auto preview = resolver.resolveMaterialFile("", "Materials\\Nested.bgsm", "");
    REQUIRE(preview.has_value());
    CHECK(readText(*preview) == "from-nested");

    const auto assets = resolver.assetResolver();
    const auto exported = assets->locate_in_archives("materials/nested.bgsm");
    REQUIRE(exported.has_value());
    const auto bytes = assets->read(*exported);
    CHECK(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()) == "from-nested");
}
