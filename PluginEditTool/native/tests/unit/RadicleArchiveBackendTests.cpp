#include <fo4ide/archive/ArchiveService.hpp>
#include <fo4ide/archive/RadicleArchiveBackend.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/archive/ba2.hpp>

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_radicle_archive_" + std::to_string(id));
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

std::vector<std::byte> bytes(std::string_view text) {
    std::vector<std::byte> out;
    out.reserve(text.size());
    for (const char ch : text) {
        out.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
    return out;
}

std::vector<std::byte> readBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    REQUIRE(stream.good());
    const auto end = stream.tellg();
    REQUIRE(end >= 0);
    std::vector<std::byte> out(static_cast<std::size_t>(end));
    stream.seekg(0, std::ios::beg);
    if (!out.empty()) {
        stream.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
        REQUIRE(stream.good());
    }
    return out;
}

fo4::archive::Ba2GeneralEntry entry(std::string path, std::string_view payload,
                                    bool compress = false) {
    const auto data = bytes(payload);
    return {
        0x11111111u,
        0x22222222u,
        0x33333333u,
        0u,
        std::move(path),
        {compress ? fo4::archive::make_compressed_ba2_chunk(data)
                  : fo4::archive::make_stored_ba2_chunk(data)}
    };
}

std::filesystem::path writeArchive(
    const std::filesystem::path& root,
    std::string_view name,
    std::vector<fo4::archive::Ba2GeneralEntry> entries) {
    fo4::archive::Ba2GeneralArchive archive;
    archive.entries = std::move(entries);
    const auto path = root / std::string(name);
    fo4::archive::write_ba2_general_file(archive, path);
    return path;
}

nlohmann::json listArguments(const std::filesystem::path& path,
                             std::string filter = {},
                             std::string mode = "simple") {
    return {
        {"archivePath", path.string()},
        {"filter", std::move(filter)},
        {"limit", 2000},
        {"filterMode", std::move(mode)}
    };
}

}

TEST_CASE("Radicle archive backend lists GNRL entries using the existing UI contract") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = writeArchive(root, "test.ba2", {
        entry("Meshes\\Weapons\\One.nif", "mesh"),
        entry("Scripts\\Quest.pex", std::string(2048, 'A'), true),
        entry("Textures\\Ignored.dds", "texture")
    });

    fo4ide::archive::RadicleArchiveBackend backend;
    const auto result = backend.invoke("list", listArguments(path, "scripts"));
    REQUIRE(result.has_value());
    CHECK(result->at("archiveName") == "test.ba2");
    CHECK(result->at("totalCount") == 3);
    CHECK(result->at("shownCount") == 1);
    CHECK_FALSE(result->at("truncated").get<bool>());
    REQUIRE(result->at("entries").size() == 1);
    CHECK(result->at("entries")[0].at("path") == "Scripts\\Quest.pex");
    CHECK(result->at("entries")[0].at("size") == 2048);
}

TEST_CASE("Null archive backend is genuinely unavailable") {
    fo4ide::archive::NullArchiveBackend backend;
    CHECK_FALSE(backend.available());
    const auto result = backend.invoke("list", nlohmann::json::array());
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ArchiveBackendUnavailable");
}

TEST_CASE("Radicle archive backend extracts selected entries under the chosen root") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = writeArchive(root, "extract.ba2", {
        entry("Meshes\\Sub\\Thing.nif", "mesh payload"),
        entry("Scripts\\Quest.pex", "script payload")
    });
    const auto output = root / "out";

    fo4ide::archive::RadicleArchiveBackend backend;
    const nlohmann::json args{
        {"archivePath", path.string()},
        {"innerPaths", nlohmann::json::array({"Meshes\\Sub\\Thing.nif", "Scripts\\Quest.pex"})},
        {"outputDirectory", output.string()}
    };
    const auto result = backend.invoke("extractSelected", args);
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().starts_with("Extracted 2"));
    CHECK(readBytes(output / "Meshes" / "Sub" / "Thing.nif") == bytes("mesh payload"));
    CHECK(readBytes(output / "Scripts" / "Quest.pex") == bytes("script payload"));
}

TEST_CASE("Radicle archive backend rejects traversal before writing extraction output") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = writeArchive(root, "unsafe.ba2", {
        entry("..\\escape.txt", "escape"),
        entry("safe\\good.txt", "good")
    });
    const auto output = root / "out";

    fo4ide::archive::RadicleArchiveBackend backend;
    const nlohmann::json args{
        {"archivePath", path.string()},
        {"outputDirectory", output.string()},
        {"filter", ""},
        {"limit", 5000},
        {"filterMode", "simple"}
    };
    const auto result = backend.invoke("extractAll", args);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ArchiveRequestInvalid");
    CHECK_FALSE(std::filesystem::exists(root / "escape.txt"));
    CHECK_FALSE(std::filesystem::exists(output / "safe" / "good.txt"));
}

TEST_CASE("Radicle archive backend reports invalid regex filters") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = writeArchive(root, "regex.ba2", {entry("A.txt", "one")});

    fo4ide::archive::RadicleArchiveBackend backend;
    const auto result = backend.invoke("list", listArguments(path, "[", "regex"));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ArchiveFilterInvalid");
}

TEST_CASE("Radicle archive backend compares content rather than only names and sizes") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto a = writeArchive(root, "a.ba2", {
        entry("Same.txt", "same"),
        entry("Changed.txt", "aaaa"),
        entry("Removed.txt", "removed")
    });
    const auto b = writeArchive(root, "b.ba2", {
        entry("same.TXT", "same"),
        entry("Changed.txt", "bbbb"),
        entry("Added.txt", "added")
    });

    fo4ide::archive::RadicleArchiveBackend backend;
    const auto result = backend.invoke("compare", {
        {"archiveA", a.string()},
        {"archiveB", b.string()}
    });
    REQUIRE(result.has_value());
    CHECK(result->at("identicalCount") == 1);
    CHECK(result->at("added") == nlohmann::json::array({"Added.txt"}));
    CHECK(result->at("removed") == nlohmann::json::array({"Removed.txt"}));
    CHECK(result->at("changed") == nlohmann::json::array({"Changed.txt"}));
}
