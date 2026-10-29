#include <fo4/graph/papyrus_source_writer.hpp>

#include <algorithm>
#include <cctype>

namespace fo4::graph {

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

}

PapyrusSourceWriter::PapyrusSourceWriter(PapyrusSourceWriterOptions options)
    : options_(std::move(options)) {}

int PapyrusSourceWriter::precedence(std::string_view op) {
    if (op == "||") return 1;
    if (op == "&&") return 2;
    if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") return 3;
    if (op == "+" || op == "-") return 4;
    if (op == "*" || op == "/" || op == "%") return 5;
    return 6;
}

std::string PapyrusSourceWriter::typeText(std::string_view typeName, bool isArray) {
    std::string out = typeName.empty() ? "var" : std::string(typeName);
    if (isArray) out += "[]";
    return out;
}

std::string PapyrusSourceWriter::declarationText(const ir::IrVariable& variable) {
    std::string text = typeText(variable.typeName, variable.isArray) + " " + variable.name;
    if (variable.initialText) text += " = " + *variable.initialText;
    for (const auto& flag : variable.flags) text += " " + flag;
    return text;
}

std::string PapyrusSourceWriter::propertyText(const ir::IrProperty& property) {
    std::string text = typeText(property.typeName, property.isArray) + " Property " + property.name;
    if (property.initialText) text += " = " + *property.initialText;

    const bool autoFlagged = std::any_of(
        property.flags.begin(), property.flags.end(), [](const std::string& flag) {
            return iequals(flag, "Auto") || iequals(flag, "AutoReadOnly");
        });
    if (!autoFlagged) text += " Auto";

    for (const auto& flag : property.flags) text += " " + flag;
    return text;
}

std::string PapyrusSourceWriter::parameterText(const ir::IrParameter& parameter) {
    std::string text = typeText(parameter.typeName, parameter.isArray) + " " + parameter.name;
    return parameter.defaultText ? text + " = " + *parameter.defaultText : text;
}

void PapyrusSourceWriter::line(std::string text) {
    for (int i = 0; i < depth_; ++i) text_.append(options_.indent);
    text_.append(text).append(options_.newLine);
    ++line_;
}

void PapyrusSourceWriter::blank() {
    text_.append(options_.newLine);
    ++line_;
}

void PapyrusSourceWriter::writeHeaderComment() {
    blank();
}

void PapyrusSourceWriter::writeHeaderLine(const ir::IrScript& script) {
    std::string header = "Scriptname " + script.name;
    if (script.extends && !script.extends->empty()) header += " extends " + *script.extends;
    for (const auto& flag : script.flags) header += " " + flag;
    line(std::move(header));
}

void PapyrusSourceWriter::writeStruct(const ir::IrStruct& declared) {
    blank();
    line("struct " + declared.name);
    ++depth_;
    for (const auto& member : declared.members) {
        line(typeText(member.typeName, member.isArray) + " " + member.name);
    }
    depth_--;
    line("endstruct");
}

void PapyrusSourceWriter::writeCallable(const ir::IrCallable& callable) {
    const int start = static_cast<int>(text_.size());
    const int startLine = line_;

    const std::string keyword = callable.isEvent ? "Event" : "Function";
    std::string signature;
    if (!callable.isEvent && callable.returnTypeName) {
        signature += typeText(*callable.returnTypeName, callable.returnIsArray) + " ";
    }
    signature += keyword + " ";
    if (callable.remoteObjectType && !callable.remoteObjectType->empty()) {
        signature += *callable.remoteObjectType + ".";
    }
    signature += callable.name + "(";
    for (std::size_t i = 0; i < callable.parameters.size(); ++i) {
        if (i > 0) signature += ", ";
        signature += parameterText(callable.parameters[i]);
    }
    signature += ")";
    if (callable.isGlobal) signature += " global";

    line(std::move(signature));
    ++depth_;

    for (const auto& local : callable.locals) {
        line(typeText(local.typeName, local.isArray) + " " + local.name);
    }
    if (!callable.locals.empty() && !callable.body.empty()) blank();

    for (const auto& statement : callable.body) writeStatement(*statement);

    depth_--;
    line(callable.isEvent ? "EndEvent" : "EndFunction");

    GraphSourceMapEntry entry;
    entry.start = start;
    entry.length = static_cast<int>(text_.size()) - start;
    entry.line = startLine;
    entry.depth = 0;
    entry.nodeId = callable.entryNodeId;
    map_.add(std::move(entry));
}

void PapyrusSourceWriter::writeStatement(const ir::IrStatement& statement) {
    const int start = static_cast<int>(text_.size());
    const int startLine = line_;

    if (const auto* define = dynamic_cast<const ir::IrDefine*>(&statement)) {
        std::string text = typeText(define->typeName, define->isArray) + " " + define->name;
        if (define->value) text += " = " + expression(*define->value, 0);
        line(std::move(text));
    } else if (const auto* assign = dynamic_cast<const ir::IrAssign*>(&statement)) {
        line(expression(*assign->target, kPostfixPrecedence) + " = " + expression(*assign->value, 0));
    } else if (const auto* exprStmt = dynamic_cast<const ir::IrExpressionStatement*>(&statement)) {
        line(expression(*exprStmt->expression, 0));
    } else if (const auto* ret = dynamic_cast<const ir::IrReturn*>(&statement)) {
        line(ret->value ? "Return " + expression(*ret->value, 0) : "Return");
    } else if (const auto* branch = dynamic_cast<const ir::IrIf*>(&statement)) {
        writeIf(*branch);
    } else if (const auto* loop = dynamic_cast<const ir::IrWhile*>(&statement)) {
        line("While (" + expression(*loop->condition, 0) + ")");
        ++depth_;
        for (const auto& inner : loop->body) writeStatement(*inner);
        depth_--;
        line("EndWhile");
    }

    if (statement.nodeId) {
        GraphSourceMapEntry entry;
        entry.start = start;
        entry.length = static_cast<int>(text_.size()) - start;
        entry.line = startLine;
        entry.depth = depth_ + 1;
        entry.nodeId = *statement.nodeId;
        entry.pinId = statement.pinId;
        map_.add(std::move(entry));
    }
}

void PapyrusSourceWriter::writeIf(const ir::IrIf& branch) {
    for (std::size_t i = 0; i < branch.branches.size(); ++i) {
        const auto& arm = branch.branches[i];
        line((i == 0 ? "If (" : "ElseIf (") + expression(*arm.condition, 0) + ")");
        ++depth_;
        for (const auto& statement : arm.body) writeStatement(*statement);
        depth_--;
    }

    if (!branch.elseBody.empty()) {
        line("Else");
        ++depth_;
        for (const auto& statement : branch.elseBody) writeStatement(*statement);
        depth_--;
    }

    line("EndIf");
}

std::string PapyrusSourceWriter::expression(const ir::IrExpression& expr, int parentPrecedence) {
    if (const auto* literal = dynamic_cast<const ir::IrLiteral*>(&expr)) {
        return literal->text;
    }
    if (const auto* name = dynamic_cast<const ir::IrName*>(&expr)) {
        return name->name;
    }
    if (dynamic_cast<const ir::IrSelf*>(&expr) != nullptr) {
        return "Self";
    }
    if (dynamic_cast<const ir::IrParent*>(&expr) != nullptr) {
        return "Parent";
    }
    if (const auto* member = dynamic_cast<const ir::IrMember*>(&expr)) {
        return expression(*member->target, kPostfixPrecedence) + "." + member->name;
    }
    if (const auto* index = dynamic_cast<const ir::IrIndex*>(&expr)) {
        return expression(*index->target, kPostfixPrecedence) + "[" +
               expression(*index->index, 0) + "]";
    }
    if (const auto* call = dynamic_cast<const ir::IrCall*>(&expr)) {
        std::string arguments;
        for (std::size_t i = 0; i < call->arguments.size(); ++i) {
            if (i > 0) arguments += ", ";
            const auto& argument = call->arguments[i];
            if (argument.name) {
                arguments += *argument.name + " = " + expression(*argument.value, 0);
            } else {
                arguments += expression(*argument.value, 0);
            }
        }
        std::string target;
        if (call->receiver) target = expression(*call->receiver, kPostfixPrecedence) + ".";
        return target + call->name + "(" + arguments + ")";
    }
    if (const auto* unary = dynamic_cast<const ir::IrUnary*>(&expr)) {
        const std::string text = unary->op + expression(*unary->operand, kUnaryPrecedence);
        return parentPrecedence > kUnaryPrecedence ? "(" + text + ")" : text;
    }
    if (const auto* binary = dynamic_cast<const ir::IrBinary*>(&expr)) {
        const int prec = precedence(binary->op);
        const std::string text = expression(*binary->left, prec) + " " + binary->op + " " +
                                 expression(*binary->right, prec + 1);
        return parentPrecedence > prec ? "(" + text + ")" : text;
    }
    if (const auto* cast = dynamic_cast<const ir::IrCast*>(&expr)) {
        const std::string text = expression(*cast->value, kCastPrecedence) + " as " +
                                 typeText(cast->targetType, cast->targetIsArray);
        return parentPrecedence > kCastPrecedence ? "(" + text + ")" : text;
    }
    if (const auto* check = dynamic_cast<const ir::IrTypeCheck*>(&expr)) {
        const std::string text = expression(*check->value, kCastPrecedence) + " is " +
                                 typeText(check->targetType, check->targetIsArray);
        return parentPrecedence > kCastPrecedence ? "(" + text + ")" : text;
    }
    if (const auto* array = dynamic_cast<const ir::IrNewArray*>(&expr)) {
        return "new " + array->elementType + "[" + expression(*array->size, 0) + "]";
    }
    if (const auto* structure = dynamic_cast<const ir::IrNewStruct*>(&expr)) {
        return "new " + structure->structName;
    }
    return "None";
}

std::string PapyrusSourceWriter::write(const ir::IrScript& script, GraphSourceMap& map) {
    text_.clear();
    map_ = GraphSourceMap{};
    depth_ = 0;
    line_ = 1;

    if (options_.emitHeaderComment) writeHeaderComment();
    writeHeaderLine(script);

    for (const auto& declared : script.structs) writeStruct(declared);

    if (!script.imports.empty()) {
        blank();
        for (const auto& import : script.imports) line("Import " + import);
    }

    if (!script.customEvents.empty()) {
        blank();
        for (const auto& declared : script.customEvents) line("CustomEvent " + declared);
    }

    if (!script.variables.empty() || !script.properties.empty()) blank();
    for (const auto& variable : script.variables) line(declarationText(variable));
    for (const auto& property : script.properties) line(propertyText(property));

    for (const auto& callable : script.callables) {
        if (callable.stateName && !callable.stateName->empty()) continue;
        blank();
        writeCallable(callable);
    }

    std::vector<std::string> stateOrder;
    for (const auto& callable : script.callables) {
        if (!callable.stateName || callable.stateName->empty()) continue;
        const bool seen = std::any_of(stateOrder.begin(), stateOrder.end(),
                                      [&](const std::string& existing) {
                                          return iequals(existing, *callable.stateName);
                                      });
        if (!seen) stateOrder.push_back(*callable.stateName);
    }

    for (const auto& state : stateOrder) {
        blank();
        const bool isAuto =
            script.autoState && iequals(*script.autoState, state);
        line((isAuto ? "Auto State " : "State ") + state);
        ++depth_;
        bool first = true;
        for (const auto& callable : script.callables) {
            if (!callable.stateName || !iequals(*callable.stateName, state)) continue;
            if (!first) blank();
            writeCallable(callable);
            first = false;
        }
        depth_--;
        line("EndState");
    }

    map = map_;
    return text_;
}

}
