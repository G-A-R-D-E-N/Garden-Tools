#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include <fo4/graph/graph_compiler.hpp>

#include "graph_compile_support.hpp"

using namespace fo4::graph;
using namespace fo4::graph::test;

TEST(GraphNegative, ACallOnAForeignTypeWithNoTargetIsRefusedNamingTheSelfPin) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto level = graph.node(env.palette, "call:Actor.GetLevel");
    graph.wire(entry, pinIds::kExec, level, pinIds::kExec);

    const auto result = compileDoc(env, graph);

    EXPECT_FALSE(result.success());
    const auto* diagnostic = firstError(result.diagnostics, codes::kUnconnectedSelf);
    ASSERT_TRUE(diagnostic != nullptr) << describe(result.diagnostics);
    EXPECT_EQ(diagnostic->nodeId.value_or(""), level);
    EXPECT_EQ(diagnostic->pinId.value_or(""), std::string(pinIds::kSelf));
}

TEST(GraphNegative, AReturningFunctionWhoseReturnNodeCarriesNoValueIsRefused) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, builtinDefs::kFunctionEntry,
                                  {{"name", "Count"}, {"returns", "int"}});
    const auto ret = graph.node(env.palette, builtinDefs::kReturn);
    graph.wire(entry, pinIds::kExec, ret, pinIds::kExec);

    const auto result = compileDoc(env, graph);

    EXPECT_FALSE(result.success());
    const auto* diagnostic = firstError(result.diagnostics, codes::kReturnValueMissing);
    ASSERT_TRUE(diagnostic != nullptr) << describe(result.diagnostics);
    EXPECT_EQ(diagnostic->nodeId.value_or(""), ret);
    EXPECT_EQ(diagnostic->pinId.value_or(""), std::string(pinIds::kValue));
}

TEST(GraphNegative, AVoidFunctionWhoseReturnNodeCarriesAValueIsRefused) {
    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");

    const auto entry = graph.node(env.palette, builtinDefs::kFunctionEntry, {{"name", "Go"}});
    const auto ret = graph.node(env.palette, builtinDefs::kReturn);
    const auto number = graph.node(env.palette, "literal.int");

    graph.value(number, pinIds::kValue, "int", "1");
    graph.wire(entry, pinIds::kExec, ret, pinIds::kExec);
    graph.wire(number, pinIds::kValue, ret, pinIds::kValue);

    const auto result = compileDoc(env, graph);

    EXPECT_FALSE(result.success());
    const auto* diagnostic = firstError(result.diagnostics, codes::kReturnValueUnexpected);
    ASSERT_TRUE(diagnostic != nullptr) << describe(result.diagnostics);
    EXPECT_EQ(diagnostic->nodeId.value_or(""), ret);
}

TEST(GraphNegative, EveryRefusalNamesANodeOrSaysWhyItCannot) {
    const std::vector<std::string_view> documentLevel{
        codes::kInvalidScriptHeader, codes::kUndeclaredReference, codes::kNoEntryNodes,
        codes::kDuplicateVariableName, codes::kMalformedDocument, codes::kUnsupportedSchema,
    };

    Env env(true);
    GraphBuilder graph("Fixture", "ObjectReference");
    const auto entry = graph.node(env.palette, "event:ObjectReference.OnLoad");
    const auto wait = graph.node(env.palette, "global:Utility.Wait");
    graph.wire(entry, pinIds::kExec, wait, pinIds::kExec);

    const auto result = compileDoc(env, graph);

    const auto errors = result.errors();
    ASSERT_FALSE(errors.empty()) << "the missing Wait argument should be refused";
    for (const auto* diagnostic : errors) {
        if (std::find(documentLevel.begin(), documentLevel.end(), diagnostic->code) !=
            documentLevel.end()) {
            continue;
        }
        EXPECT_FALSE(diagnostic->nodeId.value_or("").empty())
            << diagnostic->code << " has to name the node it is about";
    }
}
