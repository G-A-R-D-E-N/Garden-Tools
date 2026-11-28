#include <fo4/f4se/binding_json.hpp>

#include <cstdint>
#include <limits>

#include <nlohmann/json.hpp>

namespace fo4::f4se::bindingJson {

namespace {

using Json = nlohmann::ordered_json;

struct Reader {
    std::string error;

    bool failed() const { return !error.empty(); }

    void fail(const std::string& where, const std::string& what) {
        if (error.empty()) error = where + ": " + what;
    }

    const Json* member(const Json& object, const char* key) {
        const auto found = object.find(key);
        if (found == object.end() || found->is_null()) return nullptr;
        return &*found;
    }

    std::optional<std::string> text(const Json& object, const char* key, const std::string& where,
                                    bool required) {
        const auto* value = member(object, key);
        if (value == nullptr) {
            if (required) fail(where, std::string("\"") + key + "\" is required");
            return std::nullopt;
        }
        if (!value->is_string()) {
            fail(where, std::string("\"") + key + "\" must be a string");
            return std::nullopt;
        }
        return value->get<std::string>();
    }

    bool flag(const Json& object, const char* key, const std::string& where) {
        const auto* value = member(object, key);
        if (value == nullptr) return false;
        if (!value->is_boolean()) {
            fail(where, std::string("\"") + key + "\" must be true or false");
            return false;
        }
        return value->get<bool>();
    }

    int number(const Json& object, const char* key, const std::string& where, int fallback) {
        const auto* value = member(object, key);
        if (value == nullptr) return fallback;
        if (!value->is_number_integer()) {
            fail(where, std::string("\"") + key + "\" must be an integer");
            return fallback;
        }
        const auto wide = value->get<std::int64_t>();
        if (wide < std::numeric_limits<int>::min() || wide > std::numeric_limits<int>::max()) {
            fail(where, std::string("\"") + key + "\" is out of range");
            return fallback;
        }
        return static_cast<int>(wide);
    }

    const Json* list(const Json& object, const char* key, const std::string& where) {
        const auto* value = member(object, key);
        if (value == nullptr) return nullptr;
        if (!value->is_array()) {
            fail(where, std::string("\"") + key + "\" must be an array");
            return nullptr;
        }
        return value;
    }

    bool isObject(const Json& value, const std::string& where) {
        if (value.is_object()) return true;
        fail(where, "must be an object");
        return false;
    }
};

PapyrusTypeText typeText(const std::string& written) {
    if (written.size() >= 2 && written.compare(written.size() - 2, 2, "[]") == 0) {
        return PapyrusTypeText{written.substr(0, written.size() - 2), true};
    }
    return PapyrusTypeText{written, false};
}

PapyrusTypeText typeMember(Reader& reader, const Json& object, const char* key,
                           const std::string& where, bool required) {
    const auto written = reader.text(object, key, where, required);
    return written ? typeText(*written) : PapyrusTypeText{"None", false};
}

F4SETarget targetOf(Reader& reader, const Json& root) {
    const auto* value = reader.member(root, "target");
    if (value == nullptr) return F4SETarget::Og_1_10_163;
    if (value->is_number_integer()) {
        const auto number = value->get<std::int64_t>();
        if (number == 0) return F4SETarget::Og_1_10_163;
        if (number == 1) return F4SETarget::Ng_0_7_8;
    } else if (value->is_string()) {
        const auto name = value->get<std::string>();
        if (CiEqual{}(name, "Og_1_10_163")) return F4SETarget::Og_1_10_163;
        if (CiEqual{}(name, "Ng_0_7_8")) return F4SETarget::Ng_0_7_8;
    }
    reader.fail("plugin", "\"target\" must be Og_1_10_163, Ng_0_7_8, 0 or 1");
    return F4SETarget::Og_1_10_163;
}

StructBinding structOf(Reader& reader, const Json& json, const std::string& where) {
    StructBinding declared;
    if (!reader.isObject(json, where)) return declared;
    declared.name = reader.text(json, "name", where, true).value_or("");
    if (declared.name.find_first_of("<>") != std::string::npos) {
        reader.fail(where, "\"name\" must not contain '<' or '>'");
    }
    declared.ownerScript = reader.text(json, "ownerScript", where, true).value_or("");
    if (const auto* members = reader.list(json, "members", where)) {
        std::size_t index = 0;
        for (const auto& item : *members) {
            const auto at = where + ".members[" + std::to_string(index++) + "]";
            if (!reader.isObject(item, at)) continue;
            StructMemberBinding member;
            member.name = reader.text(item, "name", at, true).value_or("");
            member.type = typeMember(reader, item, "type", at, true);
            member.isUnsigned = reader.flag(item, "unsigned", at);
            declared.members.push_back(std::move(member));
        }
    }
    return declared;
}

NativeBinding nativeOf(Reader& reader, const Json& json, const std::string& where) {
    NativeBinding native;
    if (!reader.isObject(json, where)) return native;
    native.functionName = reader.text(json, "functionName", where, true).value_or("");
    native.papyrusClass = reader.text(json, "papyrusClass", where, true).value_or("");
    native.cppBaseType = reader.text(json, "cppBaseType", where, true).value_or("");
    native.returnType = typeMember(reader, json, "returnType", where, false);
    native.returnUnsigned = reader.flag(json, "returnUnsigned", where);
    native.isLatent = reader.flag(json, "latent", where);
    native.noWait = reader.flag(json, "noWait", where);
    native.cppFunctionName = reader.text(json, "cppFunctionName", where, false).value_or("");
    native.sourceFile = reader.text(json, "sourceFile", where, false).value_or("");
    native.sourceLine = reader.number(json, "sourceLine", where, 0);
    if (const auto* parameters = reader.list(json, "parameters", where)) {
        std::size_t index = 0;
        for (const auto& item : *parameters) {
            const auto at = where + ".parameters[" + std::to_string(index++) + "]";
            if (!reader.isObject(item, at)) continue;
            NativeParameter parameter;
            parameter.name = reader.text(item, "name", at, true).value_or("");
            parameter.type = typeMember(reader, item, "type", at, true);
            parameter.defaultLiteral = reader.text(item, "default", at, false);
            parameter.isUnsigned = reader.flag(item, "unsigned", at);
            native.parameters.push_back(std::move(parameter));
        }
    }
    return native;
}

ModuleBinding moduleOf(Reader& reader, const Json& json, const std::string& where) {
    ModuleBinding module;
    if (!reader.isObject(json, where)) return module;
    module.name = reader.text(json, "name", where, true).value_or("");
    module.scriptName = reader.text(json, "scriptName", where, true).value_or("");
    module.extends = reader.text(json, "extends", where, false);
    module.cppNamespace = reader.text(json, "cppNamespace", where, false).value_or("");
    if (const auto* structs = reader.list(json, "structs", where)) {
        std::size_t index = 0;
        for (const auto& item : *structs) {
            module.structs.push_back(
                structOf(reader, item, where + ".structs[" + std::to_string(index++) + "]"));
        }
    }
    if (const auto* natives = reader.list(json, "natives", where)) {
        std::size_t index = 0;
        for (const auto& item : *natives) {
            module.natives.push_back(
                nativeOf(reader, item, where + ".natives[" + std::to_string(index++) + "]"));
        }
    }
    return module;
}

Json filesOf(const F4SEEmitResult& result) {
    Json files = Json::array();
    for (const auto& file : result.files) {
        files.push_back(Json{{"path", file.relativePath}, {"text", file.text}});
    }
    return files;
}

Json diagnosticsOf(const F4SEEmitResult& result) {
    Json diagnostics = Json::array();
    for (const auto& diagnostic : result.diagnostics) {
        diagnostics.push_back(
            Json{{"code", diagnostic.code},
                 {"severity", diagnostic.severity == DiagnosticSeverity::Error ? "error" : "warning"},
                 {"message", diagnostic.message}});
    }
    return diagnostics;
}

Json roundTripOf(const CrossCheckResult& check) {
    Json mismatches = Json::array();
    for (const auto& mismatch : check.mismatches) mismatches.push_back(mismatch.toString());
    Json cppOnly = Json::array();
    for (const auto& native : check.cppOnly) cppOnly.push_back(native.toString());
    Json pscOnly = Json::array();
    for (const auto& native : check.pscOnly) pscOnly.push_back(native.toString());
    return Json{{"agrees", check.agrees()},
                {"recovered", check.recovered},
                {"declared", check.declared},
                {"matched", check.matched},
                {"latentOnlyInCpp", check.latentOnlyInCpp},
                {"noWaitOnlyInCpp", check.noWaitOnlyInCpp},
                {"mismatches", std::move(mismatches)},
                {"cppOnly", std::move(cppOnly)},
                {"pscOnly", std::move(pscOnly)}};
}

}

PluginParse parsePlugin(std::string_view json) {
    PluginParse parsed;
    const auto root = Json::parse(json.begin(), json.end(), nullptr, false);
    if (root.is_discarded()) {
        parsed.error = "the plugin binding is not valid JSON";
        return parsed;
    }

    Reader reader;
    if (!reader.isObject(root, "plugin")) {
        parsed.error = reader.error;
        return parsed;
    }

    PluginBinding plugin;
    plugin.name = reader.text(root, "name", "plugin", true).value_or("");
    plugin.author = reader.text(root, "author", "plugin", false).value_or("");
    plugin.versionMajor = reader.number(root, "versionMajor", "plugin", 1);
    plugin.versionMinor = reader.number(root, "versionMinor", "plugin", 0);
    plugin.versionPatch = reader.number(root, "versionPatch", "plugin", 0);
    plugin.target = targetOf(reader, root);

    if (const auto* modules = reader.list(root, "modules", "plugin")) {
        std::size_t index = 0;
        for (const auto& item : *modules) {
            plugin.modules.push_back(
                moduleOf(reader, item, "modules[" + std::to_string(index++) + "]"));
        }
    }

    if (const auto* messages = reader.list(root, "messages", "plugin")) {
        std::size_t index = 0;
        for (const auto& item : *messages) {
            const auto at = "messages[" + std::to_string(index++) + "]";
            if (!reader.isObject(item, at)) continue;
            MessageSubscription message;
            message.messageName = reader.text(item, "messageName", at, true).value_or("");
            message.handlerName = reader.text(item, "handlerName", at, true).value_or("");
            plugin.messages.push_back(std::move(message));
        }
    }

    if (reader.failed()) {
        parsed.error = reader.error;
        return parsed;
    }
    parsed.plugin = std::move(plugin);
    return parsed;
}

ExistingParse parseExisting(std::string_view json) {
    ExistingParse parsed;
    const auto root = Json::parse(json.begin(), json.end(), nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        parsed.ok = false;
        parsed.error = "the existing files must be a JSON object of relative path to text";
        return parsed;
    }
    for (const auto& [path, text] : root.items()) {
        if (!text.is_string()) {
            parsed.ok = false;
            parsed.error = "existing file '" + path + "' must be a string";
            parsed.files.clear();
            return parsed;
        }
        parsed.files[path] = text.get<std::string>();
    }
    return parsed;
}

std::string serializeEmit(const F4SEEmitResult& result, const CrossCheckResult* roundTrip) {
    Json out = Json::object();
    out["success"] = result.success();
    out["files"] = filesOf(result);
    out["diagnostics"] = diagnosticsOf(result);
    out["orphanedBodies"] = Json(result.orphanedBodies);
    if (roundTrip != nullptr) out["roundTrip"] = roundTripOf(*roundTrip);
    return out.dump();
}

}
