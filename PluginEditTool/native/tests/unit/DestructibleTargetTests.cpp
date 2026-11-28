#include "../helpers/TempRoot.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/plugin/destructible_stage.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/app/DestructibleTarget.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;
using json = nlohmann::json;

std::filesystem::path tempRoot() {
    return std::filesystem::temp_directory_path() / ("fo4ide_destructible_" + fo4ide_test::uniqueTempName());
}

struct Cleanup {
    std::filesystem::path root;
    ~Cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void writeBytes(const std::filesystem::path& path, const Bytes& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(stream.good());
}

Bytes destPayload(std::int32_t health, std::uint8_t stages, std::uint8_t flags) {
    ByteWriter out;
    out.i32(health);
    out.u8(stages);
    out.u8(flags);
    out.u16(0);
    return out.bytes;
}

Bytes dstdPayload(std::uint8_t healthPercent, std::uint8_t index) {
    ByteWriter out;
    out.u8(healthPercent);
    out.u8(index);
    out.u8(0);
    out.u8(0);
    out.i32(0);
    out.u32(0);
    out.u32(0);
    out.i32(0);
    return out.bytes;
}

Bytes destructiblePluginBytes() {
    ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0, 131);
    ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x900);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    for (const std::string_view master : {std::string_view{"Fallout4.esm\0", 13}, std::string_view{"Objects.esp\0", 12}}) {
        fo4::test::write_subrecord(out, "MAST", master);
        fo4::test::write_subrecord(out, "DATA", Bytes(8));
    }
    fo4::test::end_record(out, tes4);

    const auto acti = fo4::test::begin_group(out, "ACTI", 0);
    const auto grouped = fo4::test::begin_record(out, "ACTI", 0, 0x02000A01u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Destr\0", 6});
    fo4::test::write_subrecord(out, "DEST", destPayload(1500, 2, 1));
    ByteWriter damc;
    damc.u32(0x01000A10u);
    damc.u32(5);
    damc.u32(0x01000A11u);
    damc.u32(7);
    fo4::test::write_subrecord(out, "DAMC", damc.bytes);
    fo4::test::write_subrecord(out, "DSTD", dstdPayload(10, 0));
    fo4::test::write_subrecord(out, "DSTA", std::string_view{"seq0\0", 5});
    fo4::test::write_subrecord(out, "DSTF", Bytes{});
    fo4::test::write_subrecord(out, "DSTD", dstdPayload(50, 1));
    fo4::test::write_subrecord(out, "DMDL", std::string_view{"a.nif\0", 6});
    fo4::test::write_subrecord(out, "DSTF", Bytes{});
    fo4::test::end_record(out, grouped);
    const auto bare = fo4::test::begin_record(out, "ACTI", 0, 0x02000A04u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Bare\0", 5});
    fo4::test::end_record(out, bare);
    fo4::test::end_group(out, acti);

    const auto stat = fo4::test::begin_group(out, "STAT", 0);
    const auto prop = fo4::test::begin_record(out, "STAT", 0, 0x02000A03u, 131);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Prop\0", 5});
    fo4::test::end_record(out, prop);
    fo4::test::end_group(out, stat);
    return out.bytes;
}

json pluginEntry(std::string name, const std::filesystem::path& path, std::string source) {
    return {{"name", std::move(name)}, {"path", path.string()}, {"source", std::move(source)}, {"resolved", true}};
}

void load(fo4ide::plugin::RadiclePluginBackend& backend, const json& plugins) {
    REQUIRE(backend.invoke("environment.LoadMo2", json{{"plugins", plugins}}).has_value());
}

json send(fo4ide::app::DestructibleTarget& target, std::string_view method, json args = json::array()) {
    const auto reply = target.invoke(method, args);
    REQUIRE(reply.has_value());
    REQUIRE(reply->is_string());
    auto parsed = json::parse(reply->get<std::string>(), nullptr, false);
    REQUIRE_FALSE(parsed.is_discarded());
    return parsed;
}

const fo4::Record& diskRecord(const fo4::Plugin& plugin, std::string_view key) {
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(key));
    REQUIRE(record != nullptr);
    return *record;
}

}  // namespace

TEST_CASE("DestructibleTarget creates, edits and saves a destructible group") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Patch.esp";
    writeBytes(path, destructiblePluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, json::array({pluginEntry("Patch.esp", path, "DestructibleTest")}));
    fo4ide::app::DestructibleTarget target(backend);

    const auto opened = send(target, "OpenGroupEditor", json::array({path.string()}));
    CHECK(opened.at("plugin") == "Patch.esp");
    CHECK(opened.at("dirty") == false);
    CHECK(opened.at("formId").is_null());
    CHECK(opened.at("group").is_null());
    CHECK(opened.at("error").is_null());

    const auto read = send(target, "ReadGroup", json::array({"000A01"}));
    CHECK(read.at("formId") == "000A01");
    CHECK(read.at("error").is_null());
    const auto& group = read.at("group");
    CHECK(group.at("status") == "Present");
    CHECK(group.at("header").at("health") == 1500);
    CHECK(group.at("header").at("stageCount") == 2);
    CHECK(group.at("header").at("flags") == 1);
    REQUIRE(group.at("resistances").size() == 2);
    CHECK(group.at("resistances")[0].at("damageType") == "000A10:Objects.esp");
    CHECK(group.at("resistances")[1].at("value") == 7);
    REQUIRE(group.at("stages").size() == 2);
    CHECK(group.at("stages")[0].at("members").size() == 3);
    CHECK(group.at("stages")[0].at("members")[1].at("signature") == "DSTA");
    for (const auto* field : {"healthPercent", "index", "modelDamageStage", "flags", "selfDamage", "debrisCount"}) {
        INFO(field);
        REQUIRE(group.at("stages")[0].at("values").contains(field));
        CHECK_FALSE(group.at("stages")[0].at("values").at(field).get<std::string>().empty());
    }
    CHECK(group.at("stages")[0].at("values").at("healthPercent") == "10");

    const auto header = send(target, "SetGroupHeader", json::array({"000A01", 2000, 3}));
    CHECK(header.at("dirty") == true);
    CHECK(header.at("error").is_null());
    CHECK(header.at("group").at("header").at("health") == 2000);
    CHECK(header.at("group").at("header").at("flags") == 3);

    const auto stage = send(target, "InsertGroupStage", json::array({"000A01", 2}));
    CHECK(stage.at("error").is_null());
    CHECK(stage.at("group").at("stages").size() == 3);
    CHECK(stage.at("group").at("header").at("stageCount") == 3);
    const auto newStageMembers = stage.at("group").at("stages")[2].at("members").size();
    const auto member = send(target, "InsertGroupStageMember", json::array({"000A01", 2, "DMDL"}));
    CHECK(member.at("error").is_null());
    CHECK(member.at("group").at("stages")[2].at("members").size() == newStageMembers + 1);

    CHECK(send(target, "SetGroupStageValue", json::array({"000A01", 0, "healthPercent", "75"})).at("error").is_null());
    CHECK(send(target, "SetGroupResistanceValue", json::array({"000A01", 1, "9"})).at("error").is_null());
    const auto link = send(target, "SetGroupResistanceDamageType", json::array({"000A01", 0, "000A12:Objects.esp"}));
    CHECK(link.at("error").is_null());
    CHECK(link.at("group").at("resistances")[0].at("damageType") == "000A12:Objects.esp");
    CHECK(link.at("group").at("resistances")[1].at("value") == 9);

    const auto created = send(target, "CreateGroupRecord", json::array({"ARMO", "DestrNewArmor"}));
    CHECK(created.at("error").is_null());
    const auto newId = created.at("formId").get<std::string>();
    REQUIRE(newId.size() == 6);
    CHECK(newId.find_first_not_of("0123456789ABCDEF") == std::string::npos);
    CHECK(created.at("group").at("status") == "Present");
    CHECK(created.at("group").at("stages").size() == 0);
    CHECK(send(target, "InsertGroupStage", json::array({newId, 0})).at("group").at("stages").size() == 1);

    const auto saved = send(target, "SaveGroup");
    CHECK(std::filesystem::equivalent(saved.at("saved").get<std::string>(), path));
    CHECK(saved.at("reloaded") == true);
    CHECK(saved.at("plugin") == "Patch.esp");
    CHECK(saved.at("dirty") == false);
    CHECK_FALSE(saved.contains("group"));
    CHECK_FALSE(saved.contains("error"));

    const auto unchanged = send(target, "SetGroupHeader", json::array({"000A01", 2000, 3}));
    CHECK(unchanged.at("dirty") == false);
    CHECK(unchanged.at("error").is_null());

    const auto disk = fo4::read_plugin_file(path);
    fo4::DestructibleGroupView view;
    REQUIRE(fo4::read_destructible_group(disk, fo4::FormKey::parse("000A01:Patch.esp"), view) ==
            fo4::DestructibleGroupReadStatus::present);
    CHECK(view.health == 2000);
    CHECK(view.flags == 3);
    CHECK(view.stage_count == 3);
    REQUIRE(view.resistances.size() == 2);
    CHECK(view.resistances[0].damage_type == fo4::FormKey::parse("000A12:Objects.esp"));
    CHECK(view.resistances[1].value == 9);
    REQUIRE(view.stages.size() == 3);
    REQUIRE(!view.stages[0].members.empty());
    CHECK(view.stages[0].members[0].signature == fo4::Signature::from_string("DSTD"));
    CHECK(view.stages[0].members[0].data[0] == std::byte{75});
    CHECK(view.stages[2].members.size() == newStageMembers + 1);

    const auto& armor = diskRecord(disk, newId + ":Patch.esp");
    CHECK(armor.header.signature == fo4::Signature::from_string("ARMO"));
    fo4::DestructibleGroupView armorView;
    REQUIRE(fo4::read_destructible_group(disk, fo4::FormKey::parse(newId + ":Patch.esp"), armorView) ==
            fo4::DestructibleGroupReadStatus::present);
    CHECK(armorView.stages.size() == 1);
}

TEST_CASE("DestructibleTarget refuses what the panel must see as refusals") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto patchPath = root / "Patch.esp";
    const auto protectedPath = root / "Protected.esp";
    writeBytes(patchPath, destructiblePluginBytes());
    writeBytes(protectedPath, destructiblePluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    load(backend, json::array({
        pluginEntry("Patch.esp", patchPath, "DestructibleTest"),
        pluginEntry("Protected.esp", protectedPath, "data"),
    }));
    fo4ide::app::DestructibleTarget target(backend);

    const auto closed = send(target, "ReadGroup", json::array({"000A01"}));
    CHECK(closed.at("error").get<std::string>().find("no plugin is open") != std::string::npos);
    CHECK_FALSE(closed.contains("group"));
    CHECK(send(target, "OpenGroupEditor", json::array({""})).at("error") == "A plugin path is required.");

    REQUIRE(send(target, "OpenGroupEditor", json::array({patchPath.string()})).at("error").is_null());

    const auto noHeader = send(target, "ReadGroup", json::array({"000A04"}));
    CHECK(noHeader.at("error").is_null());
    CHECK(noHeader.at("group").at("status") == "HeaderMissing");
    CHECK(noHeader.at("group").at("error") == "The record has no DEST header, so it has no destructible group.");
    CHECK(noHeader.at("group").at("header").is_null());
    CHECK(noHeader.at("group").at("resistances").is_null());
    CHECK(noHeader.at("group").at("stages").is_null());

    const auto refusedStage = send(target, "InsertGroupStage", json::array({"000A04", 0}));
    CHECK(refusedStage.at("error") == "HeaderMissing");
    CHECK(refusedStage.at("dirty") == false);
    CHECK(refusedStage.at("group").at("status") == "HeaderMissing");

    const auto wrongType = send(target, "ReadGroup", json::array({"000A03"}));
    CHECK(wrongType.at("error") == "record type cannot carry destructible stages");
    CHECK_FALSE(wrongType.contains("group"));
    const auto missing = send(target, "ReadGroup", json::array({"000AFF"}));
    CHECK(missing.at("error") == "plugin has no such record");
    CHECK_FALSE(missing.contains("group"));
    CHECK(send(target, "ReadGroup", json::array({"0x"})).at("error") == "A record form ID is required.");

    CHECK(send(target, "SetGroupHeader", json::array({"000A01", nullptr, 1})).at("error") ==
          "The DEST health must be a whole number.");
    CHECK(send(target, "SetGroupHeader", json::array({"000A01", 5, 300})).at("error") == "The DEST flags byte is a u8.");

    const auto notOptional = send(target, "InsertGroupStageMember", json::array({"000A01", 0, "DSTD"}));
    CHECK(notOptional.at("error") == "NotOptional");
    CHECK(notOptional.at("group").at("status") == "Present");
    const auto present = send(target, "InsertGroupStageMember", json::array({"000A01", 0, "DSTA"}));
    CHECK(present.at("error").is_null());
    CHECK(present.at("dirty") == false);
    CHECK(send(target, "InsertGroupStage", json::array({"000A01", 9})).at("error") == "IndexOutOfRange");

    const auto badLink = send(target, "SetGroupResistanceDamageType", json::array({"000A01", 5, "000A12:Objects.esp"}));
    CHECK_FALSE(badLink.at("error").get<std::string>().empty());
    CHECK_FALSE(badLink.contains("group"));
    CHECK(send(target, "SetGroupStageValue", json::array({"000A01", 0, "nope", "1"})).at("error").get<std::string>()
              .find("nope") != std::string::npos);

    CHECK(send(target, "SetGroupHeader", json::array({"000A01", 1600, 1})).at("dirty") == true);
    const auto blocked = send(target, "OpenGroupEditor", json::array({protectedPath.string()}));
    CHECK(blocked.at("error").get<std::string>().find("unsaved edits") != std::string::npos);
    CHECK(blocked.at("plugin") == "Patch.esp");
    CHECK(blocked.at("dirty") == true);
    REQUIRE(send(target, "SaveGroup").at("dirty") == false);

    REQUIRE(send(target, "OpenGroupEditor", json::array({protectedPath.string()})).at("plugin") == "Protected.esp");
    const auto guarded = send(target, "SetGroupHeader", json::array({"000A01", 1, 1}));
    CHECK(guarded.at("error").get<std::string>().find("protected") != std::string::npos);
    CHECK_FALSE(guarded.contains("group"));
    const auto stillThere = send(target, "ReadGroup", json::array({"000A01"}));
    CHECK(stillThere.at("group").at("header").at("health") == 1500);
    CHECK(stillThere.at("dirty") == false);
}

TEST_CASE("DestructibleTarget opens an ad hoc path and saves it in place") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto path = root / "Adhoc.esp";
    writeBytes(path, destructiblePluginBytes());

    fo4ide::plugin::RadiclePluginBackend backend;
    fo4ide::app::DestructibleTarget target(backend);

    const auto opened = send(target, "OpenGroupEditor", json::array({path.string()}));
    REQUIRE(opened.at("error").is_null());
    CHECK(opened.at("plugin") == "Adhoc.esp");
    CHECK(opened.at("dirty") == false);

    const auto edited = send(target, "SetGroupHeader", json::array({"000A01", 321, 2}));
    CHECK(edited.at("error").is_null());
    CHECK(edited.at("dirty") == true);
    CHECK(edited.at("group").at("header").at("health") == 321);

    const auto saved = send(target, "SaveGroup");
    CHECK(saved.at("dirty") == false);
    CHECK(std::filesystem::equivalent(saved.at("saved").get<std::string>(), path));

    const auto disk = fo4::read_plugin_file(path);
    fo4::DestructibleGroupView view;
    REQUIRE(fo4::read_destructible_group(disk, fo4::FormKey::parse("000A01:Adhoc.esp"), view) ==
            fo4::DestructibleGroupReadStatus::present);
    CHECK(view.health == 321);
    CHECK(view.flags == 2);
}
