#include <fo4/graph/graph_ir_json.hpp>

#include <memory>
#include <optional>
#include <vector>

#include <nlohmann/json.hpp>

namespace fo4::graph::irJson {

namespace {

using ojson = nlohmann::ordered_json;

ojson expression(const std::shared_ptr<ir::IrExpression>& value);
ojson statements(const std::vector<std::shared_ptr<ir::IrStatement>>& body);

void origin(ojson& out, const ir::IrNode& node) {
    if (node.nodeId) out["nodeId"] = *node.nodeId;
    if (node.pinId) out["pinId"] = *node.pinId;
}

void optionalText(ojson& out, const char* key, const std::optional<std::string>& value) {
    if (value) out[key] = *value;
}

void child(ojson& out, const char* key, const std::shared_ptr<ir::IrExpression>& value) {
    if (value) out[key] = expression(value);
}

const char* expressionKind(const ir::IrExpression& raw) {
    if (dynamic_cast<const ir::IrLiteral*>(&raw)) return "literal";
    if (dynamic_cast<const ir::IrName*>(&raw)) return "name";
    if (dynamic_cast<const ir::IrSelf*>(&raw)) return "self";
    if (dynamic_cast<const ir::IrParent*>(&raw)) return "parent";
    if (dynamic_cast<const ir::IrMember*>(&raw)) return "member";
    if (dynamic_cast<const ir::IrIndex*>(&raw)) return "index";
    if (dynamic_cast<const ir::IrCall*>(&raw)) return "call";
    if (dynamic_cast<const ir::IrUnary*>(&raw)) return "unary";
    if (dynamic_cast<const ir::IrBinary*>(&raw)) return "binary";
    if (dynamic_cast<const ir::IrCast*>(&raw)) return "cast";
    if (dynamic_cast<const ir::IrTypeCheck*>(&raw)) return "typeCheck";
    if (dynamic_cast<const ir::IrNewArray*>(&raw)) return "newArray";
    if (dynamic_cast<const ir::IrNewStruct*>(&raw)) return "newStruct";
    return "unknown";
}

ojson expression(const std::shared_ptr<ir::IrExpression>& value) {
    if (!value) return ojson(nullptr);
    const auto& raw = *value;
    ojson out = ojson::object();
    out["kind"] = expressionKind(raw);
    origin(out, raw);
    out["typeName"] = raw.typeName;
    out["isArray"] = raw.isArray;

    if (const auto* pLiteral = dynamic_cast<const ir::IrLiteral*>(&raw)) {
        out["text"] = pLiteral->text;
    } else if (const auto* pName = dynamic_cast<const ir::IrName*>(&raw)) {
        out["name"] = pName->name;
    } else if (const auto* pMember = dynamic_cast<const ir::IrMember*>(&raw)) {
        child(out, "target", pMember->target);
        out["name"] = pMember->name;
    } else if (const auto* pIndex = dynamic_cast<const ir::IrIndex*>(&raw)) {
        child(out, "target", pIndex->target);
        child(out, "index", pIndex->index);
    } else if (const auto* pCall = dynamic_cast<const ir::IrCall*>(&raw)) {
        child(out, "receiver", pCall->receiver);
        out["name"] = pCall->name;
        out["isGlobal"] = pCall->isGlobal;
        ojson arguments = ojson::array();
        for (const auto& argument : pCall->arguments) {
            ojson item = ojson::object();
            child(item, "value", argument.value);
            optionalText(item, "name", argument.name);
            arguments.push_back(std::move(item));
        }
        out["arguments"] = std::move(arguments);
    } else if (const auto* pUnary = dynamic_cast<const ir::IrUnary*>(&raw)) {
        out["op"] = pUnary->op;
        child(out, "operand", pUnary->operand);
    } else if (const auto* pBinary = dynamic_cast<const ir::IrBinary*>(&raw)) {
        out["op"] = pBinary->op;
        child(out, "left", pBinary->left);
        child(out, "right", pBinary->right);
    } else if (const auto* pCast = dynamic_cast<const ir::IrCast*>(&raw)) {
        child(out, "value", pCast->value);
        out["targetType"] = pCast->targetType;
        out["targetIsArray"] = pCast->targetIsArray;
    } else if (const auto* pTypeCheck = dynamic_cast<const ir::IrTypeCheck*>(&raw)) {
        child(out, "value", pTypeCheck->value);
        out["targetType"] = pTypeCheck->targetType;
        out["targetIsArray"] = pTypeCheck->targetIsArray;
    } else if (const auto* pNewArray = dynamic_cast<const ir::IrNewArray*>(&raw)) {
        out["elementType"] = pNewArray->elementType;
        child(out, "size", pNewArray->size);
    } else if (const auto* pNewStruct = dynamic_cast<const ir::IrNewStruct*>(&raw)) {
        out["structName"] = pNewStruct->structName;
    }
    return out;
}

ojson statement(const std::shared_ptr<ir::IrStatement>& value) {
    if (!value) return ojson(nullptr);
    const auto& raw = *value;
    ojson out = ojson::object();
    if (const auto* pDefine = dynamic_cast<const ir::IrDefine*>(&raw)) {
        out["kind"] = "define";
        origin(out, raw);
        out["name"] = pDefine->name;
        out["typeName"] = pDefine->typeName;
        out["isArray"] = pDefine->isArray;
        child(out, "value", pDefine->value);
    } else if (const auto* pAssign = dynamic_cast<const ir::IrAssign*>(&raw)) {
        out["kind"] = "assign";
        origin(out, raw);
        child(out, "target", pAssign->target);
        child(out, "value", pAssign->value);
    } else if (const auto* pExpressionStatement = dynamic_cast<const ir::IrExpressionStatement*>(&raw)) {
        out["kind"] = "expression";
        origin(out, raw);
        child(out, "expression", pExpressionStatement->expression);
    } else if (const auto* pReturn = dynamic_cast<const ir::IrReturn*>(&raw)) {
        out["kind"] = "return";
        origin(out, raw);
        child(out, "value", pReturn->value);
    } else if (const auto* pIf = dynamic_cast<const ir::IrIf*>(&raw)) {
        out["kind"] = "if";
        origin(out, raw);
        ojson branches = ojson::array();
        for (const auto& branch : pIf->branches) {
            ojson item = ojson::object();
            child(item, "condition", branch.condition);
            item["body"] = statements(branch.body);
            branches.push_back(std::move(item));
        }
        out["branches"] = std::move(branches);
        out["elseBody"] = statements(pIf->elseBody);
    } else if (const auto* pWhile = dynamic_cast<const ir::IrWhile*>(&raw)) {
        out["kind"] = "while";
        origin(out, raw);
        child(out, "condition", pWhile->condition);
        out["body"] = statements(pWhile->body);
    } else {
        out["kind"] = "unknown";
        origin(out, raw);
    }
    return out;
}

ojson statements(const std::vector<std::shared_ptr<ir::IrStatement>>& body) {
    ojson out = ojson::array();
    for (const auto& item : body) out.push_back(statement(item));
    return out;
}

template <typename Item>
ojson typedNames(const std::vector<Item>& items) {
    ojson out = ojson::array();
    for (const auto& item : items) {
        ojson row = ojson::object();
        row["name"] = item.name;
        row["typeName"] = item.typeName;
        row["isArray"] = item.isArray;
        out.push_back(std::move(row));
    }
    return out;
}

template <typename Item>
ojson declarations(const std::vector<Item>& items) {
    ojson out = ojson::array();
    for (const auto& item : items) {
        ojson row = ojson::object();
        row["name"] = item.name;
        row["typeName"] = item.typeName;
        row["isArray"] = item.isArray;
        row["flags"] = item.flags;
        optionalText(row, "initialText", item.initialText);
        out.push_back(std::move(row));
    }
    return out;
}

ojson callable(const ir::IrCallable& value) {
    ojson out = ojson::object();
    out["name"] = value.name;
    out["entryNodeId"] = value.entryNodeId;
    out["isEvent"] = value.isEvent;
    out["isGlobal"] = value.isGlobal;
    optionalText(out, "remoteObjectType", value.remoteObjectType);
    optionalText(out, "stateName", value.stateName);
    optionalText(out, "returnTypeName", value.returnTypeName);
    out["returnIsArray"] = value.returnIsArray;
    ojson parameters = ojson::array();
    for (const auto& parameter : value.parameters) {
        ojson row = ojson::object();
        row["name"] = parameter.name;
        row["typeName"] = parameter.typeName;
        row["isArray"] = parameter.isArray;
        optionalText(row, "defaultText", parameter.defaultText);
        parameters.push_back(std::move(row));
    }
    out["parameters"] = std::move(parameters);
    out["locals"] = typedNames(value.locals);
    out["body"] = statements(value.body);
    return out;
}

}

std::string serialize(const ir::IrScript& script, bool indented) {
    ojson out = ojson::object();
    out["name"] = script.name;
    optionalText(out, "extends", script.extends);
    out["flags"] = script.flags;
    out["imports"] = script.imports;
    optionalText(out, "docComment", script.docComment);

    ojson structs = ojson::array();
    for (const auto& item : script.structs) {
        ojson row = ojson::object();
        row["name"] = item.name;
        row["members"] = typedNames(item.members);
        structs.push_back(std::move(row));
    }
    out["structs"] = std::move(structs);
    out["variables"] = declarations(script.variables);
    out["properties"] = declarations(script.properties);

    ojson callables = ojson::array();
    for (const auto& item : script.callables) callables.push_back(callable(item));
    out["callables"] = std::move(callables);

    optionalText(out, "autoState", script.autoState);
    out["customEvents"] = script.customEvents;
    return out.dump(indented ? 2 : -1, ' ', false, ojson::error_handler_t::replace);
}

}
