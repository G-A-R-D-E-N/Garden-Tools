#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <fo4/graph/graph_compiler.hpp>
#include <fo4/graph/graph_ir.hpp>
#include <fo4/graph/graph_lifter.hpp>
#include <fo4/graph/graph_name_table.hpp>
#include <fo4/graph/graph_type_resolver.hpp>
#include <fo4/graph/graph_validator.hpp>
#include <fo4/graph/papyrus_source_writer.hpp>
#include <fo4/papyrus/parser.hpp>

#include "graph_compile_support.hpp"

using namespace fo4::graph;
using namespace fo4::graph::test;

namespace {

const ir::IrIf* findIf(const std::vector<std::shared_ptr<ir::IrStatement>>& body) {
    for (const auto& statement : body) {
        if (const auto* branch = dynamic_cast<const ir::IrIf*>(statement.get())) return branch;
        if (const auto* loop = dynamic_cast<const ir::IrWhile*>(statement.get())) {
            if (const auto* inner = findIf(loop->body)) return inner;
        }
    }
    return nullptr;
}

std::string writeScript(const ir::IrScript& script) {
    PapyrusSourceWriterOptions options;
    options.emitHeaderComment = false;
    PapyrusSourceWriter writer(options);
    GraphSourceMap map;
    return writer.write(script, map);
}

ir::IrScript scriptWithOneEvent() {
    ir::IrScript script;
    script.name = "Fixture";
    ir::IrCallable callable;
    callable.name = "OnLoad";
    callable.isEvent = true;
    script.callables.push_back(std::move(callable));
    return script;
}

}

TEST(GraphForwardParity, ANegatedConditionKeepsTheNodeTheConditionCameFrom) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto enabled = graph.node(env.palette, "call:ObjectReference.IsEnabled");
    const auto branch = graph.node(env.palette, builtinDefs::kBranch);
    const auto work = graph.node(env.palette, "global:Debug.Notification");

    graph.value(work, pinIds::argument("asNotificationText"), "string", "\"work\"");
    graph.wire(entry, pinIds::kExec, enabled, pinIds::kExec);
    graph.wire(enabled, pinIds::kThen, branch, pinIds::kExec);
    graph.wire(enabled, pinIds::kReturn, branch, pinIds::kCondition);
    graph.wire(branch, pinIds::kElse, work, pinIds::kExec);

    const auto result = compileDoc(env, graph, true);
    ASSERT_TRUE(result.ir != nullptr) << describe(result.diagnostics);
    ASSERT_FALSE(result.ir->callables.empty());

    const auto* ifStatement = findIf(result.ir->callables[0].body);
    ASSERT_TRUE(ifStatement != nullptr);
    ASSERT_FALSE(ifStatement->branches.empty());
    const auto* negated = dynamic_cast<const ir::IrUnary*>(ifStatement->branches[0].condition.get());
    ASSERT_TRUE(negated != nullptr);
    EXPECT_EQ(negated->op, "!");
    EXPECT_EQ(negated->nodeId.value_or(""), enabled);
}

TEST(GraphForwardParity, AWhitespaceOnlyExtendsIsWrittenAsNoExtends) {
    auto script = scriptWithOneEvent();
    script.extends = "   ";

    const auto source = writeScript(script);

    EXPECT_TRUE(contains(source, "Scriptname Fixture"));
    EXPECT_FALSE(contains(source, "extends"));
}

TEST(GraphForwardParity, AWhitespaceOnlyRemoteObjectTypeIsWrittenAsALocalEvent) {
    auto script = scriptWithOneEvent();
    script.callables[0].remoteObjectType = "  ";

    const auto source = writeScript(script);

    EXPECT_TRUE(contains(source, "Event OnLoad()"));
    EXPECT_FALSE(contains(source, "Event ."));
}

TEST(GraphForwardParity, AWhitespaceOnlyStateNameLeavesTheHandlerOutsideAnyState) {
    auto script = scriptWithOneEvent();
    script.callables[0].stateName = "   ";

    const auto source = writeScript(script);

    EXPECT_TRUE(contains(source, "Event OnLoad()"));
    EXPECT_FALSE(contains(source, "State"));
}

TEST(GraphForwardParity, ARealStateNameStillWrapsTheHandler) {
    auto script = scriptWithOneEvent();
    script.callables[0].stateName = "Busy";

    const auto source = writeScript(script);

    EXPECT_TRUE(contains(source, "State Busy"));
    EXPECT_TRUE(contains(source, "EndState"));
}

TEST(GraphForwardParity, DuplicateVariableGroupsKeepDeclarationOrderAndTheFirstSpelling) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");
    graph.variable("Beta", "int").variable("beta", "int").variable("Alpha", "int").variable("ALPHA", "int");
    graph.node(env.palette, "event:ObjectReference.OnLoad");
    graph.document.invalidate();

    GraphValidator validator(env.index, env.palette);
    const auto validation = validator.validate(graph.document);

    std::vector<std::string> messages;
    for (const auto& diagnostic : validation.diagnostics) {
        if (diagnostic.code == codes::kDuplicateVariableName) messages.push_back(diagnostic.message);
    }
    ASSERT_EQ(messages.size(), 2u) << describe(validation.diagnostics);
    EXPECT_TRUE(contains(messages[0], "'Beta' is declared 2 times"));
    EXPECT_TRUE(contains(messages[1], "'Alpha' is declared 2 times"));
}

TEST(GraphForwardParity, ABlankPaletteQueryAndScriptFilterMeanNoFilter) {
    Env env(true);

    const auto everything = env.palette.search(std::nullopt, 100000);
    const auto blank = env.palette.search(std::string_view("   "), 100000, std::string_view(" "));

    ASSERT_FALSE(everything.entries.empty());
    EXPECT_EQ(blank.total, everything.total);
    ASSERT_EQ(blank.entries.size(), everything.entries.size());
    for (std::size_t i = 0; i < everything.entries.size(); ++i) {
        EXPECT_EQ(blank.entries[i].id, everything.entries[i].id);
    }
}

TEST(GraphForwardParity, AnEmptyExtendsStillRefusesAForeignCallWithNoTarget) {
    Env env(true);
    GraphBuilder graph("Fixture", "");

    const auto entry = graph.node(env.palette, builtinDefs::kFunctionEntry, {{"name", "Go"}});
    const auto disable = graph.node(env.palette, "call:ObjectReference.Disable");
    graph.wire(entry, pinIds::kExec, disable, pinIds::kExec);

    const auto result = compileDoc(env, graph, true);

    const auto* diagnostic = firstError(result.diagnostics, codes::kUnconnectedSelf);
    ASSERT_TRUE(diagnostic != nullptr) << describe(result.diagnostics);
    EXPECT_EQ(diagnostic->nodeId.value_or(""), disable);
}

TEST(GraphForwardParity, AnEmptyExtendsIsNotAnAncestorOfTheOwnScript) {
    Env env(true);
    GraphTypeResolver resolver(env.index, "Fixture", std::optional<std::string>(""));

    EXPECT_FALSE(resolver.inheritsFrom("Fixture", ""));
    EXPECT_TRUE(resolver.inheritsFrom("Fixture", "Fixture"));
}

TEST(GraphForwardParity, APropertyGetOnTheOwnChainWithNothingWiredToSelfNeedsNoReceiver) {
    const auto root = std::filesystem::temp_directory_path() /
                      ("fo4re-parity-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } cleanup{root};
    std::filesystem::create_directories(root);
    {
        std::ofstream stream(root / "PropHolder.psc");
        stream << "Scriptname PropHolder extends ObjectReference\n"
                  "int Property Count Auto\n";
    }

    {
        Env env(true);
        env.index.addRoot(root);
        GraphBuilder graph("Fixture", "PropHolder");

        const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
        const auto count = graph.node(env.palette, "prop.get:PropHolder.Count");
        const auto wait = graph.node(env.palette, "global:Utility.Wait");

        graph.wire(entry, pinIds::kExec, wait, pinIds::kExec);
        graph.wire(count, pinIds::kValue, wait, pinIds::argument("afSeconds"));

        const auto result = compileDoc(env, graph, true);

        EXPECT_FALSE(hasCode(result.diagnostics, codes::kUnconnectedSelf)) << describe(result.diagnostics);
        ASSERT_TRUE(result.source.has_value()) << describe(result.diagnostics);
        EXPECT_TRUE(contains(*result.source, "Wait(Count")) << *result.source;
        EXPECT_FALSE(contains(*result.source, ".Count"));
    }
}

TEST(GraphForwardParity, AStructMemberSetEmitsAnAssignmentToTheMember) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");
    GraphStruct point;
    point.name = "Point";
    GraphVariable x;
    x.name = "X";
    x.type = "int";
    point.members.push_back(x);
    graph.document.structs.push_back(point);

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto local = graph.node(env.palette, builtinDefs::kLocalDeclare,
                                  {{"name", "p"}, {"type", "Point"}});
    const auto created = graph.node(env.palette, builtinDefs::kStructNew, {{"type", "Point"}});
    const auto read = graph.node(env.palette, builtinDefs::kVariableGet,
                                 {{"name", "p"}, {"type", "Point"}});
    const auto set = graph.node(env.palette, builtinDefs::kStructSet, {{"member", "X"}});

    graph.value(set, pinIds::kValue, "int", "3");
    graph.wire(entry, pinIds::kExec, local, pinIds::kExec);
    graph.wire(created, pinIds::kReturn, local, pinIds::kValue);
    graph.wire(local, pinIds::kThen, set, pinIds::kExec);
    graph.wire(read, pinIds::kValue, set, pinIds::kTarget);

    const auto result = compileDoc(env, graph, true);
    ASSERT_TRUE(result.source.has_value()) << describe(result.diagnostics);
    const auto& source = *result.source;

    EXPECT_TRUE(contains(source, "p = new Point"));
    EXPECT_TRUE(contains(source, "p.X = 3"));
}

TEST(GraphForwardParity, TheNameCleanerKeepsAsciiLettersDigitsAndUnderscoresOnly) {
    EXPECT_EQ(GraphNameTable::clean(""), "value");
    EXPECT_EQ(GraphNameTable::clean("   "), "value");
    EXPECT_EQ(GraphNameTable::clean("!!!"), "value");
    EXPECT_EQ(GraphNameTable::clean("my var!"), "myvar");
    EXPECT_EQ(GraphNameTable::clean("9lives"), "_9lives");
    EXPECT_EQ(GraphNameTable::clean("under_score"), "under_score");
    EXPECT_EQ(GraphNameTable::clean("caf\xC3\xA9"), "caf");
    EXPECT_EQ(GraphNameTable::clean("\xC3\xA9"), "value");
}

TEST(GraphForwardParity, AllocatedNamesAreUniqueIgnoringCase) {
    GraphNameTable table;

    EXPECT_EQ(table.allocate("Counter"), "Counter");
    EXPECT_EQ(table.allocate("counter"), "counter2");
    EXPECT_EQ(table.allocate("COUNTER"), "COUNTER3");
}

TEST(GraphForwardParity, ASourceThatDoesNotParseIsRefusedWithoutALinePrefix) {
    Env env(true);
    GraphLifter lifter(env.index);

    const auto lifted = lifter.lift(fo4::papyrus::Parser::parse("Scriptname Fixture extends\n Function (", "Fixture.psc"));

    EXPECT_FALSE(lifted.document.has_value());
    ASSERT_FALSE(lifted.diagnostics.empty());
    for (const auto& diagnostic : lifted.diagnostics) {
        EXPECT_EQ(diagnostic.message.rfind("The source did not parse: ", 0), 0u) << diagnostic.message;
    }
}
