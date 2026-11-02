#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/forms/quest.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/schema/vmad.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

std::vector<std::byte> questBytes(bool full, std::string_view master = {}) {
    fo4::test::ByteWriter vmad;
    vmad.u16(6);
    vmad.u16(2);
    vmad.u16(0);
    vmad.u8(3);
    vmad.u16(1);
    vmad.u16(0);
    vmad.u16(10);
    vmad.u16(0);
    vmad.u32(0);
    vmad.u8(0);
    vmad.u16(7);
    vmad.raw(full ? "QF_Base" : "QF_Test");
    vmad.u16(11);
    vmad.raw("Fragment_10");
    vmad.u16(0);

    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(2);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    if (!master.empty()) {
        std::string name(master);
        name.push_back('\0');
        fo4::test::write_subrecord(out, "MAST", std::string_view{name});
        fo4::test::write_subrecord(out, "DATA", std::vector<std::byte>(8));
    }
    fo4::test::end_record(out, tes4);
    const auto group = fo4::test::begin_group(out, "QUST", 0);
    const auto record = fo4::test::begin_record(out, "QUST", full ? 0 : 0x4000, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", full ? std::string_view{"MasterQuest\0", 12}
                                               : std::string_view{"PartialQuest\0", 13});
    fo4::test::write_subrecord(out, "VMAD", vmad.bytes);
    if (full) {
        fo4::test::write_subrecord(out, "DNAM", std::vector<std::byte>(12));
        fo4::test::ByteWriter stage;
        stage.u16(10);
        stage.u8(0);
        stage.u8(0);
        fo4::test::write_subrecord(out, "INDX", stage.bytes);
        fo4::test::write_subrecord(out, "QSDT", std::vector<std::byte>{std::byte{0}});
        fo4::test::write_subrecord(out, "NAM2", std::string_view{"Master stage\0", 13});
        fo4::test::ByteWriter objective;
        objective.u16(5);
        fo4::test::write_subrecord(out, "QOBJ", objective.bytes);
        fo4::test::write_subrecord(out, "FNAM", std::vector<std::byte>(4));
        fo4::test::write_subrecord(out, "NNAM", std::string_view{"Master objective\0", 17});
    }
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

void writeBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::ofstream stream(path, std::ios::binary);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

}

TEST_CASE("Native SetQuestStages preserves partial-form master fragments through save and reload") {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("fo4ide_quest_partial_" + std::to_string(id));
    Cleanup cleanup{root};
    std::filesystem::create_directories(root);
    const auto path = root / "Patch.esp";

    const auto bytes = questBytes(false);
    writeBytes(path, bytes);
    const auto original = fo4::read_plugin_file(path);
    const auto key = fo4::FormKey::parse("000800:Patch.esp");
    const auto vmad = fo4::find_record(original, key)->subrecords[1].data;
    const auto readBytes = [&] {
        return fo4::test::original_bytes(fo4::read_plugin_file(path));
    };
    const auto load = [&](fo4ide::plugin::RadiclePluginBackend& backend) {
        REQUIRE(backend.invoke(
            "environment.LoadMo2",
            nlohmann::json{{"plugins", nlohmann::json::array({{
                {"name", "Patch.esp"}, {"path", path.string()},
                {"source", "QuestTest"}, {"resolved", true}
            }})}}).has_value());
    };

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend);
    REQUIRE(backend.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({"Patch.esp", "000800:Patch.esp", nlohmann::json::array()})
    ).has_value());
    CHECK(readBytes() == bytes);
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto saved = readBytes();
    CHECK(saved == bytes);
    const auto disk = fo4::read_plugin_file(path);
    const auto* quest = fo4::find_record(disk, key);
    REQUIRE(quest != nullptr);
    CHECK(quest->header.flags.test(0x4000));
    REQUIRE(quest->subrecords.size() == 2u);
    CHECK(quest->subrecords[0].signature.to_string() == "EDID");
    CHECK(quest->subrecords[1].signature == fo4::sig_vmad);
    CHECK(quest->subrecords[1].data == vmad);
    CHECK(fo4::forms::decode_quest_stages(disk, key).stages.empty());

    fo4ide::plugin::RadiclePluginBackend reloaded;
    load(reloaded);
    const auto added = reloaded.invokeBackend(
        "SetQuestStages",
        nlohmann::json::array({
            "Patch.esp", "000800:Patch.esp",
            nlohmann::json::array({{{"index", 10}}})
        }));
    REQUIRE_FALSE(added.has_value());
    CHECK(added.error().message.find("master stages") != std::string::npos);
    CHECK(readBytes() == saved);
    REQUIRE(reloaded.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(readBytes() == saved);
}

TEST_CASE("Native full master override makes partial quest stages editable") {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("fo4ide_quest_promote_" + std::to_string(id));
    Cleanup cleanup{root};
    std::filesystem::create_directories(root);
    const auto masterPath = root / "Master.esm";
    const auto patchPath = root / "Patch.esp";
    const auto masterBytes = questBytes(true);
    const auto partialBytes = questBytes(false, "Master.esm");
    writeBytes(masterPath, masterBytes);
    writeBytes(patchPath, partialBytes);
    const auto key = fo4::FormKey::parse("000800:Master.esm");
    const auto source = fo4::read_plugin_file(masterPath);
    const auto* sourceQuest = fo4::find_record(source, key);
    REQUIRE(sourceQuest != nullptr);

    const auto environment = nlohmann::json{{"plugins", nlohmann::json::array({
        {{"name", "Master.esm"}, {"path", masterPath.string()},
         {"source", "QuestTest"}, {"resolved", true}},
        {{"name", "Patch.esp"}, {"path", patchPath.string()},
         {"source", "QuestTest"}, {"resolved", true}}
    })}};
    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
    const auto skipped = backend.invokeBackend(
        "CopyAsOverride", nlohmann::json::array({
            "Master.esm", key.to_string(), "Patch.esp", false
        }));
    REQUIRE(skipped.has_value());
    CHECK(skipped->get<std::string>().starts_with("EXISTS:"));
    REQUIRE_FALSE(backend.invokeBackend(
        "SetQuestStages", nlohmann::json::array({
            "Patch.esp", key.to_string(), nlohmann::json::array({{{"index", 10}}})
        })).has_value());

    REQUIRE(backend.invokeBackend(
        "CopyAsOverride", nlohmann::json::array({
            "Master.esm", key.to_string(), "Patch.esp", true
        })).has_value());
    const auto entries = nlohmann::json::array({{{"note", "Master stage"}}});
    REQUIRE(backend.invokeBackend(
        "SetQuestStages", nlohmann::json::array({
            "Patch.esp", key.to_string(), nlohmann::json::array({
                {{"index", 15}, {"logEntries", entries}}, {{"index", 20}}
            })
        })).has_value());
    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(patchPath)) == partialBytes);
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto saved = fo4::read_plugin_file(patchPath);
    const auto* quest = fo4::find_record(saved, key);
    REQUIRE(quest != nullptr);
    CHECK_FALSE(quest->header.flags.test(0x4000));
    const auto stages = fo4::forms::decode_quest_stages(saved, key);
    REQUIRE(stages.stages.size() == 2u);
    CHECK(stages.stages[0].index == 15);
    CHECK(stages.stages[1].index == 20);
    REQUIRE(stages.stages[0].log_entries.size() == 1u);
    CHECK(stages.stages[0].log_entries[0].note == "Master stage");
    auto expectedVmad = sourceQuest->subrecords[1].data;
    const auto layout = fo4::inspect_quest_vmad(expectedVmad);
    REQUIRE_FALSE(layout.unparseable);
    REQUIRE(layout.fragments.size() == 1u);
    expectedVmad[layout.fragments[0].stage_offset] = std::byte{15};
    CHECK(quest->subrecords[1].data == expectedVmad);
    for (const auto index : {0u, 2u, 6u, 7u, 8u}) {
        const auto& original = sourceQuest->subrecords[index];
        const auto found = std::find_if(quest->subrecords.begin(), quest->subrecords.end(),
            [&](const auto& member) { return member.signature == original.signature; });
        REQUIRE(found != quest->subrecords.end());
        CHECK(found->data == original.data);
    }
    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(masterPath)) == masterBytes);

    fo4ide::plugin::RadiclePluginBackend reloaded;
    REQUIRE(reloaded.invoke("environment.LoadMo2", environment).has_value());
    REQUIRE_FALSE(reloaded.invokeBackend(
        "SetQuestStages", nlohmann::json::array({
            "Patch.esp", key.to_string(), nlohmann::json::array({{{"index", 20}}})
        })).has_value());
    REQUIRE(reloaded.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(patchPath)) ==
          fo4::test::original_bytes(saved));
    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(masterPath)) == masterBytes);
}
