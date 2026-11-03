#include <chrono>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path seqTempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_seq_" + std::to_string(id));
}

struct SeqCleanup {
    std::filesystem::path root;
    ~SeqCleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void seqWriteBytes(const std::filesystem::path& path,
                   const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

void writeQuestData(fo4::test::ByteWriter& out, std::uint16_t flags) {
    fo4::test::ByteWriter data;
    data.u16(flags);
    data.u8(50);
    data.u8(0);
    data.f32(0.0f);
    data.i32(0);
    REQUIRE(data.bytes.size() == 12);
    fo4::test::write_subrecord(out, "DNAM", data.bytes);
}

void writeHeader(fo4::test::ByteWriter& out,
                 std::uint32_t nextFormId,
                 std::initializer_list<std::string_view> masters = {}) {
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(nextFormId);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    for (const auto master : masters) {
        std::vector<std::byte> name;
        for (const char ch : master) name.push_back(std::byte{static_cast<std::uint8_t>(ch)});
        name.push_back(std::byte{0});
        fo4::test::write_subrecord(out, "MAST", name);
        fo4::test::ByteWriter data;
        data.u32(0);
        data.u32(0);
        fo4::test::write_subrecord(out, "DATA", data.bytes);
    }
    fo4::test::end_record(out, tes4);
}

std::vector<std::byte> baseQuestBytes(std::uint16_t flags = 0x0001u) {
    fo4::test::ByteWriter out;
    writeHeader(out, 0x801);
    const auto group = fo4::test::begin_group(out, "QUST", 0);
    const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x00000800u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"AlreadyEnabled\0", 15});
    writeQuestData(out, flags);
    fo4::test::end_record(out, quest);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> patchQuestBytes() {
    fo4::test::ByteWriter out;
    writeHeader(out, 0x802, {"Base.esm"});
    const auto group = fo4::test::begin_group(out, "QUST", 0);

    // Override the already-enabled Base quest. This must not be emitted into
    // the SEQ file because the previous load-order version was already enabled.
    const auto overrideQuest = fo4::test::begin_record(out, "QUST", 0, 0x00000800u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"AlreadyEnabled\0", 15});
    writeQuestData(out, 0x0001u);
    fo4::test::end_record(out, overrideQuest);

    // A local Patch.esp quest. One master means its raw on-disk FormID uses
    // high byte 1, which is exactly what the SEQ entry must contain.
    const auto localQuest = fo4::test::begin_record(out, "QUST", 0, 0x01000801u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"NewStartQuest\0", 14});
    writeQuestData(out, 0x0001u);
    fo4::test::end_record(out, localQuest);

    fo4::test::end_group(out, group);
    return out.bytes;
}

nlohmann::json seqPluginEntry(std::string name,
                              const std::filesystem::path& path) {
    return {
        {"name", std::move(name)},
        {"path", path.string()},
        {"source", "SeqTest"},
        {"resolved", true}
    };
}

}  // namespace

TEST_CASE("Native CreateSeqFile emits only newly start-game-enabled quests") {
    const auto root = seqTempRoot();
    SeqCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    seqWriteBytes(basePath, baseQuestBytes());
    seqWriteBytes(patchPath, patchQuestBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    const auto loaded = backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{
            "plugins",
            nlohmann::json::array({
                seqPluginEntry("Base.esm", basePath),
                seqPluginEntry("Patch.esp", patchPath)
            })
        }});
    REQUIRE(loaded.has_value());

    const auto result = backend.createSeqFile(
        nlohmann::json::array({"Patch.esp", ""}));
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    CHECK(result->get<std::string>().find("with 1 start-game-enabled quest(s)") != std::string::npos);
    CHECK(result->get<std::string>().find("NewStartQuest") != std::string::npos);
    CHECK(result->get<std::string>().find("AlreadyEnabled [") == std::string::npos);

    const auto seqPath = root / "Seq" / "Patch.seq";
    REQUIRE(std::filesystem::exists(seqPath));
    REQUIRE(std::filesystem::file_size(seqPath) == 4);

    std::ifstream stream(seqPath, std::ios::binary);
    REQUIRE(stream.good());
    std::uint32_t value = 0;
    stream.read(reinterpret_cast<char*>(&value), sizeof(value));
    REQUIRE(stream.good());
    CHECK(value == 0x01000801u);
}

// Detects: enabling a master quest in an override must encode that master's index rather than the patch's local index.
TEST_CASE("Native CreateSeqFile retains the origin index for a newly enabled override") {
    const auto root = seqTempRoot(); SeqCleanup cleanup{root};
    const auto base = root / "Base.esm", plugin = root / "Patch.esp";
    seqWriteBytes(base, baseQuestBytes(0)); seqWriteBytes(plugin, patchQuestBytes());
    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({
        seqPluginEntry("Base.esm", base), seqPluginEntry("Patch.esp", plugin)})}}).has_value());
    REQUIRE(backend.createSeqFile(nlohmann::json::array({"Patch.esp", ""})).has_value());
    const auto seq = root / "Seq" / "Patch.seq";
    REQUIRE(std::filesystem::file_size(seq) == 8);
    std::ifstream stream(seq, std::ios::binary);
    std::array<unsigned char, 8> actual{}; stream.read(reinterpret_cast<char*>(actual.data()), actual.size());
    REQUIRE(stream.good());
    const std::array<unsigned char, 8> expected{0x00, 0x08, 0, 0, 0x01, 0x08, 0, 1};
    CHECK(actual == expected);
}

// Detects: multiple start-enabled quests with distinct other flag bits must retain record order and the plugin's master index in SEQ output.
TEST_CASE("Native CreateSeqFile preserves multiple start-enabled quest IDs") {
    const auto root = seqTempRoot(); SeqCleanup cleanup{root};
    const auto base = root / "Base.esm", plugin = root / "Patch.esp";
    seqWriteBytes(base, baseQuestBytes());
    fo4::test::ByteWriter out; writeHeader(out, 0x26D9, {"Base.esm"});
    const auto group = fo4::test::begin_group(out, "QUST", 0);
    for (const auto [id, flags] : std::array<std::pair<std::uint32_t, std::uint16_t>, 3>{{
            {0x01001733u, 0x0111u}, {0x010026D7u, 0x0001u}, {0x010026D8u, 0x0101u}}}) {
        const auto quest = fo4::test::begin_record(out, "QUST", 0, id, 131);
        writeQuestData(out, flags); fo4::test::end_record(out, quest);
    }
    fo4::test::end_group(out, group); seqWriteBytes(plugin, out.bytes);
    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{"plugins", nlohmann::json::array({
        seqPluginEntry("Base.esm", base), seqPluginEntry("Patch.esp", plugin)})}}).has_value());
    REQUIRE(backend.createSeqFile(nlohmann::json::array({"Patch.esp", ""})).has_value());
    const auto seq = root / "Seq" / "Patch.seq";
    REQUIRE(std::filesystem::file_size(seq) == 12);
    std::ifstream stream(seq, std::ios::binary);
    std::array<unsigned char, 12> actual{}; stream.read(reinterpret_cast<char*>(actual.data()), actual.size());
    REQUIRE(stream.good());
    const std::array<unsigned char, 12> expected{0x33, 0x17, 0, 1, 0xD7, 0x26, 0, 1, 0xD8, 0x26, 0, 1};
    CHECK(actual == expected);
}

TEST_CASE("Native CreateSeqFile reports a plugin folder with non-ANSI characters") {
    const auto root = seqTempRoot() / std::filesystem::path(std::u16string{0x041C, 0x043E, 0x0434, 0x044B});
    SeqCleanup cleanup{root.parent_path()};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    seqWriteBytes(basePath, baseQuestBytes());
    seqWriteBytes(patchPath, patchQuestBytes());
    const auto utf8 = [](const std::filesystem::path& path) {
        const auto text = path.u8string();
        return std::string(text.begin(), text.end());
    };

    fo4ide::plugin::RadiclePluginBackend backend;
    const auto loaded = backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{
            "plugins",
            nlohmann::json::array({
                {{"name", "Base.esm"}, {"path", utf8(basePath)}, {"source", "SeqTest"}, {"resolved", true}},
                {{"name", "Patch.esp"}, {"path", utf8(patchPath)}, {"source", "SeqTest"}, {"resolved", true}}
            })
        }});
    REQUIRE(loaded.has_value());

    const auto result = backend.createSeqFile(nlohmann::json::array({"Patch.esp", ""}));
    REQUIRE(result.has_value());
    CHECK(result->get<std::string>().find(utf8(std::filesystem::weakly_canonical(root))) != std::string::npos);
    CHECK(std::filesystem::file_size(root / "Seq" / "Patch.seq") == 4);
}
