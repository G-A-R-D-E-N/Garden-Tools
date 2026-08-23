#pragma once

#include <optional>
#include <string>
#include <vector>

#include <fo4ide/f4se/TypeMap.hpp>

namespace fo4ide::f4se {

// Port of F4SETarget.
enum class F4SETarget {
    Og_1_10_163 = 0,
    Ng_0_7_8 = 1,
};

// Port of NativeParameter.
struct NativeParameter {
    std::string name;
    PapyrusTypeText type;
    std::optional<std::string> defaultLiteral;
    bool isUnsigned{false};

    bool isOptional() const { return defaultLiteral.has_value(); }
};

// Port of NativeBinding.
struct NativeBinding {
    std::string functionName;
    std::string papyrusClass;
    std::string cppBaseType;
    PapyrusTypeText returnType{std::string("None"), false};
    std::vector<NativeParameter> parameters;
    bool returnUnsigned{false};
    bool isLatent{false};
    bool noWait{false};
    std::string cppFunctionName;
    std::string sourceFile;
    int sourceLine{0};

    bool isGlobal() const { return cppBaseType == F4SETypeMap::kStaticFunctionTag; }
    int arity() const { return static_cast<int>(parameters.size()); }

    std::string toString() const {
        return returnType.toString() + " " + papyrusClass + "." + functionName + "/" +
               std::to_string(arity());
    }
};

// Port of StructMemberBinding.
struct StructMemberBinding {
    std::string name;
    PapyrusTypeText type;
    bool isUnsigned{false};
};

// Port of StructBinding.
struct StructBinding {
    std::string name;
    std::string ownerScript;
    std::vector<StructMemberBinding> members;

    std::string vmTypeName() const { return ownerScript + "#" + name; }
};

// Port of ModuleBinding.
struct ModuleBinding {
    std::string name;
    std::string scriptName;
    std::optional<std::string> extends;
    std::vector<StructBinding> structs;
    std::vector<NativeBinding> natives;
    std::string cppNamespace;
};

// Port of MessageSubscription.
struct MessageSubscription {
    std::string messageName;
    std::string handlerName;
};

// Port of PluginBinding.
struct PluginBinding {
    std::string name;
    std::string author;
    int versionMajor{1};
    int versionMinor{0};
    int versionPatch{0};
    F4SETarget target{F4SETarget::Og_1_10_163};
    std::vector<ModuleBinding> modules;
    std::vector<MessageSubscription> messages;

    // Port of AllNatives / AllStructs: flat views over every module.
    std::vector<const NativeBinding*> allNatives() const {
        std::vector<const NativeBinding*> out;
        for (const auto& module : modules) {
            for (const auto& native : module.natives) out.push_back(&native);
        }
        return out;
    }
    std::vector<const StructBinding*> allStructs() const {
        std::vector<const StructBinding*> out;
        for (const auto& module : modules) {
            for (const auto& declared : module.structs) out.push_back(&declared);
        }
        return out;
    }
};

// Port of EmittedFile.
struct EmittedFile {
    std::string relativePath;
    std::string text;
};

}  // namespace fo4ide::f4se
