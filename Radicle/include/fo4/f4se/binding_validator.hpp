#pragma once

#include <vector>

#include <fo4/f4se/binding_model.hpp>
#include <fo4/f4se/diagnostic.hpp>

namespace fo4::f4se {

class BindingValidator {
public:
    static constexpr int kMaximumArity = 10;

    static std::vector<Diagnostic> validate(const PluginBinding& plugin);

private:
    static void validateModules(const PluginBinding& plugin,
                                std::vector<Diagnostic>& problems);
    static void validateStructs(const PluginBinding& plugin,
                                const std::vector<const StructBinding*>& structs,
                                const F4SETypeMap& map,
                                std::vector<Diagnostic>& problems);
    static void validateNatives(const PluginBinding& plugin, const F4SETypeMap& map,
                                std::vector<Diagnostic>& problems);
    static bool isIdentifier(const std::string& text);
    static bool isScriptName(const std::string& text);
};

}
