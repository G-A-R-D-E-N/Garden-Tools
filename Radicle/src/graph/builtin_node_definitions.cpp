#include <fo4/graph/builtin_node_definitions.hpp>

namespace fo4::graph {
namespace builtinDefinitions {
namespace {

PinDefinition execIn() {
    return PinDefinition{std::string(pinIds::kExec), "", PinDirection::In, PinKind::Exec,
                         std::nullopt, false, std::nullopt, std::nullopt};
}

PinDefinition execOut(std::string_view id, std::string_view label) {
    return PinDefinition{std::string(id), std::string(label), PinDirection::Out, PinKind::Exec,
                         std::nullopt, false, std::nullopt, std::nullopt};
}

PinDefinition dataIn(std::string_view id, std::string_view label, PinTypeExpr type,
                     bool optional = false) {
    return PinDefinition{std::string(id), std::string(label), PinDirection::In, PinKind::Data,
                         std::move(type), optional, std::nullopt, std::nullopt};
}

PinDefinition dataOut(std::string_view id, std::string_view label, PinTypeExpr type) {
    return PinDefinition{std::string(id), std::string(label), PinDirection::Out, PinKind::Data,
                         std::move(type), false, std::nullopt, std::nullopt};
}

NodeDefinition make(std::string id, GraphNodeKind kind, std::string title, std::string category,
                    std::vector<PinDefinition> pins, std::optional<std::string> summary = std::nullopt,
                    bool isPure = false, std::optional<std::string> localNameHint = std::nullopt,
                    std::optional<std::string> memberName = std::nullopt) {
    NodeDefinition definition;
    definition.id = std::move(id);
    definition.kind = kind;
    definition.title = std::move(title);
    definition.category = std::move(category);
    definition.summary = std::move(summary);
    definition.isPure = isPure;
    definition.localNameHint = std::move(localNameHint);
    definition.memberName = std::move(memberName);
    definition.pins = std::move(pins);
    return definition;
}

struct ArrayMember {
    std::string_view name;
    std::string_view result;
    bool pure;
};

constexpr ArrayMember kArrayMembers[] = {
    {"Length", "int", true},
    {"Find", "int", false},
    {"RFind", "int", false},
    {"FindStruct", "int", false},
    {"RFindStruct", "int", false},
    {"Add", "None", false},
    {"Insert", "None", false},
    {"Remove", "None", false},
    {"RemoveLast", "None", false},
    {"Clear", "None", false},
    {"GetMatchingStructs", "array", false},
};

struct BinaryOperator {
    std::string_view id;
    std::string_view title;
    std::string_view token;
    std::string_view result;
};

constexpr BinaryOperator kBinaryOperators[] = {
    {"add", "Add", "+", ""},
    {"sub", "Subtract", "-", ""},
    {"mul", "Multiply", "*", ""},
    {"div", "Divide", "/", ""},
    {"mod", "Modulo", "%", ""},
    {"eq", "Equal", "==", "bool"},
    {"ne", "Not Equal", "!=", "bool"},
    {"lt", "Less Than", "<", "bool"},
    {"le", "Less Or Equal", "<=", "bool"},
    {"gt", "Greater Than", ">", "bool"},
    {"ge", "Greater Or Equal", ">=", "bool"},
    {"and", "And", "&&", "bool"},
    {"or", "Or", "||", "bool"},
};

struct UnaryOperator {
    std::string_view id;
    std::string_view title;
    std::string_view token;
    std::string_view type;
};

constexpr UnaryOperator kUnaryOperators[] = {
    {"not", "Not", "!", "bool"},
    {"neg", "Negate", "-", ""},
};

struct Literal {
    std::string_view type;
};

constexpr Literal kLiterals[] = {
    {"int"},
    {"float"},
    {"bool"},
    {"string"},
};

std::vector<NodeDefinition> build() {
    std::vector<NodeDefinition> all;

    all.push_back(make(std::string(builtinDefs::kBranch), GraphNodeKind::Branch, "Branch", "Flow",
                       {execIn(),
                        dataIn(pinIds::kCondition, "Condition", PinTypeExpr::concrete("bool")),
                        execOut(pinIds::kThen, "True"), execOut(pinIds::kElse, "False")},
                       "Runs one of two paths depending on a condition."));

    all.push_back(make(std::string(builtinDefs::kWhile), GraphNodeKind::While, "While", "Flow",
                       {execIn(),
                        dataIn(pinIds::kCondition, "Condition", PinTypeExpr::concrete("bool")),
                        execOut(pinIds::kBody, "Body"), execOut(pinIds::kCompleted, "Completed")},
                       "Repeats the body while the condition holds."));

    all.push_back(make(std::string(builtinDefs::kForEach), GraphNodeKind::ForEach, "For Each", "Flow",
                       {execIn(), dataIn(pinIds::kArray, "Array", PinTypeExpr::arrayOfGeneric()),
                        execOut(pinIds::kBody, "Body"),
                        dataOut(pinIds::kElement, "Element", PinTypeExpr::elementOfGeneric()),
                        dataOut(pinIds::kIndex, "Index", PinTypeExpr::concrete("int")),
                        execOut(pinIds::kCompleted, "Completed")},
                       "Walks an array. Lowered to a While loop over an index."));

    all.push_back(make(std::string(builtinDefs::kReturn), GraphNodeKind::Return, "Return", "Flow",
                       {execIn(), dataIn(pinIds::kValue, "Value", PinTypeExpr::any(), true)},
                       "Leaves the function, optionally with a value."));

    all.push_back(make(std::string(builtinDefs::kLocalDeclare), GraphNodeKind::LocalDeclare, "Local",
                       "Flow",
                       {execIn(), dataIn(pinIds::kValue, "Value", PinTypeExpr::any(), true),
                        execOut(pinIds::kThen, "")},
                       "Declares a variable that lives for one call of this function."));

    all.push_back(make(std::string(builtinDefs::kBreak), GraphNodeKind::Break, "Break", "Flow",
                       {execIn()}, "Leaves the enclosing loop."));

    all.push_back(make(std::string(builtinDefs::kContinue), GraphNodeKind::Continue, "Continue", "Flow",
                       {execIn()}, "Skips the rest of this pass and tests the loop again."));

    {
        auto reroute = make(std::string(builtinDefs::kReroute), GraphNodeKind::Reroute, "Reroute",
                            "Flow",
                            {dataIn(pinIds::kValue, "", PinTypeExpr::generic()),
                             dataOut(std::string(pinIds::kValue) + ".out", "", PinTypeExpr::generic())},
                            "Tidies a wire. Emits nothing.", true);
        all.push_back(std::move(reroute));
    }

    all.push_back(make(std::string(builtinDefs::kSelf), GraphNodeKind::Self, "Self", "Values",
                       {dataOut(pinIds::kValue, "Self", PinTypeExpr::selfType())},
                       std::nullopt, true, "self"));

    all.push_back(make(std::string(builtinDefs::kParent), GraphNodeKind::Parent, "Parent", "Values",
                       {dataOut(pinIds::kValue, "Parent", PinTypeExpr::selfType())},
                       std::nullopt, true));

    all.push_back(make(std::string(builtinDefs::kNoneValue), GraphNodeKind::NoneValue, "None", "Values",
                       {dataOut(pinIds::kValue, "None", PinTypeExpr::concrete("None"))},
                       std::nullopt, true));

    all.push_back(make(std::string(builtinDefs::kCast), GraphNodeKind::Cast, "Cast", "Values",
                       {dataIn(pinIds::kValue, "Value", PinTypeExpr::any()),
                        dataOut(pinIds::kReturn, "Result", PinTypeExpr::any())},
                       "Converts a value to another type with 'as'.", true, "cast"));

    all.push_back(make(std::string(builtinDefs::kTypeCheck), GraphNodeKind::TypeCheck, "Is", "Values",
                       {dataIn(pinIds::kValue, "Value", PinTypeExpr::any()),
                        dataOut(pinIds::kReturn, "Result", PinTypeExpr::concrete("bool"))},
                       "Tests whether a value is of a type.", true, "isType"));

    all.push_back(make(std::string(builtinDefs::kNewArray), GraphNodeKind::NewArray, "New Array",
                       "Array",
                       {dataIn(pinIds::kIndex, "Size", PinTypeExpr::concrete("int")),
                        dataOut(pinIds::kReturn, "Array", PinTypeExpr::arrayOfGeneric())},
                       std::nullopt, true, "array"));

    all.push_back(make(std::string(builtinDefs::kIndexGet), GraphNodeKind::IndexGet, "Get At", "Array",
                       {dataIn(pinIds::kArray, "Array", PinTypeExpr::arrayOfGeneric()),
                        dataIn(pinIds::kIndex, "Index", PinTypeExpr::concrete("int")),
                        dataOut(pinIds::kReturn, "Value", PinTypeExpr::elementOfGeneric())},
                       std::nullopt, true, "item"));

    all.push_back(make(std::string(builtinDefs::kIndexSet), GraphNodeKind::IndexSet, "Set At", "Array",
                       {execIn(), dataIn(pinIds::kArray, "Array", PinTypeExpr::arrayOfGeneric()),
                        dataIn(pinIds::kIndex, "Index", PinTypeExpr::concrete("int")),
                        dataIn(pinIds::kValue, "Value", PinTypeExpr::elementOfGeneric()),
                        execOut(pinIds::kThen, "")}));

    all.push_back(make(std::string(builtinDefs::kVariableGet), GraphNodeKind::VariableGet,
                       "Get Variable", "Variables",
                       {dataOut(pinIds::kValue, "Value", PinTypeExpr::any())},
                       std::nullopt, true));

    all.push_back(make(std::string(builtinDefs::kVariableSet), GraphNodeKind::VariableSet,
                       "Set Variable", "Variables",
                       {execIn(), dataIn(pinIds::kValue, "Value", PinTypeExpr::any()),
                        execOut(pinIds::kThen, "")}));

    all.push_back(make(std::string(builtinDefs::kFunctionEntry), GraphNodeKind::FunctionEntry,
                       "Function", "Flow", {execOut(pinIds::kExec, "")},
                       "Declares a function this script provides."));

    for (const auto& literal : kLiterals) {
        const std::string id = std::string(builtinDefs::kLiteralPrefix) + std::string(literal.type);
        all.push_back(make(id, GraphNodeKind::Literal, std::string(literal.type), "Values",
                           {dataOut(pinIds::kValue, "Value",
                                    PinTypeExpr::concrete(std::string(literal.type)))},
                           std::nullopt, true, std::string(literal.type)));
    }

    for (const auto& op : kBinaryOperators) {
        const std::string id = std::string(builtinDefs::kOperatorPrefix) + std::string(op.id);
        std::vector<PinDefinition> pins{
            dataIn(pinIds::kLeft, "A", PinTypeExpr::generic()),
            dataIn(pinIds::kRight, "B", PinTypeExpr::generic()),
        };
        pins.push_back(dataOut(pinIds::kReturn, "Result",
                               op.result.empty() ? PinTypeExpr::generic()
                                                 : PinTypeExpr::concrete(std::string(op.result))));
        all.push_back(make(id, GraphNodeKind::Binary, std::string(op.title), "Math", std::move(pins),
                           std::nullopt, true, std::string(op.id)));
    }

    for (const auto& op : kUnaryOperators) {
        const std::string id = std::string(builtinDefs::kOperatorPrefix) + std::string(op.id);
        std::vector<PinDefinition> pins{
            dataIn(pinIds::kValue, "Value",
                   op.type.empty() ? PinTypeExpr::generic()
                                   : PinTypeExpr::concrete(std::string(op.type))),
            dataOut(pinIds::kReturn, "Result",
                    op.type.empty() ? PinTypeExpr::generic()
                                    : PinTypeExpr::concrete(std::string(op.type))),
        };
        all.push_back(make(id, GraphNodeKind::Unary, std::string(op.title), "Math", std::move(pins),
                           std::nullopt, true, std::string(op.id)));
    }

    for (const auto& member : kArrayMembers) {
        std::vector<PinDefinition> pins;
        if (!member.pure) pins.push_back(execIn());
        pins.push_back(dataIn(pinIds::kArray, "Array", PinTypeExpr::arrayOfGeneric()));

        const auto name = member.name;
        if (name == "Find" || name == "RFind") {
            pins.push_back(dataIn(pinIds::kValue, "Value", PinTypeExpr::elementOfGeneric()));
            pins.push_back(dataIn(pinIds::kIndex, "Start Index", PinTypeExpr::concrete("int"),
                                  true));
        } else if (name == "FindStruct" || name == "RFindStruct" || name == "GetMatchingStructs") {
            pins.push_back(dataIn("member", "Member Name", PinTypeExpr::concrete("string")));
            pins.push_back(dataIn(pinIds::kValue, "Value", PinTypeExpr::any()));
            pins.push_back(dataIn(pinIds::kIndex, "Start Index", PinTypeExpr::concrete("int"),
                                  true));
        } else if (name == "Add" || name == "Insert") {
            pins.push_back(dataIn(pinIds::kElement, "Element", PinTypeExpr::elementOfGeneric()));
            pins.push_back(dataIn(pinIds::kIndex, "Index", PinTypeExpr::concrete("int"),
                                  true));
        } else if (name == "Remove") {
            pins.push_back(dataIn(pinIds::kIndex, "Index", PinTypeExpr::concrete("int")));
            pins.push_back(dataIn("count", "Count", PinTypeExpr::concrete("int"), true));
        }

        if (member.result == "array") {
            pins.push_back(dataOut(pinIds::kReturn, "Result", PinTypeExpr::arrayOfGeneric()));
        } else if (member.result != "None") {
            pins.push_back(
                dataOut(pinIds::kReturn, "Result", PinTypeExpr::concrete(std::string(member.result))));
        }

        if (!member.pure) pins.push_back(execOut(pinIds::kThen, ""));

        std::string id = std::string(builtinDefs::kArrayPrefix);
        for (const char c : member.name) id.push_back(static_cast<char>(std::tolower(c)));
        std::string lowerName;
        for (const char c : member.name) lowerName.push_back(static_cast<char>(std::tolower(c)));
        all.push_back(make(id, GraphNodeKind::ArrayOp, std::string(member.name), "Array",
                           std::move(pins), std::nullopt, member.pure, lowerName,
                           std::string(member.name)));
    }

    return all;
}

}

const std::vector<NodeDefinition>& all() {
    static const std::vector<NodeDefinition> catalog = build();
    return catalog;
}

const NodeDefinition* find(std::string_view id) {
    if (id.empty()) return nullptr;
    for (const auto& definition : all()) {
        if (equalsIgnoreCase(definition.id, id)) return &definition;
    }
    return nullptr;
}

std::optional<std::string_view> operatorToken(std::string_view definitionId) {
    const std::string_view prefix = builtinDefs::kOperatorPrefix;
    if (definitionId.size() <= prefix.size() ||
        !startsWithIgnoreCase(definitionId, prefix)) {
        return std::nullopt;
    }
    const auto suffix = definitionId.substr(prefix.size());
    for (const auto& op : kBinaryOperators) {
        if (equalsIgnoreCase(op.id, suffix)) return op.token;
    }
    for (const auto& op : kUnaryOperators) {
        if (equalsIgnoreCase(op.id, suffix)) return op.token;
    }
    return std::nullopt;
}

std::optional<std::string_view> arrayMemberName(std::string_view definitionId) {
    const std::string_view prefix = builtinDefs::kArrayPrefix;
    if (definitionId.size() <= prefix.size() ||
        !startsWithIgnoreCase(definitionId, prefix)) {
        return std::nullopt;
    }
    const auto suffix = definitionId.substr(prefix.size());
    for (const auto& member : kArrayMembers) {
        if (equalsIgnoreCase(member.name, suffix)) return member.name;
    }
    return std::nullopt;
}

const std::vector<std::string>& arrayMemberNames() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> out;
        out.reserve(std::size(kArrayMembers));
        for (const auto& member : kArrayMembers) out.emplace_back(member.name);
        return out;
    }();
    return names;
}

}
}
