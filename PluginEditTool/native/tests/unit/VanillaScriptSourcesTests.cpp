#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <fo4ide/papyrus/VanillaScriptSources.hpp>

namespace {

using Bytes = std::vector<std::uint8_t>;

void put16(Bytes& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}

void put32(Bytes& out, std::uint32_t value) {
    put16(out, static_cast<std::uint16_t>(value & 0xFFFFu));
    put16(out, static_cast<std::uint16_t>(value >> 16));
}

void putText(Bytes& out, const std::string& text) {
    out.insert(out.end(), text.begin(), text.end());
}

struct ZipEntry {
    std::string name;
    std::uint16_t method;
    Bytes data;
    std::uint32_t uncompressedSize;
};

// Writes a minimal zip; the central directory must match the local records.
Bytes buildZip(const std::vector<ZipEntry>& entries) {
    Bytes out;
    std::vector<std::uint32_t> offsets;
    for (const auto& entry : entries) {
        offsets.push_back(static_cast<std::uint32_t>(out.size()));
        put32(out, 0x04034b50u);
        put16(out, 20);
        put16(out, 0);
        put16(out, entry.method);
        put32(out, 0);
        put32(out, 0);
        put32(out, static_cast<std::uint32_t>(entry.data.size()));
        put32(out, entry.uncompressedSize);
        put16(out, static_cast<std::uint16_t>(entry.name.size()));
        put16(out, 0);
        putText(out, entry.name);
        out.insert(out.end(), entry.data.begin(), entry.data.end());
    }
    const auto centralStart = static_cast<std::uint32_t>(out.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        put32(out, 0x02014b50u);
        put16(out, 20);
        put16(out, 20);
        put16(out, 0);
        put16(out, entry.method);
        put32(out, 0);
        put32(out, 0);
        put32(out, static_cast<std::uint32_t>(entry.data.size()));
        put32(out, entry.uncompressedSize);
        put16(out, static_cast<std::uint16_t>(entry.name.size()));
        put16(out, 0);
        put16(out, 0);
        put16(out, 0);
        put16(out, 0);
        put32(out, 0);
        put32(out, offsets[i]);
        putText(out, entry.name);
    }
    const auto centralSize = static_cast<std::uint32_t>(out.size()) - centralStart;
    put32(out, 0x06054b50u);
    put16(out, 0);
    put16(out, 0);
    put16(out, static_cast<std::uint16_t>(entries.size()));
    put16(out, static_cast<std::uint16_t>(entries.size()));
    put32(out, centralSize);
    put32(out, centralStart);
    put16(out, 0);
    return out;
}

std::filesystem::path writeTemp(const std::string& leaf, const Bytes& bytes) {
    const auto path = std::filesystem::temp_directory_path() / "quest-script-sources-tests" / leaf;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return path;
}

}  // namespace

TEST_CASE("QuestScriptSources.ReadsStoredAndDeflatedPscEntriesOnly") {
    // Raw deflate of one byte 'a' is 4B 04 00; the entry must inflate back to "a".
    const Bytes deflatedA{0x4B, 0x04, 0x00};
    const Bytes stored{'S', 'c', 'r', 'i', 'p', 't'};
    const auto zip = buildZip({
        {"Fragments/QF_One.psc", 0, stored, static_cast<std::uint32_t>(stored.size())},
        {"two.psc", 8, deflatedA, 1},
        {"readme.txt", 0, stored, static_cast<std::uint32_t>(stored.size())},
        {"Folder/", 0, {}, 0},
    });
    const auto path = writeTemp("stored-and-deflated.zip", zip);

    const auto sources = fo4ide::papyrus::readPscZip(path);

    REQUIRE(sources.size() == 2);
    CHECK(sources[0].relativePath.generic_string() == "Fragments/QF_One.psc");
    CHECK(sources[0].text == "Script");
    CHECK(sources[1].relativePath.generic_string() == "two.psc");
    CHECK(sources[1].text == "a");
}

TEST_CASE("QuestScriptSources.RefusesUnsafeZipEntryNames") {
    const Bytes body{'x'};
    const auto zip = buildZip({{"../escape.psc", 0, body, 1}});
    const auto path = writeTemp("unsafe-name.zip", zip);

    CHECK_THROWS_AS(fo4ide::papyrus::readPscZip(path), std::runtime_error);
}

TEST_CASE("QuestScriptSources.RefusesTruncatedZip") {
    const Bytes body{'x'};
    auto zip = buildZip({{"a.psc", 0, body, 1}});
    zip.resize(zip.size() / 2);
    const auto path = writeTemp("truncated.zip", zip);

    CHECK_THROWS_AS(fo4ide::papyrus::readPscZip(path), std::runtime_error);
}

TEST_CASE("QuestScriptSources.DirectoryReadIsRelativeAndSorted") {
    const auto root = std::filesystem::temp_directory_path() / "quest-script-sources-tests" / "dir";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "Sub");
    std::ofstream(root / "Zeta.psc") << "Scriptname Zeta\n";
    std::ofstream(root / "Sub" / "Alpha.psc") << "Scriptname Alpha\n";
    std::ofstream(root / "notes.txt") << "ignored\n";

    const auto sources = fo4ide::papyrus::readPscDirectory(root);

    REQUIRE(sources.size() == 2);
    CHECK(sources[0].relativePath.generic_string() == "Sub/Alpha.psc");
    CHECK(sources[1].relativePath.generic_string() == "Zeta.psc");
    CHECK(fo4ide::papyrus::readPscDirectory(root / "Missing").empty());
}
