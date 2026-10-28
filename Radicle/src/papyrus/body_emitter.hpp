#pragma once

#include <optional>
#include <set>
#include <stack>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fo4/papyrus/ast.hpp>
#include <fo4/papyrus/binding.hpp>
#include <fo4/papyrus/code_generator.hpp>
#include <fo4/papyrus/token.hpp>
#include <fo4/papyrus/type.hpp>
#include <fo4/pex/pex.hpp>

namespace fo4::papyrus {

class BodyEmitter {
public:
    struct Local {
        std::string name;
        Type type;
    };
    struct Storage {
        std::string name;
        bool isProperty{false};
        std::optional<fo4::pex::PexValue> receiver;
    };

    BodyEmitter(CodeGenerator& gen, fo4::pex::PexFunction& fn, const CallableDecl& decl, const Type& returnType)
        : gen_(gen), fn_(fn), decl_(decl), returnType_(returnType) {}

    void run();

private:
    int add(const std::string& mnemonic, const std::vector<fo4::pex::PexValue>& args);
    int here() const { return static_cast<int>(fn_.instructions.size()); }
    void patchJump(int jumpIndex, int target);
    static fo4::pex::PexValue id(const std::string& name) {
        return fo4::pex::PexValue{fo4::pex::PexValueType::Identifier, name, 0, 0.0F, false};
    }
    static fo4::pex::PexValue intValue(int value) {
        return fo4::pex::PexValue{fo4::pex::PexValueType::Integer, "", value, 0.0F, false};
    }
    static fo4::pex::PexValue noneValue() { return fo4::pex::PexValue{fo4::pex::PexValueType::None, "", 0, 0.0F, false}; }

    std::string declareLocal(const std::string& sourceName, const Type& type);
    std::optional<Local> findLocal(const std::string& name);
    std::string noneVar();
    std::string allocTemp(const Type& type);
    void releaseTemps(int mark);

    void emitBlock(const std::vector<StatementPtr>& body);
    void emitStatement(const Statement* statement);
    void emitIf(const IfStatement& iff);
    void emitWhile(const WhileStatement& wh);
    void emitAssign(const AssignStatement& assign);
    static std::optional<TokenKind> compoundOperator(TokenKind assignOperator);

    void emitDiscarded(const Node* expression);
    fo4::pex::PexValue value(const Node* expression);
    fo4::pex::PexValue valueAs(const Node* expression, const Type& want);
    static bool needsNoCast(const Type& from, const Type& to);
    void emitInto(const Node* expression, const std::string& destination, const Type& destinationType);
    std::optional<fo4::pex::PexValue> tryDirectValue(const Node* expression);
    std::optional<Storage> storageFor(const IdentifierExpression& id);
    fo4::pex::PexValue propertyGet(const Storage& storage, const Node* at);
    fo4::pex::PexValue materialise(const fo4::pex::PexValue& value, const Type& type);

    void emitComputation(const Node* expression, const std::string& destination,
                         const Type& destinationType);
    void emitMemberRead(const MemberExpression& member, const std::string& destination);
    bool isOwnBackingProperty(const MemberExpression& member, const Binding& binding);
    static bool isAutoBackingProperty(const Binding& binding);
    void emitUnary(const UnaryExpression& unary, const std::string& destination,
                   const Type& destinationType);
    void emitBinary(const BinaryExpression& binary, const std::string& destination,
                    const Type& destinationType);
    fo4::pex::PexValue binaryInto(const std::optional<std::string>& destination, const Type& type,
                        TokenKind op, const fo4::pex::PexValue& left, const fo4::pex::PexValue& right,
                        const Span& span);
    static std::optional<std::string> arithmeticMnemonic(TokenKind op, const Type& type);
    static bool isComparison(TokenKind op);
    void emitComparison(const BinaryExpression& binary, const std::string& destination);
    Type commonComparisonType(const Type& left, const Type& right);
    bool inherits(const std::string& child, const std::string& ancestor);
    void emitShortCircuit(const BinaryExpression& binary, const std::string& destination);

    void emitCall(const CallExpression& call, const std::string& destination);
    void emit(const std::string& mnemonic, const std::vector<fo4::pex::PexValue>& parts);
    std::optional<std::vector<fo4::pex::PexValue>> buildArguments(const CallExpression& call,
                                                        const CallableDecl& callee,
                                                        const Script* calleeOwner);
    void emitArrayBuiltin(const CallExpression& call, const Binding& binding,
                          const std::string& destination);
    void cannotEmit(const Node& node);

    CodeGenerator& gen_;
    fo4::pex::PexFunction& fn_;
    const CallableDecl& decl_;
    const Type& returnType_;

    std::vector<std::unordered_map<std::string, Local>> scopes_;
    std::unordered_map<std::string, std::stack<std::string>> freeTemps_;
    std::vector<std::pair<std::string, std::string>> liveTemps_;
    std::set<std::string> usedLocalNames_;
    std::optional<std::string> noneVar_;
    int line_{0};
    int mangleCounter_{0};
};

}  // namespace fo4::papyrus
