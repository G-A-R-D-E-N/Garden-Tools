#include <catch2/catch_test_macros.hpp>

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/archive/ba2.hpp>
#include <fo4ide/papyrus/AnalysisService.hpp>
#include <fo4ide/papyrus/Parser.hpp>
#include <fo4ide/papyrus/Pex.hpp>
#include <fo4ide/papyrus/ScriptIndex.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "../helpers/GraphTestSupport.hpp"
#include "../helpers/TempRoot.hpp"
#include "plugin_bytes.hpp"

namespace {

namespace fs = std::filesystem;
using fo4ide::plugin::RadiclePluginBackend;
using Args = nlohmann::json;

constexpr std::string_view kPluginName = "Patch.esp";

fs::path tempRoot() { return fs::temp_directory_path() / ("fo4ide_quest_script_safety_" + fo4ide_test::uniqueTempName()); }

struct Cleanup {
    fs::path root;
    ~Cleanup() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

void writeText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    REQUIRE(out.good());
    out << text;
}

void writeBytes(const fs::path& path, const std::vector<std::byte>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    REQUIRE(out.good());
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(out.good());
}

std::vector<std::byte> readBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    REQUIRE(in.good());
    const auto size = in.tellg();
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    in.seekg(0);
    if (!bytes.empty()) in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return bytes;
}

std::vector<std::byte> emptyPlugin() {
    fo4::test::ByteWriter out;
    const auto tes4 = fo4::test::begin_record(out, "TES4", 0u, 0);
    fo4::test::ByteWriter hedr;
    hedr.f32(1.0f);
    hedr.u32(0);
    hedr.u32(0x800);
    fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
    fo4::test::end_record(out, tes4);
    return out.bytes;
}

// Invariant: a sandbox keeps the plugin, its trusted save root and output folder inside one temporary directory.
struct Sandbox {
    fs::path root;
    fs::path data;
    fs::path plugin;
    fs::path imports;
    fs::path out;
};

Sandbox makeSandbox(const fs::path& root) {
    Sandbox s{root, root / "Data", root / std::string(kPluginName), root / "imports", root / "out"};
    fs::create_directories(s.imports);
    fs::create_directories(s.out);
    writeBytes(s.plugin, emptyPlugin());
    return s;
}

// Constraint: the base stubs stand in for Scripts/Source/Base so that no real game is required.
void addStubData(Sandbox& s) {
    fs::create_directories(s.data / "Scripts" / "Source");
    fs::copy(fo4::graph::test::baseStubs(), s.data / "Scripts" / "Source" / "Base", fs::copy_options::recursive);
}

void loadPatch(RadiclePluginBackend& backend, const fs::path& path) {
    const Args environment{{"plugins", Args::array({{
        {"name", std::string(kPluginName)}, {"path", path.string()}, {"source", "QuestScriptSafety"}, {"resolved", true}}})}};
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
}

std::string editsJson(const std::string& find, const std::string& replace) {
    return Args::array({Args{{"find", find}, {"replace", replace}}}).dump();
}

fo4ide::Result<Args> getScript(RadiclePluginBackend& backend, const std::string& name, const fs::path& imports) {
    return backend.invokeBackend("GetQuestScript", Args::array({name, imports.string()}));
}

fo4ide::Result<Args> editScript(RadiclePluginBackend& backend, const std::string& name, const fs::path& output,
                                const std::optional<std::string>& edits, const std::optional<fs::path>& imports) {
    return backend.invokeBackend("EditQuestScript", Args::array({
        std::string(kPluginName), name, output.string(),
        nullptr, edits ? Args(*edits) : Args(nullptr), imports ? Args(imports->string()) : Args(nullptr)}));
}

// Refusals must leave the plugin file, the staged state and the output folder untouched.
void requireUnchanged(const RadiclePluginBackend& backend, const Sandbox& s) {
    CHECK(readBytes(s.plugin) == emptyPlugin());
    CHECK_FALSE(backend.testStagedQuestScript(kPluginName).has_value());
    CHECK(fs::is_empty(s.out));
}

const std::string kSynthFragment =
    "Scriptname QF_Synth extends Quest\n"
    "\n"
    "Function Fragment_Stage_0010_Item_00()\n"
    "    SetStage(10)\n"
    "EndFunction\n";

std::string namespacedFragment(int stage) {
    return "Scriptname Fragments:Quests:QF_Synth extends Quest\n"
           "\n"
           "Function Fragment_Stage_0010_Item_00()\n"
           "    SetStage(" + std::to_string(stage) + ")\n"
           "EndFunction\n";
}

// Constraint: a directory junction needs no elevation; a failed mklink makes the caller skip with a reason.
bool makeJunction(const fs::path& link, const fs::path& target) {
    std::error_code ec;
    fs::create_directories(target, ec);
    const auto command = "mklink /J \"" + link.string() + "\" \"" + target.string() + "\" >nul 2>&1";
    return std::system(command.c_str()) == 0 && fs::is_directory(link, ec);
}

// Constraint: compiles against the base stubs like the staging path does, so the PEX bytes are real.
std::vector<std::uint8_t> compiledPex(const std::string& source, const std::string& name) {
    fo4ide::papyrus::ScriptIndex index;
    index.addRoot(fo4::graph::test::baseStubs());
    auto script = fo4ide::papyrus::Parser::parse(source, name + ".psc");
    REQUIRE_FALSE(script.hasErrors());
    fo4ide::papyrus::PapyrusCompileOptions options;
    options.emitDebugInfo = false;
    options.emitDebugOnlyCode = false;
    options.emitBetaOnlyCode = false;
    auto result = fo4ide::papyrus::compileScript(index, &script, name + ".psc", options);
    REQUIRE(result.success());
    REQUIRE(result.pex);
    return result.pex->toBytes();
}

}  // namespace

TEST_CASE("QuestScriptSafety.RefusesWindowsReservedScriptSegments") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    auto s = makeSandbox(root);
    addStubData(s);
    writeText(s.imports / "QF_Synth.psc", kSynthFragment);
    RadiclePluginBackend backend;
    backend.setDataDirectory(s.data);
    loadPatch(backend, s.plugin);

    // Constraint: device names are refused in any case, bare or as a namespace segment, and COM0/LPT0/COM10 are not reserved.
    const std::vector<std::string> reserved{"CON", "prn", "Aux", "NUL", "com1", "COM9", "LPT1", "lpt9",
                                            "Fragments:CON:QF_Synth", "Fragments:Quests:nul", "Con:QF_Synth"};
    for (const auto& name : reserved) {
        INFO("name: " << name);
        const auto fetched = getScript(backend, name, s.imports);
        REQUIRE_FALSE(fetched.has_value());
        CHECK(fetched.error().type == "QuestScriptFailed");
        CHECK(fetched.error().message.find("script name must be a Papyrus identifier") != std::string::npos);
        const auto edited = editScript(backend, name, s.out, editsJson("SetStage(10)", "SetStage(20)"), s.imports);
        REQUIRE_FALSE(edited.has_value());
        CHECK(edited.error().type == "QuestScriptFailed");
    }
    requireUnchanged(backend, s);

    // Constraint: names that only look like device names are ordinary identifiers and must reach the lookup.
    for (const std::string name : {"CONSOLE", "COM10", "LPT0", "NULL"}) {
        INFO("name: " << name);
        const auto fetched = getScript(backend, name, s.imports);
        REQUIRE_FALSE(fetched.has_value());
        CHECK(fetched.error().type == "QuestScriptNotFound");
    }
}

TEST_CASE("QuestScriptSafety.RefusesImportJunctionEscapingImportsRoot") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    auto s = makeSandbox(root);
    addStubData(s);
    const auto outside = root / "outside";
    writeText(outside / "Quests" / "QF_Synth.psc", namespacedFragment(10));
    if (!makeJunction(s.imports / "Fragments", outside)) {
        SKIP("cannot create a directory junction with mklink /J on this host");
    }
    RadiclePluginBackend backend;
    backend.setDataDirectory(s.data);
    loadPatch(backend, s.plugin);

    const auto fetched = getScript(backend, "Fragments:Quests:QF_Synth", s.imports);
    REQUIRE_FALSE(fetched.has_value());
    CHECK(fetched.error().type == "QuestScriptFailed");
    CHECK(fetched.error().message.find("outside the imports directory") != std::string::npos);
    const auto edited = editScript(backend, "Fragments:Quests:QF_Synth", s.out,
                                   editsJson("SetStage(10)", "SetStage(20)"), s.imports);
    REQUIRE_FALSE(edited.has_value());
    CHECK(edited.error().message.find("outside the imports directory") != std::string::npos);
    requireUnchanged(backend, s);
}

TEST_CASE("QuestScriptSafety.RefusesImportFileSymlinkEscapingImportsRoot") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    auto s = makeSandbox(root);
    addStubData(s);
    writeText(root / "outside" / "QF_Synth.psc", kSynthFragment);
    std::error_code ec;
    fs::create_symlink(root / "outside" / "QF_Synth.psc", s.imports / "QF_Synth.psc", ec);
    if (ec) SKIP("cannot create a file symlink on this host (needs Developer Mode or elevation)");
    RadiclePluginBackend backend;
    backend.setDataDirectory(s.data);
    loadPatch(backend, s.plugin);

    const auto fetched = getScript(backend, "QF_Synth", s.imports);
    REQUIRE_FALSE(fetched.has_value());
    CHECK(fetched.error().type == "QuestScriptFailed");
    CHECK(fetched.error().message.find("outside the imports directory") != std::string::npos);
    requireUnchanged(backend, s);
}

namespace {

// Constraint: set_quest_fragment binds fragment stage 10 of the quest, so the quest must already carry that stage.
std::string createStagedQuest(RadiclePluginBackend& backend, const std::string& editorId) {
    const auto created = backend.invokeBackend("CreateRecord", Args::array({std::string(kPluginName), "QUST", Args{{"editorId", editorId}}}));
    REQUIRE(created.has_value());
    const auto record = created->is_string() ? nlohmann::json::parse(created->get<std::string>()) : *created;
    const auto key = record.at("formKey").get<std::string>();
    REQUIRE(backend.invokeBackend("SetQuestStages", Args::array({std::string(kPluginName), key,
        Args::array({{{"index", 10}, {"logEntry", "Begin"}}})})).has_value());
    return key;
}

Args fragmentArgs(const std::string& key, const Sandbox& s) {
    return Args::array({std::string(kPluginName), key, 10, 0, "QF_Synth", s.out.string(), s.imports.string(),
                        Args::array({{{"kind", "stage"}, {"value", 10}}})});
}

std::size_t countOccurrences(const std::string& text, std::string_view needle) {
    std::size_t count = 0;
    for (auto at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size())) ++count;
    return count;
}

}  // namespace

TEST_CASE("QuestScriptSafety.EditKeepsStagedFragmentQuestKey") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    auto s = makeSandbox(root);
    addStubData(s);
    fs::copy(fo4::graph::test::baseStubs(), s.imports, fs::copy_options::recursive);
    RadiclePluginBackend backend;
    backend.setDataDirectory(s.data);
    loadPatch(backend, s.plugin);
    const auto key = createStagedQuest(backend, "CoWelcome");
    REQUIRE(backend.invokeBackend("SetQuestFragment", fragmentArgs(key, s)).has_value());

    // Invariant: an edit of a fragment staged by set_quest_fragment keeps its quest key, so binding the same quest again succeeds.
    REQUIRE(editScript(backend, "QF_Synth", s.out, editsJson("SetStage(10)", "SetStage(10)\n    SetStage(10)"), s.imports).has_value());
    const auto edited = backend.testStagedQuestScript(kPluginName);
    REQUIRE(edited.has_value());
    CHECK(countOccurrences(edited->first, "SetStage(10)") == 2);

    const auto rebound = backend.invokeBackend("SetQuestFragment", fragmentArgs(key, s));
    REQUIRE(rebound.has_value());
    const auto restaged = backend.testStagedQuestScript(kPluginName);
    REQUIRE(restaged.has_value());
    CHECK(countOccurrences(restaged->first, "SetStage(10)") == 1);
    REQUIRE(backend.invoke("backend.DiscardPlugin", Args::array({std::string(kPluginName)})).has_value());
}

TEST_CASE("QuestScriptSafety.EditFirstStillRefusesLaterFragment") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    auto s = makeSandbox(root);
    addStubData(s);
    writeText(s.imports / "QF_Synth.psc", kSynthFragment);
    RadiclePluginBackend backend;
    backend.setDataDirectory(s.data);
    loadPatch(backend, s.plugin);
    const auto key = createStagedQuest(backend, "CoWelcome");

    // Constraint: a script first staged by edit_quest_script has no quest key, so set_quest_fragment on it is refused.
    REQUIRE(editScript(backend, "QF_Synth", s.out, editsJson("SetStage(10)", "SetStage(20)"), s.imports).has_value());
    const auto refused = backend.invokeBackend("SetQuestFragment", fragmentArgs(key, s));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message.find("already staged") != std::string::npos);
    REQUIRE(backend.invoke("backend.DiscardPlugin", Args::array({std::string(kPluginName)})).has_value());
}

namespace {

std::vector<std::byte> asBytes(const std::vector<std::uint8_t>& bytes) {
    std::vector<std::byte> out;
    out.reserve(bytes.size());
    for (const auto b : bytes) out.push_back(static_cast<std::byte>(b));
    return out;
}

// Constraint: a vanilla script sits under scripts/ inside a BA2, the same entry shape the game archives use.
void writeScriptArchive(const fs::path& path, const std::vector<std::uint8_t>& pex) {
    fo4::archive::Ba2GeneralEntry entry;
    entry.path = "scripts\\qf_synth.pex";
    entry.chunks.push_back(fo4::archive::make_stored_ba2_chunk(asBytes(pex)));
    fo4::archive::Ba2GeneralArchive archive;
    archive.entries.push_back(std::move(entry));
    fo4::archive::write_ba2_general_file(archive, path);
}

std::string stageSource(int stage) {
    return "Scriptname QF_Synth extends Quest\n"
           "\n"
           "Function Fragment_Stage_0010_Item_00()\n"
           "    SetStage(" + std::to_string(stage) + ")\n"
           "EndFunction\n";
}

}  // namespace

TEST_CASE("QuestScriptSafety.LooseScriptOverridesVanillaArchive") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    auto s = makeSandbox(root);
    addStubData(s);
    writeScriptArchive(s.data / "Fallout4 - Misc.ba2", compiledPex(stageSource(10), "QF_Synth"));
    writeBytes(s.data / "Scripts" / "QF_Synth.pex", asBytes(compiledPex(stageSource(30), "QF_Synth")));
    RadiclePluginBackend backend;
    backend.setDataDirectory(s.data);
    loadPatch(backend, s.plugin);

    // Invariant: a loose Data\Scripts file wins over every archive and is reported with the archive name "loose".
    const auto fetched = getScript(backend, "QF_Synth", s.imports);
    REQUIRE(fetched.has_value());
    CHECK(fetched->at("origin").get<std::string>() == "vanilla_pex");
    CHECK(fetched->at("archive").get<std::string>() == "loose");
    const auto source = fetched->at("source").get<std::string>();
    CHECK(source.find("SetStage(30)") != std::string::npos);
    CHECK(source.find("SetStage(10)") == std::string::npos);
    CHECK(fetched->at("notes").empty());
}

TEST_CASE("QuestScriptSafety.LaterArchiveOverridesMiscAndTexturesArchiveIsSkipped") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    auto s = makeSandbox(root);
    addStubData(s);
    writeScriptArchive(s.data / "Fallout4 - Misc.ba2", compiledPex(stageSource(10), "QF_Synth"));
    writeScriptArchive(s.data / "DLCRobot - Main.ba2", compiledPex(stageSource(20), "QF_Synth"));
    // Constraint: " - Textures" archives hold no scripts, so this copy sorts last yet must never be read.
    writeScriptArchive(s.data / "Zeta - Textures.ba2", compiledPex(stageSource(40), "QF_Synth"));
    RadiclePluginBackend backend;
    backend.setDataDirectory(s.data);
    loadPatch(backend, s.plugin);

    const auto fetched = getScript(backend, "QF_Synth", s.imports);
    REQUIRE(fetched.has_value());
    CHECK(fetched->at("archive").get<std::string>() == "DLCRobot - Main.ba2");
    const auto source = fetched->at("source").get<std::string>();
    CHECK(source.find("SetStage(20)") != std::string::npos);
    CHECK(source.find("SetStage(10)") == std::string::npos);
    CHECK(source.find("SetStage(40)") == std::string::npos);
    CHECK(fetched->at("notes").empty());
}

TEST_CASE("QuestScriptSafety.CorruptNonMiscArchiveIsSkippedAndNoted") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    auto s = makeSandbox(root);
    addStubData(s);
    writeScriptArchive(s.data / "Fallout4 - Misc.ba2", compiledPex(stageSource(10), "QF_Synth"));
    writeText(s.data / "DLCRobot - Main.ba2", "this is not a BA2 archive");
    RadiclePluginBackend backend;
    backend.setDataDirectory(s.data);
    loadPatch(backend, s.plugin);

    // Invariant: an unreadable non-Misc archive does not fail the lookup; it is skipped and named in notes.
    const auto fetched = getScript(backend, "QF_Synth", s.imports);
    REQUIRE(fetched.has_value());
    CHECK(fetched->at("archive").get<std::string>() == "Fallout4 - Misc.ba2");
    CHECK(fetched->at("source").get<std::string>().find("SetStage(10)") != std::string::npos);
    const auto notes = fetched->at("notes");
    REQUIRE(notes.is_array());
    REQUIRE(notes.size() == 1);
    CHECK(notes[0].get<std::string>().find("DLCRobot - Main.ba2") != std::string::npos);
}