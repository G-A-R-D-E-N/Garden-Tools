#include <fo4ide/papyrus/Parser.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <utility>

#include <fo4ide/papyrus/Lexer.hpp>

namespace fo4ide::papyrus {
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
        const auto x = a[i];
        const auto y = b[i];
        if ((x >= 'A' && x <= 'Z' ? x - 'A' + 'a' : x) !=
            (y >= 'A' && y <= 'Z' ? y - 'A' + 'a' : y)) {
            return false;
        }
    }
    return true;
}

}  // namespace

Script Parser::parse(std::string_view text, std::optional<std::string> filePath) {
    DiagnosticBag bag;
    auto tokens = Lexer::lex(text, bag);
    auto script = Parser(tokens, bag).parseScript();
    script.filePath = std::move(filePath);
    if (script.filePath) bag.setFile(*script.filePath);
    script.diagnostics = bag.items();
    return script;
}

std::optional<Script> Parser::parseFile(const std::filesystem::path& path) {
    std::error_code error;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return std::nullopt;
    const auto size = std::filesystem::file_size(path, error);
    if (error) return std::nullopt;
    std::string text;
    text.resize(static_cast<std::size_t>(size));
    stream.read(text.data(), static_cast<std::streamsize>(size));
    if (!stream && size != 0) return std::nullopt;
    return parse(text, path.string());
}

Parser::Parser(const std::vector<Token>& tokens, DiagnosticBag& diagnostics)
    : tokens_(tokens), diagnostics_(diagnostics) {}

Token Parser::expect(TokenKind expected, std::string_view what) {
    if (kind() == expected) return advance();
    diagnostics_.report(
        DiagnosticCode::ExpectedToken,
        "Expected " + std::string(what) + ", found '" + describe(current()) + "'.",
        current().span);
    return Token{expected, "", Span{current().span.start, 0, current().span.line, current().span.column}};
}

std::string Parser::describe(const Token& token) {
    switch (token.kind) {
        case TokenKind::EndOfFile: return "end of file";
        case TokenKind::Newline: return "end of line";
        default: return token.text;
    }
}

void Parser::skipNewlines() {
    while (kind() == TokenKind::Newline) advance();
}

void Parser::expectEndOfLine() {
    if (kind() == TokenKind::Newline || atEnd()) {
        match(TokenKind::Newline);
        return;
    }
    diagnostics_.report(
        DiagnosticCode::UnexpectedToken,
        "Unexpected '" + describe(current()) + "' at end of line.",
        current().span);
    skipToNextLine();
}

void Parser::skipToNextLine() {
    while (!atEnd() && kind() != TokenKind::Newline) advance();
    match(TokenKind::Newline);
}

std::optional<std::string> Parser::takeDocComment() {
    const auto save = index_;
    skipNewlines();
    if (kind() == TokenKind::DocComment) return advance().text;
    index_ = save;
    return std::nullopt;
}

void Parser::parseFlags(std::vector<std::string>& into) {
    while (true) {
        switch (kind()) {
            case TokenKind::Identifier:
            case TokenKind::Const:
            case TokenKind::Auto:
            case TokenKind::AutoReadOnly:
            case TokenKind::DebugOnly:
            case TokenKind::BetaOnly:
            case TokenKind::Native:
            case TokenKind::Global:
                into.push_back(lower(advance().text));
                continue;
            default:
                return;
        }
    }
}

Script Parser::parseScript() {
    Script script;
    parseHeader(script);

    while (!atEnd()) {
        skipNewlines();
        if (atEnd()) break;

        const auto before = index_;
        parseScriptMember(script);

        if (index_ == before) advance();
    }

    const auto start = script.nameSpan.start;
    const auto last = tokens_.back().span;
    script.span = Span{0, std::max(last.end(), start), 1, 1};
    return script;
}

void Parser::parseHeader(Script& script) {
    skipNewlines();

    while (kind() == TokenKind::DocComment) {
        if (!script.documentation) script.documentation = advance().text;
        skipNewlines();
    }

    if (kind() != TokenKind::ScriptName) {
        diagnostics_.report(
            DiagnosticCode::ExpectedScriptName,
            "Script must start with a 'ScriptName' header line.",
            current().span);
        return;
    }

    const auto keyword = advance();
    Span nameSpan;
    script.name = parseQualifiedName(nameSpan);
    script.nameSpan = nameSpan;
    script.span = keyword.span.to(nameSpan);

    if (kind() == TokenKind::Extends) {
        advance();
        script.extends = parseQualifiedName(script.extendsSpan);
    }

    parseFlags(script.flags);
    expectEndOfLine();
    if (!script.documentation) script.documentation = takeDocComment();
}

std::string Parser::parseQualifiedName(Span& span) {
    const auto first = expect(TokenKind::Identifier, "an identifier");
    span = first.span;
    auto name = first.text;
    // '#' is accepted as a ':' equivalent so that decompiled PEX type names
    // (Script#Struct) reparse; the C# oracle rejects '#' here.
    while (kind() == TokenKind::Colon || kind() == TokenKind::Hash) {
        advance();
        const auto part = expect(TokenKind::Identifier, "an identifier after ':'");
        name += ":" + part.text;
        span = span.to(part.span);
    }
    return name;
}

void Parser::parseScriptMember(Script& script) {
    switch (kind()) {
        case TokenKind::Import:
            parseImport(script);
            return;
        case TokenKind::Struct:
            script.structs.push_back(parseStruct());
            return;
        case TokenKind::CustomEvent:
            parseCustomEvent(script);
            return;
        case TokenKind::Group:
            parseGroup(script);
            return;
        case TokenKind::State:
            script.states.push_back(parseState());
            return;
        case TokenKind::Auto:
            if (peek(1).kind == TokenKind::State) {
                script.states.push_back(parseState());
                return;
            }
            break;
        case TokenKind::Event:
            script.events.push_back(parseEvent(std::nullopt));
            return;
        case TokenKind::Function:
            script.functions.push_back(parseFunction(std::nullopt, std::nullopt));
            return;
        case TokenKind::EndOfFile:
            return;
        default:
            break;
    }

    TypeRef type;
    if (!tryParseType(type)) {
        diagnostics_.report(
            DiagnosticCode::UnexpectedToken,
            "Unexpected '" + describe(current()) + "' at script level.",
            current().span);
        skipToNextLine();
        return;
    }

    switch (kind()) {
        case TokenKind::Function:
            script.functions.push_back(parseFunction(TypeRef{type}, std::nullopt));
            return;
        case TokenKind::Property:
            script.properties.push_back(parseProperty(std::move(type), std::nullopt));
            return;
        case TokenKind::Identifier:
            script.variables.push_back(parseVariable(std::move(type), false));
            return;
        default:
            diagnostics_.report(
                DiagnosticCode::UnexpectedToken,
                "Expected 'Function', 'Property' or a variable name after type '" +
                    type.toString() + "', found '" + describe(current()) + "'.",
                current().span);
            skipToNextLine();
            return;
    }
}

void Parser::parseImport(Script& script) {
    const auto keyword = advance();
    Span nameSpan;
    auto name = parseQualifiedName(nameSpan);
    Import import;
    import.name = std::move(name);
    import.nameSpan = nameSpan;
    import.span = keyword.span.to(nameSpan);
    script.imports.push_back(std::move(import));
    expectEndOfLine();
}

void Parser::parseCustomEvent(Script& script) {
    const auto keyword = advance();
    const auto name = expect(TokenKind::Identifier, "a custom event name");
    CustomEventDecl decl;
    decl.name = name.text;
    decl.nameSpan = name.span;
    decl.span = keyword.span.to(name.span);
    script.customEvents.push_back(std::move(decl));
    expectEndOfLine();
}

bool Parser::isTypeStart(TokenKind candidate) {
    switch (candidate) {
        case TokenKind::Int:
        case TokenKind::Float:
        case TokenKind::Bool:
        case TokenKind::String:
        case TokenKind::Var:
        case TokenKind::Identifier:
        case TokenKind::ScriptEventName:
        case TokenKind::CustomEventName:
        case TokenKind::StructVarName:
            return true;
        default:
            return false;
    }
}

bool Parser::tryParseType(TypeRef& type) {
    if (!isTypeStart(kind())) return false;
    type = parseType();
    return true;
}

TypeRef Parser::parseType() {
    auto start = current().span;
    std::string name;

    switch (kind()) {
        case TokenKind::Int:
        case TokenKind::Float:
        case TokenKind::Bool:
        case TokenKind::String:
        case TokenKind::Var:
        case TokenKind::ScriptEventName:
        case TokenKind::CustomEventName:
        case TokenKind::StructVarName:
            name = lower(advance().text);
            break;
        case TokenKind::Identifier: {
            Span span;
            name = parseQualifiedName(span);
            start = start.to(span);
            break;
        }
        default:
            diagnostics_.report(
                DiagnosticCode::ExpectedType,
                "Expected a type, found '" + describe(current()) + "'.",
                current().span);
            return TypeRef{"", false, current().span};
    }

    auto isArray = false;
    if (kind() == TokenKind::LBracket && peek(1).kind == TokenKind::RBracket) {
        advance();
        const auto close = advance();
        isArray = true;
        start = start.to(close.span);
    }

    return TypeRef{std::move(name), isArray, start};
}

VariableDecl Parser::parseVariable(TypeRef type, bool allowDocumentation) {
    const auto name = expect(TokenKind::Identifier, "a variable name");
    VariableDecl decl;
    decl.type = std::move(type);
    decl.name = name.text;
    decl.nameSpan = name.span;

    if (match(TokenKind::Assign)) decl.initializer = parseExpression();

    parseFlags(decl.flags);
    const auto lastIndex = index_ == 0 ? std::size_t{0} : index_ - 1;
    decl.span = decl.type.span.to(tokens_[lastIndex].span);
    expectEndOfLine();

    if (allowDocumentation) decl.documentation = takeDocComment();
    return decl;
}

StructDecl Parser::parseStruct() {
    const auto keyword = advance();
    const auto name = expect(TokenKind::Identifier, "a struct name");
    StructDecl decl;
    decl.name = name.text;
    decl.nameSpan = name.span;
    expectEndOfLine();
    decl.documentation = takeDocComment();

    while (true) {
        skipNewlines();
        if (atEnd() || kind() == TokenKind::EndStruct) break;

        const auto before = index_;
        TypeRef memberType;
        if (tryParseType(memberType)) {
            decl.members.push_back(parseVariable(std::move(memberType), true));
        } else {
            diagnostics_.report(
                DiagnosticCode::UnexpectedToken,
                "Expected a struct member or 'EndStruct', found '" + describe(current()) + "'.",
                current().span);
            skipToNextLine();
        }
        if (index_ == before) advance();
    }

    const auto end = expectBlockEnd(TokenKind::EndStruct, "EndStruct", keyword.span);
    if (decl.members.empty()) {
        diagnostics_.report(
            DiagnosticCode::StructNeedsMember,
            "Struct '" + decl.name + "' must declare at least one member.",
            decl.nameSpan);
    }
    decl.span = keyword.span.to(end);
    return decl;
}

PropertyDecl Parser::parseProperty(TypeRef type, std::optional<std::string> groupName) {
    const auto keyword = advance();
    const auto name = expect(TokenKind::Identifier, "a property name");
    PropertyDecl decl;
    decl.type = std::move(type);
    decl.name = name.text;
    decl.nameSpan = name.span;
    decl.groupName = std::move(groupName);

    if (match(TokenKind::Assign)) decl.initializer = parseExpression();

    parseFlags(decl.flags);
    decl.kind = decl.hasFlag("autoreadonly")
                    ? PropertyKind::AutoReadOnly
                    : (decl.hasFlag("auto") ? PropertyKind::Auto : PropertyKind::Full);

    const auto lastIndex = index_ == 0 ? std::size_t{0} : index_ - 1;
    decl.span = decl.type.span.to(tokens_[lastIndex].span);
    expectEndOfLine();
    decl.documentation = takeDocComment();

    if (decl.kind != PropertyKind::Full || decl.name.empty()) return decl;

    while (true) {
        skipNewlines();
        if (atEnd() || kind() == TokenKind::EndProperty) break;

        const auto before = index_;
        std::optional<TypeRef> returnType;
        if (kind() != TokenKind::Function) {
            TypeRef candidate;
            if (tryParseType(candidate)) returnType = candidate;
        }

        if (kind() == TokenKind::Function) {
            auto accessor = parseFunction(std::move(returnType), std::nullopt);
            if (iequals(accessor.name, "get")) decl.getter = std::move(accessor);
            else if (iequals(accessor.name, "set")) decl.setter = std::move(accessor);
        } else {
            diagnostics_.report(
                DiagnosticCode::UnexpectedToken,
                "Expected a Get or Set function, or 'EndProperty', found '" +
                    describe(current()) + "'.",
                current().span);
            skipToNextLine();
        }
        if (index_ == before) advance();
    }

    const auto end = expectBlockEnd(TokenKind::EndProperty, "EndProperty", keyword.span);
    if (!decl.getter && !decl.setter) {
        diagnostics_.report(
            DiagnosticCode::PropertyNeedsAccessor,
            "Property '" + decl.name + "' is not auto and has neither a Get nor a Set function.",
            decl.nameSpan);
    }
    decl.span = decl.span.to(end);
    return decl;
}

void Parser::parseGroup(Script& script) {
    const auto keyword = advance();
    const auto name = expect(TokenKind::Identifier, "a group name");
    GroupDecl group;
    group.name = name.text;
    group.nameSpan = name.span;
    parseFlags(group.flags);
    expectEndOfLine();
    group.documentation = takeDocComment();

    while (true) {
        skipNewlines();
        if (atEnd() || kind() == TokenKind::EndGroup) break;

        const auto before = index_;
        TypeRef type;
        if (tryParseType(type) && kind() == TokenKind::Property) {
            script.properties.push_back(parseProperty(std::move(type), group.name));
            group.properties.push_back(&script.properties.back());
        } else {
            diagnostics_.report(
                DiagnosticCode::UnexpectedToken,
                "Expected a property or 'EndGroup', found '" + describe(current()) + "'.",
                current().span);
            skipToNextLine();
        }
        if (index_ == before) advance();
    }

    const auto end = expectBlockEnd(TokenKind::EndGroup, "EndGroup", keyword.span);
    group.span = keyword.span.to(end);
    script.groups.push_back(std::move(group));
}

StateDecl Parser::parseState() {
    const auto start = current().span;
    const auto isAuto = match(TokenKind::Auto);
    expect(TokenKind::State, "'State'");
    const auto name = expect(TokenKind::Identifier, "a state name");
    StateDecl state;
    state.name = name.text;
    state.nameSpan = name.span;
    state.isAuto = isAuto;
    expectEndOfLine();

    while (true) {
        skipNewlines();
        if (atEnd() || kind() == TokenKind::EndState) break;

        const auto before = index_;
        if (kind() == TokenKind::Event) {
            state.events.push_back(parseEvent(state.name));
        } else if (kind() == TokenKind::Function) {
            state.functions.push_back(parseFunction(std::nullopt, state.name));
        } else {
            TypeRef returnType;
            if (tryParseType(returnType) && kind() == TokenKind::Function) {
                state.functions.push_back(parseFunction(TypeRef{returnType}, state.name));
            } else {
                diagnostics_.report(
                    DiagnosticCode::UnexpectedToken,
                    "Expected a function, an event or 'EndState', found '" +
                        describe(current()) + "'.",
                    current().span);
                skipToNextLine();
            }
        }
        if (index_ == before) advance();
    }

    const auto end = expectBlockEnd(TokenKind::EndState, "EndState", start);
    state.span = start.to(end);
    return state;
}

Span Parser::expectBlockEnd(TokenKind expected, std::string_view what, const Span& openedAt) {
    if (kind() == expected) {
        const auto& token = advance();
        expectEndOfLine();
        return token.span;
    }
    diagnostics_.report(
        DiagnosticCode::UnterminatedBlock,
        "Block opened here is never closed; expected '" + std::string(what) + "'.",
        openedAt);
    return current().span;
}

FunctionDecl Parser::parseFunction(std::optional<TypeRef> returnType,
                                   std::optional<std::string> stateName) {
    const auto keyword = advance();
    const auto start = returnType ? returnType->span : keyword.span;
    const auto name = expect(TokenKind::Identifier, "a function name");
    FunctionDecl fn;
    fn.returnType = std::move(returnType);
    fn.name = name.text;
    fn.nameSpan = name.span;
    fn.stateName = std::move(stateName);

    parseParameterList(fn.parameters);
    parseFlags(fn.flags);
    fn.isGlobal = fn.hasFlag("global");
    fn.isNative = fn.hasFlag("native");
    expectEndOfLine();
    fn.documentation = takeDocComment();

    if (fn.isNative) {
        fn.span = start.to(name.span);
        return fn;
    }

    fn.body = parseBlock(TokenKind::EndFunction);
    const auto end = expectBlockEnd(TokenKind::EndFunction, "EndFunction", start);
    fn.span = start.to(end);
    return fn;
}

EventDecl Parser::parseEvent(std::optional<std::string> stateName) {
    const auto keyword = advance();
    EventDecl evt;
    evt.stateName = std::move(stateName);

    Span firstSpan;
    const auto first = parseQualifiedName(firstSpan);
    if (kind() == TokenKind::Dot) {
        advance();
        evt.remoteObjectType = first;
        const auto name = expect(TokenKind::Identifier, "an event name after '.'");
        evt.name = name.text;
        evt.nameSpan = name.span;
    } else {
        evt.name = first;
        evt.nameSpan = firstSpan;
    }

    parseParameterList(evt.parameters);
    parseFlags(evt.flags);
    evt.isNative = evt.hasFlag("native");
    expectEndOfLine();
    evt.documentation = takeDocComment();

    if (evt.isNative) {
        evt.span = keyword.span.to(evt.nameSpan);
        return evt;
    }

    evt.body = parseBlock(TokenKind::EndEvent);
    const auto end = expectBlockEnd(TokenKind::EndEvent, "EndEvent", keyword.span);
    evt.span = keyword.span.to(end);
    return evt;
}

void Parser::parseParameterList(std::vector<Parameter>& into) {
    expect(TokenKind::LParen, "'('");
    if (match(TokenKind::RParen)) return;

    while (true) {
        const auto before = index_;
        TypeRef type;
        if (!tryParseType(type)) {
            diagnostics_.report(
                DiagnosticCode::ExpectedType,
                "Expected a parameter type, found '" + describe(current()) + "'.",
                current().span);
            break;
        }

        const auto name = expect(TokenKind::Identifier, "a parameter name");
        Parameter parameter;
        parameter.type = std::move(type);
        parameter.name = name.text;
        parameter.nameSpan = name.span;
        parameter.span = parameter.type.span.to(name.span);
        if (match(TokenKind::Assign)) {
            parameter.defaultValue = parseExpression();
            parameter.span = parameter.span.to(parameter.defaultValue->span);
        }
        into.push_back(std::move(parameter));

        if (match(TokenKind::Comma)) {
            if (index_ == before) advance();
            continue;
        }
        break;
    }

    expect(TokenKind::RParen, "')'");
}

bool Parser::isBlockTerminator(TokenKind candidate) {
    switch (candidate) {
        case TokenKind::EndFunction:
        case TokenKind::EndEvent:
        case TokenKind::EndIf:
        case TokenKind::EndWhile:
        case TokenKind::Else:
        case TokenKind::ElseIf:
        case TokenKind::EndProperty:
        case TokenKind::EndState:
        case TokenKind::EndStruct:
        case TokenKind::EndGroup:
        case TokenKind::EndOfFile:
            return true;
        default:
            return false;
    }
}

std::vector<StatementPtr> Parser::parseBlock(TokenKind terminator) {
    std::vector<StatementPtr> body;
    while (true) {
        skipNewlines();
        if (atEnd()) return body;
        if (isBlockTerminator(kind())) return body;
        if (kind() == terminator) return body;

        const auto before = index_;
        auto statement = parseStatement();
        if (statement) body.push_back(std::move(statement));
        if (index_ == before) advance();
    }
}

StatementPtr Parser::parseStatement() {
    switch (kind()) {
        case TokenKind::If:
            return parseIf();
        case TokenKind::While:
            return parseWhile();
        case TokenKind::Return:
            return parseReturn();
        default:
            break;
    }

    StatementPtr define;
    if (tryParseDefine(define)) return define;

    auto expression = parseExpression();
    if (isAssignmentOperator(kind())) {
        const auto op = advance();
        auto value = parseExpression();
        auto assign = std::make_unique<AssignStatement>();
        assign->target = std::move(expression);
        assign->op = op.kind;
        assign->value = std::move(value);
        assign->span = assign->target->span.to(assign->value->span);
        expectEndOfLine();
        return assign;
    }

    auto statement = std::make_unique<ExpressionStatement>();
    statement->expression = std::move(expression);
    statement->span = statement->expression->span;
    expectEndOfLine();
    return statement;
}

bool Parser::isAssignmentOperator(TokenKind candidate) {
    switch (candidate) {
        case TokenKind::Assign:
        case TokenKind::PlusAssign:
        case TokenKind::MinusAssign:
        case TokenKind::StarAssign:
        case TokenKind::SlashAssign:
        case TokenKind::PercentAssign:
            return true;
        default:
            return false;
    }
}

bool Parser::tryParseDefine(StatementPtr& statement) {
    if (!isTypeStart(kind())) return false;

    const auto save = index_;
    const auto savedDiagnostics = diagnostics_.items().size();

    auto type = parseType();
    if (kind() != TokenKind::Identifier) {
        index_ = save;
        diagnostics_.truncateTo(savedDiagnostics);
        return false;
    }

    const auto name = advance();
    auto define = std::make_unique<DefineStatement>();
    define->type = std::move(type);
    define->name = name.text;
    define->nameSpan = name.span;
    define->span = define->type.span.to(name.span);
    if (match(TokenKind::Assign)) {
        define->initializer = parseExpression();
        define->span = define->span.to(define->initializer->span);
    }
    parseFlags(define->flags);
    expectEndOfLine();
    statement = std::move(define);
    return true;
}

StatementPtr Parser::parseIf() {
    const auto keyword = advance();
    auto statement = std::make_unique<IfStatement>();

    auto condition = parseExpression();
    expectEndOfLine();
    IfBranch branch;
    branch.condition = std::move(condition);
    branch.body = parseBlock(TokenKind::EndOfFile);
    branch.span = branch.condition->span.to(
        branch.body.empty() ? branch.condition->span : branch.body.back()->span);
    statement->branches.push_back(std::move(branch));

    while (kind() == TokenKind::ElseIf) {
        advance();
        auto elseCondition = parseExpression();
        expectEndOfLine();
        IfBranch elseBranch;
        elseBranch.condition = std::move(elseCondition);
        elseBranch.body = parseBlock(TokenKind::EndOfFile);
        elseBranch.span = elseBranch.condition->span.to(
            elseBranch.body.empty() ? elseBranch.condition->span : elseBranch.body.back()->span);
        statement->branches.push_back(std::move(elseBranch));
    }

    if (kind() == TokenKind::Else) {
        advance();
        expectEndOfLine();
        statement->elseBody = parseBlock(TokenKind::EndOfFile);
    }

    const auto end = expectBlockEnd(TokenKind::EndIf, "EndIf", keyword.span);
    statement->span = keyword.span.to(end);
    return statement;
}

StatementPtr Parser::parseWhile() {
    const auto keyword = advance();
    auto statement = std::make_unique<WhileStatement>();
    statement->condition = parseExpression();
    expectEndOfLine();
    statement->body = parseBlock(TokenKind::EndOfFile);
    const auto end = expectBlockEnd(TokenKind::EndWhile, "EndWhile", keyword.span);
    statement->span = keyword.span.to(end);
    return statement;
}

StatementPtr Parser::parseReturn() {
    const auto keyword = advance();
    auto statement = std::make_unique<ReturnStatement>();
    statement->span = keyword.span;
    if (kind() != TokenKind::Newline && !atEnd()) {
        statement->value = parseExpression();
        statement->span = keyword.span.to(statement->value->span);
    }
    expectEndOfLine();
    return statement;
}

ExpressionPtr Parser::parseExpression() {
    return parseOr();
}

template <typename Operand>
ExpressionPtr Parser::parseLeftAssociative(Operand operand,
                                           std::initializer_list<TokenKind> operators) {
    auto left = operand();
    while (std::find(operators.begin(), operators.end(), kind()) != operators.end()) {
        const auto op = advance();
        auto right = operand();
        auto binary = std::make_unique<BinaryExpression>();
        binary->left = std::move(left);
        binary->op = op.kind;
        binary->right = std::move(right);
        binary->span = binary->left->span.to(binary->right->span);
        left = std::move(binary);
    }
    return left;
}

ExpressionPtr Parser::parseOr() {
    return parseLeftAssociative([this] { return parseAnd(); }, {TokenKind::Or});
}

ExpressionPtr Parser::parseAnd() {
    return parseLeftAssociative([this] { return parseComparison(); }, {TokenKind::And});
}

ExpressionPtr Parser::parseComparison() {
    return parseLeftAssociative([this] { return parseAdditive(); },
                                {TokenKind::Equal, TokenKind::NotEqual, TokenKind::Less,
                                 TokenKind::Greater, TokenKind::LessEqual, TokenKind::GreaterEqual});
}

ExpressionPtr Parser::parseAdditive() {
    return parseLeftAssociative([this] { return parseMultiplicative(); },
                                {TokenKind::Plus, TokenKind::Minus});
}

ExpressionPtr Parser::parseMultiplicative() {
    return parseLeftAssociative([this] { return parseUnary(); },
                                {TokenKind::Star, TokenKind::Slash, TokenKind::Percent});
}

ExpressionPtr Parser::parseUnary() {
    if (kind() == TokenKind::Minus || kind() == TokenKind::Not) {
        const auto op = advance();
        auto operand = parseUnary();
        auto unary = std::make_unique<UnaryExpression>();
        unary->op = op.kind;
        unary->operand = std::move(operand);
        unary->span = op.span.to(unary->operand->span);
        return unary;
    }
    return parseCast();
}

ExpressionPtr Parser::parseCast() {
    auto expression = parseDotChain();
    while (kind() == TokenKind::As || kind() == TokenKind::Is) {
        const auto op = advance();
        auto type = parseType();
        if (op.kind == TokenKind::As) {
            auto cast = std::make_unique<CastExpression>();
            cast->operand = std::move(expression);
            cast->type = std::move(type);
            cast->span = cast->operand->span.to(cast->type.span);
            expression = std::move(cast);
        } else {
            auto check = std::make_unique<TypeCheckExpression>();
            check->operand = std::move(expression);
            check->type = std::move(type);
            check->span = check->operand->span.to(check->type.span);
            expression = std::move(check);
        }
    }
    return expression;
}

ExpressionPtr Parser::parseDotChain() {
    auto expression = parsePostfix(parseAtom());

    while (kind() == TokenKind::Dot) {
        advance();

        Token name;
        if (kind() == TokenKind::Identifier || kind() == TokenKind::Length) {
            name = advance();
        } else {
            diagnostics_.report(
                DiagnosticCode::ExpectedIdentifier,
                "Expected a member name after '.', found '" + describe(current()) + "'.",
                current().span);
            name = Token{TokenKind::Identifier, "", current().span};
        }

        auto member = std::make_unique<MemberExpression>();
        member->target = std::move(expression);
        member->name = name.text;
        member->nameSpan = name.span;
        member->span = member->target->span.to(name.span);
        expression = parsePostfix(std::move(member));
    }

    return expression;
}

ExpressionPtr Parser::parsePostfix(ExpressionPtr expression) {
    while (true) {
        if (kind() == TokenKind::LParen) {
            auto call = std::make_unique<CallExpression>();
            call->callee = std::move(expression);
            Span closeSpan;
            parseArgumentList(call->arguments, closeSpan);
            call->span = call->callee->span.to(closeSpan);
            expression = std::move(call);
            continue;
        }

        if (kind() == TokenKind::LBracket) {
            advance();
            auto index = parseExpression();
            const auto close = expect(TokenKind::RBracket, "']'");
            auto indexed = std::make_unique<IndexExpression>();
            indexed->target = std::move(expression);
            indexed->index = std::move(index);
            indexed->span = indexed->target->span.to(close.span);
            expression = std::move(indexed);
            continue;
        }

        return expression;
    }
}

void Parser::parseArgumentList(std::vector<Argument>& into, Span& closeSpan) {
    const auto open = expect(TokenKind::LParen, "'('");
    closeSpan = open.span;

    if (kind() == TokenKind::RParen) {
        closeSpan = advance().span;
        return;
    }

    while (true) {
        const auto before = index_;
        Argument argument;

        if (kind() == TokenKind::Identifier && peek(1).kind == TokenKind::Assign) {
            const auto name = advance();
            advance();
            argument.name = name.text;
            argument.nameSpan = name.span;
        }

        argument.value = parseExpression();
        argument.span = argument.name ? argument.nameSpan.to(argument.value->span)
                                     : argument.value->span;
        into.push_back(std::move(argument));

        if (match(TokenKind::Comma)) {
            if (index_ == before) advance();
            continue;
        }
        break;
    }

    const auto close = expect(TokenKind::RParen, "')'");
    closeSpan = close.span;
}

ExpressionPtr Parser::parseAtom() {
    const auto& token = current();
    switch (kind()) {
        case TokenKind::LParen: {
            advance();
            auto inner = parseExpression();
            expect(TokenKind::RParen, "')'");
            return inner;
        }

        case TokenKind::New: {
            advance();
            auto type = parseType();
            if (kind() == TokenKind::LBracket) {
                advance();
                auto size = parseExpression();
                const auto close = expect(TokenKind::RBracket, "']'");
                auto array = std::make_unique<NewArrayExpression>();
                array->elementType = std::move(type);
                array->size = std::move(size);
                array->span = token.span.to(close.span);
                return array;
            }
            auto created = std::make_unique<NewStructExpression>();
            created->type = std::move(type);
            created->span = token.span.to(created->type.span);
            return created;
        }

        case TokenKind::IntLiteral:
        case TokenKind::FloatLiteral:
        case TokenKind::StringLiteral:
        case TokenKind::True:
        case TokenKind::False:
        case TokenKind::None: {
            advance();
            auto literal = std::make_unique<LiteralExpression>();
            literal->span = token.span;
            switch (token.kind) {
                case TokenKind::IntLiteral:
                    literal->kind = LiteralKind::Int;
                    literal->text = token.text;
                    break;
                case TokenKind::FloatLiteral:
                    literal->kind = LiteralKind::Float;
                    literal->text = token.text;
                    break;
                case TokenKind::StringLiteral:
                    literal->kind = LiteralKind::String;
                    literal->text = token.text;
                    break;
                case TokenKind::True:
                case TokenKind::False:
                    literal->kind = LiteralKind::Bool;
                    literal->text = lower(token.text);
                    break;
                case TokenKind::None:
                    literal->kind = LiteralKind::None;
                    literal->text = "none";
                    break;
                default:
                    break;
            }
            return literal;
        }

        case TokenKind::Identifier: {
            Span span;
            auto name = parseQualifiedName(span);
            auto identifier = std::make_unique<IdentifierExpression>();
            identifier->name = std::move(name);
            identifier->span = span;
            return identifier;
        }

        case TokenKind::Length:
        case TokenKind::ScriptEventName:
        case TokenKind::CustomEventName:
        case TokenKind::StructVarName: {
            advance();
            auto identifier = std::make_unique<IdentifierExpression>();
            identifier->name = token.text;
            identifier->span = token.span;
            return identifier;
        }

        default: {
            diagnostics_.report(
                DiagnosticCode::ExpectedExpression,
                "Expected an expression, found '" + describe(token) + "'.",
                token.span);
            auto error = std::make_unique<ErrorExpression>();
            error->span = Span{token.span.start, 0, token.span.line, token.span.column};
            return error;
        }
    }
}

}
