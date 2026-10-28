#include <fo4/f4se/emitter.hpp>

#include <fo4/f4se/binding_validator.hpp>
#include <fo4/f4se/cpp_emitter.hpp>
#include <fo4/f4se/papyrus_header_emitter.hpp>
#include <fo4/f4se/plugin_emitter.hpp>
#include <fo4/f4se/region_merge.hpp>
#include <fo4/f4se/registration_extractor.hpp>

#include <algorithm>

namespace fo4::f4se {

namespace {

bool endsWithIgnoreCase(std::string_view text, std::string_view suffix) {
    if (text.size() < suffix.size()) return false;
    return CiEqual{}(text.substr(text.size() - suffix.size()), suffix);
}

}

F4SEEmitResult F4SEEmitter::emit(const PluginBinding& plugin,
                                 const F4SEEmitOptions* options) const {
    F4SEEmitOptions opts;
    if (options != nullptr) opts = *options;

    auto diagnostics = BindingValidator::validate(plugin);
    const bool hasError = std::any_of(
        diagnostics.begin(), diagnostics.end(), [](const Diagnostic& d) {
            return d.severity == DiagnosticSeverity::Error;
        });
    if (hasError && !opts.emitDespiteErrors) {
        F4SEEmitResult result;
        result.diagnostics = std::move(diagnostics);
        return result;
    }

    std::vector<std::string> structNames;
    for (const auto* declared : plugin.allStructs()) structNames.push_back(declared->name);

    CppEmitter cpp(structNames);
    PapyrusHeaderEmitter psc(&plugin);
    PluginEmitter shell;

    F4SEEmitResult result;
    result.diagnostics = std::move(diagnostics);

    const auto add = [&](std::string path, std::string text) {
        const auto found = opts.existing.find(path);
        if (found != opts.existing.end()) {
            for (const auto& name : RegionMerge::orphaned(text, found->second)) {
                result.orphanedBodies.push_back(path + ": " + name);
            }
            text = RegionMerge::merge(text, found->second);
        }
        EmittedFile file;
        file.relativePath = std::move(path);
        file.text = std::move(text);
        result.files.push_back(std::move(file));
    };

    for (const auto& module : plugin.modules) {
        add("src/" + CppEmitter::headerFileName(plugin, module), cpp.emitHeader(plugin, module));
        add("src/" + CppEmitter::sourceFileName(plugin, module), cpp.emitSource(plugin, module));
        add(PapyrusHeaderEmitter::scriptFileName(module), psc.emit(plugin, module));
    }

    add("src/" + plugin.name + "Registrations.h", cpp.emitRegistrationsHeader(plugin));
    add("src/" + plugin.name + "Registrations.cpp", cpp.emitRegistrationsSource(plugin));
    add("src/main.cpp", shell.emitMain(plugin));
    add("CMakeLists.txt", shell.emitCMakeLists(plugin));
    add("README.md", shell.emitReadme(plugin));

    return result;
}

CrossCheckResult F4SEEmitter::roundTrip(const PluginBinding& plugin,
                                        const F4SEEmitResult& result) {
    RegistrationExtractor extractor;

    std::vector<std::string> structNames;
    for (const auto* declared : plugin.allStructs()) structNames.push_back(declared->name);

    std::vector<NativeBinding> recovered;
    for (const auto& file : result.files) {
        if (!endsWithIgnoreCase(file.relativePath, ".cpp")) continue;
        auto schema = extractor.extract(file.text, file.relativePath, structNames);
        for (auto& native : schema.natives) recovered.push_back(std::move(native));
    }

    PapyrusHeaderEmitter psc(&plugin);
    std::vector<OracleNative> declared;
    for (const auto& module : plugin.modules) {
        auto natives = psc.declarationsOf(module);
        for (auto& native : natives) declared.push_back(std::move(native));
    }

    return CrossCheck::compare(recovered, declared);
}

}
