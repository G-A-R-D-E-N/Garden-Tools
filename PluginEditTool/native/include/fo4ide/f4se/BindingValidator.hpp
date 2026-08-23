#pragma once

#include <vector>

#include <fo4ide/f4se/BindingModel.hpp>
#include <fo4ide/graph/GraphDiagnostic.hpp>

namespace fo4ide::f4se {

// Port of F4SEBindingValidator: the pre-emit validation pass producing
// GRA####-coded diagnostics.
class BindingValidator {
public:
    static constexpr int kMaximumArity = 10;

    static std::vector<graph::GraphDiagnostic> validate(const PluginBinding& plugin);

private:
    static void validateModules(const PluginBinding& plugin,
                                std::vector<graph::GraphDiagnostic>& problems);
    static void validateStructs(const PluginBinding& plugin,
                                const std::vector<const StructBinding*>& structs,
                                const F4SETypeMap& map,
                                std::vector<graph::GraphDiagnostic>& problems);
    static void validateNatives(const PluginBinding& plugin, const F4SETypeMap& map,
                                std::vector<graph::GraphDiagnostic>& problems);
    static bool isIdentifier(const std::string& text);
    static bool isScriptName(const std::string& text);
};

}  // namespace fo4ide::f4se
