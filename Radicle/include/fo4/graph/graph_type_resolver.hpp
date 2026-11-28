#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include <fo4/graph/graph_document.hpp>
#include <fo4/graph/node_definition.hpp>
#include <fo4/papyrus/script_index.hpp>
#include <fo4/papyrus/type.hpp>

namespace fo4::graph {

struct GenericBinding {
    std::map<std::string, std::pair<std::string, bool>> bound;

    bool tryGet(std::string_view variable, std::string& typeName, bool& isArray) const;
    void bind(std::string_view variable, std::string typeName, bool isArray);
    bool isBound(std::string_view variable) const;
};

class GraphTypeResolver {
public:
    GraphTypeResolver(papyrus::ScriptIndex& index, std::string selfType,
                      std::optional<std::string> selfExtends = std::nullopt);

    bool sourcesComplete() const { return sourcesComplete_; }

    bool inheritsFrom(std::string_view child, std::string_view ancestor);

    std::pair<std::string, bool> typeOf(const PinTypeExpr* type,
                                        const GenericBinding& generics);

    std::optional<papyrus::Type> resolve(std::string_view typeName, bool isArray,
                                         const papyrus::Script* relativeTo);

    enum class WireVerdict { Implicit, NeedsCast, Incompatible, Unknown };

    WireVerdict judge(std::string_view fromType, bool fromArray, std::string_view toType,
                      bool toArray, const papyrus::Script* relativeTo);

    GenericBinding solveGenerics(const GraphNode& node, const NodeDefinition& definition,
                                 const GraphDocument& document,
                                 const std::function<std::optional<std::pair<std::string, bool>>(
                                     const PinRef&)>& typeOfSource);

private:
    papyrus::ScriptIndex& index_;
    std::string selfType_;
    std::optional<std::string> selfExtends_;
    std::map<std::pair<std::string, std::string>, bool> inherits_;
    mutable bool sourcesComplete_{true};
};

}
