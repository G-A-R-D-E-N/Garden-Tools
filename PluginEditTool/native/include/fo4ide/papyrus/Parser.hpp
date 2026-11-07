#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fo4ide/papyrus/Ast.hpp>
#include <fo4ide/papyrus/Diagnostic.hpp>
#include <fo4ide/papyrus/Token.hpp>

namespace fo4ide::papyrus {

// Port of the C# PapyrusParser: recursive-descent parser producing the AST.
// Recovery behavior is faithful: broken members/statements cost only their own
// line, blocks report UnterminatedBlock instead of hanging, diagnostics are
// capped by the bag, and the synthetic-token/rollback handling matches the
// oracle exactly.
class Parser {
public:
    static Script parse(std::string_view text, std::optional<std::string> filePath = std::nullopt);

    // Reads and parses a .psc file; nullopt on any I/O failure (mirrors the
    // C# ParseFile's try/catch returning null).
    static std::optional<Script> parseFile(const std::filesystem::path& path);
    static std::optional<Script> parseFile(const std::filesystem::path& path,
                                           std::size_t maxBytes, std::size_t* bytesRead);

private:
    Parser(const std::vector<Token>& tokens, DiagnosticBag& diagnostics);

    Script parseScript();
    void parseHeader(Script& script);
    void parseScriptMember(Script& script);

    void parseImport(Script& script);
    void parseCustomEvent(Script& script);
    StructDecl parseStruct();
    PropertyDecl parseProperty(TypeRef type, std::optional<std::string> groupName);
    VariableDecl parseVariable(TypeRef type, bool allowDocumentation);
    void parseGroup(Script& script);
    StateDecl parseState();
    FunctionDecl parseFunction(std::optional<TypeRef> returnType,
                               std::optional<std::string> stateName);
    EventDecl parseEvent(std::optional<std::string> stateName);
    void parseParameterList(std::vector<Parameter>& into);

    std::vector<StatementPtr> parseBlock(TokenKind terminator);
    StatementPtr parseStatement();
    bool tryParseDefine(StatementPtr& statement);
    StatementPtr parseIf();
    StatementPtr parseWhile();
    StatementPtr parseReturn();

    template <typename Operand>
    ExpressionPtr parseLeftAssociative(Operand operand,
                                       std::initializer_list<TokenKind> operators);

    ExpressionPtr parseExpression();
    ExpressionPtr parseOr();
    ExpressionPtr parseAnd();
    ExpressionPtr parseComparison();
    ExpressionPtr parseAdditive();
    ExpressionPtr parseMultiplicative();
    ExpressionPtr parseUnary();
    ExpressionPtr parseCast();
    ExpressionPtr parseDotChain();
    ExpressionPtr parsePostfix(ExpressionPtr expression);
    void parseArgumentList(std::vector<Argument>& into, Span& closeSpan);
    ExpressionPtr parseAtom();

    // Helpers mirroring the C# cursor.
    const Token& current() const { return tokens_[index_]; }
    TokenKind kind() const { return tokens_[index_].kind; }
    const Token& peek(int offset) const {
        const auto i = static_cast<std::size_t>(static_cast<int>(index_) + offset);
        return i < tokens_.size() ? tokens_[i] : tokens_.back();
    }
    bool atEnd() const { return kind() == TokenKind::EndOfFile; }
    const Token& advance() {
        const auto& token = tokens_[index_];
        if (index_ < tokens_.size() - 1) ++index_;
        return token;
    }
    bool match(TokenKind expected) {
        if (kind() != expected) return false;
        advance();
        return true;
    }
    Token expect(TokenKind expected, std::string_view what);
    void skipNewlines();
    void expectEndOfLine();
    void skipToNextLine();
    std::optional<std::string> takeDocComment();
    void parseFlags(std::vector<std::string>& into);

    std::string parseQualifiedName(Span& span);
    bool tryParseType(TypeRef& type);
    TypeRef parseType();
    static bool isTypeStart(TokenKind candidate);
    static bool isAssignmentOperator(TokenKind candidate);
    static bool isBlockTerminator(TokenKind candidate);
    Span expectBlockEnd(TokenKind expected, std::string_view what, const Span& openedAt);
    static std::string describe(const Token& token);

    const std::vector<Token>& tokens_;
    DiagnosticBag& diagnostics_;
    std::size_t index_{0};
};

}
