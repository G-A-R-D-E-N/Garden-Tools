#include <fo4/papyrus/parser.hpp>

#include <fo4/papyrus/ast.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fo4::papyrus {

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

}  // namespace fo4::papyrus
