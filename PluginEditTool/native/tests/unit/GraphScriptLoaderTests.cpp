#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <fo4ide/graph/GraphCompiler.hpp>
#include <fo4ide/graph/GraphScriptLoader.hpp>
#include <fo4ide/papyrus/AnalysisService.hpp>
#include <fo4ide/papyrus/Parser.hpp>
#include <fo4ide/papyrus/Pex.hpp>

#include "../helpers/GraphBuilder.hpp"
#include "../helpers/PexComparer.hpp"
#include "../helpers/TempRoot.hpp"

using namespace fo4ide::graph;
using namespace fo4ide::graph::test;
using namespace fo4ide::papyrus;
using fo4ide_test::TempRoot;

namespace {

const std::string kSource = R"(Scriptname Fixture extends ObjectReference

Event OnLoad()
    Disable(false)
EndEvent
)";

std::string describe(const std::vector<GraphDiagnostic>& diagnostics) {
    std::string out;
    for (const auto& diagnostic : diagnostics) {
        if (!out.empty()) out += " | ";
        out += diagnostic.code + " " + diagnostic.message;
    }
    return out;
}

std::vector<std::filesystem::path> roots() {
    return {baseStubs(), graphScripts()};
}

void writeFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

void writeFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

TEST_CASE("GraphScriptLoader a source script becomes a graph") {
    const TempRoot guard("fo4ide-loader-psc");
    const auto path = guard.path() / "Fixture.psc";
    writeFile(path, kSource);

    const auto result = GraphScriptLoader::load(path, roots());

    CHECK_FALSE(result.failure.has_value());
    CHECK(result.success());
    if (!result.success()) INFO(describe(result.diagnostics));
    if (result.document) CHECK_FALSE(result.document->nodes.empty());
}

TEST_CASE("GraphScriptLoader a compiled script becomes the same graph as its source") {
    const TempRoot guard("fo4ide-loader-pex");
    const auto sourcePath = guard.path() / "Fixture.psc";
    writeFile(sourcePath, kSource);

    auto index = [&] {
        ScriptIndex built;
        built.addRoot(guard.path());
        built.addRoot(baseStubs());
        built.addRoot(graphScripts());
        return built;
    }();
    const auto parsed = Parser::parse(kSource, "Fixture.psc");
    auto original = compileScript(index, &parsed, "Fixture.psc", PapyrusCompileOptions{});
    REQUIRE(original.pex != nullptr);

    const auto pexPath = guard.path() / "Fixture.pex";
    writeFile(pexPath, original.pex->toBytes());

    std::filesystem::remove(sourcePath);

    const auto result = GraphScriptLoader::load(pexPath, roots());
    CHECK_FALSE(result.failure.has_value());
    CHECK(result.success());
    if (!result.success()) INFO(describe(result.diagnostics));

    REQUIRE(result.document.has_value());

    NodePalette palette(index);
    GraphCompiler compiler(index, &palette);
    GraphCompileOptions options;
    options.treatPapyrusErrorsAsInternalFaults = true;
    auto document = *result.document;
    const auto rebuilt = compiler.compile(document, options);
    CHECK(rebuilt.success());
    if (!rebuilt.success()) INFO(describe(rebuilt.diagnostics));
    REQUIRE(rebuilt.pex != nullptr);

    const auto difference =
        fo4ide::papyrus::test::firstDifference(original.pex->objects[0],
                                               rebuilt.pex->objects[0]);
    INFO("difference: " << difference);
    CHECK(difference.empty());
}

TEST_CASE("GraphScriptLoader a script that refuses to lift comes back with the reason and no document") {
    const TempRoot guard("fo4ide-loader-refuse");
    const auto path = guard.path() / "Fixture.psc";
    writeFile(path, R"(Scriptname Fixture extends ObjectReference

Event OnLoad()
    While (IsEnabled())
        Utility.Wait(1.0)
    EndWhile
EndEvent
)");

    const auto result = GraphScriptLoader::load(path, roots());

    CHECK_FALSE(result.failure.has_value());
    CHECK_FALSE(result.success());
    CHECK_FALSE(result.document.has_value());
    CHECK_FALSE(result.diagnostics.empty());
}

TEST_CASE("GraphScriptLoader a script is checked against its own folder") {
    const TempRoot guard("fo4ide-loader-roots");
    const auto path = guard.path() / "Fixture.psc";
    writeFile(path, R"(Scriptname Fixture extends ObjectReference

Event OnLoad()
    MoveTo(Self, 0.0, 0.0, 0.0, true)
EndEvent
)");

    const auto result = GraphScriptLoader::load(path, roots());

    CHECK_FALSE(result.failure.has_value());
    CHECK(result.success());
    if (!result.success()) INFO(describe(result.diagnostics));
}

TEST_CASE("GraphScriptLoader a lifted property read validates against the pin the getter declares") {
    const TempRoot guard("fo4ide-loader-prop");
    writeFile(guard.path() / "Holder.psc",
              "Scriptname Holder extends ObjectReference\nint Property Count Auto\n");
    const auto path = guard.path() / "Reader.psc";
    writeFile(path, R"(Scriptname Reader extends ObjectReference

Event OnLoad()
    Holder store = Self as Holder
    Debug.Trace("count " + store.Count)
EndEvent
)");

    std::vector<std::filesystem::path> loadRoots{guard.path(), baseStubs(), graphScripts()};
    const auto result = GraphScriptLoader::load(path, loadRoots);

    CHECK_FALSE(result.failure.has_value());
    CHECK(result.success());
    if (!result.success()) INFO(describe(result.diagnostics));

    REQUIRE(result.document.has_value());

    ScriptIndex index;
    for (const auto& root : loadRoots) index.addRoot(root);
    NodePalette palette(index);
    GraphValidator validator(index, palette);
    auto document = *result.document;
    const auto validation = validator.validate(document);

    std::vector<const GraphDiagnostic*> errors;
    for (const auto& diagnostic : validation.diagnostics) {
        if (diagnostic.severity == GraphSeverity::Error) errors.push_back(&diagnostic);
    }
    INFO(describe(validation.diagnostics));
    CHECK(errors.empty());
}

TEST_CASE("GraphScriptLoader a file that is not a script is refused by name") {
    const TempRoot guard("fo4ide-loader-other");
    const auto path = guard.path() / "Fixture.fograph";
    writeFile(path, "{}");

    const auto result = GraphScriptLoader::load(path, roots());

    REQUIRE(result.failure.has_value());
    CHECK(result.failure->find("Fixture.fograph") != std::string::npos);
    CHECK_FALSE(result.success());
}

TEST_CASE("GraphScriptLoader a missing file is refused rather than thrown") {
    const auto path = std::filesystem::temp_directory_path() /
                      ("fo4re-not-here-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                       ".psc");

    const auto result = GraphScriptLoader::load(path, roots());

    CHECK(result.failure.has_value());
    CHECK_FALSE(result.success());
}

TEST_CASE("GraphScriptLoader the openable extensions are the two the loader reads") {
    CHECK(GraphScriptLoader::isScript(std::filesystem::path("Fixture.psc")));
    CHECK(GraphScriptLoader::isScript(std::filesystem::path("Fixture.PEX")));
    CHECK_FALSE(GraphScriptLoader::isScript(std::filesystem::path("Fixture.fograph")));
    CHECK_FALSE(GraphScriptLoader::isScript(std::filesystem::path("Fixture")));
}
