#include <fo4/papyrus/parser.hpp>

#include "parser_detail.hpp"

#include <fo4/papyrus/ast.hpp>

#include <algorithm>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::papyrus {

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
                    literal->text = detail::lower(token.text);
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

}  // namespace fo4::papyrus
