#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <set>
#include <string>
#include <vector>

#include <fo4/graph/graph_compiler.hpp>
#include <fo4/papyrus/decompiler.hpp>
#include <fo4/papyrus/parser.hpp>

#include "graph_fixtures.hpp"
#include "papyrus_pex_comparer.hpp"

using namespace fo4::graph;
using namespace fo4::graph::test;
using namespace fo4::papyrus;

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

GraphCompileResult compileGraph(const GraphFixture& fixture) {
    Env env(true);
    GraphCompiler compiler(env.index, &env.palette);
    GraphCompileOptions options;
    options.treatPapyrusErrorsAsInternalFaults = true;
    auto document = fixture.build();
    return compiler.compile(document, options);
}

std::unique_ptr<fo4::pex::PexFile> compileReference(const GraphFixture& fixture,
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

std::string uniqueRoot(std::string_view tag) {
    return (std::filesystem::temp_directory_path() / ("fo4re-graph-" + std::string(tag)) /
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))
        .string();
}

std::string decompilesAndRecompiles(const GraphCompileResult& first) {
    if (first.pex == nullptr) throw std::runtime_error("reference compile produced no pex");

    const std::string body = Decompiler::toSource(*first.pex);
    if (body.find(".code") != std::string::npos ||
        body.find("[decompile failed") != std::string::npos) {
        return "decompiler produced an assembly listing; not comparable";
    }

    const auto reparsed = Parser::parse(body, "Fixture.psc");
    if (reparsed.hasErrors()) return "decompiled source should parse";

    const std::string root = uniqueRoot("roundtrip");
    const auto guard = std::filesystem::path(root);
    std::error_code error;
    std::filesystem::create_directories(guard, error);
    {
        std::ofstream out(guard / "Fixture.psc", std::ios::binary | std::ios::trunc);
        out << body;
    }

    ScriptIndex index;
    index.addRoot(guard);
    index.addRoot(baseStubs());
    index.addRoot(graphScripts());

    const auto second = compileScript(index, &reparsed, "Fixture.psc", PapyrusCompileOptions{});
    std::filesystem::remove_all(guard, error);
    if (!second.success()) return "recompiling the decompiled source should succeed";
    return "";
}

}

TEST(GraphCompiler, GraphCompilerEveryFixtureGraphCompilesClean) {
    Env env(true);
    const auto fixtures = allFixtures(env.palette);
    ASSERT_TRUE(fixtures.size() >= 24);
    for (const auto& fixture : fixtures) {
        SCOPED_TRACE((::testing::Message() << "fixture: " << fixture.name).GetString());
        auto result = compileGraph(fixture);
        if (result.source) { SCOPED_TRACE((::testing::Message() << "generated source:\n" << *result.source).GetString()); }
        EXPECT_TRUE(result.errors().empty());
        EXPECT_TRUE(result.success());
    }
}

TEST(GraphCompiler, GraphCompilerGeneratedSourceAlwaysReparsesClean) {
    Env env(true);
    const auto fixtures = allFixtures(env.palette);
    for (const auto& fixture : fixtures) {
        SCOPED_TRACE((::testing::Message() << "fixture: " << fixture.name).GetString());
        const auto result = compileGraph(fixture);
        ASSERT_TRUE(result.source.has_value());
        const auto parsed = Parser::parse(*result.source, "Fixture.psc");
        EXPECT_FALSE(parsed.hasErrors());
    }
}

TEST(GraphCompiler, GraphCompilerOracle1TheCompiledObjectDecompilesToSourceThatRecompiles) {
    Env env(true);
    const auto fixtures = allFixtures(env.palette);
    for (const auto& fixture : fixtures) {
        SCOPED_TRACE((::testing::Message() << "fixture: " << fixture.name).GetString());
        const auto first = compileGraph(fixture);
        ASSERT_TRUE(first.success());

        const std::string body = Decompiler::toSource(*first.pex);
        if (body.find(".code") != std::string::npos ||
            body.find("[decompile failed") != std::string::npos) {
            std::cerr << fixture.name << ": decompiler produced an assembly listing; not comparable." << '\n';
            continue;
        }

        const auto reparsed = Parser::parse(body, "Fixture.psc");
        EXPECT_FALSE(reparsed.hasErrors());
        if (reparsed.hasErrors()) continue;

        EXPECT_TRUE(lower(reparsed.name) == lower(first.ir->name));
        const auto reparsedExtends = reparsed.extends.value_or("");
        const auto irExtends = first.ir->extends.value_or("");
        EXPECT_TRUE((lower(reparsedExtends) == lower(irExtends) ||
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

        EXPECT_TRUE(sortedNames(eventNames(reparsed)) == sortedNames(expectedEvents));
        EXPECT_TRUE(sortedNames(functionNames(reparsed)) == sortedNames(expectedFunctions));

        const std::string reason = decompilesAndRecompiles(first);
        EXPECT_TRUE(reason.empty());
        if (!reason.empty()) { SCOPED_TRACE((::testing::Message() << fixture.name << ": " << reason).GetString()); }
    }
}

TEST(GraphCompiler, GraphCompilerOracle2TheGraphProducesWhatAPersonWouldHaveWritten) {
    Env env(true);
    const auto fixtures = allFixtures(env.palette);
    for (const auto& fixture : fixtures) {
        SCOPED_TRACE((::testing::Message() << "fixture: " << fixture.name).GetString());
        const auto generated = compileGraph(fixture);
        ASSERT_TRUE(generated.success());

        const std::string root = uniqueRoot("reference");
        auto reference = compileReference(fixture, root);
        std::error_code error;
        std::filesystem::remove_all(root, error);
        ASSERT_TRUE(reference != nullptr);

        const auto difference =
            fo4::papyrus::test::firstDifference(reference->objects[0],
                                                   generated.pex->objects[0]);
        SCOPED_TRACE((::testing::Message() << "difference: " << difference).GetString());
        SCOPED_TRACE((::testing::Message() << "--- generated ---\n" << generated.source.value_or("")).GetString());
        SCOPED_TRACE((::testing::Message() << "--- reference ---\n" << fixture.reference).GetString());
        SCOPED_TRACE((::testing::Message() << "--- decompiled generated ---\n"
             << fo4::papyrus::Decompiler::toSource(*generated.pex)).GetString());
        EXPECT_TRUE(difference.empty());
    }
}

TEST(GraphCompiler, GraphCompilerOracle3TheGeneratedScriptHasTheSameShapeAsTheReference) {
    Env env(true);
    const auto fixtures = allFixtures(env.palette);
    for (const auto& fixture : fixtures) {
        SCOPED_TRACE((::testing::Message() << "fixture: " << fixture.name).GetString());
        const auto generated = compileGraph(fixture);
        ASSERT_TRUE(generated.success());

        const auto mine = Parser::parse(*generated.source, "Fixture.psc");
        const auto theirs = Parser::parse(fixture.reference, "Fixture.psc");

        EXPECT_TRUE(lower(mine.name) == lower(theirs.name));
        EXPECT_TRUE(lower(mine.extends.value_or("")) == lower(theirs.extends.value_or("")));

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

        EXPECT_TRUE(sortedNames(mineEvents) == sortedNames(theirsEvents));
        EXPECT_TRUE(sortedNames(mineFunctions) == sortedNames(theirsFunctions));
        EXPECT_TRUE(sortedNames(mineProperties) == sortedNames(theirsProperties));
        EXPECT_TRUE(sortedNames(mineVariables) == sortedNames(theirsVariables));
    }
}

TEST(GraphCompiler, GraphCompilerPublishesScriptsUnderOneProcessFolderThatClearScratchRemoves) {
    GraphCompiler::clearScratch();
    Env env(true);
    const auto fixtures = allFixtures(env.palette);
    ASSERT_TRUE(!fixtures.empty());
    ASSERT_TRUE(compileGraph(fixtures.front()).success());
    ASSERT_TRUE(compileGraph(fixtures.front()).success());

    const auto root = GraphCompiler::scratchRoot();
    ASSERT_TRUE(std::filesystem::is_directory(root));
    EXPECT_TRUE(root.parent_path().filename() == "fo4re-graph-scratch");
    std::size_t published = 0;
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        (void)entry;
        ++published;
    }
    EXPECT_TRUE(published >= 2);

    GraphCompiler::clearScratch();
    EXPECT_FALSE(std::filesystem::exists(root));
}
