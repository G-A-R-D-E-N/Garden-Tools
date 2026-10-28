#include <fo4/graph/graph_lifter.hpp>

#include <cctype>
#include <utility>

#include <fo4/papyrus/resolver.hpp>
#include "graph_lifter_detail.hpp"

namespace fo4::graph {

using namespace lifter_detail;

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

}
