#include <fo4/f4se/papyrus_header_emitter.hpp>

namespace fo4::f4se {

namespace {

bool isNone(const PapyrusTypeText& type) {
    return !type.isArray && CiEqual{}(type.name, "None");
}

}

PapyrusHeaderEmitter::PapyrusHeaderEmitter(const PluginBinding* plugin) {
    if (plugin == nullptr) return;
    for (const auto* declared : plugin->allStructs()) {
        structOwners_[declared->name] = declared->ownerScript;
    }
}

std::string PapyrusHeaderEmitter::emit(const PluginBinding& plugin,
                                       const ModuleBinding& module) const {
    (void)plugin;
    std::string text;
    text += headerLineFor(module);
    text += '\n';

    if (!module.structs.empty()) {
        for (const auto& declared : module.structs) {
            text += '\n';
            text += "struct " + declared.name + "\n";
            for (const auto& member : declared.members) {
                text += "\t" + typeTextFor(member.type, module) + " " + member.name + "\n";
            }
            text += "endstruct\n";
        }
    }

    std::vector<const NativeBinding*> natives;
    for (const auto& native : module.natives) {
        if (CiEqual{}(native.papyrusClass, module.scriptName)) natives.push_back(&native);
    }

    if (!natives.empty()) text += '\n';
    for (const auto* native : natives) {
        text += declarationFor(*native, module);
        text += '\n';
    }

    return text;
}

std::string PapyrusHeaderEmitter::headerLineFor(const ModuleBinding& module) {
    std::string line = "Scriptname " + module.scriptName;
    if (module.extends && !module.extends->empty()) {
        line += " extends " + *module.extends;
    }
    line += " Native Hidden";
    return line;
}

std::string PapyrusHeaderEmitter::declarationFor(const NativeBinding& native,
                                                 const ModuleBinding& module) const {
    std::vector<std::string> parameters;
    for (const auto& parameter : native.parameters) {
        auto text = typeTextFor(parameter.type, module) + " " + parameter.name;
        if (parameter.isOptional()) text += " = " + *parameter.defaultLiteral;
        parameters.push_back(std::move(text));
    }

    std::string text;
    if (!isNone(native.returnType)) {
        text += typeTextFor(native.returnType, module) + " ";
    }
    text += "Function " + native.functionName + "(";
    for (std::size_t i = 0; i < parameters.size(); i++) {
        if (i != 0) text += ", ";
        text += parameters[i];
    }
    text += ")";
    text += native.isGlobal() ? " native global" : " native";
    return text;
}

std::string PapyrusHeaderEmitter::typeTextFor(const PapyrusTypeText& type,
                                              const ModuleBinding& module) const {
    if (type.name.find(':') != std::string::npos) return type.toString();

    const auto found = structOwners_.find(type.name);
    const auto* owner = found != structOwners_.end() ? &found->second : nullptr;
    if (owner == nullptr || CiEqual{}(*owner, module.scriptName)) return type.toString();

    return PapyrusTypeText{*owner + ":" + type.name, type.isArray}.toString();
}

std::string PapyrusHeaderEmitter::scriptFileName(const ModuleBinding& module) {
    auto path = module.scriptName;
    for (auto& ch : path) {
        if (ch == ':') ch = '/';
    }
    return "Data/Scripts/Source/User/" + path + ".psc";
}

std::vector<OracleNative> PapyrusHeaderEmitter::declarationsOf(
    const ModuleBinding& module) const {
    std::vector<OracleNative> out;
    for (const auto& native : module.natives) {
        if (!CiEqual{}(native.papyrusClass, module.scriptName)) continue;
        OracleNative declared;
        declared.script = native.papyrusClass;
        declared.name = native.functionName;
        declared.returnType = native.returnType;
        declared.isGlobal = native.isGlobal();
        for (const auto& parameter : native.parameters) {
            declared.parameterTypes.push_back(parameter.type);
        }
        out.push_back(std::move(declared));
    }
    return out;
}

}
