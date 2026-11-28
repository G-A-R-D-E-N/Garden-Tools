#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/forms/perk.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "RadiclePluginPerkFixture.hpp"
#include "plugin_bytes.hpp"

using namespace perk_fixture;

namespace {

std::vector<std::pair<std::string, std::vector<std::byte>>> perkSubrecords(const std::filesystem::path& path,
                                                                          const std::string& formKey) {
    const auto plugin = fo4::read_plugin_file(path);
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(formKey));
    REQUIRE(record != nullptr);
    std::vector<std::pair<std::string, std::vector<std::byte>>> out;
    for (const auto& subrecord : record->subrecords) {
        out.emplace_back(subrecord.signature.to_string(), subrecord.data);
    }
    return out;
}

std::vector<std::byte> subrecordBytes(const std::filesystem::path& path,
                                      const std::string& formKey,
                                      std::string_view signature) {
    const auto subrecords = perkSubrecords(path, formKey);
    const auto found = std::find_if(subrecords.begin(), subrecords.end(),
                                    [&](const auto& subrecord) { return subrecord.first == signature; });
    if (found == subrecords.end()) FAIL("PERK has no " << signature << " subrecord");
    return found->second;
}

std::vector<std::byte> u32Bytes(std::uint32_t value) {
    std::vector<std::byte> bytes(sizeof(value));
    std::memcpy(bytes.data(), &value, sizeof(value));
    return bytes;
}

std::vector<std::byte> makeLocalizedPatchReferencing40() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0x80u, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(1);
    hedr.u32(0x910);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::write_subrecord(out, "MAST", std::string_view{"Master.esp\0", 11});
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
    fo4::test::end_record(out, tes4);
    const auto group = fo4::test::begin_group(out, "PERK", 0);
    const auto record = fo4::test::begin_record(out, "PERK", 0, 0x00000900);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Referenced\0", 11});
    fo4::test::write_subrecord(out, "FULL", std::string_view{"\x28\x00\x00\x00", 4});
    fo4::test::end_record(out, record);
    fo4::test::end_group(out, group);
    return out.bytes;
}

}  // namespace

// Detects: a get_perk_effects result fed back into set_perk_effects must leave every PERK subrecord byte-identical, because the typed fields encode the same bytes as data.
TEST_CASE("Native get_perk_effects output set back unchanged leaves the PERK byte-identical") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(patchPath, makePatchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterPath, patchPath);
    const auto formKey = createPerk(backend, perkPayload(nlohmann::json::array({
        entryPoint(1, {{"type", 1}, {"value", 2.5}}),
        entryPoint(4, {{"type", 2}, {"value", 1.5}, {"value2", -2.0}}),
        entryPoint(5, {{"type", 5}, {"form", "MasterSpell"}}),
        entryPoint(6, {{"type", 6}, {"text", "Hi"}}),
        entryPoint(5, {{"type", 8}, {"actorValue", "MasterActorValue"}, {"value", 3.0}}),
    })));
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto before = perkSubrecords(patchPath, formKey);

    const auto listed = readEffects(backend, formKey);
    const auto set = backend.setPerkEffects(nlohmann::json::array({"Patch.esp", formKey, listed}));
    REQUIRE(set.has_value());
    CHECK(set->get<std::string>().starts_with("No change"));

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    REQUIRE(backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "Patch.esp", "PERK", nlohmann::json{{"editorId", "DirtyPerk"}}
    })).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    CHECK(perkSubrecords(patchPath, formKey) == before);
}

// Detects: editing a returned PERK effect must preserve its local EPFD form when a tab condition adds a master, while rejecting altered raw data.
TEST_CASE("Native PERK effect round trip rebases typed EPFD data when a condition adds a master") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto otherPath = root / "Other.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(otherPath, makeOtherPlugin());
    writeBytes(patchPath, makeOtherPlugin());
    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackendWithOther(backend, masterPath, patchPath, otherPath);
    const auto formKey = createPerk(backend, perkPayload(nlohmann::json::array({
        entryPoint(5, {{"type", 5}, {"form", "000E00:Patch.esp"}})
    })));
    auto effects = readEffects(backend, formKey);
    effects[0]["tabs"] = nlohmann::json::array({{{"runOn", 0}, {"conditions", nlohmann::json::array({
        {{"function", "GetDistance"}, {"param1", "000E00:Other.esp"}}
    })}}});
    auto invalid = effects;
    invalid[0]["parameters"]["data"] = "010e0000";
    const auto rejected = backend.setPerkEffects(nlohmann::json::array({"Patch.esp", formKey, invalid}));
    REQUIRE_FALSE(rejected.has_value());
    CHECK(rejected.error().type == "InvalidPerkEffects");
    REQUIRE(backend.setPerkEffects(nlohmann::json::array({"Patch.esp", formKey, effects})).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto plugin = fo4::read_plugin_file(patchPath);
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(formKey));
    REQUIRE(record != nullptr);
    const auto saved = fo4::forms::read_perk_effects(plugin, *record);
    REQUIRE(saved.size() == 1);
    REQUIRE(fo4::forms::perk_epfd_form(plugin, saved[0]).has_value());
    CHECK(*fo4::forms::perk_epfd_form(plugin, saved[0]) == fo4::FormKey::parse("000E00:Patch.esp"));
    REQUIRE(saved[0].tabs.size() == 1);
    REQUIRE(saved[0].tabs[0].conditions.size() == 1);
    CHECK(static_cast<std::uint32_t>(saved[0].tabs[0].conditions[0].param1) == 0x00000E00);
}

// Detects: parameters that give data and typed fields must be refused by name when the data differs from the bytes the typed fields encode, and accepted when they match.
TEST_CASE("Native PERK parameter data must match the typed fields it is given with") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(patchPath, makePatchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterPath, patchPath);
    const auto formKey = createPerk(backend, perkPayload(nlohmann::json::array()));

    const auto mismatched = backend.setPerkEffects(nlohmann::json::array({
        "Patch.esp", formKey, nlohmann::json::array({entryPoint(1, {{"type", 1}, {"data", "00000000"}, {"value", 2.5}})})
    }));
    REQUIRE_FALSE(mismatched.has_value());
    CHECK(mismatched.error().type == "InvalidPerkEffects");
    CHECK(mismatched.error().message.find("does not match") != std::string::npos);

    const auto matching = backend.setPerkEffects(nlohmann::json::array({
        "Patch.esp", formKey, nlohmann::json::array({entryPoint(1, {{"type", 1}, {"data", "00002040"}, {"value", 2.5}})})
    }));
    REQUIRE(matching.has_value());
    CHECK(readEffects(backend, formKey)[0]["parameters"]["data"] == "00002040");
}

// Detects: changing a local PERK link to the same local ID in a new master must stage the master even when link bytes are equal.
TEST_CASE("Native SetPerkNext stages a master when the old and new raw links match") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto otherPath = root / "Other.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(otherPath, makeOtherPlugin());
    writeBytes(patchPath, makeOtherPlugin());
    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackendWithOther(backend, masterPath, patchPath, otherPath);
    const auto perk = createPerk(backend, {{"editorId", "Links"}});
    REQUIRE(backend.setPerkNext(nlohmann::json::array({"Patch.esp", perk, "000C00:Patch.esp"})).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto before = subrecordBytes(patchPath, perk, "NNAM");
    const auto updated = backend.setPerkNext(nlohmann::json::array({"Patch.esp", perk, "000C00:Other.esp"}));
    REQUIRE(updated.has_value());
    CHECK_FALSE(updated->get<std::string>().starts_with("No change"));
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto plugin = fo4::read_plugin_file(patchPath);
    REQUIRE(plugin.header.masters.size() == 1);
    CHECK(plugin.header.masters[0].key.filename() == "Other.esp");
    CHECK(subrecordBytes(patchPath, perk, "NNAM") == before);
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(perk));
    REQUIRE(record != nullptr);
    CHECK(fo4::forms::read_perk_links(plugin, *record).next_perk == fo4::FormKey::parse("000C00:Other.esp"));
}

// Detects: set_perk_next must refuse a link to the same PERK and a null next, naming InvalidPerkNext and leaving the record unchanged.
TEST_CASE("Native SetPerkNext refuses a self link and a null next") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(patchPath, makePatchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterPath, patchPath);
    const auto perk = createPerk(backend, nlohmann::json{{"editorId", "SelfPerk"}});

    const auto self = backend.setPerkNext(nlohmann::json::array({"Patch.esp", perk, perk}));
    REQUIRE_FALSE(self.has_value());
    CHECK(self.error().type == "InvalidPerkNext");
    const auto empty = backend.setPerkNext(nlohmann::json::array({"Patch.esp", perk, ""}));
    REQUIRE_FALSE(empty.has_value());
    CHECK(empty.error().type == "InvalidPerkNext");
    const auto links = backend.getPerkLinks(nlohmann::json::array({"Patch.esp", perk}));
    REQUIRE(links.has_value());
    CHECK((*links)["nextPerk"].is_null());
}

// Detects: EPFD text, SWF, inline FULL and buttonLabel must refuse an embedded NUL instead of storing it cut short.
TEST_CASE("Native PERK text refuses an embedded NUL byte") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(patchPath, makePatchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterPath, patchPath);
    const std::string withNul("a\0b", 3);

    const auto inlineFull = backend.invokeBackend("CreateRecord", nlohmann::json::array({
        "Patch.esp", "PERK", nlohmann::json{{"editorId", "NulFull"}, {"full", withNul}}
    }));
    REQUIRE_FALSE(inlineFull.has_value());
    CHECK(inlineFull.error().type == "InvalidRecordCreationPayload");

    const auto formKey = createPerk(backend, perkPayload(nlohmann::json::array()));
    const auto epfdText = backend.setPerkEffects(nlohmann::json::array({
        "Patch.esp", formKey, nlohmann::json::array({entryPoint(6, {{"type", 6}, {"text", withNul}})})
    }));
    REQUIRE_FALSE(epfdText.has_value());
    CHECK(epfdText.error().type == "InvalidPerkEffects");

    const auto buttonLabel = backend.setPerkEffects(nlohmann::json::array({
        "Patch.esp", formKey, nlohmann::json::array({entryPoint(1, {{"buttonLabel", withNul}})})
    }));
    REQUIRE_FALSE(buttonLabel.has_value());
    CHECK(buttonLabel.error().type == "InvalidPerkEffects");

    const auto swf = backend.setPerkSwf(nlohmann::json::array({"Patch.esp", formKey, withNul}));
    REQUIRE_FALSE(swf.has_value());
    CHECK(swf.error().type == "InvalidPerkSwf");
}

// Detects: a localized PERK must take string IDs one past the maximum across every language's tables and staged tables, so a second language never reuses an ID the first allocated.
TEST_CASE("Native localized PERK string IDs never collide across languages") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(patchPath, makePatchPlugin(0x80));
    writeStringTable(root / "Strings" / "Patch_de.STRINGS", fo4::localization::StringTableKind::strings, 5, "Bestehend");
    writeStringTable(root / "Strings" / "Patch_en.STRINGS", fo4::localization::StringTableKind::strings, 1, "Englisch");

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterPath, patchPath);
    backend.testSetJournalRoot(root / "journal");
    const auto english = createPerk(backend, nlohmann::json{{"editorId", "AlphaPerk"}, {"full", "Alpha"}});
    const auto german = createPerk(backend, nlohmann::json{
        {"editorId", "BetaPerk"}, {"full", "Beta"}, {"language", "de"}
    });
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    CHECK(subrecordBytes(patchPath, english, "FULL") == u32Bytes(6));
    CHECK(subrecordBytes(patchPath, german, "FULL") == u32Bytes(7));
}

// Detects: a new localized string ID must be one past every ID a record of the plugin already references, so a new FULL never reuses an existing ID.
TEST_CASE("Native localized PERK text takes an ID past every ID the plugin's records reference") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(patchPath, makeLocalizedPatchReferencing40());
    writeStringTable(root / "Strings" / "Patch_en.STRINGS", fo4::localization::StringTableKind::strings, 1, "Englisch");

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterPath, patchPath);
    backend.testSetJournalRoot(root / "journal");
    const auto formKey = createPerk(backend, nlohmann::json{{"editorId", "FreshPerk"}, {"full", "Fresh"}});
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    CHECK(subrecordBytes(patchPath, formKey, "FULL") == u32Bytes(41));
}
