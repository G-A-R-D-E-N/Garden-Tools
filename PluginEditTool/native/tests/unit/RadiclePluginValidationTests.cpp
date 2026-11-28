#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include <fo4/pex/pex.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/plugin/subrecord_authoring.hpp>
#include <fo4/plugin/vmad_writer.hpp>
#include <fo4/plugin/writer.hpp>
#include <fo4ide/plugin/PluginValidation.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>
#include "plugin_bytes.hpp"
namespace {
constexpr fo4::Signature kQust = fo4::Signature::from_string("QUST");
constexpr fo4::Signature kSpel = fo4::Signature::from_string("SPEL");
constexpr fo4::Signature kGlob = fo4::Signature::from_string("GLOB");

fo4::Plugin emptyPlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x800);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
    auto plugin = fo4::read_plugin(out.bytes, "Validated.esp");
    plugin.original_range.clear();
    plugin.tes4.original_range.clear();
    return plugin;
}

fo4::Plugin settled(fo4::Plugin plugin) {
    auto bytes = fo4::write_plugin(plugin);
    auto reread = fo4::read_plugin(bytes, "Validated.esp");
    reread.original_range.clear();
    reread.tes4.original_range.clear();
    return reread;
}

fo4::FormKey createRecord(fo4::Plugin& plugin, fo4::Signature signature, std::string_view editorId);

fo4::Record* findMutable(fo4::Plugin& plugin, const fo4::FormKey& key) {
    for (auto* record : fo4::collect_records(plugin)) {
        if (record->form_key == key) return const_cast<fo4::Record*>(record);
    }
    return nullptr;
}

bool ensureSubrecord(fo4::Plugin& plugin, const fo4::FormKey& key, fo4::Signature signature) {
    const auto* record = findMutable(plugin, key);
    REQUIRE(record != nullptr);
    for (const auto& subrecord : record->subrecords) {
        if (subrecord.signature == signature) return true;
    }
    const auto status = fo4::add_record_subrecord(plugin, key, signature);
    return status == fo4::SubrecordAddStatus::added;
}

void setDnamFlags(fo4::Plugin& plugin, const fo4::FormKey& key, std::uint16_t flags) {
    auto* record = findMutable(plugin, key);
    REQUIRE(record != nullptr);
    for (auto& subrecord : record->subrecords) {
        if (subrecord.signature != fo4::Signature::from_string("DNAM")) continue;
        REQUIRE(subrecord.data.size() >= sizeof(std::uint16_t));
        std::memcpy(subrecord.data.data(), &flags, sizeof(flags));
        return;
    }
    FAIL("the fixture QUST has no DNAM to carry the quest flags");
}

fo4::FormKey prepareQuest(fo4::Plugin& plugin, std::string_view editorId, std::uint16_t flags) {
    const auto quest = createRecord(plugin, kQust, editorId);
    REQUIRE(ensureSubrecord(plugin, quest, fo4::Signature::from_string("DNAM")));
    setDnamFlags(plugin, quest, flags);
    const auto status = fo4::add_vmad(plugin, quest);
    REQUIRE((status == fo4::VmadStructureMutationStatus::updated ||
             status == fo4::VmadStructureMutationStatus::unchanged));
    return quest;
}

fo4::FormKey createRecord(fo4::Plugin& plugin, fo4::Signature signature, std::string_view editorId) {
    const auto result = fo4::create_record(plugin, signature, editorId);
    REQUIRE(result.status == fo4::RecordCreateStatus::created);
    return result.key;
}

const fo4ide::plugin::ValidationFinding* findingFor(const fo4ide::plugin::ValidationReport& report,
                                                    std::string_view code) {
    for (const auto& finding : report.findings) {
        if (finding.code == code) return &finding;
    }
    return nullptr;
}

bool hasCode(const fo4ide::plugin::ValidationReport& report, std::string_view code) {
    return std::any_of(report.findings.begin(), report.findings.end(),
                       [&](const fo4ide::plugin::ValidationFinding& finding) {
                           return finding.code == code;
                       });
}

std::size_t countCode(const fo4ide::plugin::ValidationReport& report, std::string_view code) {
    return static_cast<std::size_t>(std::count_if(
        report.findings.begin(), report.findings.end(),
        [&](const fo4ide::plugin::ValidationFinding& finding) { return finding.code == code; }));
}

fo4ide::plugin::ValidationReport run(const fo4::Plugin& plugin) {
    fo4ide::plugin::ValidationOptions options;
    options.plugin = &plugin;
    return fo4ide::plugin::validatePlugin(options);
}

struct TempDir {
    std::filesystem::path root;
    TempDir() {
        static std::atomic<unsigned> counter{0};
        std::random_device seed;
        root = std::filesystem::temp_directory_path() /
               ("fo4ide_validate_scripts_" + std::to_string(seed()) + "_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
                std::to_string(counter++));
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        std::filesystem::create_directories(root, ec);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

void writePex(const std::filesystem::path& path,
              const std::string& objectName,
              const std::vector<fo4::pex::PexProperty>& properties,
              const std::string& parentName = {}) {
    fo4::pex::PexFile file;
    file.majorVersion = 3;
    file.minorVersion = 9;
    file.gameId = 2;
    fo4::pex::PexObject object;
    object.name = objectName;
    object.parentClassName = parentName;
    object.properties = properties;
    file.objects.push_back(std::move(object));
    file.rebuildStringTable();
    const auto bytes = file.toBytes();
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    REQUIRE(out.good());
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    REQUIRE(out.good());
}

fo4::pex::PexProperty objectProperty(std::string name, std::string type) {
    fo4::pex::PexProperty property;
    property.name = std::move(name);
    property.type = std::move(type);
    property.flags = 0x04;
    property.autoVarName = property.name;
    return property;
}

}

// Detects: clean plugins must not produce validation errors.
TEST_CASE("A clean plugin reports no errors", "[validation]") {
    auto plugin = settled(emptyPlugin());
    createRecord(plugin, kGlob, "CleanGlobal");
    plugin = settled(plugin);

    const auto report = run(plugin);
    CHECK(report.errors == 0);
    CHECK_FALSE(hasCode(report, "HEDR_RECORD_COUNT_MISMATCH"));
}

// Detects: a stale on-disk HEDR record count must produce a warning.
TEST_CASE("HEDR numRecords that disagrees with a clean file's record set is a warning", "[validation]") {
    auto plugin = settled(emptyPlugin());
    createRecord(plugin, kGlob, "CleanGlobal");
    auto bytes = fo4::write_plugin(settled(plugin));

    const std::uint32_t stale = 5;
    std::memcpy(bytes.data() + 24 + 6 + 4, &stale, sizeof(stale));
    const auto file = fo4::read_plugin(bytes, "Validated.esp");

    const auto report = run(file);
    const auto* finding = findingFor(report, "HEDR_RECORD_COUNT_MISMATCH");
    REQUIRE(finding != nullptr);
    CHECK(finding->severity == fo4ide::plugin::Severity::warning);
}

// Detects: unsaved edits must not report the stale HEDR count rewritten on save.
TEST_CASE("An unsaved edit does not report the HEDR count that a save will rewrite", "[validation]") {
    auto plugin = settled(emptyPlugin());
    createRecord(plugin, kGlob, "UnsavedGlobal");

    const auto report = run(plugin);
    CHECK_FALSE(hasCode(report, "HEDR_RECORD_COUNT_MISMATCH"));
}

// Detects: a nextObjectId colliding with a local record must produce an error.
TEST_CASE("nextObjectId at or below an own FormID is an error", "[validation]") {
    auto plugin = settled(emptyPlugin());
    createRecord(plugin, kGlob, "LowNextGlobal");
    plugin = settled(plugin);
    plugin.header.next_form_id = 0x800;

    const auto report = run(plugin);
    CHECK(hasCode(report, "HEDR_NEXT_OBJECT_ID"));
}

// Detects: a FormID with an undeclared master index must produce an error.
TEST_CASE("A FormID whose master index is past the declared masters is an error", "[validation]") {
    auto plugin = settled(emptyPlugin());
    createRecord(plugin, kGlob, "OrphanGlobal");
    plugin = settled(plugin);

    // 0xFE and 0xFF are runtime prefixes, not file ones: on disk they index the master table like any other byte.
    for (const std::uint32_t raw : {0x03000800u, 0xFE000800u, 0xFF000800u}) {
        for (auto* record : fo4::collect_records(plugin)) {
            if (record->header.signature == fo4::sig_tes4) continue;
            const_cast<fo4::Record&>(*record).header.form_id = fo4::RawFormId{raw};
            break;
        }
        CAPTURE(raw);
        CHECK(hasCode(run(plugin), "FORM_ID_MASTER_OUT_OF_RANGE"));
    }
}

// Detects: duplicate record FormIDs must produce an error.
TEST_CASE("Two records sharing a FormID are an error", "[validation]") {
    auto plugin = settled(emptyPlugin());
    createRecord(plugin, kGlob, "FirstGlobal");
    createRecord(plugin, kGlob, "SecondGlobal");
    plugin = settled(plugin);

    std::uint32_t first = 0;
    for (const auto* record : fo4::collect_records(plugin)) {
        if (record->header.signature == fo4::sig_tes4) continue;
        if (first == 0) {
            first = record->header.form_id.value();
            continue;
        }
        const_cast<fo4::Record&>(*record).header.form_id = fo4::RawFormId{first};
        break;
    }

    const auto report = run(plugin);
    CHECK(hasCode(report, "DUPLICATE_FORM_ID"));
}

// Detects: a SPEL missing a required subrecord must produce an error.
TEST_CASE("A SPEL without a required subrecord is an error", "[validation]") {
    auto plugin = settled(emptyPlugin());
    const auto spell = createRecord(plugin, kSpel, "BareSpell");
    plugin = settled(plugin);

    for (auto* record : fo4::collect_records(plugin)) {
        if (record->form_key != spell) continue;
        auto& subrecords = const_cast<fo4::Record&>(*record).subrecords;
        subrecords.erase(std::remove_if(subrecords.begin(), subrecords.end(),
                                        [](const fo4::Subrecord& subrecord) {
                                            return subrecord.signature ==
                                                   fo4::Signature::from_string("SPIT");
                                        }),
                         subrecords.end());
    }

    const auto report = run(plugin);
    CHECK(hasCode(report, "REQUIRED_SUBRECORD_MISSING"));
}

// Detects: a SPEL with an empty effect list must produce a warning.
TEST_CASE("A SPEL with no effects is a warning", "[validation]") {
    auto plugin = settled(emptyPlugin());
    createRecord(plugin, kSpel, "EffectlessSpell");
    plugin = settled(plugin);

    const auto report = run(plugin);
    const auto* finding = findingFor(report, "EFFECT_LIST_EMPTY");
    REQUIRE(finding != nullptr);
    CHECK(finding->severity == fo4ide::plugin::Severity::warning);
    CHECK(report.errors == 0);
}

// Detects: a scripted quest without Start Game Enabled must produce an info note.
TEST_CASE("A QUST with a script but no Start Game Enabled is an info note", "[validation]") {
    auto plugin = settled(emptyPlugin());
    const auto quest = prepareQuest(plugin, "DormantQuest", 0x0008);
    REQUIRE(fo4::add_vmad_script(plugin, quest, 0, "RadSacrificialQuest", 0) ==
            fo4::VmadStructureMutationStatus::updated);
    plugin = settled(plugin);

    const auto report = run(plugin);
    const auto* finding = findingFor(report, "QUEST_SCRIPT_NOT_START_ENABLED");
    REQUIRE(finding != nullptr);
    CHECK(finding->severity == fo4ide::plugin::Severity::info);
    CHECK(finding->message.find("SetStage") != std::string::npos);
    CHECK(report.errors == 0);
}

// Detects: a scripted quest with Start Game Enabled must not produce that note.
TEST_CASE("A QUST with a script and Start Game Enabled is clean", "[validation]") {
    auto plugin = settled(emptyPlugin());
    const auto quest = prepareQuest(plugin, "RunningQuest", 0x0009);
    REQUIRE(fo4::add_vmad_script(plugin, quest, 0, "RadSacrificialQuest", 0) ==
            fo4::VmadStructureMutationStatus::updated);
    plugin = settled(plugin);

    const auto report = run(plugin);
    CHECK_FALSE(hasCode(report, "QUEST_SCRIPT_NOT_START_ENABLED"));
}

// Detects: a partial-form QUST override (record flag 0x4000, only EDID and VMAD) must not be reported as missing its DNAM, while a full QUST with the same subrecords must.
TEST_CASE("A partial-form QUST is not missing its subrecords but a full QUST is", "[validation]") {
    auto plugin = settled(emptyPlugin());
    const auto quest = prepareQuest(plugin, "PartialQuest", 0x0009);
    REQUIRE(fo4::add_vmad_script(plugin, quest, 0, "RadSacrificialQuest", 0) ==
            fo4::VmadStructureMutationStatus::updated);
    plugin = settled(plugin);

    auto* record = findMutable(plugin, quest);
    REQUIRE(record != nullptr);
    auto& subrecords = record->subrecords;
    subrecords.erase(std::remove_if(subrecords.begin(), subrecords.end(),
                                    [](const fo4::Subrecord& subrecord) {
                                        return subrecord.signature !=
                                                   fo4::Signature::from_string("EDID") &&
                                               subrecord.signature !=
                                                   fo4::Signature::from_string("VMAD");
                                    }),
                     subrecords.end());
    REQUIRE(subrecords.size() == 2);

    const auto full = run(plugin);
    CHECK(std::any_of(full.findings.begin(), full.findings.end(),
                      [](const fo4ide::plugin::ValidationFinding& finding) {
                          return finding.code == "REQUIRED_SUBRECORD_MISSING" &&
                                 finding.field == "DNAM";
                      }));
    CHECK(hasCode(full, "QUEST_START_GAME_UNKNOWN"));

    record->header.flags = fo4::RecordFlags{fo4::RecordFlags::partial_form};
    const auto partial = run(plugin);
    CHECK_FALSE(hasCode(partial, "REQUIRED_SUBRECORD_MISSING"));
    CHECK_FALSE(hasCode(partial, "QUEST_START_GAME_UNKNOWN"));
}

// Detects: a declared master missing from the active profile must produce an error.
TEST_CASE("A declared master missing from the profile is an error", "[validation]") {
    auto plugin = settled(emptyPlugin());
    plugin.header.masters.push_back(fo4::MasterReference{
        fo4::PluginKey::from_filename("Survival Mode Overhaul.esp"), fo4::PluginKind::full, 0, {}});

    fo4ide::plugin::ValidationOptions options;
    options.plugin = &plugin;
    const std::vector<fo4ide::plugin::ProfileEntry> profile = {
        {"Fallout4.esm", {}}, {"Validated.esp", {}}};
    options.profile = &profile;

    const auto report = fo4ide::plugin::validatePlugin(options);
    CHECK(hasCode(report, "MASTER_MISSING"));
    CHECK_FALSE(hasCode(report, "MASTER_ORDER"));
}

// Detects: a master loading after its dependent must produce an error.
TEST_CASE("A master that loads after the plugin is an order error", "[validation]") {
    auto plugin = settled(emptyPlugin());
    plugin.header.masters.push_back(fo4::MasterReference{
        fo4::PluginKey::from_filename("LateMaster.esp"), fo4::PluginKind::full, 0, {}});

    fo4ide::plugin::ValidationOptions options;
    options.plugin = &plugin;
    const std::vector<fo4ide::plugin::ProfileEntry> profile = {
        {"Fallout4.esm", {}}, {"Validated.esp", {}}, {"LateMaster.esp", {}}};
    options.profile = &profile;

    const auto report = fo4ide::plugin::validatePlugin(options);
    CHECK(hasCode(report, "MASTER_ORDER"));
    CHECK_FALSE(hasCode(report, "MASTER_MISSING"));
}

// Detects: a VMAD script missing a loose PEX must produce a warning.
TEST_CASE("A VMAD script with no compiled pex is a warning", "[validation]") {
    TempDir temp;
    auto plugin = settled(emptyPlugin());
    const auto quest = prepareQuest(plugin, "ScriptedQuest", 0x0009);
    REQUIRE(fo4::add_vmad_script(plugin, quest, 0, "MissingScript", 0) ==
            fo4::VmadStructureMutationStatus::updated);
    plugin = settled(plugin);

    fo4ide::plugin::ValidationOptions options;
    options.plugin = &plugin;
    options.scriptsDirectories = {temp.root};
    const auto report = fo4ide::plugin::validatePlugin(options);
    const auto* finding = findingFor(report, "SCRIPT_MISSING");
    REQUIRE(finding != nullptr);
    CHECK(finding->severity == fo4ide::plugin::Severity::warning);
}

// Detects: a VMAD property absent from the PEX must produce an error.
TEST_CASE("A property the pex does not declare is an error", "[validation]") {
    TempDir temp;
    writePex(temp.root / "KnownScript.pex", "KnownScript", {objectProperty("Known", "GlobalVariable")});

    auto plugin = settled(emptyPlugin());
    const auto quest = prepareQuest(plugin, "PropertyQuest", 0x0009);
    REQUIRE(fo4::add_vmad_script(plugin, quest, 0, "KnownScript", 0) ==
            fo4::VmadStructureMutationStatus::updated);
    fo4::VmadProperty property;
    property.name = "Unknown";
    property.type = 1;
    property.object = fo4::RawFormId{0x000800u};
    property.alias = static_cast<std::int16_t>(-1);
    REQUIRE(fo4::add_vmad_property(plugin, quest, 0, 0, property) ==
            fo4::VmadStructureMutationStatus::updated);
    plugin = settled(plugin);

    fo4ide::plugin::ValidationOptions options;
    options.plugin = &plugin;
    options.scriptsDirectories = {temp.root};
    const auto report = fo4ide::plugin::validatePlugin(options);
    CHECK(hasCode(report, "SCRIPT_PROPERTY_MISSING"));
}

// Detects: a VMAD property with the wrong declared type must produce an error.
TEST_CASE("A property whose declared type disagrees is an error", "[validation]") {
    TempDir temp;
    writePex(temp.root / "TypedScript.pex", "TypedScript", {objectProperty("Count", "Float")});

    auto plugin = settled(emptyPlugin());
    const auto quest = prepareQuest(plugin, "TypedQuest", 0x0009);
    REQUIRE(fo4::add_vmad_script(plugin, quest, 0, "TypedScript", 0) ==
            fo4::VmadStructureMutationStatus::updated);
    fo4::VmadProperty property;
    property.name = "Count";
    property.type = 3;
    property.integer = 0;
    REQUIRE(fo4::add_vmad_property(plugin, quest, 0, 0, property) ==
            fo4::VmadStructureMutationStatus::updated);
    plugin = settled(plugin);

    fo4ide::plugin::ValidationOptions options;
    options.plugin = &plugin;
    options.scriptsDirectories = {temp.root};
    const auto report = fo4ide::plugin::validatePlugin(options);
    CHECK(hasCode(report, "SCRIPT_PROPERTY_TYPE_MISMATCH"));
}

// Detects: an unbound object property must produce a warning rather than an error.
TEST_CASE("An object property bound to nothing is a warning", "[validation]") {
    TempDir temp;
    writePex(temp.root / "BoundScript.pex", "BoundScript",
             {objectProperty("Target", "ObjectReference")});

    auto plugin = settled(emptyPlugin());
    const auto quest = prepareQuest(plugin, "UnboundQuest", 0x0009);
    REQUIRE(fo4::add_vmad_script(plugin, quest, 0, "BoundScript", 0) ==
            fo4::VmadStructureMutationStatus::updated);
    fo4::VmadProperty property;
    property.name = "Target";
    property.type = 1;
    property.object = fo4::RawFormId{0u};
    property.alias = static_cast<std::int16_t>(-1);
    REQUIRE(fo4::add_vmad_property(plugin, quest, 0, 0, property) ==
            fo4::VmadStructureMutationStatus::updated);
    plugin = settled(plugin);

    fo4ide::plugin::ValidationOptions options;
    options.plugin = &plugin;
    options.scriptsDirectories = {temp.root};
    const auto report = fo4ide::plugin::validatePlugin(options);
    const auto* finding = findingFor(report, "SCRIPT_PROPERTY_UNBOUND");
    REQUIRE(finding != nullptr);
    CHECK(finding->severity == fo4ide::plugin::Severity::warning);
}

// Detects: contract assertions must report missing records, incorrect types and absent fields.
TEST_CASE("A contract pins record types and required fields", "[validation]") {
    auto plugin = settled(emptyPlugin());
    const auto risk = createRecord(plugin, kGlob, "RiskGlobal");
    createRecord(plugin, kGlob, "OtherGlobal");
    plugin = settled(plugin);
    const auto contract = nlohmann::json::parse(R"({
        "records": [
            {"editorId": "RiskGlobal", "type": "SPEL"},
            {"editorId": "MissingGlobal", "type": "GLOB"}
        ],
        "globals": [{"editorId": "RiskGlobal", "type": "Float"}],
        "requiredFields": [{"editorId": "RiskGlobal", "field": "DESC"}]
    })");
    fo4ide::plugin::ValidationOptions options;
    options.plugin = &plugin;
    options.contract = &contract;
    const auto report = fo4ide::plugin::validatePlugin(options);
    CHECK(hasCode(report, "CONTRACT_RECORD_TYPE_MISMATCH"));
    CHECK(hasCode(report, "CONTRACT_RECORD_MISSING"));
    CHECK(hasCode(report, "CONTRACT_GLOB_TYPE_MISMATCH"));
    CHECK(hasCode(report, "CONTRACT_REQUIRED_FIELD_UNSET"));
    // Detects: every supplied contract identifier must identify the same record, including a missing FormKey and requiredFields entries.
    for (const auto* section : {"records", "globals", "requiredFields"}) {
        nlohmann::json entry{{"formKey", risk.to_string()}, {"editorId", "OtherGlobal"}};
        if (std::string_view(section) == "requiredFields") entry["field"] = "EDID";
        nlohmann::json identifiers{{section, nlohmann::json::array({entry})}};
        options.contract = &identifiers;
        CHECK(hasCode(fo4ide::plugin::validatePlugin(options), "CONTRACT_EDITOR_ID_MISMATCH"));
        identifiers[section][0]["formKey"] = "00FFFF:Validated.esp";
        CHECK(hasCode(fo4ide::plugin::validatePlugin(options), "CONTRACT_RECORD_MISSING"));
        identifiers[section][0]["formKey"] = risk.to_string();
        identifiers[section][0]["editorId"] = "riskglobal";
        const auto matched = fo4ide::plugin::validatePlugin(options);
        CHECK_FALSE(hasCode(matched, "CONTRACT_EDITOR_ID_MISMATCH"));
        CHECK_FALSE(hasCode(matched, "CONTRACT_RECORD_MISSING"));
        identifiers[section][0]["formKey"] = nullptr;
        CHECK(hasCode(fo4ide::plugin::validatePlugin(options), "CONTRACT_RECORD_MISSING"));
    }
}

// Detects: a contract's vmad section must pass a script property bound to the pinned form however that FormKey is spelled, and name the script, the property or the binding that is absent.
TEST_CASE("A contract pins a script property to the form it must bind", "[validation]") {
    auto plugin = settled(emptyPlugin());
    const auto risk = createRecord(plugin, kGlob, "RiskGlobal");
    const auto quest = prepareQuest(plugin, "ContractQuest", 0x0009);
    REQUIRE(fo4::add_vmad_script(plugin, quest, 0, "ContractScript", 0) ==
            fo4::VmadStructureMutationStatus::updated);
    fo4::VmadProperty property;
    property.name = "Risk";
    property.type = 1;
    property.object = fo4::RawFormId{risk.local().value()};
    property.alias = static_cast<std::int16_t>(-1);
    REQUIRE(fo4::add_vmad_property(plugin, quest, 0, 0, property) ==
            fo4::VmadStructureMutationStatus::updated);
    plugin = settled(plugin);

    const auto check = [&](const std::string& record, const std::string& script, nlohmann::json properties) {
        const nlohmann::json contract{{"vmad", nlohmann::json::array({{{"record", record},
                                                                       {"script", script},
                                                                       {"properties", std::move(properties)}}})}};
        fo4ide::plugin::ValidationOptions options;
        options.plugin = &plugin;
        options.contract = &contract;
        return fo4ide::plugin::validatePlugin(options);
    };
    const auto bound = [](const std::string& name, const std::string& formKey) {
        return nlohmann::json::array({{{"name", name}, {"formKey", formKey}}});
    };

    const auto exact = check(quest.to_string(), "ContractScript", bound("Risk", risk.to_string()));
    CHECK_FALSE(hasCode(exact, "CONTRACT_VMAD_BINDING_MISSING"));
    CHECK_FALSE(hasCode(exact, "CONTRACT_RECORD_MISSING"));
    const auto respelled = check(quest.to_string(), "contractscript", bound("risk", "0x" + risk.to_string()));
    CHECK_FALSE(hasCode(respelled, "CONTRACT_VMAD_BINDING_MISSING"));

    const auto wrongForm = check(quest.to_string(), "ContractScript", bound("Risk", quest.to_string()));
    const auto* binding = findingFor(wrongForm, "CONTRACT_VMAD_BINDING_MISSING");
    REQUIRE(binding != nullptr);
    CHECK(binding->field == "Risk");
    CHECK(binding->message.find(risk.to_string()) != std::string::npos);

    const auto noProperty = check(quest.to_string(), "ContractScript", bound("Hunger", risk.to_string()));
    const auto* missingProperty = findingFor(noProperty, "CONTRACT_VMAD_BINDING_MISSING");
    REQUIRE(missingProperty != nullptr);
    CHECK(missingProperty->field == "Hunger");

    const auto noScript = check(quest.to_string(), "OtherScript", nlohmann::json::array());
    const auto* missingScript = findingFor(noScript, "CONTRACT_VMAD_BINDING_MISSING");
    REQUIRE(missingScript != nullptr);
    CHECK(missingScript->field == "VMAD");

    CHECK(hasCode(check("00FFFF:Validated.esp", "ContractScript", nlohmann::json::array()),
                  "CONTRACT_RECORD_MISSING"));
}

// Detects: a namespaced script must be found in its nested folder, and the second record that attaches the same script must still get its own findings from the one decoded file.
TEST_CASE("A namespaced script is read from nested folders for every record that attaches it", "[validation]") {
    TempDir temp;
    std::filesystem::create_directories(temp.root / "Mod" / "Sub");
    writePex(temp.root / "Mod" / "Sub" / "Thing.pex", "Mod:Sub:Thing",
             {objectProperty("Known", "GlobalVariable")});

    auto plugin = settled(emptyPlugin());
    for (const auto* editorId : {"FirstNestedQuest", "SecondNestedQuest"}) {
        const auto quest = prepareQuest(plugin, editorId, 0x0009);
        REQUIRE(fo4::add_vmad_script(plugin, quest, 0, "Mod:Sub:Thing", 0) ==
                fo4::VmadStructureMutationStatus::updated);
        fo4::VmadProperty property;
        property.name = "Unknown";
        property.type = 1;
        property.object = fo4::RawFormId{0x000800u};
        property.alias = static_cast<std::int16_t>(-1);
        REQUIRE(fo4::add_vmad_property(plugin, quest, 0, 0, property) ==
                fo4::VmadStructureMutationStatus::updated);
    }
    plugin = settled(plugin);

    fo4ide::plugin::ValidationOptions options;
    options.plugin = &plugin;
    options.scriptsDirectories = {temp.root};
    const auto report = fo4ide::plugin::validatePlugin(options);
    CHECK_FALSE(hasCode(report, "SCRIPT_MISSING"));
    CHECK_FALSE(hasCode(report, "SCRIPT_OBJECT_MISSING"));
    CHECK(countCode(report, "SCRIPT_PROPERTY_MISSING") == 2u);
}

// Detects: findings below the requested severity must be filtered out.
TEST_CASE("The severity filter drops findings below the threshold", "[validation]") {
    auto plugin = settled(emptyPlugin());
    const auto spell = createRecord(plugin, kSpel, "FilteredSpell");
    static_cast<void>(spell);
    plugin = settled(plugin);

    fo4ide::plugin::ValidationOptions options;
    options.plugin = &plugin;
    options.minimum = fo4ide::plugin::Severity::error;
    const auto report = fo4ide::plugin::validatePlugin(options);
    CHECK(report.infos == 0);
    for (const auto& finding : report.findings) {
        CHECK(finding.severity == fo4ide::plugin::Severity::error);
    }
}

// Detects: findings must identify their code, severity and affected record.
TEST_CASE("Every finding carries a code, a severity and the record it names", "[validation]") {
    auto plugin = settled(emptyPlugin());
    createRecord(plugin, kSpel, "EffectlessSpell");
    plugin = settled(plugin);

    const auto report = run(plugin);
    const auto json = report.toJson();
    REQUIRE(json.contains("findings"));
    REQUIRE(json.at("findings").is_array());
    for (const auto& row : json.at("findings")) {
        CHECK_FALSE(row.at("code").get<std::string>().empty());
        CHECK_FALSE(row.at("severity").get<std::string>().empty());
        CHECK_FALSE(row.at("message").get<std::string>().empty());
        const auto recordType = row.at("recordType").get<std::string>();
        if (!recordType.empty() && recordType != "TES4") {
            CHECK_FALSE(row.at("formKey").get<std::string>().empty());
        }
    }
    CHECK(countCode(report, "EFFECT_LIST_EMPTY") == 1);
}

// Detects: a fully bound object array must not be reported as unbound.
TEST_CASE("An object array whose elements are all bound is clean", "[validation]") {
    TempDir temp;
    writePex(temp.root / "ArrayScript.pex", "ArrayScript", {objectProperty("Targets", "ObjectReference[]")});

    auto plugin = settled(emptyPlugin());
    const auto quest = prepareQuest(plugin, "ArrayQuest", 0x0009);
    REQUIRE(fo4::add_vmad_script(plugin, quest, 0, "ArrayScript", 0) ==
            fo4::VmadStructureMutationStatus::updated);
    fo4::VmadProperty property;
    property.name = "Targets";
    property.type = 11;
    property.object_elements = std::vector<fo4::VmadObjectElement>{{fo4::RawFormId{0x800u}, -1},
                                                                   {fo4::RawFormId{0x801u}, -1}};
    REQUIRE(fo4::add_vmad_property(plugin, quest, 0, 0, property) ==
            fo4::VmadStructureMutationStatus::updated);
    plugin = settled(plugin);

    fo4ide::plugin::ValidationOptions options;
    options.plugin = &plugin;
    options.scriptsDirectories = {temp.root};
    const auto report = fo4ide::plugin::validatePlugin(options);
    CHECK_FALSE(hasCode(report, "SCRIPT_PROPERTY_UNBOUND"));
    CHECK(report.errors == 0);
}

// Detects: an object array with an unbound element must produce a warning.
TEST_CASE("An object array with a None element is a warning", "[validation]") {
    TempDir temp;
    writePex(temp.root / "NoneScript.pex", "NoneScript", {objectProperty("Targets", "ObjectReference[]")});

    auto plugin = settled(emptyPlugin());
    const auto quest = prepareQuest(plugin, "NoneQuest", 0x0009);
    REQUIRE(fo4::add_vmad_script(plugin, quest, 0, "NoneScript", 0) ==
            fo4::VmadStructureMutationStatus::updated);
    fo4::VmadProperty property;
    property.name = "Targets";
    property.type = 11;
    property.object_elements = std::vector<fo4::VmadObjectElement>{{fo4::RawFormId{0x800u}, -1},
                                                                   {fo4::RawFormId{0u}, -1}};
    REQUIRE(fo4::add_vmad_property(plugin, quest, 0, 0, property) ==
            fo4::VmadStructureMutationStatus::updated);
    plugin = settled(plugin);

    fo4ide::plugin::ValidationOptions options;
    options.plugin = &plugin;
    options.scriptsDirectories = {temp.root};
    const auto report = fo4ide::plugin::validatePlugin(options);
    const auto* finding = findingFor(report, "SCRIPT_PROPERTY_UNBOUND");
    REQUIRE(finding != nullptr);
    CHECK(finding->severity == fo4ide::plugin::Severity::warning);
    CHECK(report.errors == 0);
}

// Detects: an inherited property must not be reported as missing.
TEST_CASE("A property inherited from a parent script is found", "[validation]") {
    TempDir temp;
    writePex(temp.root / "ParentScript.pex", "ParentScript", {objectProperty("Inherited", "GlobalVariable")});
    writePex(temp.root / "ChildScript.pex", "ChildScript", {}, "ParentScript");

    auto plugin = settled(emptyPlugin());
    const auto quest = prepareQuest(plugin, "ChildQuest", 0x0009);
    REQUIRE(fo4::add_vmad_script(plugin, quest, 0, "ChildScript", 0) ==
            fo4::VmadStructureMutationStatus::updated);
    fo4::VmadProperty property;
    property.name = "Inherited";
    property.type = 1;
    property.object = fo4::RawFormId{0x800u};
    property.alias = static_cast<std::int16_t>(-1);
    REQUIRE(fo4::add_vmad_property(plugin, quest, 0, 0, property) ==
            fo4::VmadStructureMutationStatus::updated);
    plugin = settled(plugin);

    fo4ide::plugin::ValidationOptions options;
    options.plugin = &plugin;
    options.scriptsDirectories = {temp.root};
    const auto report = fo4ide::plugin::validatePlugin(options);
    CHECK_FALSE(hasCode(report, "SCRIPT_PROPERTY_MISSING"));
    CHECK_FALSE(hasCode(report, "SCRIPT_PARENT_MISSING"));
}

// Detects: a missing parent PEX must downgrade an unresolved inherited property to a warning.
TEST_CASE("A property the parent cannot supply is a warning when the parent is missing", "[validation]") {
    TempDir temp;
    writePex(temp.root / "OrphanScript.pex", "OrphanScript", {}, "AbsentParentScript");

    auto plugin = settled(emptyPlugin());
    const auto quest = prepareQuest(plugin, "OrphanQuest", 0x0009);
    REQUIRE(fo4::add_vmad_script(plugin, quest, 0, "OrphanScript", 0) ==
            fo4::VmadStructureMutationStatus::updated);
    fo4::VmadProperty property;
    property.name = "Unknown";
    property.type = 1;
    property.object = fo4::RawFormId{0x800u};
    property.alias = static_cast<std::int16_t>(-1);
    REQUIRE(fo4::add_vmad_property(plugin, quest, 0, 0, property) ==
            fo4::VmadStructureMutationStatus::updated);
    plugin = settled(plugin);

    fo4ide::plugin::ValidationOptions options;
    options.plugin = &plugin;
    options.scriptsDirectories = {temp.root};
    const auto report = fo4ide::plugin::validatePlugin(options);
    const auto* finding = findingFor(report, "SCRIPT_PARENT_MISSING");
    REQUIRE(finding != nullptr);
    CHECK(finding->severity == fo4ide::plugin::Severity::warning);
    CHECK_FALSE(hasCode(report, "SCRIPT_PROPERTY_MISSING"));
}

// Detects: a typo under a native parent without a PEX must produce an error.
TEST_CASE("A property the native parent cannot supply is an error when the parent has no pex", "[validation]") {
    TempDir temp;
    writePex(temp.root / "TypoScript.pex", "TypoScript", {}, "quest");

    auto plugin = settled(emptyPlugin());
    const auto quest = prepareQuest(plugin, "TypoQuest", 0x0009);
    REQUIRE(fo4::add_vmad_script(plugin, quest, 0, "TypoScript", 0) ==
            fo4::VmadStructureMutationStatus::updated);
    fo4::VmadProperty property;
    property.name = "Typo";
    property.type = 1;
    property.object = fo4::RawFormId{0x800u};
    property.alias = static_cast<std::int16_t>(-1);
    REQUIRE(fo4::add_vmad_property(plugin, quest, 0, 0, property) ==
            fo4::VmadStructureMutationStatus::updated);
    plugin = settled(plugin);

    fo4ide::plugin::ValidationOptions options;
    options.plugin = &plugin;
    options.scriptsDirectories = {temp.root};
    const auto report = fo4ide::plugin::validatePlugin(options);
    const auto* finding = findingFor(report, "SCRIPT_PROPERTY_MISSING");
    REQUIRE(finding != nullptr);
    CHECK(finding->severity == fo4ide::plugin::Severity::error);
    CHECK_FALSE(hasCode(report, "SCRIPT_PARENT_MISSING"));
}

// Detects: a script found in a later search folder must not be reported missing.
TEST_CASE("A script found in any listed scripts folder is not reported missing", "[validation]") {
    TempDir temp;
    std::error_code ec;
    std::filesystem::create_directories(temp.root / "Beside", ec);
    writePex(temp.root / "Beside" / "SecondRootScript.pex", "SecondRootScript",
             {objectProperty("Known", "GlobalVariable")});

    auto plugin = settled(emptyPlugin());
    const auto quest = prepareQuest(plugin, "SecondRootQuest", 0x0009);
    REQUIRE(fo4::add_vmad_script(plugin, quest, 0, "SecondRootScript", 0) ==
            fo4::VmadStructureMutationStatus::updated);
    plugin = settled(plugin);

    fo4ide::plugin::ValidationOptions options;
    options.plugin = &plugin;
    options.scriptsDirectories = {temp.root / "Data", temp.root / "Beside"};
    const auto report = fo4ide::plugin::validatePlugin(options);
    CHECK_FALSE(hasCode(report, "SCRIPT_MISSING"));
}

// Detects: a set_field path in requiredFields must be refused rather than truncated.
TEST_CASE("A requiredFields entry that is a set_field path is refused by name", "[validation]") {
    auto plugin = settled(emptyPlugin());
    createRecord(plugin, kGlob, "PathGlobal");
    plugin = settled(plugin);

    const auto contract = nlohmann::json::parse(
        R"({"requiredFields": [{"editorId": "PathGlobal", "field": "EYES\\Full"}]})");
    fo4ide::plugin::ValidationOptions options;
    options.plugin = &plugin;
    options.contract = &contract;
    const auto report = fo4ide::plugin::validatePlugin(options);
    const auto* finding = findingFor(report, "CONTRACT_FIELD_UNSUPPORTED");
    REQUIRE(finding != nullptr);
    CHECK(finding->field == "EYES\\Full");
    CHECK_FALSE(hasCode(report, "CONTRACT_REQUIRED_FIELD_UNSET"));
}

// Detects: a malformed contract must not echo its contents in the parse error.
TEST_CASE("A contract that is not JSON is refused without echoing its text", "[validation]") {
    const auto parsed = fo4ide::plugin::parseContract(R"({"secretToken": tru})");
    REQUIRE_FALSE(parsed.error.empty());
    CHECK(parsed.error.find("secretToken") == std::string::npos);
}

// Detects: a float GLOB created by the backend must satisfy a float contract.
TEST_CASE("A GLOB created as float through create_record satisfies a float contract", "[validation]") {
    TempDir temp;
    const auto path = temp.root / "Patch.esp";
    {
        const auto bytes = fo4::write_plugin(settled(emptyPlugin()));
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    fo4ide::plugin::RadiclePluginBackend backend;
    const auto loaded = backend.invoke(
        "environment.LoadMo2",
        nlohmann::json{{"plugins", nlohmann::json::array({{{"name", "Patch.esp"},
                                                           {"path", path.string()},
                                                           {"source", "ValidationTest"},
                                                           {"resolved", true}}})}});
    REQUIRE(loaded.has_value());
    const auto created = backend.invokeBackend(
        "CreateRecord",
        nlohmann::json::array({"Patch.esp", "GLOB", nlohmann::json{{"editorId", "FloatGlobal"}, {"type", "float"}}}));
    REQUIRE(created.has_value());

    const auto contract = nlohmann::json::parse(
        R"({"globals": [{"editorId": "FloatGlobal", "type": "float"}]})").dump();
    const auto result = backend.invoke("backend.ValidatePlugin",
                                       nlohmann::json::array({"Patch.esp", contract}));
    REQUIRE(result.has_value());
    REQUIRE(result->contains("findings"));
    for (const auto& row : result->at("findings")) {
        CHECK(row.at("code").get<std::string>() != "CONTRACT_GLOB_TYPE_MISMATCH");
    }
}
