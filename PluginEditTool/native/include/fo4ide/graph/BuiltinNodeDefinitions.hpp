#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fo4ide/graph/NodeDefinition.hpp>

namespace fo4ide::graph {

// Port of BuiltinNodeDefinitions: the fixed node catalog (flow, values,
// operators, literals, array operations) built once and served from a static.
// Id constants live in the `builtinDefs` namespace in GraphDocument.hpp.
namespace builtinDefinitions {

// All builtin definitions, in the C# build order.
const std::vector<NodeDefinition>& all();

// First builtin whose id equals (case-insensitively) `id`; null for unknown.
const NodeDefinition* find(std::string_view id);

// The source token for an "op.*" definition id, or null (mirrors
// OperatorToken: binary first, then unary).
std::optional<std::string_view> operatorToken(std::string_view definitionId);

// The array member name for an "array.*" definition id, or null.
std::optional<std::string_view> arrayMemberName(std::string_view definitionId);

// The member names of the array operations, in declaration order.
const std::vector<std::string>& arrayMemberNames();

}  // namespace builtinDefinitions

}  // namespace fo4ide::graph
