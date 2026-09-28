#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

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

}

TEST_CASE("Native SetQuestStages preserves partial-form master fragments through save and reload") {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("fo4ide_quest_partial_" + std::to_string(id));
    Cleanup cleanup{root};
    std::filesystem::create_directories(root);
    const auto path = root / "Patch.esp";

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
    vmad.raw("QF_Test");
    vmad.u16(11);
    vmad.raw("Fragment_10");
    vmad.u16(0);

    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x801);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
    const auto group = fo4::test::begin_group(out, "QUST", 0);
    const auto quest_record = fo4::test::begin_record(out, "QUST", 0x4000, 0x00000800);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"PartialQuest\0", 13});
    fo4::test::write_subrecord(out, "VMAD", vmad.bytes);
    fo4::test::end_record(out, quest_record);
    fo4::test::end_group(out, group);
    {
        std::ofstream stream(path, std::ios::binary);
        REQUIRE(stream.good());
        stream.write(reinterpret_cast<const char*>(out.bytes.data()),
                     static_cast<std::streamsize>(out.bytes.size()));
        REQUIRE(stream.good());
    }
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
    CHECK(readBytes() == out.bytes);
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto saved = readBytes();
    CHECK(saved == out.bytes);
    const auto disk = fo4::read_plugin_file(path);
    const auto key = fo4::FormKey::parse("000800:Patch.esp");
    const auto* quest = fo4::find_record(disk, key);
    REQUIRE(quest != nullptr);
    CHECK(quest->header.flags.test(0x4000));
    REQUIRE(quest->subrecords.size() == 2u);
    CHECK(quest->subrecords[0].signature.to_string() == "EDID");
    CHECK(quest->subrecords[1].signature == fo4::sig_vmad);
    CHECK(quest->subrecords[1].data == vmad.bytes);
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
