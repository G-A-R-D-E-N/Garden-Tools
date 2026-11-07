#include "body_emitter.hpp"
#include "codegen_detail.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fo4::papyrus {

using codegen_detail::lower;
using codegen_detail::iequals;
using fo4::pex::PexValue;

void BodyEmitter::emitCall(const CallExpression& call, const std::string& destination) {
    const Binding* binding = gen_.resolution_->bindingFor(&call);
    if (!binding) binding = gen_.resolution_->bindingFor(call.callee.get());
    if (!binding) {
        cannotEmit(call);
        return;
    }

    const auto excluded = gen_.excludedBy(*binding);
    if (excluded) {
        gen_.report(DiagnosticCode::CannotEmit,
                    "'" + binding->name + "' is " + *excluded +
                        " and is not part of this build, but its result is used here. Only a call "
                        "written as a whole statement can be dropped; there is no value to "
                        "substitute for this one.",
                    call.span);
        return;
    }

    if (binding->kind == BindingKind::ArrayMember) {
        emitArrayBuiltin(call, *binding, destination);
        return;
    }

    const auto* callee = dynamic_cast<const CallableDecl*>(binding->declaration);
    if (!callee) {
        gen_.report(DiagnosticCode::UnknownCallTarget,
                    "'" + binding->name +
                        "' has no source declaration on the import roots, so its parameter list "
                        "and defaults are unknown and the call cannot be emitted. Add the root "
                        "that declares it.",
                    call.span);
        return;
    }

    if (call.callee->nodeKind() == NodeKind::MemberExpression) {
        const auto* member = static_cast<const MemberExpression*>(call.callee.get());
        const Binding* targetBinding = gen_.resolution_->bindingFor(member->target.get());

        if (targetBinding && targetBinding->kind == BindingKind::ParentKeyword) {
            const auto parentArgs = buildArguments(call, *callee, binding->owner);
            if (!parentArgs) return;
            std::vector<PexValue> parts = {id(binding->name), id(destination)};
            parts.insert(parts.end(), parentArgs->begin(), parentArgs->end());
            emit("callparent", parts);
            return;
        }

        if (targetBinding && targetBinding->kind == BindingKind::Script) {
            const auto staticArgs = buildArguments(call, *callee, binding->owner);
            if (!staticArgs) return;
            std::vector<PexValue> parts = {
                id(targetBinding->owner ? targetBinding->owner->name : targetBinding->name),
                id(binding->name), id(destination)};
            parts.insert(parts.end(), staticArgs->begin(), staticArgs->end());
            emit("callstatic", parts);
            return;
        }

        const auto receiver = value(member->target.get());
        const auto methodArgs = buildArguments(call, *callee, binding->owner);
        if (!methodArgs) return;
        std::vector<PexValue> parts = {id(binding->name), receiver, id(destination)};
        parts.insert(parts.end(), methodArgs->begin(), methodArgs->end());
        emit("callmethod", parts);
        return;
    }

    const auto arguments = buildArguments(call, *callee, binding->owner);
    if (!arguments) return;

    const auto* fn = dynamic_cast<const FunctionDecl*>(callee);
    if (fn && fn->isGlobal) {
        std::vector<PexValue> parts = {
            id(binding->owner ? binding->owner->name : gen_.script_->name), id(binding->name),
            id(destination)};
        parts.insert(parts.end(), arguments->begin(), arguments->end());
        emit("callstatic", parts);
        return;
    }

    std::vector<PexValue> parts = {id(binding->name), id("self"), id(destination)};
    parts.insert(parts.end(), arguments->begin(), arguments->end());
    emit("callmethod", parts);
}

void BodyEmitter::emit(const std::string& mnemonic, const std::vector<PexValue>& parts) {
    add(mnemonic, parts);
}

std::optional<std::vector<PexValue>> BodyEmitter::buildArguments(const CallExpression& call,
                                                                 const CallableDecl& callee,
                                                                 const Script* calleeOwner) {
    std::vector<const Node*> slots(callee.parameters.size(), nullptr);
    std::size_t positional = 0;
    bool named = false;

    for (const auto& argument : call.arguments) {
        if (!argument.name) {
            if (named) {
                gen_.report(DiagnosticCode::ParameterOrder,
                            "A positional argument cannot follow a named one.", argument.span);
                return std::nullopt;
            }
            if (positional >= slots.size()) {
                gen_.report(DiagnosticCode::ArgumentCount,
                            "'" + callee.name + "' takes " + std::to_string(slots.size()) +
                                " argument(s).",
                            argument.span);
                return std::nullopt;
            }
            slots[positional++] = argument.value.get();
            continue;
        }

        named = true;
        std::size_t at = callee.parameters.size();
        for (std::size_t i = 0; i < callee.parameters.size(); ++i) {
            if (iequals(callee.parameters[i].name, *argument.name)) {
                at = i;
                break;
            }
        }
        if (at == callee.parameters.size()) {
            gen_.report(DiagnosticCode::UnknownArgumentName,
                        "'" + callee.name + "' has no parameter named '" + *argument.name + "'.",
                        argument.nameSpan);
            return std::nullopt;
        }
        slots[at] = argument.value.get();
    }

    std::optional<std::string> receiverScript;
    if (call.callee->nodeKind() == NodeKind::MemberExpression) {
        const auto* member = static_cast<const MemberExpression*>(call.callee.get());
        const Type& receiver = gen_.typeOf(member->target.get());
        if (receiver.kind == TypeKind::Object) receiverScript = receiver.name;
    }
    if (!receiverScript) receiverScript = gen_.script_->name;

    std::vector<PexValue> operands;
    operands.reserve(slots.size());
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const auto& parameter = callee.parameters[i];
        const auto parameterType = gen_.foreignType(parameter.type, calleeOwner);

        if (slots[i]) {
            if (parameter.semantic() == ParameterSemantic::CustomEventName) {
                if (const auto qualified =
                        gen_.qualifyCustomEvent(*slots[i], slots, static_cast<int>(i),
                                                receiverScript ? &*receiverScript : nullptr)) {
                    operands.push_back(*qualified);
                    continue;
                }
            }
            operands.push_back(valueAs(slots[i], parameterType));
            continue;
        }

        if (!parameter.defaultValue) {
            gen_.report(DiagnosticCode::ArgumentCount,
                        "'" + callee.name + "' has no default for '" + parameter.name +
                            "', so it must be given.",
                        call.span);
            return std::nullopt;
        }

        const auto constant = CodeGenerator::tryConstant(parameter.defaultValue.get(), &parameterType);
        if (!constant) {
            gen_.report(DiagnosticCode::NonConstantInitializer,
                        "The default for '" + parameter.name +
                            "' is not a literal, so it cannot be materialised at the call site.",
                        call.span);
            return std::nullopt;
        }
        operands.push_back(*constant);
    }

    return operands;
}

namespace {
const std::unordered_map<std::string, std::pair<int, int>>& arrayBuiltinArity() {
    static const std::unordered_map<std::string, std::pair<int, int>> arity = {
        {"find", {1, 2}},       {"rfind", {1, 2}},       {"findstruct", {2, 3}},
        {"rfindstruct", {2, 3}}, {"add", {1, 2}},        {"insert", {2, 2}},
        {"remove", {1, 2}},     {"removelast", {0, 0}},  {"clear", {0, 0}},
    };
    return arity;
}
}  // namespace

void BodyEmitter::emitArrayBuiltin(const CallExpression& call, const Binding& binding,
                                   const std::string& destination) {
    const auto* member = dynamic_cast<const MemberExpression*>(call.callee.get());
    if (!member) {
        cannotEmit(call);
        return;
    }

    std::vector<const Node*> arguments;
    for (const auto& argument : call.arguments) arguments.push_back(argument.value.get());

    const auto arity = arrayBuiltinArity().find(lower(binding.name));
    if (arity == arrayBuiltinArity().end()) {
        gen_.report(DiagnosticCode::CannotEmit,
                    "The array built-in '" + binding.name +
                        "' has no Fallout 4 opcode, so there is no instruction to emit for it.",
                    call.span);
        return;
    }

    if (arguments.size() < static_cast<std::size_t>(arity->second.first) ||
        arguments.size() > static_cast<std::size_t>(arity->second.second)) {
        std::string wanted = arity->second.first == arity->second.second
                                 ? std::to_string(arity->second.first)
                                 : std::to_string(arity->second.first) + " to " +
                                       std::to_string(arity->second.second);
        gen_.report(DiagnosticCode::ArgumentCount,
                    "'" + binding.name + "' on an array takes " + wanted +
                        " argument(s), not " + std::to_string(arguments.size()) +
                        ". Array built-ins are declared in no script, so this is not caught "
                        "earlier.",
                    call.span);
        return;
    }

    const Type& arrayType = gen_.typeOf(member->target.get());
    const Type& element = arrayType.element ? *arrayType.element : Type::error();
    const auto array = value(member->target.get());

    const auto argumentAt = [&](std::size_t at, const Type& type, int fallback) {
        return at < arguments.size() ? valueAs(arguments[at], type) : intValue(fallback);
    };

    const auto memberType = [&]() -> Type {
        const auto* name = dynamic_cast<const LiteralExpression*>(arguments[0]);
        const auto split = element.name.find_last_of(':');
        if (!name || name->kind != LiteralKind::String || element.kind != TypeKind::Struct ||
            split == std::string::npos) {
            return Type::error();
        }
        const Script* owner = gen_.index_.resolve(element.name.substr(0, split));
        if (!owner) return Type::error();
        for (const auto& structDecl : owner->structs) {
            if (!iequals(structDecl.name, element.name.substr(split + 1))) continue;
            for (const auto& field : structDecl.members) {
                if (iequals(field.name, name->text)) return gen_.foreignType(field.type, owner);
            }
        }
        return Type::error();
    };

    const auto key = lower(binding.name);
    if (key == "findstruct" || key == "rfindstruct") {
        const auto* name = dynamic_cast<const LiteralExpression*>(arguments[0]);
        if (!name || name->kind != LiteralKind::String) {
            gen_.report(DiagnosticCode::CannotEmit,
                        "'" + binding.name +
                            "' needs the member name as a string literal, so the member type is "
                            "known and the search value can be converted to it.",
                        call.span);
            return;
        }
    }
    if (key == "find") {
        add("array_findelement",
            {array, id(destination), valueAs(arguments[0], element), argumentAt(1, Type::int_(), 0)});
        return;
    }
    if (key == "rfind") {
        add("array_rfindelement",
            {array, id(destination), valueAs(arguments[0], element), argumentAt(1, Type::int_(), -1)});
        return;
    }
    if (key == "findstruct") {
        add("array_findstruct",
            {array, id(destination), value(arguments[0]), valueAs(arguments[1], memberType()),
             argumentAt(2, Type::int_(), 0)});
        return;
    }
    if (key == "rfindstruct") {
        add("array_rfindstruct",
            {array, id(destination), value(arguments[0]), valueAs(arguments[1], memberType()),
             argumentAt(2, Type::int_(), -1)});
        return;
    }
    if (key == "add") {
        add("array_add", {array, valueAs(arguments[0], element), argumentAt(1, Type::int_(), 1)});
        return;
    }
    if (key == "insert") {
        add("array_insert",
            {array, valueAs(arguments[0], element), valueAs(arguments[1], Type::int_())});
        return;
    }
    if (key == "remove") {
        add("array_remove",
            {array, valueAs(arguments[0], Type::int_()), argumentAt(1, Type::int_(), 1)});
        return;
    }
    if (key == "removelast") {
        add("array_removelast", {array});
        return;
    }
    if (key == "clear") {
        add("array_clear", {array});
        return;
    }

    cannotEmit(call);
}

void BodyEmitter::cannotEmit(const Node& node) {
    gen_.report(DiagnosticCode::CannotEmit,
                "This did not resolve to anything the back end can name, so no instruction was "
                "emitted for it. Nothing is guessed here; the file is refused instead.",
                node.span);
}

}  // namespace fo4::papyrus
