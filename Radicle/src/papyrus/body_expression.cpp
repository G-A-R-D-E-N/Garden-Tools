#include "body_emitter.hpp"
#include "codegen_detail.hpp"

#include <optional>
#include <string>

namespace fo4::papyrus {

using codegen_detail::iequals;
using fo4::pex::PexValue;
using fo4::pex::PexValueType;

PexValue BodyEmitter::value(const Node* expression) {
    const auto direct = tryDirectValue(expression);
    if (direct) return *direct;

    const Type& type = gen_.typeOf(expression);
    if (type.kind == TypeKind::Error) {
        cannotEmit(*expression);
        return noneValue();
    }

    const auto temp = allocTemp(type);
    emitComputation(expression, temp, type);
    return id(temp);
}

PexValue BodyEmitter::valueAs(const Node* expression, const Type& want) {
    const Type& natural = gen_.typeOf(expression);
    if (natural == want || needsNoCast(natural, want)) return value(expression);

    if (natural.kind == TypeKind::Int && want.kind == TypeKind::Float) {
        const auto folded = CodeGenerator::tryConstant(expression, &want);
        if (folded && folded->type == PexValueType::Float) return *folded;
    }

    if (want.kind == TypeKind::Bool &&
        (natural.kind == TypeKind::Int || natural.kind == TypeKind::Float)) {
        const auto folded = CodeGenerator::tryConstant(expression, &want);
        if (folded && folded->type == PexValueType::Bool) return *folded;
    }

    const auto source = value(expression);
    const auto temp = allocTemp(want);
    add("cast", {id(temp), source});
    return id(temp);
}

bool BodyEmitter::needsNoCast(const Type& from, const Type& to) {
    return (from.kind == TypeKind::None && to.isReference()) || from.kind == TypeKind::Error ||
           to.kind == TypeKind::Error;
}

void BodyEmitter::emitInto(const Node* expression, const std::string& destination,
                           const Type& destinationType) {
    const Type& natural = gen_.typeOf(expression);

    if (natural == destinationType) {
        const auto direct = tryDirectValue(expression);
        if (direct) {
            add("assign", {id(destination), *direct});
            return;
        }
        emitComputation(expression, destination, destinationType);
        return;
    }

    if (needsNoCast(natural, destinationType)) {
        add("assign", {id(destination), value(expression)});
        return;
    }

    if (natural.kind == TypeKind::Int && destinationType.kind == TypeKind::Float) {
        const auto folded = CodeGenerator::tryConstant(expression, &destinationType);
        if (folded && folded->type == PexValueType::Float) {
            add("assign", {id(destination), *folded});
            return;
        }
    }

    add("cast", {id(destination), value(expression)});
}

std::optional<PexValue> BodyEmitter::tryDirectValue(const Node* expression) {
    if (expression->nodeKind() == NodeKind::LiteralExpression) {
        return CodeGenerator::tryConstant(expression, &gen_.typeOf(expression));
    }

    if (expression->nodeKind() == NodeKind::IdentifierExpression) {
        const auto* idNode = static_cast<const IdentifierExpression*>(expression);
        const auto storage = storageFor(*idNode);
        if (!storage) return std::nullopt;
        if (storage->isProperty) return std::nullopt;
        return id(storage->name);
    }

    return std::nullopt;
}

std::optional<BodyEmitter::Storage> BodyEmitter::storageFor(const IdentifierExpression& idNode) {
    const Binding* binding = gen_.resolution_->bindingFor(&idNode);
    if (!binding) {
        cannotEmit(idNode);
        return std::nullopt;
    }

    switch (binding->kind) {
        case BindingKind::Local:
        case BindingKind::Parameter: {
            const auto local = findLocal(idNode.name);
            if (!local) {
                cannotEmit(idNode);
                return std::nullopt;
            }
            return Storage{local->name, false, std::nullopt};
        }

        case BindingKind::ScriptVariable:
            return Storage{binding->name, false, std::nullopt};

        case BindingKind::SelfKeyword:
            return Storage{"self", false, std::nullopt};

        case BindingKind::Property: {
            if (isAutoBackingProperty(*binding)) {
                return Storage{CodeGenerator::backingVarName(binding->name), false, std::nullopt};
            }
            return Storage{binding->name, true, id("self")};
        }

        default:
            cannotEmit(idNode);
            return std::nullopt;
    }
}

PexValue BodyEmitter::propertyGet(const Storage& storage, const Node* at) {
    const auto temp = allocTemp(gen_.typeOf(at));
    add("propget", {id(storage.name), *storage.receiver, id(temp)});
    return id(temp);
}

PexValue BodyEmitter::materialise(const PexValue& value, const Type& type) {
    if (value.type == PexValueType::Identifier && value.str.rfind("::temp", 0) == 0) return value;
    const auto temp = allocTemp(type);
    add("assign", {id(temp), value});
    return id(temp);
}

void BodyEmitter::emitComputation(const Node* expression, const std::string& destination,
                                  const Type& destinationType) {
    switch (expression->nodeKind()) {
        case NodeKind::LiteralExpression:
        case NodeKind::IdentifierExpression: {
            const auto direct = tryDirectValue(expression);
            if (direct) {
                add("assign", {id(destination), *direct});
                return;
            }
            if (expression->nodeKind() == NodeKind::IdentifierExpression) {
                const auto* idNode = static_cast<const IdentifierExpression*>(expression);
                const auto storage = storageFor(*idNode);
                if (storage && storage->isProperty) {
                    add("propget", {id(storage->name), *storage->receiver, id(destination)});
                    return;
                }
            }
            cannotEmit(*expression);
            return;
        }

        case NodeKind::MemberExpression:
            emitMemberRead(*static_cast<const MemberExpression*>(expression), destination);
            return;

        case NodeKind::IndexExpression: {
            const auto* index = static_cast<const IndexExpression*>(expression);
            add("array_getelement",
                {id(destination), value(index->target.get()),
                 valueAs(index->index.get(), Type::int_())});
            return;
        }

        case NodeKind::CallExpression:
            emitCall(*static_cast<const CallExpression*>(expression), destination);
            return;

        case NodeKind::UnaryExpression:
            emitUnary(*static_cast<const UnaryExpression*>(expression), destination, destinationType);
            return;

        case NodeKind::BinaryExpression:
            emitBinary(*static_cast<const BinaryExpression*>(expression), destination,
                       destinationType);
            return;

        case NodeKind::CastExpression: {
            const auto* cast = static_cast<const CastExpression*>(expression);
            const Type& natural = gen_.typeOf(cast->operand.get());
            const Type& target = gen_.typeOf(cast->type);
            if (natural == target || needsNoCast(natural, target)) {
                add("assign", {id(destination), value(cast->operand.get())});
                return;
            }
            add("cast", {id(destination), value(cast->operand.get())});
            return;
        }

        case NodeKind::TypeCheckExpression: {
            const auto* check = static_cast<const TypeCheckExpression*>(expression);
            add("is", {id(destination), value(check->operand.get()),
                       id(gen_.pexTypeName(gen_.typeOf(check->type), check->type.span))});
            return;
        }

        case NodeKind::NewArrayExpression: {
            const auto* newArray = static_cast<const NewArrayExpression*>(expression);
            add("array_create", {id(destination), valueAs(newArray->size.get(), Type::int_())});
            return;
        }

        case NodeKind::NewStructExpression:
            add("struct_create", {id(destination)});
            return;

        default:
            cannotEmit(*expression);
            return;
    }
}

void BodyEmitter::emitMemberRead(const MemberExpression& member, const std::string& destination) {
    const Binding* binding = gen_.resolution_->bindingFor(&member);
    if (!binding) {
        cannotEmit(member);
        return;
    }

    switch (binding->kind) {
        case BindingKind::ArrayMember:
            if (iequals(member.name, "length")) {
                add("array_length", {id(destination), value(member.target.get())});
                return;
            }
            cannotEmit(member);
            return;

        case BindingKind::StructMember:
            add("struct_get", {id(destination), value(member.target.get()), id(member.name)});
            return;

        case BindingKind::Property: {
            if (isOwnBackingProperty(member, *binding)) {
                add("assign", {id(destination), id(CodeGenerator::backingVarName(binding->name))});
                return;
            }
            add("propget", {id(binding->name), value(member.target.get()), id(destination)});
            return;
        }

        default:
            cannotEmit(member);
            return;
    }
}

bool BodyEmitter::isOwnBackingProperty(const MemberExpression& member, const Binding& binding) {
    if (!isAutoBackingProperty(binding)) return false;
    const Binding* targetBinding = gen_.resolution_->bindingFor(member.target.get());
    return targetBinding && targetBinding->kind == BindingKind::SelfKeyword;
}

bool BodyEmitter::isAutoBackingProperty(const Binding& binding) {
    const auto* prop = dynamic_cast<const PropertyDecl*>(binding.declaration);
    return prop && prop->kind == PropertyKind::Auto;
}

void BodyEmitter::emitUnary(const UnaryExpression& unary, const std::string& destination,
                            const Type&) {
    if (unary.op == TokenKind::Not) {
        add("not", {id(destination), value(unary.operand.get())});
        return;
    }

    const Type& operandType = gen_.typeOf(unary.operand.get());
    std::optional<std::string> mnemonic;
    if (operandType.kind == TypeKind::Float) mnemonic = "fneg";
    else if (operandType.kind == TypeKind::Int) mnemonic = "ineg";
    if (!mnemonic) {
        cannotEmit(unary);
        return;
    }
    add(*mnemonic, {id(destination), valueAs(unary.operand.get(), operandType)});
}

void BodyEmitter::emitBinary(const BinaryExpression& binary, const std::string& destination,
                             const Type&) {
    if (binary.op == TokenKind::And || binary.op == TokenKind::Or) {
        emitShortCircuit(binary, destination);
        return;
    }

    if (isComparison(binary.op)) {
        emitComparison(binary, destination);
        return;
    }

    const Type& result = gen_.typeOf(&binary);
    const auto left = valueAs(binary.left.get(), result);
    const auto right = valueAs(binary.right.get(), result);
    binaryInto(destination, result, binary.op, left, right, binary.span);
}

PexValue BodyEmitter::binaryInto(const std::optional<std::string>& destination, const Type& type,
                                 TokenKind op, const PexValue& left, const PexValue& right,
                                 const Span& span) {
    const auto mnemonic = arithmeticMnemonic(op, type);
    if (!mnemonic) {
        gen_.report(DiagnosticCode::CannotEmit,
                    "There is no instruction for this operator on '" + type.toString() + "'.", span);
        return noneValue();
    }

    const auto slot = destination ? *destination : allocTemp(type);
    add(*mnemonic, {id(slot), left, right});
    return id(slot);
}

std::optional<std::string> BodyEmitter::arithmeticMnemonic(TokenKind op, const Type& type) {
    if (type.kind == TypeKind::String) return op == TokenKind::Plus ? std::optional<std::string>("strcat") : std::nullopt;

    const bool isFloat = type.kind == TypeKind::Float;
    if (type.kind != TypeKind::Int && !isFloat) return std::nullopt;

    switch (op) {
        case TokenKind::Plus: return isFloat ? "fadd" : "iadd";
        case TokenKind::Minus: return isFloat ? "fsub" : "isub";
        case TokenKind::Star: return isFloat ? "fmul" : "imul";
        case TokenKind::Slash: return isFloat ? "fdiv" : "idiv";
        case TokenKind::Percent: return isFloat ? std::nullopt : std::optional<std::string>("imod");
        default: return std::nullopt;
    }
}

bool BodyEmitter::isComparison(TokenKind op) {
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

void BodyEmitter::emitComparison(const BinaryExpression& binary, const std::string& destination) {
    const auto common = commonComparisonType(gen_.typeOf(binary.left.get()),
                                             gen_.typeOf(binary.right.get()));

    const auto left = valueAs(binary.left.get(), common);
    const auto right = valueAs(binary.right.get(), common);

    std::string mnemonic;
    switch (binary.op) {
        case TokenKind::Equal:
        case TokenKind::NotEqual: mnemonic = "cmp_eq"; break;
        case TokenKind::Less: mnemonic = "cmp_lt"; break;
        case TokenKind::LessEqual: mnemonic = "cmp_lte"; break;
        case TokenKind::Greater: mnemonic = "cmp_gt"; break;
        default: mnemonic = "cmp_gte"; break;
    }

    add(mnemonic, {id(destination), left, right});
    if (binary.op == TokenKind::NotEqual) add("not", {id(destination), id(destination)});
}

Type BodyEmitter::commonComparisonType(const Type& left, const Type& right) {
    if (left == right) return left;
    if (left.kind == TypeKind::Error) return right;
    if (right.kind == TypeKind::Error) return left;
    if (left.kind == TypeKind::None) return right;
    if (right.kind == TypeKind::None) return left;
    if (left.kind == TypeKind::Float || right.kind == TypeKind::Float) return Type::float_();
    if (left.kind == TypeKind::String || right.kind == TypeKind::String) return Type::string_();
    if (left.kind == TypeKind::Var || right.kind == TypeKind::Var) return Type::var_();

    if (left.kind == TypeKind::Object && right.kind == TypeKind::Object) {
        if (inherits(right.name, left.name)) return left;
        if (inherits(left.name, right.name)) return right;
        return left;
    }

    return left;
}

bool BodyEmitter::inherits(const std::string& child, const std::string& ancestor) {
    const Script* script = gen_.index_.resolve(child);
    if (!script) return false;
    for (const Script* level : gen_.index_.baseChain(script)) {
        if (iequals(level->name, ancestor)) return true;
    }
    return false;
}

bool BodyEmitter::readsName(const Node* expression, const std::string& destination) {
    if (!expression) return false;
    switch (expression->nodeKind()) {
        case NodeKind::IdentifierExpression: {
            const auto* idNode = static_cast<const IdentifierExpression*>(expression);
            const Binding* binding = gen_.resolution_->bindingFor(idNode);
            if (!binding) return false;
            const bool namedStorage =
                binding->kind == BindingKind::Local || binding->kind == BindingKind::Parameter ||
                binding->kind == BindingKind::ScriptVariable ||
                (binding->kind == BindingKind::Property && isAutoBackingProperty(*binding));
            if (!namedStorage) return false;
            const auto storage = storageFor(*idNode);
            return storage && storage->name == destination;
        }

        case NodeKind::MemberExpression: {
            const auto* member = static_cast<const MemberExpression*>(expression);
            const Binding* binding = gen_.resolution_->bindingFor(member);
            if (binding && binding->kind == BindingKind::Property &&
                isOwnBackingProperty(*member, *binding) &&
                CodeGenerator::backingVarName(binding->name) == destination) {
                return true;
            }
            return readsName(member->target.get(), destination);
        }

        case NodeKind::IndexExpression: {
            const auto* index = static_cast<const IndexExpression*>(expression);
            return readsName(index->target.get(), destination) ||
                   readsName(index->index.get(), destination);
        }

        case NodeKind::CallExpression: {
            const auto* call = static_cast<const CallExpression*>(expression);
            if (readsName(call->callee.get(), destination)) return true;
            for (const auto& argument : call->arguments) {
                if (readsName(argument.value.get(), destination)) return true;
            }
            return false;
        }

        case NodeKind::UnaryExpression:
            return readsName(static_cast<const UnaryExpression*>(expression)->operand.get(),
                             destination);

        case NodeKind::BinaryExpression: {
            const auto* binary = static_cast<const BinaryExpression*>(expression);
            return readsName(binary->left.get(), destination) ||
                   readsName(binary->right.get(), destination);
        }

        case NodeKind::CastExpression:
            return readsName(static_cast<const CastExpression*>(expression)->operand.get(),
                             destination);

        case NodeKind::TypeCheckExpression:
            return readsName(static_cast<const TypeCheckExpression*>(expression)->operand.get(),
                             destination);

        case NodeKind::NewArrayExpression:
            return readsName(static_cast<const NewArrayExpression*>(expression)->size.get(),
                             destination);

        default:
            return false;
    }
}

void BodyEmitter::emitShortCircuit(const BinaryExpression& binary, const std::string& destination) {
    const bool aliased = readsName(binary.right.get(), destination);
    const auto slot = aliased ? allocTemp(gen_.typeOf(&binary)) : destination;
    add("cast", {id(slot), value(binary.left.get())});
    const int skip = add(binary.op == TokenKind::And ? "jmpf" : "jmpt", {id(slot), intValue(0)});
    add("cast", {id(slot), value(binary.right.get())});
    patchJump(skip, here());
    if (aliased) add("assign", {id(destination), id(slot)});
}

}  // namespace fo4::papyrus
