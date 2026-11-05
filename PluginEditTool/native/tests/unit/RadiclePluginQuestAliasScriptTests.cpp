#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/reader.hpp>
#include <fo4/schema/vmad.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path aliasScriptTempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("fo4ide_alias_script_" + std::to_string(id));
}

struct AliasScriptCleanup {
    std::filesystem::path root;
    ~AliasScriptCleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void aliasScriptWriteBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

bool aliasScriptPending(fo4ide::plugin::RadiclePluginBackend& backend, std::size_t index) {
    const auto plugins = backend.invoke("appInterop.GetPlugins", nlohmann::json::array());
    REQUIRE(plugins.has_value());
    return nlohmann::json::parse(plugins->get<std::string>()).at(index).at("Pending").get<bool>();
}

void aliasScriptWriteName(fo4::test::ByteWriter& out, std::string_view text) {
    out.u16(static_cast<std::uint16_t>(text.size()));
    out.raw(text);
}

void aliasScriptHeader(fo4::test::ByteWriter& out, std::initializer_list<std::string_view> masters) {
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x900);
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

void aliasScriptLeading(fo4::test::ByteWriter& out) {
    out.u16(6);
    out.u16(2);
    out.u16(0);
}

std::vector<std::byte> aliasScriptQuestWithAlias() {
    fo4::test::ByteWriter out;
    aliasScriptLeading(out);
    out.u8(3);
    out.u16(0);
    out.u16(0);
    out.u16(1);
    out.u16(0);
    out.u16(1);
    out.u32(0);
    out.u16(6);
    out.u16(2);
    out.u16(1);
    aliasScriptWriteName(out, "Old");
    out.u8(0);
    out.u16(1);
    aliasScriptWriteName(out, "Keep");
    out.u8(3);
    out.u8(0);
    out.u32(5);
    return out.bytes;
}

std::vector<std::byte> aliasScriptQuestBare() {
    fo4::test::ByteWriter out;
    aliasScriptLeading(out);
    out.u8(3);
    out.u16(0);
    out.u16(0);
    out.u16(0);
    return out.bytes;
}

std::vector<std::byte> aliasScriptEmptyVmad() {
    fo4::test::ByteWriter out;
    aliasScriptLeading(out);
    return out.bytes;
}

std::vector<std::byte> aliasScriptPatch() {
    fo4::test::ByteWriter out;
    aliasScriptHeader(out, {"Base.esm"});
    const auto top = fo4::test::begin_group(out, "QUST", 0);
    const auto quest = fo4::test::begin_record(out, "QUST", 0, 0x01000801u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Quest\0", 6});
    fo4::test::write_subrecord(out, "VMAD", aliasScriptQuestWithAlias());
    fo4::test::end_record(out, quest);
    const auto bare = fo4::test::begin_record(out, "QUST", 0, 0x01000802u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Bare\0", 5});
    fo4::test::write_subrecord(out, "VMAD", aliasScriptQuestBare());
    fo4::test::end_record(out, bare);
    const auto plain = fo4::test::begin_record(out, "QUST", 0, 0x01000803u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Plain\0", 6});
    fo4::test::end_record(out, plain);
    fo4::test::end_group(out, top);
    const auto actiGroup = fo4::test::begin_group(out, "ACTI", 0);
    const auto activator = fo4::test::begin_record(out, "ACTI", 0, 0x01000800u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Act\0", 4});
    fo4::test::write_subrecord(out, "VMAD", aliasScriptEmptyVmad());
    fo4::test::end_record(out, activator);
    fo4::test::end_group(out, actiGroup);
    return out.bytes;
}

std::vector<std::byte> aliasScriptBase() {
    fo4::test::ByteWriter out;
    aliasScriptHeader(out, {});
    return out.bytes;
}

nlohmann::json aliasScriptPluginEntry(std::string name, const std::filesystem::path& path) {
    return {{"name", std::move(name)}, {"path", path.string()}, {"source", "test"}, {"resolved", true}};
}

void aliasScriptLoad(fo4ide::plugin::RadiclePluginBackend& backend, const std::filesystem::path& basePath,
                     const std::filesystem::path& patchPath) {
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({aliasScriptPluginEntry("Base.esm", basePath),
                                           aliasScriptPluginEntry("Patch.esp", patchPath)})}};
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
}

const fo4::Subrecord* aliasScriptVmad(const fo4::Record& record) {
    for (const auto& sub : record.subrecords) {
        if (sub.signature.to_string() == "VMAD") return &sub;
    }
    return nullptr;
}

fo4::QuestVmadExtras aliasScriptExtras(const fo4::Record& record) {
    const auto* vmad = aliasScriptVmad(record);
    REQUIRE(vmad != nullptr);
    const auto scripts = fo4::read_vmad_scripts(vmad->data);
    REQUIRE(scripts.version == 6);
    REQUIRE(scripts.object_format == 2);
    REQUIRE(scripts.trailing_bytes > 0);
    return fo4::read_quest_vmad_extras(
        std::span<const std::byte>(vmad->data).subspan(vmad->data.size() - scripts.trailing_bytes),
        scripts.object_format);
}

}  // namespace

TEST_CASE("Native quest alias scripts add, edit and remove and round-trip through save") {
    const auto root = aliasScriptTempRoot();
    AliasScriptCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    aliasScriptWriteBytes(basePath, aliasScriptBase());
    aliasScriptWriteBytes(patchPath, aliasScriptPatch());

    fo4ide::plugin::RadiclePluginBackend backend;
    aliasScriptLoad(backend, basePath, patchPath);
    const std::string key = "000801:Patch.esp";

    REQUIRE(backend.addAliasScript(nlohmann::json::array({"Patch.esp", key, 1, "First", 0, 0})).has_value());
    REQUIRE(backend.addAliasScript(nlohmann::json::array({"Patch.esp", key, 0, "Early", 0, nullptr})).has_value());
    REQUIRE(backend.addAliasScript(nlohmann::json::array({"Patch.esp", key, 9, "Late", 0, nullptr})).has_value());
    REQUIRE(backend.addAliasScriptProperty(
                nlohmann::json::array({"Patch.esp", key, 1, 1, "Speed", 4, 0, nullptr})).has_value());
    REQUIRE(backend.setAliasScriptValue(
                nlohmann::json::array({"Patch.esp", key, 1, 1, 0, "int", "9", nullptr, nullptr})).has_value());
    REQUIRE(backend.setAliasScriptValue(
                nlohmann::json::array({"Patch.esp", key, 1, 1, 1, "float", "2.5", nullptr, nullptr})).has_value());
    REQUIRE(backend.removeAliasScriptProperty(nlohmann::json::array({"Patch.esp", key, 1, 1, 1})).has_value());
    REQUIRE(backend.removeAliasScript(nlohmann::json::array({"Patch.esp", key, 1, 0})).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    auto reread = fo4::read_plugin_file(patchPath);
    const auto* record = fo4::find_record(reread, fo4::FormKey::parse(key));
    REQUIRE(record != nullptr);
    const auto extras = aliasScriptExtras(*record);
    REQUIRE(extras.aliases.size() == 3u);
    CHECK(extras.aliases[0].alias_id == 0);
    CHECK(extras.aliases[1].alias_id == 1);
    CHECK(extras.aliases[2].alias_id == 9);
    for (const auto& alias : extras.aliases) CHECK(alias.version == 6);
    REQUIRE(extras.aliases[0].scripts.size() == 1u);
    CHECK(extras.aliases[0].scripts[0].name == "Early");
    REQUIRE(extras.aliases[1].scripts.size() == 1u);
    CHECK(extras.aliases[1].scripts[0].name == "Old");
    REQUIRE(extras.aliases[1].scripts[0].properties.size() == 1u);
    CHECK(extras.aliases[1].scripts[0].properties[0].name == "Keep");
    REQUIRE(extras.aliases[1].scripts[0].properties[0].integer.has_value());
    CHECK(*extras.aliases[1].scripts[0].properties[0].integer == 9);
    REQUIRE(extras.aliases[2].scripts.size() == 1u);
    CHECK(extras.aliases[2].scripts[0].name == "Late");
    CHECK(extras.fully_consumed);
}

TEST_CASE("Native quest alias script bootstraps the extras section on a bare quest VMAD") {
    const auto root = aliasScriptTempRoot();
    AliasScriptCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    aliasScriptWriteBytes(basePath, aliasScriptBase());
    aliasScriptWriteBytes(patchPath, aliasScriptPatch());

    fo4ide::plugin::RadiclePluginBackend backend;
    aliasScriptLoad(backend, basePath, patchPath);
    const std::string key = "000802:Patch.esp";

    REQUIRE(backend.addAliasScript(nlohmann::json::array({"Patch.esp", key, 4, "Only", 1, 0})).has_value());
    REQUIRE(backend.addAliasScriptProperty(
                nlohmann::json::array({"Patch.esp", key, 4, 0, "Count", 3, 0, nullptr})).has_value());
    REQUIRE(backend.setAliasScriptValue(
                nlohmann::json::array({"Patch.esp", key, 4, 0, 0, "int", "7", nullptr, nullptr})).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    auto reread = fo4::read_plugin_file(patchPath);
    const auto* record = fo4::find_record(reread, fo4::FormKey::parse(key));
    REQUIRE(record != nullptr);
    const auto extras = aliasScriptExtras(*record);
    REQUIRE(extras.aliases.size() == 1u);
    CHECK(extras.aliases[0].alias_id == 4);
    CHECK(extras.aliases[0].version == 6);
    CHECK(extras.aliases[0].script_format == 2);
    REQUIRE(extras.aliases[0].scripts.size() == 1u);
    CHECK(extras.aliases[0].scripts[0].name == "Only");
    CHECK(extras.aliases[0].scripts[0].flags == 1);
    REQUIRE(extras.aliases[0].scripts[0].properties.size() == 1u);
    CHECK(extras.aliases[0].scripts[0].properties[0].name == "Count");
    REQUIRE(extras.aliases[0].scripts[0].properties[0].integer.has_value());
    CHECK(*extras.aliases[0].scripts[0].properties[0].integer == 7);
}

TEST_CASE("Native quest alias script refusals leave the plugin untouched and report a named error") {
    const auto root = aliasScriptTempRoot();
    AliasScriptCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    aliasScriptWriteBytes(basePath, aliasScriptBase());
    aliasScriptWriteBytes(patchPath, aliasScriptPatch());

    fo4ide::plugin::RadiclePluginBackend backend;
    aliasScriptLoad(backend, basePath, patchPath);
    const std::string quest = "000801:Patch.esp";
    const std::string activator = "000800:Patch.esp";
    const std::string plain = "000803:Patch.esp";
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK_FALSE(aliasScriptPending(backend, 1));

    const auto refusal = [&](const nlohmann::json& args, const char* type, std::string_view text,
                             const std::function<fo4ide::Result<nlohmann::json>(const nlohmann::json&)>& edit) {
        const auto result = edit(args);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().type == type);
        CHECK(result.error().message.find(text) != std::string::npos);
        CHECK_FALSE(aliasScriptPending(backend, 1));
    };
    refusal(nlohmann::json::array({"Patch.esp", quest, 1, "old", 0, nullptr}), "ScriptWriteFailed",
            "already present",
            [&](const nlohmann::json& args) { return backend.addAliasScript(args); });
    refusal(nlohmann::json::array({"Patch.esp", quest, 7, "New", 0, 1}), "ScriptWriteFailed",
            "index is out of range",
            [&](const nlohmann::json& args) { return backend.addAliasScript(args); });
    refusal(nlohmann::json::array({"Patch.esp", activator, 1, "Nope", 0, nullptr}), "ScriptWriteFailed",
            "not authorable",
            [&](const nlohmann::json& args) { return backend.addAliasScript(args); });
    refusal(nlohmann::json::array({"Patch.esp", quest, 70000, "Nope", 0, nullptr}), "AliasIdInvalid",
            "Alias ID",
            [&](const nlohmann::json& args) { return backend.addAliasScript(args); });
    refusal(nlohmann::json::array({"Patch.esp", quest, 3, 0}), "ScriptWriteFailed", "binding is absent",
            [&](const nlohmann::json& args) { return backend.removeAliasScript(args); });
    refusal(nlohmann::json::array({"Patch.esp", quest, 1, 5}), "ScriptWriteFailed", "index is out of range",
            [&](const nlohmann::json& args) { return backend.removeAliasScript(args); });
    refusal(nlohmann::json::array({"Patch.esp", quest, 1, 0, "Keep", 3, 0, nullptr}), "PropertyWriteFailed",
            "already present",
            [&](const nlohmann::json& args) { return backend.addAliasScriptProperty(args); });
    refusal(nlohmann::json::array({"Patch.esp", quest, 1, 5, "Other", 3, 0, nullptr}), "ScriptIndexOutOfRange",
            "out of range", [&](const nlohmann::json& args) { return backend.addAliasScriptProperty(args); });
    refusal(nlohmann::json::array({"Patch.esp", quest, 3, 0, 0}), "PropertyWriteFailed", "binding is absent",
            [&](const nlohmann::json& args) { return backend.removeAliasScriptProperty(args); });
    refusal(nlohmann::json::array({"Patch.esp", quest, 3, 0, 0, "int", "1", nullptr, nullptr}),
            "QuestAliasMissing", "absent",
            [&](const nlohmann::json& args) { return backend.setAliasScriptValue(args); });
    refusal(nlohmann::json::array({"Patch.esp", quest, 1, 0, 0, "string", "x", nullptr, nullptr}),
            "PropertyTypeMismatch", "int",
            [&](const nlohmann::json& args) { return backend.setAliasScriptValue(args); });
    refusal(nlohmann::json::array({"Patch.esp", quest, 1, 0, 0, "int", "9", 2, nullptr}),
            "InvalidPropertyValue", "alias only applies",
            [&](const nlohmann::json& args) { return backend.setAliasScriptValue(args); });
    refusal(nlohmann::json::array({"Patch.esp", plain, 1, "Nope", 0, nullptr}), "ScriptWriteFailed",
            "no VMAD subrecord",
            [&](const nlohmann::json& args) { return backend.addAliasScript(args); });
}

namespace {

std::vector<std::byte> aliasScriptActivatorWithAliasPatch() {
    fo4::test::ByteWriter out;
    aliasScriptHeader(out, {"Base.esm"});
    const auto actiGroup = fo4::test::begin_group(out, "ACTI", 0);
    const auto activator = fo4::test::begin_record(out, "ACTI", 0, 0x01000805u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"ActAlias\0", 9});
    fo4::test::write_subrecord(out, "VMAD", aliasScriptQuestWithAlias());
    fo4::test::end_record(out, activator);
    fo4::test::end_group(out, actiGroup);
    return out.bytes;
}

}  // namespace

TEST_CASE("Native quest alias scripts keep negative alias ids in signed ascending order") {
    const auto root = aliasScriptTempRoot();
    AliasScriptCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    aliasScriptWriteBytes(basePath, aliasScriptBase());
    aliasScriptWriteBytes(patchPath, aliasScriptPatch());

    fo4ide::plugin::RadiclePluginBackend backend;
    aliasScriptLoad(backend, basePath, patchPath);
    const std::string key = "000801:Patch.esp";

    REQUIRE(backend.addAliasScript(nlohmann::json::array({"Patch.esp", key, -1, "Neg", 0, 0})).has_value());
    REQUIRE(backend.addAliasScript(nlohmann::json::array({"Patch.esp", key, -32768, "Min", 0, 0})).has_value());
    REQUIRE(backend.addAliasScript(nlohmann::json::array({"Patch.esp", key, 2, "Two", 0, 0})).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    auto reread = fo4::read_plugin_file(patchPath);
    const auto* record = fo4::find_record(reread, fo4::FormKey::parse(key));
    REQUIRE(record != nullptr);
    const auto extras = aliasScriptExtras(*record);
    REQUIRE(extras.aliases.size() == 4u);
    CHECK(extras.aliases[0].alias_id == -32768);
    CHECK(extras.aliases[1].alias_id == -1);
    CHECK(extras.aliases[2].alias_id == 1);
    CHECK(extras.aliases[3].alias_id == 2);
    REQUIRE(extras.aliases[0].scripts.size() == 1u);
    CHECK(extras.aliases[0].scripts[0].name == "Min");
    REQUIRE(extras.aliases[1].scripts.size() == 1u);
    CHECK(extras.aliases[1].scripts[0].name == "Neg");
    REQUIRE(extras.aliases[2].scripts.size() == 1u);
    CHECK(extras.aliases[2].scripts[0].name == "Old");
    CHECK(extras.fully_consumed);
}

TEST_CASE("Native quest alias script inserts a middle script into an existing alias") {
    const auto root = aliasScriptTempRoot();
    AliasScriptCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    aliasScriptWriteBytes(basePath, aliasScriptBase());
    aliasScriptWriteBytes(patchPath, aliasScriptPatch());

    fo4ide::plugin::RadiclePluginBackend backend;
    aliasScriptLoad(backend, basePath, patchPath);
    const std::string key = "000801:Patch.esp";

    REQUIRE(backend.addAliasScript(nlohmann::json::array({"Patch.esp", key, 1, "First", 0, 0})).has_value());
    REQUIRE(backend.addAliasScript(nlohmann::json::array({"Patch.esp", key, 1, "Mid", 0, 1})).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    auto reread = fo4::read_plugin_file(patchPath);
    const auto* record = fo4::find_record(reread, fo4::FormKey::parse(key));
    REQUIRE(record != nullptr);
    const auto extras = aliasScriptExtras(*record);
    REQUIRE(extras.aliases.size() == 1u);
    REQUIRE(extras.aliases[0].scripts.size() == 3u);
    CHECK(extras.aliases[0].scripts[0].name == "First");
    CHECK(extras.aliases[0].scripts[1].name == "Mid");
    CHECK(extras.aliases[0].scripts[2].name == "Old");
    REQUIRE(extras.aliases[0].scripts[2].properties.size() == 1u);
    CHECK(extras.aliases[0].scripts[2].properties[0].name == "Keep");
    CHECK(extras.fully_consumed);
}

TEST_CASE("Native quest alias script new entry carries the leading version, object format and a zero object word") {
    const auto root = aliasScriptTempRoot();
    AliasScriptCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    aliasScriptWriteBytes(basePath, aliasScriptBase());
    aliasScriptWriteBytes(patchPath, aliasScriptPatch());

    fo4ide::plugin::RadiclePluginBackend backend;
    aliasScriptLoad(backend, basePath, patchPath);
    const std::string key = "000802:Patch.esp";

    REQUIRE(backend.addAliasScript(nlohmann::json::array({"Patch.esp", key, 4, "Only", 1, 0})).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    auto reread = fo4::read_plugin_file(patchPath);
    const auto* record = fo4::find_record(reread, fo4::FormKey::parse(key));
    REQUIRE(record != nullptr);
    const auto* sub = aliasScriptVmad(*record);
    REQUIRE(sub != nullptr);
    const auto scripts = fo4::read_vmad_scripts(sub->data);
    REQUIRE(scripts.version == 6);
    REQUIRE(scripts.object_format == 2);
    const auto extras = aliasScriptExtras(*record);
    REQUIRE(extras.aliases.size() == 1u);
    const auto start = sub->data.size() - scripts.trailing_bytes + extras.aliases[0].offset;
    std::string dump;
    for (std::size_t i = 0; i < 14; ++i) dump += std::to_string(std::to_integer<unsigned>(sub->data[start + i])) + " ";
    INFO("entry bytes: " << dump << "| trailing " << scripts.trailing_bytes << " size " << sub->data.size()
                         << " offset " << extras.aliases[0].offset);
    const auto word = [&](std::size_t at) {
        return static_cast<unsigned>(std::to_integer<std::uint8_t>(sub->data[at])) |
               (static_cast<unsigned>(std::to_integer<std::uint8_t>(sub->data[at + 1])) << 8);
    };
    CHECK(word(start) == 0u);
    CHECK(word(start + 2) == 4u);
    CHECK(word(start + 4) == 0u);
    CHECK(word(start + 6) == 0u);
    CHECK(word(start + 8) == 6u);
    CHECK(word(start + 10) == 2u);
    CHECK(word(start + 12) == 1u);
}

TEST_CASE("Native quest alias script refuses a non-QUST VMAD with a named error") {
    const auto root = aliasScriptTempRoot();
    AliasScriptCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    aliasScriptWriteBytes(basePath, aliasScriptBase());
    aliasScriptWriteBytes(patchPath, aliasScriptActivatorWithAliasPatch());

    fo4ide::plugin::RadiclePluginBackend backend;
    aliasScriptLoad(backend, basePath, patchPath);
    const std::string activator = "000805:Patch.esp";
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK_FALSE(aliasScriptPending(backend, 1));

    const auto added = backend.addAliasScript(nlohmann::json::array({"Patch.esp", activator, 1, "Nope", 0, 0}));
    REQUIRE_FALSE(added.has_value());
    INFO("add error: " << added.error().type << " / " << added.error().message);
    CHECK(added.error().type == "ScriptWriteFailed");
    CHECK(added.error().message.find("not authorable") != std::string::npos);

    const auto valued = backend.setAliasScriptValue(
        nlohmann::json::array({"Patch.esp", activator, 1, 0, 0, "int", "9", nullptr, nullptr}));
    REQUIRE_FALSE(valued.has_value());
    INFO("set error: " << valued.error().type << " / " << valued.error().message);
    CHECK(valued.error().type == "ScriptValueNotAuthorable");
    CHECK(valued.error().message.find("not authorable") != std::string::npos);
    CHECK(valued.error().message.find("QUST") != std::string::npos);
    CHECK_FALSE(aliasScriptPending(backend, 1));
}
