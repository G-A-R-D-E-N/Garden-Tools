#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <fo4/graph/graph_compiler.hpp>
#include <fo4/graph/graph_script_loader.hpp>
#include <fo4/papyrus/compile_script.hpp>
#include <fo4/papyrus/parser.hpp>
#include <fo4/pex/pex.hpp>

#include "graph_builder.hpp"
#include "papyrus_pex_comparer.hpp"

using namespace fo4::graph;
using namespace fo4::graph::test;
using namespace fo4::papyrus;

namespace {

const std::string kSource = R"(Scriptname Fixture extends ObjectReference

Event OnLoad()
    Disable(false)
EndEvent
)";

std::string uniqueRoot(std::string_view tag) {
    return (std::filesystem::temp_directory_path() / ("fo4re-loader-" + std::string(tag)) /
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))
        .string();
}

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

}

TEST(GraphScriptLoader, GraphScriptLoaderASourceScriptBecomesAGraph) {
    const std::string directory = uniqueRoot("psc");
    std::filesystem::create_directories(directory);
    const auto path = std::filesystem::path(directory) / "Fixture.psc";
    writeFile(path, kSource);

    const auto result = GraphScriptLoader::load(path, roots());

    EXPECT_FALSE(result.failure.has_value());
    EXPECT_TRUE(result.success());
    if (!result.success()) { SCOPED_TRACE((::testing::Message() << describe(result.diagnostics)).GetString()); }
    if (result.document) EXPECT_FALSE(result.document->nodes.empty());

    std::filesystem::remove_all(directory);
}

TEST(GraphScriptLoader, GraphScriptLoaderACompiledScriptBecomesTheSameGraphAsItsSource) {
    const std::string directory = uniqueRoot("pex");
    std::filesystem::create_directories(directory);
    const auto sourcePath = std::filesystem::path(directory) / "Fixture.psc";
    writeFile(sourcePath, kSource);

    auto index = [&] {
        ScriptIndex built;
        built.addRoot(directory);
        built.addRoot(baseStubs());
        built.addRoot(graphScripts());
        return built;
    }();
    const auto parsed = Parser::parse(kSource, "Fixture.psc");
    auto original = compileScript(index, &parsed, "Fixture.psc", PapyrusCompileOptions{});
    ASSERT_TRUE(original.pex != nullptr);

    const auto pexPath = std::filesystem::path(directory) / "Fixture.pex";
    writeFile(pexPath, original.pex->toBytes());

    std::filesystem::remove(sourcePath);

    const auto result = GraphScriptLoader::load(pexPath, roots());
    EXPECT_FALSE(result.failure.has_value());
    EXPECT_TRUE(result.success());
    if (!result.success()) { SCOPED_TRACE((::testing::Message() << describe(result.diagnostics)).GetString()); }

    ASSERT_TRUE(result.document.has_value());

    NodePalette palette(index);
    GraphCompiler compiler(index, &palette);
    GraphCompileOptions options;
    options.treatPapyrusErrorsAsInternalFaults = true;
    auto document = *result.document;
    const auto rebuilt = compiler.compile(document, options);
    EXPECT_TRUE(rebuilt.success());
    if (!rebuilt.success()) { SCOPED_TRACE((::testing::Message() << describe(rebuilt.diagnostics)).GetString()); }
    ASSERT_TRUE(rebuilt.pex != nullptr);

    const auto difference =
        fo4::papyrus::test::firstDifference(original.pex->objects[0],
                                               rebuilt.pex->objects[0]);
    SCOPED_TRACE((::testing::Message() << "difference: " << difference).GetString());
    EXPECT_TRUE(difference.empty());

    std::filesystem::remove_all(directory);
}

TEST(GraphScriptLoader, GraphScriptLoaderAScriptThatRefusesToLiftComesBackWithTheReasonAndNoDocument) {
    const std::string directory = uniqueRoot("refuse");
    std::filesystem::create_directories(directory);
    const auto path = std::filesystem::path(directory) / "Fixture.psc";
    writeFile(path, R"(Scriptname Fixture extends ObjectReference

Event OnLoad()
    While (IsEnabled())
        Utility.Wait(1.0)
    EndWhile
EndEvent
)");

    const auto result = GraphScriptLoader::load(path, roots());

    EXPECT_FALSE(result.failure.has_value());
    EXPECT_FALSE(result.success());
    EXPECT_FALSE(result.document.has_value());
    EXPECT_FALSE(result.diagnostics.empty());

    std::filesystem::remove_all(directory);
}

TEST(GraphScriptLoader, GraphScriptLoaderAScriptIsCheckedAgainstItsOwnFolder) {
    const std::string directory = uniqueRoot("roots");
    std::filesystem::create_directories(directory);
    const auto path = std::filesystem::path(directory) / "Fixture.psc";
    writeFile(path, R"(Scriptname Fixture extends ObjectReference

Event OnLoad()
    MoveTo(Self, 0.0, 0.0, 0.0, true)
EndEvent
)");

    const auto result = GraphScriptLoader::load(path, roots());

    EXPECT_FALSE(result.failure.has_value());
    EXPECT_TRUE(result.success());
    if (!result.success()) { SCOPED_TRACE((::testing::Message() << describe(result.diagnostics)).GetString()); }

    std::filesystem::remove_all(directory);
}

TEST(GraphScriptLoader, GraphScriptLoaderALiftedPropertyReadValidatesAgainstThePinTheGetterDeclares) {
    const std::string directory = uniqueRoot("prop");
    std::filesystem::create_directories(directory);
    writeFile(std::filesystem::path(directory) / "Holder.psc",
              "Scriptname Holder extends ObjectReference\nint Property Count Auto\n");
    const auto path = std::filesystem::path(directory) / "Reader.psc";
    writeFile(path, R"(Scriptname Reader extends ObjectReference

Event OnLoad()
    Holder store = Self as Holder
    Debug.Trace("count " + store.Count)
EndEvent
)");

    std::vector<std::filesystem::path> loadRoots{directory, baseStubs(), graphScripts()};
    const auto result = GraphScriptLoader::load(path, loadRoots);

    EXPECT_FALSE(result.failure.has_value());
    EXPECT_TRUE(result.success());
    if (!result.success()) { SCOPED_TRACE((::testing::Message() << describe(result.diagnostics)).GetString()); }

    ASSERT_TRUE(result.document.has_value());

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
    SCOPED_TRACE((::testing::Message() << describe(validation.diagnostics)).GetString());
    EXPECT_TRUE(errors.empty());

    std::filesystem::remove_all(directory);
}

TEST(GraphScriptLoader, GraphScriptLoaderAFileThatIsNotAScriptIsRefusedByName) {
    const std::string directory = uniqueRoot("other");
    std::filesystem::create_directories(directory);
    const auto path = std::filesystem::path(directory) / "Fixture.fograph";
    writeFile(path, "{}");

    const auto result = GraphScriptLoader::load(path, roots());

    ASSERT_TRUE(result.failure.has_value());
    EXPECT_TRUE(result.failure->find("Fixture.fograph") != std::string::npos);
    EXPECT_FALSE(result.success());

    std::filesystem::remove_all(directory);
}

TEST(GraphScriptLoader, GraphScriptLoaderAMissingFileIsRefusedRatherThanThrown) {
    const auto path = std::filesystem::temp_directory_path() /
                      ("fo4re-not-here-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                       ".psc");

    const auto result = GraphScriptLoader::load(path, roots());

    EXPECT_TRUE(result.failure.has_value());
    EXPECT_FALSE(result.success());
}

TEST(GraphScriptLoader, GraphScriptLoaderTheOpenableExtensionsAreTheTwoTheLoaderReads) {
    EXPECT_TRUE(GraphScriptLoader::isScript(std::filesystem::path("Fixture.psc")));
    EXPECT_TRUE(GraphScriptLoader::isScript(std::filesystem::path("Fixture.PEX")));
    EXPECT_FALSE(GraphScriptLoader::isScript(std::filesystem::path("Fixture.fograph")));
    EXPECT_FALSE(GraphScriptLoader::isScript(std::filesystem::path("Fixture")));
}
