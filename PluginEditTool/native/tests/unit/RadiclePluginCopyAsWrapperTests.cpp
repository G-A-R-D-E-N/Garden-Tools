#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/forms/leveled_list.hpp>
#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("fo4ide_radicle_wrapper_" + std::to_string(id));
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void writeBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

void addLeveledList(fo4::Plugin& plugin, std::string_view signature, std::string_view edid,
                    std::initializer_list<std::string_view> items) {
    const auto list = fo4::forms::create_leveled_list(
        plugin, fo4::Signature::from_string(signature), edid, 0, 0, 0);
    std::size_t index = 0;
    for (const auto item : items) {
        fo4::forms::LeveledListEntry entry;
        entry.item = fo4::FormKey::parse(item);
        REQUIRE(fo4::forms::insert_leveled_list_entry(plugin, list, index++, entry));
    }
}

std::vector<std::byte> makeWrapperMaster() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", fo4::RecordFlags::master, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(4);
    hedr.u32(0x804);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto weaponGroup = fo4::test::begin_group(out, "WEAP", 0);
    const auto weapon = fo4::test::begin_record(out, "WEAP", 0, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"MasterWeapon\0", 13});
    fo4::test::end_record(out, weapon);
    fo4::test::end_group(out, weaponGroup);

    const auto npcGroup = fo4::test::begin_group(out, "NPC_", 0);
    const auto npc = fo4::test::begin_record(out, "NPC_", 0, 0x00000801);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"MasterNpc\0", 10});
    fo4::test::end_record(out, npc);
    fo4::test::end_group(out, npcGroup);

    const auto spellGroup = fo4::test::begin_group(out, "SPEL", 0);
    const auto spell = fo4::test::begin_record(out, "SPEL", 0, 0x00000802);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"MasterSpell\0", 12});
    fo4::test::end_record(out, spell);
    fo4::test::end_group(out, spellGroup);

    auto plugin = fo4::read_plugin(out.bytes, "Master.esm");
    addLeveledList(plugin, "LVLI", "MasterList", {"000800:Master.esm", "000801:Master.esm"});
    addLeveledList(plugin, "LVLN", "MasterNpcList", {"000801:Master.esm"});
    addLeveledList(plugin, "LVSP", "MasterSpellList", {"000802:Master.esm"});
    return fo4::write_plugin(plugin);
}

std::vector<std::byte> makeWrapperTarget() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x811);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto group = fo4::test::begin_group(out, "MISC", 0);
    const auto record = fo4::test::begin_record(out, "MISC", 0, 0x00000810);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"TargetLocal\0", 12});
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

std::vector<std::byte> makePatchSource() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x800);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::write_subrecord(out, "MAST", std::string_view{"Master.esm\0", 11});
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
    fo4::test::end_record(out, tes4);

    auto plugin = fo4::read_plugin(out.bytes, "Patch.esp");
    const auto item = fo4::forms::create_leveled_list(
        plugin, fo4::Signature::from_string("LVLI"), "PatchItem", 0, 0, 0);
    const auto list = fo4::forms::create_leveled_list(
        plugin, fo4::Signature::from_string("LVLI"), "PatchList", 0, 0, 0);
    fo4::forms::LeveledListEntry entry;
    entry.item = item;
    REQUIRE(fo4::forms::insert_leveled_list_entry(plugin, list, 0, entry));
    return fo4::write_plugin(plugin);
}

std::vector<std::byte> makeExtraMasterTarget() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x811);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::write_subrecord(out, "MAST", std::string_view{"Master.esm\0", 11});
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
    fo4::test::write_subrecord(out, "MAST", std::string_view{"Y.esp\0", 6});
    fo4::test::write_subrecord(out, "DATA", data.bytes);
    fo4::test::end_record(out, tes4);
    return out.bytes;
}

nlohmann::json pluginEntry(std::string name, const std::filesystem::path& path) {
    return {
        {"name", std::move(name)},
        {"path", path.string()},
        {"source", "test"},
        {"resolved", true}
    };
}

void loadWrapperEnvironment(fo4ide::plugin::RadiclePluginBackend& backend,
                            const std::filesystem::path& root) {
    writeBytes(root / "Master.esm", makeWrapperMaster());
    writeBytes(root / "Target.esp", makeWrapperTarget());
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Master.esm", root / "Master.esm"),
            pluginEntry("Target.esp", root / "Target.esp")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
}

std::string textResult(const fo4ide::Result<nlohmann::json>& result) {
    REQUIRE(result.has_value());
    REQUIRE(result->is_string());
    return result->get<std::string>();
}

std::string between(const std::string& text, std::string_view begin, std::string_view end) {
    const auto start = text.find(begin);
    REQUIRE(start != std::string::npos);
    const auto from = start + begin.size();
    const auto stop = text.find(end, from);
    REQUIRE(stop != std::string::npos);
    return text.substr(from, stop - from);
}

fo4::FormKey overrideKeyFrom(const std::string& message) {
    return fo4::FormKey::parse(between(message, "Copied ", " as wrapper in "));
}

fo4::FormKey copyKeyFrom(const std::string& message) {
    return fo4::FormKey::parse(between(message, "NEW record ", " ('"));
}

std::string editorIdOf(const fo4::Record& record) {
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature == fo4::sig_edid) {
            return std::string(reinterpret_cast<const char*>(subrecord.data.data()));
        }
    }
    return {};
}

bool hasEditorIdSubrecord(const fo4::Record& record) {
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature == fo4::sig_edid) return true;
    }
    return false;
}

void saveTarget(fo4ide::plugin::RadiclePluginBackend& backend) {
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Target.esp"})).has_value());
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

bool pendingPlugin(fo4ide::plugin::RadiclePluginBackend& backend, std::size_t index) {
    const auto plugins = backend.invoke("appInterop.GetPlugins", nlohmann::json::array());
    REQUIRE(plugins.has_value());
    return nlohmann::json::parse(plugins->get<std::string>()).at(index).at("Pending").get<bool>();
}

}

TEST_CASE("Native CopyAsWrapper overrides an LVLI and keeps its FormKey and EDID") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    loadWrapperEnvironment(backend, root);

    const auto message = textResult(backend.copyAsWrapper(nlohmann::json::array(
        {"Master.esm", "000804:Master.esm", "Target.esp", "WrapList"})));
    saveTarget(backend);

    const auto reloaded = fo4::read_plugin_file(root / "Target.esp");
    const auto key = overrideKeyFrom(message);
    CHECK(key == fo4::FormKey::parse("000804:Master.esm"));
    const auto* wrapper = fo4::find_record(reloaded, key);
    REQUIRE(wrapper != nullptr);
    CHECK(wrapper->header.signature == fo4::Signature::from_string("LVLI"));
    CHECK(editorIdOf(*wrapper) == "MasterList");

    const auto copied = copyKeyFrom(message);
    const auto list = fo4::forms::decode_leveled_list(reloaded, key);
    REQUIRE(list.entries.size() == 1);
    CHECK(list.entries[0].item == copied);
    CHECK(list.entries[0].level == 1);
    CHECK(list.entries[0].count == 1);
}

TEST_CASE("Native CopyAsWrapper with an empty EditorID gives the NEW copy no EDID subrecord") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    loadWrapperEnvironment(backend, root);

    const auto message = textResult(backend.copyAsWrapper(nlohmann::json::array(
        {"Master.esm", "000804:Master.esm", "Target.esp", ""})));
    saveTarget(backend);

    const auto reloaded = fo4::read_plugin_file(root / "Target.esp");
    const auto* copy = fo4::find_record(reloaded, fo4::FormKey::parse(message.substr(message.find("NEW record ") + 11, message.size() - message.find("NEW record ") - 12)));
    REQUIRE(copy != nullptr);
    CHECK_FALSE(hasEditorIdSubrecord(*copy));
    const auto* wrapper = fo4::find_record(reloaded, overrideKeyFrom(message));
    REQUIRE(wrapper != nullptr);
    CHECK(editorIdOf(*wrapper) == "MasterList");
}

TEST_CASE("Native CopyAsWrapper moves all of the original entries into the NEW copy") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    loadWrapperEnvironment(backend, root);

    const auto message = textResult(backend.copyAsWrapper(nlohmann::json::array(
        {"Master.esm", "000804:Master.esm", "Target.esp", "WrapList"})));
    saveTarget(backend);

    const auto reloaded = fo4::read_plugin_file(root / "Target.esp");
    const auto copied = copyKeyFrom(message);
    const auto* copy = fo4::find_record(reloaded, copied);
    REQUIRE(copy != nullptr);
    CHECK(copy->header.signature == fo4::Signature::from_string("LVLI"));
    CHECK(editorIdOf(*copy) == "WrapList");
    const auto entries = fo4::forms::decode_leveled_list(reloaded, copied).entries;
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].item == fo4::FormKey::parse("000800:Master.esm"));
    CHECK(entries[1].item == fo4::FormKey::parse("000801:Master.esm"));
}

TEST_CASE("Native CopyAsWrapper reuses an override already in the target") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    loadWrapperEnvironment(backend, root);

    textResult(backend.invoke("backend.CopyAsOverride", nlohmann::json::array(
        {"Master.esm", "000804:Master.esm", "Target.esp", false})));
    const auto message = textResult(backend.copyAsWrapper(nlohmann::json::array(
        {"Master.esm", "000804:Master.esm", "Target.esp", "WrapList"})));
    saveTarget(backend);

    const auto reloaded = fo4::read_plugin_file(root / "Target.esp");
    const auto key = overrideKeyFrom(message);
    CHECK(key == fo4::FormKey::parse("000804:Master.esm"));
    const auto list = fo4::forms::decode_leveled_list(reloaded, key);
    REQUIRE(list.entries.size() == 1);
    CHECK(list.entries[0].item == copyKeyFrom(message));
    CHECK(fo4::forms::decode_leveled_list(reloaded, copyKeyFrom(message)).entries.size() == 2);
}

TEST_CASE("Native CopyAsWrapper refuses a target override that differs and keeps its entries") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    loadWrapperEnvironment(backend, root);

    textResult(backend.invoke("backend.CopyAsOverride", nlohmann::json::array(
        {"Master.esm", "000804:Master.esm", "Target.esp", false})));
    REQUIRE(backend.editLeveledListEntry(nlohmann::json::array({
        "Target.esp", "000804:Master.esm", "add", 2,
        nlohmann::json{{"level", 1}, {"item", "000802:Master.esm"}, {"count", 1}, {"chanceNone", 0}}
    })).has_value());
    const auto rejected = backend.copyAsWrapper(nlohmann::json::array(
        {"Master.esm", "000804:Master.esm", "Target.esp", "WrapList"}));
    REQUIRE_FALSE(rejected.has_value());
    CHECK(rejected.error().type == "WrapperTargetOverrideDiffers");
    saveTarget(backend);

    const auto reloaded = fo4::read_plugin_file(root / "Target.esp");
    const auto key = fo4::FormKey::parse("000804:Master.esm");
    const auto* wrapper = fo4::find_record(reloaded, key);
    REQUIRE(wrapper != nullptr);
    CHECK(editorIdOf(*wrapper) == "MasterList");
    const auto entries = fo4::forms::decode_leveled_list(reloaded, key).entries;
    REQUIRE(entries.size() == 3);
    CHECK(entries[0].item == fo4::FormKey::parse("000800:Master.esm"));
    CHECK(entries[1].item == fo4::FormKey::parse("000801:Master.esm"));
    CHECK(entries[2].item == fo4::FormKey::parse("000802:Master.esm"));
}

TEST_CASE("Native CopyAsWrapper wraps an LVLN and an LVSP once each") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    loadWrapperEnvironment(backend, root);

    const auto npcMessage = textResult(backend.copyAsWrapper(nlohmann::json::array(
        {"Master.esm", "000805:Master.esm", "Target.esp", "WrapNpcs"})));
    const auto spellMessage = textResult(backend.copyAsWrapper(nlohmann::json::array(
        {"Master.esm", "000806:Master.esm", "Target.esp", "WrapSpells"})));
    saveTarget(backend);

    const auto reloaded = fo4::read_plugin_file(root / "Target.esp");
    const std::vector<std::pair<std::string, std::string>> wrapped{
        {npcMessage, "LVLN"}, {spellMessage, "LVSP"}};
    for (const auto& [message, signature] : wrapped) {
        INFO(signature);
        const auto key = overrideKeyFrom(message);
        const auto* wrapper = fo4::find_record(reloaded, key);
        REQUIRE(wrapper != nullptr);
        CHECK(wrapper->header.signature == fo4::Signature::from_string(signature));
        const auto list = fo4::forms::decode_leveled_list(reloaded, key);
        REQUIRE(list.entries.size() == 1);
        CHECK(list.entries[0].item == copyKeyFrom(message));
        CHECK(list.entries[0].level == 1);
        CHECK(list.entries[0].count == 1);
    }
}

TEST_CASE("Native CopyAsWrapper refuses a WEAP") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    loadWrapperEnvironment(backend, root);

    const auto rejected = backend.copyAsWrapper(nlohmann::json::array(
        {"Master.esm", "000800:Master.esm", "Target.esp", "WrapWeapon"}));
    REQUIRE_FALSE(rejected.has_value());
    CHECK(rejected.error().type == "WrapperUnsupportedType");
}

TEST_CASE("Native CopyAsWrapper refuses a wrapped EDID equal to the source EDID") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    loadWrapperEnvironment(backend, root);

    for (const auto* editorId : {"MasterList", "masterlist"}) {
        INFO(editorId);
        const auto rejected = backend.copyAsWrapper(nlohmann::json::array(
            {"Master.esm", "000804:Master.esm", "Target.esp", editorId}));
        REQUIRE_FALSE(rejected.has_value());
        CHECK(rejected.error().type == "WrapperEditorIdSameAsSource");
    }
}

TEST_CASE("Native CopyAsWrapper adds the source owner as a master of the target") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    loadWrapperEnvironment(backend, root);

    const auto owner = fo4::FormKey::parse("000804:Master.esm").owner();
    const auto before = fo4::read_plugin_file(root / "Target.esp");
    CHECK_FALSE(std::any_of(before.header.masters.begin(), before.header.masters.end(),
                            [&](const auto& master) { return master.key == owner; }));

    textResult(backend.copyAsWrapper(nlohmann::json::array(
        {"Master.esm", "000804:Master.esm", "Target.esp", nullptr})));
    saveTarget(backend);

    const auto after = fo4::read_plugin_file(root / "Target.esp");
    CHECK(std::any_of(after.header.masters.begin(), after.header.masters.end(),
                      [&](const auto& master) { return master.key == owner; }));
}

TEST_CASE("Native CopyAsWrapper gives an existing override the source EDID") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    loadWrapperEnvironment(backend, root);

    textResult(backend.invoke("backend.CopyAsOverride", nlohmann::json::array(
        {"Master.esm", "000804:Master.esm", "Target.esp", false})));
    REQUIRE(backend.invokeBackend("SetField", nlohmann::json::array(
        {"Target.esp", "000804:Master.esm", "EDID", "OverrideName"})).has_value());
    const auto message = textResult(backend.copyAsWrapper(nlohmann::json::array(
        {"Master.esm", "000804:Master.esm", "Target.esp", "WrapList"})));
    saveTarget(backend);

    const auto reloaded = fo4::read_plugin_file(root / "Target.esp");
    const auto* wrapper = fo4::find_record(reloaded, overrideKeyFrom(message));
    REQUIRE(wrapper != nullptr);
    CHECK(editorIdOf(*wrapper) == "MasterList");
}

TEST_CASE("Native CopyAsWrapper refuses malformed arguments and unknown plugins without staging") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    loadWrapperEnvironment(backend, root);
    const auto before = readBytes(root / "Target.esp");

    const std::vector<std::pair<nlohmann::json, std::string>> refusals{
        {nlohmann::json::array({"", "000804:Master.esm", "Target.esp", "W"}), "SourcePluginMissing"},
        {nlohmann::json::array({"Master.esm", "", "Target.esp", "W"}), "FormKeyMissing"},
        {nlohmann::json::array({"Master.esm", "000804:Master.esm", "", "W"}), "TargetPluginMissing"},
        {nlohmann::json::array({"Master.esm", "000804:Master.esm", "Target.esp", 5}), "InvalidEditorId"},
        {nlohmann::json::array({"Master.esm", "000804:Master.esm", "Target.esp", nlohmann::json::object()}), "InvalidEditorId"},
        {nlohmann::json::array({"Master.esm", "zz", "Target.esp", "W"}), "InvalidFormKey"},
        {nlohmann::json::array({"Nope.esm", "000804:Master.esm", "Target.esp", "W"}), "SourcePluginNotLoaded"},
        {nlohmann::json::array({"Master.esm", "000804:Master.esm", "Nope.esp", "W"}), "TargetPluginNotLoaded"},
        {nlohmann::json::array({"Master.esm", "000899:Master.esm", "Target.esp", "W"}), "RecordNotFound"}
    };
    for (const auto& [arguments, type] : refusals) {
        INFO(type);
        const auto rejected = backend.copyAsWrapper(arguments);
        REQUIRE_FALSE(rejected.has_value());
        CHECK(rejected.error().type == type);
    }
    CHECK_FALSE(pendingPlugin(backend, 1));
    const bool unchanged = readBytes(root / "Target.esp") == before;
    CHECK(unchanged);
}

TEST_CASE("Native CopyAsWrapper is refused on a protected target and when the target changed on disk") {
    const auto root = tempRoot();
    Cleanup cleanup{root};

    const auto protectedRoot = root / "protected";
    writeBytes(protectedRoot / "Master.esm", makeWrapperMaster());
    writeBytes(protectedRoot / "Target.esp", makeWrapperTarget());
    const auto protectedBefore = readBytes(protectedRoot / "Target.esp");
    fo4ide::plugin::RadiclePluginBackend protectedBackend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Master.esm", protectedRoot / "Master.esm"),
            nlohmann::json{
                {"name", "Target.esp"},
                {"path", (protectedRoot / "Target.esp").string()},
                {"source", "data"},
                {"resolved", true}
            }
        })}
    };
    REQUIRE(protectedBackend.invoke("environment.LoadMo2", environment).has_value());
    const auto refused = protectedBackend.copyAsWrapper(nlohmann::json::array(
        {"Master.esm", "000804:Master.esm", "Target.esp", "WrapList"}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "PluginReadOnly");
    CHECK_FALSE(pendingPlugin(protectedBackend, 1));
    const bool protectedUnchanged = readBytes(protectedRoot / "Target.esp") == protectedBefore;
    CHECK(protectedUnchanged);

    const auto changedPath = root / "changed" / "Target.esp";
    fo4ide::plugin::RadiclePluginBackend changedBackend;
    loadWrapperEnvironment(changedBackend, root / "changed");
    const auto loadedTime = std::filesystem::last_write_time(changedPath);
    writeBytes(changedPath, makeWrapperTarget());
    std::filesystem::last_write_time(changedPath, loadedTime + std::chrono::seconds(2));
    const auto changed = changedBackend.copyAsWrapper(nlohmann::json::array(
        {"Master.esm", "000804:Master.esm", "Target.esp", "WrapList"}));
    REQUIRE_FALSE(changed.has_value());
    CHECK(changed.error().type == "PluginChangedOnDisk");
    CHECK_FALSE(pendingPlugin(changedBackend, 1));
}

TEST_CASE("Native CopyAsWrapper reports LOSES and stages nothing when a later plugin overrides the record") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    writeBytes(root / "Master.esm", makeWrapperMaster());
    writeBytes(root / "Target.esp", makeWrapperTarget());
    auto later = fo4::make_empty_plugin("Later.esp");
    REQUIRE(fo4::copy_record_as_override(
        later, fo4::read_plugin_file(root / "Master.esm"), fo4::FormKey::parse("000804:Master.esm"), false) ==
        fo4::RecordCopyStatus::inserted);
    fo4::write_plugin_file(later, root / "Later.esp");

    fo4ide::plugin::RadiclePluginBackend backend;
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Master.esm", root / "Master.esm"),
            pluginEntry("Target.esp", root / "Target.esp"),
            pluginEntry("Later.esp", root / "Later.esp")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    const auto recordsBefore = fo4::collect_records(fo4::read_plugin_file(root / "Target.esp")).size();

    const auto message = textResult(backend.copyAsWrapper(nlohmann::json::array(
        {"Master.esm", "000804:Master.esm", "Target.esp", "WrapList"})));
    CHECK(message.starts_with("LOSES: Target.esp loads before Later.esp, which also override 000804:Master.esm."));
    CHECK_FALSE(pendingPlugin(backend, 1));

    saveTarget(backend);
    const auto reloaded = fo4::read_plugin_file(root / "Target.esp");
    CHECK(fo4::find_record(reloaded, fo4::FormKey::parse("000804:Master.esm")) == nullptr);
    CHECK(fo4::collect_records(reloaded).size() == recordsBefore);
}


TEST_CASE("Native CopyAsWrapper refuses an EDID-changed override that also has an extra entry") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    loadWrapperEnvironment(backend, root);

    textResult(backend.invoke("backend.CopyAsOverride", nlohmann::json::array(
        {"Master.esm", "000804:Master.esm", "Target.esp", false})));
    REQUIRE(backend.invokeBackend("SetField", nlohmann::json::array(
        {"Target.esp", "000804:Master.esm", "EDID", "OverrideName"})).has_value());
    REQUIRE(backend.editLeveledListEntry(nlohmann::json::array({
        "Target.esp", "000804:Master.esm", "add", 2,
        nlohmann::json{{"level", 1}, {"item", "000802:Master.esm"}, {"count", 1}, {"chanceNone", 0}}
    })).has_value());
    const auto rejected = backend.copyAsWrapper(nlohmann::json::array(
        {"Master.esm", "000804:Master.esm", "Target.esp", "WrapList"}));
    REQUIRE_FALSE(rejected.has_value());
    CHECK(rejected.error().type == "WrapperTargetOverrideDiffers");
}

TEST_CASE("Native CopyAsWrapper reuses a target override whose master list differs from the source") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    writeBytes(root / "Master.esm", makeWrapperMaster());
    writeBytes(root / "Patch.esp", makePatchSource());
    writeBytes(root / "Target.esp", makeExtraMasterTarget());
    const nlohmann::json environment{
        {"plugins", nlohmann::json::array({
            pluginEntry("Master.esm", root / "Master.esm"),
            pluginEntry("Patch.esp", root / "Patch.esp"),
            pluginEntry("Target.esp", root / "Target.esp")
        })}
    };
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());

    textResult(backend.invoke("backend.CopyAsOverride", nlohmann::json::array(
        {"Patch.esp", "000801:Patch.esp", "Target.esp", false})));
    const auto message = textResult(backend.copyAsWrapper(nlohmann::json::array(
        {"Patch.esp", "000801:Patch.esp", "Target.esp", "WrapList"})));
    saveTarget(backend);

    const auto reloaded = fo4::read_plugin_file(root / "Target.esp");
    const auto key = fo4::FormKey::parse("000801:Patch.esp");
    const auto* wrapper = fo4::find_record(reloaded, key);
    REQUIRE(wrapper != nullptr);
    CHECK(wrapper->header.signature == fo4::Signature::from_string("LVLI"));
    CHECK(editorIdOf(*wrapper) == "PatchList");
    const auto list = fo4::forms::decode_leveled_list(reloaded, key);
    REQUIRE(list.entries.size() == 1);
    CHECK(list.entries[0].item == copyKeyFrom(message));
    const auto copied = fo4::forms::decode_leveled_list(reloaded, copyKeyFrom(message)).entries;
    REQUIRE(copied.size() == 1);
    CHECK(copied[0].item == fo4::FormKey::parse("000800:Patch.esp"));
}
