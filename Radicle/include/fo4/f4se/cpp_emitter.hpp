#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fo4/f4se/binding_model.hpp>
#include <fo4/f4se/type_map.hpp>

namespace fo4::f4se {

class CppEmitter {
public:
    static constexpr const char* kUnimplementedMacro = "F4SE_BINDING_UNIMPLEMENTED";
    static constexpr const char* kAllowStubsMacro = "F4SE_BINDING_ALLOW_STUBS";

    explicit CppEmitter(std::vector<std::string> structNames = {})
        : map_(std::move(structNames)) {}

    std::string emitHeader(const PluginBinding& plugin, const ModuleBinding& module) const;
    std::string emitSource(const PluginBinding& plugin, const ModuleBinding& module) const;
    std::string signatureOf(const NativeBinding& native) const;
    std::string emitRegistrationsHeader(const PluginBinding& plugin) const;
    std::string emitRegistrationsSource(const PluginBinding& plugin) const;

    static std::string namespaceOf(const PluginBinding& plugin, const ModuleBinding& module);
    static std::string baseFileName(const PluginBinding& plugin, const ModuleBinding& module);
    static std::string headerFileName(const PluginBinding& plugin, const ModuleBinding& module);
    static std::string sourceFileName(const PluginBinding& plugin, const ModuleBinding& module);

private:
    std::string stubFor(const NativeBinding& native) const;
    std::optional<std::string> returnStatementFor(const NativeBinding& native) const;
    std::string registerFuncsFor(const std::string& ns, const ModuleBinding& module) const;
    static std::string receiverNameFor(const NativeBinding& native);
    std::optional<CppTypeRef> cppOf(const PapyrusTypeText& type, bool isUnsigned) const;
    std::vector<CppTypeRef> typesOf(const NativeBinding& native) const;

    F4SETypeMap map_;
};

}
