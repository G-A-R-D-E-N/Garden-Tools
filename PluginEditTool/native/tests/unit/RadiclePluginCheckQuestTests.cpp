#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path checkQuestTempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("fo4ide_check_quest_" + std::to_string(id));
}

struct CheckQuestCleanup {
    std::filesystem::path root;
    ~CheckQuestCleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void checkQuestWriteBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

nlohmann::json checkQuestEnvironment(const std::filesystem::path& path) {
    return nlohmann::json{{
        "plugins",
        nlohmann::json::array({
            {
                {"name", "Patch.esp"},
                {"path", path.string()},
                {"source", "CheckQuestTests"},
                {"resolved", true}
            }
        })
    }};
}

void writeU32Subrecord(fo4::test::ByteWriter& out, std::string_view signature, std::uint32_t value) {
    fo4::test::ByteWriter data;
    data.u32(value);
    fo4::test::write_subrecord(out, signature, data.bytes);
}

void writeLvlo(fo4::test::ByteWriter& out, std::uint32_t target) {
    fo4::test::ByteWriter lvlo;
    lvlo.u16(1);
    lvlo.u16(0);
    lvlo.u32(target);
    lvlo.u16(1);
    lvlo.u8(100);
    lvlo.u8(0);
    fo4::test::write_subrecord(out, "LVLO", lvlo.bytes);
}

std::vector<std::byte> scopedPluginBytes() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto questTop = fo4::test::begin_group(out, "QUST", 0);

    const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x800);
    writeU32Subrecord(out, "GNAM", 0x999);
    fo4::test::end_record(out, quest);

    const auto questChildren = fo4::test::begin_group(out, "", 10);
    out.patch_u32(questChildren + 8, 0x800);
    const auto dial = fo4::test::begin_record(out, "DIAL", 0, 0x801);
    writeU32Subrecord(out, "QNAM", 0x800);
    fo4::test::end_record(out, dial);
    const auto topicChildren = fo4::test::begin_group(out, "", 7);
    out.patch_u32(topicChildren + 8, 0x801);
    const auto info = fo4::test::begin_record(out, "INFO", 0, 0x802);
    writeU32Subrecord(out, "PNAM", 0x999);
    fo4::test::end_record(out, info);
    fo4::test::end_group(out, topicChildren);
    const auto scene = fo4::test::begin_record(out, "SCEN", 0, 0x803);
    writeU32Subrecord(out, "PNAM", 0x800);
    writeU32Subrecord(out, "KSIZ", 1);
    writeU32Subrecord(out, "KWDA", 0x999);
    fo4::test::end_record(out, scene);
    fo4::test::end_group(out, questChildren);

    const auto other = fo4::test::begin_record(out, "QUST", 0, 0x810);
    writeU32Subrecord(out, "XNAM", 0x998);
    fo4::test::end_record(out, other);
    const auto otherChildren = fo4::test::begin_group(out, "", 10);
    out.patch_u32(otherChildren + 8, 0x810);
    const auto otherDial = fo4::test::begin_record(out, "DIAL", 0, 0x811);
    writeU32Subrecord(out, "QNAM", 0x810);
    fo4::test::end_record(out, otherDial);
    const auto otherTopics = fo4::test::begin_group(out, "", 7);
    out.patch_u32(otherTopics + 8, 0x811);
    const auto otherInfo = fo4::test::begin_record(out, "INFO", 0, 0x812);
    writeU32Subrecord(out, "PNAM", 0x997);
    fo4::test::end_record(out, otherInfo);
    fo4::test::end_group(out, otherTopics);
    fo4::test::end_group(out, otherChildren);
    fo4::test::end_group(out, questTop);

    const auto leveled = fo4::test::begin_group(out, "LVLI", 0);
    const auto first = fo4::test::begin_record(out, "LVLI", 0, 0x820);
    writeLvlo(out, 0x821);
    fo4::test::end_record(out, first);
    const auto second = fo4::test::begin_record(out, "LVLI", 0, 0x821);
    writeLvlo(out, 0x820);
    fo4::test::end_record(out, second);
    fo4::test::end_group(out, leveled);
    return out.bytes;
}

bool hasRow(const nlohmann::json& rows, std::string_view code, std::string_view formKey) {
    for (const auto& row : rows) {
        if (row.value("Code", std::string{}) == code && row.value("FormKey", std::string{}) == formKey) {
            return true;
        }
    }
    return false;
}

bool hasCode(const nlohmann::json& rows, std::string_view code) {
    for (const auto& row : rows) {
        if (row.value("Code", std::string{}) == code) return true;
    }
    return false;
}

bool hasFormKeyLocal(const nlohmann::json& rows, std::string_view localPrefix) {
    for (const auto& row : rows) {
        if (row.value("FormKey", std::string{}).starts_with(localPrefix)) return true;
    }
    return false;
}

nlohmann::json rowsOf(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    return nlohmann::json::parse(result->get<std::string>());
}

}  // namespace

TEST_CASE("Native CheckQuest returns the check row shape for the quest, its dialogue and its scenes") {
    const auto root = checkQuestTempRoot();
    CheckQuestCleanup cleanup{root};
    const auto path = root / "Patch.esp";
    checkQuestWriteBytes(path, scopedPluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", checkQuestEnvironment(path)).has_value());

    const auto rows = rowsOf(backend.checkQuest(nlohmann::json::array({"Patch.esp", "000800:Patch.esp"})));
    CHECK(hasRow(rows, "unresolved_link", "000800:Patch.esp"));
    CHECK(hasRow(rows, "unresolved_link", "000802:Patch.esp"));
    CHECK(hasRow(rows, "unresolved_link", "000803:Patch.esp"));
    for (const auto& row : rows) {
        CHECK(row.contains("Code"));
        CHECK(row.contains("Detail"));
        CHECK(row.contains("Severity"));
        CHECK(row.contains("Plugin"));
        CHECK(row.contains("FormKey"));
        CHECK(row.contains("Subrecord"));
        CHECK(row.contains("Target"));
        CHECK(row.contains("Message"));
    }
}

TEST_CASE("Native CheckQuest omits records of other quests and of the load order passes") {
    const auto root = checkQuestTempRoot();
    CheckQuestCleanup cleanup{root};
    const auto path = root / "Patch.esp";
    checkQuestWriteBytes(path, scopedPluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", checkQuestEnvironment(path)).has_value());

    const auto first = rowsOf(backend.checkQuest(nlohmann::json::array({"Patch.esp", "000800:Patch.esp"})));
    CHECK_FALSE(hasFormKeyLocal(first, "000810"));
    CHECK_FALSE(hasFormKeyLocal(first, "000811"));
    CHECK_FALSE(hasFormKeyLocal(first, "000812"));
    CHECK_FALSE(hasCode(first, "circular_leveled_list"));

    const auto second = rowsOf(backend.checkQuest(nlohmann::json::array({"Patch.esp", "000810:Patch.esp"})));
    CHECK(hasRow(second, "unresolved_link", "000812:Patch.esp"));
    CHECK_FALSE(hasFormKeyLocal(second, "000800"));
    CHECK_FALSE(hasFormKeyLocal(second, "000801"));
    CHECK_FALSE(hasFormKeyLocal(second, "000802"));
    CHECK_FALSE(hasFormKeyLocal(second, "000803"));
    CHECK_FALSE(hasCode(second, "circular_leveled_list"));
}

TEST_CASE("Native CheckForErrors reports the leveled-list cycle that CheckQuest does not run") {
    const auto root = checkQuestTempRoot();
    CheckQuestCleanup cleanup{root};
    const auto path = root / "Patch.esp";
    checkQuestWriteBytes(path, scopedPluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", checkQuestEnvironment(path)).has_value());

    const auto whole = nlohmann::json::parse(
        backend.checkForErrors(nlohmann::json::array({"Patch.esp"})).value().get<std::string>());
    CHECK(hasCode(whole, "circular_leveled_list"));
    CHECK(hasRow(whole, "unresolved_link", "000812:Patch.esp"));

    const auto scoped = rowsOf(backend.checkQuest(nlohmann::json::array({"Patch.esp", "000800:Patch.esp"})));
    CHECK_FALSE(hasCode(scoped, "circular_leveled_list"));
}

TEST_CASE("Native CheckQuest refuses a record that is not a QUST and an unloaded plugin") {
    const auto root = checkQuestTempRoot();
    CheckQuestCleanup cleanup{root};
    const auto path = root / "Patch.esp";
    checkQuestWriteBytes(path, scopedPluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", checkQuestEnvironment(path)).has_value());

    const auto notQuest = backend.checkQuest(nlohmann::json::array({"Patch.esp", "000820:Patch.esp"}));
    REQUIRE_FALSE(notQuest.has_value());
    CHECK(notQuest.error().type == "RecordTypeMismatch");

    const auto missing = backend.checkQuest(nlohmann::json::array({"Nope.esp", "000800:Patch.esp"}));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "PluginNotLoaded");
}
