#include <fo4/graph/graph_document.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace fo4::graph {

bool equalsIgnoreCase(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(left[i])) !=
            std::tolower(static_cast<unsigned char>(right[i]))) {
            return false;
        }
    }
    return true;
}

bool startsWithIgnoreCase(std::string_view value, std::string_view prefix) {
    if (value.size() < prefix.size()) return false;
    return equalsIgnoreCase(value.substr(0, prefix.size()), prefix);
}

int compareIgnoreCase(std::string_view left, std::string_view right) {
    const auto size = std::min(left.size(), right.size());
    for (std::size_t i = 0; i < size; ++i) {
        const auto a = static_cast<unsigned char>(
            std::tolower(static_cast<unsigned char>(left[i])));
        const auto b = static_cast<unsigned char>(
            std::tolower(static_cast<unsigned char>(right[i])));
        if (a != b) return static_cast<int>(a) - static_cast<int>(b);
    }
    return static_cast<int>(left.size()) - static_cast<int>(right.size());
}

std::string_view graphNodeKindName(GraphNodeKind kind) {
    switch (kind) {
        case GraphNodeKind::Unknown: return "unknown";
        case GraphNodeKind::EventEntry: return "eventEntry";
        case GraphNodeKind::FunctionEntry: return "functionEntry";
        case GraphNodeKind::Return: return "return";
        case GraphNodeKind::Break: return "break";
        case GraphNodeKind::Continue: return "continue";
        case GraphNodeKind::Branch: return "branch";
        case GraphNodeKind::While: return "while";
        case GraphNodeKind::ForEach: return "forEach";
        case GraphNodeKind::Call: return "call";
        case GraphNodeKind::PropertyGet: return "propertyGet";
        case GraphNodeKind::PropertySet: return "propertySet";
        case GraphNodeKind::VariableGet: return "variableGet";
        case GraphNodeKind::VariableSet: return "variableSet";
        case GraphNodeKind::LocalDeclare: return "localDeclare";
        case GraphNodeKind::Literal: return "literal";
        case GraphNodeKind::Self: return "self";
        case GraphNodeKind::Parent: return "parent";
        case GraphNodeKind::NoneValue: return "noneValue";
        case GraphNodeKind::MemberGet: return "memberGet";
        case GraphNodeKind::IndexGet: return "indexGet";
        case GraphNodeKind::IndexSet: return "indexSet";
        case GraphNodeKind::ArrayOp: return "arrayOp";
        case GraphNodeKind::NewArray: return "newArray";
        case GraphNodeKind::StructNew: return "structNew";
        case GraphNodeKind::StructGet: return "structGet";
        case GraphNodeKind::StructSet: return "structSet";
        case GraphNodeKind::Unary: return "unary";
        case GraphNodeKind::Binary: return "binary";
        case GraphNodeKind::Cast: return "cast";
        case GraphNodeKind::TypeCheck: return "typeCheck";
        case GraphNodeKind::Reroute: return "reroute";
        case GraphNodeKind::Comment: return "comment";
    }
    return "unknown";
}

GraphNodeKind graphNodeKindFromName(std::string_view name) {
    for (int i = 0; i <= static_cast<int>(GraphNodeKind::Comment); ++i) {
        const auto kind = static_cast<GraphNodeKind>(i);
        if (equalsIgnoreCase(graphNodeKindName(kind), name)) return kind;
    }
    return GraphNodeKind::Unknown;
}

std::string_view graphKindName(GraphKind kind) {
    switch (kind) {
        case GraphKind::PapyrusScript: return "papyrusScript";
        case GraphKind::F4SEBinding: return "f4seBinding";
    }
    return "papyrusScript";
}

GraphKind graphKindFromName(std::string_view name) {
    if (equalsIgnoreCase(graphKindName(GraphKind::F4SEBinding), name)) {
        return GraphKind::F4SEBinding;
    }
    return GraphKind::PapyrusScript;
}

void GraphDocument::buildIndex() const {
    byId_.clear();
    byId_.reserve(nodes.size());
    for (const auto& node : nodes) {
        byId_.emplace(node.id, &node);
    }
}

const GraphNode* GraphDocument::node(const std::string& nodeId) const {
    if (nodeId.empty()) return nullptr;
    if (byId_.empty()) buildIndex();
    const auto it = byId_.find(nodeId);
    return it == byId_.end() ? nullptr : it->second;
}

GraphNode* GraphDocument::node(const std::string& nodeId) {
    return const_cast<GraphNode*>(static_cast<const GraphDocument&>(*this).node(nodeId));
}

void GraphDocument::invalidate() { byId_.clear(); }

std::vector<const GraphWire*> GraphDocument::into(const PinRef& pin) const {
    std::vector<const GraphWire*> result;
    for (const auto& wire : wires) {
        if (equalsIgnoreCase(wire.to.node, pin.node) && equalsIgnoreCase(wire.to.pin, pin.pin)) {
            result.push_back(&wire);
        }
    }
    return result;
}

std::vector<const GraphWire*> GraphDocument::outOf(const PinRef& pin) const {
    std::vector<const GraphWire*> result;
    for (const auto& wire : wires) {
        if (equalsIgnoreCase(wire.from.node, pin.node) &&
            equalsIgnoreCase(wire.from.pin, pin.pin)) {
            result.push_back(&wire);
        }
    }
    return result;
}

std::vector<const GraphWire*> GraphDocument::touching(const std::string& nodeId) const {
    std::vector<const GraphWire*> result;
    for (const auto& wire : wires) {
        if (wire.from.node == nodeId || wire.to.node == nodeId) result.push_back(&wire);
    }
    return result;
}

namespace {
namespace jsonns = nlohmann;

std::optional<int> jsonInt(const jsonns::json& value) {
    if (!value.is_number_integer()) return std::nullopt;
    if (value.is_number_unsigned()) {
        const auto number = value.get<std::uint64_t>();
        if (number > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) return std::nullopt;
        return static_cast<int>(number);
    }
    const auto number = value.get<std::int64_t>();
    if (number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max()) {
        return std::nullopt;
    }
    return static_cast<int>(number);
}

jsonns::json pinRefToJson(const PinRef& ref) {
    return jsonns::json{{"node", ref.node}, {"pin", ref.pin}};
}

PinRef pinRefFromJson(const jsonns::json& value) {
    PinRef ref;
    if (const auto it = value.find("node"); it != value.end() && it->is_string()) {
        ref.node = it->get<std::string>();
    }
    if (const auto it = value.find("pin"); it != value.end() && it->is_string()) {
        ref.pin = it->get<std::string>();
    }
    return ref;
}

jsonns::json variableToJson(const GraphVariable& variable) {
    jsonns::json out{{"name", variable.name}, {"type", variable.type},
                     {"isProperty", variable.isProperty}, {"flags", variable.flags}};
    if (variable.initial) out["initial"] = *variable.initial;
    return out;
}

GraphVariable variableFromJson(const jsonns::json& value) {
    GraphVariable variable;
    if (const auto it = value.find("name"); it != value.end() && it->is_string()) {
        variable.name = it->get<std::string>();
    }
    if (const auto it = value.find("type"); it != value.end() && it->is_string()) {
        variable.type = it->get<std::string>();
    }
    if (const auto it = value.find("isProperty"); it != value.end() && it->is_boolean()) {
        variable.isProperty = it->get<bool>();
    }
    if (const auto it = value.find("initial"); it != value.end() && it->is_string()) {
        variable.initial = it->get<std::string>();
    }
    if (const auto it = value.find("flags"); it != value.end() && it->is_array()) {
        for (const auto& flag : *it) {
            if (flag.is_string()) variable.flags.push_back(flag.get<std::string>());
        }
    }
    return variable;
}

jsonns::json structToJson(const GraphStruct& structure) {
    jsonns::json members = jsonns::json::array();
    for (const auto& member : structure.members) members.push_back(variableToJson(member));
    return jsonns::json{{"name", structure.name}, {"members", std::move(members)}};
}

GraphStruct structFromJson(const jsonns::json& value) {
    GraphStruct structure;
    if (const auto it = value.find("name"); it != value.end() && it->is_string()) {
        structure.name = it->get<std::string>();
    }
    if (const auto it = value.find("members"); it != value.end() && it->is_array()) {
        for (const auto& member : *it) {
            if (member.is_object()) structure.members.push_back(variableFromJson(member));
        }
    }
    return structure;
}

jsonns::json nodeToJson(const GraphNode& node) {
    jsonns::json out{{"id", node.id}, {"def", node.definition},
                     {"kind", graphNodeKindName(node.kind)}, {"x", node.x}, {"y", node.y},
                     {"config", node.config}, {"pinValues", jsonns::json::object()}};
    if (node.comment) out["comment"] = *node.comment;
    for (const auto& [pin, value] : node.pinValues) {
        out["pinValues"][pin] = jsonns::json{{"type", value.type}, {"value", value.value}};
    }
    return out;
}

GraphNode nodeFromJson(const jsonns::json& value) {
    GraphNode node;
    if (const auto it = value.find("id"); it != value.end() && it->is_string()) {
        node.id = it->get<std::string>();
    }
    if (const auto it = value.find("def"); it != value.end() && it->is_string()) {
        node.definition = it->get<std::string>();
    }
    if (const auto it = value.find("kind"); it != value.end() && it->is_string()) {
        node.kind = graphNodeKindFromName(it->get<std::string>());
    }
    if (const auto it = value.find("x"); it != value.end() && it->is_number()) node.x = it->get<double>();
    if (const auto it = value.find("y"); it != value.end() && it->is_number()) node.y = it->get<double>();
    if (const auto it = value.find("comment"); it != value.end() && it->is_string()) {
        node.comment = it->get<std::string>();
    }
    if (const auto it = value.find("config"); it != value.end() && it->is_object()) {
        node.config = *it;
    }
    if (const auto it = value.find("pinValues"); it != value.end() && it->is_object()) {
        for (const auto& [pin, entry] : it->items()) {
            GraphPinValue pinValue;
            if (const auto type = entry.find("type"); type != entry.end() && type->is_string()) {
                pinValue.type = type->get<std::string>();
            }
            if (const auto text = entry.find("value"); text != entry.end() && text->is_string()) {
                pinValue.value = text->get<std::string>();
            }
            node.pinValues[pin] = std::move(pinValue);
        }
    }
    return node;
}

jsonns::json wireToJson(const GraphWire& wire) {
    return jsonns::json{{"id", wire.id}, {"from", pinRefToJson(wire.from)},
                        {"to", pinRefToJson(wire.to)}};
}

GraphWire wireFromJson(const jsonns::json& value) {
    GraphWire wire;
    if (const auto it = value.find("id"); it != value.end() && it->is_string()) {
        wire.id = it->get<std::string>();
    }
    if (const auto it = value.find("from"); it != value.end() && it->is_object()) {
        wire.from = pinRefFromJson(*it);
    }
    if (const auto it = value.find("to"); it != value.end() && it->is_object()) {
        wire.to = pinRefFromJson(*it);
    }
    return wire;
}

jsonns::json documentToJson(const GraphDocument& document) {
    jsonns::json out{{"schema", document.schema}, {"id", document.id},
                     {"kind", graphKindName(document.kind)}};

    jsonns::json header{{"scriptName", document.header.scriptName},
                        {"flags", document.header.flags},
                        {"imports", document.header.imports}};
    if (document.header.extends) header["extends"] = *document.header.extends;
    if (document.header.docComment) header["docComment"] = *document.header.docComment;
    if (document.header.autoState) header["autoState"] = *document.header.autoState;
    out["header"] = std::move(header);

    jsonns::json variables = jsonns::json::array();
    for (const auto& variable : document.variables) {
        variables.push_back(variableToJson(variable));
    }
    out["variables"] = std::move(variables);

    jsonns::json structs = jsonns::json::array();
    for (const auto& structure : document.structs) structs.push_back(structToJson(structure));
    out["structs"] = std::move(structs);

    jsonns::json customEvents = jsonns::json::array();
    for (const auto& event : document.customEvents) customEvents.push_back(event);
    out["customEvents"] = std::move(customEvents);

    jsonns::json nodes = jsonns::json::array();
    for (const auto& node : document.nodes) nodes.push_back(nodeToJson(node));
    out["nodes"] = std::move(nodes);

    jsonns::json wires = jsonns::json::array();
    for (const auto& wire : document.wires) wires.push_back(wireToJson(wire));
    out["wires"] = std::move(wires);

    return out;
}

}

namespace documentJson {

std::string serialize(const GraphDocument& document, bool indented) {
    return documentToJson(document).dump(indented ? 2 : -1);
}

LoadResult tryDeserialize(std::string_view json) {
    LoadResult result;
    if (std::all_of(json.begin(), json.end(),
                    [](unsigned char c) { return std::isspace(c); })) {
        result.error = errorDiagnostic(std::string(codes::kMalformedDocument),
                                       "The graph document is empty.");
        return result;
    }

    jsonns::json root;
    try {
        root = jsonns::json::parse(json);
    } catch (const std::exception& ex) {
        result.error = errorDiagnostic(std::string(codes::kMalformedDocument),
                                       "The graph document could not be read: " +
                                           std::string(ex.what()));
        return result;
    }

    if (!root.is_object()) {
        result.error = errorDiagnostic(std::string(codes::kMalformedDocument),
                                       "The graph document contained no object.");
        return result;
    }

    GraphDocument document;
    if (const auto it = root.find("schema"); it != root.end()) {
        const auto schema = jsonInt(*it);
        if (!schema) {
            result.error = errorDiagnostic(std::string(codes::kMalformedDocument),
                                           "The graph document schema must be an integer.");
            return result;
        }
        document.schema = *schema;
    }
    if (const auto it = root.find("id"); it != root.end() && it->is_string()) {
        document.id = it->get<std::string>();
    }
    if (const auto it = root.find("kind"); it != root.end() && it->is_string()) {
        document.kind = graphKindFromName(it->get<std::string>());
    }
    if (const auto it = root.find("header"); it != root.end() && it->is_object()) {
        const auto& header = *it;
        if (const auto field = header.find("scriptName"); field != header.end() && field->is_string()) {
            document.header.scriptName = field->get<std::string>();
        }
        if (const auto field = header.find("extends"); field != header.end() && field->is_string()) {
            document.header.extends = field->get<std::string>();
        }
        if (const auto field = header.find("docComment"); field != header.end() && field->is_string()) {
            document.header.docComment = field->get<std::string>();
        }
        if (const auto field = header.find("autoState"); field != header.end() && field->is_string()) {
            document.header.autoState = field->get<std::string>();
        }
        if (const auto field = header.find("flags"); field != header.end() && field->is_array()) {
            for (const auto& flag : *field) {
                if (flag.is_string()) document.header.flags.push_back(flag.get<std::string>());
            }
        }
        if (const auto field = header.find("imports"); field != header.end() && field->is_array()) {
            for (const auto& import : *field) {
                if (import.is_string()) document.header.imports.push_back(import.get<std::string>());
            }
        }
    }
    if (const auto it = root.find("variables"); it != root.end() && it->is_array()) {
        for (const auto& entry : *it) {
            if (entry.is_object()) document.variables.push_back(variableFromJson(entry));
        }
    }
    if (const auto it = root.find("structs"); it != root.end() && it->is_array()) {
        for (const auto& entry : *it) {
            if (entry.is_object()) document.structs.push_back(structFromJson(entry));
        }
    }
    if (const auto it = root.find("customEvents"); it != root.end() && it->is_array()) {
        for (const auto& entry : *it) {
            if (entry.is_string()) document.customEvents.push_back(entry.get<std::string>());
        }
    }
    if (const auto it = root.find("nodes"); it != root.end() && it->is_array()) {
        for (const auto& entry : *it) {
            if (entry.is_object()) document.nodes.push_back(nodeFromJson(entry));
        }
    }
    if (const auto it = root.find("wires"); it != root.end() && it->is_array()) {
        for (const auto& entry : *it) {
            if (entry.is_object()) document.wires.push_back(wireFromJson(entry));
        }
    }

    if (document.schema > GraphDocument::kCurrentSchema) {
        result.error = errorDiagnostic(
            std::string(codes::kUnsupportedSchema),
            "This graph was written at schema " + std::to_string(document.schema) +
                ", and this build reads up to " +
                std::to_string(GraphDocument::kCurrentSchema) +
                ". Update the tool to open it.");
        return result;
    }

    document.invalidate();
    result.document = std::move(document);
    return result;
}

GraphDocument deserialize(std::string_view json) {
    auto result = tryDeserialize(json);
    if (result.document) return std::move(*result.document);
    throw std::runtime_error(result.error->message);
}

GraphDocument loadFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Could not read '" + path.string() + "'.");
    const std::string text((std::istreambuf_iterator<char>(stream)),
                           std::istreambuf_iterator<char>());
    return deserialize(text);
}

void saveFile(const GraphDocument& document, const std::filesystem::path& path) {
    if (!path.parent_path().empty()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("Could not open '" + path.string() + "' for writing.");
    stream << serialize(document);
    if (!stream) throw std::runtime_error("Could not write '" + path.string() + "'.");
}

}

}
