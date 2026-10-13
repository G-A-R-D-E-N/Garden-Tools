#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/forms/leveled_list.hpp>
#include <fo4/forms/leveled_spell.hpp>
#include <fo4/plugin/reader.hpp>
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

    const auto cellGroup = fo4::test::begin_group(out, "CELL", 0);
    const auto cell = fo4::test::begin_record(out, "CELL", 0, 0x00000803);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"MasterCell\0", 11});
    fo4::test::end_record(out, cell);
    fo4::test::end_group(out, cellGroup);
    return out.bytes;
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

fo4::FormKey wrapperKeyFrom(const std::string& message) {
    const std::string marker = " as wrapper ";
    REQUIRE(message.find(marker) != std::string::npos);
    const auto begin = message.find(marker) + marker.size();
    const auto end = message.find(" in ", begin);
    REQUIRE(end != std::string::npos);
    return fo4::FormKey::parse(message.substr(begin, end - begin));
}

}

TEST_CASE("Native CopyAsWrapper wraps a WEAP in a one-entry LVLI at level 1 and count 1") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    loadWrapperEnvironment(backend, root);

    const auto message = textResult(backend.copyAsWrapper(nlohmann::json::array(
        {"Master.esm", "000800:Master.esm", "Target.esp", "WrapWeapon"})));
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Target.esp"})).has_value());

    const auto reloaded = fo4::read_plugin_file(root / "Target.esp");
    const auto key = wrapperKeyFrom(message);
    const auto* wrapper = fo4::find_record(reloaded, key);
    REQUIRE(wrapper != nullptr);
    CHECK(wrapper->header.signature == fo4::Signature::from_string("LVLI"));
    const auto list = fo4::forms::decode_leveled_list(reloaded, key);
    REQUIRE(list.entries.size() == 1);
    CHECK(list.entries[0].item == fo4::FormKey::parse("000800:Master.esm"));
    CHECK(list.entries[0].level == 1);
    CHECK(list.entries[0].count == 1);
}

TEST_CASE("Native CopyAsWrapper wraps an NPC_ in an LVLN") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    loadWrapperEnvironment(backend, root);

    const auto message = textResult(backend.copyAsWrapper(nlohmann::json::array(
        {"Master.esm", "000801:Master.esm", "Target.esp", nullptr})));
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Target.esp"})).has_value());

    const auto reloaded = fo4::read_plugin_file(root / "Target.esp");
    const auto key = wrapperKeyFrom(message);
    const auto* wrapper = fo4::find_record(reloaded, key);
    REQUIRE(wrapper != nullptr);
    CHECK(wrapper->header.signature == fo4::Signature::from_string("LVLN"));
    const auto list = fo4::forms::decode_leveled_list(reloaded, key);
    REQUIRE(list.entries.size() == 1);
    CHECK(list.entries[0].item == fo4::FormKey::parse("000801:Master.esm"));
}

TEST_CASE("Native CopyAsWrapper wraps a SPEL in an LVSP") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    loadWrapperEnvironment(backend, root);

    const auto message = textResult(backend.copyAsWrapper(nlohmann::json::array(
        {"Master.esm", "000802:Master.esm", "Target.esp", "WrapSpell"})));
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Target.esp"})).has_value());

    const auto reloaded = fo4::read_plugin_file(root / "Target.esp");
    const auto key = wrapperKeyFrom(message);
    const auto* wrapper = fo4::find_record(reloaded, key);
    REQUIRE(wrapper != nullptr);
    CHECK(wrapper->header.signature == fo4::Signature::from_string("LVSP"));
    const auto list = fo4::forms::decode_leveled_spell(reloaded, key);
    REQUIRE(list.entries.size() == 1);
    CHECK(list.entries[0].spell == fo4::FormKey::parse("000802:Master.esm"));
    CHECK(list.entries[0].level == 1);
    CHECK(list.entries[0].count == 1);
}

TEST_CASE("Native CopyAsWrapper refuses a CELL") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    loadWrapperEnvironment(backend, root);

    const auto rejected = backend.copyAsWrapper(nlohmann::json::array(
        {"Master.esm", "000803:Master.esm", "Target.esp", nullptr}));
    REQUIRE_FALSE(rejected.has_value());
    CHECK(rejected.error().type == "WrapperUnsupportedType");
}

TEST_CASE("Native CopyAsWrapper adds the source owner as a master of the target") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    fo4ide::plugin::RadiclePluginBackend backend;
    loadWrapperEnvironment(backend, root);

    const auto owner = fo4::FormKey::parse("000800:Master.esm").owner();
    const auto before = fo4::read_plugin_file(root / "Target.esp");
    CHECK_FALSE(std::any_of(before.header.masters.begin(), before.header.masters.end(),
                            [&](const auto& master) { return master.key == owner; }));

    textResult(backend.copyAsWrapper(nlohmann::json::array(
        {"Master.esm", "000800:Master.esm", "Target.esp", nullptr})));
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Target.esp"})).has_value());

    const auto after = fo4::read_plugin_file(root / "Target.esp");
    CHECK(std::any_of(after.header.masters.begin(), after.header.masters.end(),
                      [&](const auto& master) { return master.key == owner; }));
}
