#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <fo4ide/graph/GraphCompiler.hpp>
#include <fo4ide/graph/GraphLifter.hpp>
#include <fo4ide/papyrus/AnalysisService.hpp>
#include <fo4ide/papyrus/Decompiler.hpp>
#include <fo4ide/papyrus/Parser.hpp>

#include "../helpers/GraphFixtures.hpp"
#include "../helpers/PexComparer.hpp"
#include "../helpers/TempRoot.hpp"

using namespace fo4ide::graph;
using namespace fo4ide::graph::test;
using namespace fo4ide::papyrus;
using fo4ide_test::TempRoot;

namespace {

std::string describe(const std::vector<GraphDiagnostic>& diagnostics) {
    std::string out;
    for (const auto& diagnostic : diagnostics) {
        if (!out.empty()) out += " | ";
        out += diagnostic.code + " " + diagnostic.message;
    }
    return out;
}

std::string describePapyrus(const std::vector<Diagnostic>& diagnostics) {
    std::string out;
    for (const auto& diagnostic : diagnostics) {
        if (!out.empty()) out += " | ";
        out += std::string(codeString(diagnostic.code)) + " " + diagnostic.message;
    }
    return out;
}

// Port of CompileText: writes the source into a fresh temp root and compiles
// against BaseStubs + GraphScripts.
std::unique_ptr<PexFile> compileText(const std::string& text) {
    const TempRoot guard("fo4ide-lift-text");
    const std::string root = guard.string();
    {
        std::ofstream out(std::filesystem::path(root) / "Fixture.psc",
                          std::ios::binary | std::ios::trunc);
        out << text;
    }

    ScriptIndex index;
    index.addRoot(root);
    index.addRoot(baseStubs());
    index.addRoot(graphScripts());

    const auto parsed = Parser::parse(text, "Fixture.psc");
    return compileScript(index, &parsed, "Fixture.psc", PapyrusCompileOptions{}).pex;
}

// Port of ThroughGraph: lift the source into a document and compile that;
// returns the compiled PEX (or null) plus a human report of any refusal or
// compile failure.
std::unique_ptr<PexFile> throughGraph(const std::string& text, std::string& report) {
    const TempRoot guard("fo4ide-lift-src");
    const std::string root = guard.string();
    {
        std::ofstream out(std::filesystem::path(root) / "Fixture.psc",
                          std::ios::binary | std::ios::trunc);
        out << text;
    }

    ScriptIndex index;
    index.addRoot(root);
    index.addRoot(baseStubs());
    index.addRoot(graphScripts());

    const auto parsed = Parser::parse(text, "Fixture.psc");
    GraphLifter lifter(index);
    const auto lifted = lifter.lift(parsed);

    if (!lifted.success()) {
        report = "LIFT REFUSED: " + describe(lifted.diagnostics);
        return nullptr;
    }

    NodePalette palette(index);
    GraphCompiler compiler(index, &palette);
    GraphCompileOptions options;
    options.treatPapyrusErrorsAsInternalFaults = true;
    auto document = *lifted.document;
    const auto compiled = compiler.compile(document, options);
    if (!compiled.success()) {
        report = "REGENERATED SOURCE DID NOT COMPILE: " + describe(compiled.diagnostics) +
                 "\n---- generated ----\n" + compiled.source.value_or("");
        return nullptr;
    }

    report = compiled.source.value_or("");
    if (!compiled.pex) return nullptr;
    return std::make_unique<PexFile>(*compiled.pex);
}

// Port of GraphLiftPexRoundTripTests.RunOne: original PEX -> decompile ->
// reparse -> lift -> compile, comparing the rebuilt PEX to the original.
struct PexLiftOutcome {
    std::string name;
    std::string stage;
    std::string detail;
};

PexLiftOutcome runOne(const GraphFixture& fixture) {
    const TempRoot guard("fo4ide-lift-pex");
    const std::string root = guard.string();
    {
        std::ofstream out(std::filesystem::path(root) / "Fixture.psc",
                          std::ios::binary | std::ios::trunc);
        out << fixture.reference;
    }

    ScriptIndex index;
    index.addRoot(root);
    index.addRoot(baseStubs());
    index.addRoot(graphScripts());

    const auto originalParsed = Parser::parse(fixture.reference, "Fixture.psc");
    const auto original =
        compileScript(index, &originalParsed, "Fixture.psc", PapyrusCompileOptions{});
    if (!original.success() || original.pex == nullptr) {
        return PexLiftOutcome{fixture.name, "ReferenceDidNotCompile",
                              describePapyrus(original.diagnostics)};
    }

    const std::string text = Decompiler::toSource(*original.pex);
    if (text.find(".code") != std::string::npos ||
        text.find("[decompile failed") != std::string::npos) {
        return PexLiftOutcome{fixture.name, "DecompiledToAssembly",
                              "a body could not be structured, so that function came back as "
                              "assembly"};
    }

    const auto parsed = Parser::parse(text, "Fixture.psc");
    if (parsed.hasErrors()) {
        std::string detail;
        for (const auto& diagnostic : parsed.diagnostics) {
            if (diagnostic.severity == Severity::Error) {
                if (!detail.empty()) detail += " | ";
                detail += std::string(codeString(diagnostic.code)) + " " + diagnostic.message;
            }
        }
        return PexLiftOutcome{fixture.name, "DecompiledSourceDidNotParse", detail};
    }

    GraphLifter lifter(index);
    const auto lifted = lifter.lift(parsed);
    if (!lifted.success()) {
        return PexLiftOutcome{fixture.name, "LiftRefused", describe(lifted.diagnostics)};
    }

    NodePalette palette(index);
    GraphCompiler compiler(index, &palette);
    GraphCompileOptions options;
    options.treatPapyrusErrorsAsInternalFaults = true;
    auto document = *lifted.document;
    const auto rebuilt = compiler.compile(document, options);
    if (!rebuilt.success() || rebuilt.pex == nullptr) {
        return PexLiftOutcome{fixture.name, "GraphDidNotCompile", describe(rebuilt.diagnostics)};
    }

    const auto difference =
        fo4ide::papyrus::test::firstDifference(original.pex->objects[0],
                                               rebuilt.pex->objects[0]);
    return difference.empty()
               ? PexLiftOutcome{fixture.name, "Matched", ""}
               : PexLiftOutcome{fixture.name, "InstructionsDiffered", difference};
}

}  // namespace

TEST_CASE("GraphLift a fixture source survives a trip through the graph") {
    Env env(/*withGraphScripts=*/true);
    const auto fixtures = allFixtures(env.palette);
    for (const auto& fixture : fixtures) {
        INFO("fixture: " << fixture.name);

        const auto original = compileText(fixture.reference);
        REQUIRE(original != nullptr);

        std::string report;
        const auto rebuilt = throughGraph(fixture.reference, report);
        CHECK(rebuilt != nullptr);
        if (rebuilt == nullptr) {
            INFO(report);
            continue;
        }

        const auto difference =
            fo4ide::papyrus::test::firstDifference(original->objects[0],
                                                   rebuilt->objects[0]);
        INFO("difference: " << difference);
        INFO("regenerated source:\n" << report);
        CHECK(difference.empty());
    }
}

TEST_CASE("GraphLift a source the lifter cannot express is refused rather than mangled") {
    const std::string text = R"(Scriptname Fixture extends ObjectReference

Event OnLoad()
    While (IsEnabled())
        Utility.Wait(1.0)
    EndWhile
EndEvent
)";

    const TempRoot guard("fo4ide-lift-refuse");
    const std::string root = guard.string();
    {
        std::ofstream out(std::filesystem::path(root) / "Fixture.psc",
                          std::ios::binary | std::ios::trunc);
        out << text;
    }

    ScriptIndex index;
    index.addRoot(root);
    index.addRoot(baseStubs());
    index.addRoot(graphScripts());

    GraphLifter lifter(index);
    const auto lifted = lifter.lift(Parser::parse(text, "Fixture.psc"));

    CHECK_FALSE(lifted.success());
    bool hasCondition = false;
    for (const auto& diagnostic : lifted.diagnostics) {
        if (diagnostic.message.find("condition") != std::string::npos) hasCondition = true;
    }
    CHECK(hasCondition);
    if (!hasCondition) INFO(describe(lifted.diagnostics));
}

TEST_CASE("GraphLift a source that does not parse is refused without a document") {
    const std::string text = "Scriptname Fixture extends\n Function (";

    Env env(/*withGraphScripts=*/true);
    GraphLifter lifter(env.index);
    const auto lifted = lifter.lift(Parser::parse(text, "Fixture.psc"));

    CHECK_FALSE(lifted.success());
    CHECK_FALSE(lifted.document.has_value());
}

TEST_CASE("GraphLiftPex a compiled binary survives a trip through the graph") {
    Env env(/*withGraphScripts=*/true);
    const auto fixtures = allFixtures(env.palette);

    int attempted = 0;
    int matched = 0;
    std::vector<PexLiftOutcome> failures;
    for (const auto& fixture : fixtures) {
        attempted++;
        const auto outcome = runOne(fixture);
        if (outcome.stage == "Matched") {
            matched++;
        } else {
            failures.push_back(outcome);
        }
    }

    INFO("PEX LIFT attempted=" << attempted << " matched=" << matched);
    for (const auto& failure : failures) {
        INFO("  " << failure.name << ": " << failure.stage << ": " << failure.detail);
    }

    // The C# pins this to its measured value (28/28); a regression fails here.
    CHECK(matched == attempted);
    for (const auto& failure : failures) {
        CHECK(failure.stage == "Matched");
    }
}
