#include <fo4/papyrus/resolver.hpp>

#include <cctype>
#include <string>

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

std::pair<std::string, std::string> splitStructName(const std::string& qualified) {
    const auto split = qualified.find_last_of(':');
    if (split == std::string::npos || split == 0) return {"", qualified};
    return {qualified.substr(0, split), qualified.substr(split + 1)};
}

}

Resolution Resolver::resolve(const Script* script) const {
    Resolution resolution;
    if (!script) return resolution;

    Context ctx;
    ctx.script = script;
    ctx.chain = index_.baseChain(script);

    if (script->extends && ctx.chain.size() < 2) ctx.baseChainComplete = false;
    const auto& last = ctx.chain.back();
    if (last->extends) ctx.baseChainComplete = false;

    for (const auto& import : script->imports) {
        const Script* imported = index_.resolve(import.name);
        if (!imported) {
            ctx.baseChainComplete = false;
        } else {
            ctx.imports.push_back(imported);
        }
    }

    resolveDeclarations(ctx);
    resolveBodies(ctx);

    resolution.script = script;
    resolution.bindings = std::move(ctx.bindings);
    resolution.types = std::move(ctx.types);
    resolution.typeRefs = std::move(ctx.typeRefs);
    resolution.diagnostics = std::move(ctx.diagnostics);
    resolution.baseChainComplete = ctx.baseChainComplete;
    return resolution;
}

void Resolver::resolveDeclarations(Context& ctx) const {
    for (const auto& s : ctx.script->structs) {
        for (const auto& m : s.members) typeOfRef(ctx, m.type);
    }
    for (const auto& v : ctx.script->variables) typeOfRef(ctx, v.type);
    for (const auto& p : ctx.script->properties) typeOfRef(ctx, p.type);
}

void Resolver::resolveBodies(Context& ctx) const {
    for (const auto& fn : ctx.script->functions) resolveCallable(ctx, fn, fn.isGlobal);
    for (const auto& ev : ctx.script->events) resolveCallable(ctx, ev, false);

    for (const auto& state : ctx.script->states) {
        for (const auto& fn : state.functions) resolveCallable(ctx, fn, fn.isGlobal);
        for (const auto& ev : state.events) resolveCallable(ctx, ev, false);
    }

    for (const auto& prop : ctx.script->properties) {
        if (prop.getter) resolveCallable(ctx, *prop.getter, false);
        if (prop.setter) resolveCallable(ctx, *prop.setter, false);
        if (prop.initializer) resolveExpression(ctx, prop.initializer.get());
    }
}

void Resolver::resolveCallable(Context& ctx, const CallableDecl& callable, bool isGlobal) const {
    ctx.selfType = isGlobal ? std::nullopt : std::optional<Type>(Type::object(ctx.script->name));
    ctx.scopes.clear();
    ctx.scopes.emplace_back();

    for (const auto& p : callable.parameters) {
        const Type* type = typeOfRef(ctx, p.type);
        Binding binding;
        binding.kind = BindingKind::Parameter;
        binding.name = p.name;
        binding.type = *type;
        binding.declaration = &p;
        binding.owner = ctx.script;
        ctx.scopes.back()[lower(p.name)] = std::move(binding);
        if (p.defaultValue) resolveExpression(ctx, p.defaultValue.get());
    }

    const auto* fn = dynamic_cast<const FunctionDecl*>(&callable);
    if (fn && fn->returnType) typeOfRef(ctx, *fn->returnType);

    resolveBlock(ctx, callable.body);
    ctx.scopes.clear();
    ctx.selfType = std::nullopt;
}

void Resolver::resolveBlock(Context& ctx, const std::vector<StatementPtr>& body) const {
    ctx.scopes.emplace_back();
    for (const auto& statement : body) resolveStatement(ctx, statement.get());
    ctx.scopes.pop_back();
}

void Resolver::resolveStatement(Context& ctx, const Statement* statement) const {
    switch (statement->nodeKind()) {
        case NodeKind::DefineStatement: {
            const auto* def = static_cast<const DefineStatement*>(statement);
            if (def->initializer) resolveExpression(ctx, def->initializer.get());
            const Type* type = typeOfRef(ctx, def->type);
            Binding binding;
            binding.kind = BindingKind::Local;
            binding.name = def->name;
            binding.type = *type;
            binding.declaration = nullptr;
            binding.owner = ctx.script;
            ctx.scopes.back()[lower(def->name)] = binding;
            ctx.bindings[def] = std::move(binding);
            break;
        }

        case NodeKind::AssignStatement: {
            const auto* assign = static_cast<const AssignStatement*>(statement);
            resolveExpression(ctx, assign->target.get());
            resolveExpression(ctx, assign->value.get());
            break;
        }

        case NodeKind::ExpressionStatement: {
            const auto* expr = static_cast<const ExpressionStatement*>(statement);
            resolveExpression(ctx, expr->expression.get());
            break;
        }

        case NodeKind::ReturnStatement: {
            const auto* ret = static_cast<const ReturnStatement*>(statement);
            if (ret->value) resolveExpression(ctx, ret->value.get());
            break;
        }

        case NodeKind::IfStatement: {
            const auto* iff = static_cast<const IfStatement*>(statement);
            for (const auto& branch : iff->branches) {
                resolveExpression(ctx, branch.condition.get());
                resolveBlock(ctx, branch.body);
            }
            if (!iff->elseBody.empty()) resolveBlock(ctx, iff->elseBody);
            break;
        }

        case NodeKind::WhileStatement: {
            const auto* wh = static_cast<const WhileStatement*>(statement);
            resolveExpression(ctx, wh->condition.get());
            resolveBlock(ctx, wh->body);
            break;
        }

        default:
            break;
    }
}

const Type* Resolver::resolveMemberTarget(Context& ctx, const Node* target) const {
    const auto* id = dynamic_cast<const IdentifierExpression*>(target);
    if (!id) return resolveExpression(ctx, target);

    const Binding* binding = lookupName(ctx, id->name, false);
    if (!binding) {
        ctx.baseChainComplete = false;
        ctx.types[id] = Type::error();
        return &Type::error();
    }

    ctx.bindings[id] = *binding;
    ctx.types[id] = binding->type;
    return &binding->type;
}

const Type* Resolver::resolveCallee(Context& ctx, const Node* callee) const {
    const auto* id = dynamic_cast<const IdentifierExpression*>(callee);
    if (!id) return resolveExpression(ctx, callee);

    const Binding* binding = lookupName(ctx, id->name, true);
    if (!binding) {
        reportUnresolved(ctx, id->name, id->span);
        ctx.types[id] = Type::error();
        return &Type::error();
    }

    ctx.bindings[id] = *binding;
    ctx.types[id] = binding->type;
    return &binding->type;
}

const Type* Resolver::resolveExpression(Context& ctx, const Node* expression) const {
    const Type* type = computeType(ctx, expression);
    ctx.types[expression] = *type;
    return &ctx.types[expression];
}

const Type* Resolver::computeType(Context& ctx, const Node* expression) const {
    const auto* expr = dynamic_cast<const Expression*>(expression);
    if (!expr) return &Type::error();
    switch (expression->nodeKind()) {
        case NodeKind::LiteralExpression: {
            const auto* lit = static_cast<const LiteralExpression*>(expression);
            switch (lit->kind) {
                case LiteralKind::Int: return &Type::int_();
                case LiteralKind::Float: return &Type::float_();
                case LiteralKind::String: return &Type::string_();
                case LiteralKind::Bool: return &Type::bool_();
                default: return &Type::none();
            }
        }

        case NodeKind::IdentifierExpression: {
            const auto* id = static_cast<const IdentifierExpression*>(expression);
            const Binding* binding = lookupName(ctx, id->name, false);
            if (!binding) {
                reportUnresolved(ctx, id->name, id->span);
                return &Type::error();
            }
            ctx.bindings[id] = *binding;
            return &binding->type;
        }

        case NodeKind::MemberExpression: {
            const auto* member = static_cast<const MemberExpression*>(expression);
            const Type* targetType = resolveMemberTarget(ctx, member->target.get());
            const Binding* binding =
                lookupMember(ctx, member->target.get(), *targetType, member->name);
            if (!binding) {
                reportUnknownMember(ctx, member->name, *targetType, member->nameSpan);
                return &Type::error();
            }
            ctx.bindings[member] = *binding;
            return &binding->type;
        }

        case NodeKind::IndexExpression: {
            const auto* index = static_cast<const IndexExpression*>(expression);
            const Type* targetType = resolveExpression(ctx, index->target.get());
            resolveExpression(ctx, index->index.get());
            if (targetType->isArray()) return targetType->element.get();
            return &Type::error();
        }

        case NodeKind::CallExpression: {
            const auto* call = static_cast<const CallExpression*>(expression);
            for (const auto& arg : call->arguments) resolveExpression(ctx, arg.value.get());
            const Type* calleeType = resolveCallee(ctx, call->callee.get());
            const auto it = ctx.bindings.find(call->callee.get());
            if (it != ctx.bindings.end()) ctx.bindings[call] = it->second;
            return calleeType;
        }

        case NodeKind::UnaryExpression: {
            const auto* unary = static_cast<const UnaryExpression*>(expression);
            const Type* operand = resolveExpression(ctx, unary->operand.get());
            return unary->op == TokenKind::Not ? &Type::bool_() : operand;
        }

        case NodeKind::BinaryExpression: {
            const auto* bin = static_cast<const BinaryExpression*>(expression);
            const Type* left = resolveExpression(ctx, bin->left.get());
            const Type* right = resolveExpression(ctx, bin->right.get());
            ctx.ownedTypes.push_back(binaryResult(bin->op, *left, *right));
            return &ctx.ownedTypes.back();
        }

        case NodeKind::CastExpression: {
            const auto* cast = static_cast<const CastExpression*>(expression);
            resolveExpression(ctx, cast->operand.get());
            return typeOfRef(ctx, cast->type);
        }

        case NodeKind::TypeCheckExpression: {
            const auto* check = static_cast<const TypeCheckExpression*>(expression);
            resolveExpression(ctx, check->operand.get());
            typeOfRef(ctx, check->type);
            return &Type::bool_();
        }

        case NodeKind::NewArrayExpression: {
            const auto* newArray = static_cast<const NewArrayExpression*>(expression);
            resolveExpression(ctx, newArray->size.get());
            ctx.ownedTypes.push_back(Type::arrayOf(*typeOfRef(ctx, newArray->elementType)));
            return &ctx.ownedTypes.back();
        }

        case NodeKind::NewStructExpression: {
            const auto* newStruct = static_cast<const NewStructExpression*>(expression);
            return typeOfRef(ctx, newStruct->type);
        }

        default:
            return &Type::error();
    }
}

Type Resolver::binaryResult(TokenKind op, const Type& left, const Type& right) {
    switch (op) {
        case TokenKind::Equal:
        case TokenKind::NotEqual:
        case TokenKind::Less:
        case TokenKind::LessEqual:
        case TokenKind::Greater:
        case TokenKind::GreaterEqual:
        case TokenKind::And:
        case TokenKind::Or:
            return Type::bool_();
        default:
            break;
    }

    if (left.kind == TypeKind::String || right.kind == TypeKind::String) {
        return op == TokenKind::Plus ? Type::string_() : Type::error();
    }
    if (left.kind == TypeKind::Float || right.kind == TypeKind::Float) return Type::float_();
    if (left.kind == TypeKind::Int && right.kind == TypeKind::Int) return Type::int_();
    return left.kind == TypeKind::Error ? right : left;
}

const Binding* Resolver::lookupName(Context& ctx, const std::string& name,
                                    bool inCallPosition) const {
    if (!inCallPosition) {
        for (auto it = ctx.scopes.rbegin(); it != ctx.scopes.rend(); ++it) {
            const auto found = it->find(lower(name));
            if (found != it->end()) return &found->second;
        }
    }

    if (ctx.selfType) {
        if (iequals(name, "self")) {
            ctx.ownedBindings.push_back(Binding{BindingKind::SelfKeyword, "Self", *ctx.selfType,
                                                ctx.script, ctx.script});
            return &ctx.ownedBindings.back();
        }

        if (iequals(name, "parent") && ctx.chain.size() > 1) {
            const Script* parent = ctx.chain[1];
            ctx.ownedBindings.push_back(
                Binding{BindingKind::ParentKeyword, "Parent", Type::object(parent->name), parent, parent});
            return &ctx.ownedBindings.back();
        }
    }

    const Binding* member = lookupOnChain(ctx, ctx.chain, name, ctx.selfType == std::nullopt);
    if (member) return member;

    for (const Script* import : ctx.imports) {
        const auto chain = index_.baseChain(import);
        const Binding* hit = lookupOnChain(ctx, chain, name, true);
        if (hit) return hit;
    }

    const Script* script = index_.resolve(name);
    if (script) {
        ctx.ownedBindings.push_back(
            Binding{BindingKind::Script, script->name, Type::object(script->name), script, script});
        return &ctx.ownedBindings.back();
    }

    return nullptr;
}

const Binding* Resolver::lookupOnChain(Context& ctx, const std::vector<const Script*>& chain,
                                       const std::string& name, bool globalsOnly) const {
    for (const Script* level : chain) {
        const Declaration* decl = ScriptIndex::findMemberOn(level, name);
        if (!decl) continue;
        if (globalsOnly) {
            const auto* fn = dynamic_cast<const FunctionDecl*>(decl);
            if (!fn || !fn->isGlobal) continue;
        }
        return bindingFor(ctx, decl, level);
    }
    return nullptr;
}

const Binding* Resolver::lookupMember(Context& ctx, const Node* target,
                                      const Type& targetType, const std::string& name) const {
    const auto targetIt = ctx.bindings.find(target);
    if (targetIt != ctx.bindings.end() && targetIt->second.kind == BindingKind::Script &&
        targetIt->second.owner != nullptr) {
        return lookupOnChain(ctx, index_.baseChain(targetIt->second.owner), name, false);
    }

    switch (targetType.kind) {
        case TypeKind::Array: {
            auto member = arrayMember(targetType, name);
            if (!member) return nullptr;
            ctx.ownedBindings.push_back(std::move(*member));
            return &ctx.ownedBindings.back();
        }

        case TypeKind::Object: {
            const Script* script = index_.resolve(targetType.name);
            if (!script) {
                ctx.baseChainComplete = false;
                return nullptr;
            }
            return lookupOnChain(ctx, index_.baseChain(script), name, false);
        }

        case TypeKind::Struct: {
            const auto [ownerName, structName] = splitStructName(targetType.name);
            const Script* owner = index_.resolve(ownerName);
            if (!owner) {
                ctx.baseChainComplete = false;
                return nullptr;
            }
            const StructDecl* structDecl = nullptr;
            for (const auto& s : owner->structs) {
                if (iequals(s.name, structName)) {
                    structDecl = &s;
                    break;
                }
            }
            if (!structDecl) {
                ctx.baseChainComplete = false;
                return nullptr;
            }
            const VariableDecl* field = nullptr;
            for (const auto& m : structDecl->members) {
                if (iequals(m.name, name)) {
                    field = &m;
                    break;
                }
            }
            if (!field) return nullptr;
            Binding binding;
            binding.kind = BindingKind::StructMember;
            binding.name = field->name;
            binding.type = *typeOfRef(ctx, field->type);
            binding.declaration = field;
            binding.owner = owner;
            ctx.ownedBindings.push_back(std::move(binding));
            return &ctx.ownedBindings.back();
        }

        case TypeKind::Var:
        case TypeKind::Error:
        default:
            return nullptr;
    }
}

std::optional<Binding> Resolver::arrayMember(const Type& arrayType, const std::string& name) {
    const auto& element = *arrayType.element;
    const auto key = lower(name);

    Binding binding;
    binding.kind = BindingKind::ArrayMember;
    if (key == "length") {
        binding.name = "Length";
        binding.type = Type::int_();
    } else if (key == "find") {
        binding.name = "Find";
        binding.type = Type::int_();
    } else if (key == "rfind") {
        binding.name = "RFind";
        binding.type = Type::int_();
    } else if (key == "findstruct") {
        binding.name = "FindStruct";
        binding.type = Type::int_();
    } else if (key == "rfindstruct") {
        binding.name = "RFindStruct";
        binding.type = Type::int_();
    } else if (key == "add") {
        binding.name = "Add";
        binding.type = Type::none();
    } else if (key == "insert") {
        binding.name = "Insert";
        binding.type = Type::none();
    } else if (key == "remove") {
        binding.name = "Remove";
        binding.type = Type::none();
    } else if (key == "removelast") {
        binding.name = "RemoveLast";
        binding.type = Type::none();
    } else if (key == "clear") {
        binding.name = "Clear";
        binding.type = Type::none();
    } else if (key == "getmatchingstructs") {
        binding.name = "GetMatchingStructs";
        binding.type = Type::arrayOf(element);
    } else {
        return std::nullopt;
    }
    return binding;
}

const Binding* Resolver::bindingFor(Context& ctx, const Declaration* declaration,
                                    const Script* owner) const {
    Binding binding;
    binding.declaration = declaration;
    binding.owner = owner;
    binding.name = declaration->name;

    switch (declaration->nodeKind()) {
        case NodeKind::PropertyDecl: {
            const auto* p = static_cast<const PropertyDecl*>(declaration);
            binding.kind = BindingKind::Property;
            binding.type = *typeOfRef(ctx, p->type, owner);
            break;
        }
        case NodeKind::VariableDecl: {
            const auto* v = static_cast<const VariableDecl*>(declaration);
            binding.kind = BindingKind::ScriptVariable;
            binding.type = *typeOfRef(ctx, v->type, owner);
            break;
        }
        case NodeKind::FunctionDecl: {
            const auto* f = static_cast<const FunctionDecl*>(declaration);
            binding.kind = BindingKind::Function;
            binding.type = f->returnType ? *typeOfRef(ctx, *f->returnType, owner) : Type::none();
            break;
        }
        case NodeKind::EventDecl:
            binding.kind = BindingKind::Event;
            binding.type = Type::none();
            break;
        case NodeKind::StructDecl: {
            const auto* s = static_cast<const StructDecl*>(declaration);
            binding.kind = BindingKind::Struct;
            binding.type = Type::structOf(owner->name, s->name);
            break;
        }
        case NodeKind::CustomEventDecl:
            binding.kind = BindingKind::CustomEvent;
            binding.type = Type::string_();
            break;
        default:
            binding.kind = BindingKind::ScriptVariable;
            binding.type = Type::error();
            break;
    }

    ctx.ownedBindings.push_back(std::move(binding));
    return &ctx.ownedBindings.back();
}

const Type* Resolver::typeOfRef(Context& ctx, const TypeRef& reference,
                                const Script* relativeTo) const {
    std::optional<Type> resolved =
        resolveTypeName(index_, reference.name, relativeTo ? relativeTo : ctx.script);
    if (!resolved) {
        ctx.baseChainComplete = false;
        resolved = Type::error();
    }
    const Type& final =
        reference.isArray ? ctx.ownedTypes.emplace_back(Type::arrayOf(*resolved)) : *resolved;
    ctx.typeRefs[&reference] = final;
    return &ctx.typeRefs[&reference];
}

std::optional<Type> Resolver::resolveTypeName(ScriptIndex& index, const std::string& name,
                                              const Script* relativeTo) {
    if (const Type* primitive = Type::primitive(name)) return *primitive;

    if (relativeTo) {
        for (const Script* level : index.baseChain(relativeTo)) {
            for (const auto& s : level->structs) {
                if (iequals(s.name, name)) return Type::structOf(level->name, s.name);
            }
        }
    }

    const auto split = name.find_last_of(':');
    if (split != std::string::npos && split > 0) {
        const auto ownerName = name.substr(0, split);
        const auto structName = name.substr(split + 1);
        const Script* owner = index.resolve(ownerName);
        if (owner) {
            for (const auto& s : owner->structs) {
                if (iequals(s.name, structName)) return Type::structOf(owner->name, structName);
            }
        }
    }

    const Script* script = index.resolve(name);
    if (script) return Type::object(script->name);

    return std::nullopt;
}

void Resolver::reportUnresolved(Context& ctx, const std::string& name, const Span& span) {
    if (!ctx.baseChainComplete) return;
    ctx.diagnostics.push_back(Diagnostic{
        DiagnosticCode::UnresolvedName, Severity::Error,
        "'" + name + "' is not defined.", span, ctx.script->filePath});
}

void Resolver::reportUnknownMember(Context& ctx, const std::string& name, const Type& targetType,
                                   const Span& span) {
    if (!ctx.baseChainComplete) return;
    if (targetType.kind == TypeKind::Error || targetType.kind == TypeKind::Var) return;
    ctx.diagnostics.push_back(Diagnostic{
        DiagnosticCode::UnknownMember, Severity::Error,
        "'" + targetType.toString() + "' has no member named '" + name + "'.", span,
        ctx.script->filePath});
}

}
