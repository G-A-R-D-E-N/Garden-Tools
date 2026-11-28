#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/graph/node_definition.hpp>

namespace fo4::graph {

namespace builtinDefinitions {

const std::vector<NodeDefinition>& all();

const NodeDefinition* find(std::string_view id);

std::optional<std::string_view> operatorToken(std::string_view definitionId);

std::optional<std::string_view> arrayMemberName(std::string_view definitionId);

const std::vector<std::string>& arrayMemberNames();

}

}
