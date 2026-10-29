#include <fo4/graph/graph_linearizer.hpp>

#include <algorithm>
#include <cctype>
#include <functional>

#include <fo4/graph/builtin_node_definitions.hpp>
#include "graph_linearizer_detail.hpp"

namespace fo4::graph {

using namespace linearizer_detail;

std::optional<std::string> GraphLinearizer::step(std::string_view nodeId, std::string_view pinId,
                                                 Frame& frame) const {
    const auto target = frame.flow.targetOf(nodeId, pinId);
    if (!target) return std::nullopt;
    if (frame.flow.backEdges().contains(
            std::pair<std::string, std::string>{std::string(nodeId), *target})) {
        return std::nullopt;
    }
    return target;
}

bool GraphLinearizer::inLoop(std::string_view nodeId, std::string_view what, Frame& frame) {
    if (!frame.loops.empty()) return true;

    GraphDiagnostic diagnostic;
    diagnostic.code = std::string(codes::kLoopExitOutsideLoop);
    diagnostic.severity = GraphSeverity::Error;
    diagnostic.message = std::string(what) +
                         " has no loop to act on. Put it inside a While or ForEach.";
    diagnostic.nodeId = std::string(nodeId);
    problems_.push_back(std::move(diagnostic));
    return false;
}

std::string GraphLinearizer::declareSentinel(Frame& frame) {
    const auto name = frame.names.allocate("broke");
    frame.locals.push_back(ir::IrLocal{name, "bool", false});
    return name;
}

std::shared_ptr<ir::IrExpression> GraphLinearizer::guardedBy(
    const Frame::Loop& loop, std::shared_ptr<ir::IrExpression> condition,
    std::vector<std::shared_ptr<ir::IrStatement>>& before) {
    if (!loop.sentinel) return condition;

    auto reset = assign(name(*loop.sentinel), lit("false"));
    reset->nodeId = loop.nodeId;
    before.push_back(std::move(reset));

    auto flag = name(*loop.sentinel);
    flag->typeName = "bool";
    auto notFlag = unary("!", std::move(flag));
    notFlag->typeName = "bool";
    auto combined = binary("&&", std::move(condition), std::move(notFlag));
    combined->typeName = "bool";
    combined->nodeId = loop.nodeId;
    return combined;
}

std::optional<std::string> GraphLinearizer::nextOf(std::string_view nodeId,
                                                   const NodeDefinition& definition,
                                                   Frame& frame) const {
    for (const auto* pin : definition.execOutputs()) {
        const auto target = frame.flow.targetOf(nodeId, pin->id);
        if (target) return target;
    }
    return std::nullopt;
}

std::shared_ptr<ir::IrIf> GraphLinearizer::fold(
    std::shared_ptr<ir::IrExpression> condition,
    std::vector<std::shared_ptr<ir::IrStatement>> thenBody,
    std::vector<std::shared_ptr<ir::IrStatement>> elseBody, std::string_view nodeId) {
    auto ifStmt = std::make_shared<ir::IrIf>();
    ifStmt->nodeId = std::string(nodeId);

    const bool thenEmpty = thenBody.empty();
    const bool elseIsSingleIf = elseBody.size() == 1 &&
                                dynamic_cast<ir::IrIf*>(elseBody[0].get()) != nullptr;
    if (thenEmpty && !elseBody.empty() && !elseIsSingleIf) {
        ir::IrBranch arm;
        arm.condition = negate(condition);
        arm.body = std::move(elseBody);
        ifStmt->branches.push_back(std::move(arm));
        return ifStmt;
    }

    ir::IrBranch arm;
    arm.condition = std::move(condition);
    arm.body = std::move(thenBody);
    ifStmt->branches.push_back(std::move(arm));

    if (elseBody.size() == 1) {
        if (auto* nested = dynamic_cast<ir::IrIf*>(elseBody[0].get())) {
            for (auto& branch : nested->branches) ifStmt->branches.push_back(std::move(branch));
            ifStmt->elseBody = std::move(nested->elseBody);
            return ifStmt;
        }
    }

    ifStmt->elseBody = std::move(elseBody);
    return ifStmt;
}

std::shared_ptr<ir::IrExpression> GraphLinearizer::negate(
    std::shared_ptr<ir::IrExpression> condition) {
    if (auto* already = dynamic_cast<ir::IrUnary*>(condition.get());
        already != nullptr && already->op == "!") {
        return already->operand;
    }
    auto neg = unary("!", std::move(condition));
    neg->typeName = "bool";
    neg->nodeId = condition ? std::nullopt : std::nullopt;
    return neg;
}

std::shared_ptr<ir::IrExpression> GraphLinearizer::pull(
    const PinRef& pin, Frame& frame, std::vector<std::shared_ptr<ir::IrStatement>>& into) {
    const auto wires = document_.into(pin);
    const auto* wire = wires.empty() ? nullptr : wires.front();
    if (wire == nullptr) return literalFor(pin);

    const auto sourceKey = wire->from.toString();

    const auto existing = frame.resultLocals.find(sourceKey);
    if (existing != frame.resultLocals.end()) {
        auto n = name(existing->second);
        n->nodeId = wire->from.node;
        n->pinId = wire->from.pin;
        return n;
    }

    const auto* sourceNode = document_.node(wire->from.node);
    const auto sourceDef = sourceNode == nullptr
                               ? validation_.definitions.end()
                               : validation_.definitions.find(sourceNode->id);
    if (sourceNode == nullptr || sourceDef == validation_.definitions.end()) {
        return lit("None");
    }

    if (!sourceDef->second->isPure) {
        GraphDiagnostic diagnostic;
        diagnostic.code = std::string(codes::kUseBeforeAssignment);
        diagnostic.severity = GraphSeverity::Error;
        diagnostic.message =
            "This value comes from a node that has not run on every path that reaches here.";
        diagnostic.nodeId = pin.node;
        diagnostic.pinId = pin.pin;
        diagnostic.relatedNodes = {wire->from.node};
        problems_.push_back(std::move(diagnostic));
        return lit("None");
    }

    return buildPure(*sourceNode, *sourceDef->second, wire->from.pin, frame, into);
}

std::shared_ptr<ir::IrExpression> GraphLinearizer::buildPure(
    const GraphNode& node, const NodeDefinition& definition, std::string_view outputPin, Frame& frame,
    std::vector<std::shared_ptr<ir::IrStatement>>& into) {
    const auto generics = genericsFor(node.id);
    const auto pins = definition.pinsFor(node, &document_);
    const PinDefinition* pin = nullptr;
    for (const auto& candidate : pins) {
        if (iequals(candidate.id, outputPin)) {
            pin = &candidate;
            break;
        }
    }
    const auto [typeName, isArr] = types_.typeOf(pin && pin->type ? &*pin->type : nullptr,
                                                 generics);

    std::shared_ptr<ir::IrExpression> built;
    switch (definition.kind) {
        case GraphNodeKind::Literal:
            built = lit(literalTextOf(node, definition));
            break;
        case GraphNodeKind::Self:
            built = selfExpr();
            break;
        case GraphNodeKind::Parent:
            built = parentExpr();
            break;
        case GraphNodeKind::NoneValue:
            built = lit("None");
            break;
        case GraphNodeKind::VariableGet:
            built = name(node.configString("name").value_or(""));
            break;
        case GraphNodeKind::PropertyGet:
            built = member(pullReceiver(node, definition, frame, into),
                           definition.memberName.value_or(""));
            break;
        case GraphNodeKind::Reroute:
            built = pull(PinRef{node.id, std::string(pinIds::kValue)}, frame, into);
            break;
        case GraphNodeKind::Unary:
            built = unary(std::string(builtinDefinitions::operatorToken(definition.id).value_or("!")),
                          pull(PinRef{node.id, std::string(pinIds::kValue)}, frame, into));
            break;
        case GraphNodeKind::Binary:
            built = binary(std::string(builtinDefinitions::operatorToken(definition.id).value_or("+")),
                           pull(PinRef{node.id, std::string(pinIds::kLeft)}, frame, into),
                           pull(PinRef{node.id, std::string(pinIds::kRight)}, frame, into));
            break;
        case GraphNodeKind::Cast:
            built = cast(pull(PinRef{node.id, std::string(pinIds::kValue)}, frame, into),
                         baseType(node.configString("type").value_or("")),
                         isArray(node.configString("type").value_or("")));
            break;
        case GraphNodeKind::TypeCheck:
            built = typeCheck(pull(PinRef{node.id, std::string(pinIds::kValue)}, frame, into),
                              baseType(node.configString("type").value_or("")),
                              isArray(node.configString("type").value_or("")));
            break;
        case GraphNodeKind::NewArray:
            built = newArray(baseType(node.configString("type").value_or("")),
                             pull(PinRef{node.id, std::string(pinIds::kIndex)}, frame, into));
            break;
        case GraphNodeKind::StructNew:
            built = newStruct(node.configString("type").value_or(""));
            break;
        case GraphNodeKind::IndexGet:
            built = indexExpr(pull(PinRef{node.id, std::string(pinIds::kArray)}, frame, into),
                              pull(PinRef{node.id, std::string(pinIds::kIndex)}, frame, into));
            break;
        case GraphNodeKind::StructGet:
            built = member(pull(PinRef{node.id, std::string(pinIds::kTarget)}, frame, into),
                           node.configString("member").value_or(""));
            break;
        case GraphNodeKind::ArrayOp:
            if (definition.memberName == "Length") {
                built = member(pull(PinRef{node.id, std::string(pinIds::kArray)}, frame, into),
                               "Length");
            } else {
                built = lit("None");
            }
            break;
        case GraphNodeKind::Call: {
            auto call = buildCall(node, definition, frame, into);
            built = call ? std::shared_ptr<ir::IrExpression>(std::move(call))
                         : std::shared_ptr<ir::IrExpression>(lit("None"));
            break;
        }
        default:
            built = lit("None");
            break;
    }

    built->nodeId = node.id;
    built->pinId = std::string(outputPin);
    built->typeName = typeName;
    built->isArray = isArr;
    return built;
}

std::shared_ptr<ir::IrCall> GraphLinearizer::buildCall(
    const GraphNode& node, const NodeDefinition& definition, Frame& frame,
    std::vector<std::shared_ptr<ir::IrStatement>>& into) {
    auto receiver = pullReceiver(node, definition, frame, into);
    std::vector<ir::IrArgument> arguments;
    bool skipped = false;

    const auto pins = definition.pinsFor(node, &document_);
    for (const auto& pin : pins) {
        if (pin.kind != PinKind::Data || pin.direction != PinDirection::In) continue;
        if (!pinIds::isArgument(pin.id)) continue;

        const auto reference = PinRef{node.id, pin.id};
        const bool supplied = !document_.into(reference).empty() ||
                              node.pinValues.contains(pin.id);

        if (!supplied && pin.isOptional) {
            skipped = true;
            continue;
        }

        const auto value = pull(reference, frame, into);
        ir::IrArgument argument;
        argument.value = value;
        if (skipped) argument.name = pinIds::argumentName(pin.id);
        arguments.push_back(std::move(argument));
    }

    const auto generics = genericsFor(node.id);
    const auto* returnPin = definition.pin(pinIds::kReturn);
    const auto [typeName, isArray] = types_.typeOf(returnPin && returnPin->type ? &*returnPin->type : nullptr,
                                                   generics);

    const auto callName = definition.memberName.value_or(definition.title);

    if (definition.kind == GraphNodeKind::ArrayOp) {
        receiver = pull(PinRef{node.id, std::string(pinIds::kArray)}, frame, into);
    }

    auto call = std::make_shared<ir::IrCall>();
    call->receiver = std::move(receiver);
    call->name = callName;
    call->arguments = std::move(arguments);
    call->isGlobal = definition.isGlobal;
    call->nodeId = node.id;
    call->pinId = std::string(pinIds::kReturn);
    call->typeName = typeName;
    call->isArray = isArray;
    return call;
}

std::shared_ptr<ir::IrExpression> GraphLinearizer::pullReceiver(
    const GraphNode& node, const NodeDefinition& definition, Frame& frame,
    std::vector<std::shared_ptr<ir::IrStatement>>& into) {
    if (definition.isGlobal) {
        auto owner = name(definition.ownerScript.value_or(""));
        owner->nodeId = node.id;
        return owner;
    }

    const auto selfPin = PinRef{node.id, std::string(pinIds::kSelf)};
    if (!document_.into(selfPin).empty()) return pull(selfPin, frame, into);

    const auto& owner = definition.ownerScript;
    if (!owner) return nullptr;

    if (iequals(*owner, document_.header.scriptName)) return nullptr;
    if (owner_ != nullptr &&
        types_.inheritsFrom(document_.header.scriptName, *owner)) {
        return nullptr;
    }

    GraphDiagnostic diagnostic;
    diagnostic.code = std::string(codes::kUnconnectedSelf);
    diagnostic.severity = GraphSeverity::Error;
    diagnostic.message = "'" + definition.title + "' belongs to " + *owner +
                         ", which this script is not, so its Target pin needs a value.";
    diagnostic.nodeId = node.id;
    diagnostic.pinId = std::string(pinIds::kSelf);
    problems_.push_back(std::move(diagnostic));
    return nullptr;
}

std::string GraphLinearizer::bind(std::string_view nodeId, Frame& frame, std::string_view hint,
                                  std::string_view typeName, bool isArray,
                                  std::shared_ptr<ir::IrExpression> value,
                                  std::vector<std::shared_ptr<ir::IrStatement>>& into,
                                  bool declareOnly) {
    const auto key = PinRef{std::string(nodeId), std::string(pinIds::kReturn)}.toString();
    const auto existing = frame.resultLocals.find(key);
    if (existing != frame.resultLocals.end()) return existing->second;

    const auto allocated = frame.names.allocate(hint);
    frame.locals.push_back(ir::IrLocal{allocated, typeName.empty() ? std::string("var") : std::string(typeName), isArray});
    frame.resultLocals[key] = allocated;

    if (!declareOnly) {
        auto set = assign(name(allocated), std::move(value));
        set->nodeId = std::string(nodeId);
        into.push_back(std::move(set));
    }
    return allocated;
}

GenericBinding GraphLinearizer::genericsFor(std::string_view nodeId) const {
    const auto found = validation_.generics.find(std::string(nodeId));
    return found == validation_.generics.end() ? GenericBinding{} : found->second;
}

std::shared_ptr<ir::IrExpression> GraphLinearizer::literalFor(const PinRef& pin) {
    const auto* node = document_.node(pin.node);
    if (node != nullptr) {
        const auto found = node->pinValues.find(pin.pin);
        if (found != node->pinValues.end()) {
            auto l = lit(found->second.value);
            l->nodeId = pin.node;
            l->pinId = pin.pin;
            return l;
        }
    }

    if (node != nullptr) {
        const auto def = validation_.definitions.find(pin.node);
        if (def != validation_.definitions.end()) {
            const auto* p = def->second->pin(pin.pin);
            if (p != nullptr && p->declaredDefault) {
                auto l = lit(*p->declaredDefault);
                l->nodeId = pin.node;
                l->pinId = pin.pin;
                return l;
            }
        }
    }

    auto l = lit("None");
    l->nodeId = pin.node;
    l->pinId = pin.pin;
    return l;
}

std::string GraphLinearizer::literalTextOf(const GraphNode& node,
                                           const NodeDefinition& definition) {
    const auto found = node.pinValues.find(std::string(pinIds::kValue));
    if (found != node.pinValues.end() && !found->second.value.empty()) {
        return found->second.value;
    }

    if (definition.id == "literal.int") return "0";
    if (definition.id == "literal.float") return "0.0";
    if (definition.id == "literal.bool") return "false";
    if (definition.id == "literal.string") return "\"\"";
    return "None";
}

std::string GraphLinearizer::baseType(std::string_view written) {
    if (written.empty()) return "";
    const std::string_view suffix = "[]";
    if (written.size() > suffix.size() &&
        written.substr(written.size() - suffix.size()) == suffix) {
        return std::string(written.substr(0, written.size() - suffix.size()));
    }
    return std::string(written);
}

bool GraphLinearizer::isArray(std::string_view written) {
    return written.size() > 2 && written.substr(written.size() - 2) == "[]";
}

}
