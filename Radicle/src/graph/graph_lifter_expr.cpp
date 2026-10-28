#include <fo4/graph/graph_lifter.hpp>

#include <cctype>
#include <utility>

#include <fo4/papyrus/resolver.hpp>
#include "graph_lifter_detail.hpp"

namespace fo4::graph {

using namespace lifter_detail;

std::optional<GraphLifter::Value> GraphLifter::liftExpression(const papyrus::Expression& expression,
                                                              std::vector<PinRef>& open) {
    if (const auto* literal = dynamic_cast<const papyrus::LiteralExpression*>(&expression)) {
        if (literal->kind == papyrus::LiteralKind::None) {
            const auto id = node(builtinDefs::kNoneValue, GraphNodeKind::NoneValue);
            Value value;
            value.pin = PinRef{id, std::string(pinIds::kValue)};
            return value;
        }

        Value value;
        value.literal = GraphPinValue{literalType(*literal), literalText(*literal)};
        return value;
    }

    if (const auto* name = dynamic_cast<const papyrus::IdentifierExpression*>(&expression)) {
        return liftIdentifier(*name);
    }

    if (const auto* member = dynamic_cast<const papyrus::MemberExpression*>(&expression)) {
        return liftMember(*member, open);
    }

    if (const auto* index = dynamic_cast<const papyrus::IndexExpression*>(&expression)) {
        const auto target = liftExpression(*index->target, open);
        const auto at = liftExpression(*index->index, open);
        const auto id = node(builtinDefs::kIndexGet, GraphNodeKind::IndexGet);
        if (target && target->pin) wire(*target->pin, PinRef{id, std::string(pinIds::kArray)});
        else if (target && target->literal) doc_->node(id)->pinValues[std::string(pinIds::kArray)] = *target->literal;
        if (at && at->pin) wire(*at->pin, PinRef{id, std::string(pinIds::kIndex)});
        else if (at && at->literal) doc_->node(id)->pinValues[std::string(pinIds::kIndex)] = *at->literal;
        Value value;
        value.pin = PinRef{id, std::string(pinIds::kReturn)};
        return value;
    }

    if (const auto* call = dynamic_cast<const papyrus::CallExpression*>(&expression)) {
        return liftCall(*call, open);
    }

    if (const auto* unary = dynamic_cast<const papyrus::UnaryExpression*>(&expression)) {
        std::optional<std::string> idText;
        switch (unary->op) {
            case papyrus::TokenKind::Not: idText = "not"; break;
            case papyrus::TokenKind::Minus: idText = "neg"; break;
            default: break;
        }
        if (!idText) {
            refuse("Unary operator '" + std::string(papyrus::tokenKindName(unary->op)) +
                       "' has no graph node.",
                   unary->span);
            return std::nullopt;
        }

        const auto operand = liftExpression(*unary->operand, open);
        const auto id = node(std::string(builtinDefs::kOperatorPrefix) + *idText,
                             GraphNodeKind::Unary);
        if (operand && operand->pin) wire(*operand->pin, PinRef{id, std::string(pinIds::kValue)});
        else if (operand && operand->literal) doc_->node(id)->pinValues[std::string(pinIds::kValue)] = *operand->literal;
        Value value;
        value.pin = PinRef{id, std::string(pinIds::kReturn)};
        return value;
    }

    if (const auto* binary = dynamic_cast<const papyrus::BinaryExpression*>(&expression)) {
        const auto idText = binaryId(binary->op);
        if (!idText) {
            refuse("Binary operator '" + std::string(papyrus::tokenKindName(binary->op)) +
                       "' has no graph node.",
                   binary->span);
            return std::nullopt;
        }

        const auto left = liftExpression(*binary->left, open);
        const auto right = liftExpression(*binary->right, open);
        const auto id = node(std::string(builtinDefs::kOperatorPrefix) + *idText,
                             GraphNodeKind::Binary);
        if (left && left->pin) wire(*left->pin, PinRef{id, std::string(pinIds::kLeft)});
        else if (left && left->literal) doc_->node(id)->pinValues[std::string(pinIds::kLeft)] = *left->literal;
        if (right && right->pin) wire(*right->pin, PinRef{id, std::string(pinIds::kRight)});
        else if (right && right->literal) doc_->node(id)->pinValues[std::string(pinIds::kRight)] = *right->literal;
        Value value;
        value.pin = PinRef{id, std::string(pinIds::kReturn)};
        return value;
    }

    if (const auto* cast = dynamic_cast<const papyrus::CastExpression*>(&expression)) {
        const auto operand = liftExpression(*cast->operand, open);
        const auto id = node(builtinDefs::kCast, GraphNodeKind::Cast);
        config(id, "type", cast->type.toString());
        if (operand && operand->pin) wire(*operand->pin, PinRef{id, std::string(pinIds::kValue)});
        else if (operand && operand->literal) doc_->node(id)->pinValues[std::string(pinIds::kValue)] = *operand->literal;
        Value value;
        value.pin = PinRef{id, std::string(pinIds::kReturn)};
        return value;
    }

    if (const auto* check = dynamic_cast<const papyrus::TypeCheckExpression*>(&expression)) {
        const auto operand = liftExpression(*check->operand, open);
        const auto id = node(builtinDefs::kTypeCheck, GraphNodeKind::TypeCheck);
        config(id, "type", check->type.toString());
        if (operand && operand->pin) wire(*operand->pin, PinRef{id, std::string(pinIds::kValue)});
        else if (operand && operand->literal) doc_->node(id)->pinValues[std::string(pinIds::kValue)] = *operand->literal;
        Value value;
        value.pin = PinRef{id, std::string(pinIds::kReturn)};
        return value;
    }

    if (const auto* created = dynamic_cast<const papyrus::NewArrayExpression*>(&expression)) {
        const auto size = liftExpression(*created->size, open);
        const auto id = node(builtinDefs::kNewArray, GraphNodeKind::NewArray);
        config(id, "type", created->elementType.toString());
        if (size && size->pin) wire(*size->pin, PinRef{id, std::string(pinIds::kValue)});
        else if (size && size->literal) doc_->node(id)->pinValues[std::string(pinIds::kValue)] = *size->literal;
        Value value;
        value.pin = PinRef{id, std::string(pinIds::kReturn)};
        return value;
    }

    if (const auto* created = dynamic_cast<const papyrus::NewStructExpression*>(&expression)) {
        const auto id = node(builtinDefs::kStructNew, GraphNodeKind::StructNew);
        config(id, "type", created->type.toString());
        Value value;
        value.pin = PinRef{id, std::string(pinIds::kReturn)};
        return value;
    }

    refuse("'" + nodeKindName(expression.nodeKind()) +
               "' is an expression the graph has no node for.",
           expression.span);
    return std::nullopt;
}

std::optional<GraphLifter::Value> GraphLifter::liftIdentifier(
    const papyrus::IdentifierExpression& name) {
    const auto* binding = resolution_->bindingFor(&name);
    if (binding == nullptr) {
        refuse("'" + name.name + "' could not be resolved to something the graph can read.",
               name.span);
        return std::nullopt;
    }

    switch (binding->kind) {
        case papyrus::BindingKind::SelfKeyword: {
            const auto id = node(builtinDefs::kSelf, GraphNodeKind::Self);
            Value value;
            value.pin = PinRef{id, std::string(pinIds::kValue)};
            return value;
        }
        case papyrus::BindingKind::ParentKeyword: {
            const auto id = node(builtinDefs::kParent, GraphNodeKind::Parent);
            Value value;
            value.pin = PinRef{id, std::string(pinIds::kValue)};
            return value;
        }
        case papyrus::BindingKind::Local:
        case papyrus::BindingKind::Parameter:
        case papyrus::BindingKind::ScriptVariable:
        case papyrus::BindingKind::Property: {
            const auto id = node(builtinDefs::kVariableGet, GraphNodeKind::VariableGet);
            config(id, "name", name.name);
            config(id, "type", typeName(name));
            Value value;
            value.pin = PinRef{id, std::string(pinIds::kValue)};
            return value;
        }
        default:
            refuse("'" + name.name + "' could not be resolved to something the graph can read.",
                   name.span);
            return std::nullopt;
    }
}

std::optional<GraphLifter::Value> GraphLifter::liftMember(const papyrus::MemberExpression& member,
                                                          std::vector<PinRef>& open) {
    const auto* binding = resolution_->bindingFor(&member);

    if (binding != nullptr && binding->kind == papyrus::BindingKind::ArrayMember &&
        iequals(member.name, "Length")) {
        const auto target = liftExpression(*member.target, open);
        const auto id = node(std::string(builtinDefs::kArrayPrefix) + "length",
                             GraphNodeKind::ArrayOp);
        if (target && target->pin) wire(*target->pin, PinRef{id, std::string(pinIds::kArray)});
        else if (target && target->literal) doc_->node(id)->pinValues[std::string(pinIds::kArray)] = *target->literal;
        Value value;
        value.pin = PinRef{id, std::string(pinIds::kReturn)};
        return value;
    }

    if (binding != nullptr && binding->kind == papyrus::BindingKind::StructMember) {
        const auto target = liftExpression(*member.target, open);
        const auto id = node(builtinDefs::kStructGet, GraphNodeKind::StructGet);
        config(id, "member", member.name);
        if (target && target->pin) wire(*target->pin, PinRef{id, std::string(pinIds::kTarget)});
        else if (target && target->literal) doc_->node(id)->pinValues[std::string(pinIds::kTarget)] = *target->literal;
        Value value;
        value.pin = PinRef{id, std::string(pinIds::kValue)};
        return value;
    }

    if (binding != nullptr && binding->kind == papyrus::BindingKind::Property &&
        binding->owner != nullptr) {
        const auto target = liftExpression(*member.target, open);
        const auto id = node(NodePalette::propertyGetId(binding->owner->name, member.name),
                             GraphNodeKind::PropertyGet);
        if (target && target->pin) wire(*target->pin, PinRef{id, std::string(pinIds::kSelf)});
        else if (target && target->literal) doc_->node(id)->pinValues[std::string(pinIds::kSelf)] = *target->literal;
        Value value;
        value.pin = PinRef{id, std::string(pinIds::kValue)};
        return value;
    }

    refuse("Reading '" + member.name + "' is not something the graph can express yet.",
           member.span);
    return std::nullopt;
}

std::optional<GraphLifter::Value> GraphLifter::liftCall(const papyrus::CallExpression& call,
                                                        std::vector<PinRef>& open) {
    const auto* binding = resolution_->bindingFor(call.callee.get());
    if (binding == nullptr) binding = resolution_->bindingFor(&call);
    if (binding == nullptr || binding->owner == nullptr ||
        (binding->kind != papyrus::BindingKind::Function &&
         binding->kind != papyrus::BindingKind::Event)) {
        refuse("Could not work out what '" + call.functionName() + "' calls.", call.span);
        return std::nullopt;
    }

    const auto* declared = dynamic_cast<const papyrus::FunctionDecl*>(binding->declaration);
    const bool isGlobal = declared != nullptr && declared->hasFlag("global");

    const auto id = NodePalette::callId(binding->owner->name, binding->name, isGlobal);
    const auto nodeId = node(id, GraphNodeKind::Call);

    if (!isGlobal) {
        if (const auto* receiver = dynamic_cast<const papyrus::MemberExpression*>(call.callee.get())) {
            const auto target = liftExpression(*receiver->target, open);
            if (target && target->pin) wire(*target->pin, PinRef{nodeId, std::string(pinIds::kSelf)});
            else if (target && target->literal) doc_->node(nodeId)->pinValues[std::string(pinIds::kSelf)] = *target->literal;
        }
    }

    const std::vector<papyrus::Parameter>* parameters =
        declared != nullptr ? &declared->parameters : nullptr;
    for (std::size_t i = 0; i < call.arguments.size(); ++i) {
        const auto& argument = call.arguments[i];
        std::optional<std::string> parameterName = argument.name;
        if (!parameterName && parameters != nullptr && i < parameters->size()) {
            parameterName = (*parameters)[i].name;
        }

        if (!parameterName) {
            refuse("'" + binding->name + "' was given more arguments than it declares.",
                   argument.span);
            continue;
        }

        const auto value = liftExpression(*argument.value, open);
        if (value && value->pin) {
            wire(*value->pin, PinRef{nodeId, pinIds::argument(*parameterName)});
        } else if (value && value->literal) {
            doc_->node(nodeId)->pinValues[pinIds::argument(*parameterName)] = *value->literal;
        }
    }

    enter(open, nodeId);
    open = std::vector<PinRef>{PinRef{nodeId, std::string(pinIds::kThen)}};

    Value value;
    value.pin = PinRef{nodeId, std::string(pinIds::kReturn)};
    return value;
}

std::string GraphLifter::node(std::string_view definitionId, GraphNodeKind kind) {
    const auto id = "n" + std::to_string(++sequence_);
    GraphNode out;
    out.id = id;
    out.definition = std::string(definitionId);
    out.kind = kind;
    out.x = penX_;
    out.y = penY_;
    doc_->nodes.push_back(std::move(out));
    penX_ += 40;
    doc_->invalidate();
    return id;
}

void GraphLifter::config(const std::string& nodeId, std::string_view key,
                         const std::optional<std::string>& value) {
    if (!value) return;
    doc_->node(nodeId)->config[std::string(key)] = *value;
}

void GraphLifter::enter(std::vector<PinRef>& open, const std::string& nodeId) {
    for (const auto& end : open) wire(end, PinRef{nodeId, std::string(pinIds::kExec)});
    open.clear();
}

void GraphLifter::wire(const PinRef& from, const PinRef& to) {
    GraphWire out;
    out.id = "w" + std::to_string(++sequence_);
    out.from = from;
    out.to = to;
    doc_->wires.push_back(std::move(out));
    doc_->invalidate();
}

void GraphLifter::attach(const Value& value, const PinRef& target) {
    if (value.pin) {
        wire(*value.pin, target);
        return;
    }
    if (value.literal) doc_->node(target.node)->pinValues[target.pin] = *value.literal;
}

void GraphLifter::refuse(const std::string& message, const papyrus::Span& span) {
    GraphDiagnostic diagnostic;
    diagnostic.code = std::string(codes::kUnsupportedSchema);
    diagnostic.severity = GraphSeverity::Error;
    diagnostic.message = span.line > 0 ? "Line " + std::to_string(span.line) + ": " + message
                                       : message;
    problems_.push_back(std::move(diagnostic));
}

std::string GraphLifter::typeName(const papyrus::Node& node) {
    const auto* type = resolution_->typeOf(&node);
    return type != nullptr ? type->toString() : std::string("var");
}

std::string GraphLifter::written(const papyrus::TypeRef* type) {
    return type != nullptr ? type->toString() : std::string("var");
}

bool GraphLifter::isNone(const papyrus::TypeRef& type) {
    return iequals(type.toString(), "None");
}

std::optional<std::string> GraphLifter::text(const papyrus::Node* node) {
    const auto* literal = dynamic_cast<const papyrus::LiteralExpression*>(node);
    return literal != nullptr ? std::optional<std::string>(literal->text) : std::nullopt;
}

std::string GraphLifter::literalText(const papyrus::LiteralExpression& literal) {
    if (literal.kind != papyrus::LiteralKind::String) return literal.text;

    std::string out = "\"";
    for (const char ch : literal.text) {
        if (ch == '\\') out += "\\\\";
        else if (ch == '"') out += "\\\"";
        else out.push_back(ch);
    }
    out += "\"";
    return out;
}

std::string GraphLifter::literalType(const papyrus::LiteralExpression& literal) {
    switch (literal.kind) {
        case papyrus::LiteralKind::Int: return "int";
        case papyrus::LiteralKind::Float: return "float";
        case papyrus::LiteralKind::String: return "string";
        case papyrus::LiteralKind::Bool: return "bool";
        default: return "int";
    }
}

std::optional<std::string> GraphLifter::compoundId(papyrus::TokenKind op) {
    switch (op) {
        case papyrus::TokenKind::PlusAssign: return "add";
        case papyrus::TokenKind::MinusAssign: return "sub";
        case papyrus::TokenKind::StarAssign: return "mul";
        case papyrus::TokenKind::SlashAssign: return "div";
        case papyrus::TokenKind::PercentAssign: return "mod";
        default: return std::nullopt;
    }
}

std::optional<std::string> GraphLifter::binaryId(papyrus::TokenKind op) {
    switch (op) {
        case papyrus::TokenKind::Plus: return "add";
        case papyrus::TokenKind::Minus: return "sub";
        case papyrus::TokenKind::Star: return "mul";
        case papyrus::TokenKind::Slash: return "div";
        case papyrus::TokenKind::Percent: return "mod";
        case papyrus::TokenKind::Equal: return "eq";
        case papyrus::TokenKind::NotEqual: return "ne";
        case papyrus::TokenKind::Less: return "lt";
        case papyrus::TokenKind::LessEqual: return "le";
        case papyrus::TokenKind::Greater: return "gt";
        case papyrus::TokenKind::GreaterEqual: return "ge";
        case papyrus::TokenKind::And: return "and";
        case papyrus::TokenKind::Or: return "or";
        default: return std::nullopt;
    }
}

}
