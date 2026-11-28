#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/papyrus/ast.hpp>
#include <fo4/papyrus/binding.hpp>
#include <fo4/papyrus/diagnostic.hpp>
#include <fo4/papyrus/script_index.hpp>
#include <fo4/papyrus/type.hpp>
#include <fo4/papyrus/user_flags.hpp>
#include <fo4/pex/pex.hpp>

namespace fo4::papyrus {

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

class CodeGenerator {
public:
    explicit CodeGenerator(ScriptIndex& index) : index_(index) {}

    fo4::pex::PexFile* generate(const Script& script, const Resolution& resolution,
                      const CodeGenOptions& options, std::vector<Diagnostic>& diagnostics);

private:
    friend class BodyEmitter;

    fo4::pex::PexObject buildObject();
    fo4::pex::PexProperty buildProperty(const PropertyDecl& p);
    fo4::pex::PexFunction constantGetter(const PropertyDecl& property, const std::string& pexType);
    static std::string backingVarName(const std::string& propertyName);
    std::string parentClassName();
    std::vector<fo4::pex::PexPropertyGroup> buildPropertyGroups() const;
    std::vector<fo4::pex::PexStructOrder> buildStructOrders() const;

    fo4::pex::PexFunction buildFunction(const FunctionDecl& decl, const std::string& stateName,
                              const std::string& debugName, std::uint8_t debugType);
    fo4::pex::PexFunction buildEvent(const EventDecl& decl, const std::string& stateName);
    void emitBody(fo4::pex::PexFunction& fn, const CallableDecl& decl, const Type& returnType,
                  const std::string& stateName, const std::string& debugName,
                  std::uint8_t debugType);

    const Type& typeOf(const Node* node) const;
    const Type& typeOf(const TypeRef& reference) const {
        const Type* type = resolution_->typeOf(&reference);
        return type ? *type : Type::error();
    }
    std::optional<fo4::pex::PexValue> qualifyCustomEvent(const Node& argument,
                                               const std::vector<const Node*>& slots, int index,
                                               const std::string* receiverScript) const;
    const Script* declaringScriptOf(const std::string& scriptName,
                                    const std::string& eventName) const;
    Type foreignType(const TypeRef& reference, const Script* owner) const;

    void report(DiagnosticCode code, std::string message, const Span& span);
    std::string pexTypeName(const Type& type, const Span& at = {});
    fo4::pex::PexValue constantOrDefault(const Node* initializer, const Type& declared);
    static fo4::pex::PexValue zeroValue(const Type& type);
    static std::optional<fo4::pex::PexValue> tryConstant(const Node* expression, const Type* want = nullptr);

    const Script* script_{nullptr};
    const Resolution* resolution_{nullptr};
    UserFlagTable ownedUserFlags_;
    const UserFlagTable* userFlags_{nullptr};
    std::vector<Diagnostic>* diagnostics_{nullptr};
    std::vector<fo4::pex::PexDebugFunction> debug_;
    int tempCounter_{0};
    bool emitDebugOnly_{true};
    bool emitBetaOnly_{true};

    std::uint32_t conditionalMask() const { return userFlags_->maskFor("conditional"); }
    std::optional<std::string> excludedBy(const Binding& binding) const;

    ScriptIndex& index_;
};

}  // namespace fo4::papyrus
