#include <chrono>
#include "../helpers/TempRoot.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_nested_formlink_list_" + id);
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

std::vector<std::byte> pluginBytes() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(4);
    hedr.u32(0x804);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto keywords = fo4::test::begin_group(out, "KYWD", 0);
    auto keyword = fo4::test::begin_record(out, "KYWD", 0, 0x00000800, 131);
    fo4::test::end_record(out, keyword);
    keyword = fo4::test::begin_record(out, "KYWD", 0, 0x00000801, 131);
    fo4::test::end_record(out, keyword);
    fo4::test::end_group(out, keywords);

    const auto colors = fo4::test::begin_group(out, "CLFM", 0);
    const auto color = fo4::test::begin_record(out, "CLFM", 0, 0x00000802, 131);
    fo4::test::end_record(out, color);
    fo4::test::end_group(out, colors);

    const auto races = fo4::test::begin_group(out, "RACE", 0);
    const auto race = fo4::test::begin_record(out, "RACE", 0, 0x00000803, 131);
    fo4::test::ByteWriter link;
    link.u32(0x00000800);

    fo4::test::write_subrecord(out, "SAKD", link.bytes);
    const std::vector<std::byte> graph0{
        std::byte{static_cast<unsigned char>('g')},
        std::byte{static_cast<unsigned char>('0')},
        std::byte{0}};
    fo4::test::write_subrecord(out, "SGNM", graph0);
    fo4::test::write_subrecord(out, "STKD", link.bytes);
    fo4::test::write_subrecord(out, "SRAF", std::vector<std::byte>(4, std::byte{0}));

    fo4::test::write_subrecord(out, "SAKD", link.bytes);
    fo4::test::write_subrecord(out, "SAKD", link.bytes);
    const std::vector<std::byte> graph1{
        std::byte{static_cast<unsigned char>('g')},
        std::byte{static_cast<unsigned char>('1')},
        std::byte{0}};
    fo4::test::write_subrecord(out, "SGNM", graph1);
    fo4::test::write_subrecord(out, "SRAF", std::vector<std::byte>(4, std::byte{0}));

    fo4::test::end_record(out, race);
    fo4::test::end_group(out, races);
    return out.bytes;
}

void writeBytes(const std::filesystem::path& path,
                const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

std::vector<std::byte> readBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    REQUIRE(stream.good());
    const auto size = stream.tellg();
    REQUIRE(size >= 0);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
        REQUIRE(stream.good());
    }
    return bytes;
}

void load(fo4ide::plugin::RadiclePluginBackend& backend,
          const std::filesystem::path& path) {
    const auto result = backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({{
            {"name", "Patch.esp"},
            {"path", path.string()},
            {"source", "NestedFormLinkListTest"},
            {"resolved", true}
        }})}});
    REQUIRE(result.has_value());
}

std::uint32_t nthRawInGroup(const fo4::Record& record,
                            std::string_view signature,
                            std::size_t group,
                            std::size_t wanted) {
    const auto sig = fo4::Signature::from_string(signature);
    const auto terminator = fo4::Signature::from_string("SRAF");
    std::size_t currentGroup = 0;
    std::size_t ordinal = 0;
    const fo4::Subrecord* found = nullptr;
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature == terminator) {
            if (currentGroup == group) break;
            ++currentGroup;
            ordinal = 0;
            continue;
        }
        if (currentGroup != group || subrecord.signature != sig) continue;
        if (ordinal++ != wanted) continue;
        found = &subrecord;
        break;
    }
    if (found == nullptr) FAIL("missing requested nested subrecord ordinal");
    REQUIRE(found->data.size() == 4u);
    std::uint32_t raw = 0;
    std::memcpy(&raw, found->data.data(), sizeof(raw));
    return raw;
}

std::size_t countInGroup(const fo4::Record& record,
                         std::string_view signature,
                         std::size_t group) {
    const auto sig = fo4::Signature::from_string(signature);
    const auto terminator = fo4::Signature::from_string("SRAF");
    std::size_t currentGroup = 0;
    std::size_t count = 0;
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature == terminator) {
            if (currentGroup == group) break;
            ++currentGroup;
            continue;
        }
        if (currentGroup == group && subrecord.signature == sig) ++count;
    }
    return count;
}

}

TEST_CASE("PluginEditTool edits indexed nested RACE subgraph FormLink") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.setFieldValue(nlohmann::json::array({
        "Patch.esp",
        "000803:Patch.esp",
        "RACE\\Subgraphs[1]\\ActorKeywords[1]",
        "000801:Patch.esp"
    }));
    REQUIRE(result.has_value());

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto* race = fo4::find_record(
        disk, fo4::FormKey::parse("000803:Patch.esp"));
    REQUIRE(race != nullptr);
    CHECK(nthRawInGroup(*race, "SAKD", 0, 0) == 0x00000800u);
    CHECK(nthRawInGroup(*race, "SAKD", 1, 0) == 0x00000800u);
    CHECK(nthRawInGroup(*race, "SAKD", 1, 1) == 0x00000801u);
}

TEST_CASE("PluginEditTool inserts into empty nested RACE subgraph list") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "InsertFormLinkListElement",
        nlohmann::json::array({
            "Patch.esp",
            "000803:Patch.esp",
            "RACE\\Subgraphs[1]\\TargetKeywords",
            0,
            "000801:Patch.esp"
        }));
    REQUIRE(result.has_value());

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto* race = fo4::find_record(
        disk, fo4::FormKey::parse("000803:Patch.esp"));
    REQUIRE(race != nullptr);
    CHECK(countInGroup(*race, "STKD", 0) == 1u);
    CHECK(countInGroup(*race, "STKD", 1) == 1u);
    CHECK(nthRawInGroup(*race, "STKD", 1, 0) == 0x00000801u);
}

TEST_CASE("PluginEditTool removes nested RACE subgraph FormLink") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "RemoveFormLinkListElement",
        nlohmann::json::array({
            "Patch.esp",
            "000803:Patch.esp",
            "RACE\\Subgraphs[1]\\ActorKeywords",
            0
        }));
    REQUIRE(result.has_value());

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(path);
    const auto* race = fo4::find_record(
        disk, fo4::FormKey::parse("000803:Patch.esp"));
    REQUIRE(race != nullptr);
    CHECK(countInGroup(*race, "SAKD", 0) == 1u);
    CHECK(countInGroup(*race, "SAKD", 1) == 1u);
}

TEST_CASE("PluginEditTool rejects wrong nested FormLink target type without write") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginBytes());
    const auto before = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "InsertFormLinkListElement",
        nlohmann::json::array({
            "Patch.esp",
            "000803:Patch.esp",
            "RACE\\Subgraphs[1]\\TargetKeywords",
            0,
            "000802:Patch.esp"
        }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "FormLinkTargetTypeMismatch");
    CHECK(readBytes(path) == before);
}

TEST_CASE("PluginEditTool rejects missing nested container without write") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, pluginBytes());
    const auto before = readBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, path);
    const auto result = backend.invokeBackend(
        "InsertFormLinkListElement",
        nlohmann::json::array({
            "Patch.esp",
            "000803:Patch.esp",
            "RACE\\Subgraphs[2]\\ActorKeywords",
            0,
            "000801:Patch.esp"
        }));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "FormLinkListContainerIndexOutOfRange");
    CHECK(readBytes(path) == before);
}
