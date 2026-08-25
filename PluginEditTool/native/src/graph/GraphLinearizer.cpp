#include <fo4ide/graph/GraphLinearizer.hpp>

#include <algorithm>
#include <cctype>
#include <functional>

#include <fo4ide/graph/BuiltinNodeDefinitions.hpp>

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

// Factory helpers mirroring the C# `new IrX(...) { ... }` records.
std::shared_ptr<ir::IrLiteral> lit(std::string text) {
    auto e = std::make_shared<ir::IrLiteral>();
    e->text = std::move(text);
    return e;
}

std::shared_ptr<ir::IrName> name(std::string text) {
    auto e = std::make_shared<ir::IrName>();
    e->name = std::move(text);
    return e;
}

std::shared_ptr<ir::IrSelf> selfExpr() { return std::make_shared<ir::IrSelf>(); }

std::shared_ptr<ir::IrParent> parentExpr() { return std::make_shared<ir::IrParent>(); }

std::shared_ptr<ir::IrMember> member(std::shared_ptr<ir::IrExpression> target, std::string memberName) {
    auto e = std::make_shared<ir::IrMember>();
    e->target = std::move(target);
    e->name = std::move(memberName);
    return e;
}

std::shared_ptr<ir::IrIndex> indexExpr(std::shared_ptr<ir::IrExpression> target,
                                       std::shared_ptr<ir::IrExpression> index) {
    auto e = std::make_shared<ir::IrIndex>();
    e->target = std::move(target);
    e->index = std::move(index);
    return e;
}

std::shared_ptr<ir::IrUnary> unary(std::string op, std::shared_ptr<ir::IrExpression> operand) {
    auto e = std::make_shared<ir::IrUnary>();
    e->op = std::move(op);
    e->operand = std::move(operand);
    return e;
}

std::shared_ptr<ir::IrBinary> binary(std::string op, std::shared_ptr<ir::IrExpression> left,
                                     std::shared_ptr<ir::IrExpression> right) {
    auto e = std::make_shared<ir::IrBinary>();
    e->op = std::move(op);
    e->left = std::move(left);
    e->right = std::move(right);
    return e;
}

std::shared_ptr<ir::IrCast> cast(std::shared_ptr<ir::IrExpression> value, std::string targetType,
                                 bool targetIsArray) {
    auto e = std::make_shared<ir::IrCast>();
    e->value = std::move(value);
    e->targetType = std::move(targetType);
    e->targetIsArray = targetIsArray;
    return e;
}

std::shared_ptr<ir::IrTypeCheck> typeCheck(std::shared_ptr<ir::IrExpression> value,
                                           std::string targetType, bool targetIsArray) {
    auto e = std::make_shared<ir::IrTypeCheck>();
    e->value = std::move(value);
    e->targetType = std::move(targetType);
    e->targetIsArray = targetIsArray;
    return e;
}

std::shared_ptr<ir::IrNewArray> newArray(std::string elementType,
                                         std::shared_ptr<ir::IrExpression> size) {
    auto e = std::make_shared<ir::IrNewArray>();
    e->elementType = std::move(elementType);
    e->size = std::move(size);
    return e;
}

std::shared_ptr<ir::IrNewStruct> newStruct(std::string structName) {
    auto e = std::make_shared<ir::IrNewStruct>();
    e->structName = std::move(structName);
    return e;
}

std::shared_ptr<ir::IrAssign> assign(std::shared_ptr<ir::IrExpression> target,
                                     std::shared_ptr<ir::IrExpression> value) {
    auto s = std::make_shared<ir::IrAssign>();
    s->target = std::move(target);
    s->value = std::move(value);
    return s;
}

std::shared_ptr<ir::IrExpressionStatement> exprStatement(std::shared_ptr<ir::IrExpression> e) {
    auto s = std::make_shared<ir::IrExpressionStatement>();
    s->expression = std::move(e);
    return s;
}

std::shared_ptr<ir::IrReturn> returnStmt(std::shared_ptr<ir::IrExpression> value) {
    auto s = std::make_shared<ir::IrReturn>();
    s->value = std::move(value);
    return s;
}

}  // namespace

GraphLinearizer::GraphLinearizer(const GraphDocument& document, const GraphValidation& validation,
                                 GraphTypeResolver& types, const papyrus::Script* owner)
    : document_(document), validation_(validation), types_(types), owner_(owner) {}

std::unique_ptr<ir::IrScript> GraphLinearizer::lower(papyrus::ScriptIndex& index) {
    std::vector<ir::IrCallable> callables;
    std::vector<GraphExecFlow> flows;

    for (const auto& node : document_.nodes) {
        const auto found = validation_.definitions.find(node.id);
        if (found == validation_.definitions.end()) continue;
        const auto kind = found->second->kind;
        if (kind != GraphNodeKind::EventEntry && kind != GraphNodeKind::FunctionEntry) continue;

        auto callable = lowerCallable(node, *found->second, index, flows);
        if (callable != nullptr) callables.push_back(std::move(*callable));
    }

    std::vector<const GraphExecFlow*> flowPtrs;
    for (const auto& flow : flows) flowPtrs.push_back(&flow);
    auto reachability = GraphExecFlow::checkReachability(document_, validation_.definitions, flowPtrs);
    for (auto& diagnostic : reachability) problems_.push_back(std::move(diagnostic));

    for (const auto& diagnostic : problems_) {
        if (diagnostic.severity == GraphSeverity::Error) return nullptr;
    }

    auto script = std::make_unique<ir::IrScript>();
    script->name = document_.header.scriptName;
    script->extends = document_.header.extends;
    script->flags = document_.header.flags;
    script->imports = document_.header.imports;
    script->docComment = document_.header.docComment;
    for (const auto& s : document_.structs) {
        ir::IrStruct out;
        out.name = s.name;
        for (const auto& m : s.members) {
            ir::IrStructMember im;
            im.name = m.name;
            im.typeName = baseType(m.type);
            im.isArray = isArray(m.type);
            out.members.push_back(std::move(im));
        }
        script->structs.push_back(std::move(out));
    }
    for (const auto& v : document_.variables) {
        if (v.isProperty) continue;
        ir::IrVariable out;
        out.name = v.name;
        out.typeName = baseType(v.type);
        out.isArray = isArray(v.type);
        out.flags = v.flags;
        out.initialText = v.initial;
        script->variables.push_back(std::move(out));
    }
    for (const auto& v : document_.variables) {
        if (!v.isProperty) continue;
        ir::IrProperty out;
        out.name = v.name;
        out.typeName = baseType(v.type);
        out.isArray = isArray(v.type);
        out.flags = v.flags;
        out.initialText = v.initial;
        script->properties.push_back(std::move(out));
    }
    script->callables = std::move(callables);
    const auto& autoState = document_.header.autoState;
    if (autoState && !autoState->empty() &&
        std::any_of(autoState->begin(), autoState->end(),
                    [](unsigned char c) { return !std::isspace(c); })) {
        script->autoState = *autoState;
    }
    script->customEvents = document_.customEvents;
    return script;
}

std::unique_ptr<ir::IrCallable> GraphLinearizer::lowerCallable(
    const GraphNode& entry, const NodeDefinition& definition, papyrus::ScriptIndex& index,
    std::vector<GraphExecFlow>& flows) {
    GraphNameTable table(&index, owner_);
    Frame frame(document_, validation_.definitions, entry.id, std::move(table));
    flows.push_back(frame.flow);

    for (const auto& diagnostic : frame.flow.checkStructured(validation_.definitions)) {
        problems_.push_back(diagnostic);
    }
    for (const auto& diagnostic :
         definiteAssignment::check(document_, validation_.definitions, frame.flow)) {
        problems_.push_back(diagnostic);
    }

    std::vector<ir::IrParameter> parameters;
    for (const auto* pin : definition.dataOutputs()) {
        if (!startsWithIgnoreCase(pin->id, pinIds::kParameterPrefix)) continue;
        const auto paramName = std::string(pin->id.substr(pinIds::kParameterPrefix.size()));
        frame.names.reserve(paramName);
        const auto [typeName, isArray] = types_.typeOf(pin->type ? &*pin->type : nullptr,
                                                       genericsFor(entry.id));
        ir::IrParameter parameter;
        parameter.name = paramName;
        parameter.typeName = typeName;
        parameter.isArray = isArray;
        parameter.defaultText = pin->declaredDefault;
        parameters.push_back(std::move(parameter));
    }

    const auto returnType = entry.configString("returns");
    frame.returnTypeName = returnType && !returnType->empty() ? baseType(*returnType)
                                                              : std::optional<std::string>{};
    frame.returnIsArray = returnType ? isArray(*returnType) : false;

    for (const auto* pin : definition.dataOutputs()) {
        if (!startsWithIgnoreCase(pin->id, pinIds::kParameterPrefix)) continue;
        frame.resultLocals[PinRef{entry.id, pin->id}.toString()] =
            std::string(pin->id.substr(pinIds::kParameterPrefix.size()));
    }

    const auto start = frame.flow.targetOf(entry.id, pinIds::kExec);

    if (frame.returnTypeName) {
        const auto leaks = frame.flow.pathsLeavingWithoutReturn(validation_.definitions, start);
        if (!leaks.empty()) {
            GraphDiagnostic diagnostic;
            diagnostic.code = std::string(codes::kNotAllPathsReturn);
            diagnostic.severity = GraphSeverity::Error;
            diagnostic.message =
                "This function declares a return value, but control can leave it without "
                "passing a Return node. Papyrus would hand back a zero value at runtime "
                "instead of refusing to compile.";
            diagnostic.nodeId = entry.id;
            diagnostic.relatedNodes = leaks;
            problems_.push_back(std::move(diagnostic));
        }
    }

    std::vector<std::shared_ptr<ir::IrStatement>> body =
        start ? lowerRegion(*start, std::string{}, frame)
              : std::vector<std::shared_ptr<ir::IrStatement>>{};

    collapseTemporaries(body, frame);

    auto callable = std::make_unique<ir::IrCallable>();
    callable->name = GraphValidator::entryNameOf(entry, definition);
    callable->entryNodeId = entry.id;
    callable->isEvent = definition.kind == GraphNodeKind::EventEntry;
    callable->isGlobal = entry.configString("global") == "true";
    if (definition.isRemoteEvent && definition.ownerScript) {
        callable->remoteObjectType = *definition.ownerScript;
    }
    callable->stateName = entry.configString("state");
    callable->returnTypeName = frame.returnTypeName;
    callable->returnIsArray = frame.returnIsArray;
    callable->parameters = std::move(parameters);
    callable->locals = std::move(frame.locals);
    callable->body = std::move(body);
    return callable;
}

std::vector<std::shared_ptr<ir::IrStatement>> GraphLinearizer::lowerRegion(std::string nodeId,
                                                                           std::string stopAt,
                                                                           Frame& frame) {
    std::vector<std::shared_ptr<ir::IrStatement>> statements;
    int guard = 0;

    while (!nodeId.empty() && !iequals(nodeId, stopAt)) {
        if (++guard > 10'000) {
            GraphDiagnostic diagnostic;
            diagnostic.code = std::string(codes::kInternalEmitterFault);
            diagnostic.severity = GraphSeverity::Error;
            diagnostic.message =
                "Lowering did not terminate. This is a defect in the compiler, not the graph.";
            diagnostic.nodeId = std::string(nodeId);
            problems_.push_back(std::move(diagnostic));
            break;
        }

        const auto* node = document_.node(nodeId);
        if (node == nullptr) break;
        const auto found = validation_.definitions.find(node->id);
        if (found == validation_.definitions.end()) break;
        const auto& definition = *found->second;

        switch (definition.kind) {
            case GraphNodeKind::Branch: {
                std::optional<std::string> merge;
                const auto& ipdom = frame.flow.immediatePostDominator();
                const auto ipdomFound = ipdom.find(std::string(nodeId));
                if (ipdomFound != ipdom.end() && ipdomFound->second) merge = *ipdomFound->second;

                const auto condition = pull(PinRef{node->id, std::string(pinIds::kCondition)},
                                            frame, statements);
                const auto thenStep = step(nodeId, pinIds::kThen, frame);
                const auto elseStep = step(nodeId, pinIds::kElse, frame);
                auto thenBody = lowerRegion(thenStep.value_or(""), merge.value_or(""), frame);
                auto elseBody = lowerRegion(elseStep.value_or(""), merge.value_or(""), frame);

                statements.push_back(fold(condition, std::move(thenBody), std::move(elseBody),
                                          nodeId));
                nodeId = merge.value_or("");
                continue;
            }

            case GraphNodeKind::While: {
                const auto condition = pull(PinRef{node->id, std::string(pinIds::kCondition)},
                                            frame, statements);

                Frame::Loop loop;
                loop.nodeId = node->id;
                frame.loops.push_back(loop);
                const auto bodyStart = frame.flow.targetOf(nodeId, pinIds::kBody);
                auto body = lowerRegion(bodyStart.value_or(""), nodeId, frame);
                // The Break arm may have allocated the sentinel on the stored
                // copy; read it back (the C# Loop is a reference type).
                loop = frame.loops.back();
                frame.loops.pop_back();

                auto whileStmt = std::make_shared<ir::IrWhile>();
                whileStmt->condition = guardedBy(loop, condition, statements);
                whileStmt->body = std::move(body);
                whileStmt->nodeId = node->id;
                statements.push_back(std::move(whileStmt));
                nodeId = step(nodeId, pinIds::kCompleted, frame).value_or("");
                continue;
            }

            case GraphNodeKind::ForEach: {
                auto lowered = lowerForEach(*node, frame, statements);
                for (auto& statement : lowered) statements.push_back(std::move(statement));
                nodeId = step(nodeId, pinIds::kCompleted, frame).value_or("");
                continue;
            }

            case GraphNodeKind::Return: {
                std::shared_ptr<ir::IrExpression> value;
                const auto valuePin = PinRef{node->id, std::string(pinIds::kValue)};
                if (!document_.into(valuePin).empty() ||
                    node->pinValues.contains(std::string(pinIds::kValue))) {
                    value = pull(valuePin, frame, statements);
                }

                if (value == nullptr && frame.returnTypeName) {
                    GraphDiagnostic diagnostic;
                    diagnostic.code = std::string(codes::kReturnValueMissing);
                    diagnostic.severity = GraphSeverity::Error;
                    diagnostic.message =
                        "This function returns a value, so its Return node needs one.";
                    diagnostic.nodeId = node->id;
                    diagnostic.pinId = std::string(pinIds::kValue);
                    problems_.push_back(std::move(diagnostic));
                } else if (value != nullptr && !frame.returnTypeName) {
                    GraphDiagnostic diagnostic;
                    diagnostic.code = std::string(codes::kReturnValueUnexpected);
                    diagnostic.severity = GraphSeverity::Error;
                    diagnostic.message =
                        "This function returns nothing, so its Return node cannot carry a value.";
                    diagnostic.nodeId = node->id;
                    diagnostic.pinId = std::string(pinIds::kValue);
                    problems_.push_back(std::move(diagnostic));
                }

                auto ret = returnStmt(std::move(value));
                ret->nodeId = node->id;
                statements.push_back(std::move(ret));
                return statements;
            }

            case GraphNodeKind::Break: {
                if (!inLoop(nodeId, "Break", frame)) return statements;

                auto& loop = frame.loops.back();
                if (!loop.sentinel) loop.sentinel = declareSentinel(frame);
                auto set = assign(name(*loop.sentinel), lit("true"));
                set->nodeId = node->id;
                statements.push_back(std::move(set));
                return statements;
            }

            case GraphNodeKind::Continue: {
                inLoop(nodeId, "Continue", frame);
                return statements;
            }

            default: {
                auto lowered = lowerSimple(*node, definition, frame, statements);
                for (auto& statement : lowered) statements.push_back(std::move(statement));

                const auto next = nextOf(nodeId, definition, frame);
                nodeId = next && !frame.flow.backEdges().contains(
                                     std::pair<std::string, std::string>{nodeId, *next})
                             ? *next
                             : std::string{};
                continue;
            }
        }
    }

    return statements;
}

std::vector<std::shared_ptr<ir::IrStatement>> GraphLinearizer::lowerForEach(
    const GraphNode& node, Frame& frame,
    std::vector<std::shared_ptr<ir::IrStatement>>& into) {
    std::vector<std::shared_ptr<ir::IrStatement>> out;

    const auto array = pull(PinRef{node.id, std::string(pinIds::kArray)}, frame, into);
    const auto arrayLocal = bind(node.id, frame, "items", array->typeName, array->isArray, array,
                                 into);

    const auto indexName = frame.names.allocate("index");
    frame.locals.push_back(ir::IrLocal{indexName, "int", false});
    auto setIndex = assign(name(indexName), lit("0"));
    setIndex->nodeId = node.id;
    out.push_back(std::move(setIndex));

    const auto elementName = frame.names.allocate("item");
    frame.locals.push_back(ir::IrLocal{elementName, array->typeName, false});

    frame.resultLocals[PinRef{node.id, std::string(pinIds::kElement)}.toString()] = elementName;
    frame.resultLocals[PinRef{node.id, std::string(pinIds::kIndex)}.toString()] = indexName;

    std::vector<std::shared_ptr<ir::IrStatement>> body;
    {
        auto idx = indexExpr(name(arrayLocal), name(indexName));
        idx->typeName = array->typeName;
        auto load = assign(name(elementName), std::move(idx));
        load->nodeId = node.id;
        body.push_back(std::move(load));
    }

    Frame::Loop loop;
    loop.nodeId = node.id;
    frame.loops.push_back(loop);
    const auto bodyStart = frame.flow.targetOf(node.id, pinIds::kBody);
    auto loweredBody = lowerRegion(bodyStart.value_or(""), node.id, frame);
    for (auto& statement : loweredBody) body.push_back(std::move(statement));
    // The Break arm may have allocated the sentinel on the stored copy; read
    // it back (the C# Loop is a reference type).
    loop = frame.loops.back();
    frame.loops.pop_back();

    {
        auto inc = binary("+", name(indexName), lit("1"));
        inc->typeName = "int";
        auto bump = assign(name(indexName), std::move(inc));
        bump->nodeId = node.id;
        body.push_back(std::move(bump));
    }

    auto boundsLeft = name(indexName);
    auto boundsRight = member(name(arrayLocal), "Length");
    boundsRight->typeName = "int";
    std::shared_ptr<ir::IrExpression> bounds =
        binary("<", std::move(boundsLeft), std::move(boundsRight));
    bounds->typeName = "bool";

    std::vector<std::shared_ptr<ir::IrStatement>> reset;
    bounds = guardedBy(loop, std::move(bounds), reset);
    for (auto& statement : reset) out.push_back(std::move(statement));

    auto whileStmt = std::make_shared<ir::IrWhile>();
    whileStmt->condition = std::move(bounds);
    whileStmt->body = std::move(body);
    whileStmt->nodeId = node.id;
    out.push_back(std::move(whileStmt));

    return out;
}

std::vector<std::shared_ptr<ir::IrStatement>> GraphLinearizer::lowerSimple(
    const GraphNode& node, const NodeDefinition& definition, Frame& frame,
    std::vector<std::shared_ptr<ir::IrStatement>>& into) {
    std::vector<std::shared_ptr<ir::IrStatement>> out;

    switch (definition.kind) {
        case GraphNodeKind::VariableSet: {
            const auto varName = node.configString("name").value_or("");
            const auto value = pull(PinRef{node.id, std::string(pinIds::kValue)}, frame, into);
            auto set = assign(name(varName), value);
            set->nodeId = node.id;
            out.push_back(std::move(set));
            break;
        }

        case GraphNodeKind::LocalDeclare: {
            const auto varName = node.configString("name").value_or("");
            const auto written = node.configString("type").value_or("var");

            std::string lowered;
            for (const char c : varName) {
                lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            if (!frame.declaredLocals.insert(lowered).second) {
                GraphDiagnostic diagnostic;
                diagnostic.code = std::string(codes::kDuplicateDeclaration);
                diagnostic.severity = GraphSeverity::Error;
                diagnostic.message = "'" + varName + "' is already declared in this function.";
                diagnostic.nodeId = node.id;
                problems_.push_back(std::move(diagnostic));
                break;
            }

            frame.names.reserve(varName);
            frame.locals.push_back(ir::IrLocal{varName, baseType(written), isArray(written)});

            if (!document_.into(PinRef{node.id, std::string(pinIds::kValue)}).empty()) {
                const auto initial = pull(PinRef{node.id, std::string(pinIds::kValue)}, frame, into);
                auto set = assign(name(varName), initial);
                set->nodeId = node.id;
                out.push_back(std::move(set));
            }
            break;
        }

        case GraphNodeKind::PropertySet: {
            const auto target = pullReceiver(node, definition, frame, into);
            const auto value = pull(PinRef{node.id, std::string(pinIds::kValue)}, frame, into);
            std::shared_ptr<ir::IrExpression> left =
                target == nullptr ? std::shared_ptr<ir::IrExpression>(name(definition.memberName.value_or("")))
                                  : member(target, definition.memberName.value_or(""));
            auto set = assign(std::move(left), value);
            set->nodeId = node.id;
            out.push_back(std::move(set));
            break;
        }

        case GraphNodeKind::IndexSet: {
            const auto array = pull(PinRef{node.id, std::string(pinIds::kArray)}, frame, into);
            const auto index = pull(PinRef{node.id, std::string(pinIds::kIndex)}, frame, into);
            const auto value = pull(PinRef{node.id, std::string(pinIds::kValue)}, frame, into);
            auto set = assign(indexExpr(array, index), value);
            set->nodeId = node.id;
            out.push_back(std::move(set));
            break;
        }

        case GraphNodeKind::Call:
        case GraphNodeKind::ArrayOp: {
            auto call = buildCall(node, definition, frame, into);
            if (call == nullptr) break;

            const bool consumed = !document_.outOf(PinRef{node.id, std::string(pinIds::kReturn)}).empty();
            if (!consumed) {
                auto statement = exprStatement(std::move(call));
                statement->nodeId = node.id;
                out.push_back(std::move(statement));
                break;
            }

            const auto localName = bind(node.id, frame,
                                        definition.localNameHint.value_or("value"), call->typeName,
                                        call->isArray, call, into, /*declareOnly=*/true);
            auto set = assign(name(localName), std::move(call));
            set->nodeId = node.id;
            out.push_back(std::move(set));
            break;
        }

        default:
            break;
    }

    return out;
}

void GraphLinearizer::collapseTemporaries(std::vector<std::shared_ptr<ir::IrStatement>>& body,
                                          Frame& frame) {
    collapseIn(body, body, frame);
}

void GraphLinearizer::collapseIn(std::vector<std::shared_ptr<ir::IrStatement>>& block,
                                 std::vector<std::shared_ptr<ir::IrStatement>>& whole, Frame& frame) {
    for (auto& statement : block) {
        if (auto* branch = dynamic_cast<ir::IrIf*>(statement.get())) {
            for (auto& arm : branch->branches) collapseIn(arm.body, whole, frame);
            collapseIn(branch->elseBody, whole, frame);
        } else if (auto* loop = dynamic_cast<ir::IrWhile*>(statement.get())) {
            collapseIn(loop->body, whole, frame);
        }
    }

    for (std::size_t i = 0; i + 1 < block.size(); ++i) {
        auto* first = dynamic_cast<ir::IrAssign*>(block[i].get());
        if (first == nullptr) continue;
        auto* produced = dynamic_cast<ir::IrName*>(first->target.get());
        if (produced == nullptr) continue;
        if (dynamic_cast<ir::IrName*>(first->value.get()) != nullptr) continue;

        auto* second = dynamic_cast<ir::IrAssign*>(block[i + 1].get());
        if (second == nullptr) continue;
        if (dynamic_cast<ir::IrName*>(second->target.get()) == nullptr) continue;
        auto* copied = dynamic_cast<ir::IrName*>(second->value.get());
        if (copied == nullptr) continue;
        if (!iequals(produced->name, copied->name)) continue;

        std::string lowered;
        for (const char c : produced->name) {
            lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        if (frame.declaredLocals.contains(lowered)) continue;

        const auto local = std::find_if(frame.locals.begin(), frame.locals.end(),
                                        [&](const ir::IrLocal& l) { return l.name == produced->name; });
        if (local == frame.locals.end()) continue;
        if (countReads(whole, produced->name) != 1) continue;

        auto replacement = assign(second->target, first->value);
        replacement->nodeId = second->nodeId ? second->nodeId : first->nodeId;
        replacement->pinId = second->pinId;
        block[i] = std::move(replacement);
        block.erase(block.begin() + static_cast<std::ptrdiff_t>(i + 1));
        frame.locals.erase(local);
    }
}

int GraphLinearizer::countReads(const std::vector<std::shared_ptr<ir::IrStatement>>& body,
                                std::string_view name) {
    int count = 0;

    const std::function<void(const std::shared_ptr<ir::IrExpression>&)> read =
        [&](const std::shared_ptr<ir::IrExpression>& expression) {
            if (!expression) return;
            if (const auto* named = dynamic_cast<const ir::IrName*>(expression.get())) {
                if (iequals(named->name, name)) ++count;
                return;
            }
            if (const auto* m = dynamic_cast<const ir::IrMember*>(expression.get())) {
                read(m->target);
                return;
            }
            if (const auto* idx = dynamic_cast<const ir::IrIndex*>(expression.get())) {
                read(idx->target);
                read(idx->index);
                return;
            }
            if (const auto* call = dynamic_cast<const ir::IrCall*>(expression.get())) {
                read(call->receiver);
                for (const auto& argument : call->arguments) read(argument.value);
                return;
            }
            if (const auto* un = dynamic_cast<const ir::IrUnary*>(expression.get())) {
                read(un->operand);
                return;
            }
            if (const auto* bin = dynamic_cast<const ir::IrBinary*>(expression.get())) {
                read(bin->left);
                read(bin->right);
                return;
            }
            if (const auto* c = dynamic_cast<const ir::IrCast*>(expression.get())) {
                read(c->value);
                return;
            }
            if (const auto* t = dynamic_cast<const ir::IrTypeCheck*>(expression.get())) {
                read(t->value);
                return;
            }
            if (const auto* n = dynamic_cast<const ir::IrNewArray*>(expression.get())) {
                read(n->size);
                return;
            }
        };

    const std::function<void(const std::shared_ptr<ir::IrStatement>&)> walk =
        [&](const std::shared_ptr<ir::IrStatement>& statement) {
            if (const auto* a = dynamic_cast<const ir::IrAssign*>(statement.get())) {
                if (dynamic_cast<const ir::IrName*>(a->target.get()) == nullptr) read(a->target);
                read(a->value);
            } else if (const auto* d = dynamic_cast<const ir::IrDefine*>(statement.get())) {
                read(d->value);
            } else if (const auto* e = dynamic_cast<const ir::IrExpressionStatement*>(statement.get())) {
                read(e->expression);
            } else if (const auto* r = dynamic_cast<const ir::IrReturn*>(statement.get())) {
                read(r->value);
            } else if (const auto* b = dynamic_cast<const ir::IrIf*>(statement.get())) {
                for (const auto& arm : b->branches) {
                    read(arm.condition);
                    for (const auto& inner : arm.body) walk(inner);
                }
                for (const auto& inner : b->elseBody) walk(inner);
            } else if (const auto* l = dynamic_cast<const ir::IrWhile*>(statement.get())) {
                read(l->condition);
                for (const auto& inner : l->body) walk(inner);
            }
        };

    for (const auto& statement : body) walk(statement);
    return count;
}

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

}  // namespace fo4ide::graph
