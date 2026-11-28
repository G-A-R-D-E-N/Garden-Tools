#include <fo4ide/archive/RadicleArchiveBackend.hpp>

#include "../helpers/TempRoot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/archive/ba2.hpp>
#include <fo4/archive/ba2_hash.hpp>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

namespace fs = std::filesystem;
using fo4::archive::Ba2GeneralEntry;

fs::path tempRoot() {
    return fs::temp_directory_path() / ("fo4ide_archive_oracle_" + fo4ide_test::uniqueTempName());
}

struct Cleanup {
    fs::path root;
    ~Cleanup() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

struct LinkGuard {
    fs::path link;
    ~LinkGuard() {
        std::error_code ec;
        fs::remove(link, ec);
    }
};

std::vector<std::byte> bytes(std::string_view text) {
    std::vector<std::byte> out;
    out.reserve(text.size());
    for (const char ch : text) out.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    return out;
}

std::vector<std::byte> readBytes(const fs::path& path) {
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

void writeText(const fs::path& path, std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
}

Ba2GeneralEntry entry(std::string path, std::string_view payload, bool compress = false) {
    const auto data = bytes(payload);
    return {
        0x11111111u, 0x22222222u, 0x33333333u, 0u, std::move(path),
        {compress ? fo4::archive::make_compressed_ba2_chunk(data)
                  : fo4::archive::make_stored_ba2_chunk(data)}
    };
}

fs::path writeArchive(const fs::path& root, std::string_view name, std::vector<Ba2GeneralEntry> entries) {
    fo4::archive::Ba2GeneralArchive archive;
    archive.entries = std::move(entries);
    const auto path = root / std::string(name);
    fo4::archive::write_ba2_general_file(archive, path);
    return path;
}

nlohmann::json extractAllArgs(const fs::path& archive, const fs::path& output, int limit = 0,
                              bool overwrite = false) {
    return {
        {"archivePath", archive.string()}, {"outputDirectory", output.string()},
        {"filter", ""}, {"limit", limit}, {"filterMode", "simple"}, {"overwrite", overwrite}
    };
}

nlohmann::json extractSelectedArgs(const fs::path& archive, const fs::path& output,
                                   std::initializer_list<std::string> inner, bool overwrite = false) {
    auto paths = nlohmann::json::array();
    for (const auto& path : inner) paths.push_back(path);
    return {
        {"archivePath", archive.string()}, {"outputDirectory", output.string()},
        {"innerPaths", paths}, {"overwrite", overwrite}
    };
}

nlohmann::json listArgs(const fs::path& archive, std::string filter, std::string mode, int limit = 2000) {
    return {
        {"archivePath", archive.string()}, {"filter", std::move(filter)},
        {"limit", limit}, {"filterMode", std::move(mode)}
    };
}

std::vector<std::string> listedPaths(const nlohmann::json& result) {
    std::vector<std::string> out;
    for (const auto& row : result.at("entries")) out.push_back(row.at("path").get<std::string>());
    return out;
}

std::size_t entryCount(const fs::path& directory) {
    std::size_t count = 0;
    for ([[maybe_unused]] const auto& item : fs::recursive_directory_iterator(directory)) ++count;
    return count;
}

void requireRefusedWithoutOutput(const fo4ide::Result<nlohmann::json>& result, const fs::path& root,
                                 const fs::path& output) {
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ArchiveRequestInvalid");
    CHECK_FALSE(fs::exists(output));
    CHECK(entryCount(root) == 1);
}

}

TEST_CASE("Archive oracle rejects unsafe entry paths before writing anything, in extract all and extract selected") {
    const std::vector<std::string> unsafe{
        "..\\escape.txt",
        "safe\\..\\escape.txt",
        "/absolute.txt",
        "\\\\server\\share.txt",
        "C:\\escape.txt",
        "safe\\bad:name.txt",
        "safe\\NUL.txt",
        "safe\\alias. ",
        "safe\\\\double.txt",
        "safe\\ctl\x01name.txt",
    };
    for (const auto& badPath : unsafe) {
        DYNAMIC_SECTION("entry " << badPath) {
            const auto root = tempRoot();
            Cleanup cleanup{root};
            const auto archive = writeArchive(root, "unsafe.ba2", {
                entry("Meshes\\Valid.nif", "valid"), entry(badPath, "payload")});
            const auto output = root / "out";
            fo4ide::archive::RadicleArchiveBackend backend;

            requireRefusedWithoutOutput(backend.invoke("extractAll", extractAllArgs(archive, output)), root, output);
            requireRefusedWithoutOutput(
                backend.invoke("extractSelected",
                               extractSelectedArgs(archive, output, {"Meshes\\Valid.nif", badPath})),
                root, output);
        }
    }
}

TEST_CASE("Archive oracle rejects Windows device names, including console handles and superscript digits") {
    const std::vector<std::string> reserved{
        "safe\\CONIN$.txt", "safe\\CONOUT$", "safe\\conin$.pex",
        "safe\\COM\xC2\xB9.txt", "safe\\com\xC2\xB2", "safe\\LPT\xC2\xB3.nif",
        "safe\\COM1.txt", "safe\\lpt9", "safe\\aux.txt", "safe\\PRN",
    };
    for (const auto& badPath : reserved) {
        DYNAMIC_SECTION("entry " << badPath) {
            const auto root = tempRoot();
            Cleanup cleanup{root};
            const auto archive = writeArchive(root, "device.ba2", {
                entry("Meshes\\Valid.nif", "valid"), entry(badPath, "payload")});
            const auto output = root / "out";
            fo4ide::archive::RadicleArchiveBackend backend;

            requireRefusedWithoutOutput(backend.invoke("extractAll", extractAllArgs(archive, output)), root, output);
            requireRefusedWithoutOutput(
                backend.invoke("extractSelected",
                               extractSelectedArgs(archive, output, {"Meshes\\Valid.nif", badPath})),
                root, output);
        }
    }
}

TEST_CASE("Archive oracle accepts names that only look like device names") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto archive = writeArchive(root, "lookalike.ba2", {
        entry("safe\\console.txt", "a"), entry("safe\\com10.txt", "b"), entry("safe\\conin.txt", "c"),
        entry("safe\\com0.txt", "d"), entry("safe\\lpt.txt", "e")});
    const auto output = root / "out";
    fo4ide::archive::RadicleArchiveBackend backend;

    const auto result = backend.invoke("extractAll", extractAllArgs(archive, output));
    REQUIRE(result.has_value());
    CHECK(readBytes(output / "safe" / "console.txt") == bytes("a"));
    CHECK(readBytes(output / "safe" / "lpt.txt") == bytes("e"));
}

TEST_CASE("Archive oracle rejects an over-long filename component before writing earlier entries") {
    const std::vector<std::pair<std::string, std::string>> rows{
        {"256 ASCII units plus extension", "Meshes\\" + std::string(256, 'a') + ".nif"},
        {"128 two-byte characters, 256 UTF-8 bytes", std::string([] {
            std::string text;
            for (int i = 0; i < 128; ++i) text += "\xC3\xA9";
            return text;
        }())},
    };
    for (const auto& [label, badPath] : rows) {
        DYNAMIC_SECTION(label) {
            const auto root = tempRoot();
            Cleanup cleanup{root};
            const auto archive = writeArchive(root, "long.ba2", {
                entry("Meshes\\Valid.nif", "valid"), entry(badPath, "payload")});
            const auto output = root / "out";
            fo4ide::archive::RadicleArchiveBackend backend;

            requireRefusedWithoutOutput(backend.invoke("extractAll", extractAllArgs(archive, output)), root, output);
            requireRefusedWithoutOutput(
                backend.invoke("extractSelected",
                               extractSelectedArgs(archive, output, {"Meshes\\Valid.nif", badPath})),
                root, output);
        }
    }
}

TEST_CASE("Archive oracle extracts the longest file name that can be staged, 220 bytes") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const std::string name(220, 'a');
    const auto archive = writeArchive(root, "edge.ba2", {entry(name, "payload")});
    auto output = root / "o";
#if defined(_WIN32)
    output = fs::path(L"\\\\?\\" + fs::absolute(output).wstring());
#endif
    fo4ide::archive::RadicleArchiveBackend backend;

    const auto result = backend.invoke("extractAll", extractAllArgs(archive, output));
    const std::string failure = result.has_value() ? std::string() : result.error().message;
    INFO(failure);
    REQUIRE(result.has_value());
    CHECK(readBytes(output / name) == bytes("payload"));
}

TEST_CASE("Archive oracle refuses a file name over the staging limit up front (native delta, legacy extracts up to 255)") {
    for (const std::size_t length : {221u, 255u}) {
        DYNAMIC_SECTION(length << " byte file name") {
            const auto root = tempRoot();
            Cleanup cleanup{root};
            const auto archive = writeArchive(root, "stage.ba2", {
                entry("Meshes\\Valid.nif", "valid"), entry(std::string(length, 'a'), "payload")});
            const auto output = root / "out";
            fo4ide::archive::RadicleArchiveBackend backend;

            const auto all = backend.invoke("extractAll", extractAllArgs(archive, output));
            requireRefusedWithoutOutput(all, root, output);
            CHECK(all.error().message.find("too long to stage") != std::string::npos);
            CHECK(all.error().message.find("limit 220 bytes") != std::string::npos);
            requireRefusedWithoutOutput(
                backend.invoke("extractSelected",
                               extractSelectedArgs(archive, output, {"Meshes\\Valid.nif", std::string(length, 'a')})),
                root, output);
        }
    }
}

TEST_CASE("Archive oracle names the component gate when a 256 byte component is refused") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto archive = writeArchive(root, "edge.ba2", {entry(std::string(256, 'a'), "payload")});
    fo4ide::archive::RadicleArchiveBackend backend;

    const auto result = backend.invoke("extractAll", extractAllArgs(archive, root / "o"));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ArchiveRequestInvalid");
    CHECK(result.error().message.find("unsafe archive entry path component") != std::string::npos);
}

TEST_CASE("Archive oracle folds non-ASCII case when comparing planned output paths") {
    const std::string lowerE = "\xC3\xA9";
    const std::string upperE = "\xC3\x89";
    const std::vector<std::vector<std::string>> rows{
        {"Meshes\\" + lowerE, "Meshes\\" + upperE + "\\x.nif"},
        {"Meshes\\" + lowerE + ".nif", "Meshes\\" + upperE + ".NIF"},
    };
    for (const auto& row : rows) {
        DYNAMIC_SECTION(row[0] << " against " << row[1]) {
            const auto root = tempRoot();
            Cleanup cleanup{root};
            const auto archive = writeArchive(root, "fold.ba2", {
                entry("Other\\Valid.nif", "valid"), entry(row[0], "one"), entry(row[1], "two")});
            const auto output = root / "out";
            fo4ide::archive::RadicleArchiveBackend backend;

            requireRefusedWithoutOutput(backend.invoke("extractAll", extractAllArgs(archive, output)), root, output);
            requireRefusedWithoutOutput(
                backend.invoke("extractSelected",
                               extractSelectedArgs(archive, output, {"Other\\Valid.nif", row[0], row[1]})),
                root, output);
        }
    }
}

TEST_CASE("Archive oracle refuses an entry that is both a file and a directory before writing anything") {
    const std::vector<std::vector<std::string>> orders{
        {"Meshes\\Conflict", "Meshes\\Conflict\\Child.nif"},
        {"Meshes\\Conflict\\Child.nif", "Meshes\\Conflict"},
        {"Meshes\\Conflict", "meshes\\CONFLICT\\Child.nif"},
    };
    for (const auto& order : orders) {
        DYNAMIC_SECTION(order[0] << " then " << order[1]) {
            const auto root = tempRoot();
            Cleanup cleanup{root};
            const auto archive = writeArchive(root, "conflict.ba2", {
                entry("Other\\Valid.nif", "valid"), entry(order[0], "one"), entry(order[1], "two")});
            const auto output = root / "out";
            fo4ide::archive::RadicleArchiveBackend backend;

            requireRefusedWithoutOutput(backend.invoke("extractAll", extractAllArgs(archive, output)), root, output);
            requireRefusedWithoutOutput(
                backend.invoke("extractSelected",
                               extractSelectedArgs(archive, output, {"Other\\Valid.nif", order[0], order[1]})),
                root, output);
        }
    }
}

TEST_CASE("Archive oracle refuses an existing file where the archive needs a directory, before writing anything") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto archive = writeArchive(root, "blocked.ba2", {
        entry("Other\\Valid.nif", "valid"), entry("Meshes\\A.nif", "mesh")});
    const auto output = root / "out";
    writeText(output / "Meshes", "i am a file");
    fo4ide::archive::RadicleArchiveBackend backend;

    for (const bool overwrite : {false, true}) {
        const auto result = backend.invoke("extractAll", extractAllArgs(archive, output, 0, overwrite));
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().type == "ArchiveRequestInvalid");
        CHECK_FALSE(fs::exists(output / "Other"));
        CHECK(readBytes(output / "Meshes") == bytes("i am a file"));
    }
}

TEST_CASE("Archive oracle refuses an existing directory where the archive needs a file, before writing anything") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto archive = writeArchive(root, "blocked.ba2", {entry("B.txt", "bee"), entry("A.txt", "ay")});
    const auto output = root / "out";
    fs::create_directories(output / "A.txt");
    fo4ide::archive::RadicleArchiveBackend backend;

    const auto all = backend.invoke("extractAll", extractAllArgs(archive, output, 0, true));
    REQUIRE_FALSE(all.has_value());
    CHECK(all.error().type == "ArchiveRequestInvalid");
    CHECK_FALSE(fs::exists(output / "B.txt"));

    const auto selected = backend.invoke("extractSelected", extractSelectedArgs(archive, output, {"B.txt", "A.txt"}, true));
    REQUIRE_FALSE(selected.has_value());
    CHECK(selected.error().type == "ArchiveRequestInvalid");
    CHECK_FALSE(fs::exists(output / "B.txt"));
}

TEST_CASE("Archive oracle refuses case-insensitive file collisions before writing") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto archive = writeArchive(root, "collide.ba2", {
        entry("Other\\Valid.nif", "valid"), entry("Meshes\\Weapon.nif", "first"),
        entry("meshes\\WEAPON.NIF", "second")});
    const auto output = root / "out";
    fo4ide::archive::RadicleArchiveBackend backend;

    requireRefusedWithoutOutput(backend.invoke("extractAll", extractAllArgs(archive, output)), root, output);
}

#if defined(_WIN32)
TEST_CASE("Archive oracle merges case-only directory variants into one tree on Windows") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto archive = writeArchive(root, "merge.ba2", {
        entry("Scripts\\Hardcore\\Quest.pex", "pex"), entry("scripts\\Source\\Quest.psc", "psc")});
    const auto output = root / "out";
    fo4ide::archive::RadicleArchiveBackend backend;

    const auto result = backend.invoke("extractAll", extractAllArgs(archive, output));
    REQUIRE(result.has_value());
    std::size_t topLevel = 0;
    for ([[maybe_unused]] const auto& item : fs::directory_iterator(output)) ++topLevel;
    CHECK(topLevel == 1);
    CHECK(readBytes(output / "scripts" / "Hardcore" / "Quest.pex") == bytes("pex"));
    CHECK(readBytes(output / "scripts" / "Source" / "Quest.psc") == bytes("psc"));
}
#endif

TEST_CASE("Archive oracle extract file replaces an existing destination exactly and leaves no staging file") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto archive = writeArchive(root, "replace.ba2", {entry("Meshes\\Thing.nif", "new exact data")});
    const auto output = root / "single" / "Thing.nif";
    writeText(output, "old data");
    fo4ide::archive::RadicleArchiveBackend backend;
    const nlohmann::json args{
        {"archivePath", archive.string()}, {"innerPath", "Meshes\\Thing.nif"},
        {"outputPath", output.string()}, {"overwrite", true}
    };

    const auto result = backend.invoke("extractFile", args);
    REQUIRE(result.has_value());
    CHECK(readBytes(output) == bytes("new exact data"));
    CHECK(entryCount(output.parent_path()) == 1);
}

TEST_CASE("Archive oracle read failure leaves an existing destination untouched and no staging file") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4::archive::Ba2GeneralArchive archiveData;
    Ba2GeneralEntry corrupt = entry("Meshes\\Thing.nif", "x");
    corrupt.chunks[0] = {bytes("not a zlib stream at all"), 100u, true, 0};
    archiveData.entries.push_back(std::move(corrupt));
    const auto archive = root / "corrupt.ba2";
    fo4::archive::write_ba2_general_file(archiveData, archive);
    const auto output = root / "out";
    const auto destination = output / "Meshes" / "Thing.nif";
    writeText(destination, "original bytes must survive");
    fo4ide::archive::RadicleArchiveBackend backend;

    const auto file = backend.invoke("extractFile", {
        {"archivePath", archive.string()}, {"innerPath", "Meshes\\Thing.nif"},
        {"outputPath", destination.string()}, {"overwrite", true}});
    REQUIRE_FALSE(file.has_value());
    CHECK(file.error().type == "ArchiveReadFailed");
    CHECK(readBytes(destination) == bytes("original bytes must survive"));

    const auto selected = backend.invoke("extractSelected", extractSelectedArgs(archive, output, {"Meshes\\Thing.nif"}, true));
    REQUIRE_FALSE(selected.has_value());
    CHECK(selected.error().type == "ArchiveReadFailed");
    CHECK(readBytes(destination) == bytes("original bytes must survive"));

    const auto all = backend.invoke("extractAll", extractAllArgs(archive, output, 0, true));
    REQUIRE_FALSE(all.has_value());
    CHECK(all.error().type == "ArchiveReadFailed");
    CHECK(readBytes(destination) == bytes("original bytes must survive"));
    CHECK(entryCount(output) == 2);
}

TEST_CASE("Archive oracle extract all over the limit truncates with a warning (pinned native, legacy refuses)") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto archive = writeArchive(root, "limit.ba2", {entry("Meshes\\A.txt", "a"), entry("Meshes\\B.txt", "b")});
    const auto output = root / "out";
    fo4ide::archive::RadicleArchiveBackend backend;

    const auto result = backend.invoke("extractAll", extractAllArgs(archive, output, 1));
    REQUIRE(result.has_value());
    const auto text = result->get<std::string>();
    CHECK(text.find("Extracted 1 file(s)") != std::string::npos);
    CHECK(text.find("Warning: 1 more matching entry was not extracted (limit 1)") != std::string::npos);
    CHECK(fs::exists(output / "Meshes" / "A.txt"));
    CHECK_FALSE(fs::exists(output / "Meshes" / "B.txt"));
}

TEST_CASE("Archive oracle extract all does not validate entries past the limit (pinned native)") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto archive = writeArchive(root, "past.ba2", {entry("Meshes\\A.txt", "a"), entry("..\\escape.txt", "x")});
    const auto output = root / "out";
    fo4ide::archive::RadicleArchiveBackend backend;

    const auto result = backend.invoke("extractAll", extractAllArgs(archive, output, 1));
    REQUIRE(result.has_value());
    CHECK(fs::exists(output / "Meshes" / "A.txt"));
    CHECK_FALSE(fs::exists(root / "escape.txt"));
}

TEST_CASE("Archive oracle reports an unknown inner path and a selection with no match") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto archive = writeArchive(root, "lookup.ba2", {entry("Meshes\\Test\\thing.txt", "x")});
    const auto output = root / "out";
    fo4ide::archive::RadicleArchiveBackend backend;

    const auto file = backend.invoke("extractFile", {
        {"archivePath", archive.string()}, {"innerPath", "Meshes\\NotHere.txt"},
        {"outputPath", (root / "NotHere.txt").string()}});
    REQUIRE_FALSE(file.has_value());
    CHECK(file.error().type == "ArchiveRequestInvalid");
    CHECK(file.error().message.find("archive entry not found") != std::string::npos);
    CHECK_FALSE(fs::exists(root / "NotHere.txt"));

    const auto selected = backend.invoke("extractSelected", extractSelectedArgs(archive, output, {"Meshes\\NotHere.txt"}));
    REQUIRE_FALSE(selected.has_value());
    CHECK(selected.error().type == "ArchiveRequestInvalid");
    CHECK(selected.error().message.find("archive entry not found") != std::string::npos);
    CHECK_FALSE(fs::exists(output));

    const auto preview = backend.invoke("previewTexture", {
        {"archivePath", archive.string()}, {"innerPath", "Meshes\\NotHere.dds"}});
    REQUIRE_FALSE(preview.has_value());
    CHECK(preview.error().type == "ArchiveRequestInvalid");
}

TEST_CASE("Archive oracle extract selected writes only the chosen entry whatever the separator or case") {
    for (const std::string selectedPath : {"Meshes\\A.txt", "Meshes/A.txt", "meshes\\a.TXT"}) {
        DYNAMIC_SECTION("selected " << selectedPath) {
            const auto root = tempRoot();
            Cleanup cleanup{root};
            const auto archive = writeArchive(root, "pick.ba2", {
                entry("Meshes\\A.txt", "payload"), entry("Meshes\\B.txt", "skip me"),
                entry("Meshes\\C.txt", "skip me too")});
            const auto output = root / "out";
            fo4ide::archive::RadicleArchiveBackend backend;

            const auto result = backend.invoke("extractSelected", extractSelectedArgs(archive, output, {selectedPath}));
            REQUIRE(result.has_value());
            CHECK(result->get<std::string>().find("Extracted 1 file(s)") != std::string::npos);
            CHECK(readBytes(output / "Meshes" / "A.txt") == bytes("payload"));
            CHECK_FALSE(fs::exists(output / "Meshes" / "B.txt"));
            CHECK_FALSE(fs::exists(output / "Meshes" / "C.txt"));
        }
    }
}

TEST_CASE("Archive oracle extract selected refuses caller leading separators (pinned native, legacy trims them)") {
    for (const std::string selectedPath : {"\\Meshes\\A.txt", "/Meshes/A.txt"}) {
        DYNAMIC_SECTION("selected " << selectedPath) {
            const auto root = tempRoot();
            Cleanup cleanup{root};
            const auto archive = writeArchive(root, "pick.ba2", {
                entry("Meshes\\A.txt", "payload"), entry("Meshes\\B.txt", "skip me")});
            const auto output = root / "out";
            fo4ide::archive::RadicleArchiveBackend backend;

            const auto result = backend.invoke("extractSelected", extractSelectedArgs(archive, output, {selectedPath}));
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error().type == "ArchiveRequestInvalid");
            CHECK(result.error().message.find("archive entry not found") != std::string::npos);
            CHECK_FALSE(fs::exists(output));
        }
    }
}

TEST_CASE("Archive oracle list reports truncation and a missing archive") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto archive = writeArchive(root, "three.ba2", {entry("A.txt", "a"), entry("B.txt", "b"), entry("C.txt", "c")});
    fo4ide::archive::RadicleArchiveBackend backend;

    const auto truncated = backend.invoke("list", listArgs(archive, "", "simple", 2));
    REQUIRE(truncated.has_value());
    CHECK(truncated->at("totalCount") == 3);
    CHECK(truncated->at("shownCount") == 2);
    CHECK(truncated->at("truncated").get<bool>());

    const auto missing = backend.invoke("list", listArgs(root / "does-not-exist.ba2", "", "simple"));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "ArchiveRequestInvalid");
    CHECK(missing.error().message.find("could not open archive") != std::string::npos);

    for (const char* side : {"archiveA", "archiveB"}) {
        nlohmann::json args{{"archiveA", archive.string()}, {"archiveB", archive.string()}};
        args[side] = (root / "does-not-exist.ba2").string();
        const auto compared = backend.invoke("compare", args);
        REQUIRE_FALSE(compared.has_value());
        CHECK(compared.error().type == "ArchiveRequestInvalid");
        CHECK(compared.error().message.find("could not open archive") != std::string::npos);
    }
}

TEST_CASE("Archive oracle list filter modes: wildcard, regex, literal star and unknown mode") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto archive = writeArchive(root, "filters.ba2", {
        entry("Meshes\\gun.nif", "a"), entry("Textures\\gun_d.dds", "b"),
        entry("Meshes\\gun01.nif", "c"), entry("Meshes\\gunXX.nif", "d"),
        entry("Meshes\\gun*special.nif", "e")});
    fo4ide::archive::RadicleArchiveBackend backend;

    const auto wildcard = backend.invoke("list", listArgs(archive, "*.dds", "wildcard"));
    REQUIRE(wildcard.has_value());
    CHECK(listedPaths(*wildcard) == std::vector<std::string>{"Textures\\gun_d.dds"});

    const auto wildcardQuestion = backend.invoke("list", listArgs(archive, "Meshes\\gun0?.nif", "wildcard"));
    REQUIRE(wildcardQuestion.has_value());
    CHECK(listedPaths(*wildcardQuestion) == std::vector<std::string>{"Meshes\\gun01.nif"});

    const auto regex = backend.invoke("list", listArgs(archive, R"(gun\d+\.nif)", "regex"));
    REQUIRE(regex.has_value());
    CHECK(listedPaths(*regex) == std::vector<std::string>{"Meshes\\gun01.nif"});

    const auto literal = backend.invoke("list", listArgs(archive, "gun*special", "simple"));
    REQUIRE(literal.has_value());
    CHECK(listedPaths(*literal) == std::vector<std::string>{"Meshes\\gun*special.nif"});

    const auto unknown = backend.invoke("list", listArgs(archive, "*.nif", "typo"));
    REQUIRE_FALSE(unknown.has_value());
    CHECK(unknown.error().type == "ArchiveRequestInvalid");
    CHECK(unknown.error().message.find("filterMode must be simple, wildcard, or regex") != std::string::npos);
}

TEST_CASE("Archive oracle list refuses the legacy glob alias (pinned native)") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto archive = writeArchive(root, "glob.ba2", {entry("Scripts\\Nested\\Quest.pex", "a")});
    fo4ide::archive::RadicleArchiveBackend backend;

    const auto glob = backend.invoke("list", listArgs(archive, "*.pex", "glob"));
    REQUIRE_FALSE(glob.has_value());
    CHECK(glob.error().type == "ArchiveRequestInvalid");
}

TEST_CASE("Archive oracle extract all honours a wildcard filter") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto archive = writeArchive(root, "wild.ba2", {
        entry("Scripts\\Nested\\Quest.pex", "a"), entry("Scripts\\Nested\\Quest.psc", "b")});
    const auto output = root / "out";
    fo4ide::archive::RadicleArchiveBackend backend;

    auto args = extractAllArgs(archive, output);
    args["filter"] = "*.pex";
    args["filterMode"] = "wildcard";
    REQUIRE(backend.invoke("extractAll", args).has_value());
    CHECK(fs::exists(output / "Scripts" / "Nested" / "Quest.pex"));
    CHECK_FALSE(fs::exists(output / "Scripts" / "Nested" / "Quest.psc"));
}

TEST_CASE("Archive oracle stops a catastrophic regex filter and reports it (legacy RegexFilter_TimeoutIsCaughtAndReported)") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto hostile = std::string(40, 'a') + "!";
    const auto archive = writeArchive(root, "regex.ba2", {entry(hostile, "x"), entry("Meshes\\ok.nif", "y")});
    const auto output = root / "out";
    fo4ide::archive::RadicleArchiveBackend backend;

    const auto started = std::chrono::steady_clock::now();
    const auto listed = backend.invoke("list", listArgs(archive, "(a+)+$", "regex"));
    REQUIRE_FALSE(listed.has_value());
    CHECK(listed.error().type == "ArchiveFilterTimeout");
    CHECK(listed.error().message.find("too long") != std::string::npos);
    CHECK(listed.error().message.find("stopped") != std::string::npos);

    auto args = extractAllArgs(archive, output);
    args["filter"] = "(a+)+$";
    args["filterMode"] = "regex";
    const auto extracted = backend.invoke("extractAll", args);
    REQUIRE_FALSE(extracted.has_value());
    CHECK(extracted.error().type == "ArchiveFilterTimeout");
    CHECK_FALSE(fs::exists(output));
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(10));
}

TEST_CASE("Archive oracle reports a regex that overruns the engine on a very long path as a timeout, not a raw engine error") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto archive = writeArchive(root, "longregex.ba2", {entry(std::string(30000, 'a') + "!", "x")});
    fo4ide::archive::RadicleArchiveBackend backend;

    const auto listed = backend.invoke("list", listArgs(archive, "^(a|aa)+$", "regex"));
    REQUIRE_FALSE(listed.has_value());
    CHECK(listed.error().type == "ArchiveFilterTimeout");
}

#if defined(_WIN32)
TEST_CASE("Archive oracle refuses an entry named like an 8.3 short alias of an existing file, even with overwrite (legacy Plan_RejectsAPathThatResolvesOutsideTheDirectorySnapshot)") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto output = root / "out";
    const auto existing = output / "LongFileNameForAlias.nif";
    writeText(existing, "must survive");
    const auto queryShortName = [&] {
        wchar_t buffer[MAX_PATH];
        const auto length = GetShortPathNameW(existing.c_str(), buffer, MAX_PATH);
        return length == 0 || length >= MAX_PATH ? fs::path{} : fs::path(buffer).filename();
    };
    auto shortName = queryShortName();
    if (shortName.empty() || shortName == existing.filename()) {
        const auto command = "%SystemRoot%\\System32\\fsutil.exe file setshortname \"" + fs::path(existing).make_preferred().string() + "\" LONGFI~1.NIF > NUL 2>&1";
        if (std::system(command.c_str()) == 0) shortName = queryShortName();
    }
    if (shortName.empty() || shortName == existing.filename()) SKIP("no 8.3 short name on this volume and fsutil could not set one");
    const auto archive = writeArchive(root, "alias.ba2", {entry(shortName.string(), "replacement")});
    fo4ide::archive::RadicleArchiveBackend backend;

    for (const bool overwrite : {false, true}) {
        const auto all = backend.invoke("extractAll", extractAllArgs(archive, output, 0, overwrite));
        REQUIRE_FALSE(all.has_value());
        CHECK(all.error().type == "ArchiveRequestInvalid");
        const auto selected = backend.invoke(
            "extractSelected", extractSelectedArgs(archive, output, {shortName.string()}, overwrite));
        REQUIRE_FALSE(selected.has_value());
        CHECK(selected.error().type == "ArchiveRequestInvalid");
        CHECK(readBytes(existing) == bytes("must survive"));
    }
    CHECK(entryCount(output) == 1);
}
#endif

namespace {

enum class LinkKind { Symlink, Junction };

bool makeDirectoryLink(LinkKind kind, const fs::path& link, const fs::path& target) {
    std::error_code ec;
    if (kind == LinkKind::Symlink) {
        fs::create_directory_symlink(target, link, ec);
        return !ec;
    }
#if defined(_WIN32)
    const auto command = "mklink /J \"" + link.string() + "\" \"" + target.string() + "\" > NUL 2>&1";
    return std::system(command.c_str()) == 0 && fs::exists(link);
#else
    return false;
#endif
}

const char* linkName(LinkKind kind) { return kind == LinkKind::Symlink ? "symlink" : "junction"; }

void requireLink(LinkKind kind, const fs::path& link, const fs::path& target) {
    if (makeDirectoryLink(kind, link, target)) return;
#if defined(_WIN32)
    if (kind == LinkKind::Junction) FAIL("cannot create a junction");
#endif
    SKIP("cannot create a " << linkName(kind));
}

void linkInsideRoot(LinkKind kind) {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto archive = writeArchive(root, "link.ba2", {entry("Meshes\\Escape.nif", "payload")});
    const auto outside = root / "outside";
    const auto output = root / "out";
    fs::create_directories(outside);
    fs::create_directories(output);
    LinkGuard guard{output / "Meshes"};
    requireLink(kind, output / "Meshes", outside);
    fo4ide::archive::RadicleArchiveBackend backend;

    const auto all = backend.invoke("extractAll", extractAllArgs(archive, output));
    REQUIRE_FALSE(all.has_value());
    CHECK(all.error().type == "ArchiveRequestInvalid");
    CHECK(all.error().message.find("pass the resolved target path") != std::string::npos);
    const auto selected = backend.invoke("extractSelected", extractSelectedArgs(archive, output, {"Meshes\\Escape.nif"}));
    REQUIRE_FALSE(selected.has_value());
    CHECK(selected.error().type == "ArchiveRequestInvalid");
    CHECK_FALSE(fs::exists(outside / "Escape.nif"));
}

void linkAboveNewRoot(LinkKind kind) {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto archive = writeArchive(root, "link.ba2", {entry("Meshes\\Escape.nif", "payload")});
    const auto outside = root / "outside";
    const auto linked = root / "linked-parent";
    fs::create_directories(outside);
    LinkGuard guard{linked};
    requireLink(kind, linked, outside);
    fo4ide::archive::RadicleArchiveBackend backend;

    const auto all = backend.invoke("extractAll", extractAllArgs(archive, linked / "new-root"));
    REQUIRE_FALSE(all.has_value());
    CHECK(all.error().type == "ArchiveRequestInvalid");
    CHECK_FALSE(fs::exists(outside / "new-root"));
    CHECK_FALSE(fs::exists(outside / "new-root" / "Meshes" / "Escape.nif"));
}

void linkAboveExplicitFile(LinkKind kind) {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto archive = writeArchive(root, "link.ba2", {entry("Meshes\\Escape.nif", "payload")});
    const auto outside = root / "explicit-outside";
    const auto linked = root / "explicit-link";
    fs::create_directories(outside);
    LinkGuard guard{linked};
    requireLink(kind, linked, outside);
    fo4ide::archive::RadicleArchiveBackend backend;

    const auto file = backend.invoke("extractFile", {
        {"archivePath", archive.string()}, {"innerPath", "Meshes\\Escape.nif"},
        {"outputPath", (linked / "nested" / "Thing.nif").string()}});
    REQUIRE_FALSE(file.has_value());
    CHECK(file.error().type == "ArchiveRequestInvalid");
    CHECK_FALSE(fs::exists(outside / "nested"));
}

void linkAtDestinationSlot(LinkKind kind) {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto archive = writeArchive(root, "slot.ba2", {entry("B.txt", "bee"), entry("A.txt", "ay")});
    const auto outside = root / "slot-outside";
    const auto output = root / "out";
    fs::create_directories(outside);
    fs::create_directories(output);
    LinkGuard guard{output / "A.txt"};
    requireLink(kind, output / "A.txt", outside);
    fo4ide::archive::RadicleArchiveBackend backend;

    const auto all = backend.invoke("extractAll", extractAllArgs(archive, output, 0, true));
    REQUIRE_FALSE(all.has_value());
    CHECK(all.error().type == "ArchiveRequestInvalid");
    CHECK_FALSE(fs::exists(output / "B.txt"));
    const auto selected = backend.invoke("extractSelected", extractSelectedArgs(archive, output, {"B.txt", "A.txt"}, true));
    REQUIRE_FALSE(selected.has_value());
    CHECK(selected.error().type == "ArchiveRequestInvalid");
    CHECK_FALSE(fs::exists(output / "B.txt"));
    const auto file = backend.invoke("extractFile", {
        {"archivePath", archive.string()}, {"innerPath", "A.txt"},
        {"outputPath", (output / "A.txt").string()}, {"overwrite", true}});
    REQUIRE_FALSE(file.has_value());
    CHECK(file.error().type == "ArchiveRequestInvalid");
    CHECK(fs::is_directory(outside));
    CHECK(fs::is_empty(outside));
    CHECK(fs::exists(fs::symlink_status(output / "A.txt")));
}

}

TEST_CASE("Archive oracle refuses to extract through a junction") {
    SECTION("inside the extraction root") { linkInsideRoot(LinkKind::Junction); }
    SECTION("as an ancestor of a new extraction root") { linkAboveNewRoot(LinkKind::Junction); }
    SECTION("as an ancestor of an explicit extract file output") { linkAboveExplicitFile(LinkKind::Junction); }
    SECTION("at a destination file slot") { linkAtDestinationSlot(LinkKind::Junction); }
}

TEST_CASE("Archive oracle refuses to extract through a symlink") {
    SECTION("inside the extraction root") { linkInsideRoot(LinkKind::Symlink); }
    SECTION("as an ancestor of a new extraction root") { linkAboveNewRoot(LinkKind::Symlink); }
    SECTION("as an ancestor of an explicit extract file output") { linkAboveExplicitFile(LinkKind::Symlink); }
    SECTION("at a destination file slot") { linkAtDestinationSlot(LinkKind::Symlink); }
}

TEST_CASE("Archive oracle refuses an explicit extract onto a symlinked output file") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto archive = writeArchive(root, "link.ba2", {entry("A.txt", "payload")});
    const auto victim = root / "victim.txt";
    writeText(victim, "innocent");
    const auto link = root / "out" / "A.txt";
    fs::create_directories(link.parent_path());
    std::error_code ec;
    fs::create_symlink(victim, link, ec);
    if (ec) SKIP("cannot create a file symlink: " << ec.message());
    fo4ide::archive::RadicleArchiveBackend backend;

    const auto result = backend.invoke("extractFile", {
        {"archivePath", archive.string()}, {"innerPath", "A.txt"},
        {"outputPath", link.string()}, {"overwrite", true}});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "ArchiveRequestInvalid");
    CHECK(fs::is_symlink(link));
    CHECK(readBytes(victim) == bytes("innocent"));
}

TEST_CASE("Archive oracle BA2 hashes match the values of real archive entries") {
    struct Row { const char* path; std::uint32_t name; std::uint32_t directory; std::uint32_t extension; };
    const Row rows[] = {
        {"Meshes\\Weapons\\HandMade\\Muzzles\\HandMadeMuzzleParentObject.nif", 0x9B990F90u, 0xFF78256Eu, 0x0066696Eu},
        {"Meshes\\Weapons\\HandMade\\Grips\\HandMadePistolGrip.nif", 0xBB6215A0u, 0xF6CEE42Au, 0x0066696Eu},
        {"Meshes\\Weapons\\HandMade\\Scopes\\PipeRifleIronSights.nif", 0x342D080Eu, 0xEE753300u, 0x0066696Eu},
    };
    for (const auto& row : rows) {
        const auto hashes = fo4::archive::compute_ba2_hashes(row.path);
        CHECK(hashes.name == row.name);
        CHECK(hashes.directory == row.directory);
        CHECK(hashes.extension == row.extension);
    }
    const auto slashes = fo4::archive::compute_ba2_hashes("Strings/DLCCoast_cn.DLSTRINGS");
    CHECK(slashes.name == 0x771F4BECu);
    CHECK(slashes.directory == 0x29F6B58Bu);
}

TEST_CASE("Archive oracle writer stores incompressible data raw") {
    std::vector<std::byte> noise(8192);
    std::uint32_t state = 12345u;
    for (auto& value : noise) {
        state = state * 1664525u + 1013904223u;
        value = static_cast<std::byte>(state >> 24u);
    }
    const auto chunk = fo4::archive::make_compressed_ba2_chunk(noise);
    CHECK_FALSE(chunk.compressed);
    CHECK(chunk.data == noise);
    CHECK(chunk.decompressed_size == noise.size());

    Ba2GeneralEntry stored{1u, 2u, 3u, 0u, "Meshes\\Noise.bin", {chunk}};
    CHECK(fo4::archive::extract_ba2_general_entry(stored) == noise);
}

TEST_CASE("Archive oracle writer round trips an empty file and extracts a zero byte output") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto archive = writeArchive(root, "empty.ba2", {
        entry("Meshes\\Empty.txt", "", true), entry("Meshes\\After.txt", "after")});

    const auto read = fo4::archive::read_ba2_general_file(archive);
    REQUIRE(read.entries.size() == 2);
    REQUIRE(read.entries[0].chunks.size() == 1);
    CHECK(read.entries[0].chunks[0].decompressed_size == 0u);
    CHECK_FALSE(read.entries[0].chunks[0].compressed);
    CHECK(fo4::archive::extract_ba2_general_entry(read.entries[0]).empty());
    CHECK(fo4::archive::extract_ba2_general_entry(read.entries[1]) == bytes("after"));

    const auto output = root / "out";
    fo4ide::archive::RadicleArchiveBackend backend;
    REQUIRE(backend.invoke("extractAll", extractAllArgs(archive, output)).has_value());
    CHECK(fs::file_size(output / "Meshes" / "Empty.txt") == 0u);
    CHECK(readBytes(output / "Meshes" / "After.txt") == bytes("after"));
}

namespace {

void putU16(std::vector<std::byte>& out, std::uint16_t value) {
    out.push_back(static_cast<std::byte>(value & 0xFFu));
    out.push_back(static_cast<std::byte>(value >> 8u));
}

void putU32(std::vector<std::byte>& out, std::uint32_t value) {
    putU16(out, static_cast<std::uint16_t>(value & 0xFFFFu));
    putU16(out, static_cast<std::uint16_t>(value >> 16u));
}

void putU64(std::vector<std::byte>& out, std::uint64_t value) {
    putU32(out, static_cast<std::uint32_t>(value & 0xFFFFFFFFu));
    putU32(out, static_cast<std::uint32_t>(value >> 32u));
}

struct RawChunk {
    std::vector<std::byte> data;
    std::uint32_t packed;
    std::uint32_t unpacked;
};

struct RawEntry {
    std::uint32_t name, ext, dir;
    std::uint8_t unknown;
    std::vector<RawChunk> chunks;
    std::string storedName;
};

std::vector<std::byte> encodeRawGnrl(std::uint32_t version, const std::vector<RawEntry>& entries, bool stringTable) {
    std::size_t tableSize = 0;
    for (const auto& item : entries) tableSize += 16 + 20 * item.chunks.size();
    std::uint64_t cursor = 24 + tableSize;
    std::vector<std::byte> out;
    for (const char ch : std::string_view("BTDX")) out.push_back(static_cast<std::byte>(ch));
    putU32(out, version);
    for (const char ch : std::string_view("GNRL")) out.push_back(static_cast<std::byte>(ch));
    putU32(out, static_cast<std::uint32_t>(entries.size()));
    std::uint64_t dataSize = 0;
    for (const auto& item : entries)
        for (const auto& chunk : item.chunks) dataSize += chunk.data.size();
    putU64(out, stringTable ? cursor + dataSize : 0u);
    for (const auto& item : entries) {
        putU32(out, item.name);
        putU32(out, item.ext);
        putU32(out, item.dir);
        out.push_back(static_cast<std::byte>(item.unknown));
        out.push_back(static_cast<std::byte>(item.chunks.size()));
        putU16(out, 0x10);
        for (const auto& chunk : item.chunks) {
            putU64(out, cursor);
            putU32(out, chunk.packed);
            putU32(out, chunk.unpacked);
            putU32(out, 0xBAADF00Du);
            cursor += chunk.data.size();
        }
    }
    for (const auto& item : entries)
        for (const auto& chunk : item.chunks) out.insert(out.end(), chunk.data.begin(), chunk.data.end());
    if (stringTable) {
        for (const auto& item : entries) {
            putU16(out, static_cast<std::uint16_t>(item.storedName.size()));
            for (const char ch : item.storedName) out.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
        }
    }
    return out;
}

RawChunk storedChunk(std::string_view text) {
    const auto data = bytes(text);
    return {data, 0u, static_cast<std::uint32_t>(data.size())};
}

RawChunk packedChunk(std::string_view text) {
    const auto chunk = fo4::archive::make_compressed_ba2_chunk(bytes(text));
    REQUIRE(chunk.compressed);
    return {chunk.data, static_cast<std::uint32_t>(chunk.data.size()), chunk.decompressed_size};
}

}

TEST_CASE("Archive oracle rewrites a hand built archive byte for byte, including a non UTF-8 name") {
    const std::string longText(4096, 'A');
    const std::vector<RawEntry> entries{
        {0x11111111u, 0x00666E69u, 0x22222222u, 0u, {storedChunk("stored payload")}, "Meshes\\Stored.nif"},
        {0x33333333u, 0x00786570u, 0x44444444u, 0u, {packedChunk(longText)}, "Scripts\\Compressed.pex"},
        {0x55555555u, 0x00786570u, 0x66666666u, 0x5Au,
         {storedChunk("first half "), packedChunk(longText), storedChunk("tail")}, "Multi\\Chunks.bin"},
        {0x77777777u, 0x00667A75u, 0x88888888u, 0u, {storedChunk("voice")},
         std::string("Sound\\Voice\\Fallout4.esm\\RobotMrHandy\\Mar\xED" "a_F.fuz")},
        {0x99999999u, 0x00747874u, 0xAAAAAAAAu, 0u, {storedChunk("")}, "Meshes\\Empty.txt"},
    };

    for (const std::uint32_t version : {1u, 7u, 8u}) {
        for (const bool stringTable : {true, false}) {
            DYNAMIC_SECTION("version " << version << (stringTable ? " with" : " without") << " string table") {
                const auto original = encodeRawGnrl(version, entries, stringTable);
                const auto archive = fo4::archive::read_ba2_general(original);
                CHECK(archive.has_string_table == stringTable);
                REQUIRE(archive.entries.size() == entries.size());
                if (stringTable) {
                    CHECK(archive.entries[3].path == "Sound\\Voice\\Fallout4.esm\\RobotMrHandy\\Mar\xC3\xAD" "a_F.fuz");
                    CHECK(fo4::archive::extract_ba2_general_entry(archive.entries[2]) ==
                          bytes("first half " + longText + "tail"));
                }
                const auto rewritten = fo4::archive::write_ba2_general(archive);
                REQUIRE(rewritten.size() == original.size());
                const auto mismatch = std::mismatch(rewritten.begin(), rewritten.end(), original.begin());
                INFO("first difference at byte " << (mismatch.first - rewritten.begin()));
                CHECK(rewritten == original);
            }
        }
    }
}
