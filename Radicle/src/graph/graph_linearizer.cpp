#include <fo4/graph/graph_linearizer.hpp>

#include <algorithm>
#include <cctype>
#include <functional>

#include <fo4/graph/builtin_node_definitions.hpp>
#include "graph_linearizer_detail.hpp"

namespace fo4::graph {

using namespace linearizer_detail;

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
                                        call->isArray, call, into, true);
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

}
