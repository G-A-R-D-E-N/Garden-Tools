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
