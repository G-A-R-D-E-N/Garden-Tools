#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/forms/leveled_list.hpp>
#include <fo4/forms/leveled_spell.hpp>
#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path leveledTempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_leveled_" + std::to_string(id));
}

struct LeveledCleanup {
    std::filesystem::path root;
    ~LeveledCleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void leveledWriteBytes(const std::filesystem::path& path,
                       const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

void writeLvlo(fo4::test::ByteWriter& out,
               std::uint32_t target,
               std::uint16_t unused = 0,
               std::uint8_t padding = 0) {
    fo4::test::ByteWriter lvlo;
    lvlo.u16(1);       // level
    lvlo.u16(unused);  // unused
    lvlo.u32(target);  // reference FormID, schema-proven offset 4
    lvlo.u16(1);       // count
    lvlo.u8(100);      // chance none / entry chance byte
    lvlo.u8(padding);  // padding
    fo4::test::write_subrecord(out, "LVLO", lvlo.bytes);
}

std::vector<std::byte> circularLeveledBytes() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x802u);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "LVLI", 0);
    const auto first = fo4::test::begin_record(out, "LVLI", 0, 0x00000800u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"ListA\0", 6});
    writeLvlo(out, 0x00000801u);
    fo4::test::end_record(out, first);

    const auto second = fo4::test::begin_record(out, "LVLI", 0, 0x00000801u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"ListB\0", 6});
    writeLvlo(out, 0x00000800u);
    fo4::test::end_record(out, second);
    fo4::test::end_group(out, group);
    return out.bytes;
}

void leveledWriteByte(fo4::test::ByteWriter& out, std::string_view signature, std::uint8_t value) {
    fo4::test::ByteWriter data;
    data.u8(value);
    fo4::test::write_subrecord(out, signature, data.bytes);
}

std::vector<std::byte> settledLeveledBytes(std::uint16_t unused = 0, std::uint8_t padding = 0) {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x803u);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "LVLI", 0);
    const auto list = fo4::test::begin_record(out, "LVLI", 0, 0x00000800u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"ListC\0", 6});
    leveledWriteByte(out, "LVLD", 10);
    leveledWriteByte(out, "LVLM", 2);
    leveledWriteByte(out, "LVLF", 1);
    leveledWriteByte(out, "LLCT", 1);
    writeLvlo(out, 0x00000801u, unused, padding);
    fo4::test::ByteWriter coed;
    coed.u32(0x00000800u);
    coed.u32(0);
    coed.f32(1.0f);
    fo4::test::write_subrecord(out, "COED", coed.bytes);
    fo4::test::end_record(out, list);
    fo4::test::end_group(out, group);

    const auto misc_group = fo4::test::begin_group(out, "MISC", 0);
    const auto misc = fo4::test::begin_record(out, "MISC", 0, 0x00000802u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"TargetD\0", 8});
    fo4::test::end_record(out, misc);
    fo4::test::end_group(out, misc_group);
    return out.bytes;
}

std::vector<std::byte> leveledReadBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream.good());
    const std::vector<char> raw((std::istreambuf_iterator<char>(stream)),
                                std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes;
    bytes.reserve(raw.size());
    for (const char ch : raw) bytes.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    return bytes;
}

nlohmann::json leveledEnvironment(const std::filesystem::path& path) {
    return nlohmann::json{{
        "plugins",
        nlohmann::json::array({
            {
                {"name", "Patch.esp"},
                {"path", path.string()},
                {"source", "LeveledTest"},
                {"resolved", true}
            }
        })
    }};
}

}  // namespace

TEST_CASE("Native CheckCircularLeveledLists follows schema-proven LVLO references") {
    const auto root = leveledTempRoot();
    LeveledCleanup cleanup{root};
    const auto path = root / "Patch.esp";
    leveledWriteBytes(path, circularLeveledBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    const auto loaded = backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{
            "plugins",
            nlohmann::json::array({
                {
                    {"name", "Patch.esp"},
                    {"path", path.string()},
                    {"source", "LeveledTest"},
                    {"resolved", true}
                }
            })
        }});
    REQUIRE(loaded.has_value());

    const auto report = backend.checkCircularLeveledLists(
        nlohmann::json::array({"Patch.esp", 200}));
    REQUIRE(report.has_value());
    REQUIRE(report->is_string());
    CHECK(report->get<std::string>().find("1 circular leveled list(s)") != std::string::npos);
    CHECK(report->get<std::string>().find("ListA [000800:Patch.esp]") != std::string::npos);
    CHECK(report->get<std::string>().find("ListB [000801:Patch.esp]") != std::string::npos);
}

TEST_CASE("Native leveled list typed decode reads settings, entries and COED") {
    const auto root = leveledTempRoot();
    LeveledCleanup cleanup{root};
    const auto path = root / "Patch.esp";
    leveledWriteBytes(path, settledLeveledBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", leveledEnvironment(path)).has_value());

    const auto list = backend.getLeveledList(nlohmann::json::array({"000800:Patch.esp"}));
    REQUIRE(list.has_value());
    CHECK(list->at("chanceNone") == 10);
    CHECK(list->at("maxCount") == 2);
    CHECK(list->at("flags") == 1);
    REQUIRE(list->at("entries").size() == 1);
    const auto& entry = list->at("entries")[0];
    CHECK(entry.at("level") == 1);
    CHECK(entry.at("item") == "000801:Patch.esp");
    CHECK(entry.at("count") == 1);
    CHECK(entry.at("chanceNone") == 100);
    CHECK(entry.at("extra").at("owner") == "000800:Patch.esp");
    CHECK(entry.at("extra").at("globalOrRank") == 0);
    CHECK(entry.at("extra").at("itemCondition") == 1.0);
}

TEST_CASE("Native leveled list entry edit round-trips through save and re-read") {
    const auto root = leveledTempRoot();
    LeveledCleanup cleanup{root};
    const auto path = root / "Patch.esp";
    leveledWriteBytes(path, settledLeveledBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", leveledEnvironment(path)).has_value());

    const auto edited = backend.editLeveledListEntry(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "set", 0,
        nlohmann::json{{"level", 2}, {"item", "000802:Patch.esp"}, {"count", 3}, {"chanceNone", 50}}
    }));
    REQUIRE(edited.has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto reread = fo4::read_plugin_file(path);
    const auto decoded = fo4::forms::decode_leveled_list(reread, fo4::FormKey::parse("000800:Patch.esp"));
    CHECK(decoded.chance_none == 10);
    CHECK(decoded.max_count == 2);
    CHECK(decoded.flags == 1);
    REQUIRE(decoded.entries.size() == 1);
    CHECK(decoded.entries[0].level == 2);
    CHECK(decoded.entries[0].item == fo4::FormKey::parse("000802:Patch.esp"));
    CHECK(decoded.entries[0].count == 3);
    CHECK(decoded.entries[0].chance_none == 50);
    CHECK_FALSE(decoded.entries[0].extra.has_value());
}

TEST_CASE("Native leveled list remove refuses an out-of-range index and leaves the plugin bytes unchanged") {
    const auto root = leveledTempRoot();
    LeveledCleanup cleanup{root};
    const auto path = root / "Patch.esp";
    leveledWriteBytes(path, settledLeveledBytes());
    const auto before = leveledReadBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", leveledEnvironment(path)).has_value());

    const auto refused = backend.editLeveledListEntry(
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", "remove", 5, nullptr}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "LeveledListWriteFailed");

    const auto list = backend.getLeveledList(nlohmann::json::array({"000800:Patch.esp"}));
    REQUIRE(list.has_value());
    CHECK(list->at("entries").size() == 1);
    const bool unchanged = leveledReadBytes(path) == before;
    CHECK(unchanged);
    const auto plugins = backend.invoke("appInterop.GetPlugins", nlohmann::json::array());
    REQUIRE(plugins.has_value());
    CHECK(nlohmann::json::parse(plugins->get<std::string>()).at(0).at("Pending") == false);
}

TEST_CASE("Native leveled list set keeps the replaced entry's reserved bytes through save and re-read") {
    const auto root = leveledTempRoot();
    LeveledCleanup cleanup{root};
    const auto path = root / "Patch.esp";
    leveledWriteBytes(path, settledLeveledBytes(0xABCDu, 0x5Au));
    const auto key = fo4::FormKey::parse("000800:Patch.esp");
    const auto original = fo4::forms::decode_leveled_list(fo4::read_plugin_file(path), key);
    REQUIRE(original.entries.size() == 1);
    REQUIRE(original.entries[0].reserved_before_form_id[0] != std::byte{0x00});
    REQUIRE(original.entries[0].reserved_tail != std::byte{0x00});

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", leveledEnvironment(path)).has_value());

    const auto edited = backend.editLeveledListEntry(nlohmann::json::array({
        "Patch.esp", "000800:Patch.esp", "set", 0,
        nlohmann::json{{"level", 2}, {"item", "000802:Patch.esp"}, {"count", 3}, {"chanceNone", 50}}
    }));
    REQUIRE(edited.has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto reread = fo4::forms::decode_leveled_list(fo4::read_plugin_file(path), key);
    REQUIRE(reread.entries.size() == 1);
    CHECK(reread.entries[0].level == 2);
    CHECK(reread.entries[0].reserved_before_form_id == original.entries[0].reserved_before_form_id);
    CHECK(reread.entries[0].reserved_tail == original.entries[0].reserved_tail);
}

TEST_CASE("Native leveled list typed read picks the winning version or the named plugin") {
    const auto root = leveledTempRoot();
    LeveledCleanup cleanup{root};
    const auto patchPath = root / "Patch.esp";
    const auto winnerPath = root / "Winner.esp";

    auto patch = fo4::read_plugin(settledLeveledBytes(), "Patch.esp");
    const auto listKey = fo4::FormKey::parse("000800:Patch.esp");
    const auto listType = fo4::Signature::from_string("LVLI");
    fo4::forms::create_leveled_list(patch, listType, std::string_view{"ItemB"}, 0, 0, 0);
    fo4::forms::create_leveled_list(patch, listType, std::string_view{"ItemC"}, 0, 0, 0);
    const auto entryFor = [](std::string_view item) {
        fo4::forms::LeveledListEntry entry;
        entry.item = fo4::FormKey::parse(item);
        return entry;
    };
    const std::vector patchEntries{
        entryFor("000802:Patch.esp"), entryFor("000803:Patch.esp"), entryFor("000804:Patch.esp")};
    REQUIRE(fo4::forms::replace_leveled_list_entries(patch, listKey, patchEntries));

    auto winner = fo4::make_empty_plugin("Winner.esp");
    REQUIRE(fo4::copy_record_as_override(winner, patch, listKey, false) ==
            fo4::RecordCopyStatus::inserted);
    const std::vector winnerEntries{entryFor("000802:Patch.esp"), entryFor("000804:Patch.esp")};
    REQUIRE(fo4::forms::replace_leveled_list_entries(winner, listKey, winnerEntries));

    leveledWriteBytes(patchPath, fo4::write_plugin(patch));
    leveledWriteBytes(winnerPath, fo4::write_plugin(winner));

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", nlohmann::json{{
        "plugins",
        nlohmann::json::array({
            {
                {"name", "Patch.esp"},
                {"path", patchPath.string()},
                {"source", "LeveledTest"},
                {"resolved", true}
            },
            {
                {"name", "Winner.esp"},
                {"path", winnerPath.string()},
                {"source", "LeveledTest"},
                {"resolved", true}
            }
        })
    }}).has_value());

    const auto winning = backend.getLeveledList(nlohmann::json::array({"000800:Patch.esp"}));
    REQUIRE(winning.has_value());
    REQUIRE(winning->at("entries").size() == 2);
    CHECK(winning->at("entries")[0].at("item") == "000802:Patch.esp");
    CHECK(winning->at("entries")[1].at("item") == "000804:Patch.esp");

    const auto patchVersion = backend.getLeveledList(nlohmann::json::array({"000800:Patch.esp", "Patch.esp"}));
    REQUIRE(patchVersion.has_value());
    REQUIRE(patchVersion->at("entries").size() == 3);

    const auto removed = backend.editLeveledListEntry(
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", "remove", 1, nullptr}));
    REQUIRE(removed.has_value());

    const auto afterRemove = backend.getLeveledList(nlohmann::json::array({"000800:Patch.esp", "Patch.esp"}));
    REQUIRE(afterRemove.has_value());
    REQUIRE(afterRemove->at("entries").size() == 2);
    CHECK(afterRemove->at("entries")[0].at("item") == "000802:Patch.esp");
    CHECK(afterRemove->at("entries")[1].at("item") == "000804:Patch.esp");

    const auto stillWinning = backend.getLeveledList(nlohmann::json::array({"000800:Patch.esp"}));
    REQUIRE(stillWinning.has_value());
    CHECK(stillWinning->at("entries").size() == 2);
}

TEST_CASE("Native leveled spell list refuses extra COED entries and leaves the plugin unstaged") {
    const auto root = leveledTempRoot();
    LeveledCleanup cleanup{root};
    const auto path = root / "Patch.esp";

    auto patch = fo4::read_plugin(settledLeveledBytes(), "Patch.esp");
    const std::vector<fo4::forms::LeveledSpellEntry> noEntries;
    const auto spell = fo4::forms::create_leveled_spell(patch, 0u, 0u, 0u, noEntries);
    REQUIRE(spell == fo4::FormKey::parse("000803:Patch.esp"));
    leveledWriteBytes(path, fo4::write_plugin(patch));
    const auto before = leveledReadBytes(path);

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", leveledEnvironment(path)).has_value());

    const nlohmann::json entry{
        {"level", 1},
        {"item", "000802:Patch.esp"},
        {"count", 1},
        {"chanceNone", 100},
        {"extra", {{"owner", "000800:Patch.esp"}, {"globalOrRank", 0}, {"itemCondition", 1.0}}}
    };
    const auto added = backend.editLeveledListEntry(
        nlohmann::json::array({"Patch.esp", "000803:Patch.esp", "add", 0, entry}));
    REQUIRE_FALSE(added.has_value());
    CHECK(added.error().type == "LeveledListWriteFailed");

    const auto replaced = backend.editLeveledListEntry(
        nlohmann::json::array({"Patch.esp", "000803:Patch.esp", "set", 0, entry}));
    REQUIRE_FALSE(replaced.has_value());
    CHECK(replaced.error().type == "LeveledListWriteFailed");

    const auto plugins = backend.invoke("appInterop.GetPlugins", nlohmann::json::array());
    REQUIRE(plugins.has_value());
    CHECK(nlohmann::json::parse(plugins->get<std::string>()).at(0).at("Pending") == false);
    const bool unchanged = leveledReadBytes(path) == before;
    CHECK(unchanged);
}
