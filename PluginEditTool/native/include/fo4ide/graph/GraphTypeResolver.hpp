#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include <fo4ide/graph/GraphDocument.hpp>
#include <fo4ide/graph/NodeDefinition.hpp>
#include <fo4ide/papyrus/ScriptIndex.hpp>
#include <fo4ide/papyrus/Type.hpp>

namespace fo4ide::graph {

// Port of GenericBinding: solved generic variables (T) to concrete
// (typeName, isArray) pairs. First binding wins, mirroring the C#.
struct GenericBinding {
    std::map<std::string, std::pair<std::string, bool>> bound;

    bool tryGet(std::string_view variable, std::string& typeName, bool& isArray) const;
    void bind(std::string_view variable, std::string typeName, bool isArray);
    bool isBound(std::string_view variable) const;
};

// Port of GraphTypeResolver: converts pin type expressions to concrete types
// (resolving Self against the graph's script name), judges wire
// compatibility via the Papyrus conversion rules, and solves generics from
// the wires touching each node. SourcesComplete goes false when a needed
// script or type name cannot be resolved from the import roots.
class GraphTypeResolver {
public:
    GraphTypeResolver(papyrus::ScriptIndex& index, std::string selfType,
                      std::optional<std::string> selfExtends = std::nullopt);

    bool sourcesComplete() const { return sourcesComplete_; }

    bool inheritsFrom(std::string_view child, std::string_view ancestor);

    // Port of TypeOf: (typeName, isArray) for a pin type expression. An
    // unsolved generic resolves to empty, like the C#.
    std::pair<std::string, bool> typeOf(const PinTypeExpr* type,
                                        const GenericBinding& generics);

    // Port of Resolve: primitives by name, then the Papyrus resolver against
    // `relativeTo`; nullopt (and sourcesComplete=false) when unresolvable.
    std::optional<papyrus::Type> resolve(std::string_view typeName, bool isArray,
                                         const papyrus::Script* relativeTo);

    enum class WireVerdict { Implicit, NeedsCast, Incompatible, Unknown };

    WireVerdict judge(std::string_view fromType, bool fromArray, std::string_view toType,
                      bool toArray, const papyrus::Script* relativeTo);

    // Port of SolveGenerics: binds generic variables from the data pins'
    // source types, inputs first (then id order), first binding wins.
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

}  // namespace fo4ide::graph
