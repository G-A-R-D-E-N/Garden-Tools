#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fo4ide/archive/Ba2Reader.hpp>
#include <fo4ide/papyrus/AnalysisService.hpp>
#include <fo4ide/papyrus/Decompiler.hpp>
#include <fo4ide/papyrus/Parser.hpp>
#include <fo4ide/papyrus/Pex.hpp>
#include <fo4ide/papyrus/ScriptIndex.hpp>
#include <fo4ide/papyrus/VanillaScriptSources.hpp>
#include <fo4ide/plugin/RadiclePluginBackend.hpp>

#include "../helpers/GraphTestSupport.hpp"
#include "plugin_bytes.hpp"

namespace {

namespace fs = std::filesystem;
using fo4ide::archive::Ba2Reader;
using fo4ide::papyrus::Decompiler;
using fo4ide::papyrus::PapyrusCompileOptions;
using fo4ide::papyrus::PexFile;
using fo4ide::papyrus::Parser;
using fo4ide::papyrus::ScriptIndex;
using fo4ide::plugin::RadiclePluginBackend;
using Args = nlohmann::json;

constexpr std::string_view kPluginName = "Patch.esp";
constexpr auto& kImportFolders = fo4ide::papyrus::kVanillaImportFolders;

fs::path tempRoot() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    return fs::temp_directory_path() / ("fo4ide_quest_script_edit_" + std::to_string(id));
}

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

std::string lowerText(std::string text) {
    for (auto& ch : text) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return text;
}

std::size_t countOf(const std::string& text, std::string_view needle) {
    std::size_t count = 0;
    for (auto at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) ++count;
    return count;
}

std::string firstError(const std::vector<fo4ide::papyrus::Diagnostic>& diagnostics) {
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.severity == fo4ide::papyrus::Severity::Error) return diagnostic.toString();
    }
    return {};
}

// Requires the source to compile against the vanilla roots; an empty result means it compiles.
std::string compileError(ScriptIndex& index, const std::string& source, const std::string& name) {
    auto script = Parser::parse(source, name + ".psc");
    if (script.hasErrors()) return "parse: " + firstError(script.diagnostics);
    PapyrusCompileOptions options;
    options.emitDebugInfo = false;
    options.emitDebugOnlyCode = false;
    options.emitBetaOnlyCode = false;
    auto result = fo4ide::papyrus::compileScript(index, &script, name + ".psc", options);
    if (!result.sourcesComplete) return "imports incomplete";
    if (!result.pex) return "compile: " + firstError(result.diagnostics);
    return firstError(result.diagnostics);
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
    const nlohmann::json environment{{"plugins", nlohmann::json::array({{
        {"name", std::string(kPluginName)}, {"path", path.string()}, {"source", "QuestScriptEdit"}, {"resolved", true}}})}};
    REQUIRE(backend.invoke("environment.LoadMo2", environment).has_value());
}

Args optionalString(const std::optional<std::string>& value) {
    return value ? Args(*value) : Args(nullptr);
}

Args optionalPath(const std::optional<fs::path>& value) {
    return value ? Args(value->string()) : Args(nullptr);
}

std::string editsJson(const std::string& find, const std::string& replace) {
    return Args::array({Args{{"find", find}, {"replace", replace}}}).dump();
}

fo4ide::Result<Args> editScript(RadiclePluginBackend& backend, const std::string& name, const fs::path& output,
                                const std::optional<std::string>& source, const std::optional<std::string>& edits,
                                const std::optional<fs::path>& imports) {
    return backend.invokeBackend("EditQuestScript", Args::array({
        std::string(kPluginName), name, output.string(), optionalString(source), optionalString(edits),
        optionalPath(imports)}));
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

// ---- Real vanilla helpers: requires the Fallout 4 Data folder named by FO4_DATA_DIR ----

std::optional<fs::path> vanillaDataDir() {
    fs::path data = "D:/SteamLibrary/steamapps/common/Fallout 4/Data";
    if (const char* env = std::getenv("FO4_DATA_DIR"); env != nullptr && *env != '\0') data = env;
    std::error_code error;
    if (!fs::exists(data / "Fallout4 - Misc.ba2", error)) return std::nullopt;
    if (!fs::exists(data / "Scripts" / "Source" / "Base", error)) return std::nullopt;
    return data;
}

struct Sample {
    std::size_t index{0};
    std::string name;  // Requires the lowercase file stem
};

std::vector<Sample> questSamples(const Ba2Reader& reader, bool fragments) {
    std::vector<Sample> out;
    for (std::size_t i = 0; i < reader.entryCount(); ++i) {
        auto path = lowerText(reader.entryPath(i));
        std::replace(path.begin(), path.end(), '\\', '/');
        if (!path.starts_with("scripts/") || !path.ends_with(".pex")) continue;
        const auto leaf = path.substr(path.find_last_of('/') + 1);
        const auto name = leaf.substr(0, leaf.size() - 4);
        if (fragments) {
            if (name.starts_with("qf_")) out.push_back(Sample{i, name});
        } else if (name.size() > 2 && name.starts_with("mq") && std::isdigit(static_cast<unsigned char>(name[2])) != 0) {
            out.push_back(Sample{i, name});
        }
    }
    return out;
}

std::optional<PexFile> loadPex(const Ba2Reader& reader, std::size_t index) {
    const auto raw = reader.extract(index);
    std::vector<std::uint8_t> bytes(raw.size());
    std::transform(raw.begin(), raw.end(), bytes.begin(), [](std::byte b) { return static_cast<std::uint8_t>(b); });
    try {
        auto pex = PexFile::fromBytes(bytes);
        // Constraint: namespaced objects (Hardcore:Fragments:...) are outside the plain-identifier name rule.
        if (pex.objects.size() == 1 && pex.objects.front().name.find(':') != std::string::npos) return std::nullopt;
        return pex;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool hasNamedState(const PexFile& pex) {
    for (const auto& object : pex.objects) {
        for (const auto& state : object.states) {
            if (!state.name.empty()) return true;
        }
    }
    return false;
}

// Requires the backend read path, the same call an MCP client makes.
std::optional<Args> fetchScript(RadiclePluginBackend& backend, const std::string& name) {
    const auto result = backend.invokeBackend("GetQuestScript", Args::array({name}));
    if (!result) return std::nullopt;
    return *result;
}

struct SetStageEdit {
    std::string find;
    std::string replace;
};

// Constraint: the picked call text must occur exactly once; the sampled vanilla QF_ scripts never call SetStage.
std::optional<SetStageEdit> uniqueQuestCall(const std::string& source) {
    const auto lower = lowerText(source);
    constexpr std::array<std::string_view, 3> kHeads{"setstage(", "setobjectivedisplayed(", "setobjectivecompleted("};
    for (const auto head : kHeads) {
        for (auto at = lower.find(head); at != std::string::npos; at = lower.find(head, at + 1)) {
            const auto start = at + head.size();
            auto end = start;
            while (end < source.size() && std::isdigit(static_cast<unsigned char>(source[end])) != 0) ++end;
            if (end == start || end - start > 6) continue;
            const auto close = source.find(')', end);
            if (close == std::string::npos || source.find('\n', at) < close) continue;
            const auto find = source.substr(at, close + 1 - at);
            if (countOf(source, find) != 1) continue;
            const auto value = std::stoi(source.substr(start, end - start));
            return SetStageEdit{find, source.substr(at, start - at) + std::to_string(value + 1) +
                                          source.substr(end, close + 1 - end)};
        }
    }
    return std::nullopt;
}

}  // namespace

// ---- Synthetic: requires the staged-source seam and every refusal, runnable without the game ----

TEST_CASE("QuestScriptEdit.GetQuestScriptReadsLooseImportSource") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    auto s = makeSandbox(root);
    addStubData(s);
    writeText(s.imports / "QF_Synth.psc", kSynthFragment);
    RadiclePluginBackend backend;
    backend.setDataDirectory(s.data);
    loadPatch(backend, s.plugin);
    const auto result = backend.invokeBackend("GetQuestScript", Args::array({"QF_Synth", s.imports.string()}));
    INFO("error: " << (result.has_value() ? std::string() : result.error().message));
    REQUIRE(result.has_value());
    CHECK((*result)["origin"] == "imports_psc");
    CHECK((*result)["extends"] == "Quest");
    CHECK((*result)["parses"] == true);
    CHECK((*result)["source"] == kSynthFragment);
}

TEST_CASE("QuestScriptEdit.StagesSetStageChangeThatRedecompiles") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    auto s = makeSandbox(root);
    addStubData(s);
    writeText(s.imports / "QF_Synth.psc", kSynthFragment);
    RadiclePluginBackend backend;
    backend.setDataDirectory(s.data);
    loadPatch(backend, s.plugin);

    const auto result = editScript(backend, "QF_Synth", s.out, std::nullopt,
                                   editsJson("SetStage(10)", "SetStage(20)"), s.imports);
    INFO("error: " << (result.has_value() ? std::string() : result.error().message));
    REQUIRE(result.has_value());
    CHECK((*result)["origin"] == "imports_psc");
    // Staging is in memory only: the plugin file on disk must not change before save_plugin.
    CHECK(readBytes(s.plugin) == emptyPlugin());

    const auto staged = backend.testStagedQuestScript(kPluginName);
    REQUIRE(staged.has_value());
    CHECK(staged->first.find("SetStage(20)") != std::string::npos);
    CHECK(staged->first.find("SetStage(10)") == std::string::npos);
    const auto redecompiled = Decompiler::toSource(PexFile::fromBytes(staged->second));
    CHECK(redecompiled.find("[decompile failed:") == std::string::npos);
    CHECK(countOf(redecompiled, "SetStage(20)") == 1);
    CHECK(countOf(redecompiled, "SetStage(10)") == 0);
}

TEST_CASE("QuestScriptEdit.RefusesEditFindThatIsNotExactlyOnce") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    auto s = makeSandbox(root);
    addStubData(s);
    writeText(s.imports / "QF_Synth.psc", kSynthFragment);
    RadiclePluginBackend backend;
    backend.setDataDirectory(s.data);
    loadPatch(backend, s.plugin);

    const auto missing = editScript(backend, "QF_Synth", s.out, std::nullopt,
                                    editsJson("SetStage(99)", "SetStage(1)"), s.imports);
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().type == "QuestScriptEditNotFound");

    // Invariant: "Function" occurs in both the header and EndFunction, so it must match twice.
    const auto twice = editScript(backend, "QF_Synth", s.out, std::nullopt,
                                  editsJson("Function", "Routine"), s.imports);
    REQUIRE_FALSE(twice.has_value());
    CHECK(twice.error().type == "QuestScriptEditAmbiguous");
    requireUnchanged(backend, s);
}

TEST_CASE("QuestScriptEdit.RefusesNonQuestScript") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    auto s = makeSandbox(root);
    addStubData(s);
    const std::string nonQuest =
        "Scriptname QF_NotQuest extends ObjectReference\n\nFunction Fragment_Stage_0010_Item_00()\nEndFunction\n";
    writeText(s.imports / "QF_NotQuest.psc", nonQuest);
    RadiclePluginBackend backend;
    backend.setDataDirectory(s.data);
    loadPatch(backend, s.plugin);

    const auto refused = editScript(backend, "QF_NotQuest", s.out, nonQuest, std::nullopt, s.imports);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "QuestScriptNotQuest");
    requireUnchanged(backend, s);
}

TEST_CASE("QuestScriptEdit.RefusesCompileErrorWithoutStaging") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    auto s = makeSandbox(root);
    addStubData(s);
    writeText(s.imports / "QF_Synth.psc", kSynthFragment);
    RadiclePluginBackend backend;
    backend.setDataDirectory(s.data);
    loadPatch(backend, s.plugin);

    const auto refused = editScript(backend, "QF_Synth", s.out, std::nullopt,
                                    editsJson("SetStage(10)", "NoSuchFunctionHere(10)"), s.imports);
    INFO("error: " << (refused.has_value() ? std::string() : refused.error().type + " " + refused.error().message));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "QuestScriptCompileFailed");
    requireUnchanged(backend, s);
}

TEST_CASE("QuestScriptEdit.RefusesOutputInsideGameData") {
    const auto root = tempRoot();
    Cleanup cleanup{root};
    auto s = makeSandbox(root);
    addStubData(s);
    writeText(s.imports / "QF_Synth.psc", kSynthFragment);
    RadiclePluginBackend backend;
    backend.setDataDirectory(s.data);
    loadPatch(backend, s.plugin);

    const auto refused = editScript(backend, "QF_Synth", s.data / "Scripts", std::nullopt,
                                    editsJson("SetStage(10)", "SetStage(20)"), s.imports);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().type == "QuestScriptOutputInsideData");
    CHECK(readBytes(s.plugin) == emptyPlugin());
    CHECK_FALSE(backend.testStagedQuestScript(kPluginName).has_value());
    CHECK_FALSE(fs::exists(s.data / "Scripts" / "QF_Synth.psc"));
}

// ---- Real vanilla: requires the Fallout 4 Data folder; these skip with a named reason when it is absent ----

TEST_CASE("QuestScriptReal.GetQuestScriptReturnsCompilableFragmentSource") {
    const auto data = vanillaDataDir();
    if (!data) SKIP("Fallout 4 Data folder not found; set FO4_DATA_DIR to it");
    const Ba2Reader reader = Ba2Reader::open(*data / "Fallout4 - Misc.ba2");
    ScriptIndex index;
    REQUIRE(fo4ide::papyrus::addVanillaImports(index, *data, kImportFolders) == std::size(kImportFolders));
    RadiclePluginBackend backend;
    backend.setDataDirectory(*data);

    bool compiled = false;
    std::string lastError;
    for (const auto& sample : questSamples(reader, true)) {
        const auto pex = loadPex(reader, sample.index);
        if (!pex || pex->objects.size() != 1) continue;
        const auto name = pex->objects.front().name;
        const auto fetched = fetchScript(backend, name);
        REQUIRE(fetched.has_value());
        CHECK((*fetched)["origin"] == "vanilla_pex");
        const std::string source = (*fetched)["source"];
        CHECK(source.find("[decompile failed:") == std::string::npos);
        lastError = compileError(index, source, name);
        if (lastError.empty()) {
            compiled = true;
            break;
        }
    }
    CHECK(compiled);
    INFO("last compile error: " << lastError);
}

TEST_CASE("QuestScriptReal.GetQuestScriptReturnsCompilableMainQuestSource") {
    const auto data = vanillaDataDir();
    if (!data) SKIP("Fallout 4 Data folder not found; set FO4_DATA_DIR to it");
    ScriptIndex index;
    REQUIRE(fo4ide::papyrus::addVanillaImports(index, *data, kImportFolders) == std::size(kImportFolders));
    RadiclePluginBackend backend;
    backend.setDataDirectory(*data);

    const std::string name = "MQ101RadScannerTriggerScript";
    const auto fetched = fetchScript(backend, name);
    REQUIRE(fetched.has_value());
    CHECK((*fetched)["origin"] == "vanilla_pex");
    CHECK((*fetched)["script"] == name);
    const std::string source = (*fetched)["source"];
    CHECK(source.find("[decompile failed:") == std::string::npos);
    CHECK(compileError(index, source, name).empty());
}

TEST_CASE("QuestScriptReal.GetQuestScriptReturnsCompilableScriptWithStates") {
    const auto data = vanillaDataDir();
    if (!data) SKIP("Fallout 4 Data folder not found; set FO4_DATA_DIR to it");
    const Ba2Reader reader = Ba2Reader::open(*data / "Fallout4 - Misc.ba2");
    ScriptIndex index;
    REQUIRE(fo4ide::papyrus::addVanillaImports(index, *data, kImportFolders) == std::size(kImportFolders));
    RadiclePluginBackend backend;
    backend.setDataDirectory(*data);

    bool found = false;
    bool compiled = false;
    for (const auto& sample : questSamples(reader, false)) {
        const auto pex = loadPex(reader, sample.index);
        if (!pex || pex->objects.size() != 1 || !hasNamedState(*pex)) continue;
        found = true;
        const auto name = pex->objects.front().name;
        const auto fetched = fetchScript(backend, name);
        REQUIRE(fetched.has_value());
        const std::string source = (*fetched)["source"];
        CHECK(lowerText(source).find("state ") != std::string::npos);
        compiled = compileError(index, source, name).empty();
        break;
    }
    REQUIRE(found);
    CHECK(compiled);
}

TEST_CASE("QuestScriptReal.EditSetStageOnVanillaFragmentRecompilesWithChange") {
    const auto data = vanillaDataDir();
    if (!data) SKIP("Fallout 4 Data folder not found; set FO4_DATA_DIR to it");
    const Ba2Reader reader = Ba2Reader::open(*data / "Fallout4 - Misc.ba2");
    const auto root = tempRoot();
    Cleanup cleanup{root};
    auto s = makeSandbox(root);
    RadiclePluginBackend backend;
    backend.setDataDirectory(*data);
    loadPatch(backend, s.plugin);

    bool edited = false;
    std::string lastError;
    for (const auto& sample : questSamples(reader, true)) {
        const auto pex = loadPex(reader, sample.index);
        if (!pex || pex->objects.size() != 1) continue;
        const auto name = pex->objects.front().name;
        const auto fetched = fetchScript(backend, name);
        if (!fetched) continue;
        const std::string source = (*fetched)["source"];
        const auto edit = uniqueQuestCall(source);
        if (!edit || source.find("[decompile failed:") != std::string::npos) continue;

        const auto result = editScript(backend, name, s.out, std::nullopt, editsJson(edit->find, edit->replace),
                                       std::nullopt);
        if (!result) {
            lastError = name + ": " + result.error().type + " " + result.error().message;
            continue;
        }
        CHECK((*result)["origin"] == "vanilla_pex");
        const auto staged = backend.testStagedQuestScript(kPluginName);
        REQUIRE(staged.has_value());
        const auto redecompiled = Decompiler::toSource(PexFile::fromBytes(staged->second));
        CHECK(redecompiled.find("[decompile failed:") == std::string::npos);
        CHECK(countOf(redecompiled, edit->replace) == countOf(source, edit->replace) + 1);
        CHECK(countOf(redecompiled, edit->find) == 0);
        CHECK(readBytes(s.plugin) == emptyPlugin());
        edited = true;
        break;
    }
    INFO("last edit error: " << lastError);
    CHECK(edited);
}

TEST_CASE("QuestScriptReal.EditRefusalsLeavePluginUnchanged") {
    const auto data = vanillaDataDir();
    if (!data) SKIP("Fallout 4 Data folder not found; set FO4_DATA_DIR to it");
    const Ba2Reader reader = Ba2Reader::open(*data / "Fallout4 - Misc.ba2");
    const auto root = tempRoot();
    Cleanup cleanup{root};
    auto s = makeSandbox(root);
    RadiclePluginBackend backend;
    backend.setDataDirectory(*data);
    loadPatch(backend, s.plugin);

    std::optional<std::string> name;
    std::optional<std::string> source;
    std::optional<SetStageEdit> edit;
    for (const auto& sample : questSamples(reader, true)) {
        const auto pex = loadPex(reader, sample.index);
        if (!pex || pex->objects.size() != 1) continue;
        const auto candidate = pex->objects.front().name;
        const auto fetched = fetchScript(backend, candidate);
        if (!fetched) continue;
        const std::string text = (*fetched)["source"];
        edit = uniqueQuestCall(text);
        if (!edit) continue;
        name = candidate;
        source = text;
        break;
    }
    REQUIRE(name.has_value());
    REQUIRE(edit.has_value());

    const auto notFound = editScript(backend, *name, s.out, std::nullopt,
                                     editsJson("NoSuchTextInScript_9f3a", "x"), std::nullopt);
    REQUIRE_FALSE(notFound.has_value());
    CHECK(notFound.error().type == "QuestScriptEditNotFound");
    requireUnchanged(backend, s);

    const auto ambiguous = editScript(backend, *name, s.out, std::nullopt, editsJson("Function", "Routine"),
                                      std::nullopt);
    REQUIRE_FALSE(ambiguous.has_value());
    CHECK(ambiguous.error().type == "QuestScriptEditAmbiguous");
    requireUnchanged(backend, s);

    const auto compileFailure = editScript(backend, *name, s.out, std::nullopt,
                                           editsJson(edit->find, "NoSuchFunctionHere(0)"), std::nullopt);
    REQUIRE_FALSE(compileFailure.has_value());
    CHECK(compileFailure.error().type == "QuestScriptCompileFailed");
    requireUnchanged(backend, s);

    const auto insideData = editScript(backend, *name, *data / "Scripts", std::nullopt,
                                       editsJson(edit->find, edit->replace), std::nullopt);
    REQUIRE_FALSE(insideData.has_value());
    CHECK(insideData.error().type == "QuestScriptOutputInsideData");
    CHECK(readBytes(s.plugin) == emptyPlugin());
    CHECK_FALSE(backend.testStagedQuestScript(kPluginName).has_value());

    // Constraint: a non-Quest script must be refused even when its source is otherwise unchanged.
    const auto mainName = std::string("MQ101RadScannerTriggerScript");
    const auto mainFetched = fetchScript(backend, mainName);
    REQUIRE(mainFetched.has_value());
    CHECK((*mainFetched)["extends"] != "Quest");
    const std::string mainSource = (*mainFetched)["source"];
    const auto nonQuest = editScript(backend, mainName, s.out, mainSource, std::nullopt, std::nullopt);
    REQUIRE_FALSE(nonQuest.has_value());
    CHECK(nonQuest.error().type == "QuestScriptNotQuest");
    requireUnchanged(backend, s);
}
