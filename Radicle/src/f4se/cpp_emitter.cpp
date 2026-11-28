#include <fo4/f4se/cpp_emitter.hpp>

#include <cctype>

#include <fo4/f4se/region_merge.hpp>

namespace fo4::f4se {

std::string CppEmitter::emitHeader(const PluginBinding& plugin,
                                   const ModuleBinding& module) const {
    std::string text;
    text += "#pragma once\n\n";
    text += "class VirtualMachine;\n\n";

    if (!module.structs.empty()) {
        text += "#include \"f4se/PapyrusStruct.h\"\n\n";
        for (const auto& declared : module.structs) {
            text += "DECLARE_EXTERN_STRUCT(" + declared.name + ")\n";
        }
        text += '\n';
    }

    text += "namespace " + namespaceOf(plugin, module) + "\n";
    text += "{\n";
    text += "\tvoid RegisterFuncs(VirtualMachine * vm);\n";
    text += "}\n";
    return text;
}

std::string CppEmitter::emitSource(const PluginBinding& plugin,
                                   const ModuleBinding& module) const {
    const auto ns = namespaceOf(plugin, module);

    std::vector<CppTypeRef> types;
    for (const auto& native : module.natives) {
        for (auto& type : typesOf(native)) types.push_back(std::move(type));
    }
    for (const auto& declared : module.structs) {
        for (const auto& member : declared.members) {
            if (const auto cpp = cppOf(member.type, member.isUnsigned)) {
                types.push_back(*cpp);
            }
        }
    }

    std::string text;
    text += "#include \"" + headerFileName(plugin, module) + "\"\n\n";
    for (const auto& include : map_.requiredIncludesFor(types)) {
        text += "#include \"" + include + "\"\n";
    }
    text += '\n';

    text += "#ifndef " + std::string(kAllowStubsMacro) + "\n";
    text += "#define " + std::string(kUnimplementedMacro) + "(name) \\\n";
    text += "\tstatic_assert(false, \"F4SE binding '\" name \"' has no implementation. \" \\\n";
    text += "\t\t\"Write one, or define " + std::string(kAllowStubsMacro) +
            " to build without it.\")\n";
    text += "#else\n";
    text += "#define " + std::string(kUnimplementedMacro) + "(name) ((void)0)\n";
    text += "#endif\n\n";

    text += "namespace " + ns + "\n{\n";

    for (const auto& declared : module.structs) {
        text += "\tDECLARE_STRUCT(" + declared.name + ", \"" + declared.ownerScript + "\")\n";
    }
    if (!module.structs.empty()) text += '\n';

    for (const auto& native : module.natives) {
        text += stubFor(native);
        text += '\n';
    }

    text += "}\n\n";
    text += registerFuncsFor(ns, module);
    return text;
}

std::string CppEmitter::stubFor(const NativeBinding& native) const {
    std::string text;
    text += '\t';
    text += signatureOf(native);
    text += '\n';
    text += "\t{\n";
    text += "\t\t" + RegionMerge::begin(native.functionName) + "\n";
    text += "\t\t" + std::string(kUnimplementedMacro) + "(\"" + native.papyrusClass + "." +
            native.functionName + "\");\n";

    if (const auto returnStatement = returnStatementFor(native)) {
        text += "\t\t" + *returnStatement + "\n";
    }

    text += "\t\t" + RegionMerge::end(native.functionName) + "\n";
    text += "\t}\n";
    return text;
}

std::string CppEmitter::signatureOf(const NativeBinding& native) const {
    const auto returnType = cppOf(native.returnType, native.returnUnsigned);
    const auto returnText = returnType ? returnType->format() : std::string("void");

    std::vector<std::string> parameters;
    if (native.isGlobal()) {
        parameters.push_back(std::string(F4SETypeMap::kStaticFunctionTag) + " * base");
    } else {
        parameters.push_back(native.cppBaseType + " * " + receiverNameFor(native));
    }
    for (const auto& parameter : native.parameters) {
        const auto type = cppOf(parameter.type, parameter.isUnsigned);
        parameters.push_back((type ? type->format() : std::string("void")) + " " +
                             parameter.name);
    }

    std::string text = returnText + " " + native.functionName + "(";
    for (std::size_t i = 0; i < parameters.size(); i++) {
        if (i != 0) text += ", ";
        text += parameters[i];
    }
    text += ")";
    return text;
}

std::optional<std::string> CppEmitter::returnStatementFor(const NativeBinding& native) const {
    const auto type = cppOf(native.returnType, native.returnUnsigned);
    if (!type || type->name == "void") return std::nullopt;
    return "return " + type->format() + "();";
}

std::string CppEmitter::registerFuncsFor(const std::string& ns,
                                         const ModuleBinding& module) const {
    std::string text;
    text += "void " + ns + "::RegisterFuncs(VirtualMachine * vm)\n";
    text += "{\n";

    for (const auto& native : module.natives) {
        const auto returnType = cppOf(native.returnType, native.returnUnsigned)
                                    ? *cppOf(native.returnType, native.returnUnsigned)
                                    : CppTypeRef("void");
        std::vector<CppTypeRef> parameters;
        for (const auto& parameter : native.parameters) {
            parameters.push_back(cppOf(parameter.type, parameter.isUnsigned)
                                     ? *cppOf(parameter.type, parameter.isUnsigned)
                                     : CppTypeRef("void"));
        }

        const auto instantiation = F4SETypeMap::templateInstantiation(
            native.isLatent, native.arity(), native.cppBaseType, returnType, parameters);

        text += "\tvm->RegisterFunction(\n";
        text += "\t\tnew " + instantiation + "(\"" + native.functionName + "\", \"" +
                native.papyrusClass + "\", " + ns + "::" + native.functionName + ", vm));\n";
    }

    std::vector<const NativeBinding*> flagged;
    for (const auto& native : module.natives) {
        if (native.noWait) flagged.push_back(&native);
    }
    if (!flagged.empty()) {
        text += '\n';
        for (const auto* native : flagged) {
            text += "\tvm->SetFunctionFlags(\"" + native->papyrusClass + "\", \"" +
                    native->functionName + "\", IFunction::kFunctionFlag_NoWait);\n";
        }
    }

    text += "}\n";
    return text;
}

std::string CppEmitter::emitRegistrationsHeader(const PluginBinding& plugin) const {
    (void)plugin;
    return "#pragma once\n\nclass VirtualMachine;\n\nbool RegisterAllFuncs(VirtualMachine * vm);\n";
}

std::string CppEmitter::emitRegistrationsSource(const PluginBinding& plugin) const {
    std::string text;
    text += "#include \"" + plugin.name + "Registrations.h\"\n\n";
    text += "#include \"f4se/PapyrusVM.h\"\n";
    for (const auto& module : plugin.modules) {
        text += "#include \"" + headerFileName(plugin, module) + "\"\n";
    }
    text += '\n';

    text += "bool RegisterAllFuncs(VirtualMachine * vm)\n{\n";
    for (const auto& module : plugin.modules) {
        text += "\t" + namespaceOf(plugin, module) + "::RegisterFuncs(vm);\n";
    }
    text += "\treturn true;\n}\n";
    return text;
}

std::string CppEmitter::namespaceOf(const PluginBinding& plugin, const ModuleBinding& module) {
    (void)plugin;
    return module.cppNamespace.empty()
               ? "papyrus" + plugin.name + module.name
               : module.cppNamespace;
}

std::string CppEmitter::baseFileName(const PluginBinding& plugin, const ModuleBinding& module) {
    (void)module;
    return "Papyrus" + plugin.name + module.name;
}

std::string CppEmitter::headerFileName(const PluginBinding& plugin, const ModuleBinding& module) {
    return baseFileName(plugin, module) + ".h";
}

std::string CppEmitter::sourceFileName(const PluginBinding& plugin, const ModuleBinding& module) {
    return baseFileName(plugin, module) + ".cpp";
}

std::string CppEmitter::receiverNameFor(const NativeBinding& native) {
    auto name = native.cppBaseType;
    const auto colon = name.rfind(':');
    if (colon != std::string::npos) name = name.substr(colon + 1);
    if (name.empty()) return "self";

    name[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[0])));
    return name == "base" ? "self" : name;
}

std::optional<CppTypeRef> CppEmitter::cppOf(const PapyrusTypeText& type,
                                            bool isUnsigned) const {
    std::optional<CppTypeRef> cpp;
    if (!map_.tryToCpp(type, isUnsigned, cpp, nullptr)) return std::nullopt;
    return cpp;
}

std::vector<CppTypeRef> CppEmitter::typesOf(const NativeBinding& native) const {
    std::vector<CppTypeRef> out;
    if (const auto returnType = cppOf(native.returnType, native.returnUnsigned)) {
        out.push_back(*returnType);
    }
    for (const auto& parameter : native.parameters) {
        if (const auto type = cppOf(parameter.type, parameter.isUnsigned)) {
            out.push_back(*type);
        }
    }
    return out;
}

}
