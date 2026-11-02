#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <fo4ide/graph/GraphCompiler.hpp>
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

std::string lower(std::string_view text) {
    std::string out(text);
    for (auto& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

std::set<std::string> sortedNames(const std::vector<std::string>& names) {
    std::set<std::string> out;
    for (const auto& name : names) out.insert(lower(name));
    return out;
}

std::vector<std::string> eventNames(const Script& script) {
    std::vector<std::string> names;
    for (const auto& event : script.events) names.push_back(event.name);
    for (const auto& state : script.states) {
        for (const auto& event : state.events) names.push_back(event.name);
    }
    return names;
}

std::vector<std::string> functionNames(const Script& script) {
    std::vector<std::string> names;
    for (const auto& fn : script.functions) names.push_back(fn.name);
    for (const auto& state : script.states) {
        for (const auto& fn : state.functions) names.push_back(fn.name);
    }
    return names;
}

// Port of GraphTestEnvironment.Compile: a fresh index over BaseStubs +
// GraphScripts per call, with Papyrus errors treated as internal faults.
GraphCompileResult compileGraph(const GraphFixture& fixture) {
    Env env(/*withGraphScripts=*/true);
    GraphCompiler compiler(env.index, &env.palette);
    GraphCompileOptions options;
    options.treatPapyrusErrorsAsInternalFaults = true;
    auto document = fixture.build();
    return compiler.compile(document, options);
}

// Port of CompileReference: writes the fixture's hand-written source into a
// fresh temp root and compiles it against the standard roots.
std::unique_ptr<PexFile> compileReference(const GraphFixture& fixture,
                                          const std::filesystem::path& root) {
    std::filesystem::create_directories(root);
    const auto path = root / "Fixture.psc";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << fixture.reference;
    }

    ScriptIndex index;
    index.addRoot(root);
    index.addRoot(baseStubs());
    index.addRoot(graphScripts());

    const auto parsed = Parser::parse(fixture.reference, "Fixture.psc");
    PapyrusCompileOptions options;
    return compileScript(index, &parsed, "Fixture.psc", options).pex;
}

// Port of Oracle1's DecompilesAndRecompiles helper: decompile the generated
// PEX to source, reparse, and recompile against a fresh index that also sees
// the decompiled file. Returns the failure reason, or empty on success.
std::string decompilesAndRecompiles(const GraphCompileResult& first) {
    REQUIRE(first.pex != nullptr);

    const std::string body = Decompiler::toSource(*first.pex);
    if (body.find(".code") != std::string::npos ||
        body.find("[decompile failed") != std::string::npos) {
        return "decompiler produced an assembly listing; not comparable";
    }

    const auto reparsed = Parser::parse(body, "Fixture.psc");
    if (reparsed.hasErrors()) return "decompiled source should parse";

    const TempRoot guard("fo4ide-graph-roundtrip");
    {
        std::ofstream out(guard.path() / "Fixture.psc", std::ios::binary | std::ios::trunc);
        out << body;
    }

    ScriptIndex index;
    index.addRoot(guard.path());
    index.addRoot(baseStubs());
    index.addRoot(graphScripts());

    const auto second = compileScript(index, &reparsed, "Fixture.psc", PapyrusCompileOptions{});
    if (!second.success()) return "recompiling the decompiled source should succeed";
    return "";
}

}  // namespace

TEST_CASE("GraphCompiler every fixture graph compiles clean") {
    Env env(/*withGraphScripts=*/true);
    const auto fixtures = allFixtures(env.palette);
    REQUIRE(fixtures.size() >= 24);
    for (const auto& fixture : fixtures) {
        INFO("fixture: " << fixture.name);
        auto result = compileGraph(fixture);
        if (result.source) INFO("generated source:\n" << *result.source);
        CHECK(result.errors().empty());
        CHECK(result.success());
    }
}

TEST_CASE("GraphCompiler generated source always reparses clean") {
    Env env(/*withGraphScripts=*/true);
    const auto fixtures = allFixtures(env.palette);
    for (const auto& fixture : fixtures) {
        INFO("fixture: " << fixture.name);
        const auto result = compileGraph(fixture);
        REQUIRE(result.source.has_value());
        const auto parsed = Parser::parse(*result.source, "Fixture.psc");
        CHECK_FALSE(parsed.hasErrors());
    }
}

TEST_CASE("GraphCompiler Oracle1 the compiled object decompiles to source that recompiles") {
    Env env(/*withGraphScripts=*/true);
    const auto fixtures = allFixtures(env.palette);
    for (const auto& fixture : fixtures) {
        INFO("fixture: " << fixture.name);
        const auto first = compileGraph(fixture);
        REQUIRE(first.success());

        const std::string body = Decompiler::toSource(*first.pex);
        if (body.find(".code") != std::string::npos ||
            body.find("[decompile failed") != std::string::npos) {
            WARN(fixture.name << ": decompiler produced an assembly listing; not comparable.");
            continue;
        }

        const auto reparsed = Parser::parse(body, "Fixture.psc");
        CHECK_FALSE(reparsed.hasErrors());
        if (reparsed.hasErrors()) continue;

        CHECK(lower(reparsed.name) == lower(first.ir->name));
        const auto reparsedExtends = reparsed.extends.value_or("");
        const auto irExtends = first.ir->extends.value_or("");
        CHECK((lower(reparsedExtends) == lower(irExtends) ||
               lower(reparsedExtends) == lower("ScriptObject")));

        const auto expectedEvents =
            [&] {
                std::vector<std::string> names;
                for (const auto& callable : first.ir->callables) {
                    if (callable.isEvent) names.push_back(callable.name);
                }
                return names;
            }();
        const auto expectedFunctions =
            [&] {
                std::vector<std::string> names;
                for (const auto& callable : first.ir->callables) {
                    if (!callable.isEvent) names.push_back(callable.name);
                }
                return names;
            }();

        CHECK(sortedNames(eventNames(reparsed)) == sortedNames(expectedEvents));
        CHECK(sortedNames(functionNames(reparsed)) == sortedNames(expectedFunctions));

        const std::string reason = decompilesAndRecompiles(first);
        CHECK(reason.empty());
        if (!reason.empty()) INFO(fixture.name << ": " << reason);
    }
}

TEST_CASE("GraphCompiler Oracle2 the graph produces what a person would have written") {
    Env env(/*withGraphScripts=*/true);
    const auto fixtures = allFixtures(env.palette);
    for (const auto& fixture : fixtures) {
        INFO("fixture: " << fixture.name);
        const auto generated = compileGraph(fixture);
        REQUIRE(generated.success());

        const TempRoot root("fo4ide-graph-reference");
        auto reference = compileReference(fixture, root.path());
        REQUIRE(reference != nullptr);

        const auto difference =
            fo4ide::papyrus::test::firstDifference(reference->objects[0],
                                                   generated.pex->objects[0]);
        INFO("difference: " << difference);
        INFO("--- generated ---\n" << generated.source.value_or(""));
        INFO("--- reference ---\n" << fixture.reference);
        INFO("--- decompiled generated ---\n"
             << fo4ide::papyrus::Decompiler::toSource(*generated.pex));
        CHECK(difference.empty());
    }
}

TEST_CASE("GraphCompiler Oracle3 the generated script has the same shape as the reference") {
    Env env(/*withGraphScripts=*/true);
    const auto fixtures = allFixtures(env.palette);
    for (const auto& fixture : fixtures) {
        INFO("fixture: " << fixture.name);
        const auto generated = compileGraph(fixture);
        REQUIRE(generated.success());

        const auto mine = Parser::parse(*generated.source, "Fixture.psc");
        const auto theirs = Parser::parse(fixture.reference, "Fixture.psc");

        CHECK(lower(mine.name) == lower(theirs.name));
        CHECK(lower(mine.extends.value_or("")) == lower(theirs.extends.value_or("")));

        std::vector<std::string> mineEvents;
        std::vector<std::string> theirsEvents;
        std::vector<std::string> mineFunctions;
        std::vector<std::string> theirsFunctions;
        for (const auto& event : mine.events) mineEvents.push_back(event.name);
        for (const auto& event : theirs.events) theirsEvents.push_back(event.name);
        for (const auto& fn : mine.functions) mineFunctions.push_back(fn.name);
        for (const auto& fn : theirs.functions) theirsFunctions.push_back(fn.name);

        std::vector<std::string> mineProperties;
        std::vector<std::string> theirsProperties;
        for (const auto& property : mine.properties) mineProperties.push_back(property.name);
        for (const auto& property : theirs.properties) theirsProperties.push_back(property.name);

        std::vector<std::string> mineVariables;
        std::vector<std::string> theirsVariables;
        for (const auto& variable : mine.variables) mineVariables.push_back(variable.name);
        for (const auto& variable : theirs.variables) theirsVariables.push_back(variable.name);

        CHECK(sortedNames(mineEvents) == sortedNames(theirsEvents));
        CHECK(sortedNames(mineFunctions) == sortedNames(theirsFunctions));
        CHECK(sortedNames(mineProperties) == sortedNames(theirsProperties));
        CHECK(sortedNames(mineVariables) == sortedNames(theirsVariables));
    }
}

TEST_CASE("GraphCompiler publishes scripts under one process folder that clearScratch removes") {
    GraphCompiler::clearScratch();
    Env env(/*withGraphScripts=*/true);
    const auto fixtures = allFixtures(env.palette);
    REQUIRE(!fixtures.empty());
    REQUIRE(compileGraph(fixtures.front()).success());
    REQUIRE(compileGraph(fixtures.front()).success());

    const auto root = GraphCompiler::scratchRoot();
    REQUIRE(std::filesystem::is_directory(root));
    CHECK(root.parent_path().filename() == "fo4re-graph-scratch");
    std::size_t published = 0;
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        (void)entry;
        ++published;
    }
    CHECK(published >= 2);

    GraphCompiler::clearScratch();
    CHECK_FALSE(std::filesystem::exists(root));
}
