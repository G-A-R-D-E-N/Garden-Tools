#include <fo4/papyrus/code_generator.hpp>

#include "body_emitter.hpp"
#include "codegen_detail.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::papyrus {

using codegen_detail::lower;
using codegen_detail::iequals;
using codegen_detail::opcodeFor;
using fo4::pex::PexDebugFunction;
using fo4::pex::PexFile;
using fo4::pex::PexFunction;
using fo4::pex::PexInstruction;
using fo4::pex::PexObject;
using fo4::pex::PexProperty;
using fo4::pex::PexPropertyGroup;
using fo4::pex::PexState;
using fo4::pex::PexStruct;
using fo4::pex::PexStructMember;
using fo4::pex::PexStructOrder;
using fo4::pex::PexTypedName;
using fo4::pex::PexValue;
using fo4::pex::PexValueType;
using fo4::pex::PexVariable;

namespace {

bool hasFlag(const Declaration& decl, std::string_view flag) {
    return decl.hasFlag(flag);
}

}  // namespace

PexFile* CodeGenerator::generate(const Script& script, const Resolution& resolution,
                                 const CodeGenOptions& options,
                                 std::vector<Diagnostic>& diagnostics) {
    script_ = &script;
    resolution_ = &resolution;
    if (options.userFlags) {
        userFlags_ = options.userFlags;
    } else {
        ownedUserFlags_ = UserFlagTable::fallout4Default();
        userFlags_ = &ownedUserFlags_;
    }
    diagnostics_ = &diagnostics;
    debug_.clear();
    emitDebugOnly_ = options.emitDebugOnlyCode;
    emitBetaOnly_ = options.emitBetaOnlyCode;
    tempCounter_ = 0;

    auto pex = std::make_unique<PexFile>();
    pex->majorVersion = 3;
    pex->minorVersion = 9;
    pex->gameId = 2;
    pex->compilationTime = options.compilationTime;
    pex->modificationTime = options.modificationTime;
    pex->sourceFileName = options.sourceFileName;
    pex->userName = options.userName;
    pex->computerName = options.computerName;
    pex->hasDebugInfo = options.emitDebugInfo;
    pex->userFlags = userFlags_->flags();

    pex->objects.push_back(buildObject());

    if (options.emitDebugInfo) {
        pex->debugFunctions = std::move(debug_);
        pex->propertyGroups = buildPropertyGroups();
        pex->structOrders = buildStructOrders();
    }

    pex->rebuildStringTable();

    for (const auto& d : *diagnostics_) {
        if (d.severity == Severity::Error) return nullptr;
    }
    return pex.release();
}

PexObject CodeGenerator::buildObject() {
    PexObject obj;
    obj.name = script_->name;
    obj.parentClassName = parentClassName();
    obj.docString = script_->documentation.value_or("");
    obj.isConst = hasFlag(*script_, "const");
    obj.userFlags = userFlags_->maskFor(script_->flags);
    obj.autoStateName = "";
    for (const auto& state : script_->states) {
        if (state.isAuto) {
            obj.autoStateName = state.name;
            break;
        }
    }

    for (const auto& st : script_->structs) {
        PexStruct pexStruct;
        pexStruct.name = st.name;
        for (const auto& m : st.members) {
            PexStructMember member;
            member.name = m.name;
            member.type = pexTypeName(typeOf(m.type), m.type.span);
            member.userFlags = userFlags_->maskFor(m.flags);
            member.defaultValue = constantOrDefault(m.initializer.get(), typeOf(m.type));
            member.isConst = hasFlag(m, "const");
            member.docString = m.documentation.value_or("");
            pexStruct.members.push_back(std::move(member));
        }
        obj.structs.push_back(std::move(pexStruct));
    }

    for (const auto& p : script_->properties) {
        if (p.kind != PropertyKind::Auto) continue;
        PexVariable variable;
        variable.name = backingVarName(p.name);
        variable.type = pexTypeName(typeOf(p.type), p.type.span);
        variable.userFlags = userFlags_->maskFor(p.flags) & conditionalMask();
        variable.defaultValue = constantOrDefault(p.initializer.get(), typeOf(p.type));
        variable.isConst = hasFlag(p, "const");
        obj.variables.push_back(std::move(variable));
    }

    for (const auto& v : script_->variables) {
        PexVariable variable;
        variable.name = v.name;
        variable.type = pexTypeName(typeOf(v.type), v.type.span);
        variable.userFlags = userFlags_->maskFor(v.flags);
        variable.defaultValue = constantOrDefault(v.initializer.get(), typeOf(v.type));
        variable.isConst = hasFlag(v, "const");
        obj.variables.push_back(std::move(variable));
    }

    for (const auto& p : script_->properties) obj.properties.push_back(buildProperty(p));

    PexState emptyState;
    emptyState.name = "";
    for (const auto& fn : script_->functions) {
        emptyState.functions.push_back(buildFunction(fn, "", fn.name, 0));
    }
    for (const auto& ev : script_->events) {
        emptyState.functions.push_back(buildEvent(ev, ""));
    }
    obj.states.push_back(std::move(emptyState));

    for (const auto& state : script_->states) {
        PexState pexState;
        pexState.name = state.name;
        for (const auto& fn : state.functions) {
            pexState.functions.push_back(buildFunction(fn, state.name, fn.name, 0));
        }
        for (const auto& ev : state.events) {
            pexState.functions.push_back(buildEvent(ev, state.name));
        }
        obj.states.push_back(std::move(pexState));
    }

    return obj;
}

PexProperty CodeGenerator::buildProperty(const PropertyDecl& p) {
    const auto type = pexTypeName(typeOf(p.type), p.type.span);
    PexProperty prop;
    prop.name = p.name;
    prop.type = type;
    prop.docString = p.documentation.value_or("");
    prop.userFlags = userFlags_->maskFor(p.flags) & ~conditionalMask();

    switch (p.kind) {
        case PropertyKind::Auto:
            prop.flags = 0x07;
            prop.autoVarName = backingVarName(p.name);
            break;

        case PropertyKind::AutoReadOnly:
            prop.flags = 0x01;
            prop.readHandler = constantGetter(p, type);
            break;

        default:
            prop.flags = static_cast<std::uint8_t>((p.getter ? 0x01 : 0) | (p.setter ? 0x02 : 0));
            if (p.getter) prop.readHandler = buildFunction(*p.getter, "", p.name, 1);
            if (p.setter) prop.writeHandler = buildFunction(*p.setter, "", p.name, 2);
            break;
    }

    return prop;
}

PexFunction CodeGenerator::constantGetter(const PropertyDecl& property, const std::string& pexType) {
    const auto value = constantOrDefault(property.initializer.get(), typeOf(property.type));
    PexFunction fn;
    fn.name = "";
    fn.returnType = pexType;
    PexInstruction ret;
    ret.opCode = opcodeFor("return");
    ret.mnemonic = "return";
    ret.fixedArgCount = 1;
    ret.line = property.span.line;
    ret.args.push_back(value);
    fn.instructions.push_back(std::move(ret));

    PexDebugFunction df;
    df.objectName = script_->name;
    df.stateName = "";
    df.functionName = property.name;
    df.functionType = 1;
    df.lineNumbers.push_back(static_cast<std::uint16_t>(
        std::clamp(property.span.line, 0, static_cast<int>(0xFFFF))));
    debug_.push_back(std::move(df));
    return fn;
}

std::string CodeGenerator::backingVarName(const std::string& propertyName) {
    return "::" + propertyName + "_var";
}

std::string CodeGenerator::parentClassName() {
    if (script_->extends) {
        const Script* parent = index_.resolve(*script_->extends);
        return parent ? parent->name : *script_->extends;
    }
    return iequals(script_->name, "ScriptObject") ? "" : "ScriptObject";
}

std::vector<PexPropertyGroup> CodeGenerator::buildPropertyGroups() const {
    std::vector<PexPropertyGroup> groups;

    std::vector<std::string> ungrouped;
    for (const auto& p : script_->properties) {
        if (!p.groupName) ungrouped.push_back(p.name);
    }
    if (!ungrouped.empty()) {
        PexPropertyGroup group;
        group.objectName = script_->name;
        group.groupName = "";
        group.docString = "";
        group.propertyNames = std::move(ungrouped);
        groups.push_back(std::move(group));
    }

    for (const auto& g : script_->groups) {
        PexPropertyGroup group;
        group.objectName = script_->name;
        group.groupName = g.name;
        group.docString = g.documentation.value_or("");
        group.userFlags = userFlags_->maskFor(g.flags);
        for (const PropertyDecl* p : g.properties) group.propertyNames.push_back(p->name);
        groups.push_back(std::move(group));
    }
    return groups;
}

std::vector<PexStructOrder> CodeGenerator::buildStructOrders() const {
    std::vector<PexStructOrder> orders;
    for (const auto& st : script_->structs) {
        PexStructOrder order;
        order.objectName = script_->name;
        order.orderName = st.name;
        for (const auto& m : st.members) order.memberNames.push_back(m.name);
        orders.push_back(std::move(order));
    }
    return orders;
}

PexFunction CodeGenerator::buildFunction(const FunctionDecl& decl, const std::string& stateName,
                                         const std::string& debugName, std::uint8_t debugType) {
    const Type& returnType = decl.returnType ? typeOf(*decl.returnType) : Type::none();

    PexFunction fn;
    fn.name = debugType == 0 ? decl.name : "";
    fn.returnType = decl.returnType ? pexTypeName(returnType, decl.returnType->span) : "None";
    fn.docString = decl.documentation.value_or("");
    fn.userFlags = userFlags_->maskFor(decl.flags);
    fn.isGlobal = decl.isGlobal;
    fn.isNative = decl.isNative;

    emitBody(fn, decl, returnType, stateName, debugName, debugType);
    return fn;
}

PexFunction CodeGenerator::buildEvent(const EventDecl& decl, const std::string& stateName) {
    std::string name = decl.name;
    if (decl.remoteObjectType) name = "::remote_" + *decl.remoteObjectType + "_" + decl.name;

    PexFunction fn;
    fn.name = name;
    fn.returnType = "None";
    fn.docString = decl.documentation.value_or("");
    fn.userFlags = userFlags_->maskFor(decl.flags);
    fn.isGlobal = false;
    fn.isNative = decl.isNative;

    emitBody(fn, decl, Type::none(), stateName, name, 0);
    return fn;
}

void CodeGenerator::emitBody(PexFunction& fn, const CallableDecl& decl, const Type& returnType,
                             const std::string& stateName, const std::string& debugName,
                             std::uint8_t debugType) {
    for (const auto& p : decl.parameters) {
        PexTypedName param;
        param.name = p.name;
        param.type = pexTypeName(typeOf(p.type), p.type.span);
        fn.params.push_back(std::move(param));
    }

    if (decl.isNative) return;

    BodyEmitter emitter(*this, fn, decl, returnType);
    emitter.run();

    PexDebugFunction df;
    df.objectName = script_->name;
    df.stateName = stateName;
    df.functionName = debugName;
    df.functionType = debugType;
    df.lineNumbers.reserve(fn.instructions.size());
    for (const auto& instruction : fn.instructions) {
        df.lineNumbers.push_back(
            static_cast<std::uint16_t>(std::clamp(instruction.line, 0, static_cast<int>(0xFFFF))));
    }
    debug_.push_back(std::move(df));
}

const Type& CodeGenerator::typeOf(const Node* node) const {
    const Type* type = resolution_->typeOf(node);
    return type ? *type : Type::error();
}

std::optional<PexValue> CodeGenerator::qualifyCustomEvent(const Node& argument,
                                                          const std::vector<const Node*>& slots,
                                                          int index,
                                                          const std::string* receiverScript) const {
    const auto* literal = dynamic_cast<const LiteralExpression*>(&argument);
    if (!literal || literal->kind != LiteralKind::String) return std::nullopt;

    std::optional<std::string> target;
    if (receiverScript) target = *receiverScript;
    for (int i = index - 1; i >= 0; --i) {
        if (!slots[static_cast<std::size_t>(i)]) continue;
        const Type& type = typeOf(slots[static_cast<std::size_t>(i)]);
        if (type.kind == TypeKind::Object) {
            target = type.name;
            break;
        }
    }
    if (!target) return std::nullopt;

    const Script* declarer = declaringScriptOf(*target, literal->text);
    if (!declarer) return std::nullopt;

    PexValue value;
    value.type = PexValueType::String;
    value.str = lower(declarer->name) + "_" + literal->text;
    return value;
}

const Script* CodeGenerator::declaringScriptOf(const std::string& scriptName,
                                               const std::string& eventName) const {
    const Script* script = index_.resolve(scriptName);
    if (!script) return nullptr;

    for (const Script* ancestor : index_.baseChain(script)) {
        for (const auto& event : ancestor->customEvents) {
            if (iequals(event.name, eventName)) return ancestor;
        }
    }
    return nullptr;
}

Type CodeGenerator::foreignType(const TypeRef& reference, const Script* owner) const {
    const Type* known = resolution_->typeOf(&reference);
    if (known && known->kind != TypeKind::Error) return *known;
    if (!owner) return Type::error();

    const auto resolved = Resolver::resolveTypeName(index_, reference.name, owner);
    if (!resolved) return Type::error();
    return reference.isArray ? Type::arrayOf(*resolved) : *resolved;
}

void CodeGenerator::report(DiagnosticCode code, std::string message, const Span& span) {
    for (const auto& existing : *diagnostics_) {
        if (existing.code == code && existing.span == span && existing.message == message) return;
    }
    diagnostics_->push_back(Diagnostic{code, Severity::Error, std::move(message), span,
                                       script_->filePath});
}

std::string CodeGenerator::pexTypeName(const Type& type, const Span& at) {
    switch (type.kind) {
        case TypeKind::None: return "None";
        case TypeKind::Bool: return "Bool";
        case TypeKind::Int: return "Int";
        case TypeKind::Float: return "Float";
        case TypeKind::String: return "String";
        case TypeKind::Var: return "Var";
        case TypeKind::Object: return type.name;
        case TypeKind::Array: return pexTypeName(*type.element, at) + "[]";

        case TypeKind::Struct: {
            const auto split = type.name.find_last_of(':');
            if (split == std::string::npos || split == 0) return type.name;
            return type.name.substr(0, split) + "#" + type.name.substr(split + 1);
        }

        default:
            report(DiagnosticCode::CannotEmit,
                   "The type of this is not known, so no instruction can name it. This is a "
                   "resolver gap or a missing import root, not a syntax error.",
                   at);
            return "None";
    }
}

PexValue CodeGenerator::constantOrDefault(const Node* initializer, const Type& declared) {
    if (!initializer) return zeroValue(declared);
    const auto value = tryConstant(initializer, &declared);
    if (value) return *value;

    report(DiagnosticCode::NonConstantInitializer,
           "A variable, property or struct member may only be initialised with a literal; the "
           "format stores one value, not an expression.",
           initializer->span);
    return zeroValue(declared);
}

PexValue CodeGenerator::zeroValue(const Type& type) {
    switch (type.kind) {
        case TypeKind::Bool: return PexValue{PexValueType::Bool, "", 0, 0.0F, false};
        case TypeKind::Int: return PexValue{PexValueType::Integer, "", 0, 0.0F, false};
        case TypeKind::Float: return PexValue{PexValueType::Float, "", 0, 0.0F, false};
        case TypeKind::String: return PexValue{PexValueType::String, "", 0, 0.0F, false};
        default: return PexValue{PexValueType::None, "", 0, 0.0F, false};
    }
}

namespace {

bool tryParseInt(const std::string& text, int& value) {
    try {
        if (text.size() > 2 && (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))) {
            value = static_cast<int>(std::stoul(text.substr(2), nullptr, 16));
            return true;
        }
        value = std::stoi(text);
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace

std::optional<PexValue> CodeGenerator::tryConstant(const Node* expression, const Type* want) {
    const auto* unary = dynamic_cast<const UnaryExpression*>(expression);
    if (unary && unary->op == TokenKind::Minus) {
        const auto inner = tryConstant(unary->operand.get(), want);
        if (!inner) return std::nullopt;
        switch (inner->type) {
            case PexValueType::Integer:
                return PexValue{PexValueType::Integer, "", -inner->int_, 0.0F, false};
            case PexValueType::Float:
                return PexValue{PexValueType::Float, "", 0, -inner->float_, false};
            default:
                return std::nullopt;
        }
    }

    const auto* literal = dynamic_cast<const LiteralExpression*>(expression);
    if (!literal) return std::nullopt;

    switch (literal->kind) {
        case LiteralKind::String:
            return PexValue{PexValueType::String, literal->text, 0, 0.0F, false};

        case LiteralKind::Bool:
            return PexValue{PexValueType::Bool, "", 0, 0.0F, iequals(literal->text, "true")};

        case LiteralKind::None:
            return PexValue{PexValueType::None, "", 0, 0.0F, false};

        case LiteralKind::Int: {
            int parsed = 0;
            if (!tryParseInt(literal->text, parsed)) return std::nullopt;

            if (want && want->kind == TypeKind::Float) {
                return PexValue{PexValueType::Float, "", 0, static_cast<float>(parsed), false};
            }
            if (want && want->kind == TypeKind::Bool) {
                return PexValue{PexValueType::Bool, "", 0, 0.0F, parsed != 0};
            }
            return PexValue{PexValueType::Integer, "", parsed, 0.0F, false};
        }

        case LiteralKind::Float: {
            auto text = literal->text;
            if (!text.empty() && (text.back() == 'f' || text.back() == 'F')) text.pop_back();
            float parsed = 0.0F;
            try {
                parsed = std::stof(text);
            } catch (...) {
                return std::nullopt;
            }
            if (want && want->kind == TypeKind::Bool) {
                return PexValue{PexValueType::Bool, "", 0, 0.0F, parsed != 0.0F};
            }
            return PexValue{PexValueType::Float, "", 0, parsed, false};
        }

        default:
            return std::nullopt;
    }
}

std::optional<std::string> CodeGenerator::excludedBy(const Binding& binding) const {
    const auto flagged = [&](std::string_view flag) {
        return (binding.declaration && hasFlag(*binding.declaration, flag)) ||
               (binding.owner && hasFlag(*binding.owner, flag));
    };

    if (!emitDebugOnly_ && flagged("debugonly")) return "DebugOnly";
    if (!emitBetaOnly_ && flagged("betaonly")) return "BetaOnly";
    return std::nullopt;
}

}  // namespace fo4::papyrus
