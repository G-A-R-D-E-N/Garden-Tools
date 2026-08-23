#pragma once

#include <map>
#include <string>
#include <vector>

#include <fo4ide/f4se/BindingModel.hpp>
#include <fo4ide/f4se/NativeOracle.hpp>

namespace fo4ide::f4se {

// Port of F4SEPapyrusHeaderEmitter: the .psc each module's natives are
// declared in (structs first, then the natives owned by the module script).
class PapyrusHeaderEmitter {
public:
    explicit PapyrusHeaderEmitter(const PluginBinding* plugin = nullptr);

    std::string emit(const PluginBinding& plugin, const ModuleBinding& module) const;

    static std::string headerLineFor(const ModuleBinding& module);

    std::string declarationFor(const NativeBinding& native, const ModuleBinding& module) const;

    // Qualifies a struct type with its owning script when the owner is another
    // emitted script.
    std::string typeTextFor(const PapyrusTypeText& type, const ModuleBinding& module) const;

    static std::string scriptFileName(const ModuleBinding& module);

    std::vector<OracleNative> declarationsOf(const ModuleBinding& module) const;

private:
    std::map<std::string, std::string, CiLess> structOwners_;
};

}  // namespace fo4ide::f4se
