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

#include <fo4/graph/graph_diagnostic.hpp>

namespace fo4::graph {

bool equalsIgnoreCase(std::string_view left, std::string_view right);
bool startsWithIgnoreCase(std::string_view value, std::string_view prefix);
int compareIgnoreCase(std::string_view left, std::string_view right);

struct PinRef {
    std::string node;
    std::string pin;

    bool isEmpty() const { return node.empty() || pin.empty(); }
    std::string toString() const { return node + ":" + pin; }

    bool operator==(const PinRef&) const = default;
};

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

std::string_view graphNodeKindName(GraphNodeKind kind);
GraphNodeKind graphNodeKindFromName(std::string_view name);

struct GraphPinValue {
    std::string type;
    std::string value;

    std::string toString() const { return type + ":" + value; }
};

struct PinCiLess {
    bool operator()(const std::string& left, const std::string& right) const {
        return compareIgnoreCase(left, right) < 0;
    }
};

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

struct GraphWire {
    std::string id;
    PinRef from;
    PinRef to;

    std::string toString() const { return from.toString() + " -> " + to.toString(); }
};

struct GraphVariable {
    std::string name;
    std::string type;
    bool isProperty{false};
    std::optional<std::string> initial;
    std::vector<std::string> flags;
};

struct GraphStruct {
    std::string name;
    std::vector<GraphVariable> members;
};

struct GraphScriptHeader {
    std::string scriptName;
    std::optional<std::string> extends;
    std::vector<std::string> flags;
    std::vector<std::string> imports;
    std::optional<std::string> docComment;
    std::optional<std::string> autoState;
};

enum class GraphKind { PapyrusScript = 0, F4SEBinding = 1 };

std::string_view graphKindName(GraphKind kind);
GraphKind graphKindFromName(std::string_view name);

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
}

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
}

namespace documentJson {

std::string serialize(const GraphDocument& document, bool indented = true);

struct LoadResult {
    std::optional<GraphDocument> document;
    std::optional<GraphDiagnostic> error;
};

LoadResult tryDeserialize(std::string_view json);

GraphDocument deserialize(std::string_view json);

GraphDocument loadFile(const std::filesystem::path& path);
void saveFile(const GraphDocument& document, const std::filesystem::path& path);

}

}
