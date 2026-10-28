#include "body_emitter.hpp"
#include "codegen_detail.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fo4::papyrus {

using codegen_detail::lower;
using codegen_detail::opcodeFor;
using fo4::pex::PexFile;
using fo4::pex::PexInstruction;
using fo4::pex::PexTypedName;
using fo4::pex::PexValue;
using fo4::pex::PexValueType;

void BodyEmitter::run() {
    scopes_.emplace_back();
    for (const auto& p : decl_.parameters) {
        usedLocalNames_.insert(p.name);
        scopes_.back()[lower(p.name)] = Local{p.name, gen_.typeOf(p.type)};
    }

    emitBlock(decl_.body);
    scopes_.clear();
}

int BodyEmitter::add(const std::string& mnemonic, const std::vector<PexValue>& args) {
    const auto op = opcodeFor(mnemonic);
    const auto& meta = PexFile::kOpCodes[op];
    PexInstruction instruction;
    instruction.opCode = op;
    instruction.mnemonic = meta.name;
    instruction.fixedArgCount = meta.args;
    instruction.hasVarArgs = meta.varargs;
    instruction.line = line_;
    instruction.args = args;
    fn_.instructions.push_back(std::move(instruction));
    return static_cast<int>(fn_.instructions.size()) - 1;
}

void BodyEmitter::patchJump(int jumpIndex, int target) {
    auto& instruction = fn_.instructions[static_cast<std::size_t>(jumpIndex)];
    auto& operand = instruction.args.back();
    operand.type = PexValueType::Integer;
    operand.int_ = target - jumpIndex;
}

std::string BodyEmitter::declareLocal(const std::string& sourceName, const Type& type) {
    auto emitted = sourceName;
    if (!usedLocalNames_.insert(emitted).second) {
        emitted = "::mangled_" + sourceName + "_" + std::to_string(mangleCounter_++);
        usedLocalNames_.insert(emitted);
    }

    PexTypedName local;
    local.name = emitted;
    local.type = gen_.pexTypeName(type);
    fn_.locals.push_back(std::move(local));
    scopes_.back()[lower(sourceName)] = Local{emitted, type};
    return emitted;
}

std::optional<BodyEmitter::Local> BodyEmitter::findLocal(const std::string& name) {
    const auto key = lower(name);
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        const auto found = it->find(key);
        if (found != it->end()) return found->second;
    }
    return std::nullopt;
}

std::string BodyEmitter::noneVar() {
    if (noneVar_) return *noneVar_;
    noneVar_ = "::nonevar";
    usedLocalNames_.insert(*noneVar_);
    PexTypedName local;
    local.name = *noneVar_;
    local.type = "None";
    fn_.locals.push_back(std::move(local));
    return *noneVar_;
}

std::string BodyEmitter::allocTemp(const Type& type) {
    const auto typeName = gen_.pexTypeName(type);
    const auto pool = freeTemps_.find(typeName);
    if (pool != freeTemps_.end() && !pool->second.empty()) {
        const auto reused = pool->second.top();
        pool->second.pop();
        liveTemps_.emplace_back(reused, typeName);
        return reused;
    }

    const auto name = "::temp" + std::to_string(gen_.tempCounter_++);
    PexTypedName local;
    local.name = name;
    local.type = typeName;
    fn_.locals.push_back(std::move(local));
    liveTemps_.emplace_back(name, typeName);
    return name;
}

void BodyEmitter::releaseTemps(int mark) {
    for (std::size_t i = liveTemps_.size(); i > static_cast<std::size_t>(mark); --i) {
        const auto& [name, type] = liveTemps_[i - 1];
        freeTemps_[type].push(name);
    }
    if (static_cast<int>(liveTemps_.size()) > mark) {
        liveTemps_.erase(liveTemps_.begin() + mark, liveTemps_.end());
    }
}

void BodyEmitter::emitBlock(const std::vector<StatementPtr>& body) {
    scopes_.emplace_back();
    for (const auto& statement : body) emitStatement(statement.get());
    scopes_.pop_back();
}

void BodyEmitter::emitStatement(const Statement* statement) {
    const int mark = static_cast<int>(liveTemps_.size());
    line_ = statement->span.line;

    switch (statement->nodeKind()) {
        case NodeKind::DefineStatement: {
            const auto* define = static_cast<const DefineStatement*>(statement);
            const Type& type = gen_.typeOf(define->type);
            const auto name = declareLocal(define->name, type);

            if (define->initializer) {
                emitInto(define->initializer.get(), name, type);
            } else {
                add("assign", {id(name), CodeGenerator::zeroValue(type)});
            }
            break;
        }

        case NodeKind::AssignStatement:
            emitAssign(*static_cast<const AssignStatement*>(statement));
            break;

        case NodeKind::ExpressionStatement: {
            const auto* expr = static_cast<const ExpressionStatement*>(statement);
            emitDiscarded(expr->expression.get());
            break;
        }

        case NodeKind::ReturnStatement: {
            const auto* ret = static_cast<const ReturnStatement*>(statement);
            if (!ret->value || returnType_.kind == TypeKind::None) {
                add("return", {noneValue()});
            } else {
                add("return", {valueAs(ret->value.get(), returnType_)});
            }
            break;
        }

        case NodeKind::IfStatement:
            emitIf(*static_cast<const IfStatement*>(statement));
            break;

        case NodeKind::WhileStatement:
            emitWhile(*static_cast<const WhileStatement*>(statement));
            break;

        default:
            break;
    }

    releaseTemps(mark);
}

void BodyEmitter::emitIf(const IfStatement& iff) {
    std::vector<int> toEnd;

    for (const auto& branch : iff.branches) {
        line_ = branch.condition->span.line;
        const auto condition = value(branch.condition.get());
        const int jumpPastBranch = add("jmpf", {condition, intValue(0)});

        emitBlock(branch.body);

        toEnd.push_back(add("jmp", {intValue(0)}));
        patchJump(jumpPastBranch, here());
    }

    if (!iff.elseBody.empty()) emitBlock(iff.elseBody);

    for (const int jump : toEnd) patchJump(jump, here());
}

void BodyEmitter::emitWhile(const WhileStatement& wh) {
    const int top = here();
    line_ = wh.condition->span.line;
    const auto condition = value(wh.condition.get());
    const int exit = add("jmpf", {condition, intValue(0)});

    emitBlock(wh.body);
    patchJump(add("jmp", {intValue(0)}), top);
    patchJump(exit, here());
}

void BodyEmitter::emitAssign(const AssignStatement& assign) {
    const Type& targetType = gen_.typeOf(assign.target.get());
    const Node* valueNode = assign.value.get();
    const auto op = compoundOperator(assign.op);

    switch (assign.target->nodeKind()) {
        case NodeKind::IdentifierExpression: {
            const auto* idNode = static_cast<const IdentifierExpression*>(assign.target.get());
            const auto slot = storageFor(*idNode);
            if (!slot) return;
            if (slot->isProperty) {
                if (!op) {
                    const auto stored = materialise(valueAs(valueNode, targetType), targetType);
                    add("propset", {id(slot->name), *slot->receiver, stored});
                    return;
                }
                const auto current = propertyGet(*slot, idNode);
                const auto result = binaryInto(std::nullopt, targetType, *op, current,
                                               valueAs(valueNode, targetType), assign.span);
                add("propset", {id(slot->name), *slot->receiver, materialise(result, targetType)});
                return;
            }

            if (!op) {
                emitInto(valueNode, slot->name, targetType);
                return;
            }
            binaryInto(slot->name, targetType, *op, id(slot->name),
                       valueAs(valueNode, targetType), assign.span);
            return;
        }

        case NodeKind::MemberExpression: {
            const auto* member = static_cast<const MemberExpression*>(assign.target.get());
            const Binding* binding = gen_.resolution_->bindingFor(member);
            if (!binding) {
                cannotEmit(*member);
                return;
            }

            if (binding->kind == BindingKind::StructMember) {
                if (!op) {
                    const auto stored = materialise(valueAs(valueNode, targetType), targetType);
                    add("struct_set", {value(member->target.get()), id(member->name), stored});
                    return;
                }
                const auto structValue = value(member->target.get());
                const auto newValue = [&] {
                    const auto current = allocTemp(targetType);
                    add("struct_get", {id(current), structValue, id(member->name)});
                    return binaryInto(std::nullopt, targetType, *op, id(current),
                                      valueAs(valueNode, targetType), assign.span);
                }();
                add("struct_set", {structValue, id(member->name), materialise(newValue, targetType)});
                return;
            }

            if (binding->kind == BindingKind::Property) {
                if (!op) {
                    const auto stored = materialise(valueAs(valueNode, targetType), targetType);
                    add("propset", {id(member->name), value(member->target.get()), stored});
                    return;
                }
                const auto receiver = value(member->target.get());
                const auto current = allocTemp(targetType);
                add("propget", {id(member->name), receiver, id(current)});
                const auto newValue = binaryInto(std::nullopt, targetType, *op, id(current),
                                                 valueAs(valueNode, targetType), assign.span);
                add("propset", {id(member->name), receiver, materialise(newValue, targetType)});
                return;
            }

            cannotEmit(*member);
            return;
        }

        case NodeKind::IndexExpression: {
            const auto* index = static_cast<const IndexExpression*>(assign.target.get());
            if (!op) {
                const auto stored = valueAs(valueNode, targetType);
                const auto target = value(index->target.get());
                const auto subscript = valueAs(index->index.get(), Type::int_());
                add("array_setelement", {target, subscript, stored});
                return;
            }

            const auto array = value(index->target.get());
            const auto subscript = valueAs(index->index.get(), Type::int_());
            const auto current = allocTemp(targetType);
            add("array_getelement", {id(current), array, subscript});
            const auto newValue = binaryInto(std::nullopt, targetType, *op, id(current),
                                             valueAs(valueNode, targetType), assign.span);
            add("array_setelement", {array, subscript, newValue});
            return;
        }

        default:
            cannotEmit(*assign.target);
            return;
    }
}

std::optional<TokenKind> BodyEmitter::compoundOperator(TokenKind assignOperator) {
    switch (assignOperator) {
        case TokenKind::PlusAssign: return TokenKind::Plus;
        case TokenKind::MinusAssign: return TokenKind::Minus;
        case TokenKind::StarAssign: return TokenKind::Star;
        case TokenKind::SlashAssign: return TokenKind::Slash;
        case TokenKind::PercentAssign: return TokenKind::Percent;
        default: return std::nullopt;
    }
}

void BodyEmitter::emitDiscarded(const Node* expression) {
    const auto* call = dynamic_cast<const CallExpression*>(expression);
    if (!call) {
        value(expression);
        return;
    }

    const Binding* callBinding = gen_.resolution_->bindingFor(call);
    if (!callBinding) callBinding = gen_.resolution_->bindingFor(call->callee.get());
    if (callBinding && gen_.excludedBy(*callBinding)) return;

    const Type& type = gen_.typeOf(call);
    if (type.kind == TypeKind::None) {
        emitCall(*call, noneVar());
        return;
    }
    emitCall(*call, allocTemp(type));
}

}  // namespace fo4::papyrus
