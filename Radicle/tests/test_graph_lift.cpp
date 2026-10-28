#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string_view>
#include <system_error>
#include <string>
#include <vector>

#include <fo4/graph/graph_compiler.hpp>
#include <fo4/graph/graph_lifter.hpp>
#include <fo4/papyrus/compile_script.hpp>
#include <fo4/papyrus/decompiler.hpp>
#include <fo4/papyrus/parser.hpp>

#include "graph_fixtures.hpp"
#include "papyrus_pex_comparer.hpp"

using namespace fo4::graph;
using namespace fo4::graph::test;
using namespace fo4::papyrus;

namespace {

class UniqueRoot {
public:
    explicit UniqueRoot(std::string_view tag)
        : parent_(std::filesystem::temp_directory_path() / ("fo4re-lift-" + std::string(tag))),
          path_(parent_ / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())) {}
    ~UniqueRoot() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
        std::filesystem::remove(parent_, ignored);
    }
    UniqueRoot(const UniqueRoot&) = delete;
    UniqueRoot& operator=(const UniqueRoot&) = delete;
    std::string string() const { return path_.string(); }

private:
    std::filesystem::path parent_;
    std::filesystem::path path_;
};

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

std::unique_ptr<fo4::pex::PexFile> compileText(const std::string& text) {
    const UniqueRoot guard("text");
    const std::string root = guard.string();
    std::filesystem::create_directories(root);
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

std::unique_ptr<fo4::pex::PexFile> throughGraph(const std::string& text, std::string& report) {
    const UniqueRoot guard("src");
    const std::string root = guard.string();
    std::filesystem::create_directories(root);
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
    return std::make_unique<fo4::pex::PexFile>(*compiled.pex);
}

struct PexLiftOutcome {
    std::string name;
    std::string stage;
    std::string detail;
};

PexLiftOutcome runOne(const GraphFixture& fixture) {
    const UniqueRoot guard("pex");
    const std::string root = guard.string();
    std::filesystem::create_directories(root);
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
        fo4::papyrus::test::firstDifference(original.pex->objects[0],
                                               rebuilt.pex->objects[0]);
    return difference.empty()
               ? PexLiftOutcome{fixture.name, "Matched", ""}
               : PexLiftOutcome{fixture.name, "InstructionsDiffered", difference};
}

void expectSurvivesTheGraph(const std::string& text) {
    const auto original = compileText(text);
    ASSERT_TRUE(original != nullptr);

    std::string report;
    const auto rebuilt = throughGraph(text, report);
    ASSERT_TRUE(rebuilt != nullptr) << report;

    const auto difference =
        fo4::papyrus::test::firstDifference(original->objects[0], rebuilt->objects[0]);
    EXPECT_TRUE(difference.empty()) << "difference: " << difference
                                    << "\nregenerated source:\n" << report;
}

}

TEST(GraphLift, GraphLiftAFixtureSourceSurvivesATripThroughTheGraph) {
    Env env(true);
    const auto fixtures = allFixtures(env.palette);
    for (const auto& fixture : fixtures) {
        SCOPED_TRACE((::testing::Message() << "fixture: " << fixture.name).GetString());

        const auto original = compileText(fixture.reference);
        ASSERT_TRUE(original != nullptr);

        std::string report;
        const auto rebuilt = throughGraph(fixture.reference, report);
        EXPECT_TRUE(rebuilt != nullptr);
        if (rebuilt == nullptr) {
            SCOPED_TRACE((::testing::Message() << report).GetString());
            continue;
        }

        const auto difference =
            fo4::papyrus::test::firstDifference(original->objects[0],
                                                   rebuilt->objects[0]);
        SCOPED_TRACE((::testing::Message() << "difference: " << difference).GetString());
        SCOPED_TRACE((::testing::Message() << "regenerated source:\n" << report).GetString());
        EXPECT_TRUE(difference.empty());
    }
}

TEST(GraphLift, GraphLiftASourceTheLifterCannotExpressIsRefusedRatherThanMangled) {
    const std::string text = R"(Scriptname Fixture extends ObjectReference

Event OnLoad()
    While (IsEnabled())
        Utility.Wait(1.0)
    EndWhile
EndEvent
)";

    const UniqueRoot guard("refuse");
    const std::string root = guard.string();
    std::filesystem::create_directories(root);
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

    EXPECT_FALSE(lifted.success());
    bool hasCondition = false;
    for (const auto& diagnostic : lifted.diagnostics) {
        if (diagnostic.message.find("condition") != std::string::npos) hasCondition = true;
    }
    EXPECT_TRUE(hasCondition);
    if (!hasCondition) { SCOPED_TRACE((::testing::Message() << describe(lifted.diagnostics)).GetString()); }
}

TEST(GraphLift, GraphLiftASourceThatDoesNotParseIsRefusedWithoutADocument) {
    const std::string text = "Scriptname Fixture extends\n Function (";

    Env env(true);
    GraphLifter lifter(env.index);
    const auto lifted = lifter.lift(Parser::parse(text, "Fixture.psc"));

    EXPECT_FALSE(lifted.success());
    EXPECT_FALSE(lifted.document.has_value());
}

TEST(GraphLift, GraphLiftPexACompiledBinarySurvivesATripThroughTheGraph) {
    Env env(true);
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

    SCOPED_TRACE((::testing::Message() << "PEX LIFT attempted=" << attempted << " matched=" << matched).GetString());
    for (const auto& failure : failures) {
        SCOPED_TRACE((::testing::Message() << "  " << failure.name << ": " << failure.stage << ": " << failure.detail).GetString());
    }

    EXPECT_TRUE(matched == attempted);
    for (const auto& failure : failures) {
        EXPECT_TRUE(failure.stage == "Matched");
    }
}

TEST(GraphLift, StructNewGetAndSetSurviveATripThroughTheGraph) {
    expectSurvivesTheGraph(R"(Scriptname Fixture extends ObjectReference

Struct Point
    Int X
    Int Y
EndStruct

Event OnLoad()
    Point p = new Point
    p.X = 3
    p.Y = p.X + 4
EndEvent
)");
}

TEST(GraphLift, StructMemberCompoundAssignmentSurvivesATripThroughTheGraph) {
    expectSurvivesTheGraph(R"(Scriptname Fixture extends ObjectReference

Struct Point
    Int X
    Int Y
EndStruct

Event OnLoad()
    Point p = new Point
    p.X = 1
    p.X += 2
    p.Y -= p.X
EndEvent
)");
}

TEST(GraphLift, StructMemberCompoundWithACallIsRefusedRatherThanReordered) {
    const std::string text = R"(Scriptname Fixture extends ObjectReference

Struct Point
    Int X
EndStruct

Int Function Next()
    Return 1
EndFunction

Event OnLoad()
    Point p = new Point
    p.X += Next()
EndEvent
)";

    Env env(true);
    GraphLifter lifter(env.index);
    const auto lifted = lifter.lift(Parser::parse(text, "Fixture.psc"));

    EXPECT_FALSE(lifted.success());
    bool refused = false;
    for (const auto& diagnostic : lifted.diagnostics) {
        if (diagnostic.message.find("struct member") != std::string::npos) refused = true;
    }
    EXPECT_TRUE(refused);
    if (!refused) { SCOPED_TRACE((::testing::Message() << describe(lifted.diagnostics)).GetString()); }
}
