#include <fo4/graph/graph_validator.hpp>

#include <algorithm>
#include <cctype>
#include <set>

#include <fo4/papyrus/parser.hpp>

namespace fo4::graph {
namespace {

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::optional<PinDefinition> pinOf(const NodeDefinition& definition, const GraphNode& node,
                                   const GraphDocument& document, std::string_view id) {
    for (const auto& pin : definition.pinsFor(node, &document)) {
        if (iequals(pin.id, id)) return pin;
    }
    return std::nullopt;
}

}

GraphValidator::GraphValidator(papyrus::ScriptIndex& index, NodePalette& palette)
    : index_(index), palette_(palette) {}

GraphValidation GraphValidator::validate(GraphDocument& document) {
    document.invalidate();

    std::vector<GraphDiagnostic> problems;
    std::map<std::string, const NodeDefinition*> definitions;

    validateHeader(document, problems);
    resolveDefinitions(document, definitions, problems);
    validateWires(document, definitions, problems);
    validateDeclarations(document, problems);

    const auto selfType = document.header.scriptName;
    GraphTypeResolver types(index_, selfType, document.header.extends);
    const auto owner = ownerScriptFor(document);
    const auto generics = solveGenerics(document, definitions, types);
    const papyrus::Script* ownerPtr = owner ? &*owner : nullptr;

    validateWireTypes(document, definitions, generics, types, ownerPtr, problems);
    validateRequiredInputs(document, definitions, problems);
    validateEntries(document, definitions, problems);

    return GraphValidation{std::move(problems), std::move(definitions), generics,
                           types.sourcesComplete()};
}

std::optional<papyrus::Script> GraphValidator::ownerScriptFor(
    const GraphDocument& document) const {
    if (document.header.scriptName.empty() ||
        std::all_of(document.header.scriptName.begin(), document.header.scriptName.end(),
                    [](unsigned char c) { return std::isspace(c); })) {
        return std::nullopt;
    }

    std::string header = "Scriptname " + document.header.scriptName;
    if (document.header.extends && !document.header.extends->empty()) {
        header += " extends " + *document.header.extends;
    }

    for (const auto& import : document.header.imports) {
        header += "\nImport " + import;
    }

    return papyrus::Parser::parse(header + "\n", document.header.scriptName + ".psc");
}

void GraphValidator::validateHeader(const GraphDocument& document,
                                    std::vector<GraphDiagnostic>& problems) const {
    const auto& name = document.header.scriptName;
    if (name.empty() ||
        std::all_of(name.begin(), name.end(),
                    [](unsigned char c) { return std::isspace(c); })) {
        problems.push_back(GraphDiagnostic{std::string(codes::kInvalidScriptHeader),
                                           GraphSeverity::Error,
                                           "The graph has no script name.",
                                           std::nullopt, std::nullopt, std::nullopt, {}, 0,
                                           std::nullopt});
        return;
    }

    bool badPart = false;
    std::size_t start = 0;
    while (start <= name.size()) {
        const auto colon = name.find(':', start);
        const auto part = colon == std::string::npos
                              ? std::string_view(name).substr(start)
                              : std::string_view(name).substr(start, colon - start);
        if (!isIdentifier(part)) {
            badPart = true;
            break;
        }
        if (colon == std::string::npos) break;
        start = colon + 1;
    }
    if (badPart) {
        problems.push_back(GraphDiagnostic{std::string(codes::kInvalidScriptHeader),
                                           GraphSeverity::Error,
                                           "'" + name + "' is not a usable script name.",
                                           std::nullopt, std::nullopt, std::nullopt, {}, 0,
                                           std::nullopt});
    }

    const auto& extends = document.header.extends;
    if (extends && !extends->empty() &&
        std::any_of(extends->begin(), extends->end(),
                    [](unsigned char c) { return !std::isspace(c); }) &&
        index_.resolve(*extends) == nullptr) {
        problems.push_back(GraphDiagnostic{
            std::string(codes::kUndeclaredReference), GraphSeverity::Error,
            "This graph extends '" + *extends + "', which is not on the import roots.",
            std::nullopt, std::nullopt, std::nullopt, {}, 0, std::nullopt});
    }
}

void GraphValidator::resolveDefinitions(
    const GraphDocument& document, std::map<std::string, const NodeDefinition*>& definitions,
    std::vector<GraphDiagnostic>& problems) const {
    std::set<std::string> seenIds;

    for (const auto& node : document.nodes) {
        if (node.id.empty()) {
            problems.push_back(GraphDiagnostic{std::string(codes::kMalformedDocument),
                                               GraphSeverity::Error, "A node has no id.",
                                               std::nullopt, std::nullopt, std::nullopt, {}, 0,
                                               std::nullopt});
            continue;
        }

        if (!seenIds.insert(node.id).second) {
            problems.push_back(GraphDiagnostic{
                std::string(codes::kDuplicateDeclaration), GraphSeverity::Error,
                "Node id '" + node.id + "' appears more than once.", node.id, std::nullopt,
                std::nullopt, {}, 0, std::nullopt});
            continue;
        }

        const auto* definition = palette_.find(node.definition);
        if (definition == nullptr) {
            problems.push_back(GraphDiagnostic{
                std::string(codes::kUnknownNodeDefinition), GraphSeverity::Error,
                "'" + node.definition + "' is not on this palette. The script it comes from may "
                "not be on the import roots.",
                node.id, std::nullopt, std::nullopt, {}, 0, std::nullopt});
            continue;
        }

        definitions[node.id] = definition;
    }
}

void GraphValidator::validateWires(
    const GraphDocument& document,
    const std::map<std::string, const NodeDefinition*>& definitions,
    std::vector<GraphDiagnostic>& problems) const {
    struct Tracked {
        PinRef pin;
        std::vector<const GraphWire*> wires;
    };
    std::map<std::string, Tracked> dataInputs;
    std::map<std::string, Tracked> execOutputs;

    const auto track = [](std::map<std::string, Tracked>& into, const PinRef& pin,
                          const GraphWire& wire) {
        const auto key = pin.toString();
        auto& entry = into[key];
        entry.pin = pin;
        entry.wires.push_back(&wire);
    };

    const auto report = [](const std::map<std::string, Tracked>& tracked, std::string_view code,
                           std::string message, std::vector<GraphDiagnostic>& out) {
        for (const auto& [key, entry] : tracked) {
            if (entry.wires.size() <= 1) continue;
            out.push_back(GraphDiagnostic{std::string(code), GraphSeverity::Error,
                                          message + " This one has " +
                                              std::to_string(entry.wires.size()) + ".",
                                          entry.pin.node, entry.pin.pin,
                                          entry.wires.front()->id, {}, 0, std::nullopt});
        }
    };

    for (const auto& wire : document.wires) {
        const auto* fromNode = document.node(wire.from.node);
        const auto* toNode = document.node(wire.to.node);

        if (fromNode == nullptr || toNode == nullptr) {
            problems.push_back(GraphDiagnostic{std::string(codes::kDanglingWire),
                                               GraphSeverity::Error,
                                               "A wire names a node that is not in this graph.",
                                               std::nullopt, std::nullopt, wire.id, {}, 0,
                                               std::nullopt});
            continue;
        }

        const auto fromDef = definitions.find(fromNode->id);
        const auto toDef = definitions.find(toNode->id);
        if (fromDef == definitions.end() || toDef == definitions.end()) continue;

        const auto fromPin = pinOf(*fromDef->second, *fromNode, document, wire.from.pin);
        const auto toPin = pinOf(*toDef->second, *toNode, document, wire.to.pin);

        if (!fromPin || !toPin) {
            const auto& missing = !fromPin ? wire.from : wire.to;
            problems.push_back(GraphDiagnostic{
                std::string(codes::kDanglingWire), GraphSeverity::Error,
                "Pin '" + missing.pin + "' no longer exists on this node.", missing.node,
                missing.pin, wire.id, {}, 0, std::nullopt});
            continue;
        }

        if (fromPin->direction != PinDirection::Out || toPin->direction != PinDirection::In) {
            problems.push_back(GraphDiagnostic{
                std::string(codes::kWireDirection), GraphSeverity::Error,
                "A wire has to run from an output pin to an input pin.", wire.from.node,
                wire.from.pin, wire.id, {wire.to.node}, 0, std::nullopt});
            continue;
        }

        if (fromPin->kind != toPin->kind) {
            problems.push_back(GraphDiagnostic{
                std::string(codes::kPinKindMismatch), GraphSeverity::Error,
                "A control flow pin cannot be wired to a value pin.", wire.to.node, wire.to.pin,
                wire.id, {wire.from.node}, 0, std::nullopt});
            continue;
        }

        if (toPin->kind == PinKind::Data) {
            track(dataInputs, wire.to, wire);
        } else {
            track(execOutputs, wire.from, wire);
        }
    }

    report(dataInputs, codes::kMultipleDataSources, "A value input can only take one wire.",
           problems);
    report(execOutputs, codes::kMultipleExecSuccessors,
           "A control flow output can only lead to one node.", problems);
}

void GraphValidator::validateDeclarations(const GraphDocument& document,
                                          std::vector<GraphDiagnostic>& problems) const {
    std::map<std::string, int, std::less<>> byLower;
    for (const auto& variable : document.variables) {
        std::string key;
        for (const char c : variable.name) {
            key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        ++byLower[key];
    }
    for (const auto& [key, count] : byLower) {
        if (count <= 1) continue;
        problems.push_back(GraphDiagnostic{std::string(codes::kDuplicateVariableName),
                                           GraphSeverity::Error,
                                           "'" + key + "' is declared " + std::to_string(count) +
                                               " times.",
                                           std::nullopt, std::nullopt, std::nullopt, {}, 0,
                                           std::nullopt});
    }

    for (const auto& variable : document.variables) {
        if (isIdentifier(variable.name)) continue;
        problems.push_back(GraphDiagnostic{std::string(codes::kInvalidScriptHeader),
                                           GraphSeverity::Error,
                                           "'" + variable.name +
                                               "' is not a usable variable name.",
                                           std::nullopt, std::nullopt, std::nullopt, {}, 0,
                                           std::nullopt});
    }
}

std::map<std::string, GenericBinding> GraphValidator::solveGenerics(
    const GraphDocument& document,
    const std::map<std::string, const NodeDefinition*>& definitions,
    GraphTypeResolver& types) const {
    std::map<std::string, GenericBinding> solved;

    for (const auto& node : document.nodes) {
        const auto found = definitions.find(node.id);
        if (found == definitions.end()) continue;

        const auto sourceTypeOf =
            [&](const PinRef& pin) -> std::optional<std::pair<std::string, bool>> {
            const auto into = document.into(pin);
            const auto outOf = document.outOf(pin);
            const auto* wire = !into.empty() ? into.front() : (!outOf.empty() ? outOf.front() : nullptr);
            if (wire == nullptr) return std::nullopt;

            const auto other = iequals(wire->to.node, pin.node) &&
                                       iequals(wire->to.pin, pin.pin)
                                   ? wire->from
                                   : wire->to;

            const auto* otherNode = document.node(other.node);
            if (otherNode == nullptr) return std::nullopt;
            const auto otherDef = definitions.find(other.node);
            if (otherDef == definitions.end()) return std::nullopt;

            const auto otherPin = pinOf(*otherDef->second, *otherNode, document, other.pin);
            if (!otherPin || !otherPin->type) return std::nullopt;
            if (otherPin->type->form != PinTypeForm::Concrete &&
                otherPin->type->form != PinTypeForm::SelfType) {
                return std::nullopt;
            }

            return types.typeOf(&*otherPin->type, GenericBinding{});
        };

        solved[node.id] = types.solveGenerics(node, *found->second, document, sourceTypeOf);
    }

    return solved;
}

void GraphValidator::validateWireTypes(
    const GraphDocument& document,
    const std::map<std::string, const NodeDefinition*>& definitions,
    const std::map<std::string, GenericBinding>& generics, GraphTypeResolver& types,
    const papyrus::Script* owner, std::vector<GraphDiagnostic>& problems) const {
    for (const auto& wire : document.wires) {
        const auto* fromNode = document.node(wire.from.node);
        const auto* toNode = document.node(wire.to.node);
        if (fromNode == nullptr || toNode == nullptr) continue;
        const auto fromDef = definitions.find(fromNode->id);
        const auto toDef = definitions.find(toNode->id);
        if (fromDef == definitions.end() || toDef == definitions.end()) continue;

        const auto fromPin = pinOf(*fromDef->second, *fromNode, document, wire.from.pin);
        const auto toPin = pinOf(*toDef->second, *toNode, document, wire.to.pin);
        if (!fromPin || !toPin) continue;
        if (!fromPin->type || !toPin->type) continue;
        if (fromPin->kind != PinKind::Data || toPin->kind != PinKind::Data) continue;

        const auto fromGenerics = [&] {
            const auto found = generics.find(fromNode->id);
            return found == generics.end() ? GenericBinding{} : found->second;
        }();
        const auto toGenerics = [&] {
            const auto found = generics.find(toNode->id);
            return found == generics.end() ? GenericBinding{} : found->second;
        }();

        const auto [fromType, fromArray] = types.typeOf(&*fromPin->type, fromGenerics);
        const auto [toType, toArray] = types.typeOf(&*toPin->type, toGenerics);

        if (fromType.empty() || toType.empty()) {
            problems.push_back(GraphDiagnostic{
                std::string(codes::kUndeclaredReference), GraphSeverity::Error,
                "This pin's type could not be worked out. Wire the array pin first.",
                fromType.empty() ? wire.from.node : wire.to.node,
                fromType.empty() ? wire.from.pin : wire.to.pin, wire.id, {}, 0, std::nullopt});
            continue;
        }

        const auto verdict = types.judge(fromType, fromArray, toType, toArray, owner);
        if (verdict == GraphTypeResolver::WireVerdict::Implicit ||
            verdict == GraphTypeResolver::WireVerdict::Unknown) {
            continue;
        }

        const auto array = [](bool a) { return a ? "[]" : ""; };
        const auto isNeedsCast = verdict == GraphTypeResolver::WireVerdict::NeedsCast;
        const auto message = isNeedsCast
                                 ? fromType + array(fromArray) + " does not fit " + toType +
                                       array(toArray) + " on its own. Put a Cast node in between."
                                 : fromType + array(fromArray) + " cannot be used as " + toType +
                                       array(toArray) + ".";
        problems.push_back(GraphDiagnostic{
            isNeedsCast ? std::string(codes::kNarrowingWireNeedsCast)
                        : std::string(codes::kIncompatibleWireType),
            GraphSeverity::Error, message, wire.to.node, wire.to.pin, wire.id,
            {wire.from.node}, 0, std::nullopt});
    }
}

void GraphValidator::validateRequiredInputs(
    const GraphDocument& document,
    const std::map<std::string, const NodeDefinition*>& definitions,
    std::vector<GraphDiagnostic>& problems) const {
    for (const auto& node : document.nodes) {
        const auto found = definitions.find(node.id);
        if (found == definitions.end()) continue;

        for (const auto& pin : found->second->pinsFor(node, &document)) {
            if (pin.kind != PinKind::Data || pin.direction != PinDirection::In) continue;
            if (pin.isOptional) continue;
            if (!document.into(PinRef{node.id, pin.id}).empty()) continue;
            if (node.pinValues.contains(pin.id)) continue;
            if (pin.declaredDefault) continue;
            if (iequals(pin.id, pinIds::kSelf)) continue;

            problems.push_back(GraphDiagnostic{
                std::string(codes::kMissingRequiredInput), GraphSeverity::Error,
                "'" + (pin.label.empty() ? pin.id : pin.label) + "' needs a value.", node.id,
                pin.id, std::nullopt, {}, 0, std::nullopt});
        }
    }
}

void GraphValidator::validateEntries(
    const GraphDocument& document,
    const std::map<std::string, const NodeDefinition*>& definitions,
    std::vector<GraphDiagnostic>& problems) const {
    std::vector<const GraphNode*> entries;
    for (const auto& node : document.nodes) {
        const auto found = definitions.find(node.id);
        if (found == definitions.end()) continue;
        const auto kind = found->second->kind;
        if (kind == GraphNodeKind::EventEntry || kind == GraphNodeKind::FunctionEntry) {
            entries.push_back(&node);
        }
    }

    if (entries.empty() && !document.nodes.empty()) {
        problems.push_back(GraphDiagnostic{
            std::string(codes::kNoEntryNodes), GraphSeverity::Error,
            "This graph has no event or function to start from, so nothing would run.",
            std::nullopt, std::nullopt, std::nullopt, {}, 0, std::nullopt});
    }

    std::map<std::string, std::vector<const GraphNode*>, std::less<>> byKey;
    for (const auto* entry : entries) {
        const auto* definition = definitions.at(entry->id);
        std::string key;
        if (definition->isRemoteEvent && definition->ownerScript) {
            key += *definition->ownerScript + ".";
        }
        key += entryNameOf(*entry, *definition);
        key += "/";
        key += entry->configString("state").value_or("");
        std::string lowered;
        for (const char c : key) {
            lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        byKey[lowered].push_back(entry);
    }

    for (const auto& [key, group] : byKey) {
        if (group.size() <= 1) continue;
        const auto* first = group.front();
        const auto* definition = definitions.at(first->id);
        std::vector<std::string> related;
        for (std::size_t i = 1; i < group.size(); ++i) related.push_back(group[i]->id);
        problems.push_back(GraphDiagnostic{
            std::string(codes::kDuplicateDeclaration), GraphSeverity::Error,
            "'" + entryNameOf(*first, *definition) + "' is declared more than once.", first->id,
            std::nullopt, std::nullopt, std::move(related), 0, std::nullopt});
    }

    validateStates(document, entries, problems);
    validateCustomEvents(document, problems);
}

void GraphValidator::validateStates(const GraphDocument& document,
                                    const std::vector<const GraphNode*>& entries,
                                    std::vector<GraphDiagnostic>& problems) const {
    std::set<std::string, std::less<>> declared;
    for (const auto& entry : entries) {
        const auto state = entry->configString("state");
        if (!state || state->empty() ||
            std::all_of(state->begin(), state->end(),
                        [](unsigned char c) { return std::isspace(c); })) {
            continue;
        }

        if (!isIdentifier(*state)) {
            problems.push_back(GraphDiagnostic{std::string(codes::kInvalidScriptHeader),
                                               GraphSeverity::Error,
                                               "'" + *state + "' is not a usable state name.",
                                               entry->id, std::nullopt, std::nullopt, {}, 0,
                                               std::nullopt});
            continue;
        }

        std::string lowered;
        for (const char c : *state) {
            lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        declared.insert(lowered);

        if (entry->configString("global") == "true") {
            problems.push_back(GraphDiagnostic{
                std::string(codes::kInvalidScriptHeader), GraphSeverity::Error,
                "A global function belongs to the script, not to a state, so it cannot be "
                "placed in one.",
                entry->id, std::nullopt, std::nullopt, {}, 0, std::nullopt});
        }
    }

    const auto& autoState = document.header.autoState;
    if (autoState && !autoState->empty() &&
        std::any_of(autoState->begin(), autoState->end(),
                    [](unsigned char c) { return !std::isspace(c); })) {
        std::string lowered;
        for (const char c : *autoState) {
            lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        if (!declared.contains(lowered)) {
            problems.push_back(GraphDiagnostic{
                std::string(codes::kInvalidScriptHeader), GraphSeverity::Error,
                "The script starts in state '" + *autoState +
                    "', but no event or function declares that state.",
                std::nullopt, std::nullopt, std::nullopt, {}, 0, std::nullopt});
        }
    }
}

void GraphValidator::validateCustomEvents(const GraphDocument& document,
                                          std::vector<GraphDiagnostic>& problems) const {
    std::set<std::string, std::less<>> seen;
    for (const auto& declared : document.customEvents) {
        if (!isIdentifier(declared)) {
            problems.push_back(GraphDiagnostic{std::string(codes::kInvalidScriptHeader),
                                               GraphSeverity::Error,
                                               "'" + declared +
                                                   "' is not a usable custom event name.",
                                               std::nullopt, std::nullopt, std::nullopt, {}, 0,
                                               std::nullopt});
            continue;
        }

        std::string lowered;
        for (const char c : declared) {
            lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        if (!seen.insert(lowered).second) {
            problems.push_back(GraphDiagnostic{
                std::string(codes::kDuplicateDeclaration), GraphSeverity::Error,
                "Custom event '" + declared + "' is declared more than once.", std::nullopt,
                std::nullopt, std::nullopt, {}, 0, std::nullopt});
        }
    }
}

std::string GraphValidator::entryNameOf(const GraphNode& node,
                                        const NodeDefinition& definition) {
    if (definition.kind == GraphNodeKind::FunctionEntry) {
        return node.configString("name").value_or("Unnamed");
    }
    if (definition.memberName) return *definition.memberName;
    return definition.title;
}

bool GraphValidator::isIdentifier(std::string_view text) {
    if (text.empty()) return false;
    const auto first = static_cast<unsigned char>(text.front());
    if (!std::isalpha(first) && first != '_') return false;
    for (const auto c : text) {
        const auto u = static_cast<unsigned char>(c);
        if (!std::isalnum(u) && c != '_') return false;
    }
    return true;
}

}
