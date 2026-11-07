#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <fo4ide/papyrus/Ast.hpp>
#include <fo4ide/papyrus/Binding.hpp>
#include <fo4ide/papyrus/Diagnostic.hpp>
#include <fo4ide/papyrus/Pex.hpp>
#include <fo4ide/papyrus/ScriptIndex.hpp>
#include <fo4ide/papyrus/Type.hpp>

namespace fo4ide::papyrus {

// Port of PapyrusUserFlagTable: the Institute_Papyrus_Flags.flg table used to
// turn written flag names into bit masks.
class UserFlagTable {
public:
    std::uint32_t maskFor(std::string_view flagName) const;
    bool knows(std::string_view flagName) const;
    std::uint32_t maskFor(const std::vector<std::string>& written) const;

    const std::vector<PexUserFlag>& flags() const { return flags_; }

    static UserFlagTable fallout4Default();
    static UserFlagTable parse(std::string_view text);
    static UserFlagTable fromFileOrDefault(const std::filesystem::path& path);
    static std::optional<std::filesystem::path> findFlagFile(
        const std::vector<std::filesystem::path>& roots);

private:
    void declare(std::string name, std::uint8_t bit);
    void declareComposite(std::string name, std::uint32_t mask);
    void sort();

    std::unordered_map<std::string, std::uint32_t> masks_;
    std::vector<PexUserFlag> flags_;
};

// Port of PapyrusCodeGenOptions.
struct CodeGenOptions {
    std::string sourceFileName;
    std::string userName;
    std::string computerName;
    std::int64_t compilationTime{0};
    std::int64_t modificationTime{0};
    bool emitDebugInfo{true};
    bool emitDebugOnlyCode{true};
    bool emitBetaOnlyCode{true};
    const UserFlagTable* userFlags{nullptr};
};

// Port of PapyrusDeclarationCheck: duplicate-declaration diagnostics.
std::vector<Diagnostic> declarationCheck(const Script& script);

// Port of PapyrusCodeGenerator: AST + Resolution -> PexFile. Returns nullptr
// (with diagnostics out) when anything cannot be emitted faithfully.
class CodeGenerator {
public:
    explicit CodeGenerator(ScriptIndex& index) : index_(index) {}

    PexFile* generate(const Script& script, const Resolution& resolution,
                      const CodeGenOptions& options, std::vector<Diagnostic>& diagnostics);

private:
    friend class BodyEmitter;

    PexObject buildObject();
    PexProperty buildProperty(const PropertyDecl& p);
    PexFunction constantGetter(const PropertyDecl& property, const std::string& pexType);
    static std::string backingVarName(const std::string& propertyName);
    std::string parentClassName();
    std::vector<PexPropertyGroup> buildPropertyGroups() const;
    std::vector<PexStructOrder> buildStructOrders() const;

    PexFunction buildFunction(const FunctionDecl& decl, const std::string& stateName,
                              const std::string& debugName, std::uint8_t debugType);
    PexFunction buildEvent(const EventDecl& decl, const std::string& stateName);
    void emitBody(PexFunction& fn, const CallableDecl& decl, const Type& returnType,
                  const std::string& stateName, const std::string& debugName,
                  std::uint8_t debugType);

    const Type& typeOf(const Node* node) const;
    const Type& typeOf(const TypeRef& reference) const {
        const Type* type = resolution_->typeOf(&reference);
        return type ? *type : Type::error();
    }
    std::optional<PexValue> qualifyCustomEvent(const Node& argument,
                                               const std::vector<const Node*>& slots, int index,
                                               const std::string* receiverScript) const;
    const Script* declaringScriptOf(const std::string& scriptName,
                                    const std::string& eventName) const;
    Type foreignType(const TypeRef& reference, const Script* owner) const;

    void report(DiagnosticCode code, std::string message, const Span& span);
    // Non-const: the Error case reports a diagnostic.
    std::string pexTypeName(const Type& type, const Span& at = {});
    PexValue constantOrDefault(const Node* initializer, const Type& declared);
    static PexValue zeroValue(const Type& type);
    static std::optional<PexValue> tryConstant(const Node* expression, const Type* want = nullptr);

    const Script* script_{nullptr};
    const Resolution* resolution_{nullptr};
    // Owns the default table when options carry none; userFlags_ points at it
    // (or at the caller's table) for the whole generate() call.
    UserFlagTable ownedUserFlags_;
    const UserFlagTable* userFlags_{nullptr};
    std::vector<Diagnostic>* diagnostics_{nullptr};
    std::vector<PexDebugFunction> debug_;
    int tempCounter_{0};
    bool emitDebugOnly_{true};
    bool emitBetaOnly_{true};

    std::uint32_t conditionalMask() const { return userFlags_->maskFor("conditional"); }
    std::optional<std::string> excludedBy(const Binding& binding) const;

    ScriptIndex& index_;
};

}  // namespace fo4ide::papyrus
