#pragma once

#include <cctype>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include <fo4ide/graph/GraphDiagnostic.hpp>

namespace fo4ide::graph {

// Case helpers shared by the model and the JSON round-trip.
bool equalsIgnoreCase(std::string_view left, std::string_view right);
bool startsWithIgnoreCase(std::string_view value, std::string_view prefix);
int compareIgnoreCase(std::string_view left, std::string_view right);

// Port of PinRef.
struct PinRef {
    std::string node;
    std::string pin;

    bool isEmpty() const { return node.empty() || pin.empty(); }
    std::string toString() const { return node + ":" + pin; }

    bool operator==(const PinRef&) const = default;
};

// Port of GraphNodeKind, in the same order as the C# enum so names serialize
// identically (camelCase).
enum class GraphNodeKind {
    Unknown = 0,
    EventEntry,
    FunctionEntry,
    Return,
    Break,
    Continue,
    Branch,
    While,
    ForEach,
    Call,
    PropertyGet,
    PropertySet,
    VariableGet,
    VariableSet,
    LocalDeclare,
    Literal,
    Self,
    Parent,
    NoneValue,
    MemberGet,
    IndexGet,
    IndexSet,
    ArrayOp,
    NewArray,
    StructNew,
    StructGet,
    StructSet,
    Unary,
    Binary,
    Cast,
    TypeCheck,
    Reroute,
    Comment,
};

// camelCase names, matching the C# StringEnumConverter; unknown names decode
// as Unknown rather than failing (tolerant read of newer canvases).
std::string_view graphNodeKindName(GraphNodeKind kind);
GraphNodeKind graphNodeKindFromName(std::string_view name);

// Port of GraphPinValue.
struct GraphPinValue {
    std::string type;
    std::string value;

    std::string toString() const { return type + ":" + value; }
};

// Case-insensitive pin map, matching the C# OrdinalIgnoreCase dictionary.
struct PinCiLess {
    bool operator()(const std::string& left, const std::string& right) const {
        return compareIgnoreCase(left, right) < 0;
    }
};

// Port of GraphNode.
struct GraphNode {
    std::string id;
    std::string definition;
    GraphNodeKind kind{GraphNodeKind::Unknown};
    double x{0};
    double y{0};
    std::optional<std::string> comment;
    nlohmann::json config = nlohmann::json::object();
    std::map<std::string, GraphPinValue, PinCiLess> pinValues;

    std::optional<std::string> configString(const std::string& key) const {
        const auto it = config.find(key);
        if (it == config.end() || !it->is_string()) return std::nullopt;
        return it->get<std::string>();
    }

    std::string toString() const {
        return std::string(graphNodeKindName(kind)) + " " + definition + " (" + id + ")";
    }
};

// Port of GraphWire.
struct GraphWire {
    std::string id;
    PinRef from;
    PinRef to;

    std::string toString() const { return from.toString() + " -> " + to.toString(); }
};

// Port of GraphVariable.
struct GraphVariable {
    std::string name;
    std::string type;
    bool isProperty{false};
    std::optional<std::string> initial;
    std::vector<std::string> flags;
};

// Port of GraphStruct.
struct GraphStruct {
    std::string name;
    std::vector<GraphVariable> members;
};

// Port of GraphScriptHeader.
struct GraphScriptHeader {
    std::string scriptName;
    std::optional<std::string> extends;
    std::vector<std::string> flags;
    std::vector<std::string> imports;
    std::optional<std::string> docComment;
    std::optional<std::string> autoState;
};

// Port of GraphKind.
enum class GraphKind { PapyrusScript = 0, F4SEBinding = 1 };

std::string_view graphKindName(GraphKind kind);
GraphKind graphKindFromName(std::string_view name);

// Port of GraphDocument.
class GraphDocument {
public:
    static constexpr int kCurrentSchema = 1;

    int schema{kCurrentSchema};
    std::string id;
    GraphKind kind{GraphKind::PapyrusScript};
    GraphScriptHeader header;
    std::vector<GraphVariable> variables;
    std::vector<GraphStruct> structs;
    std::vector<std::string> customEvents;
    std::vector<GraphNode> nodes;
    std::vector<GraphWire> wires;

    // First node with the id wins, mirroring the C# GroupBy-then-First lookup.
    const GraphNode* node(const std::string& id) const;
    GraphNode* node(const std::string& id);
    void invalidate();

    std::vector<const GraphWire*> into(const PinRef& pin) const;
    std::vector<const GraphWire*> outOf(const PinRef& pin) const;
    std::vector<const GraphWire*> touching(const std::string& nodeId) const;

private:
    void buildIndex() const;

    mutable std::unordered_map<std::string, const GraphNode*> byId_;
};

// Port of PinIds: the canonical pin ids plus argument/parameter helpers.
namespace pinIds {
inline constexpr std::string_view kExec = "exec";
inline constexpr std::string_view kThen = "then";
inline constexpr std::string_view kElse = "else";
inline constexpr std::string_view kBody = "body";
inline constexpr std::string_view kCompleted = "completed";
inline constexpr std::string_view kSelf = "self";
inline constexpr std::string_view kReturn = "ret";
inline constexpr std::string_view kValue = "value";
inline constexpr std::string_view kTarget = "target";
inline constexpr std::string_view kCondition = "cond";
inline constexpr std::string_view kIndex = "index";
inline constexpr std::string_view kArray = "array";
inline constexpr std::string_view kElement = "element";
inline constexpr std::string_view kLeft = "a";
inline constexpr std::string_view kRight = "b";

inline constexpr std::string_view kArgumentPrefix = "arg:";
inline constexpr std::string_view kParameterPrefix = "param:";

inline std::string argument(const std::string& parameterName) {
    return std::string(kArgumentPrefix) + parameterName;
}
inline std::string parameter(const std::string& parameterName) {
    return std::string(kParameterPrefix) + parameterName;
}
inline bool isArgument(std::string_view pinId) {
    return startsWithIgnoreCase(pinId, kArgumentPrefix);
}
inline std::string argumentName(std::string_view pinId) {
    return isArgument(pinId) ? std::string(pinId.substr(kArgumentPrefix.size()))
                             : std::string(pinId);
}
}  // namespace pinIds

// Port of the BuiltinNodeDefinitions id constants.
namespace builtinDefs {
inline constexpr std::string_view kBranch = "branch";
inline constexpr std::string_view kWhile = "while";
inline constexpr std::string_view kForEach = "foreach";
inline constexpr std::string_view kReturn = "return";
inline constexpr std::string_view kBreak = "break";
inline constexpr std::string_view kContinue = "continue";
inline constexpr std::string_view kReroute = "reroute";
inline constexpr std::string_view kSelf = "self";
inline constexpr std::string_view kParent = "parent";
inline constexpr std::string_view kNoneValue = "none";
inline constexpr std::string_view kCast = "cast";
inline constexpr std::string_view kTypeCheck = "is";
inline constexpr std::string_view kNewArray = "new.array";
inline constexpr std::string_view kIndexGet = "index.get";
inline constexpr std::string_view kIndexSet = "index.set";
inline constexpr std::string_view kVariableGet = "var.get";
inline constexpr std::string_view kVariableSet = "var.set";
inline constexpr std::string_view kLocalDeclare = "local.declare";
inline constexpr std::string_view kFunctionEntry = "function.entry";

inline constexpr std::string_view kLiteralPrefix = "literal.";
inline constexpr std::string_view kOperatorPrefix = "op.";
inline constexpr std::string_view kArrayPrefix = "array.";
}  // namespace builtinDefs

// Port of GraphDocumentJson: camelCase serialization with a short "def" key,
// nulls omitted, unknown fields and node kinds tolerated on read, and a
// future schema refused with GRA0001.
namespace documentJson {

std::string serialize(const GraphDocument& document, bool indented = true);

struct LoadResult {
    std::optional<GraphDocument> document;
    std::optional<GraphDiagnostic> error;
};

LoadResult tryDeserialize(std::string_view json);

// Throws std::runtime_error with the diagnostic message on failure.
GraphDocument deserialize(std::string_view json);

GraphDocument loadFile(const std::filesystem::path& path);
void saveFile(const GraphDocument& document, const std::filesystem::path& path);

}  // namespace documentJson

}  // namespace fo4ide::graph
