#pragma once

#include <string>

#include <fo4/f4se/binding_model.hpp>

namespace fo4::f4se {

class PluginEmitter {
public:
    static constexpr const char* kOgRuntimeVersion = "CURRENT_RELEASE_RUNTIME";

    std::string emitMain(const PluginBinding& plugin) const;
    std::string emitCMakeLists(const PluginBinding& plugin) const;
    std::string emitReadme(const PluginBinding& plugin) const;

private:
    std::string emitOgMain(const PluginBinding& plugin) const;
    std::string emitNgMain(const PluginBinding& plugin) const;
    static void appendCommonTop(std::string& text, const PluginBinding& plugin);
    static void appendLoad(std::string& text, const PluginBinding& plugin);
    static std::string versionLiteral(const PluginBinding& plugin);
};

}
