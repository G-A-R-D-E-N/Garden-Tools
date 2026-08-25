#include <fo4ide/graph/GraphLifter.hpp>

#include <cctype>
#include <utility>

#include <fo4ide/papyrus/Resolver.hpp>

namespace fo4ide::graph {

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

// The C# refusal messages use GetType().Name; the closest native equivalent is
// the NodeKind name (e.g. "IfStatement").
std::string nodeKindName(papyrus::NodeKind kind) {
    switch (kind) {
        case papyrus::NodeKind::Import: return "Import";
        case papyrus::NodeKind::VariableDecl: return "VariableDecl";
        case papyrus::NodeKind::StructDecl: return "StructDecl";
        case papyrus::NodeKind::CustomEventDecl: return "CustomEventDecl";
        case papyrus::NodeKind::Parameter: return "Parameter";
        case papyrus::NodeKind::FunctionDecl: return "FunctionDecl";
        case papyrus::NodeKind::EventDecl: return "EventDecl";
        case papyrus::NodeKind::PropertyDecl: return "PropertyDecl";
        case papyrus::NodeKind::GroupDecl: return "GroupDecl";
        case papyrus::NodeKind::StateDecl: return "StateDecl";
        case papyrus::NodeKind::Script: return "Script";
        case papyrus::NodeKind::DefineStatement: return "DefineStatement";
        case papyrus::NodeKind::AssignStatement: return "AssignStatement";
        case papyrus::NodeKind::ExpressionStatement: return "ExpressionStatement";
        case papyrus::NodeKind::ReturnStatement: return "ReturnStatement";
        case papyrus::NodeKind::IfStatement: return "IfStatement";
        case papyrus::NodeKind::WhileStatement: return "WhileStatement";
        case papyrus::NodeKind::LiteralExpression: return "LiteralExpression";
        case papyrus::NodeKind::IdentifierExpression: return "IdentifierExpression";
        case papyrus::NodeKind::MemberExpression: return "MemberExpression";
        case papyrus::NodeKind::IndexExpression: return "IndexExpression";
        case papyrus::NodeKind::Argument: return "Argument";
        case papyrus::NodeKind::CallExpression: return "CallExpression";
        case papyrus::NodeKind::UnaryExpression: return "UnaryExpression";
        case papyrus::NodeKind::BinaryExpression: return "BinaryExpression";
        case papyrus::NodeKind::CastExpression: return "CastExpression";
        case papyrus::NodeKind::TypeCheckExpression: return "TypeCheckExpression";
        case papyrus::NodeKind::NewArrayExpression: return "NewArrayExpression";
        case papyrus::NodeKind::NewStructExpression: return "NewStructExpression";
        case papyrus::NodeKind::ErrorExpression: return "ErrorExpression";
    }
    return "Node";
}

}  // namespace

GraphLifter::GraphLifter(papyrus::ScriptIndex& index) : index_(index) {}

GraphLiftResult GraphLifter::lift(const papyrus::Script& script) {
    script_ = &script;
    problems_.clear();
    sequence_ = 0;
    penX_ = 0;
    penY_ = 0;

    if (script.hasErrors()) {
        for (const auto& diagnostic : script.diagnostics) {
            if (diagnostic.severity == papyrus::Severity::Error) {
                refuse("The source did not parse: " + diagnostic.message, diagnostic.span);
            }
        }
        GraphLiftResult result;
        result.diagnostics = std::move(problems_);
        return result;
    }

    resolution_.emplace(papyrus::Resolver(index_).resolve(script_));
    doc_.emplace();
    doc_->id = "lift";

    liftHeader();
    liftDeclarations();

    // Port of Callables(): top-level functions, events, then each state's.
    for (const auto& function : script_->functions) {
        penX_ = 0;
        penY_ += 260;
        const auto entry = entryNode(function, std::nullopt);
        if (!entry) continue;
        auto open = std::vector<PinRef>{PinRef{*entry, std::string(pinIds::kExec)}};
        liftBlock(function.body, std::move(open));
    }
    for (const auto& declared : script_->events) {
        penX_ = 0;
        penY_ += 260;
        const auto entry = entryNode(declared, std::nullopt);
        if (!entry) continue;
        auto open = std::vector<PinRef>{PinRef{*entry, std::string(pinIds::kExec)}};
        liftBlock(declared.body, std::move(open));
    }
    for (const auto& state : script_->states) {
        for (const auto& function : state.functions) {
            penX_ = 0;
            penY_ += 260;
            const auto entry = entryNode(function, state.name);
            if (!entry) continue;
            auto open = std::vector<PinRef>{PinRef{*entry, std::string(pinIds::kExec)}};
            liftBlock(function.body, std::move(open));
        }
        for (const auto& declared : state.events) {
            penX_ = 0;
            penY_ += 260;
            const auto entry = entryNode(declared, state.name);
            if (!entry) continue;
            auto open = std::vector<PinRef>{PinRef{*entry, std::string(pinIds::kExec)}};
            liftBlock(declared.body, std::move(open));
        }
    }

    GraphLiftResult result;
    result.document = std::move(doc_);
    result.diagnostics = std::move(problems_);
    return result;
}

void GraphLifter::liftHeader() {
    doc_->header.scriptName = script_->name;
    doc_->header.extends = script_->extends;
    doc_->header.flags = script_->flags;
    doc_->header.docComment = script_->documentation;

    for (const auto& state : script_->states) {
        if (state.isAuto) {
            doc_->header.autoState = state.name;
            break;
        }
    }
}

void GraphLifter::liftDeclarations() {
    for (const auto& variable : script_->variables) {
        GraphVariable out;
        out.name = variable.name;
        out.type = written(&variable.type);
        out.isProperty = false;
        out.flags = variable.flags;
        out.initial = text(variable.initializer.get());
        doc_->variables.push_back(std::move(out));
    }

    for (const auto& property : script_->properties) {
        if (property.kind != papyrus::PropertyKind::Auto &&
            property.kind != papyrus::PropertyKind::AutoReadOnly) {
            refuse("Property '" + property.name +
                       "' has explicit Get or Set bodies, which the graph cannot hold yet.",
                   property.nameSpan);
            continue;
        }

        GraphVariable out;
        out.name = property.name;
        out.type = written(&property.type);
        out.isProperty = true;
        out.flags = property.flags;
        out.initial = text(property.initializer.get());
        doc_->variables.push_back(std::move(out));
    }

    for (const auto& declared : script_->structs) {
        GraphStruct out;
        out.name = declared.name;
        for (const auto& member : declared.members) {
            GraphVariable memberOut;
            memberOut.name = member.name;
            memberOut.type = written(&member.type);
            out.members.push_back(std::move(memberOut));
        }
        doc_->structs.push_back(std::move(out));
    }

    for (const auto& custom : script_->customEvents) doc_->customEvents.push_back(custom.name);
}

std::optional<std::string> GraphLifter::entryNode(const papyrus::Declaration& declaration,
                                                  const std::optional<std::string>& state) {
    if (const auto* function = dynamic_cast<const papyrus::FunctionDecl*>(&declaration)) {
        const auto id = node(builtinDefs::kFunctionEntry, GraphNodeKind::FunctionEntry);
        config(id, "name", function->name);
        if (function->returnType && !isNone(*function->returnType)) {
            config(id, "returns", written(&*function->returnType));
        }
        if (function->hasFlag("global")) config(id, "global", std::string("true"));
        if (state) config(id, "state", state);
        return id;
    }

    if (const auto* declared = dynamic_cast<const papyrus::EventDecl*>(&declaration)) {
        const auto owner = declared->remoteObjectType
                               ? std::optional<std::string>(*declared->remoteObjectType)
                               : declaringScriptOf(declared->name);
        if (!owner) {
            refuse("Could not work out which script declares event '" + declared->name + "'.",
                   declared->nameSpan);
            return std::nullopt;
        }

        const auto id = declared->remoteObjectType
                            ? NodePalette::remoteEventId(*owner, declared->name)
                            : NodePalette::eventId(*owner, declared->name);
        const auto nodeId = node(id, GraphNodeKind::EventEntry);
        if (state) config(nodeId, "state", state);
        return nodeId;
    }

    refuse("'" + declaration.name +
               "' is a kind of declaration the graph has no entry for.",
           declaration.nameSpan);
    return std::nullopt;
}

std::optional<std::string> GraphLifter::declaringScriptOf(const std::string& eventName) {
    const papyrus::Script* current = script_;
    int guard = 0;

    while (current != nullptr && guard++ < 64) {
        bool declares = false;
        for (const auto& event : current->events) {
            if (!event.remoteObjectType && iequals(event.name, eventName)) {
                declares = true;
                break;
            }
        }
        if (declares && current != script_) return current->name;

        if (!current->extends || current->extends->empty()) break;
        current = index_.resolve(*current->extends);
    }

    return script_->extends ? *script_->extends : script_->name;
}

std::vector<PinRef> GraphLifter::liftBlock(const std::vector<papyrus::StatementPtr>& body,
                                           std::vector<PinRef> open) {
    for (const auto& statement : body) {
        if (statement) open = liftStatement(*statement, std::move(open));
    }
    return open;
}

std::vector<PinRef> GraphLifter::liftStatement(const papyrus::Statement& statement,
                                               std::vector<PinRef> open) {
    if (const auto* define = dynamic_cast<const papyrus::DefineStatement*>(&statement)) {
        std::optional<Value> value;
        if (define->initializer) {
            value = liftExpression(*dynamic_cast<const papyrus::Expression*>(define->initializer.get()),
                                   open);
        }

        const auto id = node(builtinDefs::kLocalDeclare, GraphNodeKind::LocalDeclare);
        config(id, "name", define->name);
        config(id, "type", define->type.toString());
        enter(open, id);
        if (value && value->pin) wire(*value->pin, PinRef{id, std::string(pinIds::kValue)});
        else if (value && value->literal) doc_->node(id)->pinValues[std::string(pinIds::kValue)] = *value->literal;
        return std::vector<PinRef>{PinRef{id, std::string(pinIds::kThen)}};
    }

    if (const auto* assign = dynamic_cast<const papyrus::AssignStatement*>(&statement)) {
        return liftAssign(*assign, std::move(open));
    }

    if (const auto* expression = dynamic_cast<const papyrus::ExpressionStatement*>(&statement)) {
        liftExpression(*expression->expression, open);
        return open;
    }

    if (const auto* returned = dynamic_cast<const papyrus::ReturnStatement*>(&statement)) {
        std::optional<Value> value;
        if (returned->value) {
            value = liftExpression(*returned->value, open);
        }

        const auto id = node(builtinDefs::kReturn, GraphNodeKind::Return);
        enter(open, id);
        if (value && value->pin) wire(*value->pin, PinRef{id, std::string(pinIds::kValue)});
        else if (value && value->literal) doc_->node(id)->pinValues[std::string(pinIds::kValue)] = *value->literal;
        return {};
    }

    if (const auto* branch = dynamic_cast<const papyrus::IfStatement*>(&statement)) {
        return liftIf(*branch, std::move(open), 0);
    }

    if (const auto* loop = dynamic_cast<const papyrus::WhileStatement*>(&statement)) {
        return liftWhile(*loop, std::move(open));
    }

    refuse("'" + nodeKindName(statement.nodeKind()) +
               "' is a statement the graph has no node for.",
           statement.span);
    return open;
}

std::vector<PinRef> GraphLifter::liftAssign(const papyrus::AssignStatement& assign,
                                            std::vector<PinRef> open) {
    const auto compound = compoundId(assign.op);
    if (compound && dynamic_cast<const papyrus::IdentifierExpression*>(assign.target.get()) == nullptr) {
        refuse("A compound assignment to something other than a plain variable cannot be "
               "expanded safely, because the target would be evaluated twice.",
               assign.span);
        return open;
    }

    if (!compound && assign.op != papyrus::TokenKind::Assign) {
        refuse("Assignment operator '" + std::string(papyrus::tokenKindName(assign.op)) +
                   "' has no graph node.",
               assign.span);
        return open;
    }

    std::optional<Value> value = liftExpression(*assign.value, open);

    if (compound && value &&
        dynamic_cast<const papyrus::IdentifierExpression*>(assign.target.get()) != nullptr) {
        const auto& current = *static_cast<const papyrus::IdentifierExpression*>(assign.target.get());

        const auto read = node(builtinDefs::kVariableGet, GraphNodeKind::VariableGet);
        config(read, "name", current.name);
        config(read, "type", typeName(current));

        const auto combined = node(std::string(builtinDefs::kOperatorPrefix) + *compound,
                                   GraphNodeKind::Binary);
        wire(PinRef{read, std::string(pinIds::kValue)},
             PinRef{combined, std::string(pinIds::kLeft)});
        if (value->pin) {
            wire(*value->pin, PinRef{combined, std::string(pinIds::kRight)});
        } else if (value->literal) {
            doc_->node(combined)->pinValues[std::string(pinIds::kRight)] = *value->literal;
        }

        Value combinedValue;
        combinedValue.pin = PinRef{combined, std::string(pinIds::kReturn)};
        value = std::move(combinedValue);
    }

    if (const auto* name = dynamic_cast<const papyrus::IdentifierExpression*>(assign.target.get())) {
        const auto id = node(builtinDefs::kVariableSet, GraphNodeKind::VariableSet);
        config(id, "name", name->name);
        config(id, "type", typeName(*name));
        enter(open, id);
        if (value && value->pin) wire(*value->pin, PinRef{id, std::string(pinIds::kValue)});
        else if (value && value->literal) doc_->node(id)->pinValues[std::string(pinIds::kValue)] = *value->literal;
        return std::vector<PinRef>{PinRef{id, std::string(pinIds::kThen)}};
    }

    if (const auto* index = dynamic_cast<const papyrus::IndexExpression*>(assign.target.get())) {
        const auto target = liftExpression(*index->target, open);
        const auto at = liftExpression(*index->index, open);

        const auto id = node(builtinDefs::kIndexSet, GraphNodeKind::IndexSet);
        enter(open, id);
        if (target && target->pin) wire(*target->pin, PinRef{id, std::string(pinIds::kArray)});
        else if (target && target->literal) doc_->node(id)->pinValues[std::string(pinIds::kArray)] = *target->literal;
        if (at && at->pin) wire(*at->pin, PinRef{id, std::string(pinIds::kIndex)});
        else if (at && at->literal) doc_->node(id)->pinValues[std::string(pinIds::kIndex)] = *at->literal;
        if (value && value->pin) wire(*value->pin, PinRef{id, std::string(pinIds::kValue)});
        else if (value && value->literal) doc_->node(id)->pinValues[std::string(pinIds::kValue)] = *value->literal;
        return std::vector<PinRef>{PinRef{id, std::string(pinIds::kThen)}};
    }

    if (const auto* member = dynamic_cast<const papyrus::MemberExpression*>(assign.target.get())) {
        const auto binding = resolution_->bindingFor(member);
        if (binding == nullptr || binding->kind != papyrus::BindingKind::Property ||
            binding->owner == nullptr) {
            refuse("Assigning to '" + member->name +
                       "' is not something the graph can express yet.",
                   member->span);
            return open;
        }

        const auto target = liftExpression(*member->target, open);
        const auto id = node(NodePalette::propertySetId(binding->owner->name, member->name),
                             GraphNodeKind::PropertySet);
        enter(open, id);
        if (target && target->pin) wire(*target->pin, PinRef{id, std::string(pinIds::kSelf)});
        else if (target && target->literal) doc_->node(id)->pinValues[std::string(pinIds::kSelf)] = *target->literal;
        if (value && value->pin) wire(*value->pin, PinRef{id, std::string(pinIds::kValue)});
        else if (value && value->literal) doc_->node(id)->pinValues[std::string(pinIds::kValue)] = *value->literal;
        return std::vector<PinRef>{PinRef{id, std::string(pinIds::kThen)}};
    }

    refuse("This assignment target has no graph node.", assign.target->span);
    return open;
}

std::vector<PinRef> GraphLifter::liftIf(const papyrus::IfStatement& statement,
                                        std::vector<PinRef> open, std::size_t arm) {
    const auto& branchArm = statement.branches[arm];
    const auto condition = liftExpression(*branchArm.condition, open);

    const auto id = node(builtinDefs::kBranch, GraphNodeKind::Branch);
    enter(open, id);
    if (condition && condition->pin) wire(*condition->pin, PinRef{id, std::string(pinIds::kCondition)});
    else if (condition && condition->literal) doc_->node(id)->pinValues[std::string(pinIds::kCondition)] = *condition->literal;

    auto thenOpen = liftBlock(branchArm.body,
                              std::vector<PinRef>{PinRef{id, std::string(pinIds::kThen)}});

    std::vector<PinRef> elseOpen;
    if (arm + 1 < statement.branches.size()) {
        elseOpen = liftIf(statement, std::vector<PinRef>{PinRef{id, std::string(pinIds::kElse)}}, arm + 1);
    } else if (!statement.elseBody.empty()) {
        elseOpen = liftBlock(statement.elseBody,
                             std::vector<PinRef>{PinRef{id, std::string(pinIds::kElse)}});
    } else {
        elseOpen = std::vector<PinRef>{PinRef{id, std::string(pinIds::kElse)}};
    }

    thenOpen.insert(thenOpen.end(), elseOpen.begin(), elseOpen.end());
    return thenOpen;
}

std::vector<PinRef> GraphLifter::liftWhile(const papyrus::WhileStatement& loop,
                                           std::vector<PinRef> open) {
    if (loop.condition && containsCall(*loop.condition)) {
        refuse("This loop's condition calls a function. The graph would evaluate it once "
               "before the loop rather than on each pass, so it cannot be lifted.",
               loop.condition->span);
        return open;
    }

    const auto condition = liftExpression(*loop.condition, open);

    const auto id = node(builtinDefs::kWhile, GraphNodeKind::While);
    enter(open, id);
    if (condition && condition->pin) wire(*condition->pin, PinRef{id, std::string(pinIds::kCondition)});
    else if (condition && condition->literal) doc_->node(id)->pinValues[std::string(pinIds::kCondition)] = *condition->literal;

    auto bodyOpen = liftBlock(loop.body, std::vector<PinRef>{PinRef{id, std::string(pinIds::kBody)}});
    for (const auto& end : bodyOpen) wire(end, PinRef{id, std::string(pinIds::kExec)});

    return std::vector<PinRef>{PinRef{id, std::string(pinIds::kCompleted)}};
}

bool GraphLifter::containsCall(const papyrus::Node& node) {
    if (dynamic_cast<const papyrus::CallExpression*>(&node) != nullptr) return true;
    if (const auto* member = dynamic_cast<const papyrus::MemberExpression*>(&node)) {
        return member->target && containsCall(*member->target);
    }
    if (const auto* index = dynamic_cast<const papyrus::IndexExpression*>(&node)) {
        return (index->target && containsCall(*index->target)) ||
               (index->index && containsCall(*index->index));
    }
    if (const auto* unary = dynamic_cast<const papyrus::UnaryExpression*>(&node)) {
        return unary->operand && containsCall(*unary->operand);
    }
    if (const auto* binary = dynamic_cast<const papyrus::BinaryExpression*>(&node)) {
        return (binary->left && containsCall(*binary->left)) ||
               (binary->right && containsCall(*binary->right));
    }
    if (const auto* cast = dynamic_cast<const papyrus::CastExpression*>(&node)) {
        return cast->operand && containsCall(*cast->operand);
    }
    if (const auto* check = dynamic_cast<const papyrus::TypeCheckExpression*>(&node)) {
        return check->operand && containsCall(*check->operand);
    }
    if (const auto* created = dynamic_cast<const papyrus::NewArrayExpression*>(&node)) {
        return created->size && containsCall(*created->size);
    }
    return false;
}

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

}  // namespace fo4ide::graph
