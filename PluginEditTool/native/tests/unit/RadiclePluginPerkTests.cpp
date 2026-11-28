#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fo4/forms/perk.hpp>
#include <fo4/localization/strings.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "plugin_bytes.hpp"

#include "RadiclePluginPerkFixture.hpp"

using namespace perk_fixture;

// Detects: adding a master for a top-level condition must not retarget an already encoded local EPFD form.
TEST_CASE("Native PERK creation rebases effects when conditions add a master") {
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
    auto payload = perkPayload(nlohmann::json::array({
        entryPoint(5, {{"type", 5}, {"form", "000E00:Patch.esp"}})
    }));
    payload["conditions"] = nlohmann::json::array({
        {{"function", "GetDistance"}, {"param1", "000E00:Other.esp"}}
    });
    const auto formKey = createPerk(backend, payload);
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto plugin = fo4::read_plugin_file(patchPath);
    REQUIRE(plugin.header.masters.size() == 1);
    CHECK(plugin.header.masters[0].key.filename() == "Other.esp");
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(formKey));
    REQUIRE(record != nullptr);
    const auto effects = fo4::forms::read_perk_effects(plugin, *record);
    REQUIRE(effects.size() == 1);
    REQUIRE(fo4::forms::perk_epfd_form(plugin, effects[0]).has_value());
    CHECK(*fo4::forms::perk_epfd_form(plugin, effects[0]) == fo4::FormKey::parse("000E00:Patch.esp"));
}

// Detects: conditions and typed PERK references to a later plugin must report MasterOrder without adding a master.
TEST_CASE("Native PERK creation propagates condition master order errors") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto otherPath = root / "Other.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(otherPath, makeOtherPlugin());
    writeBytes(patchPath, makePatchPlugin());
    fo4ide::plugin::RadiclePluginBackend backend;
    REQUIRE(backend.invoke("environment.LoadMo2", {{"plugins", nlohmann::json::array({
        pluginEntry("Master.esp", masterPath), pluginEntry("Patch.esp", patchPath),
        pluginEntry("Other.esp", otherPath)
    })}}).has_value());
    auto payload = perkPayload(nlohmann::json::array());
    payload["conditions"] = nlohmann::json::array({
        {{"function", "GetDistance"}, {"param1", "000E00:Other.esp"}}
    });
    const auto result = backend.invokeBackend("CreateRecord", nlohmann::json::array({"Patch.esp", "PERK", payload}));
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().type == "MasterOrder");
    payload.erase("conditions");
    payload["nextPerk"] = "000C00:Other.esp";
    const auto typed = backend.invokeBackend("CreateRecord", nlohmann::json::array({"Patch.esp", "PERK", payload}));
    REQUIRE_FALSE(typed.has_value());
    CHECK(typed.error().type == "MasterOrder");
    const auto listed = backend.invoke("masters.List", nlohmann::json::array({"Patch.esp"}));
    REQUIRE(listed.has_value());
    CHECK(nlohmann::json::parse(listed->get<std::string>()).at("masters").size() == 1);
}


TEST_CASE("Native CreateRecord authors a PERK whose header and effects persist through save") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(patchPath, makePatchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterPath, patchPath);
    const auto formKey = createPerk(backend, perkPayload(nlohmann::json::array({
        questEffect(), abilityEffect()
    })));

    const auto listed = readEffects(backend, formKey);
    REQUIRE(listed.size() == 2u);
    CHECK(listed[0]["type"] == "quest_stage");
    CHECK(listed[0]["quest"] == "000100:Master.esp");
    CHECK(listed[0]["questStage"] == 10);
    CHECK(listed[1]["ability"] == "000123:Master.esp");

    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto plugin = fo4::read_plugin_file(patchPath);
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(formKey));
    REQUIRE(record != nullptr);
    const auto header = std::find_if(record->subrecords.begin(), record->subrecords.end(),
        [](const fo4::Subrecord& subrecord) { return subrecord.signature.to_string() == "DATA"; });
    REQUIRE(header != record->subrecords.end());
    CHECK(header->data == std::vector<std::byte>{
        std::byte{1}, std::byte{5}, std::byte{3}, std::byte{0}, std::byte{0}});
    const auto effects = fo4::forms::read_perk_effects(plugin, *record);
    REQUIRE(effects.size() == 2u);
    CHECK(effects[0].quest == fo4::FormKey::parse("000100:Master.esp"));
    CHECK(effects[0].quest_stage == 10);
    CHECK(effects[1].ability == fo4::FormKey::parse("000123:Master.esp"));
}

TEST_CASE("Native CreateRecord refuses effect FormKeys of the wrong type or unknown records") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(patchPath, makePatchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterPath, patchPath);

    const auto wrongAbility = backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "PERK", perkPayload(nlohmann::json::array({
            {{"type", "ability"}, {"ability", "000100:Master.esp"}}
        }))}));
    REQUIRE_FALSE(wrongAbility.has_value());
    CHECK(wrongAbility.error().type == "InvalidTypedFormKey");

    const auto unknownQuest = backend.invokeBackend(
        "CreateRecord", nlohmann::json::array({"Patch.esp", "PERK", perkPayload(nlohmann::json::array({
            {{"type", "quest_stage"}, {"quest", "000999:Master.esp"}, {"questStage", 1}}
        }))}));
    REQUIRE_FALSE(unknownQuest.has_value());
    CHECK(unknownQuest.error().type == "InvalidTypedFormKey");
}

TEST_CASE("Native SetPerkEffects replaces the effect list and leaves the record unchanged on refusal") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(patchPath, makePatchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterPath, patchPath);
    const auto formKey = createPerk(backend, perkPayload(nlohmann::json::array({
        questEffect(), abilityEffect()
    })));

    const auto refused = backend.setPerkEffects(nlohmann::json::array({
        "Patch.esp", formKey, nlohmann::json::array({
            {{"type", "ability"}, {"ability", "000456:Master.esp"}}
        })
    }));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "InvalidTypedFormKey");
    CHECK(readEffects(backend, formKey).size() == 2u);

    const auto replaced = backend.setPerkEffects(nlohmann::json::array({
        "Patch.esp", formKey, nlohmann::json::array({
            {{"type", "entry_point"}, {"entryPoint", 3}, {"function", 5},
             {"parameters", {{"type", 4}, {"entryId", 7}, {"scriptFlags", 1}, {"data", "23010000"}}}}
        })
    }));
    REQUIRE(replaced.has_value());

    const auto listed = readEffects(backend, formKey);
    REQUIRE(listed.size() == 1u);
    CHECK(listed[0]["type"] == "entry_point");
    CHECK(listed[0]["entryPoint"] == 3);
    CHECK(listed[0]["function"] == 5);
    CHECK(listed[0]["parameters"]["entryId"] == 7);
    CHECK(listed[0]["parameters"]["data"] == "23010000");
}

TEST_CASE("Native SetPerkEffects refuses EPFD data whose length does not match its EPFT type and leaves the record unchanged") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(patchPath, makePatchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterPath, patchPath);
    const auto formKey = createPerk(backend, perkPayload(nlohmann::json::array({
        questEffect(), abilityEffect()
    })));

    const auto refused = backend.setPerkEffects(nlohmann::json::array({
        "Patch.esp", formKey, nlohmann::json::array({
            {{"type", "entry_point"}, {"entryPoint", 3}, {"function", 5},
             {"parameters", {{"type", 4}, {"data", "2301"}}}}
        })
    }));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "InvalidPerkEffects");

    const auto listed = readEffects(backend, formKey);
    REQUIRE(listed.size() == 2u);
    CHECK(listed[0]["type"] == "quest_stage");
    CHECK(listed[1]["type"] == "ability");
}

TEST_CASE("Native SetField edits PERK Level and Playable header fields and persists them") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(patchPath, makePatchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterPath, patchPath);
    const auto formKey = createPerk(backend, nlohmann::json{{"editorId", "BarePerk"}});

    REQUIRE(backend.invokeBackend("SetField", nlohmann::json::array({
        "Patch.esp", formKey, "PERK\\Level", "9"
    })).has_value());
    REQUIRE(backend.invokeBackend("SetField", nlohmann::json::array({
        "Patch.esp", formKey, "PERK\\Playable", "0"
    })).has_value());
    REQUIRE(backend.invoke(
        "backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto plugin = fo4::read_plugin_file(patchPath);
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(formKey));
    REQUIRE(record != nullptr);
    const auto header = std::find_if(record->subrecords.begin(), record->subrecords.end(),
        [](const fo4::Subrecord& subrecord) { return subrecord.signature.to_string() == "DATA"; });
    REQUIRE(header != record->subrecords.end());
    CHECK(header->data == std::vector<std::byte>{
        std::byte{0}, std::byte{9}, std::byte{1}, std::byte{0}, std::byte{0}});
}


// Detects: typed EPFD fields must encode to the EPFD bytes for EPFT 1, 2, 5, 6 and 8 and read back as the same typed fields.
TEST_CASE("Native typed EPFD fields encode per EPFT and read back with their hex") {
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

    const auto listed = readEffects(backend, formKey);
    REQUIRE(listed.size() == 5u);
    CHECK(listed[0]["parameters"]["data"] == "00002040");
    CHECK(listed[0]["parameters"]["value"] == 2.5);
    CHECK(listed[1]["parameters"]["data"] == "0000c03f000000c0");
    CHECK(listed[1]["parameters"]["value"] == 1.5);
    CHECK(listed[1]["parameters"]["value2"] == -2.0);
    CHECK(listed[2]["parameters"]["data"] == "23010000");
    CHECK(listed[2]["parameters"]["form"] == "000123:Master.esp");
    CHECK(listed[3]["parameters"]["data"] == "486900");
    CHECK(listed[3]["parameters"]["text"] == "Hi");
    CHECK(listed[4]["parameters"]["data"] == "8907000000004040");
    CHECK(listed[4]["parameters"]["actorValue"] == "000789:Master.esp");
    CHECK(listed[4]["parameters"]["value"] == 3.0);
}

// Detects: typed parameters must refuse hex data with typed fields, typed fields that do not apply to the EPFT, and forms of the wrong record type.
TEST_CASE("Native typed EPFD refuses mixed, misplaced and wrong-typed fields") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(patchPath, makePatchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterPath, patchPath);
    const auto formKey = createPerk(backend, perkPayload(nlohmann::json::array()));
    const auto refusedType = [&](nlohmann::json parameters) {
        const auto result = backend.setPerkEffects(nlohmann::json::array({
            "Patch.esp", formKey, nlohmann::json::array({entryPoint(5, std::move(parameters))})
        }));
        REQUIRE_FALSE(result.has_value());
        return result.error().type;
    };
    CHECK(refusedType({{"type", 1}, {"data", "00002040"}, {"value", 1.0}}) == "InvalidPerkEffects");
    CHECK(refusedType({{"type", 1}, {"form", "MasterSpell"}, {"value", 1.0}}) == "InvalidPerkEffects");
    CHECK(refusedType({{"type", 8}, {"value", 1.0}}) == "InvalidPerkEffects");
    CHECK(refusedType({{"type", 5}, {"form", "MasterList"}}) == "InvalidTypedFormKey");
    CHECK(refusedType({{"type", 3}, {"form", "MasterSpell"}}) == "InvalidTypedFormKey");
    CHECK(refusedType({{"type", 5}, {"form", "NoSuchRecord"}}) == "InvalidTypedFormKey");
    CHECK(refusedType({{"type", 8}, {"actorValue", "MasterSpell"}, {"value", 1.0}}) == "InvalidTypedFormKey");
}

// Detects: create_record must write conditions, nextPerk and sound with their type checks, and get_perk_links must read them back.
TEST_CASE("Native CreateRecord writes PERK conditions, next perk and sound and refuses other types") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(patchPath, makePatchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterPath, patchPath);
    const auto nextKey = createPerk(backend, nlohmann::json{{"editorId", "NextPerk"}});
    const nlohmann::json condition{{"function", "GetStage"}, {"operator", ">="}, {"value", 5}, {"param1", 2}, {"runOn", "Subject"}};
    const auto formKey = createPerk(backend, nlohmann::json{
        {"editorId", "LinkedPerk"},
        {"conditions", nlohmann::json::array({condition})},
        {"nextPerk", nextKey},
        {"sound", "000B00:Master.esp"},
        {"swf", "Interface\\Perk.swf"}
    });

    const auto links = backend.getPerkLinks(nlohmann::json::array({"Patch.esp", formKey}));
    REQUIRE(links.has_value());
    CHECK((*links)["nextPerk"] == nextKey);
    CHECK((*links)["sound"] == "000B00:Master.esp");
    CHECK((*links)["swf"] == "Interface\\Perk.swf");
    REQUIRE((*links)["conditions"].size() == 1u);
    CHECK((*links)["conditions"][0]["function"] == "GetStage");

    const auto refusedType = [&](nlohmann::json payload) {
        const auto result = backend.invokeBackend(
            "CreateRecord", nlohmann::json::array({"Patch.esp", "PERK", payload}));
        REQUIRE_FALSE(result.has_value());
        return result.error().type;
    };
    CHECK(refusedType({{"editorId", "BadNext"}, {"nextPerk", "000123:Master.esp"}}) == "InvalidTypedFormKey");
    CHECK(refusedType({{"editorId", "BadSound"}, {"sound", "000100:Master.esp"}}) == "InvalidTypedFormKey");
    CHECK(refusedType({{"editorId", "BadNextText"}, {"nextPerk", "NextPerk"}}) == "InvalidFormKey");
}

// Detects: set_perk_next must refuse a target that is not a PERK by name and leave the record unchanged.
TEST_CASE("Native SetPerkNext links a PERK and refuses a non-PERK target") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(patchPath, makePatchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterPath, patchPath);
    const auto perk = createPerk(backend, nlohmann::json{{"editorId", "LinkingPerk"}});
    const auto next = createPerk(backend, nlohmann::json{{"editorId", "FollowingPerk"}});

    const auto refused = backend.setPerkNext(nlohmann::json::array({"Patch.esp", perk, "000123:Master.esp"}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "InvalidTypedFormKey");
    const auto unlinked = backend.getPerkLinks(nlohmann::json::array({"Patch.esp", perk}));
    REQUIRE(unlinked.has_value());
    CHECK((*unlinked)["nextPerk"].is_null());

    REQUIRE(backend.setPerkNext(nlohmann::json::array({"Patch.esp", perk, next})).has_value());
    const auto linked = backend.getPerkLinks(nlohmann::json::array({"Patch.esp", perk}));
    REQUIRE(linked.has_value());
    CHECK((*linked)["nextPerk"] == next);

    const auto invalid = backend.setPerkNext(nlohmann::json::array({"Patch.esp", perk, "FollowingPerk"}));
    REQUIRE_FALSE(invalid.has_value());
    CHECK(invalid.error().type == "InvalidPerkNext");
}

// Detects: a localized PERK must store FULL and DESC as string IDs after the set-wide maximum and never renumber existing IDs.
TEST_CASE("Native CreateRecord writes localized PERK text as string IDs past the existing maximum") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(patchPath, makePatchPlugin(0x80));
    writeStringTable(root / "Strings" / "Patch_en.STRINGS", fo4::localization::StringTableKind::strings, 7, "Existing name");
    writeStringTable(root / "Strings" / "Patch_en.DLSTRINGS", fo4::localization::StringTableKind::dlstrings, 9, "Existing description");

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterPath, patchPath);
    backend.testSetJournalRoot(root / "journal");
    const auto formKey = createPerk(backend, nlohmann::json{
        {"editorId", "LocalPerk"}, {"full", "New name"}, {"desc", "New description"}
    });
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto plugin = fo4::read_plugin_file(patchPath);
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(formKey));
    REQUIRE(record != nullptr);
    const auto subrecordData = [record](std::string_view signature) {
        const auto found = std::find_if(record->subrecords.begin(), record->subrecords.end(),
            [signature](const fo4::Subrecord& subrecord) { return subrecord.signature.to_string() == signature; });
        REQUIRE(found != record->subrecords.end());
        return found->data;
    };
    CHECK(subrecordData("FULL") == std::vector<std::byte>{std::byte{10}, std::byte{0}, std::byte{0}, std::byte{0}});
    CHECK(subrecordData("DESC") == std::vector<std::byte>{std::byte{11}, std::byte{0}, std::byte{0}, std::byte{0}});

    const auto names = backend.invoke("strings.ExportStrings", nlohmann::json::array({"Patch.esp", "", "STRINGS"}));
    REQUIRE(names.has_value());
    const auto nameRows = nlohmann::json::parse(names->get<std::string>())["rows"];
    REQUIRE(nameRows.size() == 2u);
    CHECK(nameRows[0] == nlohmann::json{{"id", 7}, {"text", "Existing name"}});
    CHECK(nameRows[1] == nlohmann::json{{"id", 10}, {"text", "New name"}});
    const auto descriptions = backend.invoke("strings.ExportStrings", nlohmann::json::array({"Patch.esp", "", "DLSTRINGS"}));
    REQUIRE(descriptions.has_value());
    const auto descriptionRows = nlohmann::json::parse(descriptions->get<std::string>())["rows"];
    REQUIRE(descriptionRows.size() == 2u);
    CHECK(descriptionRows[0] == nlohmann::json{{"id", 9}, {"text", "Existing description"}});
    CHECK(descriptionRows[1] == nlohmann::json{{"id", 11}, {"text", "New description"}});
}

// Detects: mixed-case PERK language tokens must compose with pending edits to the same localized table.
TEST_CASE("Native localized PERK composes mixed-case pending table edits") {
    const auto root = tempRoot(); Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp", patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin()); writeBytes(patchPath, makePatchPlugin(0x80));
    writeStringTable(root / "Strings" / "Patch_en.STRINGS", fo4::localization::StringTableKind::strings, 7, "Old");
    writeStringTable(root / "Strings" / "Patch_en.DLSTRINGS", fo4::localization::StringTableKind::dlstrings, 9, "Old description");
    fo4ide::plugin::RadiclePluginBackend backend; loadBackend(backend, masterPath, patchPath);
    REQUIRE(backend.invoke("strings.SetString", nlohmann::json::array({"Patch.esp", "EN", "STRINGS", 7, "Edited"})).has_value());
    static_cast<void>(createPerk(backend, nlohmann::json{{"editorId", "MixedPerk"}, {"language", "eN"}, {"full", "New"}}));
    const auto exported = backend.invoke("strings.ExportStrings", nlohmann::json::array({"Patch.esp", "en", "STRINGS"}));
    REQUIRE(exported.has_value());
    const auto rows = nlohmann::json::parse(exported->get<std::string>())["rows"];
    CHECK(std::any_of(rows.begin(), rows.end(), [](const auto& row) { return row.at("id") == 7 && row.at("text") == "Edited"; }));
    CHECK(std::any_of(rows.begin(), rows.end(), [](const auto& row) { return row.at("id") == 10 && row.at("text") == "New"; }));
}

// Detects: set_perk_sound and set_perk_swf must insert, rewrite and remove SNAM and FNAM, and must refuse a sound that is not an SNDR by name.
TEST_CASE("Native SetPerkSound and SetPerkSwf insert, rewrite and remove, and refuse a non-SNDR sound") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto patchPath = root / "Patch.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(patchPath, makePatchPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackend(backend, masterPath, patchPath);
    const auto perk = createPerk(backend, nlohmann::json{{"editorId", "SoundPerk"}});

    const auto refused = backend.setPerkSound(nlohmann::json::array({"Patch.esp", perk, "000100:Master.esp"}));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "InvalidTypedFormKey");
    const auto notText = backend.setPerkSound(nlohmann::json::array({"Patch.esp", perk, 5}));
    REQUIRE_FALSE(notText.has_value());
    CHECK(notText.error().type == "InvalidPerkSound");
    const auto unrepresentable = backend.setPerkSwf(nlohmann::json::array({"Patch.esp", perk, "\xE4\xB8\xAD"}));
    REQUIRE_FALSE(unrepresentable.has_value());
    CHECK(unrepresentable.error().type == "InvalidPerkSwf");

    REQUIRE(backend.setPerkSound(nlohmann::json::array({"Patch.esp", perk, "000B00:Master.esp"})).has_value());
    REQUIRE(backend.setPerkSwf(nlohmann::json::array({"Patch.esp", perk, "Interface\\Perk.swf"})).has_value());
    auto links = backend.getPerkLinks(nlohmann::json::array({"Patch.esp", perk}));
    REQUIRE(links.has_value());
    CHECK((*links)["sound"] == "000B00:Master.esp");
    CHECK((*links)["swf"] == "Interface\\Perk.swf");

    REQUIRE(backend.setPerkSound(nlohmann::json::array({"Patch.esp", perk, ""})).has_value());
    REQUIRE(backend.setPerkSwf(nlohmann::json::array({"Patch.esp", perk, ""})).has_value());
    links = backend.getPerkLinks(nlohmann::json::array({"Patch.esp", perk}));
    REQUIRE(links.has_value());
    CHECK((*links)["sound"].is_null());
    CHECK((*links)["swf"].is_null());
}

// Detects: set_perk_next and set_perk_sound must declare another loaded plugin's PERK and SNDR as masters, save them once, and link to them.
TEST_CASE("Native SetPerkNext and SetPerkSound declare another loaded plugin's form as a master") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto patchPath = root / "Patch.esp";
    const auto otherPath = root / "Other.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(patchPath, makePatchPlugin());
    writeBytes(otherPath, makeOtherPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackendWithOther(backend, masterPath, patchPath, otherPath);
    const auto perk = createPerk(backend, nlohmann::json{{"editorId", "LinkingPerk"}});

    REQUIRE(backend.setPerkNext(nlohmann::json::array({"Patch.esp", perk, "000C00:Other.esp"})).has_value());
    REQUIRE(backend.setPerkSound(nlohmann::json::array({"Patch.esp", perk, "000D00:Other.esp"})).has_value());
    const auto links = backend.getPerkLinks(nlohmann::json::array({"Patch.esp", perk}));
    REQUIRE(links.has_value());
    CHECK((*links)["nextPerk"] == "000C00:Other.esp");
    CHECK((*links)["sound"] == "000D00:Other.esp");

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto plugin = fo4::read_plugin_file(patchPath);
    REQUIRE(plugin.header.masters.size() == 2u);
    CHECK(plugin.header.masters[1].key == fo4::PluginKey::from_filename("Other.esp"));
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(perk));
    REQUIRE(record != nullptr);
    const auto saved = fo4::forms::read_perk_links(plugin, *record);
    CHECK(saved.next_perk == fo4::FormKey::parse("000C00:Other.esp"));
    CHECK(saved.sound == fo4::FormKey::parse("000D00:Other.esp"));
}

// Detects: a PERK effect edit refused after it declares another loaded plugin's spell must leave the master list and the record's own next link unchanged.
TEST_CASE("Native SetPerkEffects refusal after declaring a loaded plugin's spell leaves masters and links unchanged") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    const auto masterPath = root / "Master.esp";
    const auto patchPath = root / "Patch.esp";
    const auto otherPath = root / "Other.esp";
    writeBytes(masterPath, makeMasterPlugin());
    writeBytes(patchPath, makePatchPlugin());
    writeBytes(otherPath, makeOtherPlugin());

    fo4ide::plugin::RadiclePluginBackend backend;
    loadBackendWithOther(backend, masterPath, patchPath, otherPath);
    const auto perk = createPerk(backend, nlohmann::json{{"editorId", "LinkingPerk"}});
    const auto next = createPerk(backend, nlohmann::json{{"editorId", "FollowingPerk"}});
    REQUIRE(backend.setPerkNext(nlohmann::json::array({"Patch.esp", perk, next})).has_value());
    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());

    const auto refused = backend.setPerkEffects(nlohmann::json::array({
        "Patch.esp", perk, nlohmann::json::array({
            {{"type", "ability"}, {"ability", "000E00:Other.esp"}},
            {{"type", "ability"}, {"ability", "000C00:Other.esp"}}
        })
    }));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "InvalidTypedFormKey");

    REQUIRE(backend.invoke("backend.SavePlugin", nlohmann::json::array({"Patch.esp"})).has_value());
    const auto plugin = fo4::read_plugin_file(patchPath);
    CHECK(plugin.header.masters.size() == 1u);
    const auto* record = fo4::find_record(plugin, fo4::FormKey::parse(perk));
    REQUIRE(record != nullptr);
    CHECK(fo4::forms::read_perk_links(plugin, *record).next_perk == fo4::FormKey::parse(next));
    CHECK(readEffects(backend, perk).empty());
}
