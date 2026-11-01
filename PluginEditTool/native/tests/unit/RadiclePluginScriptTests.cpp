#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/forms/effects.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/schema/vmad.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path scriptEditTempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_script_edit_" + std::to_string(id));
}

struct ScriptEditCleanup {
    std::filesystem::path root;
    ~ScriptEditCleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void scriptEditWriteBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

std::vector<std::byte> scriptEditReadBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream.good());
    const std::vector<char> raw((std::istreambuf_iterator<char>(stream)),
                                std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes;
    bytes.reserve(raw.size());
    for (const char ch : raw) bytes.push_back(std::byte{static_cast<std::uint8_t>(ch)});
    return bytes;
}

void scriptEditHeader(fo4::test::ByteWriter& out, std::initializer_list<std::string_view> masters) {
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    for (const auto master : masters) {
        std::vector<std::byte> name;
        name.reserve(master.size() + 1);
        for (const char ch : master) {
            name.push_back(std::byte{static_cast<std::uint8_t>(ch)});
        }
        name.push_back(std::byte{0});
        fo4::test::write_subrecord(out, "MAST", name);
        fo4::test::ByteWriter data;
        data.u32(0);
        data.u32(0);
        fo4::test::write_subrecord(out, "DATA", data.bytes);
    }
    fo4::test::end_record(out, tes4);
}

std::vector<std::byte> scriptEditEmptyVmad() {
    fo4::test::ByteWriter out;
    out.u16(6);
    out.u16(2);
    out.u16(0);
    return out.bytes;
}

std::vector<std::byte> scriptEditPatch() {
    fo4::test::ByteWriter out;
    scriptEditHeader(out, {"Base.esm"});
    const auto top = fo4::test::begin_group(out, "ACTI", 0);
    const auto bare = fo4::test::begin_record(out, "ACTI", 0, 0x01000800u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Bare\0", 5});
    fo4::test::end_record(out, bare);
    const auto withVmad = fo4::test::begin_record(out, "ACTI", 0, 0x01000801u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"WithVmad\0", 9});
    fo4::test::write_subrecord(out, "VMAD", scriptEditEmptyVmad());
    fo4::test::end_record(out, withVmad);
    fo4::test::end_group(out, top);
    return out.bytes;
}

std::vector<std::byte> scriptEditBase() {
    fo4::test::ByteWriter out;
    scriptEditHeader(out, {});
    return out.bytes;
}

std::vector<std::byte> scriptEditEffectBase() {
    fo4::test::ByteWriter out;
    scriptEditHeader(out, {});
    const auto effectGroup = fo4::test::begin_group(out, "MGEF", 0);
    const auto effect = fo4::test::begin_record(out, "MGEF", 0, 0x00000800u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"BaseEffect\0", 11});
    fo4::test::end_record(out, effect);
    fo4::test::end_group(out, effectGroup);
    const auto weaponGroup = fo4::test::begin_group(out, "WEAP", 0);
    const auto weapon = fo4::test::begin_record(out, "WEAP", 0, 0x00000801u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"BaseWeapon\0", 11});
    fo4::test::end_record(out, weapon);
    fo4::test::end_group(out, weaponGroup);
    return out.bytes;
}

std::vector<std::byte> scriptEditEffectPatch() {
    fo4::test::ByteWriter out;
    scriptEditHeader(out, {"Base.esm"});
    const auto spelGroup = fo4::test::begin_group(out, "SPEL", 0);
    const auto spell = fo4::test::begin_record(out, "SPEL", 0, 0x01000800u);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"PatchSpell\0", 12});
    fo4::test::end_record(out, spell);
    fo4::test::end_group(out, spelGroup);
    return out.bytes;
}

nlohmann::json scriptEditPluginEntry(std::string name, const std::filesystem::path& path) {
    return {{"name", std::move(name)}, {"path", path.string()}, {"source", "test"}, {"resolved", true}};
}

void scriptEditLoad(fo4ide::plugin::RadiclePluginBackend& backend,
                    const std::filesystem::path& basePath,
                    const std::filesystem::path& patchPath) {
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({scriptEditPluginEntry("Base.esm", basePath),
                                           scriptEditPluginEntry("Patch.esp", patchPath)})}};
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
}

bool scriptEditPending(fo4ide::plugin::RadiclePluginBackend& backend, std::size_t index) {
    const auto plugins = backend.invoke("appInterop.GetPlugins", nlohmann::json::array());
    REQUIRE(plugins.has_value());
    return nlohmann::json::parse(plugins->get<std::string>()).at(index).at("Pending").get<bool>();
}

const fo4::Subrecord* scriptEditVmad(const fo4::Record& record) {
    for (const auto& sub : record.subrecords) {
        if (sub.signature.to_string() == "VMAD") return &sub;
    }
    return nullptr;
}

}  // namespace

TEST_CASE("Native script add, property add and removals round-trip through save and re-read") {
    const auto root = scriptEditTempRoot();
    ScriptEditCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    scriptEditWriteBytes(basePath, scriptEditBase());
    scriptEditWriteBytes(patchPath, scriptEditPatch());

    fo4ide::plugin::RadiclePluginBackend backend;
    scriptEditLoad(backend, basePath, patchPath);
    const std::string key = "000800:Patch.esp";

    const auto added = backend.addScript(nlohmann::json::array({"Patch.esp", key, "PF_Test", 1, nullptr}));
    REQUIRE(added.has_value());
    CHECK(added->get<std::string>().find("index 0") != std::string::npos);
    REQUIRE(backend.addScript(nlohmann::json::array({"Patch.esp", key, "PF_Other", 0, 1})).has_value());
    REQUIRE(backend.addScript(nlohmann::json::array({"Patch.esp", "000801:Patch.esp", "PF_Third", 0, nullptr}))
                .has_value());
    REQUIRE(backend.addScriptProperty(nlohmann::json::array({"Patch.esp", key, 0, "Count", 3, 7, nullptr}))
                .has_value());
    REQUIRE(backend.addScriptProperty(nlohmann::json::array({"Patch.esp", key, 0, "Speed", 4, 0, nullptr}))
                .has_value());

    const auto duplicate = backend.addScript(nlohmann::json::array({"Patch.esp", key, "pf_test"}));
    REQUIRE_FALSE(duplicate.has_value());
    CHECK(duplicate.error().type == "ScriptWriteFailed");
    CHECK(duplicate.error().message.find("already present") != std::string::npos);

    CHECK(backend.removeScriptProperty(nlohmann::json::array({"Patch.esp", key, 0, 1})).has_value());
    CHECK(backend.removeScript(nlohmann::json::array({"Patch.esp", key, 1})).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    auto reread = fo4::read_plugin_file(patchPath);
    const auto* record = fo4::find_record(reread, fo4::FormKey::parse(key));
    REQUIRE(record != nullptr);
    const auto* vmad = scriptEditVmad(*record);
    REQUIRE(vmad != nullptr);
    const auto scripts = fo4::read_vmad_scripts(vmad->data);
    REQUIRE(scripts.scripts.size() == 1u);
    CHECK(scripts.scripts[0].name == "PF_Test");
    CHECK(scripts.scripts[0].flags == 1);
    REQUIRE(scripts.scripts[0].properties.size() == 1u);
    CHECK(scripts.scripts[0].properties[0].name == "Count");
    CHECK(scripts.scripts[0].properties[0].type == 3);
    CHECK(scripts.scripts[0].properties[0].flags == 7);
    REQUIRE(scripts.scripts[0].properties[0].integer.has_value());
    CHECK(*scripts.scripts[0].properties[0].integer == 0);

    CHECK_FALSE(scriptEditPending(backend, 1));
}

TEST_CASE("Native script refusals leave the plugin untouched and report a named error") {
    const auto root = scriptEditTempRoot();
    ScriptEditCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    scriptEditWriteBytes(basePath, scriptEditBase());
    scriptEditWriteBytes(patchPath, scriptEditPatch());
    fo4ide::plugin::RadiclePluginBackend backend;
    scriptEditLoad(backend, basePath, patchPath);
    const std::string bare = "000800:Patch.esp";
    const std::string withVmad = "000801:Patch.esp";
    REQUIRE(backend.addScript(nlohmann::json::array({"Patch.esp", withVmad, "PF_Third", 0, nullptr})).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto before = scriptEditReadBytes(patchPath);

    const auto refusals = [&](const nlohmann::json& args, const char* type,
                              const std::function<fo4ide::Result<nlohmann::json>(const nlohmann::json&)>& edit) {
        const auto result = edit(args);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().type == type);
        CHECK_FALSE(scriptEditPending(backend, 1));
    };
    refusals(nlohmann::json::array({"Patch.esp", bare, ""}), "InvalidScriptName",
             [&](const nlohmann::json& args) { return backend.addScript(args); });
    refusals(nlohmann::json::array({"Missing.esp", bare, "PF_Test"}), "PluginNotLoaded",
             [&](const nlohmann::json& args) { return backend.addScript(args); });
    refusals(nlohmann::json::array({"Patch.esp", bare, "PF_Test", 0, 9}), "ScriptWriteFailed",
             [&](const nlohmann::json& args) { return backend.addScript(args); });
    refusals(nlohmann::json::array({"Patch.esp", bare, 0, "Count", 3, 0, nullptr}), "VmadMissing",
             [&](const nlohmann::json& args) { return backend.addScriptProperty(args); });
    refusals(nlohmann::json::array({"Patch.esp", "000801:Patch.esp", 0, "Weird", 16, 0, nullptr}),
             "PropertyTypeUnsupported",
             [&](const nlohmann::json& args) { return backend.addScriptProperty(args); });
    refusals(nlohmann::json::array({"Patch.esp", "000801:Patch.esp", 4}), "ScriptWriteFailed",
             [&](const nlohmann::json& args) { return backend.removeScript(args); });
    refusals(nlohmann::json::array({"Patch.esp", bare, 0, 0}), "PropertyWriteFailed",
             [&](const nlohmann::json& args) { return backend.removeScriptProperty(args); });
    refusals(nlohmann::json::array({"Patch.esp", withVmad, -1}), "ScriptIndexInvalid",
             [&](const nlohmann::json& args) { return backend.removeScript(args); });
    refusals(nlohmann::json::array({"Patch.esp", withVmad, 4294967296}), "ScriptIndexInvalid",
             [&](const nlohmann::json& args) { return backend.removeScript(args); });
    refusals(nlohmann::json::array({"Patch.esp", withVmad, "PF_Fourth", 0, -1}), "ScriptIndexInvalid",
             [&](const nlohmann::json& args) { return backend.addScript(args); });
    refusals(nlohmann::json::array({"Patch.esp", withVmad, "PF_Fourth", 256}), "ScriptFlagsInvalid",
             [&](const nlohmann::json& args) { return backend.addScript(args); });
    refusals(nlohmann::json::array({"Patch.esp", withVmad, std::string("PF_Bad\0Name", 11), 0}), "InvalidScriptName",
             [&](const nlohmann::json& args) { return backend.addScript(args); });
    refusals(nlohmann::json::array({"Patch.esp", withVmad, 0, "Count", 300, 0, nullptr}), "PropertyTypeInvalid",
             [&](const nlohmann::json& args) { return backend.addScriptProperty(args); });
    refusals(nlohmann::json::array({"Patch.esp", withVmad, 0, "Count", 3, 256, nullptr}), "PropertyFlagsInvalid",
             [&](const nlohmann::json& args) { return backend.addScriptProperty(args); });

    CHECK_FALSE(scriptEditPending(backend, 1));
    CHECK(scriptEditReadBytes(patchPath) == before);
}

TEST_CASE("Native magic effect takes an MGEF from a loaded master and writes its master index") {
    const auto root = scriptEditTempRoot();
    ScriptEditCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    scriptEditWriteBytes(basePath, scriptEditEffectBase());
    scriptEditWriteBytes(patchPath, scriptEditEffectPatch());
    fo4ide::plugin::RadiclePluginBackend backend;
    scriptEditLoad(backend, basePath, patchPath);
    const std::string spell = "000800:Patch.esp";

    const auto added = backend.addMagicEffect(
        nlohmann::json::array({"Patch.esp", spell, "000800:Base.esm", 2.5, 1, 3}));
    REQUIRE(added.has_value());
    CHECK(added->at("index") == 0);
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto reread = fo4::read_plugin_file(patchPath);
    const auto* record = fo4::find_record(reread, fo4::FormKey::parse(spell));
    REQUIRE(record != nullptr);
    const auto effects = fo4::forms::read_magic_effects(reread, *record);
    REQUIRE(effects.size() == 1u);
    CHECK(effects[0].effect == fo4::FormKey::parse("000800:Base.esm"));
    CHECK(effects[0].duration == 3u);
}

TEST_CASE("Native magic effect refuses a master effect that is not an MGEF, is missing or has an unloaded owner") {
    const auto root = scriptEditTempRoot();
    ScriptEditCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    scriptEditWriteBytes(basePath, scriptEditEffectBase());
    scriptEditWriteBytes(patchPath, scriptEditEffectPatch());
    fo4ide::plugin::RadiclePluginBackend backend;
    scriptEditLoad(backend, basePath, patchPath);
    const std::string spell = "000800:Patch.esp";
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto before = scriptEditReadBytes(patchPath);

    const auto refusals = [&](const nlohmann::json& args, const char* type,
                              const std::function<fo4ide::Result<nlohmann::json>(const nlohmann::json&)>& edit) {
        const auto result = edit(args);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().type == type);
        CHECK_FALSE(scriptEditPending(backend, 1));
    };
    const auto add = [&](const nlohmann::json& args) { return backend.addMagicEffect(args); };
    const auto set = [&](const nlohmann::json& args) { return backend.setMagicEffect(args); };
    refusals(nlohmann::json::array({"Patch.esp", spell, "000801:Base.esm", 1.0, 0, 0}), "NotMagicEffect", add);
    refusals(nlohmann::json::array({"Patch.esp", spell, "000999:Base.esm", 1.0, 0, 0}), "RecordNotFound", add);
    refusals(nlohmann::json::array({"Patch.esp", spell, "000800:Other.esm", 1.0, 0, 0}),
             "MagicEffectOwnerNotLoaded", add);
    refusals(nlohmann::json::array({"Patch.esp", spell, 0, "000801:Base.esm", 1.0, 0, 0}),
             "NotMagicEffect", set);
    CHECK(scriptEditReadBytes(patchPath) == before);
}

TEST_CASE("Native magic effect refuses a loaded owner that the edited plugin does not declare as a master") {
    const auto root = scriptEditTempRoot();
    ScriptEditCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    const auto otherPath = root / "Other.esm";
    scriptEditWriteBytes(basePath, scriptEditEffectBase());
    scriptEditWriteBytes(patchPath, scriptEditEffectPatch());
    scriptEditWriteBytes(otherPath, scriptEditEffectBase());
    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({scriptEditPluginEntry("Base.esm", basePath),
                                           scriptEditPluginEntry("Patch.esp", patchPath),
                                           scriptEditPluginEntry("Other.esm", otherPath)})}};
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto before = scriptEditReadBytes(patchPath);

    const auto result = backend.addMagicEffect(
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", "000800:Other.esm", 1.0, 0, 0}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "MagicEffectOwnerNotMaster");
    CHECK_FALSE(scriptEditPending(backend, 1));
    CHECK(scriptEditReadBytes(patchPath) == before);
}

TEST_CASE("Native script property values bind through the typed setter") {
    const auto root = scriptEditTempRoot();
    ScriptEditCleanup cleanup{root};
    const auto basePath = root / "Base.esm";
    const auto patchPath = root / "Patch.esp";
    scriptEditWriteBytes(basePath, scriptEditBase());
    scriptEditWriteBytes(patchPath, scriptEditPatch());

    fo4ide::plugin::RadiclePluginBackend backend;
    scriptEditLoad(backend, basePath, patchPath);
    const std::string bare = "000800:Patch.esp";

    REQUIRE(backend.addScript(nlohmann::json::array({"Patch.esp", bare, "PF_Values", 0, nullptr}))
                .has_value());
    struct Property {
        const char* name;
        int type;
    };
    const Property properties[] = {{"Msg", 2}, {"Count", 3},  {"Speed", 4},   {"Flag", 5},
                                   {"Target", 1}, {"Names", 12}, {"Nums", 13}, {"Floats", 14},
                                   {"Bools", 15}, {"Objs", 11}};
    for (const auto& property : properties) {
        REQUIRE(backend.addScriptProperty(nlohmann::json::array(
                    {"Patch.esp", bare, 0, property.name, property.type, 0, nullptr}))
                    .has_value());
    }

    REQUIRE(backend.setScriptValue(nlohmann::json::array(
                {"Patch.esp", bare, 0, 0, "string", "hello"})).has_value());
    REQUIRE(backend.setScriptValue(nlohmann::json::array(
                {"Patch.esp", bare, 0, 1, "int", "-7"})).has_value());
    REQUIRE(backend.setScriptValue(nlohmann::json::array(
                {"Patch.esp", bare, 0, 2, "float", "1.5"})).has_value());
    REQUIRE(backend.setScriptValue(nlohmann::json::array(
                {"Patch.esp", bare, 0, 3, "bool", "true"})).has_value());
    REQUIRE(backend.setScriptValue(nlohmann::json::array(
                {"Patch.esp", bare, 0, 4, "object", "000801:Patch.esp", 3})).has_value());
    REQUIRE(backend.setScriptValue(nlohmann::json::array(
                {"Patch.esp", bare, 0, 5, "string", "alpha"})).has_value());
    REQUIRE(backend.setScriptValue(nlohmann::json::array(
                {"Patch.esp", bare, 0, 6, "int", "42"})).has_value());
    REQUIRE(backend.setScriptValue(nlohmann::json::array(
                {"Patch.esp", bare, 0, 7, "float", "2.5"})).has_value());
    REQUIRE(backend.setScriptValue(nlohmann::json::array(
                {"Patch.esp", bare, 0, 8, "bool", "true"})).has_value());
    REQUIRE(backend.setScriptValue(nlohmann::json::array(
                {"Patch.esp", bare, 0, 9, "object", "000801:Patch.esp", 4})).has_value());

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto reread = fo4::read_plugin_file(patchPath);
    const auto* record = fo4::find_record(reread, fo4::FormKey::parse(bare));
    REQUIRE(record != nullptr);
    const auto* vmad = scriptEditVmad(*record);
    REQUIRE(vmad != nullptr);
    const auto scripts = fo4::read_vmad_scripts(vmad->data);
    REQUIRE(scripts.scripts.size() == 1u);
    const auto& values = scripts.scripts[0].properties;
    REQUIRE(values.size() == 10u);
    CHECK(values[0].text == std::optional<std::string>{"hello"});
    CHECK(values[1].integer == std::optional<std::int32_t>{-7});
    CHECK(values[2].real == std::optional<float>{1.5f});
    CHECK(values[3].boolean == std::optional<bool>{true});
    REQUIRE(values[4].object.has_value());
    CHECK(values[4].object->value() == 0x01000801u);
    CHECK(values[4].alias == std::optional<std::int16_t>{static_cast<std::int16_t>(3)});
    REQUIRE(values[5].text_elements.has_value());
    CHECK(values[5].text_elements->at(0) == "alpha");
    REQUIRE(values[6].integer_elements.has_value());
    CHECK(values[6].integer_elements->at(0) == 42);
    REQUIRE(values[7].real_elements.has_value());
    CHECK(values[7].real_elements->at(0) == 2.5f);
    REQUIRE(values[8].boolean_elements.has_value());
    CHECK(values[8].boolean_elements->at(0) == true);
    REQUIRE(values[9].object_elements.has_value());
    CHECK(values[9].object_elements->at(0).object.value() == 0x01000801u);
    CHECK(values[9].object_elements->at(0).alias == 4);

    const auto before = scriptEditReadBytes(patchPath);
    const auto refusal = [&](const nlohmann::json& args, const char* type) {
        const auto result = backend.setScriptValue(args);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().type == type);
    };
    refusal(nlohmann::json::array({"Patch.esp", bare, 9, 0, "string", "x"}),
            "ScriptIndexOutOfRange");
    refusal(nlohmann::json::array({"Patch.esp", bare, 0, 99, "string", "x"}),
            "PropertyIndexOutOfRange");
    refusal(nlohmann::json::array({"Patch.esp", bare, 0, 0, "int", "1"}),
            "PropertyTypeMismatch");
    refusal(nlohmann::json::array({"Patch.esp", bare, 0, 1, "int", "1", nullptr, 1}),
            "InvalidPropertyValue");
    refusal(nlohmann::json::array({"Patch.esp", bare, 0, 3, "bool", "yes"}),
            "InvalidPropertyValue");
    refusal(nlohmann::json::array({"Patch.esp", bare, 0, 5, "string", "x", nullptr, 5}),
            "ElementIndexOutOfRange");
    refusal(nlohmann::json::array({"Patch.esp", bare, 0, 0, "string", "x", 2}),
            "InvalidPropertyValue");
    refusal(nlohmann::json::array({"Patch.esp", "000801:Patch.esp", 0, 0, "string", "x"}),
            "ScriptIndexOutOfRange");
    CHECK(scriptEditReadBytes(patchPath) == before);
}

