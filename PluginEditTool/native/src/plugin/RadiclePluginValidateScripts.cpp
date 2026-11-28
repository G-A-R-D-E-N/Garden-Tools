#include <fo4ide/plugin/PluginValidation.hpp>

#include "RadiclePluginValidateInternal.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fo4/f4se/type_map.hpp>
#include <fo4/pex/pex.hpp>
#include <fo4/plugin/reader.hpp>
#include <fo4/schema/vmad.hpp>

namespace fo4ide::plugin::detail {
namespace {

enum class TypeShape {
    unknown,
    primitive,
    object,
    primitiveArray,
    objectArray,
};

TypeShape shapeOf(std::uint8_t type) {
    switch (type) {
        case 2: return TypeShape::primitive;
        case 3: return TypeShape::primitive;
        case 4: return TypeShape::primitive;
        case 5: return TypeShape::primitive;
        case 1: return TypeShape::object;
        case 12: return TypeShape::primitiveArray;
        case 13: return TypeShape::primitiveArray;
        case 14: return TypeShape::primitiveArray;
        case 15: return TypeShape::primitiveArray;
        case 11: return TypeShape::objectArray;
        default: return TypeShape::unknown;
    }
}

std::string_view primitiveName(std::uint8_t type) {
    switch (type) {
        case 2: return "String";
        case 3: return "Int";
        case 4: return "Float";
        case 5: return "Bool";
        case 12: return "String[]";
        case 13: return "Int[]";
        case 14: return "Float[]";
        case 15: return "Bool[]";
        default: return {};
    }
}

bool isPrimitiveType(std::string_view type) {
    return sameName(type, "None") || sameName(type, "Int") || sameName(type, "Float") ||
           sameName(type, "String") || sameName(type, "Bool");
}

std::string typeMismatch(std::uint8_t vmadType, std::string_view declared) {
    switch (shapeOf(vmadType)) {
        case TypeShape::unknown:
            return {};
        case TypeShape::primitive:
        case TypeShape::primitiveArray:
            if (sameName(declared, primitiveName(vmadType))) return {};
            return std::string(primitiveName(vmadType));
        case TypeShape::object: {
            if (!declared.ends_with("[]") && !isPrimitiveType(declared)) return {};
            return std::string("an object class name");
        }
        case TypeShape::objectArray: {
            if (!declared.ends_with("[]")) return std::string("an object class array");
            std::string base(declared.substr(0, declared.size() - 2));
            if (!isPrimitiveType(base)) return {};
            return std::string("an object class array");
        }
    }
    return {};
}

std::filesystem::path resolveScriptFile(const std::vector<std::filesystem::path>& roots,
                                        const std::string& name) {
    for (const auto& root : roots) {
        // A namespaced script "A:B:C" compiles to A\B\C.pex; the colon spelling is not a file name.
        std::vector<std::filesystem::path> candidates;
        if (name.find(':') == std::string::npos) {
            candidates.push_back(root / (name + ".pex"));
        } else {
            std::filesystem::path nested = root;
            std::size_t start = 0;
            for (auto colon = name.find(':'); colon != std::string::npos;
                 start = colon + 1, colon = name.find(':', start)) {
                nested /= name.substr(start, colon - start);
            }
            candidates.push_back(nested / (name.substr(start) + ".pex"));
            std::string underscore = name;
            for (auto& ch : underscore) {
                if (ch == ':') ch = '_';
            }
            candidates.push_back(root / (underscore + ".pex"));
        }
        for (const auto& candidate : candidates) {
            std::error_code ec;
            if (std::filesystem::is_regular_file(candidate, ec) && !ec) return candidate;
        }
    }
    return {};
}

std::optional<fo4::pex::PexFile> readPex(const std::filesystem::path& path, std::string& failure) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        failure = "Could not open " + path.string();
        return std::nullopt;
    }
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>{file},
                                          std::istreambuf_iterator<char>{}};
    try {
        return fo4::pex::PexFile::fromBytes(bytes);
    } catch (const std::exception& error) {
        failure = std::string("Could not decode ") + path.filename().string() + ": " + error.what();
        return std::nullopt;
    }
}

std::string baseScriptName(const std::string& name) {
    const auto colon = name.rfind(':');
    return colon == std::string::npos ? name : name.substr(colon + 1);
}

bool isNativePapyrusType(std::string_view name) {
    static const auto forms = fo4::f4se::F4SETypeMap::formTypes();
    static constexpr std::array<std::string_view, 4> scripts = {"Quest", "Alias", "ReferenceAlias",
                                                                "ActiveMagicEffect"};
    if (forms.contains(std::string(name))) return true;
    return std::any_of(scripts.begin(), scripts.end(),
                       [&](std::string_view script) { return sameName(script, name); });
}

struct ScriptProperty {
    std::string name;
    std::string type;
};

struct ScriptObject {
    std::string name;
    std::string parent;
    std::vector<ScriptProperty> properties;
};

// What the checks need from one .pex. A plugin attaches the same script to many records and
// scripts share parents, so each file is read and decoded once for a whole validation run.
struct LoadedScript {
    std::filesystem::path path;
    bool readable{false};
    std::string failure;
    std::vector<ScriptObject> objects;

    const ScriptObject* object(std::string_view name) const {
        for (const auto& candidate : objects) {
            if (sameName(candidate.name, name) ||
                sameName(candidate.name, baseScriptName(std::string(name)))) {
                return &candidate;
            }
        }
        return nullptr;
    }
};

class ScriptCache {
public:
    explicit ScriptCache(const std::vector<std::filesystem::path>& roots) : roots_(roots) {}

    const LoadedScript& load(const std::string& name) {
        const auto key = asciiLower(name);
        if (const auto found = scripts_.find(key); found != scripts_.end()) return found->second;
        LoadedScript script;
        script.path = resolveScriptFile(roots_, name);
        if (!script.path.empty()) {
            if (const auto pex = readPex(script.path, script.failure)) {
                script.readable = true;
                script.objects.reserve(pex->objects.size());
                for (const auto& object : pex->objects) {
                    ScriptObject slim{object.name, object.parentClassName, {}};
                    slim.properties.reserve(object.properties.size());
                    for (const auto& property : object.properties) {
                        slim.properties.push_back({property.name, property.type});
                    }
                    script.objects.push_back(std::move(slim));
                }
            }
        }
        return scripts_.emplace(key, std::move(script)).first->second;
    }

private:
    const std::vector<std::filesystem::path>& roots_;
    std::unordered_map<std::string, LoadedScript> scripts_;
};

const ScriptProperty* findProperty(const ScriptObject& object, std::string_view name) {
    for (const auto& property : object.properties) {
        if (sameName(property.name, name)) return &property;
    }
    return nullptr;
}

struct PropertyLookup {
    const ScriptProperty* property{nullptr};
    std::string unresolvedParent;
};

PropertyLookup findInherited(const ScriptObject& object, std::string_view name, ScriptCache& cache) {
    const ScriptObject* current = &object;
    for (int depth = 0; depth < 32; ++depth) {
        if (const auto* property = findProperty(*current, name)) return {property, {}};
        const auto& parent = current->parent;
        if (parent.empty() || sameName(parent, "Object")) return {};
        const auto& script = cache.load(parent);
        if (!script.readable) {
            if (script.path.empty() && isNativePapyrusType(parent)) return {};
            return {nullptr, parent};
        }
        const auto* next = script.object(parent);
        if (next == nullptr) return {nullptr, parent};
        current = next;
    }
    return {};
}

void checkScript(const std::string& scriptName,
                 const fo4::VmadScript& script,
                 ScriptCache& cache,
                 const fo4::Record& record,
                 FindingSink& sink) {
    const auto formKey = record.form_key.to_string();
    const auto recordType = record.header.signature.to_string();
    const auto editorId = editorIdOf(record);

    const auto& loaded = cache.load(scriptName);
    const auto& path = loaded.path;
    if (path.empty()) {
        sink.add("SCRIPT_MISSING", Severity::warning, formKey, editorId, recordType, "VMAD",
                 "VMAD attaches script " + scriptName + " but no compiled " + scriptName +
                     ".pex is in the scripts folders searched; it may be packed in an archive, so the properties were not compared");
        return;
    }
    if (!loaded.readable) {
        sink.add("SCRIPT_UNREADABLE", Severity::error, formKey, editorId, recordType, "VMAD",
                 loaded.failure);
        return;
    }

    const auto* object = loaded.object(scriptName);
    if (object == nullptr) {
        sink.add("SCRIPT_OBJECT_MISSING", Severity::error, formKey, editorId, recordType, "VMAD",
                 path.filename().string() + " does not define the object " + scriptName);
        return;
    }

    for (const auto& property : script.properties) {
        const auto lookup = findInherited(*object, property.name, cache);
        if (lookup.property == nullptr && !lookup.unresolvedParent.empty()) {
            sink.add("SCRIPT_PARENT_MISSING", Severity::warning, formKey, editorId, recordType,
                     property.name,
                     "VMAD property " + property.name + " is not declared by " + scriptName +
                         ", and its parent " + lookup.unresolvedParent +
                         " has no readable .pex, so it may be inherited");
            continue;
        }
        if (lookup.property == nullptr) {
            sink.add("SCRIPT_PROPERTY_MISSING", Severity::error, formKey, editorId, recordType,
                     property.name,
                     "VMAD property " + property.name + " does not exist in " +
                         path.filename().string() + " or its parents, so the binding is dropped "
                         "when the script loads");
            continue;
        }
        const auto* declared = lookup.property;

        const auto mismatch = typeMismatch(property.type, declared->type);
        if (!mismatch.empty()) {
            sink.add("SCRIPT_PROPERTY_TYPE_MISMATCH", Severity::error, formKey, editorId,
                     recordType, property.name,
                     "VMAD property " + property.name + " binds " + mismatch + " but " +
                         path.filename().string() + " declares it as " + declared->type);
        }

        if (property.type == 1) {
            const bool boundToForm = property.object.has_value() && !property.object->is_null();
            const bool boundToAlias = property.alias.value_or(-1) >= 0;
            if (!boundToForm && !boundToAlias) {
                sink.add("SCRIPT_PROPERTY_UNBOUND", Severity::warning, formKey, editorId, recordType,
                         property.name,
                         "VMAD property " + property.name +
                             " is an object property with no form and no alias, so the script sees None");
            }
        } else if (property.type == 11 && property.object_elements.has_value()) {
            const auto& elements = *property.object_elements;
            const bool allBound = std::all_of(elements.begin(), elements.end(),
                                              [](const fo4::VmadObjectElement& element) {
                                                  return !element.object.is_null();
                                              });
            if (!allBound) {
                sink.add("SCRIPT_PROPERTY_UNBOUND", Severity::warning, formKey, editorId, recordType,
                         property.name,
                         "VMAD array property " + property.name +
                             " has an element that is None, so the script sees None for that entry");
            }
        }
    }
}

}

void validateScripts(const fo4::Plugin& plugin,
                     const ValidationOptions& options,
                     FindingSink& sink) {
    if (options.scriptsDirectories.empty()) {
        sink.add("SCRIPT_CHECK_SKIPPED", Severity::info, {}, {}, {}, "VMAD",
                 "No script directory was supplied, so VMAD scripts were not compared with "
                 "compiled .pex files");
        return;
    }

    ScriptCache cache(options.scriptsDirectories);
    for (const auto* record : fo4::collect_records(plugin)) {
        if (record->header.signature == fo4::sig_tes4) continue;
        const auto* vmad = findSubrecord(*record, fo4::Signature::from_string("VMAD"));
        if (vmad == nullptr) continue;

        fo4::VmadScripts scripts;
        try {
            scripts = fo4::read_vmad_scripts(vmad->data);
        } catch (const std::exception& error) {
            sink.add("VMAD_UNREADABLE", Severity::warning, record->form_key.to_string(),
                     editorIdOf(*record), record->header.signature.to_string(), "VMAD",
                     std::string("VMAD could not be decoded: ") + error.what());
            continue;
        }
        if (scripts.unsupported) {
            sink.add("VMAD_UNREADABLE", Severity::warning, record->form_key.to_string(),
                     editorIdOf(*record), record->header.signature.to_string(), "VMAD",
                     "VMAD holds a property shape this reader stops at, so later scripts "
                     "were not checked");
        }
        for (const auto& script : scripts.scripts) {
            checkScript(script.name, script, cache, *record, sink);
        }
    }
}

}
