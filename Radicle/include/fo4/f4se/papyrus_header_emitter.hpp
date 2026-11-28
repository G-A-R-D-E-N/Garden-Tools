#pragma once

#include <map>
#include <string>
#include <vector>

#include <fo4/f4se/binding_model.hpp>
#include <fo4/f4se/native_oracle.hpp>

namespace fo4::f4se {

class PapyrusHeaderEmitter {
public:
    explicit PapyrusHeaderEmitter(const PluginBinding* plugin = nullptr);

    std::string emit(const PluginBinding& plugin, const ModuleBinding& module) const;

    static std::string headerLineFor(const ModuleBinding& module);

    std::string declarationFor(const NativeBinding& native, const ModuleBinding& module) const;

    std::string typeTextFor(const PapyrusTypeText& type, const ModuleBinding& module) const;

    static std::string scriptFileName(const ModuleBinding& module);

    std::vector<OracleNative> declarationsOf(const ModuleBinding& module) const;

private:
    std::map<std::string, std::string, CiLess> structOwners_;
};

}
