#include <algorithm>
#include "../helpers/TempRoot.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/mutation.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

std::filesystem::path renumberTempRoot() {
    const auto id = fo4ide_test::uniqueTempName();
    return std::filesystem::temp_directory_path() /
           ("fo4ide_renumber_" + id);
}

struct RenumberCleanup {
    std::filesystem::path root;
    ~RenumberCleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void renumberWriteBytes(const std::filesystem::path& path,
                        const std::vector<std::byte>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

std::vector<std::byte> renumberSourceBytes() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x1001u);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto colors = fo4::test::begin_group(out, "CLFM", 0);
    const auto color = fo4::test::begin_record(out, "CLFM", 0, 0x00001000u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"MoveFirst\0", 10});
    fo4::test::end_record(out, color);
    fo4::test::end_group(out, colors);

    const auto races = fo4::test::begin_group(out, "RACE", 0);
    const auto race = fo4::test::begin_record(out, "RACE", 0, 0x00000800u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"MoveSecond\0", 11});
    fo4::test::ByteWriter link;
    link.u32(0x00001000u);
    fo4::test::write_subrecord(out, "AHCM", link.bytes);
    fo4::test::end_record(out, race);
    fo4::test::end_group(out, races);
    return out.bytes;
}

std::vector<std::byte> injectMasterBytes() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x0A06u);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);

    const auto miscs = fo4::test::begin_group(out, "MISC", 0);
    const auto occupied = fo4::test::begin_record(out, "MISC", 0, 0x00000A05u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Occupied\0", 9});
    fo4::test::end_record(out, occupied);
    fo4::test::end_group(out, miscs);
    return out.bytes;
}

std::vector<std::byte> injectLightMasterBytes() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x800u);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
    return out.bytes;
}

std::vector<std::byte> injectSourceBytes() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(2);
    hedr.u32(0x802u);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::write_subrecord(out, "MAST", std::string_view{"Master.esp\0", 11});
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
    fo4::test::write_subrecord(out, "MAST", std::string_view{"Light.esl\0", 10});
    fo4::test::write_subrecord(out, "DATA", data.bytes);
    fo4::test::end_record(out, tes4);

    const auto miscs = fo4::test::begin_group(out, "MISC", 0);
    const auto target = fo4::test::begin_record(out, "MISC", 0, 0x02000800u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Target\0", 7});
    fo4::test::end_record(out, target);
    const auto referrer = fo4::test::begin_record(out, "MISC", 0, 0x02000801u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Referrer\0", 9});
    fo4::test::ByteWriter link;
    link.u32(0x02000800u);
    fo4::test::write_subrecord(out, "NAME", link.bytes);
    fo4::test::end_record(out, referrer);
    fo4::test::end_group(out, miscs);
    return out.bytes;
}

nlohmann::json injectLoadEntries(const std::filesystem::path& root,
                                 const std::vector<std::string>& names) {
    auto entries = nlohmann::json::array();
    for (const auto& name : names) {
        entries.push_back({
            {"name", name},
            {"path", (root / name).string()},
            {"source", "InjectTest"},
            {"resolved", true}
        });
    }
    return entries;
}

std::string injectHex(std::uint32_t value) {
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setw(6) << std::setfill('0') << value;
    return out.str();
}

std::uint32_t renumberRawSubrecord(const fo4::Record& record,
                                   std::string_view signature) {
    const auto wanted = fo4::Signature::from_string(signature);
    const fo4::Subrecord* found = nullptr;
    for (const auto& subrecord : record.subrecords) {
        if (subrecord.signature != wanted) continue;
        found = &subrecord;
        break;
    }
    if (found == nullptr) FAIL("missing requested FormLink subrecord");
    REQUIRE(found->data.size() == 4);
    std::uint32_t value = 0;
    std::memcpy(&value, found->data.data(), sizeof(value));
    return value;
}

}  // namespace

TEST_CASE("Native RenumberPluginFormIds vacates overlapping targets before rekeying dependents") {
    const auto root = renumberTempRoot();
    RenumberCleanup cleanup{root};
    const auto path = root / "Patch.esp";
    renumberWriteBytes(path, renumberSourceBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    const auto loaded = backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{
            "plugins",
            nlohmann::json::array({
                {
                    {"name", "Patch.esp"},
                    {"path", path.string()},
                    {"source", "RenumberTest"},
                    {"resolved", true}
                }
            })
        }});
    REQUIRE(loaded.has_value());

    const auto preview = backend.renumberPluginFormIds(
        nlohmann::json::array({"Patch.esp", "001000", false}));
    REQUIRE(preview.has_value());
    REQUIRE(preview->is_string());
    CHECK(preview->get<std::string>().starts_with("DRY RUN:"));
    CHECK(preview->get<std::string>().find("001000-001001") != std::string::npos);

    const auto beforeSave = fo4::test::original_bytes(fo4::read_plugin_file(path));

    const auto applied = backend.renumberPluginFormIds(
        nlohmann::json::array({"Patch.esp", "001000", true}));
    REQUIRE(applied.has_value());
    REQUIRE(applied->is_string());
    CHECK(applied->get<std::string>().find("Renumbered 2 record(s)") != std::string::npos);

    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(path)) == beforeSave);
    CHECK(applied->get<std::string>().find("SavePlugin") != std::string::npos);
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto disk = fo4::read_plugin_file(path);
    CHECK(fo4::find_record(disk, fo4::FormKey::parse("000800:Patch.esp")) == nullptr);

    const auto* race = fo4::find_record(
        disk, fo4::FormKey::parse("001000:Patch.esp"));
    REQUIRE(race != nullptr);
    CHECK(race->header.signature == fo4::Signature::from_string("RACE"));
    CHECK(renumberRawSubrecord(*race, "AHCM") == 0x00001001u);

    const auto* color = fo4::find_record(
        disk, fo4::FormKey::parse("001001:Patch.esp"));
    REQUIRE(color != nullptr);
    CHECK(color->header.signature == fo4::Signature::from_string("CLFM"));
}

TEST_CASE("Native InjectFormsIntoMaster dry run stages nothing") {
    const auto root = renumberTempRoot();
    RenumberCleanup cleanup{root};
    renumberWriteBytes(root / "Master.esp", injectMasterBytes());
    renumberWriteBytes(root / "Light.esl", injectLightMasterBytes());
    renumberWriteBytes(root / "Injector.esp", injectSourceBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2",
                           nlohmann::json{{"plugins", injectLoadEntries(
                               root, {"Master.esp", "Light.esl", "Injector.esp"})}}).has_value());

    const auto preview = backend.injectFormsIntoMaster(
        nlohmann::json::array({"Injector.esp", "Master.esp", "000A00", false}));
    REQUIRE(preview.has_value());
    REQUIRE(preview->is_string());
    CHECK(preview->get<std::string>().starts_with("DRY RUN:"));

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Injector.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(root / "Injector.esp");
    CHECK(fo4::find_record(disk, fo4::FormKey::parse("000800:Injector.esp")) != nullptr);
    CHECK(fo4::find_record(disk, fo4::FormKey::parse("000A00:Master.esp")) == nullptr);
}

TEST_CASE("Native InjectFormsIntoMaster apply then SavePlugin moves own records into the master") {
    const auto root = renumberTempRoot();
    RenumberCleanup cleanup{root};
    renumberWriteBytes(root / "Master.esp", injectMasterBytes());
    renumberWriteBytes(root / "Light.esl", injectLightMasterBytes());
    renumberWriteBytes(root / "Injector.esp", injectSourceBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2",
                           nlohmann::json{{"plugins", injectLoadEntries(
                               root, {"Master.esp", "Light.esl", "Injector.esp"})}}).has_value());

    const auto applied = backend.injectFormsIntoMaster(
        nlohmann::json::array({"Injector.esp", "Master.esp", "000A00", true}));
    REQUIRE(applied.has_value());
    REQUIRE(applied->is_string());
    CHECK(applied->get<std::string>().find("Injected 2 record(s)") != std::string::npos);
    CHECK(applied->get<std::string>().ends_with("SavePlugin to persist."));

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Injector.esp"})).has_value());
    const auto disk = fo4::read_plugin_file(root / "Injector.esp");
    CHECK(fo4::find_record(disk, fo4::FormKey::parse("000800:Injector.esp")) == nullptr);

    const auto* target = fo4::find_record(disk, fo4::FormKey::parse("000A00:Master.esp"));
    REQUIRE(target != nullptr);
    CHECK(target->header.form_id.value() == 0x00000A00u);

    const auto* referrer = fo4::find_record(disk, fo4::FormKey::parse("000A01:Master.esp"));
    REQUIRE(referrer != nullptr);
    CHECK(renumberRawSubrecord(*referrer, "NAME") == 0x00000A00u);
}

TEST_CASE("Native InjectFormsIntoMaster refuses a target id the master already uses") {
    const auto root = renumberTempRoot();
    RenumberCleanup cleanup{root};
    renumberWriteBytes(root / "Master.esp", injectMasterBytes());
    renumberWriteBytes(root / "Light.esl", injectLightMasterBytes());
    renumberWriteBytes(root / "Injector.esp", injectSourceBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2",
                           nlohmann::json{{"plugins", injectLoadEntries(
                               root, {"Master.esp", "Light.esl", "Injector.esp"})}}).has_value());
    const auto before = fo4::test::original_bytes(fo4::read_plugin_file(root / "Injector.esp"));

    const auto result = backend.injectFormsIntoMaster(
        nlohmann::json::array({"Injector.esp", "Master.esp", "000A04", true}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InjectTargetOccupied");
    CHECK(result.error().message.find("000A05:Master.esp") != std::string::npos);
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Injector.esp"})).has_value());
    CHECK(fo4::test::original_bytes(fo4::read_plugin_file(root / "Injector.esp")) == before);
}

TEST_CASE("Native InjectFormsIntoMaster refuses a master that is not loaded") {
    const auto root = renumberTempRoot();
    RenumberCleanup cleanup{root};
    renumberWriteBytes(root / "Master.esp", injectMasterBytes());
    renumberWriteBytes(root / "Light.esl", injectLightMasterBytes());
    renumberWriteBytes(root / "Injector.esp", injectSourceBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2",
                           nlohmann::json{{"plugins", injectLoadEntries(
                               root, {"Light.esl", "Injector.esp"})}}).has_value());

    const auto result = backend.injectFormsIntoMaster(
        nlohmann::json::array({"Injector.esp", "Master.esp", "000A00", true}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InjectMasterNotLoaded");
}

TEST_CASE("Native InjectFormsIntoMaster refuses ids past the light master's local range") {
    const auto root = renumberTempRoot();
    RenumberCleanup cleanup{root};
    renumberWriteBytes(root / "Master.esp", injectMasterBytes());
    renumberWriteBytes(root / "Light.esl", injectLightMasterBytes());
    renumberWriteBytes(root / "Injector.esp", injectSourceBytes());

    const auto lightBounds = fo4::local_form_id_range(fo4::read_plugin_file(root / "Light.esl"));
    REQUIRE(lightBounds.second == 0xFFFu);

    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2",
                           nlohmann::json{{"plugins", injectLoadEntries(
                               root, {"Master.esp", "Light.esl", "Injector.esp"})}}).has_value());

    const auto result = backend.injectFormsIntoMaster(nlohmann::json::array(
        {"Injector.esp", "Light.esl", injectHex(lightBounds.second), true}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "InjectRangeInvalid");
}
