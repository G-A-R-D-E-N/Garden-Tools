#include <fo4ide/f4se/BindingValidator.hpp>

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace fo4ide::f4se {

using graph::GraphDiagnostic;
using graph::errorDiagnostic;

namespace {

std::string toLower(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const auto ch : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

bool isIdentifier(const std::string& text) {
    if (text.empty()) return false;
    const auto first = static_cast<unsigned char>(text[0]);
    if (!std::isalpha(first) && text[0] != '_') return false;
    for (const auto ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if (!std::isalnum(c) && ch != '_') return false;
    }
    return true;
}

bool isScriptName(const std::string& text) {
    if (text.empty()) return false;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto colon = text.find(':', start);
        const auto part = text.substr(start, colon == std::string::npos ? std::string::npos
                                                                         : colon - start);
        if (!isIdentifier(part)) return false;
        if (colon == std::string::npos) break;
        start = colon + 1;
    }
    return true;
}

}  // namespace

std::vector<GraphDiagnostic> BindingValidator::validate(const PluginBinding& plugin) {
    std::vector<GraphDiagnostic> problems;

    if (plugin.name.empty() || !isIdentifier(plugin.name)) {
        problems.push_back(errorDiagnostic(
            std::string(graph::codes::kInvalidBindingName),
            "'" + plugin.name +
                "' is not usable as a plugin name: it becomes a C++ identifier and a file name."));
    }

    if (plugin.modules.empty()) {
        problems.push_back(errorDiagnostic(std::string(graph::codes::kNoModules),
                                           "A plugin has to declare at least one module to emit."));
    }

    std::vector<const StructBinding*> structOwners = plugin.allStructs();
    std::vector<std::string> structNames;
    structNames.reserve(structOwners.size());
    for (const auto* declared : structOwners) structNames.push_back(declared->name);
    F4SETypeMap map(std::move(structNames));

    validateStructs(plugin, structOwners, map, problems);
    validateModules(plugin, problems);
    validateNatives(plugin, map, problems);

    return problems;
}

void BindingValidator::validateModules(const PluginBinding& plugin,
                                       std::vector<GraphDiagnostic>& problems) {
    for (const auto& module : plugin.modules) {
        if (!isIdentifier(module.name)) {
            problems.push_back(errorDiagnostic(
                std::string(graph::codes::kInvalidBindingName),
                "Module name '" + module.name + "' is not a usable C++ identifier."));
        }
        if (!isScriptName(module.scriptName)) {
            problems.push_back(errorDiagnostic(
                std::string(graph::codes::kInvalidBindingName),
                "Script name '" + module.scriptName + "' is not a usable Papyrus script name."));
        }
    }

    // Duplicate modules writing into one script (case-insensitive).
    std::map<std::string, int, CiLess> byScript;
    for (const auto& module : plugin.modules) byScript[module.scriptName]++;
    for (const auto& [scriptName, count] : byScript) {
        if (count > 1) {
            problems.push_back(errorDiagnostic(
                std::string(graph::codes::kDuplicateDeclaration),
                std::to_string(count) + " modules write into script '" + scriptName +
                    "', which would emit one file twice."));
        }
    }
}

void BindingValidator::validateStructs(const PluginBinding& plugin,
                                       const std::vector<const StructBinding*>& structs,
                                       const F4SETypeMap& map,
                                       std::vector<GraphDiagnostic>& problems) {
    std::set<std::string, CiLess> scriptNames;
    for (const auto& module : plugin.modules) scriptNames.insert(module.scriptName);

    for (const auto& module : plugin.modules) {
        for (const auto& declared : module.structs) {
            const bool sameOwner = CiEqual{}(declared.ownerScript, module.scriptName);
            if (!sameOwner && scriptNames.find(declared.ownerScript) == scriptNames.end()) {
                problems.push_back(errorDiagnostic(
                    std::string(graph::codes::kStructOwnerMismatch),
                    "Struct '" + declared.name + "' names owner '" + declared.ownerScript +
                        "', which no module in this plugin emits."));
            }

            for (const auto& member : declared.members) {
                std::optional<CppTypeRef> cpp;
                std::string refusal;
                if (map.tryToCpp(member.type, member.isUnsigned, cpp, &refusal)) continue;
                problems.push_back(errorDiagnostic(
                    std::string(graph::codes::kUnmappedNativeType),
                    "Struct '" + declared.name + "' member '" + member.name + "' is typed '" +
                        member.type.toString() + "', which cannot cross the boundary: " +
                        refusal));
            }
        }
    }

    // Duplicate struct names (case-sensitive, matching the C# GroupBy Ordinal).
    std::map<std::string, int> byName;
    for (const auto* declared : structs) byName[declared->name]++;
    for (const auto& [name, count] : byName) {
        if (count > 1) {
            problems.push_back(errorDiagnostic(
                std::string(graph::codes::kDuplicateDeclaration),
                "Struct name '" + name + "' is declared " + std::to_string(count) +
                    " times; the C++ typedef uses the bare name, so they would collide."));
        }
    }
}

void BindingValidator::validateNatives(const PluginBinding& plugin, const F4SETypeMap& map,
                                       std::vector<GraphDiagnostic>& problems) {
    // Duplicate (class, function) registrations, case-insensitive.
    std::map<std::pair<std::string, std::string>, int> byKey;
    for (const auto* native : plugin.allNatives()) {
        byKey[{toLower(native->papyrusClass), toLower(native->functionName)}]++;
    }
    for (const auto& [key, count] : byKey) {
        if (count > 1) {
            problems.push_back(errorDiagnostic(
                std::string(graph::codes::kDuplicateNativeBinding),
                key.first + "." + key.second + " is registered " + std::to_string(count) +
                    " times."));
        }
    }

    for (const auto* native : plugin.allNatives()) {
        if (!isIdentifier(native->functionName)) {
            problems.push_back(errorDiagnostic(
                std::string(graph::codes::kInvalidBindingName),
                "'" + native->functionName + "' is not a usable Papyrus function name."));
        }

        if (native->arity() > kMaximumArity) {
            problems.push_back(errorDiagnostic(
                std::string(graph::codes::kNativeArityUnsupported),
                native->papyrusClass + "." + native->functionName + " takes " +
                    std::to_string(native->arity()) +
                    " parameters, and the NativeFunction template family stops at " +
                    std::to_string(kMaximumArity) + "."));
        }

        std::optional<CppTypeRef> cpp;
        std::string returnRefusal;
        if (!map.tryToCpp(native->returnType, native->returnUnsigned, cpp, &returnRefusal)) {
            problems.push_back(errorDiagnostic(
                std::string(graph::codes::kUnmappedNativeType),
                native->papyrusClass + "." + native->functionName + " returns '" +
                    native->returnType.toString() +
                    "', which cannot cross the boundary: " + returnRefusal));
        }

        std::set<std::string, CiLess> names;
        for (const auto& parameter : native->parameters) {
            if (!names.insert(parameter.name).second) {
                problems.push_back(errorDiagnostic(
                    std::string(graph::codes::kDuplicateVariableName),
                    native->papyrusClass + "." + native->functionName +
                        " has two parameters named '" + parameter.name + "'."));
            }

            std::string refusal;
            if (map.tryToCpp(parameter.type, parameter.isUnsigned, cpp, &refusal)) continue;
            problems.push_back(errorDiagnostic(
                std::string(graph::codes::kUnmappedNativeType),
                native->papyrusClass + "." + native->functionName + " parameter '" +
                    parameter.name + "' is typed '" + parameter.type.toString() +
                    "', which cannot cross the boundary: " + refusal));
        }

        // A required parameter may not follow an optional one.
        int firstOptional = -1;
        for (int i = 0; i < native->arity(); i++) {
            if (native->parameters[static_cast<std::size_t>(i)].isOptional()) {
                if (firstOptional < 0) firstOptional = i;
            } else if (firstOptional >= 0) {
                problems.push_back(errorDiagnostic(
                    std::string(graph::codes::kArgumentCount),
                    native->papyrusClass + "." + native->functionName + " parameter '" +
                        native->parameters[static_cast<std::size_t>(i)].name +
                        "' is required but follows an optional one."));
                break;
            }
        }
    }
}

bool BindingValidator::isIdentifier(const std::string& text) {
    return ::fo4ide::f4se::isIdentifier(text);
}

bool BindingValidator::isScriptName(const std::string& text) {
    return ::fo4ide::f4se::isScriptName(text);
}

}  // namespace fo4ide::f4se
