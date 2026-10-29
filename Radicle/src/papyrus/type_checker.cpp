#include <fo4/papyrus/type_checker.hpp>

#include <cctype>
#include <string>

#include <fo4/papyrus/resolver.hpp>

namespace fo4::papyrus {

namespace {

std::string lower(std::string_view text) {
    std::string out(text);
    for (auto& ch : out) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return out;
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto x = static_cast<unsigned char>(a[i]);
        const auto y = static_cast<unsigned char>(b[i]);
        if (std::tolower(x) != std::tolower(y)) return false;
    }
    return true;
}

void visitChildExpressions(const Node* node, const auto& visitor) {
    switch (node->nodeKind()) {
        case NodeKind::MemberExpression: {
            const auto* member = static_cast<const MemberExpression*>(node);
            visitor(member->target.get());
            break;
        }
        case NodeKind::UnaryExpression: {
            const auto* unary = static_cast<const UnaryExpression*>(node);
            visitor(unary->operand.get());
            break;
        }
        case NodeKind::BinaryExpression: {
            const auto* bin = static_cast<const BinaryExpression*>(node);
            visitor(bin->left.get());
            visitor(bin->right.get());
            break;
        }
        case NodeKind::NewArrayExpression: {
            const auto* newArray = static_cast<const NewArrayExpression*>(node);
            visitor(newArray->size.get());
            break;
        }
        case NodeKind::NewStructExpression:
        case NodeKind::LiteralExpression:
        case NodeKind::IdentifierExpression:
        case NodeKind::ErrorExpression:
            break;
        default:
            break;
    }
}

std::string describe(const std::optional<Type>& type) {
    return type ? type->toString() : std::string("nothing");
}

bool isComparison(TokenKind op) {
    switch (op) {
        case TokenKind::Equal:
        case TokenKind::NotEqual:
        case TokenKind::Less:
        case TokenKind::LessEqual:
        case TokenKind::Greater:
        case TokenKind::GreaterEqual:
            return true;
        default:
            return false;
    }
}

}

void TypeChecker::State::report(DiagnosticCode code, std::string message, const Span& span) {
    diagnostics.push_back(Diagnostic{code, Severity::Error, std::move(message), span, file});
}

std::vector<Diagnostic> TypeChecker::check(const Resolution& resolution) {
    if (!resolution.baseChainComplete) return {};

    State state;
    state.resolution = &resolution;
    state.file = resolution.script ? resolution.script->filePath : std::nullopt;
    const Script* script = resolution.script;
    if (!script) return {};

    for (const auto& v : script->variables) checkInitializer(state, v.type, v.initializer.get());
    for (const auto& p : script->properties) {
        checkInitializer(state, p.type, p.initializer.get());
        if (p.getter) checkCallable(state, *p.getter);
        if (p.setter) checkCallable(state, *p.setter);
    }
    for (const auto& s : script->structs) {
        for (const auto& m : s.members) checkInitializer(state, m.type, m.initializer.get());
    }

    for (const auto& fn : script->functions) {
        checkOverride(state, fn);
        checkCallable(state, fn);
    }
    for (const auto& ev : script->events) checkCallable(state, ev);
    for (const auto& st : script->states) {
        for (const auto& fn : st.functions) checkCallable(state, fn);
        for (const auto& ev : st.events) checkCallable(state, ev);
    }

    return std::move(state.diagnostics);
}

void TypeChecker::checkCallable(State& state, const CallableDecl& callable) {
    const auto* fn = dynamic_cast<const FunctionDecl*>(&callable);
    state.returnType =
        fn && fn->returnType ? *state.resolution->typeOf(&*fn->returnType) : std::optional<Type>{};

    bool seenDefault = false;
    for (const auto& p : callable.parameters) {
        if (p.defaultValue) {
            seenDefault = true;
        } else if (seenDefault) {
            state.report(DiagnosticCode::ParameterOrder,
                         "'" + p.name + "' has no default value, but an earlier parameter does. " +
                             "Every parameter after one with a default must have one too.",
                         p.span);
        }
        checkInitializer(state, p.type, p.defaultValue.get());
    }

    checkStatements(state, callable.body);
    state.returnType = std::nullopt;
}

void TypeChecker::checkOverride(State& state, const FunctionDecl& fn) {
    const Script* script = state.resolution->script;
    if (!script || !script->extends) return;

    const auto declared = [this](const TypeRef& type, const Script* owner) -> std::optional<Type> {
        const auto resolved = Resolver::resolveTypeName(index_, type.name, owner);
        if (!resolved) return std::nullopt;
        return type.isArray ? Type::arrayOf(*resolved) : *resolved;
    };

    const auto chain = index_.baseChain(script);
    for (std::size_t i = 1; i < chain.size(); ++i) {
        const Declaration* parentDecl = ScriptIndex::findMemberOn(chain[i], fn.name);
        const auto* parent = dynamic_cast<const FunctionDecl*>(parentDecl);
        if (!parent) continue;

        const std::string overridden =
            "'" + fn.name + "' overrides " + chain[i]->name + "." + parent->name;
        if (parent->parameters.size() != fn.parameters.size()) {
            state.report(DiagnosticCode::OverrideMismatch,
                         overridden + ", which takes " + std::to_string(parent->parameters.size()) +
                             " parameter(s), not " + std::to_string(fn.parameters.size()) + ".",
                         fn.nameSpan);
            return;
        }

        for (std::size_t p = 0; p < fn.parameters.size(); ++p) {
            const auto expected = declared(parent->parameters[p].type, chain[i]);
            const auto actual = declared(fn.parameters[p].type, script);
            if (expected && actual && *expected != *actual) {
                state.report(DiagnosticCode::OverrideMismatch,
                             overridden + ", whose parameter " + std::to_string(p + 1) + " is '" +
                                 expected->toString() + "', not '" + actual->toString() + "'.",
                             fn.nameSpan);
                return;
            }
        }

        const auto expectedReturn =
            parent->returnType ? declared(*parent->returnType, chain[i]) : std::nullopt;
        const auto actualReturn = fn.returnType ? declared(*fn.returnType, script) : std::nullopt;
        const bool known =
            (!parent->returnType || expectedReturn) && (!fn.returnType || actualReturn);
        if (known && expectedReturn != actualReturn) {
            state.report(DiagnosticCode::OverrideMismatch,
                         overridden + ", which returns " + describe(expectedReturn) + ", not " +
                             describe(actualReturn) + ".",
                         fn.nameSpan);
        }
        return;
    }
}

void TypeChecker::checkStatements(State& state, const std::vector<StatementPtr>& body) {
    for (const auto& stmt : body) checkStatement(state, stmt.get());
}

void TypeChecker::checkStatement(State& state, const Statement* statement) {
    switch (statement->nodeKind()) {
        case NodeKind::DefineStatement: {
            const auto* def = static_cast<const DefineStatement*>(statement);
            checkExpression(state, def->initializer.get());
            if (def->initializer) {
                checkAssignable(state, *state.resolution->typeOf(&def->type),
                                *state.resolution->typeOf(def->initializer.get()),
                                def->initializer->span, "initialise '" + def->name + "'");
            }
            break;
        }

        case NodeKind::AssignStatement: {
            const auto* assign = static_cast<const AssignStatement*>(statement);
            checkExpression(state, assign->target.get());
            checkExpression(state, assign->value.get());

            if (assign->op == TokenKind::Assign) {
                checkAssignable(state, *state.resolution->typeOf(assign->target.get()),
                                *state.resolution->typeOf(assign->value.get()),
                                assign->value->span, "assign");
            }
            break;
        }

        case NodeKind::ExpressionStatement: {
            const auto* expr = static_cast<const ExpressionStatement*>(statement);
            checkExpression(state, expr->expression.get());
            break;
        }

        case NodeKind::ReturnStatement: {
            const auto* ret = static_cast<const ReturnStatement*>(statement);
            checkExpression(state, ret->value.get());
            if (!ret->value) break;
            if (!state.returnType) {
                state.report(DiagnosticCode::TypeMismatch,
                             "This function returns nothing, so it cannot return a value.",
                             ret->span);
                break;
            }
            checkAssignable(state, *state.returnType,
                            *state.resolution->typeOf(ret->value.get()), ret->value->span, "return");
            break;
        }

        case NodeKind::IfStatement: {
            const auto* iff = static_cast<const IfStatement*>(statement);
            for (const auto& branch : iff->branches) {
                checkExpression(state, branch.condition.get());
                checkStatements(state, branch.body);
            }
            if (!iff->elseBody.empty()) checkStatements(state, iff->elseBody);
            break;
        }

        case NodeKind::WhileStatement: {
            const auto* wh = static_cast<const WhileStatement*>(statement);
            checkExpression(state, wh->condition.get());
            checkStatements(state, wh->body);
            break;
        }

        default:
            break;
    }
}

void TypeChecker::checkInitializer(State& state, const TypeRef& declared,
                                   const Node* initializer) {
    if (!initializer) return;
    checkExpression(state, initializer);
    checkAssignable(state, *state.resolution->typeOf(&declared),
                    *state.resolution->typeOf(initializer), initializer->span, "initialise");
}

void TypeChecker::checkExpression(State& state, const Node* expression) {
    if (!expression) return;

    switch (expression->nodeKind()) {
        case NodeKind::CallExpression: {
            const auto* call = static_cast<const CallExpression*>(expression);
            checkExpression(state, call->callee.get());
            for (const auto& arg : call->arguments) checkExpression(state, arg.value.get());
            checkCall(state, *call);
            return;
        }

        case NodeKind::CastExpression: {
            const auto* cast = static_cast<const CastExpression*>(expression);
            checkExpression(state, cast->operand.get());
            const Type& from = *state.resolution->typeOf(cast->operand.get());
            const Type& to = *state.resolution->typeOf(&cast->type);
            if (!isExplicit(from, to, [this](const std::string& child, const std::string& ancestor) {
                    return inherits(child, ancestor);
                })) {
                state.report(DiagnosticCode::InvalidCast,
                             "'" + from.toString() + "' cannot be cast to '" + to.toString() + "'.",
                             cast->span);
            }
            return;
        }

        case NodeKind::BinaryExpression: {
            const auto* bin = static_cast<const BinaryExpression*>(expression);
            checkExpression(state, bin->left.get());
            checkExpression(state, bin->right.get());
            if (isComparison(bin->op)) {
                checkComparable(state, *state.resolution->typeOf(bin->left.get()),
                                *state.resolution->typeOf(bin->right.get()), bin->span);
            }
            return;
        }

        case NodeKind::IndexExpression: {
            const auto* index = static_cast<const IndexExpression*>(expression);
            checkExpression(state, index->target.get());
            checkExpression(state, index->index.get());
            return;
        }

        default:
            visitChildExpressions(expression,
                                  [&](const Node* child) { checkExpression(state, child); });
            return;
    }
}

void TypeChecker::checkCall(State& state, const CallExpression& call) {
    const Binding* binding = state.resolution->bindingFor(&call);
    const auto* declaration = binding ? dynamic_cast<const CallableDecl*>(binding->declaration)
                                      : nullptr;
    if (!declaration) return;

    const auto& parameters = declaration->parameters;
    std::size_t required = 0;
    for (const auto& p : parameters) {
        if (!p.defaultValue) ++required;
    }
    std::size_t positional = 0;
    for (const auto& arg : call.arguments) {
        if (!arg.name) ++positional;
    }

    if (call.arguments.size() > parameters.size()) {
        state.report(DiagnosticCode::ArgumentCount,
                     "'" + declaration->name + "' takes at most " +
                         std::to_string(parameters.size()) + " argument(s), but " +
                         std::to_string(call.arguments.size()) + " were given.",
                     call.span);
        return;
    }

    if (positional < required && call.arguments.size() < required) {
        state.report(DiagnosticCode::ArgumentCount,
                     "'" + declaration->name + "' needs " + std::to_string(required) +
                         " argument(s), but " + std::to_string(call.arguments.size()) +
                         " were given.",
                     call.span);
        return;
    }

    for (std::size_t i = 0; i < call.arguments.size(); ++i) {
        const auto& arg = call.arguments[i];
        const Parameter* parameter = nullptr;

        if (!arg.name) {
            if (i >= parameters.size()) continue;
            parameter = &parameters[i];
        } else {
            for (const auto& p : parameters) {
                if (iequals(p.name, *arg.name)) {
                    parameter = &p;
                    break;
                }
            }
            if (!parameter) {
                state.report(DiagnosticCode::UnknownArgumentName,
                             "'" + declaration->name + "' has no parameter named '" + *arg.name +
                                 "'.",
                             arg.nameSpan);
                continue;
            }
        }

        const Type expected = parameterType(*parameter, binding->owner);
        checkAssignable(state, expected, *state.resolution->typeOf(arg.value.get()),
                        arg.value->span, "pass to '" + parameter->name + "'");
    }
}

Type TypeChecker::parameterType(const Parameter& parameter, const Script* owner) {
    const std::string& name = parameter.type.name;
    std::optional<Type> resolved;
    if (const Type* primitive = Type::primitive(name)) resolved = *primitive;

    if (!resolved && owner) {
        for (const Script* level : index_.baseChain(owner)) {
            for (const auto& s : level->structs) {
                if (iequals(s.name, name)) {
                    resolved = Type::structOf(level->name, name);
                    break;
                }
            }
            if (resolved) break;
        }
    }

    if (!resolved) {
        const Script* script = index_.resolve(name);
        resolved = script ? Type::object(script->name) : Type::error();
    }

    return parameter.type.isArray ? Type::arrayOf(*resolved) : *resolved;
}

void TypeChecker::checkAssignable(State& state, const Type& target, const Type& value,
                                  const Span& span, std::string_view what) {
    if (target.kind == TypeKind::Error || value.kind == TypeKind::Error) return;
    if (isImplicit(value, target, [this](const std::string& child, const std::string& ancestor) {
            return inherits(child, ancestor);
        })) {
        return;
    }

    state.report(DiagnosticCode::TypeMismatch,
                 "Cannot " + std::string(what) + ": '" + value.toString() +
                     "' is not compatible with '" + target.toString() + "'.",
                 span);
}

void TypeChecker::checkComparable(State& state, const Type& left, const Type& right,
                                  const Span& span) {
    if (comparable(left, right)) return;

    state.report(DiagnosticCode::TypeMismatch,
                 "Cannot compare '" + left.toString() + "' with '" + right.toString() +
                     "': the operands are not compatible.",
                 span);
}

bool TypeChecker::comparable(const Type& left, const Type& right) {
    const auto known = [](TypeKind kind) {
        return kind == TypeKind::Bool || kind == TypeKind::Int || kind == TypeKind::Float ||
               kind == TypeKind::String || kind == TypeKind::Object;
    };
    if (!known(left.kind) || !known(right.kind)) return true;

    if (left.kind == TypeKind::Object && right.kind == TypeKind::Object) {
        const auto ancestryUnknown = [this](const std::string& name) {
            const Script* script = index_.resolve(name);
            if (!script) return true;
            const auto chain = index_.baseChain(script);
            return chain.back()->extends.has_value();
        };
        return ancestryUnknown(left.name) || ancestryUnknown(right.name) ||
               inherits(left.name, right.name) || inherits(right.name, left.name);
    }

    if (left.kind == TypeKind::Object || right.kind == TypeKind::Object) {
        const TypeKind other = left.kind == TypeKind::Object ? right.kind : left.kind;
        return other != TypeKind::Int && other != TypeKind::Float;
    }
    return true;
}

bool TypeChecker::inherits(const std::string& child, const std::string& ancestor) {
    if (iequals(child, ancestor)) return true;

    const auto key = lower(child) + "\x1f" + lower(ancestor);
    const auto cached = inheritsCache_.find(key);
    if (cached != inheritsCache_.end()) return cached->second;

    const Script* script = index_.resolve(child);
    bool result = false;
    if (script) {
        const auto chain = index_.baseChain(script);
        for (const Script* level : chain) {
            if (iequals(level->name, ancestor)) {
                result = true;
                break;
            }
        }
    }
    inheritsCache_[key] = result;
    return result;
}

}
