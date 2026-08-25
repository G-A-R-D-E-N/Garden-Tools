#pragma once

#include <string>

#include <fo4ide/f4se/BindingModel.hpp>

namespace fo4ide::f4se {

// Port of F4SEPluginEmitter: the plugin shell — main.cpp for both F4SE
// targets, the CMake build file and the README.
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

}  // namespace fo4ide::f4se
